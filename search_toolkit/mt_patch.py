#!/usr/bin/env python3
"""Turn solved candidates (from mt_search.py) into a patch for matetb.

Steps
  1. re-verify every solved root with the real binary,
  2. optionally minimise its flags (--minimize): a flag is dropped if the tree
     stays exactly as large (or at most --tolerance larger) and the proven mate
     is unchanged,
  3. add the position one ply down the PV (bm -(N-1) for a mating root) and
     verify it with the same flags,
  4. drop duplicates (against the repo and within the new set),
  5. insert lines into matetb.epd (sorted by |bm|, positive first) and one block
     per distinct flag set into options.hpp (blocks sorted by their largest |bm|),
  6. write <out-dir>/new_puzzles.patch, new.epd and report.txt,
  7. with --final-check apply the patch to a scratch copy, build it and run all
     new lines through matetb_threaded exactly as check.sh would.
"""
import argparse
import collections
import concurrent.futures as cf
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

import chess

import mtlib

VALUE_FLAGS = {"--excludeFrom", "--excludePromotionTo", "--excludeAllowingFrom",
               "--excludeCapturesOf"}
ASSIGN = {  # flag -> (C++ member, is_bool)
    "--excludeFrom": ("excludeFrom", False),
    "--excludePromotionTo": ("excludePromotionTo", False),
    "--excludeCaptures": ("excludeCaptures", True),
    "--excludeCapturesOf": ("excludeCapturesOf", False),
    "--excludeToAttacked": ("excludeToAttacked", True),
    "--excludeToCapturable": ("excludeToCapturable", True),
    "--excludeAllowingCapture": ("excludeAllowingCapture", True),
    "--excludeAllowingFrom": ("excludeAllowingFrom", False),
}
FLAG_ORDER = list(ASSIGN)


def split_flags(argv):
    out, i = [], 0
    while i < len(argv):
        if argv[i] in VALUE_FLAGS:
            out.append((argv[i], argv[i + 1]))
            i += 2
        else:
            out.append((argv[i], None))
            i += 1
    return out


def join_flags(flags):
    flags = sorted(flags, key=lambda t: FLAG_ORDER.index(t[0]))
    argv = []
    for k, v in flags:
        argv.append(k)
        if v is not None:
            argv.append(v)
    return argv


def key_bm(bm):
    return (abs(bm), bm < 0)


# --------------------------------------------------------------------------
def verify_root(repo, rec, threads):
    s = rec["solved"]
    bm = mtlib.solved_bm(rec)
    epd = f"{rec['fen4']} bm #{bm};"
    cap = int(s["positions"] * 2 + 1000)
    r = mtlib.run_matetb(repo, epd, s["argv"], cap, threads)
    if r["status"] == "ok" and r["mate"] == bm and rec.get("improved"):
        s["pv"] = r["pv"]  # matetrack's PV belongs to the longer mate
    return r["status"] == "ok" and r["mate"] == bm, r


def minimize(repo, rec, threads, tol, squares):
    """Greedy removal of flags (and optionally single squares) that do not matter."""
    bm = mtlib.solved_bm(rec)
    epd = f"{rec['fen4']} bm #{bm};"
    flags = split_flags(rec["solved"]["argv"])
    base = rec["solved"]["positions"]
    limit = int(base * (1 + tol)) + 1

    def ok(fl):
        r = mtlib.run_matetb(repo, epd, join_flags(fl), limit + 1, threads)
        return r["status"] == "ok" and r["mate"] == bm and r["positions"] <= limit

    changed = True
    while changed:
        changed = False
        for f in list(flags):
            trial = [x for x in flags if x != f]
            if ok(trial):
                flags, changed = trial, True
    if squares:
        for idx, (k, v) in enumerate(list(flags)):
            if v is None or k not in ("--excludeFrom", "--excludeAllowingFrom"):
                continue
            for s in v.split():
                cur = dict(flags)[k].split()
                if len(cur) <= 1 or s not in cur:
                    continue
                trial = [(kk, (" ".join(x for x in cur if x != s) if kk == k else vv))
                         for kk, vv in flags]
                if ok(trial):
                    flags = trial
    return join_flags(flags)


# --------------------------------------------------------------------------
def assigns(argv):
    lines = []
    for k, v in sorted(split_flags(argv), key=lambda t: FLAG_ORDER.index(t[0])):
        name, is_bool = ASSIGN[k]
        lines.append(f"{name} = true;" if is_bool else f'{name} = "{v}";')
    return lines


def make_block(group):
    items = sorted(group["items"], key=lambda x: key_bm(x[1]))
    codes = []
    for i, (f4, bm) in enumerate(items):
        pre = "  } else if (" if i == 0 else "             "
        codes.append(pre + f'epd == "{f4}"' + (") {" if i == len(items) - 1 else " ||"))
    w = max(len(c) for c in codes)
    txt = [c.ljust(w) + f" // bm #{items[i][1]}" for i, c in enumerate(codes)]
    txt += ["    " + a for a in assigns(group["argv"])]
    return max(abs(b) for _, b in items), txt


def edit_files(repo_copy, groups):
    # ---- matetb.epd
    p = os.path.join(repo_copy, "matetb.epd")
    lines = open(p).read().split("\n")
    assert lines[-1] == ""
    lines = lines[:-1]
    new = sorted(((key_bm(bm), f"{f4} bm #{bm};")
                  for g in groups for f4, bm in g["items"]), key=lambda x: x[0])

    def lk(l):
        return key_bm(int(re.search(r"bm #(-?\d+)", l).group(1)))

    out, ni = [], 0
    for l in lines:
        while ni < len(new) and new[ni][0] < lk(l):
            out.append(new[ni][1])
            ni += 1
        out.append(l)
    out += [n[1] for n in new[ni:]]
    open(p, "w").write("\n".join(out) + "\n")
    # ---- options.hpp
    p = os.path.join(repo_copy, "options.hpp")
    H = open(p).read().split("\n")
    starts = [i for i, l in enumerate(H) if re.match(r"^  (\} else )?if \(epd == ", l)]
    endi = next(i for i, l in enumerate(H) if l == "  // clang-format on") - 1
    assert H[endi] == "  }", "unexpected options.hpp layout"
    bounds = starts + [endi]
    fk = [max(abs(int(x)) for x in re.findall(r"// bm #(-?\d+)",
                                              " ".join(H[starts[j]:bounds[j + 1]])))
          for j in range(len(starts))]
    newb = sorted((make_block(g) for g in groups if g["argv"]), key=lambda x: x[0])
    out, ni = H[:starts[1]], 0
    for j in range(1, len(starts)):
        while ni < len(newb) and newb[ni][0] < fk[j]:
            out += newb[ni][1]
            ni += 1
        out += H[starts[j]:bounds[j + 1]]
    for _, t in newb[ni:]:
        out += t
    out += H[endi:]
    open(p, "w").write("\n".join(out))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", required=True, help="matetb checkout (clean, built)")
    ap.add_argument("--matetrack", default="matetrackpv.epd")
    ap.add_argument("--results", nargs="+", required=True, help="mt_search.py outputs")
    ap.add_argument("--out-dir", default="patch_out")
    ap.add_argument("--workers", type=int, default=os.cpu_count())
    ap.add_argument("--threads", type=int, default=1,
                    help="threads per verification run (use >1 for huge trees)")
    ap.add_argument("--min-bm", type=int, default=3)
    ap.add_argument("--minimize", action="store_true")
    ap.add_argument("--minimize-squares", action="store_true",
                    help="also try to shorten excludeFrom/excludeAllowingFrom lists")
    ap.add_argument("--tolerance", type=float, default=0.0,
                    help="relative tree growth accepted when dropping a flag")
    ap.add_argument("--no-children", action="store_true")
    ap.add_argument("--final-check", action="store_true")
    args = ap.parse_args()
    os.makedirs(args.out_dir, exist_ok=True)

    mt = {e["fen4"]: e for e in mtlib.parse_epd(args.matetrack)}
    have, keyed = mtlib.existing_keys(args.repo)
    have4 = {f for f, _ in have}
    solved = {}
    for path in args.results:
        for l in open(path):
            d = json.loads(l)
            mtlib.promote_improved(d)
            if "solved" in d and abs(mtlib.solved_bm(d)) >= args.min_bm:
                solved[d["fen4"]] = d
    roots = [d for f, d in solved.items()
             if (f, mtlib.solved_bm(d)) not in have and f not in keyed and f not in have4]
    print(f"{len(solved)} solved records, {len(roots)} not yet in the repo")

    report = []
    with cf.ThreadPoolExecutor(args.workers) as ex:
        res = list(ex.map(lambda d: verify_root(args.repo, d, args.threads), roots))
    good = []
    for d, (ok, r) in zip(roots, res):
        if ok:
            good.append(d)
        else:
            report.append(f"VERIFY FAILED root {d['fen4']} bm #{mtlib.solved_bm(d)}: {r}")
    print(f"{len(good)} roots verified")
    for d in good:
        if d.get("improved"):
            msg = (f"SHORTER MATE: {d['fen4']}  matetrack bm #{d['bm']}, "
                   f"matetb proves #{mtlib.solved_bm(d)} (added with the shorter bm)")
            report.append(msg)
            print("  *** " + msg + " ***")
    if args.minimize:
        with cf.ThreadPoolExecutor(args.workers) as ex:
            new_argv = list(ex.map(lambda d: minimize(
                args.repo, d, args.threads, args.tolerance, args.minimize_squares), good))
        nflags = sum(len(split_flags(d["solved"]["argv"])) for d in good)
        for d, a in zip(good, new_argv):
            d["solved"]["argv"] = a
        print(f"minimised: {nflags} -> "
              f"{sum(len(split_flags(d['solved']['argv'])) for d in good)} flags")

    # ---- children
    entries = collections.OrderedDict()   # (fen4, bm) -> argv
    for d in sorted(good, key=lambda d: (abs(mtlib.solved_bm(d)), mtlib.solved_bm(d) < 0)):
        entries.setdefault((d["fen4"], mtlib.solved_bm(d)), tuple(d["solved"]["argv"]))
    if not args.no_children:
        jobs = []
        for d in good:
            e = mt[d["fen4"]]
            bm = mtlib.solved_bm(d)
            b = chess.Board(e["fen4"] + " 0 1")
            if abs(bm) < 2 and bm > 0:
                continue
            # for a shorter mate than matetrack's, follow matetb's own PV
            pv = (d["solved"].get("pv") if d.get("improved") else None) or e["pv"]
            b.push(chess.Move.from_uci(pv[0]))
            cbm = -(bm - 1) if bm > 0 else -bm
            c4 = " ".join(b.fen().split()[:4])
            if abs(cbm) < args.min_bm - 1 or (c4, cbm) in have or c4 in keyed:
                continue
            if (c4, cbm) in entries:
                report.append(f"duplicate child dropped: {c4} bm #{cbm}")
                continue
            entries[(c4, cbm)] = None  # reserve (first come, first served)
            jobs.append((c4, cbm, d))

        def vchild(j):
            c4, cbm, d = j
            cap = int(d["solved"]["positions"] * 2 + 1000)
            return mtlib.run_matetb(args.repo, f"{c4} bm #{cbm};", d["solved"]["argv"],
                                    cap, args.threads)
        with cf.ThreadPoolExecutor(args.workers) as ex:
            cres = list(ex.map(vchild, jobs))
        nbad = 0
        for (c4, cbm, d), r in zip(jobs, cres):
            if r["status"] == "ok" and r["mate"] == cbm:
                entries[(c4, cbm)] = tuple(d["solved"]["argv"])
            else:
                del entries[(c4, cbm)]
                nbad += 1
                report.append(f"child not solved with root's flags: {c4} bm #{cbm} {r}")
        print(f"{len(jobs) - nbad} children verified, {nbad} rejected")

    # ---- groups by identical flag set
    groups = collections.OrderedDict()
    for (f4, bm), argv in entries.items():
        if argv is None:
            continue
        groups.setdefault(argv, dict(argv=list(argv), items=[]))["items"].append((f4, bm))
    groups = list(groups.values())
    nlines = sum(len(g["items"]) for g in groups)
    print(f"{nlines} lines in {len(groups)} blocks")
    if not groups:
        print("nothing to do")
        return

    # ---- build patch
    tmp = tempfile.mkdtemp()
    for side in ("a", "b"):
        os.makedirs(os.path.join(tmp, side))
        for fn in ("matetb.epd", "options.hpp"):
            shutil.copy(os.path.join(args.repo, fn), os.path.join(tmp, side, fn))
    edit_files(os.path.join(tmp, "b"), groups)
    p = subprocess.run(["git", "diff", "--no-index", "--patience", "a", "b"], cwd=tmp,
                       capture_output=True, text=True)
    # git prints the directory names a/ and b/ as part of the path: strip them once
    patch = re.sub(r"^(diff --git a/)a/(\S+) b/b/", r"\1\2 b/", p.stdout, flags=re.M)
    patch = re.sub(r"^--- a/a/", "--- a/", patch, flags=re.M)
    patch = re.sub(r"^\+\+\+ b/b/", "+++ b/", patch, flags=re.M)
    open(os.path.join(args.out_dir, "new_puzzles.patch"), "w").write(patch)
    new_lines = [f"{f4} bm #{bm};" for g in groups for f4, bm in g["items"]]
    new_lines.sort(key=lambda l: key_bm(int(re.search(r"#(-?\d+)", l).group(1))))
    open(os.path.join(args.out_dir, "new.epd"), "w").write("\n".join(new_lines) + "\n")
    open(os.path.join(args.out_dir, "report.txt"), "w").write("\n".join(report) + "\n")
    dels = sum(1 for l in patch.split("\n") if l.startswith("-") and not l.startswith("---"))
    print(f"wrote {args.out_dir}/new_puzzles.patch ({dels} deleted lines), new.epd, report.txt")

    if args.final_check:
        work = os.path.join(tmp, "check")
        shutil.copytree(args.repo, work, ignore=shutil.ignore_patterns(".git"))
        subprocess.run(["git", "apply", os.path.abspath(
            os.path.join(args.out_dir, "new_puzzles.patch"))], cwd=work, check=True)
        subprocess.run(["make", "-j"], cwd=work, check=True, capture_output=True)
        bad = 0

        def chk(line):
            epd = line.split(";")[0]
            out = subprocess.run([os.path.join(work, "matetb_threaded"), "--epd", epd],
                                 capture_output=True, text=True).stdout.replace("\r", "\n")
            m = re.search(r"Matetrack:\n([^;\n]*)", out)
            return (m and m.group(1)) == epd, epd
        with cf.ThreadPoolExecutor(max(1, args.workers // 4)) as ex:
            for ok, epd in ex.map(chk, new_lines):
                if not ok:
                    bad += 1
                    print("  NO MATCH:", epd)
        print(f"final check: {len(new_lines) - bad}/{len(new_lines)} lines match")
        if bad:
            sys.exit(1)
    shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
