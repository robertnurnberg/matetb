#!/usr/bin/env python3
"""Summarise results.jsonl: what was solved, what needs a larger --max-nodes, and the
near misses (a restricted tree finished but proved a longer mate, or none)."""
import collections
import json
import sys


def flags(argv):
    return " ".join(a.replace("--exclude", "") for a in argv if a.startswith("--")) or "none"


recs = {}
for path in sys.argv[1:]:
    for line in open(path):
        d = json.loads(line)
        old = recs.get(d["fen4"])
        if old is None or "mate" in d or ("mate" not in old and d["cap"] >= old["cap"]):
            recs[d["fen4"]] = d
solved = [d for d in recs.values() if "mate" in d]
print(f"{len(recs)} candidates, {len(solved)} solved")
by = collections.Counter(abs(d["mate"]) // 10 * 10 for d in solved)
print("solved by mate length:", ", ".join(f"{k}s: {v}" for k, v in sorted(by.items())))
for d in solved:
    if d["mate"] != d["bm"]:
        print(f"  SHORTER MATE: {d['fen4']} matetrack #{d['bm']}, matetb #{d['mate']}")
big = [d for d in recs.values() if "mate" not in d and d["tried"][0]["status"] == "cap"]
near = [d for d in recs.values() if "mate" not in d and d["tried"][0]["status"] != "cap"]
print(f"{len(big)} exceed the cap even with the strictest flags (retry with a larger "
      f"--max-nodes); {len(near)} other unsolved:")
for d in sorted(near, key=lambda d: -abs(d["bm"]))[:15]:
    t = [x for x in d["tried"] if x["status"] == "ok"]
    last = t[-1] if t else d["tried"][-1]
    what = f"proved #{last['mate']}" if last["mate"] else "no mate"
    print(f"  bm #{d['bm']}: {what} with {last['positions']:,} positions "
          f"({flags(last['argv'])}); next weaker set too big  {d['fen4']}")
