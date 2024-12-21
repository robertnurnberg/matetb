# matetb

<table width="100%">
  <tr>
    <td width="80%" align="left" valign="top">
        A custom tablebase generator for mate puzzles.
        <br><br>
        This is the original `matetb.py` script from <a href="https://github.com/robertnurnberg/matetools">matetools</a> ported to C++.
    </td>
    <td width="20%" align="right">
      <img src="https://lichess1.org/export/fen.gif?fen=/8/8/1p6/6k1/1p2Q3/p1p1p3/rbrbK3%20w%20-%20-">
    </td>
  </tr>
</table>

Example usage for the position on the right:

```
> ./matetb --epd "8/8/8/1p6/6k1/1p2Q3/p1p1p3/rbrbK3 w - -" --excludeFrom e1 --excludeTo "a1 c1" --excludeToAttacked
Running with options --epd "8/8/8/1p6/6k1/1p2Q3/p1p1p3/rbrbK3 w - -" --excludeFrom e1 --excludeTo "a1 c1" --excludeToAttacked 
Restrict moves for WHITE side.
Create the allowed part of the game tree ...
Found 40252 positions to depth 21 in 0.36s
Connect child nodes ...
Connected 40252 positions in 0.16s
Generate tablebase ...
Tablebase generated with 33 iterations in 0.07s

Matetrack:
8/8/8/1p6/6k1/1p2Q3/p1p1p3/rbrbK3 w - - bm #36; PV: e3b3 g4h4 b3f3 b5b4 f3g2 b4b3 g2f2 h4h3 f2g1 h3h4 g1g2 h4h5 g2f3 h5h4 f3b3 h4g4 b3e3 g4h4 e3f3 h4g5 f3h3 g5g6 h3h4 g6g7 h4h5 g7f6 h5g4 f6f7 g4g5 f7e6 g5f4 e6e7 f4f5 e7d6 f5e4 d6d7 e4e5 d7c6 e5d4 c6c7 d4d5 c7c8 d5d6 c8b7 d6c5 b7a8 c5b5 a8a7 b5d5 a7b8 d5c6 b8a7 c6c8 a7b6 c8d7 b6c5 d7e6 c5d4 e6f5 d4e3 f5g4 e3d3 g4f4 d3c3 f4e4 c3b3 e4d4 b3a3 d4c4 a3b2 c4b4;
```

```
Usage: matetb [--help] [--version] [--epd VAR] [--depth VAR] [--openingMoves VAR] [--excludeMoves VAR] [--excludeSANs VAR] [--excludeFrom VAR] [--excludeTo VAR] [--excludeCaptures] [--excludeCapturesOf VAR] [--excludeToAttacked] [--excludeToCapturable] [--excludePromotionTo VAR] [--excludeAllowingCapture] [--excludeAllowingFrom VAR] [--excludeAllowingTo VAR] [--excludeAllowingMoves VAR] [--excludeAllowingSANs VAR] [--outFile VAR] [--verbose VAR]

Prove (upper bound) for best mate for a given position by constructing a custom tablebase for a (reduced) game tree.

Optional arguments:
  -h, --help                shows help message and exits 
  -v, --version             prints version information and exits 
  --epd                     EPD for the root position. If bm is not given, it is assumed that the side to move is mating. [nargs=0..1] [default: "8/8/8/1p6/6k1/1p2Q3/p1p1p3/rbrbK3 w - - bm #36;"]
  --depth                   Maximal depth for the to be constructed game tree (a too low value means mate cannot be found). [nargs=0..1] [default: 2147483647]
  --openingMoves            Comma separated opening lines in UCI notation that specify the mating side's moves. In each line a single placeholder '*' is allowed for the defending side. [nargs=0..1] [default: ""]
  --excludeMoves            Space separated UCI moves that are not allowed. [nargs=0..1] [default: ""]
  --excludeSANs             Space separated SAN moves that are not allowed. [nargs=0..1] [default: ""]
  --excludeFrom             Space separated square names that pieces should never move from. [nargs=0..1] [default: ""]
  --excludeTo               Space separated square names that pieces should never move to. [nargs=0..1] [default: ""]
  --excludeCaptures         Never capture. 
  --excludeCapturesOf       String containing piece types that should never be captured, e.g. "qrbn". [nargs=0..1] [default: ""]
  --excludeToAttacked       Never move to attacked squares (including from pinned pieces, but ignoring en passant). 
  --excludeToCapturable     Never move to a square that risks capture (much slower than --excludeToAttacked). 
  --excludePromotionTo      String containing piece types that should never be promoted to, e.g. "qrb". [nargs=0..1] [default: ""]
  --excludeAllowingCapture  Avoid moves that allow a capture somewhere on the board (much slower than --excludeToAttacked). 
  --excludeAllowingFrom     Space separated square names that opponent's pieces should not be allowed to move from in reply to our move. [nargs=0..1] [default: ""]
  --excludeAllowingTo       Space separated square names that opponent's pieces should not be allowed to move to in reply to our move. [nargs=0..1] [default: ""]
  --excludeAllowingMoves    Space separated UCI moves that opponent should not be allowed to make in reply to our move. [nargs=0..1] [default: ""]
  --excludeAllowingSANs     Space separated SAN moves that opponent should not be allowed to make in reply to our move. [nargs=0..1] [default: ""]
  --outFile                 Optional output file for the TB. [nargs=0..1] [default: ""]
  --verbose                 Specify the verbosity level. E.g. --verbose 1 shows PVs for all legal moves, and --verbose 2 also links to chessdb.cn. [nargs=0..1] [default: 0]
```
