#!/usr/bin/env python3
"""Summarise a results.jsonl produced by mt_search.py.

Prints how many candidates were solved (by mate length), how many exceeded the cap
even with the most restrictive flags (retry with a larger --max-nodes), and the
"near misses": a restricted tree finished but proved a longer mate than matetrack's
bm (or none). For those, the ladder's flags were too strong while the next, weaker
set was too big; hand-tuned flags may still solve them.
"""
import argparse
import collections
import json


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("results", nargs="+")
    ap.add_argument("--top", type=int, default=15)
    args = ap.parse_args()
    recs = {}
    for path in args.results:
        for l in open(path):
            d = json.loads(l)
            p = recs.get(d["fen4"])
            if p is None or "solved" in d or ("solved" not in p and d["cap"] >= p["cap"]):
                recs[d["fen4"]] = d
    solved = [d for d in recs.values() if "solved" in d]
    print(f"{len(recs)} candidates, {len(solved)} solved")
    by = collections.Counter(min(abs(d["bm"]) // 10 * 10, 100) for d in solved)
    print("solved by |bm| decade:", ", ".join(f"{k}+:{v}" for k, v in sorted(by.items())))
    if solved:
        sizes = sorted(d["solved"]["positions"] for d in solved)
        print(f"tree sizes of solved: median {sizes[len(sizes)//2]:,}, max {sizes[-1]:,}")
    capped0, near, other = [], [], []
    for d in recs.values():
        if "solved" in d:
            continue
        t = d["tried"]
        if t and t[0]["status"] == "cap":
            capped0.append(d)
        elif t and any(x["status"] == "ok" for x in t):
            near.append(d)
        else:
            other.append(d)
    caps = collections.Counter(d["cap"] for d in capped0)
    print(f"unsolved: {len(capped0)} exceed the cap even with the most restrictive flags "
          f"(caps used: {dict(caps)}) -> retry with a larger --max-nodes")
    print(f"          {len(near)} near misses (a restricted tree completed with the wrong mate)")
    print(f"          {len(other)} other (timeouts/errors)")
    near.sort(key=lambda d: min((abs(x["mate"] - abs(d["bm"])) if x.get("mate") else 99)
                                for x in d["tried"] if x["status"] == "ok"))
    for d in near[:args.top]:
        ok = [x for x in d["tried"] if x["status"] == "ok"][-1]
        proved = f"mate in {ok['mate']}" if ok["mate"] else "no mate"
        print(f"  bm #{d['bm']:>4}  {proved:>11} with {ok['positions']:>9,} pos "
              f"({'+'.join(ok['keys']) or 'no flags'}); weaker set too big  {d['fen4']}")


if __name__ == "__main__":
    main()
