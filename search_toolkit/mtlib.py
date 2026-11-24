"""Helpers shared by the search scripts: matetrack parsing, PV analysis, the ladder of
restriction sets, and a runner for the real matetb / matetb_threaded binaries."""
import heapq
import os
import re
import signal
import subprocess
import sys
import threading

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
    allow_capture = tocapturable = allow_promotion = False
    d_moves = []
    # what the defender can do right after one of our moves (this is where the
    # --excludeAllowing* flags look), and where the defender promotes in the PV
    reply_from, reply_to, promo_squares = set(), set(), set()
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
            allow_promotion |= any(r.promotion for r in replies)
            tocapturable |= any(b.is_capture(r) and r.to_square == m.to_square
                                for r in replies)
            reply_from |= {r.from_square for r in replies}
            reply_to |= {r.to_square for r in replies}
        else:
            d_moves.append(b.legal_moves.count())
            if m.promotion:
                promo_squares.add(m.to_square)
            b.push(m)
    pieces = root.piece_map()
    mine = {s for s, p in pieces.items() if p.color == ms}
    frozen = mine - moved_from
    pawns = {s for s in mine if pieces[s].piece_type == chess.PAWN}
    their_types = {LETTER[p.piece_type] for p in pieces.values()
                   if p.color != ms and p.piece_type in LETTER}
    runners = def_pawns = 0  # defender pawns (with a free path to the promotion rank)
    for s, p in pieces.items():
        if p.piece_type == chess.PAWN and p.color != ms:
            def_pawns += 1
            step, t = (8 if p.color else -8), s + (8 if p.color else -8)
            while 0 <= t < 64 and t not in pieces:
                t += step
            runners += not (0 <= t < 64)
    # defender pieces (and promoted pieces) that never have a move after one of our
    # moves along the PV: a pawn that promotes on e.g. c1 and then never moves again
    # must not be allowed to leave c1 in the tree either
    immobile = ({s for s, p in pieces.items() if p.color != ms} | promo_squares) - reply_from
    # promotion squares the PV never needs: a defender pawn is only allowed to
    # promote where the PV lets it (only used if allowing *any* promotion is not an option)
    promo_rank = chess.BB_RANK_1 if (not ms) == chess.BLACK else chess.BB_RANK_8
    unused_promo_to = chess.SquareSet(promo_rank) - chess.SquareSet(reply_to)
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
        no_allowed_promotion=not allow_promotion,
        promo_unused_to=names(unused_promo_to),
        def_pawns=def_pawns,
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
    if f["def_pawns"]:
        # A defender pawn that promotes and whose new piece then wanders around is the
        # classic tree explosion: the defender's moves are never restricted, only ours.
        if f["no_allowed_promotion"]:
            A["allowpromo"] = ["--excludeAllowingPromotion"]
        elif f["promo_unused_to"]:
            A["allowto"] = ["--excludeAllowingTo", " ".join(f["promo_unused_to"])]
    keep = list(A)
    sets = [list(keep)]
    for drop in ("allowfrom", "allowto", "allowpromo", "allowcap", "tocap", "capof", "nocap"):
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


# --- relaxing a constraint set ----------------------------------------------------
#
# A constraint set is a frozenset of atoms: ("from", sq), ("allowfrom", sq),
# ("allowto", sq), ("promo", letter), ("capof", letter), ("nocap",), ("tocap",),
# ("allowcap",), ("allowpromo",).  A set with fewer atoms allows more moves for the
# mating side, so its tree is a superset.

SQUARE_FLAGS = {"--excludeFrom": "from", "--excludeAllowingFrom": "allowfrom",
                "--excludeAllowingTo": "allowto"}
LETTER_FLAGS = {"--excludePromotionTo": "promo", "--excludeCapturesOf": "capof"}
BOOL_FLAGS = {"--excludeCaptures": "nocap", "--excludeToCapturable": "tocap",
              "--excludeAllowingCapture": "allowcap",
              "--excludeAllowingPromotion": "allowpromo"}
FLAG_OF = {**{v: k for k, v in SQUARE_FLAGS.items()}, **{v: k for k, v in LETTER_FLAGS.items()},
           **{v: k for k, v in BOOL_FLAGS.items()}}
LETTER_KINDS = set(LETTER_FLAGS.values())
# the order in which the atoms are given up (most speculative first), as in ladder()
RELAX_ORDER = ["allowfrom", "allowto", "allowpromo", "allowcap", "tocap", "capof", "nocap",
               "promo", "from"]


def to_state(argv):
    state, i = set(), 0
    while i < len(argv):
        flag = argv[i]
        if flag in BOOL_FLAGS:
            state.add((BOOL_FLAGS[flag],))
            i += 1
            continue
        kind = SQUARE_FLAGS.get(flag) or LETTER_FLAGS[flag]
        state |= {(kind, x) for x in (argv[i + 1].split() if flag in SQUARE_FLAGS
                                      else argv[i + 1])}
        i += 2
    return frozenset(state)


def to_argv(state):
    out = []
    for kind in ("from", "promo", "nocap", "capof", "tocap", "allowcap", "allowpromo",
                 "allowfrom", "allowto"):
        vals = [a[1] for a in state if a[0] == kind and len(a) > 1]
        if kind in LETTER_KINDS:
            vals.sort(key="qrbnp".index)
        else:
            vals.sort(key=chess.parse_square)
        if vals:
            out += [FLAG_OF[kind], ("" if kind in LETTER_KINDS else " ").join(vals)]
        elif (kind,) in state:
            out.append(FLAG_OF[kind])
    return out


def _implied(atom, state):
    return (atom in state
            or (atom[0] == "capof" and ("nocap",) in state)
            or (atom == ("tocap",) and ("allowcap",) in state))


def stricter(a, b):
    """True if constraint set a allows no move that b forbids (tree(a) <= tree(b))."""
    return all(_implied(x, a) for x in b)


def _neighbors(state, king):
    """One-step relaxations of a state, the most speculative atoms first."""
    def dist(atom):
        if len(atom) == 2 and atom[0] in ("from", "allowfrom", "allowto"):
            sq = chess.parse_square(atom[1])
            return chess.square_distance(sq, king)
        return 0
    out = []
    for atom in sorted(state, key=lambda a: (RELAX_ORDER.index(a[0]), dist(a), a)):
        rest = state - {atom}
        if atom == ("allowcap",):
            out.append(rest | {("tocap",)})  # the smaller step first
        out.append(rest)
    return out


def relax_search(run, ok_states, capped_states, king, budget):
    """Budget-aware relaxation, used once the ladder has reached a state whose tree
    completes without proving the mate (an "ok" state) and the next ladder step blows
    the node cap.  Between the two there are many constraint sets that would still fit:
    drop single atoms (one frozen piece, one promotion piece, one defender square, ...)
    from the most relaxed ok state, keep going while trees complete, and back off when
    a tree exceeds the cap.  Nothing is run that a known result already decides:
    a state at least as weak as a capped one is too big, a state at least as strict as a
    completed one cannot prove more.

    run(argv) -> run_matetb result plus the key "good": True if the tree proved the
    wanted mate (or a shorter one).
    ok_states: [(state, positions)] of the completed trees that did not.
    king: square of the defending king (pieces close to it are given up first).
    Returns (None or (argv, result) of the solving run, tried, runs used, exhausted)
    where "exhausted" means the run budget ran out with relaxations left to try.
    """
    failed = [s for s, _ in ok_states]
    capped = list(capped_states)
    seen = set(failed) | set(capped)
    heap = [(len(s), n, i, s) for i, (s, n) in enumerate(ok_states)]
    heapq.heapify(heap)
    tried, runs, tie = [], 0, len(heap)
    while heap:
        _, _, _, state = heapq.heappop(heap)
        for nb in _neighbors(state, king):
            if nb in seen or any(stricter(c, nb) for c in capped) \
                    or any(stricter(nb, f) for f in failed):
                continue
            if runs >= budget:
                return None, tried, runs, True
            seen.add(nb)
            argv = to_argv(nb)
            r = run(argv)
            runs += 1
            tried.append(dict(argv=argv, status=r["status"], positions=r["positions"],
                              mate=r["mate"], relax=True,
                              **({"error": r["error"]} if "error" in r else {})))
            if r["status"] == "ok":
                if r["good"]:
                    return (argv, r), tried, runs, False
                failed.append(nb)
                tie += 1
                heapq.heappush(heap, (len(nb), r["positions"], tie, nb))
            elif r["status"] == "cap":
                capped.append(nb)
            else:
                return None, tried, runs, False  # fail: give up on this candidate
    return None, tried, runs, False


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


_peak = dict(kb=0, epd="")
_peak_lock = threading.Lock()


def peak_memory_report(workers):
    """The largest memory use of any single matetb run so far, and the resulting upper
    bound for `workers` runs at the same time, as a printable line."""
    gb = _peak["kb"] / 1024 ** 2
    return (f"largest memory use of a single matetb run: {gb:.2f} GB "
            f"({_peak['epd'].split(';')[0]}); upper bound for {workers} workers: "
            f"{gb * workers:.2f} GB")


def run_matetb(repo, epd, argv, max_nodes, threads=1, timeout=None):
    """Run matetb (threads == 1) or matetb_threaded with --maxNodes.

    Returns dict(status, positions, mate, pv[, error]); status is 'ok' (tree complete;
    mate is the proven mate or None), 'cap' (more than max_nodes positions), or 'fail'.
    """
    exe = "matetb" if threads <= 1 else "matetb_threaded"
    cmd = [os.path.join(repo, exe), "--epd", epd, *argv, "--maxNodes", str(max_nodes)]
    if threads > 1:
        cmd += ["--concurrency", str(threads)]
    # wait4 (instead of subprocess.run) gives the peak memory of this very run
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    timed_out = threading.Event()
    def on_timeout():
        timed_out.set()
        try:
            p.kill()
        except ProcessLookupError:
            pass
    timer = threading.Timer(timeout, on_timeout) if timeout else None
    if timer:
        timer.start()
    out = p.stdout.read().decode(errors="replace").replace("\r", "\n")
    p.stdout.close()
    _, status, usage = os.wait4(p.pid, 0)
    returncode = os.waitstatus_to_exitcode(status)
    if timer:
        timer.cancel()
    with _peak_lock:
        kb = usage.ru_maxrss // 1024 if sys.platform == "darwin" else usage.ru_maxrss
        if kb > _peak["kb"]:
            _peak.update(kb=kb, epd=epd)
    if returncode == 3:
        return dict(status="cap", positions=max_nodes, mate=None, pv=None)
    if returncode != 0:
        if timed_out.is_set():
            err = "timeout"
        elif returncode < 0:
            try:
                err = f"{signal.Signals(-returncode).name} (rc={returncode})"
            except ValueError:
                err = f"rc={returncode}"
        else:
            err = f"rc={returncode}"
        return dict(status="fail", positions=0, mate=None, pv=None, error=err)
    found = re.search(r"Found (\d+) positions", out)
    res = re.search(r"Matetrack:\n.*bm #(-?\d+); PV: ([^;]*);", out)
    return dict(status="ok", positions=int(found.group(1)) if found else 0,
                mate=int(res.group(1)) if res else None,
                pv=res.group(2).split() if res else None)


def shorter(mate, bm):
    """True if the proven mate is strictly shorter than bm (same side mating)."""
    return bool(mate) and (mate > 0) == (bm > 0) and abs(mate) < abs(bm)
