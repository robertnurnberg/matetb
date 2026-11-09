#!/usr/bin/env python3
"""Search matetrackpv.epd for positions that matetb can solve.

For each candidate a ladder of restriction sets is derived from its PV (see
mtlib.ladder) and tried from the most to the least restrictive. A candidate is
*solved* if some restricted tree completes within --max-nodes and proves
exactly the matetrack mate length.

The results file is JSON lines and resumable. Run the script repeatedly with
increasing --max-nodes: solved candidates are never revisited, candidates
that already exceeded an equal or larger cap are skipped, the rest are retried.

Example (pass 1 / pass 2 / pass 3 on a big machine):
  mt_search.py --repo ~/matetb --matetrack matetrackpv.epd --max-nodes 200000 --workers 32
  mt_search.py ... --max-nodes 10000000 --workers 12
  mt_search.py ... --max-nodes 500000000 --workers 1 --threads 32
"""
import argparse
import concurrent.futures as cf
import json
import os
import sys
import time

import mtlib


def load_done(path):
    done = {}
    if os.path.exists(path):
        for l in open(path):
            try:
                d = json.loads(l)
            except json.JSONDecodeError:
                continue
            prev = done.get(d["fen4"])
            # keep the most informative record: solved > larger cap
            if prev is None or "solved" in d or (
                    "solved" not in prev and d.get("cap", 0) >= prev.get("cap", 0)):
                done[d["fen4"]] = d
    return done


def solve_candidate(repo, entry, feats, cap, threads, timeout):
    epd = f"{entry['fen4']} bm #{entry['bm']};"
    rec = dict(fen4=entry["fen4"], bm=entry["bm"], cap=cap, tried=[])
    for keys, argv in mtlib.ladder(feats):
        r = mtlib.run_matetb(repo, epd, argv, cap, threads, timeout)
        rec["tried"].append(dict(keys=keys, argv=argv, **{
            k: r[k] for k in ("status", "positions", "mate", "secs")}))
        if r["status"] != "ok":
            break  # all weaker sets contain this tree: they can only be larger
        if r["mate"] == entry["bm"]:
            rec["solved"] = dict(keys=keys, argv=argv, positions=r["positions"])
            break
    return rec


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", required=True,
                    help="matetb checkout containing built matetb / matetb_threaded "
                         "(upstream main with --maxNodes)")
    ap.add_argument("--matetrack", default="matetrackpv.epd")
    ap.add_argument("--out", default="results.jsonl")
    ap.add_argument("--max-nodes", "--max-positions", dest="max_positions", type=int,
                    required=True, help="abort a tree above this many positions (--maxNodes)")
    ap.add_argument("--workers", type=int, default=os.cpu_count())
    ap.add_argument("--threads", type=int, default=1,
                    help=">1 runs matetb_threaded with that concurrency per job")
    ap.add_argument("--timeout", type=float, default=None,
                    help="wall-clock limit in seconds per matetb run")
    ap.add_argument("--min-bm", type=int, default=3, help="min |bm| to consider")
    ap.add_argument("--max-bm", type=int, default=10**6)
    ap.add_argument("--max-davg", type=float, default=1e9,
                    help="skip candidates whose mean defender mobility along the PV "
                         "is larger (cheap pre-filter for big caps)")
    ap.add_argument("--limit", type=int, default=0, help="process at most N candidates")
    ap.add_argument("--include-existing", action="store_true",
                    help="do not skip positions already in matetb.epd/options.hpp")
    args = ap.parse_args()

    for exe in ("matetb", "matetb_threaded"):
        if not os.path.exists(os.path.join(args.repo, exe)):
            sys.exit(f"{exe} not found in {args.repo}; run 'make' there first")
    have, keyed = mtlib.existing_keys(args.repo)
    have4 = {f for f, _ in have}
    done = load_done(args.out)

    todo = []
    for e in mtlib.parse_epd(args.matetrack):
        if not (args.min_bm <= abs(e["bm"]) <= args.max_bm):
            continue
        if not args.include_existing and (
                (e["fen4"], e["bm"]) in have or e["fen4"] in keyed or e["fen4"] in have4):
            continue
        d = done.get(e["fen4"])
        if d is not None and ("solved" in d or d.get("cap", 0) >= args.max_positions):
            continue
        f = mtlib.analyze(e)
        if f is None or f["d_avg"] > args.max_davg:
            continue
        todo.append((mtlib.promise_key(f, e["bm"]), e, f))
    todo.sort(key=lambda t: t[0])
    if args.limit:
        todo = todo[:args.limit]
    mem = mtlib.mem_available_gb()
    # measured peak: ~180 B/position (matetb), ~350 B/position (matetb_threaded)
    est = args.workers * args.max_positions * (200 if args.threads <= 1 else 400) / 1e9
    print(f"{len(todo)} candidates; {args.workers} workers x {args.threads} threads; "
          f"worst-case memory ~{est:.1f} GB" + (f" (available {mem:.1f} GB)" if mem else ""),
          flush=True)
    if mem and est > mem:
        print("WARNING: workers*max-nodes may exceed available memory; "
              "reduce --workers or --max-nodes", flush=True)

    t0, nsolved = time.time(), 0
    with open(args.out, "a") as fo, cf.ThreadPoolExecutor(args.workers) as ex:
        futs = {ex.submit(solve_candidate, args.repo, e, f, args.max_positions,
                          args.threads, args.timeout): e for _, e, f in todo}
        for n, fut in enumerate(cf.as_completed(futs), 1):
            rec = fut.result()
            fo.write(json.dumps(rec) + "\n")
            fo.flush()
            if "solved" in rec:
                nsolved += 1
                print(f"  SOLVED #{rec['bm']:>4} {rec['solved']['positions']:>10} pos  "
                      f"{rec['fen4']}", flush=True)
            if n % 200 == 0:
                print(f"[{n}/{len(todo)}] {time.time()-t0:.0f}s, {nsolved} solved so far",
                      flush=True)
    print(f"done: {nsolved} solved of {len(todo)} in {time.time()-t0:.0f}s")


if __name__ == "__main__":
    main()
