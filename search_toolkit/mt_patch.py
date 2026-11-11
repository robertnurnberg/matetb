#!/usr/bin/env python3
"""Turn the solved candidates of mt_search.py into a patch for matetb.epd / options.hpp.

For every solved position the position one ply down its PV is added as well (bm -(N-1)
for a mating root), checked with the same flags. Positions already in matetb.epd and
duplicates are skipped. With --minimize, flags that do not shrink the tree are dropped.

Writes OUT_DIR/new_puzzles.patch (git apply it) and OUT_DIR/new.epd (the new lines).
Afterwards: git apply, make, and run your usual check on new.epd.
"""
import argparse
import collections
import concurrent.futures as cf
import json
import os
import re
import shutil
import subprocess
import tempfile

import chess

import mtlib

VALUE_FLAGS = ("--excludeFrom", "--excludePromotionTo", "--excludeAllowingFrom",
               "--excludeCapturesOf")
FLAGS = ["--excludeFrom", "--excludePromotionTo", "--excludeCaptures", "--excludeCapturesOf",
         "--excludeToCapturable", "--excludeAllowingCapture", "--excludeAllowingFrom"]


def split_flags(argv):
    out, i = [], 0
    while i < len(argv):
        n = 2 if argv[i] in VALUE_FLAGS else 1
        out.append(tuple(argv[i:i + n]))
        i += n
    return sorted(out, key=lambda f: FLAGS.index(f[0]))


def join_flags(flags):
    return [x for f in flags for x in f]


def key_bm(bm):
    return abs(bm), bm < 0


def minimize(repo, rec, threads):
    """Drop every flag whose removal keeps the proven mate and does not grow the tree."""
    epd = f"{rec['fen4']} bm #{rec['mate']};"
    flags = split_flags(rec["argv"])
    for f in list(flags):
        trial = [x for x in flags if x != f]
        r = mtlib.run_matetb(repo, epd, join_flags(trial), rec["positions"] + 1, threads)
        if r["status"] == "ok" and r["mate"] == rec["mate"] and r["positions"] <= rec["positions"]:
            flags = trial
    return join_flags(flags)


def block(argv, items):
    """The options.hpp text for one set of flags and the positions using it."""
    items = sorted(items, key=lambda x: key_bm(x[1]))
    code = [("  } else if (" if i == 0 else "             ") + f'epd == "{f4}"'
            + (") {" if i == len(items) - 1 else " ||") for i, (f4, _) in enumerate(items)]
    width = max(len(c) for c in code)
    text = [c.ljust(width) + f" // bm #{items[i][1]}" for i, c in enumerate(code)]
    for flag in split_flags(argv):
        name = flag[0][2:]
        text.append(f"    {name} = true;" if len(flag) == 1 else f'    {name} = "{flag[1]}";')
    return max(abs(bm) for _, bm in items), text


def edit_repo(path, groups):
    """Insert the new lines into matetb.epd and the new blocks into options.hpp."""
    p = os.path.join(path, "matetb.epd")
    lines = open(p).read().split("\n")[:-1]
    new = sorted(((key_bm(bm), f"{f4} bm #{bm};") for _, items in groups for f4, bm in items))
    key = lambda l: key_bm(int(re.search(r"bm #(-?\d+)", l).group(1)))
    out, i = [], 0
    for l in lines:
        while i < len(new) and new[i][0] < key(l):
            out.append(new[i][1])
            i += 1
        out.append(l)
    open(p, "w").write("\n".join(out + [n[1] for n in new[i:]]) + "\n")

    p = os.path.join(path, "options.hpp")
    H = open(p).read().split("\n")
    starts = [i for i, l in enumerate(H) if re.match(r"^  (\} else )?if \(epd == ", l)]
    end = next(i for i, l in enumerate(H) if l == "  // clang-format on") - 1
    bounds = starts + [end]
    # existing blocks are ordered by their largest |bm|
    largest = [max(abs(int(x)) for x in re.findall(r"// bm #(-?\d+)",
                                                   " ".join(H[bounds[j]:bounds[j + 1]])))
               for j in range(len(starts))]
    blocks = sorted(block(argv, items) for argv, items in groups if argv)
    out, i = H[:starts[1]], 0
    for j in range(1, len(starts)):
        while i < len(blocks) and blocks[i][0] < largest[j]:
            out += blocks[i][1]
            i += 1
        out += H[bounds[j]:bounds[j + 1]]
    for _, text in blocks[i:]:
        out += text
    open(p, "w").write("\n".join(out + H[end:]))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", required=True)
    ap.add_argument("--matetrack", default="matetrackpv.epd")
    ap.add_argument("--results", nargs="+", required=True)
    ap.add_argument("--out-dir", default="patch_out")
    ap.add_argument("--workers", type=int, default=os.cpu_count())
    ap.add_argument("--threads", type=int, default=1, help="threads per matetb run")
    ap.add_argument("--min-bm", type=int, default=3)
    ap.add_argument("--minimize", action="store_true")
    args = ap.parse_args()
    os.makedirs(args.out_dir, exist_ok=True)

    matetrack = {e["fen4"]: e for e in mtlib.parse_epd(args.matetrack)}
    have = {e["fen4"] for e in mtlib.parse_epd(os.path.join(args.repo, "matetb.epd"))}
    roots = {}
    for path in args.results:
        for line in open(path):
            d = json.loads(line)
            if "mate" in d and abs(d["mate"]) >= args.min_bm and d["fen4"] not in have:
                roots[d["fen4"]] = d
    roots = sorted(roots.values(), key=lambda d: key_bm(d["mate"]))
    print(f"{len(roots)} solved positions not yet in the repo")
    if args.minimize:
        with cf.ThreadPoolExecutor(args.workers) as pool:
            for d, argv in zip(roots, pool.map(lambda d: minimize(args.repo, d, args.threads),
                                               roots)):
                d["argv"] = argv

    entries = collections.OrderedDict(((d["fen4"], d["mate"]), d["argv"]) for d in roots)
    children = []
    for d in roots:
        if d["mate"] == 1:
            continue
        # matetrack's PV belongs to matetrack's bm; for a shorter mate use matetb's PV
        pv = matetrack[d["fen4"]]["pv"] if d["mate"] == d["bm"] else d["pv"]
        board = chess.Board(d["fen4"] + " 0 1")
        board.push_uci(pv[0])
        c4 = " ".join(board.fen().split()[:4])
        cbm = -(d["mate"] - 1) if d["mate"] > 0 else -d["mate"]
        if c4 not in have and (c4, cbm) not in entries and abs(cbm) >= args.min_bm - 1:
            entries[(c4, cbm)] = None  # reserve, so duplicates are dropped
            children.append((c4, cbm, d))

    def check(c):
        c4, cbm, d = c
        return mtlib.run_matetb(args.repo, f"{c4} bm #{cbm};", d["argv"],
                                2 * d["positions"] + 1000, args.threads)
    with cf.ThreadPoolExecutor(args.workers) as pool:
        for (c4, cbm, d), r in zip(children, pool.map(check, children)):
            if r["status"] == "ok" and r["mate"] == cbm:
                entries[(c4, cbm)] = d["argv"]
            else:
                del entries[(c4, cbm)]
                print(f"  child not solved with the root's flags: {c4} bm #{cbm}")

    by_flags = collections.OrderedDict()
    for (f4, bm), argv in entries.items():
        by_flags.setdefault(tuple(argv), []).append((f4, bm))
    groups = [(list(argv), items) for argv, items in by_flags.items()]
    lines = sorted((f"{f4} bm #{bm};" for _, items in groups for f4, bm in items),
                   key=lambda l: key_bm(int(re.search(r"#(-?\d+)", l).group(1))))
    print(f"{len(lines)} new lines in {len(groups)} blocks")
    for d in roots:
        if d["mate"] != d["bm"]:
            print(f"  note: {d['fen4']} added as bm #{d['mate']} (matetrack: #{d['bm']})")

    tmp = tempfile.mkdtemp()
    for side in "ab":
        os.makedirs(os.path.join(tmp, side))
        for name in ("matetb.epd", "options.hpp"):
            shutil.copy(os.path.join(args.repo, name), os.path.join(tmp, side, name))
    edit_repo(os.path.join(tmp, "b"), groups)
    diff = subprocess.run(["git", "diff", "--no-index", "--patience", "a", "b"], cwd=tmp,
                          capture_output=True, text=True).stdout
    # git keeps the directory names a/ and b/ in the paths: strip them (headers only)
    diff = re.sub(r"^(diff --git a/)a/(\S+) b/b/", r"\1\2 b/", diff, flags=re.M)
    diff = re.sub(r"^(--- a/)a/|^(\+\+\+ b/)b/", lambda m: m.group(1) or m.group(2), diff,
                  flags=re.M)
    open(os.path.join(args.out_dir, "new_puzzles.patch"), "w").write(diff)
    open(os.path.join(args.out_dir, "new.epd"), "w").write("\n".join(lines) + "\n")
    shutil.rmtree(tmp)
    print(f"wrote {args.out_dir}/new_puzzles.patch and new.epd")


if __name__ == "__main__":
    main()
