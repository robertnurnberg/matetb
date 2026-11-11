# matetb search toolkit

Finds positions of `matetrackpv.epd` that `matetb` can solve with PV-derived restrictions and
turns them into a patch for `matetb.epd` / `options.hpp`. The scripts call the real `matetb` /
`matetb_threaded` binaries (`--maxNodes` aborts a tree that gets too big). Needs `pip install chess`.

    cd ~/matetb && make -j
    python3 search_toolkit/mt_search.py --repo ~/matetb --matetrack matetrackpv.epd \
            --max-nodes 5000000 --workers 16 --min-bm 10        # resumable, appends to results.jsonl
    python3 search_toolkit/mt_stats.py results.jsonl             # what is left
    python3 search_toolkit/mt_patch.py --repo ~/matetb --matetrack matetrackpv.epd \
            --results results.jsonl --minimize                   # -> patch_out/
    git apply patch_out/new_puzzles.patch && make -j             # then run your check on patch_out/new.epd

## Search

For each candidate, restriction sets are derived from its PV and tried strictest first (`mtlib.ladder`):

| flag | used when the PV is compatible with it |
|---|---|
| `--excludeFrom` | mating pieces that never move in the PV |
| `--excludePromotionTo` | a mating pawn is not frozen: the piece types the PV does not promote to (all of `qrbn` if it has no promotion; `rbn` for a PV that only queens; `qrb` for a knight underpromotion) |
| `--excludeCaptures` / `--excludeCapturesOf` | the mating side never captures / never captures those piece types |
| `--excludeAllowingCapture` | the defender never has a capture after a mating move |
| `--excludeToCapturable` | no mating move lets the moved piece be captured (only if the previous one does not apply) |
| `--excludeAllowingFrom` | defender pieces that never have a legal move along the PV |

Flags are then dropped (`allowfrom`, `allowcap`/`tocap`, `capof`/`nocap`), followed by sets with only
frozen pieces plus promotions, only frozen pieces, and no flags. A PV shows one defence only, so other
lines may need other promotion pieces; the last levels free the promotions for that reason.
A tree that exceeds `--max-nodes` stops the ladder (weaker sets have bigger trees). A candidate is
*solved* when a tree completes and proves matetrack's `bm`, or a **shorter** mate: that is printed as
`SHORTER MATE` and the proven mate becomes the position's bm everywhere (epd line, `options.hpp`, child).

Results are JSON lines. Reruns skip solved candidates and those that already exceeded a cap at least as
large; use a larger `--max-nodes` to go deeper. Suggested passes: 200k (all cores), 5M (cores/2 workers),
then 100M+ with `--workers 1 --threads N` and `--max-davg 4 --min-bm 20`. Peak memory is about 200 bytes per
position per worker (about 400 with `--threads`). `--max-davg` skips candidates whose defender has many
replies along the PV (solved entries have a median of about 2.4, the rest about 6).

## Patch

`mt_patch.py` takes one or more results files and adds, for every solved position not yet in `matetb.epd`,
the position one ply down its PV (`bm #-(N-1)`; matetb's own PV is used if the mate is shorter than
matetrack's). Children are checked with the root's flags, duplicates are dropped, `matetb.epd` stays sorted
by |bm| (positive first) and `options.hpp` blocks by their largest |bm|. `--minimize` drops each flag whose
removal leaves the tree no larger (one extra run per flag; skip it for huge trees). A restricted tree proves
"mate in at most N"; optimality rests on matetrack's bm, as for the existing entries.
