"""Helpers shared by the search scripts: matetrack parsing, PV analysis, the ladder of
restriction sets, and a runner for the real matetb / matetb_threaded binaries."""
import os
import re
import subprocess
import sys

import chess

LETTER = {chess.PAWN: "p", chess.KNIGHT: "n", chess.BISHOP: "b", chess.ROOK: "r",
          chess.QUEEN: "q"}


def parse_epd(path):
    """Entries of matetrackpv.epd / matetb.epd: dicts with fen4, bm and pv (or None)."""
    out = []
    for line in open(path):
        m = re.match(r"^(\S+ \S+ \S+ \S+)\s+bm #(-?\d+);(.*)$", line.strip())
        if m:
            pv = re.search(r"PV:\s*([^;]*);", m.group(3))
            out.append(dict(fen4=m.group(1), bm=int(m.group(2)),
                            pv=pv.group(1).split() if pv else None))
    return out


def analyze(entry):
    """Walk the PV and collect what the restriction flags need.

    All flags only constrain the mating side, and each one is only used if the PV
    itself is compatible with it. Returns None if the PV is missing or illegal.
    """
    if not entry["pv"]:
        return None
    b = chess.Board(entry["fen4"] + " 0 1")
    ms = b.turn if entry["bm"] > 0 else (not b.turn)
    root = b.copy()
    moved_from, promoted, captured = set(), set(), set()
    allow_capture = tocapturable = False
    d_moves = []
    immobile = {s for s, p in root.piece_map().items() if p.color != ms}
    for u in entry["pv"]:
        m = chess.Move.from_uci(u)
        if m not in b.legal_moves:
            return None
        if b.turn == ms:
            moved_from.add(m.from_square)
            if m.promotion:
                promoted.add(LETTER[m.promotion])
            if b.is_capture(m):
                captured.add("p" if b.is_en_passant(m)
                             else LETTER[b.piece_type_at(m.to_square)])
            b.push(m)
            replies = list(b.legal_moves)
            allow_capture |= any(b.is_capture(r) for r in replies)
            tocapturable |= any(b.is_capture(r) and r.to_square == m.to_square
                                for r in replies)
        else:
            replies = list(b.legal_moves)
            d_moves.append(len(replies))
            immobile -= {r.from_square for r in replies}
            b.push(m)
    pieces = root.piece_map()
    mine = {s for s, p in pieces.items() if p.color == ms}
    frozen = mine - moved_from
    pawns = {s for s in mine if pieces[s].piece_type == chess.PAWN}
    their_types = {LETTER[p.piece_type] for p in pieces.values()
                   if p.color != ms and p.piece_type in LETTER}
    runners = 0  # defender pawns with a free path to the promotion rank
    for s, p in pieces.items():
        if p.piece_type == chess.PAWN and p.color != ms:
            step, t = (8 if p.color else -8), s + (8 if p.color else -8)
            while 0 <= t < 64 and t not in pieces:
                t += step
            runners += not (0 <= t < 64)
    names = lambda squares: [chess.square_name(s) for s in sorted(squares)]
    return dict(
        frozen=names(frozen),
        pawns_frozen=pawns <= frozen,
        promo_unused="".join(t for t in "qrbn" if t not in promoted),
        no_captures=not captured,
        capture_unused="".join(t for t in "pnbrq" if t in their_types and t not in captured),
        no_allowed_capture=not allow_capture,
        no_tocapturable=not tocapturable,
        immobile=names(immobile),
        d_avg=sum(d_moves) / len(d_moves) if d_moves else 0.0,
        runners=runners)


def ladder(f):
    """Restriction sets for a candidate, strictest first: [argv, ...].

    A weaker set only allows more moves, so its tree contains every tree of a
    stricter set: once a tree exceeds the cap, the weaker ones can be skipped.
    """
    A = {}
    if f["frozen"]:
        A["freeze"] = ["--excludeFrom", " ".join(f["frozen"])]
    if f["promo_unused"] and not f["pawns_frozen"]:
        A["promo"] = ["--excludePromotionTo", f["promo_unused"]]
    if f["no_captures"]:
        A["nocap"] = ["--excludeCaptures"]
    elif f["capture_unused"]:
        A["capof"] = ["--excludeCapturesOf", f["capture_unused"]]
    if f["no_allowed_capture"]:
        A["allowcap"] = ["--excludeAllowingCapture"]  # implies "tocap"
    elif f["no_tocapturable"]:
        A["tocap"] = ["--excludeToCapturable"]
    if f["immobile"]:
        A["allowfrom"] = ["--excludeAllowingFrom", " ".join(f["immobile"])]
    keep = list(A)
    sets = [list(keep)]
    for drop in ("allowfrom", "allowcap", "tocap", "capof", "nocap"):
        if drop in keep:
            keep.remove(drop)
            sets.append(list(keep))
    # a PV shows one defence only: other lines may need other promotion pieces
    sets += [[k for k in ("freeze", "promo") if k in A], [k for k in ("freeze",) if k in A], []]
    out, seen = [], set()
    for keys in sets:
        if tuple(keys) not in seen:
            seen.add(tuple(keys))
            out.append([x for k in keys for x in A[k]])
    return out


def prepare(repo):
    """Compile the binaries (make does nothing if they are up to date) and check that
    they support --maxNodes."""
    p = subprocess.run(["make", "-j"], cwd=repo, capture_output=True, text=True)
    if p.returncode:
        sys.exit("make failed:\n" + (p.stdout + p.stderr)[-2000:])
    for exe in ("matetb", "matetb_threaded"):
        h = subprocess.run([os.path.join(repo, exe), "--help"], capture_output=True, text=True)
        if "--maxNodes" not in h.stdout + h.stderr:
            sys.exit(f"{exe} in {repo} does not support --maxNodes: update the sources")


def run_matetb(repo, epd, argv, max_nodes, threads=1, timeout=None):
    """Run matetb (threads == 1) or matetb_threaded with --maxNodes.

    Returns dict(status, positions, mate, pv); status is 'ok' (tree complete; mate is
    the proven mate or None), 'cap' (more than max_nodes positions), or 'fail'.
    """
    exe = "matetb" if threads <= 1 else "matetb_threaded"
    cmd = [os.path.join(repo, exe), "--epd", epd, *argv, "--maxNodes", str(max_nodes)]
    if threads > 1:
        cmd += ["--concurrency", str(threads)]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return dict(status="fail", positions=0, mate=None, pv=None)
    out = p.stdout.replace("\r", "\n")
    if p.returncode == 3:
        return dict(status="cap", positions=max_nodes, mate=None, pv=None)
    if p.returncode != 0:
        return dict(status="fail", positions=0, mate=None, pv=None)
    found = re.search(r"Found (\d+) positions", out)
    res = re.search(r"Matetrack:\n.*bm #(-?\d+); PV: ([^;]*);", out)
    return dict(status="ok", positions=int(found.group(1)) if found else 0,
                mate=int(res.group(1)) if res else None,
                pv=res.group(2).split() if res else None)


def shorter(mate, bm):
    """True if the proven mate is strictly shorter than bm (same side mating)."""
    return bool(mate) and (mate > 0) == (bm > 0) and abs(mate) < abs(bm)
