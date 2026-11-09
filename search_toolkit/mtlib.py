"""Shared helpers for the matetb search toolkit.

Everything here talks to the *real* matetb / matetb_threaded binaries (built from
a matetb checkout that has --maxNodes (upstream main, #15)), so results never depend on
a re-implementation of the tool.
"""
import os
import re
import subprocess
import time

import chess

PIECE_LETTER = {chess.PAWN: "p", chess.KNIGHT: "n", chess.BISHOP: "b",
                chess.ROOK: "r", chess.QUEEN: "q"}


# --------------------------------------------------------------------------
# parsing
# --------------------------------------------------------------------------
def parse_epd(path):
    """Parse matetrackpv.epd / matetb.epd style files.

    Returns a list of dicts with fen4, bm (int, negative if the side to move
    is being mated), pv (list of uci moves or None) and the raw line.
    """
    out = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            m = re.match(r"^(\S+ \S+ \S+ \S+)\s+bm #(-?\d+);(.*)$", line)
            if not m:
                continue
            pv = None
            pm = re.search(r"PV:\s*([^;]*);", m.group(3))
            if pm:
                pv = pm.group(1).split()
            out.append(dict(fen4=m.group(1), bm=int(m.group(2)), pv=pv, line=line))
    return out


def existing_keys(repo):
    """(fen4, bm) pairs already in matetb.epd, and fen4 strings keyed in options.hpp."""
    have = set()
    p = os.path.join(repo, "matetb.epd")
    if os.path.exists(p):
        for e in parse_epd(p):
            have.add((e["fen4"], e["bm"]))
    keyed = set()
    p = os.path.join(repo, "options.hpp")
    if os.path.exists(p):
        for m in re.finditer(r'epd == "([^"]+)"', open(p).read()):
            keyed.add(m.group(1))
    return have, keyed


# --------------------------------------------------------------------------
# PV features and the restriction ladder
# --------------------------------------------------------------------------
def sq(s):
    return chess.square_name(s)


def analyze(entry):
    """Walk the PV and collect everything needed to derive restrictions.

    All restrictions only constrain the *mating* side (as in matetb), and every
    flag is only switched on if the PV itself is compatible with it.
    """
    pv = entry.get("pv")
    if not pv:
        return None
    b = chess.Board(entry["fen4"] + " 0 1")
    bm = entry["bm"]
    ms = b.turn if bm > 0 else (not b.turn)
    root = b.copy()
    ms_from, ms_moves, ms_king, ms_prom, ms_cap = set(), 0, 0, 0, 0
    ms_toatt = ms_tocap = allow_cap = 0
    cap_types = set()
    d_moves, d_prom = [], 0
    d_immobile = {s for s, p in root.piece_map().items() if p.color != ms}
    ident = {s: s for s, p in root.piece_map().items() if p.color == ms}
    moved_ids = set()
    for u in pv:
        try:
            m = chess.Move.from_uci(u)
        except ValueError:
            return None
        if m not in b.legal_moves:
            return None
        if b.turn == ms:
            ms_moves += 1
            ms_from.add(m.from_square)
            if b.piece_type_at(m.from_square) == chess.KING:
                ms_king += 1
            if m.promotion:
                ms_prom += 1
            if b.is_capture(m):
                ms_cap += 1
                if b.is_en_passant(m):
                    cap_types.add("p")
                else:
                    cap_types.add(PIECE_LETTER.get(b.piece_type_at(m.to_square), "?"))
            if b.is_attacked_by(not ms, m.to_square):
                ms_toatt += 1
            if m.from_square in ident:
                i = ident.pop(m.from_square)
                moved_ids.add(i)
                ident[m.to_square] = i
            b.push(m)
            legal = list(b.legal_moves)
            if any(b.is_capture(x) for x in legal):
                allow_cap += 1
            if any(x.to_square == m.to_square and b.is_capture(x) for x in legal):
                ms_tocap += 1
        else:
            legal = list(b.legal_moves)
            d_moves.append(len(legal))
            for x in legal:
                d_immobile.discard(x.from_square)
            if m.promotion:
                d_prom += 1
            ident.pop(m.to_square, None)
            b.push(m)
    pieces = root.piece_map()
    ms_squares = {s for s, p in pieces.items() if p.color == ms}
    frozen = sorted(ms_squares - ms_from)
    ms_pawns = {s for s in ms_squares if pieces[s].piece_type == chess.PAWN}
    d_types = {PIECE_LETTER[p.piece_type] for p in pieces.values()
               if p.color != ms and p.piece_type in PIECE_LETTER}
    runners = 0
    for s, p in pieces.items():
        if p.piece_type == chess.PAWN and p.color != ms:
            step = 8 if p.color else -8
            t, clear = s + step, True
            while 0 <= t < 64:
                if t in pieces:
                    clear = False
                    break
                t += step
            runners += clear
    return dict(
        ms=ms, bm=bm, plen=len(pv), ms_moves=ms_moves, ms_king=ms_king,
        ms_distinct=len(moved_ids), n_ms=len(ms_squares), ms_prom=ms_prom,
        d_prom=d_prom, ms_cap=ms_cap, ms_toatt=ms_toatt, ms_tocap=ms_tocap,
        allow_cap=allow_cap, cap_types="".join(sorted(cap_types)),
        d_avg=(sum(d_moves) / len(d_moves)) if d_moves else 0.0,
        d_max=max(d_moves) if d_moves else 0, runners=runners,
        frozen=[sq(s) for s in frozen],
        all_pawns_frozen=all(s in set(frozen) for s in ms_pawns),
        d_immobile=[sq(s) for s in sorted(d_immobile)],
        not_captured=[t for t in "pnbrq" if t in d_types and t not in cap_types],
    )


def ladder(f):
    """Restriction sets, from most to least restrictive: [(keys, argv), ...].

    A tree for a more restrictive set is a subset of the tree of any weaker one,
    so a search can stop at the first set whose tree is too large.
    """
    A = {}
    if f["frozen"]:
        A["freeze"] = ["--excludeFrom", " ".join(f["frozen"])]
    if f["ms_prom"] == 0 and not f["all_pawns_frozen"]:
        A["nopromo"] = ["--excludePromotionTo", "qrbn"]
    if f["ms_cap"] == 0:
        A["nocap"] = ["--excludeCaptures"]
    elif f["not_captured"]:
        A["capof"] = ["--excludeCapturesOf", "".join(f["not_captured"])]
    if f["allow_cap"] == 0:
        A["allowcap"] = ["--excludeAllowingCapture"]  # implies "att"
    elif f["ms_tocap"] == 0:
        A["tocap"] = ["--excludeToCapturable"]
    if f["allow_cap"] != 0 and f["ms_toatt"] == 0:
        A["att"] = ["--excludeToAttacked"]
    if f["d_immobile"]:
        A["allowfrom"] = ["--excludeAllowingFrom", " ".join(f["d_immobile"])]
    order = ["freeze", "nopromo", "nocap", "capof", "att", "tocap", "allowcap",
             "allowfrom"]
    drop_order = ["allowfrom", "allowcap", "tocap", "att", "capof", "nocap"]
    base = [k for k in order if k in A]
    levels, seen = [], set()

    def add(keys):
        keys = tuple(k for k in order if k in keys)
        if keys in seen:
            return
        seen.add(keys)
        argv = []
        for k in keys:
            argv += A[k]
        levels.append((list(keys), argv))

    cur = set(base)
    add(cur)
    for d in drop_order:
        if d in cur:
            cur = cur - {d}
            add(cur)
    add({k for k in base if k in ("freeze", "nopromo")})
    add(set())
    return levels


def promise_key(f, bm):
    """Heuristic ordering: constrained defender first, then longer mates."""
    return (f["d_avg"], f["runners"], -abs(bm))


# --------------------------------------------------------------------------
# running the real binaries
# --------------------------------------------------------------------------
def run_matetb(repo, epd, argv, max_positions, threads=1, timeout=None):
    """Run matetb (threads == 1) or matetb_threaded (threads > 1).

    Returns dict(status, positions, mate, secs):
      status 'ok'      tree complete; mate is the proven upper bound (or None)
             'cap'     tree exceeded max_positions
             'timeout' wall-clock limit hit
             'error'   anything else
    """
    exe = "matetb" if threads <= 1 else "matetb_threaded"
    cmd = [os.path.join(repo, exe), "--epd", epd] + list(argv)
    if max_positions:
        cmd += ["--maxNodes", str(max_positions)]
    if threads > 1:
        cmd += ["--concurrency", str(threads)]
    t0 = time.time()
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return dict(status="timeout", positions=0, mate=None, secs=time.time() - t0)
    secs = time.time() - t0
    out = p.stdout.replace("\r", "\n")
    if p.returncode == 3 or "Aborted: the game tree has more than" in out:
        return dict(status="cap", positions=max_positions or 0, mate=None, secs=secs)
    if p.returncode != 0:
        return dict(status="error", positions=0, mate=None, secs=secs,
                    msg=(out + p.stderr)[-300:])
    fm = re.search(r"Found (\d+) positions", out)
    positions = int(fm.group(1)) if fm else 0
    mate = None
    lines = out.split("\n")
    for i, l in enumerate(lines):
        if l.startswith("Matetrack:") and i + 1 < len(lines):
            mm = re.search(r"bm #(-?\d+);", lines[i + 1])
            if mm:
                mate = int(mm.group(1))
    return dict(status="ok", positions=positions, mate=mate, secs=secs)


def mem_available_gb():
    try:
        for l in open("/proc/meminfo"):
            if l.startswith("MemAvailable:"):
                return int(l.split()[1]) / 1024 / 1024
    except OSError:
        pass
    return None
