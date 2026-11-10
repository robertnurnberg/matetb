# matetb search toolkit

Finds positions in `matetrackpv.epd` that `matetb` can solve with PV-derived restrictions, and
turns them into a patch for `matetb.epd` / `options.hpp` (including the position one ply down
each PV). It drives the **real** `matetb` / `matetb_threaded` binaries via `--maxNodes`.

Files: `mtlib.py` (PV analysis, restriction ladder, runner), `mt_search.py` (search),
`mt_stats.py` (diagnostics), `mt_patch.py` (verification + patch). Python 3.9+, `pip install chess`.

## Quick start

```
# 1. search; repeat with growing caps. Results accumulate in results.jsonl.
python3 mt_search.py --repo ~/git/matetb --matetrack ~/git/matetrack/matetrackpv.epd --out results.jsonl --max-nodes 1000000 --workers 32 --min-bm 10
python3 mt_stats.py results.jsonl                # what is left, what is a near miss

# 2. build a patch from everything solved so far
python3 mt_patch.py --repo ~/matetb --matetrack matetrackpv.epd --results results.jsonl \
        --out-dir out --minimize --final-check
cd ~/matetb && git apply out/new_puzzles.patch && git diff --stat
```

## Choosing caps on a 128 GB machine

Measured peak memory: about 180 B/position for `matetb`, about 350 B/position for `matetb_threaded`
(rehashing adds a transient bit more). Budget `workers x max-nodes x 200 B`, or `x 400 B` with
`--threads`; `mt_search.py` prints the estimate and warns when it exceeds free memory.
Speed was ~50k positions/s per core on my sandbox; a tree of N positions also needs several
passes in `generate_tb` (roughly one per ply of mate length), so very deep mates cost more.

Suggested ladder of passes (each pass only retries what exceeded the previous cap):

| pass | `--max-nodes` | `--workers` x `--threads` | notes |
|---|---|---|---|
| 1 | 200,000 | all cores x 1 | seconds to minutes; finds all the easy ones |
| 2 | 5,000,000 | cores/2 x 1 | about 1 GB per worker |
| 3 | 100,000,000 | 1 x all cores | about 35 GB, use `--threads` |
| 4 | 300,000,000 | 1 x all cores | about 100 GB; use `--max-davg 3 --min-bm 20` to focus |

`--max-davg D` skips candidates whose average number of defender replies along the PV is above D
(the strongest cheap predictor: solved entries have a median of about 2.4, others about 6).
`--min-bm` / `--max-bm` restrict the mate length. `--timeout` caps wall-clock per run.

## How candidates are tried

For each candidate the restriction sets (`mtlib.ladder`) are derived from its PV, strongest first:

| flag | used when the PV is compatible |
|---|---|
| `--excludeFrom` | mating pieces that never move in the PV |
| `--excludePromotionTo qrbn` | no mating promotion in the PV and some mating pawn is not frozen |
| `--excludeCaptures` / `--excludeCapturesOf` | mating side never captures / never captures those piece types |
| `--excludeAllowingCapture` | the defender never has a capture after a mating move (implies "to attacked") |
| `--excludeToAttacked` / `--excludeToCapturable` | only when the previous flag does not apply |
| `--excludeAllowingFrom` | defender pieces with no legal move anywhere along the PV |

Then flags are dropped from the end of this list, ending with only `--excludeFrom`/promotion, then
none. A candidate is **solved** when some tree completes within the cap and proves exactly
matetrack's `bm`. If even the strictest tree exceeds the cap, weaker ones are supersets and are
skipped. `mt_stats.py` splits the unsolved ones into "exceeds the cap" (try a larger cap) and
"near misses" (a restricted tree finished but proved a longer mate, or none: the flags were too
strong and the weaker set too big; hand-tune `--excludeTo`, `--excludeMoves`, `--openingMoves`...).

## Shorter mates than matetrack's bm

If a restricted tree proves a mate that is *shorter* than matetrack's `bm` (same side mating), this
counts as solved. `mt_search.py` prints it as `*** SHORTER MATE: matetb proves #N (matetrack bm #M) ***`,
the record in `results.jsonl` gets `"improved": true` and the proven mate, and `mt_patch.py` adds the
position with the proven mate as its `bm` (epd line, `options.hpp` block comment, and the child
position, which follows matetb's own PV since matetrack's PV belongs to the longer mate). Such cases
are also listed in `mt_stats.py` and `report.txt`; please report them to the matetrack maintainers.
Result files from older runs are handled too: such records, which were stored as unsolved, are
promoted automatically when the file is read, so resuming a run does not lose them.
Note the ladder stops at the first (strictest) tree that proves a mate no longer than `bm`; a weaker
flag set might prove an even shorter one.

## mt_patch.py

* re-verifies every solve with the real binary;
* `--minimize` drops every flag whose removal leaves the tree no larger and the mate unchanged
  (`--tolerance 0.05` also accepts 5 % growth; `--minimize-squares` shortens square lists);
* adds the position one ply down the PV (`bm #-(N-1)`; for a negative root `bm #N`), verified with
  the same flags; duplicates against the repo and within the new set are dropped (see `report.txt`);
* keeps `matetb.epd` sorted (|bm|, positive first) and `options.hpp` blocks sorted by their largest
  |bm|, in the existing layout;
* writes `new_puzzles.patch`, `new.epd` (new lines only) and `report.txt`;
* `--final-check` applies the patch to a scratch copy, builds, and runs every new line through
  `matetb_threaded` with no flags, as `check.sh` does. Use `--workers`/`--threads` to speed up
  verification of big trees.
