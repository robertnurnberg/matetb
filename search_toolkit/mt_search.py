#!/usr/bin/env python3
"""Search matetrackpv.epd for positions that matetb can solve.

For every candidate the restriction sets of mtlib.ladder() are tried, strictest first.
A candidate is solved if a tree completes within --max-nodes and proves matetrack's bm,
or a shorter mate (printed as SHORTER MATE; the proven mate then counts as the bm).

The output is JSON lines, one record per finished candidate, and the run is resumable:
solved candidates and candidates that already exceeded a cap >= --max-nodes are skipped.
Run it again with a larger --max-nodes to go deeper, e.g.

  mt_search.py --repo ~/matetb --matetrack matetrackpv.epd --max-nodes 5000000 --workers 16
  mt_search.py --repo ~/matetb --matetrack matetrackpv.epd --max-nodes 200000000 \\
               --workers 1 --threads 32 --max-davg 4
"""
import argparse
import concurrent.futures as cf
import json
import os
import sys
import time

import mtlib


def solve(repo, entry, features, cap, threads, timeout):
    epd = f"{entry['fen4']} bm #{entry['bm']};"
    rec = dict(fen4=entry["fen4"], bm=entry["bm"], cap=cap, tried=[])
    for argv in mtlib.ladder(features):
        r = mtlib.run_matetb(repo, epd, argv, cap, threads, timeout)
        rec["tried"].append(dict(argv=argv, status=r["status"], positions=r["positions"],
                                 mate=r["mate"]))
        if r["status"] != "ok":
            break  # weaker sets have bigger trees
        if r["mate"] == entry["bm"] or mtlib.shorter(r["mate"], entry["bm"]):
            rec.update(mate=r["mate"], argv=argv, positions=r["positions"], pv=r["pv"])
            break
    return rec


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", required=True, help="matetb checkout with built binaries")
    ap.add_argument("--matetrack", default="matetrackpv.epd")
    ap.add_argument("--out", default="results.jsonl")
    ap.add_argument("--max-nodes", type=int, required=True,
                    help="give up on a tree with more positions than this")
    ap.add_argument("--workers", type=int, default=os.cpu_count())
    ap.add_argument("--threads", type=int, default=1,
                    help=">1 runs matetb_threaded with this many threads per job")
    ap.add_argument("--timeout", type=float, help="seconds per matetb run")
    ap.add_argument("--min-bm", type=int, default=3)
    ap.add_argument("--max-bm", type=int, default=10**6)
    ap.add_argument("--max-davg", type=float, default=1e9,
                    help="skip candidates with more defender replies per move along the PV")
    ap.add_argument("--limit", type=int, default=0, help="stop after this many candidates")
    args = ap.parse_args()

    for exe in ("matetb", "matetb_threaded"):
        if not os.path.exists(os.path.join(args.repo, exe)):
            sys.exit(f"{exe} not found in {args.repo}; run make there first")
    in_repo = {e["fen4"] for e in mtlib.parse_epd(os.path.join(args.repo, "matetb.epd"))}
    done = {}
    if os.path.exists(args.out):
        for line in open(args.out):
            try:
                d = json.loads(line)
            except json.JSONDecodeError:  # a line cut off by an interrupted run
                continue
            old = done.get(d["fen4"])
            if old is None or "mate" in d or ("mate" not in old and d["cap"] >= old["cap"]):
                done[d["fen4"]] = d

    todo = []
    for e in mtlib.parse_epd(args.matetrack):
        d = done.get(e["fen4"])
        if (not args.min_bm <= abs(e["bm"]) <= args.max_bm or e["fen4"] in in_repo
                or (d and ("mate" in d or d["cap"] >= args.max_nodes))):
            continue
        f = mtlib.analyze(e)
        if f and f["d_avg"] <= args.max_davg:
            todo.append(((f["d_avg"], f["runners"], -abs(e["bm"])), e, f))
    todo.sort(key=lambda t: t[0])  # most constrained defender first
    if args.limit:
        todo = todo[:args.limit]
    est = args.workers * args.max_nodes * (200 if args.threads <= 1 else 400) / 1e9
    print(f"{len(todo)} candidates, {args.workers} workers x {args.threads} threads, "
          f"memory up to ~{est:.0f} GB", flush=True)

    t0, solved, shorter = time.time(), 0, 0
    with open(args.out, "a") as out, cf.ThreadPoolExecutor(args.workers) as pool:
        futures = [pool.submit(solve, args.repo, e, f, args.max_nodes, args.threads,
                               args.timeout) for _, e, f in todo]
        for n, fut in enumerate(cf.as_completed(futures), 1):
            rec = fut.result()
            out.write(json.dumps(rec) + "\n")
            out.flush()
            if "mate" in rec:
                solved += 1
                if rec["mate"] == rec["bm"]:
                    print(f"  SOLVED #{rec['bm']} with {rec['positions']} positions: "
                          f"{rec['fen4']}", flush=True)
                else:
                    shorter += 1
                    print(f"  *** SHORTER MATE: matetb proves #{rec['mate']}, matetrack "
                          f"has #{rec['bm']} ({rec['positions']} positions): "
                          f"{rec['fen4']} ***", flush=True)
            if n % 100 == 0:
                print(f"[{n}/{len(todo)}] {time.time() - t0:.0f}s, {solved} solved, "
                      f"{shorter} shorter", flush=True)
    print(f"done: {solved} solved ({shorter} with a shorter mate) of {len(todo)} "
          f"in {time.time() - t0:.0f}s")


if __name__ == "__main__":
    main()
