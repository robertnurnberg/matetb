#pragma once

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "external/argparse.hpp"
#include "misc.hpp"

class Options {
public:
  std::string epdStr, openingMoves, excludeMoves, excludeSANs, restrictTo,
      excludeFrom, excludeTo, excludeCapturesOf, excludePromotionTo,
      excludeAllowingFrom, excludeAllowingTo, excludeAllowingMoves,
      excludeAllowingSANs, outFile;
  bool excludeCaptures, excludeToAttacked, excludeToCapturable,
      excludeAllowingCapture;
  int depth, verbose, concurrency;
  size_t maxNodes;
  Options()
      : epdStr(""), openingMoves(""), excludeMoves(""), excludeSANs(""),
        restrictTo(""), excludeFrom(""), excludeTo(""), excludeCapturesOf(""),
        excludePromotionTo(""), excludeAllowingFrom(""), excludeAllowingTo(""),
        excludeAllowingMoves(""), excludeAllowingSANs(""), outFile(""),
        excludeCaptures(false), excludeToAttacked(false),
        excludeToCapturable(false), excludeAllowingCapture(false),
        depth(MAX_DEPTH), verbose(0), concurrency(0), maxNodes(0) {}
  Options(int argc, char **argv, bool use_concurrency = false);
  void fill_exclude_options();
  void print(std::ostream &os) const;
};

inline Options::Options(int argc, char **argv, bool use_concurrency)
    : Options() {
  argparse::ArgumentParser args(use_concurrency ? "matetb_threaded" : "matetb");
  args.add_description(
      "Prove (upper bound) for best mate for a given position by constructing "
      "a custom tablebase for a (reduced) game tree.");
  args.add_argument("--epd")
      .default_value("8/8/8/1p6/6k1/1p2Q3/p1p1p3/rbrbK3 w - - bm #36;")
      .help("EPD for the root position. If bm is not given, it is assumed that "
            "the side to move is mating.");
  args.add_argument("--depth")
      .default_value(MAX_DEPTH)
      .action([](const std::string &value) { return std::stoi(value); })
      .help("Maximal depth for the to be constructed game tree (a too low "
            "value means mate cannot be found).");
  args.add_argument("--maxNodes")
      .default_value(std::string("0"))
      .help("Maximal allowed size for the game tree (0 means no limit). "
            "Program aborts when limit is reached.");
  args.add_argument("--openingMoves")
      .default_value("")
      .help("Comma separated opening lines in UCI notation that specify the "
            "mating side's moves. In each line a single placeholder '*' is "
            "allowed for the defending side.");
  args.add_argument("--excludeMoves")
      .default_value("")
      .help("Space separated UCI moves that are not allowed.");
  args.add_argument("--excludeSANs")
      .default_value("")
      .help("Space separated SAN moves that are not allowed.");
  args.add_argument("--restrictTo")
      .default_value("")
      .help("Space separated square names that pieces can move to.");
  args.add_argument("--excludeFrom")
      .default_value("")
      .help("Space separated square names that pieces should never move from.");
  args.add_argument("--excludeTo")
      .default_value("")
      .help("Space separated square names that pieces should never move to.");
  args.add_argument("--excludeCaptures")
      .default_value(false)
      .implicit_value(true)
      .help("Never capture.");
  args.add_argument("--excludeCapturesOf")
      .default_value("")
      .help("String containing piece types that should never be captured, e.g. "
            "\"qrbn\".");
  args.add_argument("--excludeToAttacked")
      .default_value(false)
      .implicit_value(true)
      .help("Never move to attacked squares (including from pinned pieces, but "
            "ignoring en passant).");
  args.add_argument("--excludeToCapturable")
      .default_value(false)
      .implicit_value(true)
      .help("Never move to a square that risks capture (much slower than "
            "--excludeToAttacked).");
  args.add_argument("--excludePromotionTo")
      .default_value("")
      .help("String containing piece types that should never be promoted to, "
            "e.g. \"qrb\".");
  args.add_argument("--excludeAllowingCapture")
      .default_value(false)
      .implicit_value(true)
      .help("Avoid moves that allow a capture somewhere on the board (much "
            "slower than --excludeToAttacked).");
  args.add_argument("--excludeAllowingFrom")
      .default_value("")
      .help("Space separated square names that opponent's pieces should not be "
            "allowed to move from in reply to our move.");
  args.add_argument("--excludeAllowingTo")
      .default_value("")
      .help("Space separated square names that opponent's pieces should not be "
            "allowed to move to in reply to our move.");
  args.add_argument("--excludeAllowingMoves")
      .default_value("")
      .help("Space separated UCI moves that opponent should not be allowed to "
            "make in reply to our move.");
  args.add_argument("--excludeAllowingSANs")
      .default_value("")
      .help("Space separated SAN moves that opponent should not be allowed to "
            "make in reply to our move.");
  args.add_argument("--outFile")
      .default_value("")
      .help("Optional output file for the TB.");
  args.add_argument("--verbose")
      .default_value(0)
      .action([](const std::string &value) { return std::stoi(value); })
      .help(
          "Specify the verbosity level. E.g. --verbose 1 shows PVs for all "
          "legal moves, and --verbose 2 also links to chessdb.cn and bm info.");
  if (use_concurrency)
    args.add_argument("--concurrency")
        .default_value(int(std::thread::hardware_concurrency()))
        .action([](const std::string &value) { return std::stoi(value); })
        .help("Number of concurrent threads to use.");
  try {
    args.parse_args(argc, argv);
  } catch (const std::runtime_error &err) {
    std::cerr << err.what() << std::endl;
    std::cerr << args;
    std::exit(1);
  }
  epdStr = args.get("epd");
  depth = args.get<int>("depth");
  maxNodes = std::stoull(args.get<std::string>("maxNodes"));
  openingMoves = args.get("openingMoves");
  excludeMoves = args.get("excludeMoves");
  excludeSANs = args.get("excludeSANs");
  restrictTo = args.get("restrictTo");
  excludeFrom = args.get("excludeFrom");
  excludeTo = args.get("excludeTo");
  excludeCaptures = args.get<bool>("excludeCaptures");
  excludeCapturesOf = args.get("excludeCapturesOf");
  excludeToAttacked = args.get<bool>("excludeToAttacked");
  excludeToCapturable = args.get<bool>("excludeToCapturable");
  excludePromotionTo = args.get("excludePromotionTo");
  excludeAllowingCapture = args.get<bool>("excludeAllowingCapture");
  excludeAllowingFrom = args.get("excludeAllowingFrom");
  excludeAllowingTo = args.get("excludeAllowingTo");
  excludeAllowingMoves = args.get("excludeAllowingMoves");
  excludeAllowingSANs = args.get("excludeAllowingSANs");
  outFile = args.get("outFile");
  verbose = args.get<int>("verbose");
  if (use_concurrency)
    concurrency = std::max(1, args.get<int>("concurrency"));
  fill_exclude_options();
}

inline void Options::fill_exclude_options() {
  // For some known EPDs, this defines the right exclude commands.
  if (!openingMoves.empty() || !excludeMoves.empty() || !excludeSANs.empty() ||
      !restrictTo.empty() || !excludeFrom.empty() || !excludeTo.empty() ||
      excludeCaptures || !excludeCapturesOf.empty() || excludeToAttacked ||
      excludeToCapturable || !excludePromotionTo.empty() ||
      excludeAllowingCapture || !excludeAllowingFrom.empty() ||
      !excludeAllowingTo.empty() || !excludeAllowingMoves.empty() ||
      !excludeAllowingSANs.empty())
    return;

  auto parts = split(epdStr);
  parts.resize(4);
  std::string epd = join(parts.begin(), parts.end(), " ");

  // clang-format off
  if (epd == "8/2Nb4/pp6/4rp1p/1Pp1pPkP/PpPpR3/1B1P2N1/2K5 b - -" || // bm #-4
      epd == "8/2Nb4/pp6/4rp1p/1Pp1pPkP/PpPpR3/1B1P2N1/1K6 w - -") { // bm #5
    excludeFrom = "f4 h4 d2 c3 a8 e6 b5 a6 g7 d6";
    excludeTo = "f5 h5 b3 c4 d3 e4";
    excludeMoves = "a3a4";
    excludeAllowingFrom = "g4 f5 h5 e4 d3 c4 a1 b1 c1 d1 e1 f1 g1 h1";
    if (depth == MAX_DEPTH)
      depth = 9;
  } else if (epd == "k1R5/1p3R2/p6r/8/1N6/2K5/3P4/4B3 b - -" || // bm #-2
             epd == "k7/1pR2R2/p6r/8/1N6/2K5/3P4/4B3 w - -") {  // bm #3
    excludeFrom = "d2 c3 f7";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a6 b7";
  } else if (epd == "1K3RQ1/ppn3p1/r3NNpq/2p5/2b1kPB1/b5r1/8/2BR4 b - -" ||  // bm #-2
             epd == "1K3RQ1/ppn3p1/r3N1pq/2pN4/2b1kPB1/b5r1/8/2BR4 w - -") { // bm #3
    excludeFrom = "c1 d1 f4 g4 b8 f8";
    excludeAllowingFrom = "a3 g3 c4 e4 c5 a6 g6 a7 b7 c7";
  } else if (epd == "3K4/pp3B2/qrk2B2/bp6/1p1P4/1P6/5P2/8 b - -" || // bm #-2
             epd == "3K4/pp3B2/qrk5/bp2B3/1p1P4/1P6/5P2/8 w - -") { // bm #3
    excludeFrom = "f2 b3 f7 d8";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b4 a5 b5 a6 b6 a7 b7";
  } else if (epd == "8/5p2/3N4/2Pp1P2/BBpk1Pp1/2p2pP1/2p2K2/2N5 b - -" ||  // bm #-2
             epd == "8/5p2/3N4/1BPp1P2/1Bpk1Pp1/2p2pP1/2p2K2/2N5 w - -") { // bm #3
    excludeFrom = "c1 f2 g3 b4 f4 c5 f5";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c2 c3 f3 c4 g4 d5";
  } else if (epd == "R1K4B/4pp1B/N2pnkR1/1p1q2r1/2p2pb1/4p3/P2b4/2rn2N1 b - -" || // bm #-3
             epd == "R1K4B/4ppRB/N2pnk2/1p1q2r1/2p2pb1/4p3/P2b4/2rn2N1 w - -") {  // bm #4
    excludeFrom = "g1 a2 h7 a8 c8 h8";
    excludeAllowingFrom = "c1 d1 d2 e3 c4 f4 g4 b5 d5 g5 d6 e6 e7 f7";
  } else if (epd == "8/3N4/3K1p1p/5pbr/2B2k1p/2P2p1P/2P2P2/8 b - -" || // bm #-3
             epd == "8/3N4/3K1p1p/3B1pbr/5k1p/2P2p1P/2P2P2/8 w - -") { // bm #4
    excludeFrom = "c2 f2 c3 h3 d6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f3 h4 f5 g5 h5 f6 h6";
  } else if (epd == "qnbn1K2/bprk1p2/p1rppP2/pPP1N3/B3P3/B7/8/8 b - -" || // bm #-4
             epd == "qnbn1K2/bprk1p2/p1rppP2/pPP5/B3P3/B2N4/8/8 w - -") { // bm #5
    excludeFrom = "a3 a4 e4 f6 f8";
    excludeAllowingFrom = "a5 a6 c6 e6 c7 d7 f7 b8 c8 d8";
  } else if (epd == "5n2/1Rr1pP2/2p3KP/2k3PR/1p1bP3/1bP5/3N4/3n1Q1q w - -" ||  // bm #-4
             epd == "5B2/1Rr1pP1n/2p3KP/2k3PR/1p1bP3/1bP5/3N4/3n1Q1q b - -") { // bm #5
    excludeFrom = "d1 h1 b4 c5 c6 c7 e7";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d2 c3 e4 g5 h5 h6 b7 f7 f8";
  } else if (epd == "6r1/p6b/K1Np2pq/1Pk5/3pR1Pb/Bn1PpRp1/N3n3/8 b - -" ||  // bm #-4
             epd == "6r1/p6b/K2p2pq/1Pk5/1N1pR1Pb/Bn1PpRp1/N3n3/8 w - -") { // bm #5
    excludeFrom = "a3 d3 f3 e4 g4 b5 a6";
    excludeAllowingFrom = "e2 b3 e3 g3 d4 d6 g6 h6 a7 h7 g8";
  } else if (epd == "5R2/8/5B2/6Pp/7P/5p1p/5P1N/5K1k b - -" || // bm #-4
             epd == "5R2/8/5B2/6Pp/6NP/5p1p/5P2/5K1k w - -") { // bm #5
    excludeFrom = "f1 f2 h4 g5";
    excludeAllowingFrom = "f3 h3 h5";
  } else if (epd == "8/pR1r2b1/q1ppbB2/Pp1k4/5N2/2P5/1K1N1P1r/8 b - -" || // bm #-4
             epd == "8/pR1r2b1/q1ppbB2/Pp1k4/8/2P5/1K1NNP1r/8 w - -") {   // bm #5
    excludeFrom = "b2 f2 a5 f6 b7";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingFrom = "h2 a6 c6 d6 e6 a7 d7 g7";
  } else if (epd == "K2nk1Br/1P2p1NP/p1n1RpNp/4pr2/qP3P2/3pP2P/2b3pR/1Q4B1 b k -" || // bm #-4
             epd == "K2nk1Br/1P2p2P/p1n1RpNp/4pr1N/qP3P2/3pP2P/2b3pR/1Q4B1 w k -") { // bm #5
    excludeFrom = "b1 g1 h2 e3 h3 b4 f4 e6 h7 a8 g8";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c2 g2 d3 a4 e5 f5 a6 c6 f6 h6 e7 h8";
  } else if (epd == "8/1p2p3/1P2K3/1P6/3k4/3B2P1/1P1BP3/8 b - -" || // bm #-4
             epd == "8/1p2p3/1P2K2B/1P6/3k4/3B2P1/1P2P3/8 w - -") { // bm #5
    excludeFrom = "b2 e2 d3 g3 b5 b6 e6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b7 e7";
  } else if (epd == "8/p7/NP6/8/2k5/2N1K3/P2P4/8 b - -" ||  // bm #-4
             epd == "8/p7/N7/1P6/2k5/2N1K3/P2P4/8 w - -") { // bm #5
    excludeFrom = "d2 a6";
    excludePromotionTo = "qrbn";
    excludeAllowingFrom = "c4";
  } else if (epd == "3b3k/7P/6K1/6P1/2N5/8/8/8 b - -" || // bm #-4
             epd == "3b3k/7P/6K1/6P1/8/8/1N6/8 w - -") { // bm #5
    excludeFrom = "g5 g6 h7";
    excludeCaptures = true;
    excludeToAttacked = true;
    excludeAllowingFrom = "h8";
  } else if (epd == "k1N5/1r6/N7/3K4/8/6p1/2P3B1/8 b - -" || // bm #-4
             epd == "k1N5/1r6/N1K5/8/8/6p1/2P3B1/8 w - -") { // bm #5
    excludeFrom = "c2 a6 c8";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g3 a8";
  } else if (epd == "k1K5/8/2P5/1N6/7b/3N4/8/8 b - -" || // bm #-4
             epd == "k1K5/8/2P5/1N2N3/7b/8/8/8 w - -") { // bm #5
    excludeFrom = "b5 c8";
    excludePromotionTo = "qrbn";
    excludeAllowingFrom = "a8";
  } else if (epd == "8/8/7p/R4K1k/8/8/8/8 b - -" || // bm #-5
             epd == "8/8/7p/5K1k/R7/8/8/8 w - -") { // bm #6
    excludeAllowingCapture = true;
    excludeAllowingMoves = "h2h1q";
    if (depth == MAX_DEPTH)
      depth = 11;
  } else if (epd == "K1k5/P1Pp4/1p1P4/8/p2P4/P7/8/8 b - -" || // bm #-5
             epd == "K1k5/P1Pp4/1p1P4/8/p7/P2P4/8/8 w - -") { // bm #6
    excludeFrom = "d6 a7 c7 a8";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d7 c8";
  } else if (epd == "7Q/8/3ppr2/R1b2kr1/2Pn3N/1P1P2P1/3P1K1N/1BB5 b - -" || // bm #-5
             epd == "7Q/8/3ppr2/R1b2kr1/2Pn4/1P1P1NP1/3P1K1N/1BB5 w - -") { // bm #6
    excludeFrom = "b1 f2 b3 g3 c4 a5 h8";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingFrom = "c5 g5 d6 e6 f6";
  } else if (epd == "r2qk2r/ppp2Ppp/1b3n2/8/2Bp4/BnP2N2/P4PPP/RN2R1K1 b kq -" ||  // bm #-5
             epd == "r2qk2r/ppp2ppp/1b2Pn2/8/2Bp4/BnP2N2/P4PPP/RN2R1K1 w kq -") { // bm #6
    excludeFrom = "a1 b1 e1 g1 f2 g2 h2 c3";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b3 d4 b6 f6 a7 b7 c7 f7 g7 h7 a8 d8 h8";
  } else if (epd == "3n4/2p3p1/4prB1/n1P5/p1pP1rp1/4b3/1pPNNp1K/1k5Q b - -" ||   // bm #-5
             epd == "3n4/2p3p1/4prB1/n1P5/p1pP1rp1/4b3/1pP1Np1K/1k3N1Q w - -") { // bm #6
    excludeFrom = "c2 h2 d4 c5 g6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b2 f2 e3 a4 c4 f4 g4 a5 e6 f6 c7 g7 d8";
  } else if (epd == "3n4/2p3p1/4prB1/n1P5/p1pP1rp1/8/1pPNNp1K/1k5Q b - -" ||   // bm #-5
             epd == "3n4/2p3p1/4prB1/n1P5/p1pP1rp1/8/1pP1Np1K/1k3N1Q w - -") { // bm #6
    excludeFrom = "c2 h2 d4 c5 g6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b2 f2 a4 c4 f4 g4 a5 e6 f6 c7 g7 d8";
  } else if (epd == "2B5/8/1kNN4/3K4/8/P7/8/8 b - -" || // bm #-5
             epd == "2B5/8/1kNN4/3K4/8/8/P7/8 w - -") { // bm #6
    excludeFrom = "d5 c6 d6 c8";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "8/2p5/p1N5/2p5/k1K5/p7/P7/8 b - -" || // bm #-5
             epd == "8/2p5/p7/2p5/kNK5/p7/P7/8 w - -") {   // bm #6
    excludePromotionTo = "qrbn";
    excludeAllowingFrom = "a3 a4 c5";
  } else if (epd == "8/8/6kN/8/6NK/5P2/p7/B7 b - -" || // bm #-5
             epd == "8/8/6kN/8/6NK/8/p4P2/B7 w - -") { // bm #6
    excludeFrom = "a1 g4 h4 h6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a2";
  } else if (epd == "8/6pN/3Bp1p1/4p1Pk/8/4P1K1/8/8 b - -" || // bm #-5
             epd == "8/6pN/4p1p1/4p1Pk/8/B3P1K1/8/8 w - -") { // bm #6
    excludeFrom = "g3 g5";
    excludePromotionTo = "qrbn";
    excludeToAttacked = true;
    excludeAllowingFrom = "h5 g6 g7";
  } else if (epd == "8/4p2p/8/8/5B2/8/6p1/4K1kb b - -" || // bm #-6
             epd == "8/4p2p/8/8/8/8/6p1/2B1K1kb w - -") { // bm #7
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g1";
    excludeAllowingMoves = "e6e5 e5e4";
  } else if (epd == "8/7P/8/8/pp6/kp6/1p6/1Kb5 b - -" || // bm #-6
             epd == "8/8/7P/8/pp6/kp6/1p6/1Kb5 w - -") { // bm #7
    excludeFrom = "b1";
    excludeCaptures = true;
    excludePromotionTo = "qrb";
    excludeToCapturable = true;
  } else if (epd == "8/8/5Q2/7k/8/6p1/6p1/6Kb b - -" || // bm #-6
             epd == "8/6Q1/8/7k/8/6p1/6p1/6Kb w - -" || // bm #7
             epd == "4Q3/8/8/8/8/5kp1/6p1/6Kb b - -" || // bm #-6
             epd == "8/8/8/8/Q7/5kp1/6p1/6Kb w - -") {  // bm #7
    excludeFrom = "g1";
    excludeToCapturable = true;
    if (depth == MAX_DEPTH)
      depth = 13;
  } else if (epd == "5brk/4p1p1/4P1Kp/4P1p1/p1p3P1/2P3p1/p5P1/7R b - -" || // bm #-6
             epd == "5brk/4p1p1/4P1Kp/4P1p1/p1p3P1/2P3p1/p5P1/R7 w - -") { // bm #7
    excludeFrom = "g6 g4 g2";
    excludeAllowingTo = "g1 b1";
    excludeAllowingFrom = "a1 h1";
    excludeToCapturable = true;
  } else if (epd == "1k1n4/2p3p1/N3prB1/nNP5/p1pP1rp1/4b3/1pP2p1K/7Q b - -" || // bm #-6
             epd == "1k1n4/2p3p1/4prB1/nNP5/pNpP1rp1/4b3/1pP2p1K/7Q w - -") {  // bm #7
    excludeFrom = "h1 c2 d4 c5 g6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b2 f2 e3 a4 c4 f4 g4 a5 e6 f6 c7 g7 d8";
  } else if (epd == "n2n4/1bp5/q2p4/1r1B4/nr1P4/kQ1K4/P2P4/RR6 b - -" ||  // bm #-6
             epd == "n2n4/1bp5/q2p4/1r1B4/nr1P4/k2K4/P2P4/RR1Q4 w - -") { // bm #7
    excludeFrom = "a1 b1 d2 d3 d4 d5";
    excludeAllowingFrom = "a4 b5 a6 d6 b7 c7 a8 d8";
  } else if (epd == "8/5pB1/6p1/6k1/4NN1r/5K2/8/8 b - -" || // bm #-6
             epd == "8/5pB1/6p1/6k1/5N1r/5KN1/8/8 w - -") { // bm #7
    excludeFrom = "f3 g7";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h4 g6 f7";
  } else if (epd == "n2n4/1bp5/q2p4/1r1B4/pr1P4/kQ1K4/P2P4/RR6 b - -" ||  // bm #-6
             epd == "n2n4/1bp5/q2p4/1r1B4/pr1P4/k2K4/P2P4/RR1Q4 w - -") { // bm #7
    excludeFrom = "a1 b1 d2 d3 d4 d5";
    excludeAllowingFrom = "b5 a6 d6 b7 c7 a8 d8";
  } else if (epd == "5kBK/3p1P1P/3P1p2/5Pp1/5PP1/5P2/8/8 b - -" ||  // bm #-6
             epd == "5kBK/3p1P1P/3P1p2/5Pp1/6PP/7P/8/8 b - -" ||    // bm #-6
             epd == "5kBK/3p1P1P/3P1p2/5Pp1/5pP1/5PP1/8/8 w - -" || // bm #7
             epd == "5kBK/3p1P1P/3P1p2/5Pp1/6Pp/6PP/8/8 w - -") {   // bm #7
    excludeFrom = "d6 f7 h7 g8 h8";
    excludePromotionTo = "qrbn";
    excludeAllowingFrom = "d7 f8";
  } else if (epd == "r2r1Rk1/7p/3Q2p1/8/8/2B1p1n1/p3P2q/R2K4 b - -" ||  // bm #-6
             epd == "r2r2k1/7p/3Q2p1/8/8/2B1p1n1/p3P2q/R2K1R2 w - -") { // bm #7
    excludeFrom = "d1 e2 c3";
    excludeAllowingFrom = "a2 h2 e3 g3 g6 h7 a8 g8";
  } else if (epd == "KBk5/P1P4p/2Pp3P/P6p/2p3BP/2P3p1/6P1/8 b - -" || // bm #-6
             epd == "KBk5/P1P4p/2Pp3P/P6p/2p3rP/2P3pB/6P1/8 w - -") { // bm #7
    excludeFrom = "g2 c6 h6 a7 c7 a8 b8";
    excludePromotionTo = "qrbn";
    excludeAllowingFrom = "g3 g4 h7 c8";
  } else if (epd == "8/8/7Q/5P2/1pP5/pP6/P2nppp1/KBk1bqrb b - -" ||   // bm #-6
             epd == "8/8/7Q/5P2/1pP5/pP1B4/P2nppp1/K1k1bqrb w - -") { // bm #7
    excludeFrom = "a1 a2 b3 c4 f5";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e1 f1 g1 h1 d2 e2 f2 g2 a3 b4";
  } else if (epd == "8/1K2p1p1/3pP1P1/3N1B1p/kB1p1p1P/3P1P2/2P5/8 b - -" || // bm #-6
             epd == "8/1K2p1p1/3pP1P1/3N3p/kB1pBp1P/3P1P2/2P5/8 w - -") {   // bm #7
    excludeFrom = "d3 f3 b4 h4 d5 e6 g6 b7";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d4 f4 h5 d6 e7 g7";
  } else if (epd == "r3rb1k/ppq2pp1/2p3P1/8/2n3P1/2N5/PPP2P2/1KQR3R b - -" ||  // bm #-6
             epd == "r3rb1k/ppq2pp1/2p3P1/8/2n3P1/2N5/PPP2P2/1KQR2R1 w - -") { // bm #7
    excludeFrom = "b1 a2 b2 c2 f2 c3 g4 g6";
    excludeAllowingFrom = "c4 c6 a7 b7 f7 g7 a8 e8 f8";
  } else if (epd == "1r4r1/p2nn2p/2p2p2/P1p2p1P/bp2k1pb/3PP3/1N2K1N1/8 b - -" || // bm #-6
             epd == "1r4r1/p2nn2p/2p2p2/P1p2p1P/bp2k1pb/4P3/1N1PK1N1/8 w - -") { // bm #7
    excludeFrom = "e2 e3 a5 h5";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingFrom = "a4 b4 g4 h4 c5 f5 c6 f6 a7 d7 e7 h7 b8 g8";
  } else if (epd == "8/2p5/2Kp4/3P1p2/1p2kP2/1p2pR2/1P2B3/8 b - -" ||  // bm #-6
             epd == "8/2p5/2Kp4/3P1p2/1p2kP2/1p2p3/1P2B3/5R2 w - -") { // bm #7
    excludeFrom = "b2 e2 f4 d5 c6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b3 e3 b4 f5 d6 c7";
  } else if (epd == "8/2N5/1n1B4/3Q2Kp/3k1pn1/1r3p2/1PPq1P2/5B2 b - -" ||  // bm #-6
             epd == "8/2N5/1n1BQ3/3b2Kp/3k1pn1/1r3p2/1PPq1P2/5B2 w - -") { // bm #7
    excludeFrom = "b2 c2 f2 g5 d6";
    excludeAllowingFrom = "d2 b3 f3 f4 g4 d5 h5";
  } else if (epd == "8/8/N2p2p1/3P2P1/5kPB/3K3P/6P1/8 b - -" ||  // bm #-6
             epd == "8/8/3p2p1/3P2P1/1N3kPB/3K3P/6P1/8 w - -") { // bm #7
    excludeFrom = "g2 d3 h3 g4 h4 d5 g5";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d6 g6";
  } else if (epd == "1k2r3/pp6/4N3/n4b2/8/7P/rq4PB/2R3K1 b - -" ||   // bm #-6
             epd == "1k2r3/pp6/4n3/n4b2/5N2/7P/rq4PB/2R3K1 w - -") { // bm #7
    excludeFrom = "g1 g2 h3";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a2 a5 f5 e6 a7 b7 e8";
  } else if (epd == "8/8/8/B1p2p2/2Pk1P2/3P1p2/2K2p2/5B2 b - -" || // bm #-6
             epd == "8/8/8/2p2p2/2Pk1P2/3P1p2/2KB1p2/5B2 w - -") { // bm #7
    excludeFrom = "f1 c2 d3 c4 f4";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f2 f3 c5 f5";
  } else if (epd == "5rk1/R4Qp1/5nNp/1p3P2/4p3/2q3P1/5PKP/3r4 b - -" ||  // bm #-6
             epd == "5rk1/R4pp1/5nNp/1p3P2/4p3/2q3P1/Q4PKP/3r4 w - -") { // bm #7
    excludeFrom = "f2 g2 h2 g3 f5 g6";
    excludeAllowingFrom = "e4 b5 h6 g7";
  } else if (epd == "1KN5/2NB4/1kpp4/pn2p3/2p5/2Pp1pP1/4p1r1/4b1n1 b - -" || // bm #-6
             epd == "1K6/N1NB4/1kpp4/pn2p3/2p5/2Pp1pP1/4p1r1/4b1n1 w - -") { // bm #7
    excludeFrom = "c3 g3 d7 b8";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e1 g1 e2 g2 d3 f3 c4 a5 b5 e5 c6 d6";
  } else if (epd == "8/8/8/4p1p1/3pPpPb/pN1P1Rp1/P1P3P1/5K1k b - -" || // bm #-6
             epd == "8/8/8/4p1p1/3pPpPb/p2P1Rp1/P1P3P1/N4K1k w - -") { // bm #7
    excludeFrom = "f1 a2 c2 g2 d3 e4 g4";
    excludeCaptures = true;
    excludeAllowingFrom = "a3 d4 f4 h4 e5 g5";
  } else if (epd == "7R/2P1p3/4Pp2/2PK4/2p2P2/4p1pN/2P1B1kp/7b b - -" || // bm #-6
             epd == "7R/4p3/2P1Pp2/2PK4/2p2P2/4p1pN/2P1B1kp/7b w - -") { // bm #7
    excludeFrom = "c2 e2 h3 f4 c5 d5 h8";
    excludeCaptures = true;
    excludeAllowingFrom = "h1 h2 e3 g3";
  } else if (epd == "8/1p1p1pRp/NP1P1P1k/4p3/4P1P1/K1p5/P1P5/8 b - -" || // bm #-6
             epd == "8/1pNp1pRp/1P1P1P1k/4p3/4P1P1/K1p5/P1P5/8 w - -") { // bm #7
    excludeFrom = "a2 c2 a3 e4 g4 f6 g7";
    excludeAllowingFrom = "c3 e5 h6 f7 h7";
  } else if (epd == "1N6/2pp4/p4N2/PbkRK3/nn5r/4b1r1/8/1R3B2 b - -" ||   // bm #-6
             epd == "1N6/2pp4/p4N2/Pbk1K3/nn5r/4b1r1/8/1R1R1B2 w - -") { // bm #7
    excludeFrom = "f1 a5 e5";
    excludeAllowingFrom = "e3 g3 a4 h4 a6 c7 d7";
  } else if (epd == "2K5/4p2B/2p1Pp2/2p1kP2/2P1p3/4P3/2N5/8 b - -" || // bm #-6
             epd == "2K5/4p2B/2p1Pp2/2p1kP2/2P1p3/4P3/8/4N3 w - -") { // bm #7
    excludeFrom = "e3 c4 f5 e6 h7 c8";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e4 c5 c6 f6 e7";
  } else if (epd == "8/4p3/2B1Pp1K/1p2pP2/pPk1P3/P4P2/P3P3/3N4 b - -" || // bm #-6
             epd == "B7/4p3/4Pp1K/1p2pP2/pPk1P3/P4P2/P3P3/3N4 w - -") {  // bm #7
    excludeFrom = "d1 a2 a3 f3 b4 e4 f5 e6 h6";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a4 b5 e5 f6 e7";
  } else if (epd == "8/3p4/1p4p1/1P1N2Nb/KP1kB1p1/PP6/8/4B3 b - -" || // bm #-6
             epd == "8/3p4/1p4p1/1P1N2Nb/KP1kB1p1/PP4B1/8/8 w - -") { // bm #7
    excludeFrom = "a3 b3 a4 b4 e4 b5 d5 g5";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h5 b6 g6";
  } else if (epd == "R5nk/4p1pN/4PpP1/5P2/8/6p1/8/6K1 b - -" || // bm #-6
             epd == "5Rnk/4p1pN/4PpP1/5P2/8/6p1/8/6K1 w - -") { // bm #7
    excludeFrom = "g1 f5 e6 g6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f6 e7 g7 h8";
  } else if (epd == "1q1N4/bp1r1N2/pk6/2Qp1p2/Pp4n1/1P6/1B1K1PnP/5b2 b - -" || // bm #-6
             epd == "1q1N4/bp1r1N2/pk6/3p1p2/Pp4n1/1P6/1BQK1PnP/5b2 w - -") {  // bm #7
    excludeFrom = "d2 h2 b3 a4";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingFrom = "f1 g2 b4 g4 d5 f5 a6 a7 b7 d7 b8";
  } else if (epd == "4r2k/1p4rQ/2p5/p1bq3p/3B1P2/6R1/P4K2/6R1 b - -" ||   // bm #-6
             epd == "4r2k/1p4r1/2p5/p1bq3p/3B1P2/3Q2R1/P4K2/6R1 w - -") { // bm #7
    excludeFrom = "a2 f2 d4 f4";
    excludeAllowingFrom = "a5 c5 d5 h5 c6 b7 g7 e8";
  } else if (epd == "8/3p4/8/3p1Np1/3P2B1/5Pk1/3P2p1/6Kb b - -" ||  // bm #-6
             epd == "8/3p2N1/8/3p2p1/3P2B1/5Pk1/3P2p1/6Kb w - -") { // bm #7
    excludeFrom = "g1 d2 f3 d4 g4";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 g2 d5 g5";
  } else if (epd == "8/4p1pN/2p3p1/2p1p1P1/4P1P1/2P1P3/p7/k1K5 b - -" || // bm #-6
             epd == "8/4p1pN/2p3p1/2p1p1P1/4P1P1/2P1P3/p7/k2K4 w - -") { // bm #7
    excludeFrom = "c3 e3 e4 g4 g5";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 a2 e5 g6";
  } else if (epd == "7R/8/8/1P2p3/4p1p1/6p1/6p1/4K1kb b - -" || // bm #-6
             epd == "7R/8/8/4p3/1P2p1p1/6p1/6p1/4K1kb w - -") { // bm #7
    excludeFrom = "e1";
    excludeAllowingFrom = "g1 h1 g2 g3";
  } else if (epd == "B7/8/2R5/4k1p1/6P1/1P2K3/8/8 b - -" || // bm #-6
             epd == "B7/8/2R5/4k1p1/6P1/4K3/1P6/8 w - -") { // bm #7
    excludeFrom = "e3 g4 c6 a8";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g5";
  } else if (epd == "3R4/p7/p5p1/3n1pP1/P2k1P2/BKp2P2/2P2P2/8 b - -" ||   // bm #-6
             epd == "3R1B2/p7/p5p1/3n1pP1/P2k1P2/1Kp2P2/2P2P2/8 w - -") { // bm #7
    excludeFrom = "c2 f2 f3 a4 f4 g5";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c3 d5 f5 g6";
  } else if (epd == "8/3p4/p2P4/P2Pp3/P2kP2R/1p1N1p2/1P1K1P2/8 b - -" ||  // bm #-6
             epd == "8/3p4/p2P4/P2Pp3/P2kP1R1/1p1N1p2/1P1K1P2/8 w - -") { // bm #7
    excludeFrom = "b2 d2 f2 d3 a4 e4 a5 d5 d6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b3 f3 e5 a6 d7";
  } else if (epd == "8/4p1p1/2p1N1p1/2p1p1P1/4P3/2P1P3/p7/k1K5 b - -" || // bm #-6
             epd == "8/4p1p1/2p1N1p1/2p1p1P1/4P3/2P1P3/p7/k2K4 w - -") { // bm #7
    excludeFrom = "c3 e3 e4 g5";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 a2 e5 g6";
  } else if (epd == "5brq/4p1kp/2B1K2p/7p/1N5P/8/8/8 b - -" || // bm #-6
             epd == "5brq/4p1kp/4K2p/7p/1N2B2P/8/8/8 w - -") { // bm #7
    excludeFrom = "h4 e6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h5 h6 e7 h7 f8 g8 h8";
  } else if (epd == "8/8/8/4p1p1/3pPpPb/pP1P1Rp1/P1N3P1/5K1k b - -" || // bm #-6
             epd == "8/8/8/4p1p1/3pPpPb/pP1P1Rp1/P5P1/N4K1k w - -") {  // bm #7
    excludeFrom = "f1 a2 g2 b3 d3 e4 g4";
    excludeAllowingFrom = "a3 d4 f4 h4 e5 g5";
  } else if (epd == "8/8/3B4/5p2/3Pp3/4p2P/7p/4KBkr b - -" ||  // bm #-6
             epd == "8/8/3B4/5p2/3Pp3/4p2P/4K2p/5Bkr w - -") { // bm #7
    excludeFrom = "f1 h3 d4";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g1 h1 h2";
  } else if (epd == "8/p7/K7/8/8/7p/p4R2/R5bk b - -" || // bm #-6
             epd == "8/pK6/8/8/8/7p/p4R2/R5bk w - -") { // bm #7
    excludeFrom = "a1 f2";
    excludeToAttacked = true;
    excludeAllowingFrom = "h1 a2";
  } else if (epd == "8/8/4K3/1p1N4/1N1k4/3P1P2/8/8 b - -" || // bm #-6
             epd == "8/8/4K3/1p1N4/1N1k4/3P4/5P2/8 w - -") { // bm #7
    excludeFrom = "d3 b4 d5 e6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b5";
  } else if (epd == "8/8/8/8/8/1Np1PK2/2P4N/1B1k4 b - -" ||  // bm #-6
             epd == "8/8/8/8/8/1Np2K2/2P1P2N/1B1k4 w - -") { // bm #7
    excludeFrom = "b1 c2 h2 b3 f3";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c3";
  } else if (epd == "6kN/6B1/8/5N2/1P6/8/8/K7 b - -" || // bm #-6
             epd == "6kN/6B1/8/5N2/8/8/1P6/K7 w - -") { // bm #7
    excludeFrom = "a1 f5 g7 h8";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "1k5Q/2p1pp2/p2bp3/1n4p1/r1p3P1/P5pR/P2K2Bq/1R6 b - -" || // bm #-6
             epd == "1k6/2p1pp2/p2bp3/1n4pQ/r1p3P1/P5pR/P2K2Bq/1R6 w - -") {  // bm #7
    excludeFrom = "a2 d2 g2 a3 h3 g4";
    excludeAllowingFrom = "h2 g3 a4 c4 b5 g5 d6 e6 c7 e7 f7";
  } else if (epd == "Rk1r3r/1ppq2pp/3bbpn1/1p6/3NP3/4B2P/2PN1PP1/3Q1RK1 b - -" ||  // bm #-6
             epd == "1k1r3r/1ppq2pp/3bbpn1/1p6/3NP3/4B2P/2PN1PP1/R2Q1RK1 w - -") { // bm #7
    excludeFrom = "g1 c2 d2 f2 g2 e3 h3 e4";
    excludeAllowingFrom = "b5 f6 g6 b7 c7 d7 g7 h7 d8 h8";
  } else if (epd == "8/8/1p6/8/1P5p/4B1pr/4K1kp/R6b b - -" ||  // bm #-6
             epd == "8/8/1p6/8/1P5p/4B1pr/4K1kp/4R2b w - -") { // bm #7
    excludeFrom = "e2 b4";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 h2 g3 h3 h4";
  } else if (epd == "5R2/6p1/5pPb/5Pp1/3p2P1/3P2p1/5pPk/5K2 b - -" || // bm #-6
             epd == "8/5Pp1/5pPb/5Pp1/3p2P1/3P2p1/5pPk/5K2 w - -") {  // bm #7
    excludeFrom = "f1 g2 d3 g4 f5 g6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f2 g3 d4 g5 f6 h6 g7";
  } else if (epd == "8/2K5/p1pBp2p/p1PkPp2/5P2/2pPP3/P1P4P/8 b - -" || // bm #-6
             epd == "3K4/8/p1pBp2p/p1PkPp2/5P2/2pPP3/P1P4P/8 w - -") { // bm #7
    excludeFrom = "a2 h2 d3 e3 f4 c5 e5 d6";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c3 d5 f5 c6 e6";
  } else if (epd == "1k6/1B6/8/N2N4/8/2P5/6p1/6Kb b - -" || // bm #-6
             epd == "1k6/1B6/8/N2N4/8/8/2P3p1/6Kb w - -") { // bm #7
    excludeFrom = "g1 d5 b7";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 g2";
  } else if (epd == "7B/p7/2p5/P1P5/2P1p3/pP2P3/k1pN4/1bK3R1 b - -" ||  // bm #-6
             epd == "8/p7/2p5/P1P1B3/2P1p3/pP2P3/k1pN4/1bK3R1 w - -") { // bm #7
    excludeFrom = "c1 d2 b3 e3 c4 a5 c5";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b1 c2 a3 e4 c6";
  } else if (epd == "8/P1p5/2P3p1/1BP3p1/P7/1K3pB1/5Pp1/6kb b - -" || // bm #-6
             epd == "8/2p5/P1P3p1/1BP3p1/P7/1K3pB1/5Pp1/6kb w - -") { // bm #7
    excludeFrom = "f2 b3 g3 a4 b5 c5";
    excludeAllowingFrom = "h1 g2 f3";
  } else if (epd == "8/3p1p2/p2Pkp2/Pp3Np1/1p1P2P1/1P6/4BP2/K7 b - -" ||  // bm #-6
             epd == "8/3p1p2/p2Pkp2/Pp3Np1/1p1P2P1/1P3B2/5P2/K7 w - -") { // bm #7
    excludeFrom = "a1 b3 g4 a5 f5 d6";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b4 b5 g5 a6 f6 d7 f7";
  } else if (epd == "b1K5/B3R3/1Np1n3/2P1k1P1/6P1/4PP2/8/8 b - -" || // bm #-6
             epd == "bK6/B3R3/1Np1n3/2P1k1P1/6P1/4PP2/8/8 w - -") {  // bm #7
    excludeFrom = "e3 f3 g4 c5 g5 b6 e7";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e5 c6 e6";
  } else if (epd == "3b4/8/5p2/3K1k2/6R1/7P/3N4/8 b - -" || // bm #-6
             epd == "3b4/8/5p2/3K1k2/6R1/7P/8/1N6 w - -") { // bm #7
    excludeFrom = "h3 g4 d5";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f5 f6";
  } else if (epd == "1N6/8/1K2p3/2N1B3/2k5/P7/8/8 b - -" || // bm #-6
             epd == "8/8/NK2p3/2N1B3/2k5/P7/8/8 w - -") {   // bm #7
    excludeFrom = "a3 c5 e5 b6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e6";
  } else if (epd == "5r2/8/8/8/B7/5P1k/7p/4B2K b - -" || // bm #-6
             epd == "5r2/8/8/8/8/5P1k/7p/3BB2K w - -") { // bm #7
    excludeFrom = "e1 h1 f3";
    excludeAllowingFrom = "h2 h3";
  } else if (epd == "8/2K5/pp1N1B2/qrk2P2/bp6/1p1B4/1P3P2/8 b - -" || // bm #-6
             epd == "8/2K5/pp1N1B2/qrk2P2/bp2B3/1p6/1P3P2/8 w - -") { // bm #7
    excludeFrom = "b2 f2 f5 f6 c7";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b3 a4 b4 a5 b5 a6 b6";
  } else if (epd == "8/5N2/8/1P1k1P2/6pp/1NbP2pq/2P3rp/2K4B b - -" || // bm #-6
             epd == "8/5N2/8/1P1k1P2/6pp/1NbP2pq/2P3rp/3K3B w - -") { // bm #7
    excludeFrom = "h1 b3 d3 b5 f5 f7";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g2 h2 g3 h3 g4 h4 d5";
  } else if (epd == "8/3K4/3N1B2/3k1p2/5P2/2P1p3/4P3/8 b - -" || // bm #-6
             epd == "8/3K4/3N4/3k1p2/3B1P2/2P1p3/4P3/8 w - -") { // bm #7
    excludeFrom = "e2 f4 d6 d7";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e3 f5";
  } else if (epd == "4Q3/8/8/8/8/5kp1/5prp/5Kbb b - -" || // bm #-6
             epd == "8/8/6Q1/8/8/5kp1/5prp/5Kbb w - -") { // bm #7
    excludeFrom = "f1";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g1 h1 f2 g2 h2 g3";
  } else if (epd == "8/1p2p3/1K2P3/8/kp1pN3/3P4/1PP5/8 b - -" || // bm #-6
             epd == "8/1p2p3/1K2P3/8/kp1p4/3P4/1PPN4/8 w - -") { // bm #7
    excludeFrom = "b2 d3 b6 e6";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d4 b7 e7";
  } else if (epd == "8/7p/6pP/1pK2pP1/3BpP2/pP2p3/p3N3/rk1B4 b - -" || // bm #-6
             epd == "8/7p/6pP/1pK2pP1/4pP2/pPB1p3/p3N3/rk1B4 w - -") { // bm #7
    excludeFrom = "e2 b3 f4 g5 h6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 a2 a3 e3 f5 g6 h7";
  } else if (epd == "5Q2/1Kp3rp/6pk/6rq/5Bpb/6p1/6P1/8 b - -" || // bm #-6
             epd == "5Q2/2p3rp/K5pk/6rq/5Bpb/6p1/6P1/8 w - -") { // bm #7
    excludeFrom = "g2 f4";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g3 g4 h4 g5 h5 g6 h6 g7 h7";
  } else if (epd == "8/8/8/1p1p4/1P6/2pP4/B2pK3/N1kN4 b - -" || // bm #-6
             epd == "8/8/8/1p1p4/8/1PpP4/B2pK3/N1kN4 w - -") {  // bm #7
    excludeFrom = "d1 a2 e2";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d2 b5";
  } else if (epd == "8/8/8/2N5/4p3/4P3/4bK1p/7k b - -" || // bm #-6
             epd == "8/8/8/8/N3p3/4P3/4bK1p/7k w - -") {  // bm #7
    excludeFrom = "f2 e3";
    excludeToAttacked = true;
  } else if (epd == "8/8/1p2R3/1p5B/1K1k4/8/4P3/8 b - -" ||  // bm #-6
             epd == "8/8/1p2R3/1p6/1K1k2B1/8/4P3/8 w - -") { // bm #7
    excludeFrom = "b4 e6";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b5 b6";
  } else if (epd == "8/8/1B1R2p1/6p1/4k1K1/8/3P4/8 b - -" || // bm #-6
             epd == "8/8/3R2p1/2B3p1/4k1K1/8/3P4/8 w - -") { // bm #7
    excludeFrom = "g4 d6";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g5 g6";
  } else if (epd == "r7/ppp1bQ2/2n4p/3k4/5P2/2K5/PPPN4/q5B1 b - -" || // bm #-6
             epd == "r7/ppp1b2Q/2n4p/3k4/5P2/2K5/PPPN4/q5B1 w - -") { // bm #7
    excludeFrom = "g1 a2 b2 c2 c3 f4";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 c6 h6 a7 b7 c7 e7 a8";
  } else if (epd == "8/3B4/8/2K1k1B1/6P1/8/2P2P2/8 b - -" || // bm #-6
             epd == "2B5/8/8/2K1k1B1/6P1/8/2P2P2/8 w - -") { // bm #7
    excludeFrom = "c2 g4 c5 g5";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "8/8/5p1p/5P1P/8/6R1/ppp1K3/rbk3b1 b - -" || // bm #-6
             epd == "8/8/5p1p/5P1P/8/7R/ppp1K3/rbk3b1 w - -") {  // bm #7
    excludeFrom = "e2 f5 h5";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 b1 c1 a2 b2 c2 f6 h6";
  } else if (epd == "8/8/p2R4/p5B1/K1k5/8/1P6/8 b - -" || // bm #-6
             epd == "8/8/p2R4/p7/K1k2B2/8/1P6/8 w - -") { // bm #7
    excludeFrom = "a4 d6";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a5 a6";
  } else if (epd == "8/p4p1p/P7/5K2/6Pk/7p/P6P/8 b - -" || // bm #-6
             epd == "8/p4p1p/P7/8/4K1Pk/7p/P6P/8 w - -") { // bm #7
    excludeFrom = "a6";
    excludePromotionTo = "qrbn";
    excludeAllowingFrom = "h3 h4 a7";
  } else if (epd == "BK6/8/p7/8/P7/4N1B1/8/6k1 b - -" || // bm #-6
             epd == "1K6/8/p7/8/P7/4NBB1/8/6k1 w - -") { // bm #7
    excludeFrom = "e3 g3 a4";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "8/Q7/8/1r6/kp6/bp6/1p6/1K6 b - -" ||  // bm #-7
             epd == "8/3Q4/8/1r6/kp6/bp6/1p6/1K6 w - -") { // bm #8
    excludeFrom = "b1";
    excludeTo = "b3";
    excludeToCapturable = true;
    if (depth == MAX_DEPTH)
      depth = 15;
  } else if (epd == "2rbQ3/n2k1r2/1q1P4/2pn1P2/4N1p1/1P4P1/2B1R3/2K5 b - -" || // bm #-7
             epd == "2rb4/n2k1r2/1q1P4/2pnQP2/4N1p1/1P4P1/2B1R3/2K5 w - -") {  // bm #8
    excludeFrom = "c1 c2 b3 g3 f5 d6";
    excludeCaptures = true;
    excludeAllowingFrom = "g4 c5 d5 b6 a7 f7 c8 d8";
  } else if (epd == "8/p1R5/3pnp1r/1p1k1r2/2pnb3/4N1P1/2P5/5BK1 b - -" || // bm #-7
             epd == "8/p1R5/3pnp1r/1p1k1r2/2pnb3/6P1/2P5/3N1BK1 w - -") { // bm #8
    excludeFrom = "g1 g3 c7";
    excludePromotionTo = "qrbn";
    excludeAllowingFrom = "c4 d4 e4 f5 d6 e6 f6 h6 a7";
  } else if (epd == "7K/8/2p1p3/3p4/1P1k1P2/1P3P1p/1PP2P1B/8 b - -" ||  // bm #-7
             epd == "7K/8/2p1p3/3p4/1P1k1P2/1P3P1p/1PP2P2/6B1 w - -") { // bm #8
    excludeFrom = "c2 h8";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h3 d4 d5";
  } else if (epd == "8/3N4/4K1p1/6p1/5kp1/2P3r1/2PP2PB/8 b - -" || // bm #-7
             epd == "5N2/8/4K1p1/6p1/5kp1/2P3r1/2PP2PB/8 w - -") { // bm #8
    excludeFrom = "c2 d2 g2 h2 c3 e6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g3 g4 g5 g6";
  } else if (epd == "k5R1/r2p4/8/P1n2p2/2p5/2Pp3p/3P3B/R3K3 b Q -" || // bm #-7
             epd == "kB4R1/r2p4/8/P1n2p2/2p5/2Pp3p/3P4/R3K3 w Q -") { // bm #8
    excludeFrom = "a1 d2 c3 a5";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d3 h3 c4 c5 f5 a7 d7";
  } else if (epd == "8/3pp2N/3pp1p1/3pk3/3p2P1/N2P1K2/2P5/B7 b - -" || // bm #-7
             epd == "8/3pp2N/3pp1p1/3pk3/3p2P1/3P1K2/2P5/BN6 w - -") { // bm #8
    excludeFrom = "c2 d3 f3 g4";
    excludeCaptures = true;
    excludeAllowingFrom = "e5 e6";
  } else if (epd == "q7/2NBp3/4P3/1ppk2Kn/1br3N1/1p2pP2/4P3/8 b - -" ||  // bm #-7
             epd == "q3N3/3Bp3/4P3/1ppk2Kn/1br3N1/1p2pP2/4P3/8 w - -") { // bm #8
    excludeFrom = "f3 g5 e6 d7";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b3 e3 b4 c4 b5 c5 h5 e7 a8";
  } else if (epd == "8/5p2/3p1p2/2pP1Pp1/2p2NPp/2p2N1P/2P5/5K1k b - -" ||  // bm #-7
             epd == "8/5p2/3p1p2/2pP1Pp1/2p3Pp/2p2N1P/2P1N3/5K1k w - -") { // bm #8
    excludeFrom = "f1 c2 d5";
    excludePromotionTo = "qrbn";
    excludeAllowingFrom = "h1 c3 c4 c5 d6 f7";
  } else if (epd == "3kr2q/6Q1/8/8/8/3R4/8/K7 b - -" || // bm #-7
             epd == "3kr2q/6Q1/8/8/8/4R3/8/K7 w - -") { // bm #8
    excludeFrom = "a1";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e8 h8";
  } else if (epd == "8/7p/6pB/6Pk/6p1/3p2p1/p2P2P1/R6K b - -" || // bm #-7
             epd == "8/7p/6pB/6Pk/6p1/3p2p1/p2P2P1/6RK w - -") { // bm #8
    excludeFrom = "d2 g2 g5 h6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a2 d3 g3 g4 g6 h7";
  } else if (epd == "8/p1p1p1p1/P1P1P1PN/8/8/3p1P1p/3P1p1p/5Kbk b - -" ||   // bm #-7
             epd == "8/p1p1p1p1/P1P1P1P1/8/6N1/3p1P1p/3P1p1p/5Kbk w - -") { // bm #8
    excludeFrom = "f1 d2 a6 c6 e6";
    excludeAllowingFrom = "g1 h1 f2 h2 d3 h3 a7 c7";
  } else if (epd == "8/3K4/p5p1/Pp1k2P1/1p1Pp3/1N2P3/PP2P3/8 b - -" || // bm #-7
             epd == "8/3K4/p5p1/Pp1k2P1/1p1Pp3/4P3/PP1NP3/8 w - -") {  // bm #8
    excludeFrom = "b2 e2 e3 d4 a5 g5 d7";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e4 a6 g6";
  } else if (epd == "8/8/2K5/p7/6p1/p3NpPp/P2N1P1p/R5nk b - -" ||  // bm #-7
             epd == "8/8/2K5/p7/6p1/p3NpPp/P2N1P1p/4R1nk w - -") { // bm #8
    excludeFrom = "a2 f2 e3 g3 c6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 h2 a3 f3 h3 g4";
  } else if (epd == "8/2p5/8/2p5/8/7P/p7/k1K5 b - -" || // bm #-7
             epd == "8/2p5/8/2p5/8/7P/p2K4/k7 w - -") { // bm #8
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 a2";
  } else if (epd == "4N3/6p1/6P1/3p3B/1p6/1p1PpPP1/pP2P1pp/R3K1kq b Q -" || // bm #-7
             epd == "8/4P1p1/6P1/3p3B/1p6/1p1PpPP1/pP2P1pp/R3K1kq w Q -") { // bm #8
    excludeFrom = "a1 b2 e2 d3 g3 h5";
    excludeCaptures = true;
    excludeAllowingFrom = "h1 a2 g2 h2 b3 e3 b4";
  } else if (epd == "3K4/5P1p/B6P/p7/8/5p1p/Pp2npbr/bR2nkrq b - -" || // bm #-7
             epd == "3K4/7p/B4P1P/p7/8/5p1p/Pp2npbr/bR2nkrq w - -") { // bm #8
    excludeFrom = "b1 a2 a6 d8";
    excludeAllowingFrom = "a1 e1 f1 g1 h1 b2 f2 g2 h2 f3 h3";
  } else if (epd == "8/8/4Bp2/1p3p2/pP1k1P2/K3p3/1P2P3/7R b - -" ||  // bm #-7
             epd == "8/8/4Bp2/1p3p2/pP1k1P2/K3p3/1P2P3/5R2 w - -") { // bm #8
    excludeFrom = "b2 e2 a3 b4 f4 e6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e3 a4 b5 f5";
  } else if (epd == "8/5N1B/1p6/1P1k1P2/K3p3/1P2P3/8/8 b - -" ||  // bm #-7
             epd == "6B1/5N2/1p6/1P1k1P2/K3p3/1P2P3/8/8 w - -") { // bm #8
    excludeFrom = "e3 a4 b5 f5 f7";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e4 b6";
  } else if (epd == "7N/3p4/3p4/3p4/3p4/8/p1pP4/kbK5 b - -" || // bm #-7
             epd == "8/3p3P/3p4/3p4/3p4/8/p1pP4/kbK5 w - -") { // bm #8
    excludeFrom = "c1 d2";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 b1 a2 c2";
  } else if (epd == "K7/8/p6p/p1p4p/4pP2/2N1B2B/P2PP2P/4k3 b - f3" || // bm #-7
             epd == "K7/8/p6p/p1p4p/4p3/2N1B2B/P2PPP1P/4k3 w - -") {  // bm #8
    excludeFrom = "a2 d2 h2 c3 e3 h3 a8";
    excludeAllowingFrom = "e1";
  } else if (epd == "1KB5/pp6/qrkB4/pbP2P2/p1p5/P1P2P2/4P3/8 b - -" || // bm #-7
             epd == "1K6/pp6/qrkBB3/pbP2P2/p1p5/P1P2P2/4P3/8 w - -") { // bm #8
    excludeFrom = "a3 c3 f3 c5 f5 d6 b8";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a4 c4 a5 b5 a6 b6 a7 b7";
  } else if (epd == "2B5/5p2/5P2/3p4/1K1Np3/3kBp2/5P2/3b4 b - -" || // bm #-7
             epd == "8/5p2/5P2/3p4/1K1Np3/3kBp1B/5P2/3b4 w - -") {  // bm #8
    excludeFrom = "f2 e3 b4 d4 f6";
    excludeToAttacked = true;
  } else if (epd == "8/8/3p4/2pPp3/1pB1PpK1/pn1N1Pp1/k4pP1/5Rb1 b - -" || // bm #-7
             epd == "8/8/3p4/2pPp3/1pB1Pp2/pn1N1PpK/k4pP1/5Rb1 w - -") {  // bm #8
    excludeFrom = "f1 g2 d3 f3 e4 d5";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a2 f2 a3 b3 g3 b4 f4 c5 e5 d6";
  } else if (epd == "8/2p5/1p1p4/1B1k1K2/1N1p4/N7/1p6/8 b - -" || // bm #-7
             epd == "8/2p5/1p1p4/1B1k1K2/3p4/N2N4/1p6/8 w - -") { // bm #8
    excludeFrom = "b5 f5";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d4 b6 d6 c7";
  } else if (epd == "6R1/5P2/5p2/8/4pP2/3pPp2/3PpPp1/4K1kb b - -" || // bm #-7
             epd == "R7/5P2/5p2/8/4pP2/3pPp2/3PpPp1/4K1kb w - -") {  // bm #8
    excludeFrom = "e1 d2 f2 e3 f4";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 e2 g2 d3 f3 e4";
  } else if (epd == "5K2/5p1p/8/7p/7p/7p/R6p/R5bk b - -" || // bm #-7
             epd == "4K3/5p1p/8/7p/7p/7p/R6p/R5bk w - -") { // bm #8
    excludeFrom = "a1";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g1 h1 h2 h3 h4";
  } else if (epd == "1N6/2R5/8/8/8/6p1/3K2p1/6k1 b - -" || // bm #-7
             epd == "1N6/2R5/8/8/8/4K1p1/6p1/6k1 w - -") { // bm #8
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g2 g3";
  } else if (epd == "1k6/1r6/2K5/Q7/8/8/8/8 b - -" || // bm #-7
             epd == "1k6/1r6/2K5/8/8/8/8/Q7 w - -") { // bm #8
    excludeFrom = "c6";
    excludeAllowingCapture = true;
  } else if (epd == "1k6/8/3N4/1N6/6pK/8/8/7B b - -" ||  // bm #-7
             epd == "1k6/8/3N4/1N6/4B1pK/8/8/8 w - -") { // bm #8
    excludeFrom = "b5 d6";
    excludeAllowingCapture = true;
  } else if (epd == "1r6/8/8/5B2/1P3p2/p7/3p1pp1/3Kbkrb b - -" || // bm #-7
             epd == "1r6/8/8/8/1P3p2/p6B/3p1pp1/3Kbkrb w - -") {  // bm #8
    excludeFrom = "d1 b4";
    excludeToCapturable = true;
  } else if (epd == "1r6/8/8/8/1p6/8/4Bp1p/5K1k b - -" || // bm #-7
             epd == "1r6/8/B7/8/1p6/8/5p1p/5K1k w - -") { // bm #8
    excludeFrom = "f1";
    excludeAllowingCapture = true;
  } else if (epd == "2B5/2p5/2P3p1/1K4p1/4p1p1/2k1P1P1/P1p5/2B5 b - -" || // bm #-7
             epd == "8/2p5/B1P3p1/1K4p1/4p1p1/2k1P1P1/P1p5/2B5 w - -") {  // bm #8
    excludeFrom = "c1 a2 e3 g3 c6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c2 e4 g4 c7";
  } else if (epd == "2N1K1B1/2p5/2p5/p1k4p/P7/2P2P2/1P5P/8 b - -" || // bm #-7
             epd == "2N1K3/2p5/2p5/p1k4p/P7/1BP2P2/1P5P/8 w - -") {  // bm #8
    excludeFrom = "h2 c3 f3 a4 c8";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a5 c7";
  } else if (epd == "2b5/1p1p1p2/1P1k1P2/P7/1N1B2P1/3NK3/8/8 b - -" ||  // bm #-7
             epd == "2b5/1p1p1p2/1P1k1P2/P1N5/1N1B2P1/4K3/8/8 w - -") { // bm #8
    excludeFrom = "e3 g4 a5 b6 f6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b7 d7 f7 c8";
  } else if (epd == "2k5/2p4Q/2P3K1/8/8/8/2p5/8 b - -" || // bm #-7
             epd == "2k5/2p4Q/6K1/2P5/8/8/2p5/8 w - -") { // bm #8
    excludeFrom = "g6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c7";
  } else if (epd == "2n5/8/5N2/1p5p/3pBp2/3PkP2/8/4K3 b - -" || // bm #-7
             epd == "2n1N3/8/8/1p5p/3pBp2/3PkP2/8/4K3 w - -") { // bm #8
    excludeFrom = "e1 d3 f3 e4";
    excludeAllowingCapture = true;
  } else if (epd == "3N4/2p3pp/6kr/r6p/2K4B/8/8/1B6 b - -" || // bm #-7
             epd == "3N4/2p3pp/6kr/r6p/7B/3K4/8/1B6 w - -") { // bm #8
    excludeFrom = "h4 d8";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h5 g6 h6 g7 h7";
  } else if (epd == "3N4/kBp5/2p5/b1p5/1pP5/1K4P1/8/8 b - -" || // bm #-7
             epd == "3N4/kBp5/2p5/b1p5/1pP5/1K6/6P1/8 w - -") { // bm #8
    excludeFrom = "b3 c4 b7 d8";
    excludePromotionTo = "qrb";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b4 c5 c6 c7";
  } else if (epd == "3b4/K1k1p3/3pP3/pB1p4/P2P4/5p2/5P2/6B1 b - -" || // bm #-7
             epd == "3bB3/K1k1p3/3pP3/p2p4/P2P4/5p2/5P2/6B1 w - -") { // bm #8
    excludeFrom = "f2 a4 d4 e6 a7";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f3 a5 d5 e7 d8";
  } else if (epd == "5B2/1p1p1p2/1p1P1P2/1p2pPp1/1P2PpB1/8/5K1k/8 b - -" || // bm #-7
             epd == "8/1p1pBp2/1p1P1P2/1p2pPp1/1P2PpB1/8/5K1k/8 w - -") {   // bm #8
    excludeFrom = "f2 b4 e4 f5 d6 f6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b5 e5 g5 b6 b7 d7 f7";
  } else if (epd == "6B1/pp5p/bp1N1p2/1p3P1P/1P1P4/3PP3/1P1P4/k1K5 b - -" || // bm #-7
             epd == "8/pp5p/bp1N1p2/1p1B1P1P/1P1P4/3PP3/1P1P4/k1K5 w - -") { // bm #8
    excludeFrom = "c1 b2 d2 d3 e3 b4 f5 h5";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "6BR/8/2p2p2/5P2/2P5/4pK2/4p1p1/4Rbk1 b - -" ||  // bm #-7
             epd == "6B1/8/2p2p2/5P2/2P5/4pK1R/4p1p1/4Rbk1 w - -") { // bm #8
    excludeFrom = "e1 f3 c4 f5";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f1 e2 g2 e3 f6";
  } else if (epd == "6N1/8/5ppp/5p1r/5pkn/5p1b/5Pp1/6K1 b - -" || // bm #-7
             epd == "8/6P1/5ppp/5p1r/5pkn/5p1b/5Pp1/6K1 w - -") { // bm #8
    excludeFrom = "g1 f2";
    excludePromotionTo = "qrb";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g2 f3 f4 h4 f5 f6 h6";
  } else if (epd == "6b1/5p1k/5B2/6PN/8/8/4K3/8 b - -" ||   // bm #-7
             epd == "6b1/5p1k/5B2/6P1/5N2/8/4K3/8 w - -") { // bm #8
    excludeFrom = "g5 f6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "6br/2K2prq/p4P1b/6p1/7k/P6P/R4BP1/8 b - -" || // bm #-7
             epd == "6br/B1K2prq/p4P1b/6p1/7k/P6P/R5P1/8 w - -") { // bm #8
    excludeFrom = "a3 h3 f6 c7";
    excludeCaptures = true;
    excludeToCapturable = true;
    excludeAllowingFrom = "g5 h6 f7 g7 h7 g8 h8";
  } else if (epd == "6k1/8/4NBB1/8/p7/8/8/1K6 b - -" || // bm #-7
             epd == "6k1/8/4NBB1/8/p7/8/8/K7 w - -") {  // bm #8
    excludeFrom = "e6 g6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "6kr/4K2p/3N3B/8/8/8/6b1/8 b - -" || // bm #-7
             epd == "6kr/4K2p/7B/8/2N5/8/6b1/8 w - -") { // bm #8
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h7 g8 h8";
  } else if (epd == "7R/8/8/5p2/4pPp1/4P3/3N1Np1/K3B1k1 b - -" || // bm #-7
             epd == "8/8/8/5p2/4pPpR/4P3/3N1Np1/K3B1k1 w - -") {  // bm #8
    excludeFrom = "a1 e1 d2 e3 f4";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g2 e4 f5";
  } else if (epd == "7k/6pP/8/6N1/2P1p3/BK2P3/8/1b6 b - -" ||   // bm #-7
             epd == "5B1k/6pP/8/6N1/2P1p3/1K2P3/8/1b6 w - -") { // bm #8
    excludeFrom = "e3 c4 g5 h7";
    excludeToCapturable = true;
    excludeAllowingFrom = "e4 h8";
  } else if (epd == "7k/6pP/8/K5N1/4p3/BP2P3/8/1b6 b - -" ||   // bm #-7
             epd == "5B1k/6pP/8/K5N1/4p3/1P2P3/8/1b6 w - -") { // bm #8
    excludeFrom = "b3 e3 g5 h7";
    excludeToCapturable = true;
    excludeAllowingFrom = "e4 h8";
  } else if (epd == "7k/p6P/7K/8/1P6/8/P7/8 b - -" || // bm #-7
             epd == "7k/p6P/7K/8/8/1P6/P7/8 w - -") { // bm #8
    excludeFrom = "h6 h7";
    excludePromotionTo = "rbn";
    excludeToCapturable = true;
    excludeAllowingFrom = "h8";
  } else if (epd == "8/1B6/4p3/1p2P3/PP4p1/p4pKp/rp3P1p/bR4nk b - -" || // bm #-7
             epd == "8/8/B3p3/1p2P3/PP4p1/p4pKp/rp3P1p/bR4nk w - -") {  // bm #8
    excludeFrom = "b1 f2 g3 b4";
    excludePromotionTo = "qrn";
    excludeAllowingFrom = "a1 h1 a2 b2 h2 a3 f3 h3 g4";
  } else if (epd == "8/1K6/3p4/k2Pp3/4p3/PP2P3/N7/8 b - -" || // bm #-7
             epd == "8/K7/3p4/k2Pp3/4p3/PP2P3/N7/8 w - -") {  // bm #8
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "8/1N6/8/2p5/k1P5/5N2/1P6/1K6 b - -" || // bm #-7
             epd == "8/1N6/8/2p1N3/k1P5/8/1P6/1K6 w - -") { // bm #8
    excludeFrom = "c4 b7";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "8/1Q1p1p2/3P1PN1/8/8/p1p3P1/rpPpP3/kn1K4 b - -" || // bm #-7
             epd == "8/1Q1pNp2/3P1P2/8/8/p1p3P1/rpPpP3/kn1K4 w - -") {  // bm #8
    excludeFrom = "d1 c2 e2 g3 d6";
    excludePromotionTo = "qrb";
    excludeCaptures = true;
    excludeAllowingFrom = "a1 b1 a2 b2 d2 a3 c3";
  } else if (epd == "8/1p5B/8/1PR5/pK1N4/rp6/pk1P4/b7 b - -" ||  // bm #-7
             epd == "8/1p6/8/1PR5/pK1N4/rp1B4/pk1P4/b7 w - -") { // bm #8
    excludeFrom = "d2 b4 b5";
    excludeCaptures = true;
    excludeToCapturable = true;
    excludeAllowingFrom = "a1 a2 a3 b3 a4";
  } else if (epd == "8/1p6/1p6/8/1N6/1P6/2K1N3/k7 b - -" || // bm #-7
             epd == "8/1p6/1p6/8/1N6/1P6/2K5/k5N1 w - -") { // bm #8
    excludePromotionTo = "qrbn";
    excludeAllowingFrom = "a1";
  } else if (epd == "8/2B1p3/4p3/2P5/1P2PP2/PB1p1P2/1p1P4/bk1K4 b - -" || // bm #-7
             epd == "8/2B1p3/4p3/8/1PP1PP2/PB1p1P2/1p1P4/bk1K4 w - -") {  // bm #8
    excludeFrom = "d1 d2 a3 f3 b4 e4 f4";
    excludePromotionTo = "qrbn";
    excludeAllowingFrom = "a1 b1 b2 d3";
  } else if (epd == "8/2BN4/8/3k4/2pN2p1/2K3P1/8/8 b - -" || // bm #-7
             epd == "8/2BN4/8/3k4/2pN2p1/2K5/6P1/8 w - -") { // bm #8
    excludeFrom = "c3 d4";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c4 g4";
  } else if (epd == "8/2N5/8/8/3B3b/4P3/7p/5K1k b - -" || // bm #-7
             epd == "8/2N5/8/8/7b/2B1P3/7p/5K1k w - -") { // bm #8
    excludeFrom = "f1 e3";
    excludeAllowingFrom = "h1 h2";
  } else if (epd == "8/2N5/R7/8/8/1p6/1p2K3/1k6 b - -" || // bm #-7
             epd == "8/2N5/R7/8/8/1p6/1p6/1k1K4 w - -") { // bm #8
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b2 b3";
  } else if (epd == "8/2k5/2p5/2B1P3/2BPpP2/4K3/8/8 b - -" ||   // bm #-7
             epd == "8/2k5/2p5/2B1P3/2pPpP2/4K3/4B3/8 w - -") { // bm #8
    excludeFrom = "e3 d4 c5 e5";
    excludePromotionTo = "rbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c4 e4 c6";
  } else if (epd == "8/2r3n1/7p/8/3p1PP1/1P1Qn2K/Npk1p3/1N6 b - -" || // bm #-7
             epd == "8/2r3n1/Q6p/8/3p1PP1/1P2n2K/Npk1p3/1N6 w - -") { // bm #8
    excludeFrom = "b3 h3 f4 g4";
    excludeCaptures = true;
    excludeAllowingFrom = "b2 e2 e3 d4 h6 c7 g7";
  } else if (epd == "8/3K4/B1N1p3/2k1p3/2P1P3/2P5/8/8 b - -" || // bm #-7
             epd == "8/3K4/2N1p3/1Bk1p3/2P1P3/2P5/8/8 w - -") { // bm #8
    excludeFrom = "c3 c4 e4";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "8/3p3p/3Pp2P/4P3/3p1N2/1p6/pPpK4/rkB5 b - -" || // bm #-7
             epd == "8/3p3p/3Pp2P/4P3/3p4/1p6/pPpKN3/rkB5 w - -") {  // bm #8
    excludeFrom = "c1 b2 d2 e5 d6";
    excludePromotionTo = "rbn";
    excludeAllowingFrom = "a1 b1 a2 c2 b3 e6 d7";
  } else if (epd == "8/3p4/3B4/8/2p3N1/2P1Nk2/8/4K3 b - -" || // bm #-7
             epd == "8/3p4/3B4/8/2p3N1/2P1Nk2/8/5K2 w - -") { // bm #8
    excludeFrom = "c3 e3 d6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "8/3p4/8/6p1/6p1/6p1/3K2p1/Q5bk b - -" ||  // bm #-7
             epd == "8/3p4/8/6p1/6p1/6p1/3K2p1/2Q3bk w - -") { // bm #8
    excludeFrom = "d2";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g2 g3 g4 g5";
  } else if (epd == "8/4p2p/4p2k/4p2N/2N1P1pK/6P1/8/8 b - -" || // bm #-7
             epd == "8/4p2p/4p2k/4p2N/4P1pK/N5P1/8/8 w - -") {  // bm #8
    excludeFrom = "e4 h4 h5";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g4 e5 e7 h7";
  } else if (epd == "8/4p3/4N3/5p2/5p2/1P1k1P2/1p2p1R1/bK2B3 b - -" || // bm #-7
             epd == "8/4p3/4N3/5p2/5p2/1P1k4/1p2pPR1/bK2B3 w - -") {   // bm #8
    excludeFrom = "b1 e1 b3 e6";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 b2 e2 f4 e7";
  } else if (epd == "8/4pp1B/7p/8/6p1/1p3kP1/1P1P1P2/5K2 b - -" ||  // bm #-7
             epd == "8/4pp2/7p/8/6p1/1p3kP1/1P1P1P2/1B3K2 w - -") { // bm #8
    excludeFrom = "f1 b2 f2 g3";
    excludePromotionTo = "qrbn";
    excludeAllowingFrom = "b3 f3 g4";
  } else if (epd == "8/5N1p/5p2/5P1B/4Pk2/3P4/3K3P/8 b - -" ||  // bm #-7
             epd == "8/5N1p/5p2/5P2/4Pk2/3P4/3K3P/3B4 w - -") { // bm #8
    excludeFrom = "h2 d3 e4 f5 f7";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "8/5p2/5p2/5p2/2B2p2/B1K2P2/1p6/bk6 b - -" || // bm #-7
             epd == "8/5p2/5p2/5p2/2B2p2/BK3P2/1p6/bk6 w - -") {  // bm #8
    excludeFrom = "f3";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 b2 f4";
  } else if (epd == "8/5p2/8/3B1K2/3B4/8/6rp/7k b - -" || // bm #-7
             epd == "8/5p2/5K2/3B4/3B4/8/6rp/7k w - -") { // bm #8
    excludeFrom = "d4";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 h2";
  } else if (epd == "8/7B/8/8/8/2RK4/pk1P4/b7 b - -" ||  // bm #-7
             epd == "8/7B/8/8/3K4/2R5/pk1P4/b7 w - -") { // bm #8
    excludeFrom = "h7";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 a2";
  } else if (epd == "8/8/1K6/8/1kp5/5p2/2PP1P2/1N1N4 b - -" || // bm #-7
             epd == "8/8/1K6/8/1kp5/5p2/1NPP1P2/1N6 w - -") {  // bm #8
    excludeFrom = "b1 f2 b6";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f3";
  } else if (epd == "8/8/1N1p1K2/8/1P1k4/8/3BP3/8 b - -" || // bm #-7
             epd == "8/8/1N1p4/5K2/1P1k4/8/3BP3/8 w - -") { // bm #8
    excludeFrom = "d2 e2 b4";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "8/8/2Np4/pp6/kp6/1p6/1K6/1b6 b - -" ||  // bm #-7
             epd == "8/8/3p4/pp6/kp1N4/1p6/1K6/1b6 w - -") { // bm #8
    excludeFrom = "b2";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b3 a4 b4 a5 b5";
  } else if (epd == "8/8/2Q5/8/8/3p4/5p1p/3K1k2 b - -" || // bm #-7
             epd == "8/8/8/8/8/3p4/5p1p/2QK1k2 w - -") {  // bm #8
    excludeFrom = "d1";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f2";
  } else if (epd == "8/8/3Np2p/2p3pP/2Pp2p1/3p1kP1/3P1P2/5K2 b - -" ||  // bm #-7
             epd == "4N3/8/4p2p/2p3pP/2Pp2p1/3p1kP1/3P1P2/5K2 w - -") { // bm #8
    excludeFrom = "f1 d2 f2 g3 c4";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d3 d4 g4 c5 g5 h6";
  } else if (epd == "8/8/5B2/5p2/2K2k2/N6p/5P1N/8 b - -" ||  // bm #-7
             epd == "8/8/5B2/5p2/2K2k2/7p/5P1N/1N6 w - -") { // bm #8
    excludeFrom = "h2 c4";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "8/8/6K1/3p4/3pk3/3N4/4p2B/4N3 b - -" ||  // bm #-7
             epd == "8/8/6K1/3p4/3pk3/3N4/4p3/4N1B1 w - -") { // bm #8
    excludeFrom = "e1 d3";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "8/8/6K1/8/8/2B5/k1N5/N7 b - -" || // bm #-7
             epd == "8/7K/8/8/8/2B5/k1N5/N7 w - -") {  // bm #8
    excludeFrom = "c3";
    excludeAllowingCapture = true;
  } else if (epd == "8/8/7N/8/3N2p1/2K1k1B1/6P1/8 b - -" ||  // bm #-7
             epd == "6N1/8/8/8/3N2p1/2K1k1B1/6P1/8 w - -") { // bm #8
    excludeFrom = "c3 d4";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g4";
  } else if (epd == "8/8/8/7p/P7/7P/8/kBK5 b - -" || // bm #-7
             epd == "8/8/8/7p/8/7P/P7/kBK5 w - -") { // bm #8
    excludeFrom = "h3";
    excludePromotionTo = "rbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "8/8/8/8/N1B5/4K3/8/k1N5 b - -" || // bm #-7
             epd == "8/8/8/8/N7/4K3/B7/k1N5 w - -") {  // bm #8
    excludeFrom = "a4";
    excludeAllowingCapture = true;
  } else if (epd == "8/B5p1/p3B1Pp/p3p3/2p1k1KP/6P1/P1P3P1/8 b - -" || // bm #-7
             epd == "8/B5p1/p3B1Pp/p3p2K/2p1k2P/6P1/P1P3P1/8 w - -") { // bm #8
    excludeFrom = "a2 c2 g2 g3 h4 e6 g6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e5 g7";
  } else if (epd == "8/p3K3/2p5/N1p5/p1p5/kbP5/p7/B7 b - -" || // bm #-7
             epd == "5K2/p7/2p5/N1p5/p1p5/kbP5/p7/B7 w - -") { // bm #8
    excludeFrom = "c3 a5";
    excludeAllowingFrom = "a2 a3 a4 c4 c5";
  } else if (epd == "8/p7/R7/1kN5/2Np1p2/P2P1Pp1/6K1/8 b - -" || // bm #-7
             epd == "8/p7/R7/NkN5/3p1p2/P2P1Pp1/6K1/8 w - -") {  // bm #8
    excludeFrom = "g2 a3 d3 f3 c5";
    excludeCaptures = true;
    excludeToCapturable = true;
    excludeAllowingFrom = "g3 d4 f4 a7";
  } else if (epd == "8/pK6/p3p3/P3p3/2N1p3/8/1ppPp3/brkbR3 b - -" ||  // bm #-7
             epd == "2K5/p7/p3p3/P3p3/2N1p3/8/1ppPp3/brkbR3 w - -") { // bm #8
    excludePromotionTo = "qrbn";
    excludeToCapturable = true;
    excludeAllowingFrom = "a1 b1 c1 d1 b2 c2 e2 a6 a7";
  } else if (epd == "K4b2/1p2p1pB/kp2P1PQ/1p2p3/1P2P3/P7/8/8 b - -" ||  // bm #-7
             epd == "K4b2/1p2p1pB/kp2P1P1/1p2p3/1P2P2Q/P7/8/8 w - -") { // bm #8
    excludeFrom = "b4 e4 e6 a8";
    excludePromotionTo = "qrbn";
    excludeAllowingFrom = "b5 e5 a6 b6 b7 e7";
  } else if (epd == "QK6/8/8/8/1p6/6p1/1pppppp1/1kqbbrrn b - -" || // bm #-7
             epd == "1K6/8/8/8/Qp6/6p1/1pppppp1/1kqbbrrn w - -") { // bm #8
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c1 d1 e1 f1 g1 h1 b2 c2 d2 e2 f2 g2 g3";
  } else if (epd == "QK6/8/8/8/1p6/8/1pp5/1kb4R b - -" || // bm #-7
             epd == "1K6/8/8/8/Qp6/8/1pp5/1kb4R w - -") { // bm #8
    excludeFrom = "h1";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c1 b2 c2";
  } else if (epd == "R2Q4/4p3/1p2P3/8/1P6/1pp5/1prp4/1kbK4 b - -" || // bm #-7
             epd == "R7/3Pp3/1p2P3/8/1P6/1pp5/1prp4/1kbK4 w - -") {  // bm #8
    excludeFrom = "d1 b4 e6";
    excludePromotionTo = "rbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c1 b2 c2 d2 b3 c3 e7";
  } else if (epd == "R7/8/8/8/8/4N1P1/2K4p/6nk b - -" || // bm #-7
             epd == "7R/8/8/8/8/4N1P1/2K4p/6nk w - -") { // bm #8
    excludeFrom = "g3";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 h2";
  } else if (epd == "k1n5/BP6/P7/P7/P7/P7/P7/2K5 b - -" || // bm #-7
             epd == "k1n5/B7/PP6/P7/P7/P7/P7/2K5 w - -") { // bm #8
    excludeFrom = "c1 a2 a3 a4 a5 a6 a7";
    excludePromotionTo = "qbn";
    excludeToCapturable = true;
    excludeAllowingFrom = "c8";
  } else if (epd == "k7/2K5/8/8/1P6/8/2P5/8 b - -" || // bm #-7
             epd == "k7/2K5/8/8/8/8/1PP5/8 w - -") {  // bm #8
    excludeFrom = "c7";
    excludePromotionTo = "rbn";
    excludeAllowingCapture = true;
  } else if (epd == "k7/P7/P7/P7/P7/P7/P7/R3K3 w Q -") { // bm #8
    excludeFrom = "a2 a3 a4 a5 a6 a7";
    excludeToCapturable = true;
  } else if (epd == "k7/pr4p1/N7/6p1/1N4P1/1P4P1/6B1/3K4 b - -" || // bm #-7
             epd == "k7/pr4p1/N7/3B2p1/1N4P1/1P4P1/8/3K4 w - -") { // bm #8
    excludeFrom = "d1 b3 g3 g4 a6";
    excludeToCapturable = true;
    excludeAllowingFrom = "g5 a7 a8";
  } else if (epd == "krb5/1p1p2p1/1p1p4/bN1P1RKP/1p1p4/1P1P3P/8/8 b - -" ||  // bm #-7
             epd == "krb5/1p1p2p1/1p1p4/bN1P2KP/1p1p4/1P1P3P/5R2/8 w - -") { // bm #8
    excludeFrom = "b3 d3 h3 d5 g5";
    excludePromotionTo = "qrb";
    excludeToCapturable = true;
    excludeAllowingFrom = "b4 d4 a5 b6 d6 b7 d7 b8 c8";
  } else if (epd == "n7/3p4/N1nP4/2N5/p7/k7/1p1B4/bK6 b - -" || // bm #-7
             epd == "n7/3p4/N1nP4/2N5/p7/k1B5/1p6/bK6 w - -") { // bm #8
    excludeFrom = "b1 c5 d6";
    excludeAllowingFrom = "a1 b2 a3 a4 d7";
  } else if (epd == "nN2k1b1/5pP1/N4Bp1/6P1/8/8/6K1/8 b - -" || // bm #-7
             epd == "nN2k1b1/5pP1/N4Bp1/6P1/8/8/8/7K w - -") {  // bm #8
    excludeFrom = "g5 f6 g7 b8";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g6 f7 e8";
  } else if (epd == "rnbqr1k1/pp1p1p1Q/2p2p2/4P3/8/3B4/P4PPP/R4RK1 b - -" ||  // bm #-7
             epd == "rnbqr1k1/pp1p1p1p/2p2p2/4P2Q/8/3B4/P4PPP/R4RK1 w - -") { // bm #8
    excludeFrom = "a1 f1 g1 a2 f2 g2 h2 e5";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c6 f6 a7 b7 d7 f7 h7 a8 b8 c8 d8 e8";
  } else if (epd == "1Nr1n3/p3p1q1/P2p1prk/4p3/1pB1n1P1/1P5R/3b2KN/8 b - -" ||  // bm #-8
             epd == "1Nr1n3/p3p1q1/P2p1prk/4p3/1pB1n1P1/1P1R4/3b2KN/8 w - -") { // bm #9
    excludeFrom = "g2 h2 b3 c4 g4 a6 b8";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d2 b4 e4 e5 d6 f6 g6 a7 e7 g7 c8 e8";
  } else if (epd == "2nr1NBB/1q1P1K2/n1P4p/1P4p1/N4p2/p3p3/8/kb5r b - -" || // bm #-8
             epd == "2nr1NBB/1q1P2K1/n1P4p/1P4p1/N4p2/p3p3/8/kb5r w - -") { // bm #9
    excludeFrom = "a4 b5 c6 d7 f8 g8 h8";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b1 h1 a3 e3 f4 g5 a6 h6 b7 c8 d8";
  } else if (epd == "r1q2rk1/n4ppB/8/8/2QR4/7R/8/2K5 b - -" ||  // bm #-8
             epd == "r1q2rk1/n4pp1/8/8/2QRB3/7R/8/2K5 w - -") { // bm #9
    excludeFrom = "c1";
    excludeCaptures = true;
    excludeAllowingFrom = "a7 f7 g7 a8 c8 f8";
  } else if (epd == "r7/5k1p/2pP2p1/1p6/1q3B2/1p2Q1P1/1P6/1rK1R2R w - -" || // bm #-8
             epd == "r7/5k1p/2pP2p1/1p6/1q3B2/1p2Q1P1/1P6/rNK1R2R b - -") { // bm #9
    excludeFrom = "b3 b5 c6 g6 f7 h7";
    excludeAllowingFrom = "e1 h1 b2 e3 g3 f4 d6";
  } else if (epd == "r7/5k1p/2pP2pP/1p6/1q3B2/1p2Q1P1/1P6/1rK1R2R w - -" || // bm #-8
             epd == "r7/5k1p/2pP2pP/1p6/1q3B2/1p2Q1P1/1P6/rNK1R2R b - -") { // bm #9
    excludeFrom = "b3 b5 c6 g6 f7 h7";
    excludeAllowingFrom = "e1 h1 b2 e3 g3 f4 d6 h6";
  } else if (epd == "kbb2rr1/Qp6/p3p1q1/PN1pB3/1n6/R7/5PB1/6K1 b - -" ||    // bm #-7
             epd == "kbb2rr1/1p6/p3p1q1/PN1pB3/1n6/R3Q3/5PB1/6K1 w - -" ||  // bm #8
             epd == "1bb2rr1/kp6/p3p1q1/PN1pB3/1n6/R3Q3/5PB1/6K1 b - -" ||  // bm #-8
             epd == "1bb2rr1/kp6/p3p1q1/P2pB3/1n1N4/R3Q3/5PB1/6K1 w - -") { // bm #9
    excludeFrom = "g1 f2 g2 e5";
    excludePromotionTo = "qrbn";
    excludeAllowingFrom = "d5 a6 e6 g6 b7 c8 f8 g8";
  } else if (epd == "8/8/8/7p/7p/5NpB/3K1pnb/5k2 b - -" || // bm #-8
             epd == "8/8/8/7p/7p/5NpB/5pnb/3K1k2 w - -") { // bm #9
    excludeFrom = "h3";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f1 f2 g2 g3 h4";
  } else if (epd == "8/7B/8/8/N1pPR1P1/1pP1pP2/pP1pP3/k2K4 b - -" || // bm #-8
             epd == "8/7B/8/8/N1pP1RP1/1pP1pP2/pP1pP3/k2K4 w - -") { // bm #9
    excludeFrom = "b2 c3 f3 a4 d4 g4 h7";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a2 d2 b3 e3 c4";
  } else if (epd == "1rB2r1n/3p4/3pp3/1R1p2K1/3pkpN1/4pp2/8/1Q6 b - -" || // bm #-8
             epd == "1rB2r1n/3p4/3pp3/1R1p2K1/3pkpN1/4pp2/8/3Q4 w - -") { // bm #9
    excludeFrom = "g5";
    excludeAllowingFrom = "e3 f3 e4 f4";
  } else if (epd == "R7/8/b7/k7/N1P5/p1p1p1p1/N1P1PbP1/7K b - -" || // bm #-8
             epd == "8/R7/b7/k7/N1P5/p1p1p1p1/N1P1PbP1/7K w - -") { // bm #9
    excludeFrom = "a2 c2 e2 g2 a4 c4";
    excludeToAttacked = true;
    excludeAllowingFrom = "a3 c3 e3 g3 a6";
  } else if (epd == "1B6/2R5/8/5B2/3Pp3/4Pp1P/4pPp1/4K1kb b - -" || // bm #-8
             epd == "1B6/8/8/5B2/3Pp3/4Pp1P/4pPp1/2R1K1kb w - -") { // bm #9
    excludeFrom = "f2 e3 h3 d4 f5 b8";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 e2 g2 f3 e4";
  } else if (epd == "6k1/7p/5Pp1/4p1P1/3p4/3Kb1q1/Q7/8 b - -" ||  // bm #-8
             epd == "6k1/7p/5Pp1/4p1P1/3p4/3Kb1q1/4Q3/8 w - -") { // bm #9
    excludeFrom = "d3 g5 f6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e3 g3 d4 e5 g6 h7";
  } else if (epd == "k7/3p4/1PpN4/2P5/8/8/1K5p/7B b - -" || // bm #-8
             epd == "k7/3p4/1PpN4/2P5/8/8/7p/K6B w - -") {  // bm #9
    excludeFrom = "h1 c5 b6 d6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h2 c6 d7";
  } else if (epd == "8/1p4R1/brp5/kpP5/1p3p2/1p1PpP1p/1P2P2P/1K6 b - -" || // bm #-8
             epd == "8/1p4R1/brp5/kpP5/1p3p2/1p1PpP1p/1P2P2P/K7 w - -") {  // bm #9
    excludeFrom = "b2 e2 h2 d3 f3 c5";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b3 e3 h3 b4 f4 b5 a6 b6 c6 b7";
  } else if (epd == "8/2pp4/2pp4/P1kp4/P2p4/P5N1/3N4/2B3K1 b - -" || // bm #-8
             epd == "8/2pp4/2pp4/P1kp4/P2p4/P7/3N4/2B3KN w - -") {   // bm #9
    excludeFrom = "g1 d2";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingFrom = "c5 c6";
  } else if (epd == "n7/2pK1pp1/1pP2kp1/1Pb1p1p1/2P1P1P1/1P1B4/4P3/4N3 b - -" || // bm #-8
             epd == "n7/2pK1pp1/1pP2kp1/1Pb1p1p1/2P1P1P1/1P1B4/2N1P3/8 w - -") { // bm #9
    excludeFrom = "e2 b3 c4 e4 g4 b5 c6 d7";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e5 g5 b6 f6 g6 c7 f7 g7 a8";
  } else if (epd == "8/2p5/2R5/5p2/B2k1P2/5P2/2P2P2/7K b - -" ||  // bm #-8
             epd == "8/2p5/2R5/5p2/3k1P2/1B3P2/2P2P2/7K w - -") { // bm #9
    excludeFrom = "h1 f2 f3 f4 c6";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f5 c7";
  } else if (epd == "1B2b2k/6np/7N/3K4/8/8/8/8 b - -" || // bm #-8
             epd == "1B2b2k/6np/7N/4K3/8/8/8/8 w - -") { // bm #9
    excludeFrom = "h6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h7 h8";
  } else if (epd == "1K6/8/8/3Q4/8/1p4p1/1pppppp1/1kqbbrrn b - -" || // bm #-8
             epd == "1K6/8/8/Q7/8/1p4p1/1pppppp1/1kqbbrrn w - -") {  // bm #9
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c1 d1 e1 f1 g1 h1 b2 c2 d2 e2 f2 g2 b3 g3";
  } else if (epd == "1N6/5q2/3p4/2bn4/p7/8/B7/Q1k1K3 b - -" || // bm #-8
             epd == "1N5Q/5q2/3p4/2bn4/p7/8/B7/2k1K3 w - -") { // bm #9
    excludeFrom = "e1";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a4 c5 d5 d6 f7";
  } else if (epd == "1b6/2p5/1pP2p2/1p3p2/1Pp2P2/P1N5/p1p5/kbK5 b - -" || // bm #-8
             epd == "1b6/2p5/1pP2p2/1p1N1p2/1Pp2P2/P7/p1p5/kbK5 w - -") { // bm #9
    excludeFrom = "c1 b4 f4 c6";
    excludePromotionTo = "qrbn";
    excludeToCapturable = true;
    excludeAllowingFrom = "a1 b1 a2 c2 f5 c7";
  } else if (epd == "1k6/1P6/K7/1P6/8/8/8/8 b - -" ||  // bm #-8
             epd == "1k6/1P6/1K6/1P6/8/8/8/8 w - -") { // bm #9
    excludePromotionTo = "rbn";
    excludeAllowingCapture = true;
  } else if (epd == "1r3b2/q1p3pr/5k2/2Np3B/2N4P/8/8/5RK1 b - -" || // bm #-8
             epd == "1r3b2/q1p3pr/5k2/2Np3B/2N4P/8/8/4K2R w K -") { // bm #9
    excludeFrom = "h1 h4 h5";
    excludeCaptures = true;
    excludeToCapturable = true;
    excludeAllowingFrom = "d5 a7 c7 g7 h7 b8 f8";
  } else if (epd == "2K5/4p3/2p1Pp2/2p1kPp1/2P1p1P1/4P2N/8/8 b - -" || // bm #-8
             epd == "8/3Kp3/2p1Pp2/2p1kPp1/2P1p1P1/4P2N/8/8 w - -") {  // bm #9
    excludeFrom = "e3 c4 g4 f5 e6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e4 c5 g5 c6 f6 e7";
  } else if (epd == "2N1K1B1/p1p5/rbp3p1/p1k5/P5Pp/2Pp1P1P/1P1P1P2/8 b - -" || // bm #-8
             epd == "2N1K3/p1p5/rbp3p1/p1k5/P5Pp/2Pp1P1P/BP1P1P2/8 w - -") {   // bm #9
    excludeFrom = "d2 f2 c3 h3 a4 g4 c8";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingFrom = "d3 h4 a5 a6 b6 c6 a7 c7";
  } else if (epd == "2R5/k7/p7/P7/8/2K5/8/8 b - -" || // bm #-8
             epd == "8/k1P5/p7/P7/8/2K5/8/8 w - -") { // bm #9
    excludePromotionTo = "qbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "3B4/8/2kNK3/8/8/2P1p3/4P3/8 b - -" ||  // bm #-8
             epd == "3B4/8/2kN4/4K3/8/2P1p3/4P3/8 w - -") { // bm #9
    excludeFrom = "c3 d6";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e3";
  } else if (epd == "4N3/4b3/6p1/3Pk1pb/1p1pP1p1/1P1P2K1/1B4P1/8 b - -" || // bm #-8
             epd == "4N3/4b3/6p1/3Pk1pb/1p1pP1p1/1P1P2K1/6P1/B7 w - -") {  // bm #9
    excludeFrom = "g2 b3 d3 e4 d5 e8";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b4 d4 e5 g5 h5 g6";
  } else if (epd == "5B2/8/7k/5Bp1/8/8/6P1/7K b - -" || // bm #-8
             epd == "8/8/3B3k/5Bp1/8/8/6P1/7K w - -") { // bm #9
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g5";
  } else if (epd == "5N2/1p2K3/1p4p1/1P1k2p1/1P1p2P1/3P4/5P2/8 b - -" || // bm #-8
             epd == "8/1p1NK3/1p4p1/1P1k2p1/1P1p2P1/3P4/5P2/8 w - -") {  // bm #9
    excludeFrom = "d3 b4 g4 b5 e7";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d4 g5 b6 g6 b7";
  } else if (epd == "6R1/3p4/Pp1P1N2/1p6/pP6/rkP5/qrpPP3/nbK5 b - -" || // bm #-8
             epd == "8/3p4/Pp1P1N2/1p6/pP4R1/rkP5/qrpPP3/nbK5 w - -") { // bm #9
    excludeFrom = "c1 d2 e2 c3 b4 a6 d6";
    excludeCaptures = true;
    excludeAllowingFrom = "a1 b1 a2 b2 c2 a3 a4 d7";
  } else if (epd == "7R/8/8/1pN5/1pp3B1/1prp4/1prp4/nkbK4 b - -" ||  // bm #-8
             epd == "7R/1N6/8/1p6/1pp3B1/1prp4/1prp4/nkbK4 w - -") { // bm #9
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 c1 b2 c2 d2 b3 c3 d3 b4 c4";
  } else if (epd == "7k/7p/2N4B/8/8/8/1K6/8 b - -" || // bm #-8
             epd == "1N5k/7p/7B/8/8/8/1K6/8 w - -") { // bm #9
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h7";
  } else if (epd == "8/1K6/p6p/p1p4p/4p3/2N1B2B/P2PP2P/4k3 b - -" || // bm #-8
             epd == "K7/8/p6p/p1p4p/4p3/2N1B2B/P2PP2P/4k3 w - -") {  // bm #9
    excludeFrom = "a2 d2 e2 h2 c3 h3";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e4";
  } else if (epd == "8/1Kppp3/2pbp3/2p1p3/1kP1P2p/1P5P/P7/RN6 b - -" || // bm #-8
             epd == "8/2ppp3/K1pbp3/2p1p3/1kP1P2p/1P5P/P7/RN6 w - -") { // bm #9
    excludeFrom = "a2 b3 h3 c4 e4";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "8/1N2N2p/6pr/6pr/3K2kb/6p1/6P1/8 b - -" || // bm #-8
             epd == "3N4/4N2p/6pr/6pr/3K2kb/6p1/6P1/8 w - -") { // bm #9
    excludeFrom = "g2 d4";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g3 h4 h5 g6 h6 h7";
  } else if (epd == "8/1N6/3N2pp/6pr/3K2kb/6p1/6P1/8 b - -" || // bm #-8
             epd == "3N4/8/3N2pp/6pr/3K2kb/6p1/6P1/8 w - -") { // bm #9
    excludeFrom = "g2 d4";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g3 h4 h5 g6 h6";
  } else if (epd == "8/1p6/pPkpK3/P5p1/P2p2P1/B2P4/8/8 b - -" || // bm #-8
             epd == "8/1p6/pPkpK3/P5p1/PB1p2P1/3P4/8/8 w - -") { // bm #9
    excludeFrom = "d3 a4 g4 a5 b6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d4 g5 a6 b7";
  } else if (epd == "8/2B3K1/6p1/6k1/6P1/5P2/6B1/8 b - -" || // bm #-8
             epd == "8/2B3K1/6p1/6k1/6P1/5P1B/8/8 w - -") {  // bm #9
    excludeFrom = "f3 g4";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "8/2KN4/8/1k1N4/8/1P6/8/8 b - -" ||  // bm #-8
             epd == "8/1K1N4/8/1k1N4/8/1P6/8/8 w - -") { // bm #9
    excludeFrom = "d7";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
  } else if (epd == "8/2N5/7p/7k/5K1N/6P1/b7/8 b - -" || // bm #-8
             epd == "N7/8/7p/7k/5K1N/6P1/b7/8 w - -") {  // bm #9
    excludeFrom = "g3 f4";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h5 h6";
  } else if (epd == "8/2p1p3/2P1P3/8/2N1p2p/1p4pP/1P2PpPk/5K1n b - -" || // bm #-8
             epd == "8/2p1p3/2P1P3/8/4p2p/1p4pP/1P1NPpPk/5K1n w - -") {  // bm #9
    excludeFrom = "f1 b2 e2 g2 h3";
    excludePromotionTo = "qrb";
    excludeCaptures = true;
    excludeAllowingFrom = "h1 f2 h2 b3 g3 h4";
  } else if (epd == "8/2p2p2/2R2p2/5p2/B2k1K2/8/2P5/8 b - -" ||  // bm #-8
             epd == "8/2p2p2/2R2p2/5p2/3k1K2/1B6/2P5/8 w - -") { // bm #9
    excludeFrom = "f4 c6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f5 f6 c7 f7";
  } else if (epd == "8/3p2p1/2pK2P1/2P5/P1k4p/P1p1NP1p/2p4P/2N5 b - -" ||  // bm #-8
             epd == "8/3p2p1/2pK2P1/2P5/P1k4p/P1p2P1p/2p4P/2N2N2 w - -") { // bm #9
    excludeFrom = "c1 h2 a3 f3 a4 c5 d6";
    excludePromotionTo = "qbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c2 c3 h3 h4 c6 d7 g7";
  } else if (epd == "8/3p4/3p3K/3P1kN1/3B4/4pP2/4P3/8 b - -" || // bm #-8
             epd == "8/3p4/3p3K/3P1k2/3B4/4pP1N/4P3/8 w - -") { // bm #9
    excludeFrom = "f3 d4 d5 h6";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e3 d6 d7";
  } else if (epd == "8/3p4/8/2pK4/8/2PPk2N/3pP3/3B4 b - -" ||  // bm #-8
             epd == "8/3p4/8/2p1K3/8/2PPk2N/3pP3/3B4 w - -") { // bm #9
    excludeFrom = "d1 e2 c3 h3";
    excludePromotionTo = "rbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d2";
  } else if (epd == "8/4p1Rp/4pk1P/4p3/1n2P1p1/B2P2p1/3P2P1/1K6 b - -" || // bm #-8
             epd == "8/4p1Rp/4pk1P/4p3/1n2P1p1/B2P2p1/3P2P1/K7 w - -") {  // bm #9
    excludeFrom = "d2 g2 d3 e4 h6 g7";
    excludeToCapturable = true;
    excludeAllowingFrom = "g3 g4 e5 e6 f6 e7 h7";
  } else if (epd == "8/5N2/1p1b4/kB6/2K5/6p1/8/8 b - -" || // bm #-8
             epd == "7N/8/1p1b4/kB6/2K5/6p1/8/8 w - -") {  // bm #9
    excludeFrom = "c4 b5";
    excludeAllowingCapture = true;
  } else if (epd == "8/8/2p5/2p5/2p3p1/2p3P1/1pP5/kb3Q1K b - -" || // bm #-8
             epd == "8/8/2p5/2p5/2p3p1/2p3P1/1pP5/kb2Q2K w - -") { // bm #9
    excludeFrom = "c2 g3";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b1 b2 c3 c4 g4";
  } else if (epd == "8/8/5P2/6p1/6p1/6pk/6p1/6K1 b - -" || // bm #-8
             epd == "8/8/8/5Pp1/6p1/6pk/6p1/6K1 w - -") {  // bm #9
    excludePromotionTo = "rbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g2 g3";
  } else if (epd == "8/8/7R/7N/6p1/6p1/4K1p1/6kb b - -" || // bm #-8
             epd == "8/8/7R/8/5Np1/6p1/4K1p1/6kb w - -") { // bm #9
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 g2 g3 g4";
  } else if (epd == "8/8/8/5p2/5P1k/6N1/4NKP1/8 b - -" ||   // bm #-8
             epd == "8/8/8/5p2/5P1k/6N1/4N1P1/6K1 w - -") { // bm #9
    excludeFrom = "e2 f4";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "8/8/8/p7/P3N3/B5p1/p3B1Pp/k6K b - -" || // bm #-8
             epd == "8/8/8/p7/P3N3/B5p1/p5Pp/k2B3K w - -") { // bm #9
    excludeFrom = "h1 g2 a3 a4";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "8/B1p5/4Bp2/3K4/5P2/2Pp4/3P1npp/4Rbkq b - -" || // bm #-8
             epd == "8/B1p5/2K1Bp2/8/5P2/2Pp4/3P1npp/4Rbkq w - -") { // bm #9
    excludeFrom = "e1 d2 c3 f4";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f1 g1 h1 g2 h2 d3";
  } else if (epd == "N2B4/pp4p1/qr6/kn4RK/p3p3/P7/4P3/8 b - -" || // bm #-8
             epd == "N2B4/pp4p1/qr6/kn4RK/p3p3/8/P3P3/8 w - -") { // bm #9
    excludeFrom = "e2 a8";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a4 a5 b5 a6 a7 b7";
  } else if (epd == "R4k2/5P2/5P2/5P2/5P2/5P2/5P2/4K3 b - -" || // bm #-8
             epd == "5k2/5P2/5P2/5P2/5P2/5P2/5P2/R3K3 w - -") { // bm #9
    excludeFrom = "f2 f3 f4 f5 f6 f7";
    excludeToCapturable = true;
  } else if (epd == "k1R5/8/1pPB4/1p1B4/8/pP6/N3q3/K7 b - -" || // bm #-8
             epd == "k7/2R5/1pPB4/1p1B4/8/pP6/N3q3/K7 w - -") { // bm #9
    excludeFrom = "a1 d5 d6";
    excludePromotionTo = "qrb";
    excludeAllowingFrom = "e2 a3 b5 b6";
  } else if (epd == "k7/2p5/N1P5/K7/8/8/1P6/8 b - -" ||  // bm #-8
             epd == "k7/2p5/N1P5/8/1K6/8/1P6/8 w - -") { // bm #9
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c7";
  } else if (epd == "k7/P7/P7/P7/P7/P7/8/2KR4 b - -" || // bm #-8
             epd == "k7/P7/P7/P7/P7/P7/8/R3K3 w Q -") { // bm #9
    excludeFrom = "a1 a3 a4 a5 a6 a7";
    excludeToCapturable = true;
  } else if (epd == "k7/p7/B7/8/8/8/8/4B2K b - -" || // bm #-8
             epd == "k7/p7/8/8/8/8/8/4BB1K w - -") { // bm #9
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a7";
  } else if (epd == "n6n/2p1Bp2/1pP2Kp1/1p1N2P1/1P1k2P1/4N3/4P3/8 b - -" ||  // bm #-8
             epd == "n2B3n/2p2p2/1pP2Kp1/1p1N2P1/1P1k2P1/4N3/4P3/8 w - -") { // bm #9
    excludeFrom = "e2 e3 b4 g4 d5 g5 c6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b5 b6 g6 c7 f7 a8 h8";
  } else if (epd == "r1bqkb1r/pppp1Bpp/8/n1n1N3/8/2NP4/PPP2PPP/R1B1K2R b KQkq -" ||   // bm #-8
             epd == "r1bqkb1r/pppp1ppp/8/n1n1N3/2B5/2NP4/PPP2PPP/R1B1K2R w KQkq -") { // bm #9
    excludeFrom = "a1 e1 h1 a2 b2 c2 g2 d3 e5";
    excludeToCapturable = true;
    excludeAllowingFrom = "a5 a7 b7 c7 d7 f7 g7 h7 a8 c8 d8 f8 h8";
  } else if (epd == "r5k1/5p2/5PpQ/8/8/8/8/7K b - -" ||   // bm #-8
             epd == "r5k1/5p2/5Pp1/8/8/8/8/2Q4K w - -") { // bm #9
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f7 g8";
  } else if (epd == "8/2n2K2/3k4/4NN2/2pPr3/2P5/8/8 b - -" || // bm #-9
             epd == "8/2n2KN1/3k4/4N3/2pPr3/2P5/8/8 w - -") { // bm #10
    excludeFrom = "d4 e5 f7";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c4 e4 c7";
  } else if (epd == "3nn3/2p2p1k/1p4p1/3BB1N1/1P4N1/1K6/8/8 b - -" || // bm #-9
             epd == "3nn3/2p2p1k/1p4p1/3BB3/1P4N1/1K3N2/8/8 w - -") { // bm #10
    excludeFrom = "b3 b4 d5 e5";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b6 g6 c7 f7 d8 e8";
  } else if (epd == "8/1K1pn3/3kp3/1N6/1p1Pp3/3PP3/8/8 b - -" || // bm #-9
             epd == "8/1K1pn3/3kp3/8/1p1Pp3/N2PP3/8/8 w - -") {  // bm #10
    excludeFrom = "d3 d4 b7";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b4 e4 e6 d7 e7";
  } else if (epd == "8/8/8/8/8/2Q5/pp1KPP2/k7 b - -" || // bm #-9
             epd == "8/8/8/8/8/8/ppQKPP2/k7 w - -") {   // bm #10
    excludeFrom = "d2 e2 f2";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a2 b2";
  } else if (epd == "8/8/5p2/p2p4/Pk1P1P2/BP1p4/K2p4/3B4 b - -" ||  // bm #-9
             epd == "8/8/5p2/p2p4/Pk1P1P2/1P1p4/KB1p4/3B4 w - -") { // bm #10
    excludeFrom = "d1 a2 b3 a4 d4 f4";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d2 d3 a5 d5";
  } else if (epd == "6R1/p7/Np2p3/1P2p3/P1k1P2p/2p4p/2P2Ppr/6Kb b - -" || // bm #-9
             epd == "3R4/p7/Np2p3/1P2p3/P1k1P2p/2p4p/2P2Ppr/6Kb w - -") { // bm #10
    excludeFrom = "g1 c2 f2 a4 e4 b5 a6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 g2 h2 c3 h3 h4 e5 b6 e6 a7";
  } else if (epd == "1B6/1R6/3P4/1P2P3/2P3pp/3P2pq/4K1kp/7b b - -" || // bm #-9
             epd == "8/1RB5/3P4/1P2P3/2P3pp/3P2pq/4K1kp/7b w - -") {  // bm #10
    excludeFrom = "e2 d3 c4 b5 e5 d6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 h2 g3 h3 g4 h4";
  } else if (epd == "1k6/8/3N2pK/1N6/8/7p/7p/7B b - -" ||  // bm #-9
             epd == "1k6/7K/3N2p1/1N6/8/7p/7p/7B w - -") { // bm #10
    excludeFrom = "h1 b5 d6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h2 h3";
  } else if (epd == "6k1/7p/5Pp1/4p3/3p4/3Kb1q1/Q7/8 b - -" ||  // bm #-9
             epd == "6k1/7p/5Pp1/4p3/3p4/3Kb1q1/4Q3/8 w - -") { // bm #10
    excludeFrom = "d3 f6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e3 g3 d4 e5 g6 h7";
  } else if (epd == "7k/4p2p/4p2B/4N3/8/8/8/6K1 b - -" ||  // bm #-9
             epd == "5B1k/4p2p/4p3/4N3/8/8/8/6K1 w - -") { // bm #10
    excludeFrom = "e5";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e6 h7";
  } else if (epd == "8/2N5/6p1/p5p1/p1kPp1Pp/p3P2P/K2PPB2/8 b - -" ||  // bm #-9
             epd == "8/2N5/6p1/p5p1/p1kPp1Pp/p3P2P/K2PP3/4B3 w - -") { // bm #10
    excludeFrom = "a2 e2 e3 h3 d4 c7";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a3 a4 e4 h4 a5 g5";
  } else if (epd == "b7/1kp5/2N5/1K1B4/8/8/P7/8 b - -" || // bm #-9
             epd == "b7/1kp5/2N5/K2B4/8/8/P7/8 w - -") {  // bm #10
    excludeFrom = "c6";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c7 a8";
  } else if (epd == "8/8/8/R6B/pp1P1P2/qp2P3/pk1K4/b7 b - -" ||  // bm #-9
             epd == "8/8/8/R7/pp1P1P2/qp2P3/pk1K4/b2B4 w - -") { // bm #10
    excludeFrom = "d2 d4";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 a2 a3 b3 a4 b4";
  } else if (epd == "2r5/1b2pq2/B4p1K/b1p1p3/5p2/n1r1p3/1n4pp/R6k b - -" ||  // bm #-9
             epd == "2r5/1b2pq2/5p1K/b1p1p3/5p2/n1r1p3/1n4pp/R4B1k w - -") { // bm #10
    excludeFrom = "h6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 h2 e3 c5 f6 b7 e7";
  } else if (epd == "1q2B2q/2q1B1q1/3qkq2/qrr1R1RQ/3QKQ2/2Q3Q1/1Q2n2Q/Q3n3 b - -" || // bm #-9
             epd == "1q2B2q/2q1B1q1/3qkq2/qrr1rRRQ/3QKQ2/2Q3Q1/1Q2n2Q/Q3n3 w - -") { // bm #10
    excludeFrom = "e4 e7 e8";
    excludeAllowingFrom = "e1 e2 e5 e6";
  } else if (epd == "7K/Q7/8/1p6/1p6/1p6/pp2N3/kB6 b - -" || // bm #-9
             epd == "7K/Q6B/8/1p6/1p6/1p6/pp2N3/k7 w - -") { // bm #10
    excludeFrom = "e2 h8";
    excludeCaptures = true;
    excludeAllowingFrom = "a2 b2 b3 b4 b5";
  } else if (epd == "8/8/3Q4/8/2k5/2p5/2P3p1/6Kb b - -" || // bm #-9
             epd == "8/8/8/4Q3/2k5/2p5/2P3p1/6Kb w - -") { // bm #10
    excludeFrom = "g1 c2";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 g2 c3";
  } else if (epd == "8/8/1Npp1p2/2pp1p2/2pk1K2/2p5/5P2/2N5 b - -" ||  // bm #-9
             epd == "8/3N4/2pp1p2/2pp1p2/2pk1K2/2p5/5P2/2N5 w - -") { // bm #10
    excludeFrom = "c1";
    excludePromotionTo = "qrbn";
    excludeAllowingFrom = "d4 d5";
  } else if (epd == "8/3BK3/3p4/3Pk3/3p1p2/3P1P2/8/2N5 b - -" || // bm #-9
             epd == "8/3BK3/3p4/3Pk3/3p1p2/3P1P2/4N3/8 w - -") { // bm #10
    excludeFrom = "d3 f3 d5 d7 e7";
    excludeCaptures = true;
    excludeAllowingFrom = "d4 f4";
  } else if (epd == "3K4/1p1N4/1p1k4/1P2p3/2p1P3/p1P5/P7/3B4 b - -" || // bm #-9
             epd == "3K4/1p1N4/1p1k4/1P2p3/2p1P3/pBP5/P7/8 w - -") {   // bm #10
    excludeFrom = "a2 e4 b5 d7 d8";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a3 c4 e5 b6 b7";
  } else if (epd == "8/3pK1pp/3P2kr/6pq/3B2pb/1PPP2p1/3P2P1/4R3 b - -" || // bm #-9
             epd == "8/3pK1pp/3P2kr/4R1pq/3B2pb/1PPP2p1/3P2P1/8 w - -") { // bm #10
    excludeFrom = "d2 g2 b3 c3 d3 d6 e7";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g3 g4 h4 g5 h5 h6 d7 g7 h7";
  } else if (epd == "1N5n/2p2p2/2Pk1Pp1/1p4P1/bP2Pp2/rp3P2/pBp5/n1K1N3 b - -" || // bm #-9
             epd == "1N5n/2p2p2/2Pk1Pp1/1p4P1/bP2Pp2/rp3P2/pBp3N1/n1K5 w - -") { // bm #10
    excludeFrom = "c1 b2 f3 b4 g5 c6 f6";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 a2 c2 a3 b3 a4 f4 b5 g6 c7 f7 h8";
  } else if (epd == "8/8/8/8/P1B5/4K3/5Bkp/7b b - -" || // bm #-9
             epd == "8/8/8/8/2B5/4K3/P4Bkp/7b w - -") { // bm #10
    excludeFrom = "f2 e3";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 h2";
  } else if (epd == "8/8/4p1p1/4P1P1/7p/4P2P/5K2/6Nk b - -" || // bm #-9
             epd == "8/8/4p1p1/4P1P1/7p/7P/4PK2/6Nk w - -") {  // bm #10
    excludeFrom = "f2 h3 e5";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h4 e6 g6";
  } else if (epd == "8/8/1p6/1Pp5/k1P2p2/2R2Pp1/3N2p1/6Kb b - -" || // bm #-9
             epd == "8/8/1p6/1Pp5/k1P2p2/1R3Pp1/3N2p1/6Kb w - -") { // bm #10
    excludeFrom = "g1 f3 c4 b5";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 g2 g3 f4 c5 b6";
  } else if (epd == "8/p7/P1p4p/KpP5/1P1p4/1k1Pp3/1RR1P2P/8 b - -" || // bm #-9
             epd == "8/p7/P1p4p/KpP5/1P1p4/1k1Pp3/R1R1P2P/8 w - -") { // bm #10
    excludeFrom = "e2 h2 d3 b4 a5 c5 a6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e3 d4 b5 c6 a7";
  } else if (epd == "2r5/6B1/8/8/p4p2/k7/8/KR6 b - -" || // bm #-9
             epd == "2r4B/8/8/8/p4p2/k7/8/KR6 w - -") {  // bm #10
    excludeFrom = "a1";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a3 a4";
  } else if (epd == "3B1b2/8/2KNk3/2N5/8/2P5/8/8 b - -" || // bm #-9
             epd == "3B1b2/8/2KNk3/8/4N3/2P5/8/8 w - -") { // bm #10
    excludeFrom = "c3 d6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f8";
  } else if (epd == "5K1N/1pp2PnP/5kPn/8/8/P7/1P6/8 w - -" ||   // bm #-9
             epd == "5K1N/1pp2P1P/5kPn/5n2/8/P7/1P6/8 b - -") { // bm #10
    excludeFrom = "f6 h6";
    excludePromotionTo = "rbn";
    excludeToCapturable = true;
    excludeAllowingFrom = "g6 f7 h7 f8 h8";
  } else if (epd == "5K2/B6p/8/8/2P2PpP/6N1/3RPr1p/4N1kr b - -" ||  // bm #-9
             epd == "5K2/7p/8/8/2P2PpP/4B1N1/3RPr1p/4N1kr w - -") { // bm #10
    excludeFrom = "e1 g3 c4 f4 h4 f8";
    excludePromotionTo = "qrbn";
    excludeToCapturable = true;
    excludeAllowingFrom = "g1 h1 h2 g4";
  } else if (epd == "6R1/8/8/5p2/5p2/5p1k/5P2/7K b - -" || // bm #-9
             epd == "8/8/8/5p2/5p2/5p1k/5P2/6RK w - -") {  // bm #10
    excludeFrom = "f2";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f3 f4 f5";
  } else if (epd == "6b1/3p1p1n/3k4/1P1N1K2/2P5/8/3B1p2/5B2 b - -" || // bm #-9
             epd == "6b1/3p1p1n/3k4/1P1N1K2/2P5/4B3/5p2/5B2 w - -") { // bm #10
    excludeFrom = "b5 d5 f5";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f2 g8";
  } else if (epd == "6kR/1p4p1/p1r1p1P1/r3Bp2/2q2P2/2P5/PP1n1P2/KQR5 b - -" ||  // bm #-9
             epd == "6k1/1p4p1/p1r1p1P1/r3Bp2/2q2P2/2P5/PP1n1P2/KQR4R w - -") { // bm #10
    excludeFrom = "a1 a2 b2 f2 c3 f4 g6";
    excludeAllowingFrom = "d2 c4 a5 f5 a6 c6 e6 b7 g7";
  } else if (epd == "7b/4N3/5p1p/5K1k/5P1p/7p/7p/7N b - -" || // bm #-9
             epd == "6Nb/8/5p1p/5K1k/5P1p/7p/7p/7N w - -") {  // bm #10
    excludeFrom = "f4 f5";
    excludeAllowingFrom = "h2 h4 h5 f6 h6";
  } else if (epd == "7k/6pP/4K1p1/6P1/3B4/8/8/8 b - -" || // bm #-9
             epd == "7k/6pP/6p1/3K2P1/3B4/8/8/8 w - -") { // bm #10
    excludeFrom = "h7";
    excludePromotionTo = "qrbn";
    excludeToCapturable = true;
    excludeAllowingFrom = "g6 g7";
  } else if (epd == "7n/5p2/2N2Np1/2Bpk1K1/3p4/8/4Pp2/8 b - -" || // bm #-9
             epd == "3N3n/5p2/5Np1/2Bpk1K1/3p4/8/4Pp2/8 w - -") { // bm #10
    excludeFrom = "c5 g5";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f2 d4 g6 f7 h8";
  } else if (epd == "8/1k2N3/N7/2K5/8/1Rp5/r7/rbb5 b - -" || // bm #-9
             epd == "8/1k2N3/N7/2K5/8/R1p5/r7/rbb5 w - -") { // bm #10
    excludeFrom = "c5";
    excludeCaptures = true;
    excludeToCapturable = true;
    excludeAllowingFrom = "a1 b1 c1 a2 c3";
  } else if (epd == "8/2p3p1/2p3Kb/2p3p1/2p3P1/8/1pP5/kb2Q3 b - -" || // bm #-9
             epd == "8/2p3p1/2p3Kb/2p3p1/2p3P1/8/1pP5/kb1Q4 w - -") { // bm #10
    excludeFrom = "c2 g4 g6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b1 b2 g5 h6 g7";
  } else if (epd == "8/3K1N2/5k2/6p1/2P3P1/8/7P/8 b - -" || // bm #-9
             epd == "8/5N2/2K2k2/6p1/2P3P1/8/7P/8 w - -") { // bm #10
    excludeFrom = "h2 g4 f7";
    excludePromotionTo = "rbn";
    excludeCaptures = true;
    excludeToCapturable = true;
    excludeAllowingFrom = "g5";
  } else if (epd == "8/5N2/1p3p2/pPk2Pp1/K3p1P1/1P2P3/8/1B6 b - -" || // bm #-9
             epd == "8/5N2/1p3p2/pPk2Pp1/K3p1P1/1P2P3/B7/8 w - -") {  // bm #10
    excludeFrom = "e3 g4 b5 f5 f7";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "8/6p1/6p1/p1p2pP1/P1P2Pp1/P1P3p1/1R4P1/KBk5 b - -" || // bm #-9
             epd == "8/6p1/6p1/p1p2pP1/P1P2Pp1/P1P3p1/BR4P1/K1k5 w - -") { // bm #10
    excludeFrom = "g2 a3 c3 a4 c4 f4 g5";
    excludeCaptures = true;
    excludeToCapturable = true;
    excludeAllowingFrom = "g3 g4 a5 c5 f5 g6 g7";
  } else if (epd == "8/8/2p5/1kP5/1N6/1P1N4/1K6/8 b - -" || // bm #-9
             epd == "8/8/2p5/1kP5/1N6/KP1N4/8/8 w - -") {   // bm #10
    excludeFrom = "d3 c5";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "8/8/8/8/8/1BP3p1/3K2pp/3N2kb b - -" || // bm #-9
             epd == "8/8/8/8/8/1BP3p1/4K1pp/3N2kb w - -") { // bm #10
    excludeFrom = "c3";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 g2 h2 g3";
  } else if (epd == "8/8/8/p7/P1K5/4N3/3p4/1k1N4 b - -" || // bm #-9
             epd == "8/8/8/p7/P7/2K1N3/3p4/1k1N4 w - -") { // bm #10
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d2 a5";
  } else if (epd == "8/K7/8/8/2N5/N1B5/3P4/2kb4 b - -" || // bm #-9
             epd == "8/K7/8/B7/2N5/N7/3P4/2kb4 w - -") {  // bm #10
    excludeFrom = "d2 a3";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c1";
  } else if (epd == "N6K/2p1p1p1/2P1P1Pk/6pP/3p2P1/1P6/3P4/8 b - -" || // bm #-9
             epd == "7K/P1p1p1p1/2P1P1Pk/6pP/3p2P1/1P6/3P4/8 w - -") { // bm #10
    excludeFrom = "d2 b3 g4 h5 h8";
    excludePromotionTo = "qrb";
    excludeCaptures = true;
  } else if (epd == "Q7/2P1RP2/8/p1P1P3/Pp2Pp2/1p2pP2/brp1P3/Rnk2K2 b - -" ||  // bm #-9
             epd == "3Q4/2P1RP2/8/p1P1P3/Pp2Pp2/1p2pP2/brp1P3/Rnk2K2 w - -") { // bm #10
    excludeFrom = "a1 f1 e2 f3 a4 e4 c5 e5 c7 f7";
    excludeCaptures = true;
    excludeAllowingFrom = "b1 a2 b2 c2 b3 e3 b4 f4 a5";
  } else if (epd == "Q7/8/1r6/kp6/1p6/bp6/1p6/1K6 b - -" ||  // bm #-9
             epd == "3Q4/8/1r6/kp6/1p6/bp6/1p6/1K6 w - -") { // bm #10
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b2 a3 b3 b4 b5";
  } else if (epd == "R7/8/7p/5p1p/B1p2p1P/4bp2/1p1r1P2/1Rnk1K2 b - -" ||  // bm #-9
             epd == "R7/8/2B4p/5p1p/2p2p1P/4bp2/1p1r1P2/1Rnk1K2 w - -") { // bm #10
    excludeFrom = "b1 f1 f2 h4";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c1 d1 b2 f3 f4 f5 h5";
  } else if (epd == "Rk6/8/2K5/1p6/1P2B3/8/8/7b b - -" || // bm #-9
             epd == "1k6/8/2K5/1p6/RP2B3/8/8/7b w - -") { // bm #10
    excludePromotionTo = "qrbn";
    excludeAllowingFrom = "h1 b5";
  } else if (epd == "k7/3Q4/8/2p5/1pp5/1pp5/prp5/nbK5 b - -" || // bm #-10
             epd == "k7/2Q5/8/2p5/1pp5/1pp5/prp5/nbK5 w - -") { // bm #11
    excludeFrom = "c1";
    excludeTo = "b2";
    excludeToCapturable = true;
  } else if (epd == "b7/rp6/1p3K2/1p2BB2/rp6/qp5p/pp5P/k7 b - -" ||  // bm #-10
             epd == "b7/rp4K1/1p6/1p2BB2/rp6/qp5p/pp5P/k7 w - -") {  // bm #11
    excludeTo = "b2 b3 b4 b5 b6 b7";
    excludeAllowingTo = "b1";
    excludePromotionTo = "rbn";
    excludeToCapturable = true;
  } else if (epd == "2Bbb3/2pnrp2/2pqkN1K/2prp3/2PpP3/3P4/3R1N2/QRB5 b - -" ||  // bm #-10
             epd == "2Bbb3/2pnrp2/2pqk2K/2prp3/2PpP1N1/3P4/3R1N2/QRB5 w - -") { // bm #11
    excludeFrom = "d3 c4 e4 h6 c8";
    excludeCaptures = true;
    excludeAllowingFrom = "d4 c5 d5 e5 c6 d6 c7 d7 e7 f7 d8 e8";
  } else if (epd == "2K5/6p1/1pnNr1r1/3qp3/P2b3k/1p2pBp1/2nNP3/R2b3R b - -" ||  // bm #-10
             epd == "2K5/6p1/1pnNr1r1/3qp3/P2b3k/1p2pBp1/2nNP3/R2b1R2 w - -") { // bm #11
    excludeFrom = "a1 d2 e2 f3 a4 d6 c8";
    excludeAllowingFrom = "d1 c2 b3 e3 g3 d4 d5 e5 b6 c6 e6 g6 g7";
  } else if (epd == "6K1/8/3N2k1/4p1P1/4P2P/3p3P/3P4/8 b - -" || // bm #-10
             epd == "4N1K1/8/6k1/4p1P1/4P2P/3p3P/3P4/8 w - -") { // bm #11
    excludeFrom = "d2 h3 e4 h4 g5 g8";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d3 e5";
  } else if (epd == "8/2rBp3/3PQ3/3kn3/1B6/2bPr2q/2P1N3/3K4 b - -" ||  // bm #-10
             epd == "8/2rBp3/3P2Q1/3kn3/1B6/2bPr2q/2P1N3/3K4 w - -") { // bm #11
    excludeFrom = "d1 d3 b4 d6 d7";
    excludePromotionTo = "qrbn";
    excludeAllowingFrom = "c3 e3 e5 c7 e7";
  } else if (epd == "2b3N1/3R2p1/3K2p1/r5p1/r7/5B2/6Rk/2q2nN1 b - -" ||  // bm #-10
             epd == "2b3N1/3R2p1/3K2p1/r5p1/r5R1/5B2/7k/2q2nN1 w - -") { // bm #11
    excludeFrom = "g1 f3 d6 g8";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c1 f1 a5 g5 g6 g7";
  } else if (epd == "B7/p2p4/rk1P4/bp6/1p2p3/1P2P3/4P2B/K5R1 b - -" || // bm #-10
             epd == "B7/p2p4/rk1P4/bp6/1p2p3/1P2P3/4P2B/K1R5 w - -") { // bm #11
    excludeFrom = "a1 e2 b3 e3 d6 a8";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b4 e4 a5 b5 a6 a7 d7";
  } else if (epd == "8/8/R1p5/2N5/2P5/pNkP4/rp1p4/rnbK3B b - -" || // bm #-10
             epd == "8/8/2p5/2N5/R1P5/pNkP4/rp1p4/rnbK3B w - -") { // bm #11
    excludeFrom = "d1 b3 d3 c4";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 b1 c1 a2 b2 d2 a3 c6";
  } else if (epd == "5b2/2p1p1pB/2P1P1P1/2p1pN1p/2P1P1kP/3P2p1/4K1P1/8 b - -" || // bm #-10
             epd == "5b2/2p1p1pB/2P1P1P1/2p1pN1p/2P1P1kP/3PK1p1/6P1/8 w - -") {  // bm #11
    excludeFrom = "g2 d3 c4 e4 h4 f5 c6 e6 g6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g3 c5 e5 h5 c7 e7 g7 f8";
  } else if (epd == "2b3r1/6p1/2p1p2p/1n1kQ3/4NrPN/1PPq1P2/3PnK2/2B5 b - -" ||  // bm #-10
             epd == "2b3r1/2Q3p1/2p1p2p/1n1k4/4NrPN/1PPq1P2/3PnK2/2B5 w - -") { // bm #11
    excludeFrom = "f2 b3 f3 e4 g4";
    excludePromotionTo = "qrbn";
    excludeAllowingFrom = "d3 f4 c6 e6 h6 g7 c8 g8";
  } else if (epd == "k7/P2b4/1K3N2/1p6/1P6/8/8/8 b - -" || // bm #-10
             epd == "k7/P2b3N/1K6/1p6/1P6/8/8/8 w - -") {  // bm #11
    excludeFrom = "b4 a7";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b5 a8";
  } else if (epd == "8/p7/2Q5/pr6/k7/1p6/1K6/8 b - -" || // bm #-10
             epd == "8/p2Q4/8/pr6/k7/1p6/1K6/8 w - -") { // bm #11
    excludeFrom = "b2";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b3 a5";
  } else if (epd == "1kb5/1p1p3p/1P1P4/8/8/1p4PP/pP6/K7 b - -" ||   // bm #-10
             epd == "1kb5/1p1p3p/1P1P4/8/8/1p4pP/pP3P2/K7 w - -") { // bm #11
    excludeFrom = "a1 b2 b6 d6";
    excludePromotionTo = "rbn";
    excludeAllowingFrom = "a2 b3 g3 b7 d7 c8";
  } else if (epd == "3kB3/8/2R5/7B/8/8/1p6/bK6 b - -" ||  // bm #-10
             epd == "3kB3/8/2R3B1/8/8/8/1p6/bK6 w - -") { // bm #11
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 b2";
  } else if (epd == "5B2/5K2/8/8/8/8/pn6/kb3R2 b - -" || // bm #-10
             epd == "8/5K2/8/2B5/8/8/pn6/kb3R2 w - -" || // bm #11
             epd == "8/5K2/8/8/8/B7/pn6/kb3R2 w - -") {  // bm #11
    excludeFrom = "f1";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 b1 a2";
  } else if (epd == "5K2/B7/8/8/3P1P1P/5PN1/3RPr1p/4N1kr b - -" || // bm #-10
             epd == "5K2/B7/8/8/5P1P/3P1PN1/3RPr1p/4N1kr w - -") { // bm #11
    excludeFrom = "e1 f3 g3 f4 h4 f8";
    excludePromotionTo = "qrbn";
    excludeToCapturable = true;
    excludeAllowingFrom = "g1 h1 h2";
  } else if (epd == "5k1N/1K2nPn1/8/8/5N1p/4p1p1/4P1pr/6Bb b - -" || // bm #-10
             epd == "K4k1N/4nPn1/8/8/5N1p/4p1p1/4P1pr/6Bb w - -") {  // bm #11
    excludeFrom = "g1 e2 f7 h8";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 g2 e3 g3 f8";
  } else if (epd == "7k/4p2P/4P1K1/2b5/8/8/8/4N3 b - -" || // bm #-10
             epd == "7k/4p2P/4P1K1/2b5/8/8/2N5/8 w - -") { // bm #11
    excludeFrom = "g6 h7";
    excludePromotionTo = "qrbn";
  } else if (epd == "8/1K3Bpp/4N1rq/5Rpk/7p/7P/6Pb/8 b - -" || // bm #-10
             epd == "K7/5Bpp/4N1rq/5Rpk/7p/7P/6Pb/8 w - -") {  // bm #11
    excludeFrom = "g2 h3 f5";
    excludeCaptures = true;
    excludeToCapturable = true;
    excludeAllowingFrom = "h4 g5 h5 h6 g7 h7";
  } else if (epd == "8/4K3/7B/8/8/8/pn6/kb5R b - -" ||  // bm #-10
             epd == "8/4K3/8/8/8/8/pn1B4/kb5R w - -") { // bm #11
    excludeFrom = "h1";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 b1 a2";
  } else if (epd == "8/4bp2/5p1p/1p2pKpk/1p2P3/1P2N1P1/6P1/8 b - -" || // bm #-10
             epd == "8/4bp2/5p1p/1p2pKpk/1p2P1N1/1P4P1/6P1/8 w - -") { // bm #11
    excludeFrom = "g2 b3 e4 f5";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b4 b5 e5 h5 f6 h6 f7";
  } else if (epd == "8/8/4NK2/1B6/4k3/5p2/3P1P2/8 b - -" || // bm #-10
             epd == "8/8/4NK2/8/4k3/5p2/3P1P2/5B2 w - -") { // bm #11
    excludeFrom = "e6";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f3";
  } else if (epd == "8/8/8/1B6/1B6/3K4/pp6/bk6 b - -" || // bm #-10
             epd == "8/8/B7/8/1B6/3K4/pp6/bk6 w - -") {  // bm #11
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 a2 b2";
  } else if (epd == "8/8/8/5p1p/1p3P1p/1PpK1P1P/2P1R2P/5k2 b - -" || // bm #-10
             epd == "8/8/8/5p1p/1p3P1p/1Pp1KP1P/2P1R2P/5k2 w - -") { // bm #11
    excludeFrom = "c2 h2 b3 f3 h3 f4";
    excludeCaptures = true;
    excludeToCapturable = true;
    excludeAllowingFrom = "c3 b4 h4 f5 h5";
  } else if (epd == "8/8/8/8/2Q5/6pk/n5p1/6Kb b - -" || // bm #-10
             epd == "8/8/8/8/3Q4/6pk/n5p1/6Kb w - -") { // bm #11
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 g2 g3";
  } else if (epd == "8/8/p7/p7/p7/R7/8/1K5k b - -" || // bm #-10
             epd == "8/8/p7/p7/p7/R7/8/K6k w - -") {  // bm #11
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a4 a5 a6";
  } else if (epd == "K6R/8/8/8/5B2/p1p5/p1p1p1pk/r7 b - -" || // bm #-10
             epd == "K6R/8/7B/8/8/p1p5/p1p1p1pk/r7 w - -") {  // bm #11
    excludeFrom = "a8";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 a2 c2 e2 g2 a3 c3";
  } else if (epd == "K7/P7/p7/8/3Q4/8/pr6/k7 b - -" || // bm #-10
             epd == "K6Q/P7/p7/8/8/8/pr6/k7 w - -") {  // bm #11
    excludeFrom = "a7 a8";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a2";
  } else if (epd == "k1K5/p7/7N/P7/8/1p6/1P6/8 b - -" || // bm #-10
             epd == "k1K5/p7/7N/8/P7/1p6/1P6/8 w - -") { // bm #11
    excludeFrom = "c8";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b3";
  } else if (epd == "k7/6p1/6Pb/1Q4p1/6P1/6PK/6P1/8 b - -" || // bm #-8
             epd == "k7/6p1/6Pb/6p1/2Q3P1/6PK/6P1/8 w - -" || // bm #9
             epd == "8/7K/8/8/k1B5/2B5/8/8 b - -" ||          // bm #-10
             epd == "8/7K/8/8/k1B5/8/1B6/8 w - -") {          // bm #11
    excludeAllowingCapture = true;
  } else if (epd == "r3k3/8/3K4/8/8/8/8/4Q3 b q -" || // bm #-10
             epd == "r3k3/8/4K3/8/8/8/8/4Q3 w q -") { // bm #11
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a8";
  } else if (epd == "2Q5/8/8/8/8/1p2k1p1/1p1pppp1/1Kbrqbrn b - -" || // bm #-11
             epd == "8/2P5/8/8/8/1p2k1p1/1p1pppp1/1Kbrqbrn w - -") { // bm #12
    if (epd.find(" w ") != std::string::npos)
      openingMoves = "c7c8q";
    excludeFrom = "b1";
    excludeToCapturable = true;
  } else if (epd == "1k6/2pNN3/1p6/n7/1P1pB3/1Pr4p/1nP4P/b2br1BK b - -" ||   // bm #-11
             epd == "1k6/2p1N3/1p3N2/n7/1P1pB3/1Pr4p/1nP4P/b2br1BK w - -") { // bm #12
    excludeFrom = "g1 h1 c2 h2 b3 e4";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeToAttacked = true;
    excludeAllowingFrom = "a1 d1 e1 b2 c3 h3 d4 a5 b6 c7";
  } else if (epd == "3nn3/2p2p1k/1p1pp1p1/p2B2Np/r2B2N1/4P3/3PKPP1/6q1 b - -" || // bm #-11
             epd == "3nn3/2p2p1k/1p1pp1p1/p2B3p/r2B2N1/4P2N/3PKPP1/6q1 w - -") { // bm #12
    excludeFrom = "d2 e2 f2 g2 e3 d4 d5";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g1 a4 a5 h5 b6 d6 e6 g6 c7 f7 d8 e8";
  } else if (epd == "3nn3/2p2p1k/1p1pp1p1/p2B2Np/r2B2N1/8/8/7K b - -" || // bm #-11
             epd == "3nn3/2p2p1k/1p1pp1p1/p2B3p/r2B2N1/7N/8/7K w - -") { // bm #12
    excludeFrom = "h1 d4 d5";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a4 a5 h5 b6 d6 e6 g6 c7 f7 d8 e8";
  } else if (epd == "8/8/8/8/3Q4/8/pp1KPPP1/k7 b - -" || // bm #-9
             epd == "8/8/8/8/8/3Q4/pp1KPPP1/k7 w - -" || // bm #10
             epd == "8/8/8/8/8/2Q5/pp1KPPP1/k7 b - -" || // bm #-11
             epd == "8/8/8/8/8/1Q6/pp1KPPP1/k7 w - -" || // bm #12
             epd == "8/8/8/8/8/8/ppQKPPP1/k7 w - -") {   // bm #12
    excludeFrom = "d2 e2 f2 g2";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a2 b2";
  } else if (epd == "2q5/2pp4/3pr3/4pb2/K1p2pn1/2bn1kp1/3prR2/6BB b - -" ||  // bm #-11
             epd == "2q5/2pp4/3pr3/4pb2/K1p2pn1/2bn1kp1/3pr1R1/6BB w - -") { // bm #12
    excludeFrom = "g1 h1 a4";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d2 e2 c3 d3 g3 c4 f4 g4 e5 f5 d6 e6 c7 d7 c8";
  } else if (epd == "b2b2R1/1np1kp1p/1p6/n2K1N2/p5N1/5pP1/P1p3pr/2B2rq1 b - -" || // bm #-11
             epd == "b2b2R1/1np1kp1p/1p6/n2K4/p2N2N1/5pP1/P1p3pr/2B2rq1 w - -") { // bm #12
    excludeFrom = "c1 a2 g3 d5 g8";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f1 g1 c2 g2 h2 f3 a4 a5 b6 b7 c7 f7 h7 a8 d8";
  } else if (epd == "1b3r1k/5pp1/6B1/5p2/4p3/2pp3R/2r5/KQ6 b - -" ||  // bm #-11
             epd == "1b3r1k/5pp1/6p1/5p1B/4p3/2pp3R/2r5/KQ6 w - -") { // bm #12
    excludeFrom = "a1 h3";
    excludeAllowingFrom = "c2 c3 d3 e4 f5 g6 f7 g7 b8 f8";
  } else if (epd == "q4b1k/1n5p/5QpP/8/8/Bp6/2P5/K7 b - -" ||  // bm #-11
             epd == "q4b1k/1n3Q1p/6pP/8/8/Bp6/2P5/K7 w - -") { // bm #12
    excludeFrom = "a1 c2";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b3 g6 b7 h7 a8";
  } else if (epd == "3r1bk1/1p4pn/p1pN1p1p/R3p1r1/K6n/8/BP3q2/7R b - -" ||   // bm #-11
             epd == "3r1bk1/1p4pn/p1pp1p1p/R3p1r1/K1N4n/8/BP3q2/7R w - -") { // bm #12
    excludeFrom = "a2 b2 a4";
    excludeAllowingFrom = "f2 e5 g5 a6 c6 d6 f6 h6 b7 g7 h7 d8 f8";
  } else if (epd == "8/8/8/2P2B2/5B2/3P2PP/3p1kp1/3Kbrrb b - -" || // bm #-11
             epd == "8/8/8/2P5/4BB2/3P2PP/3p1kp1/3Kbrrb w - -") {  // bm #12
    excludeFrom = "d1 d3 g3 h3";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e1 f1 g1 h1 d2 g2";
  } else if (epd == "8/3p4/p2P4/P7/p2K1N2/rp2p1pp/1p2Bkbr/bR5n b - -" || // bm #-11
             epd == "8/3p4/p2P4/P7/p4N2/rpK1p1pp/1p2Bkbr/bR5n w - -") {  // bm #12
    excludeFrom = "e2 f4 a5 d6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 h1 b2 f2 h2 b3 e3 g3 h3 a6 d7";
  } else if (epd == "4k1nr/4p1pr/R3NpPp/1p3P1K/1p6/1P6/8/8 b - -" ||  // bm #-11
             epd == "4k1nr/4p1pr/2R1NpPp/1p3P1K/1p6/1P6/8/8 w - -") { // bm #12
    excludeFrom = "f5 h5 e6 g6";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b4 b5 f6 h6 e7 g7 h7 g8 h8";
  } else if (epd == "8/2p5/2P5/3p1p2/3P1p2/2P1kp2/B1K1prp1/4RbN1 b - -" ||  // bm #-11
             epd == "8/2p5/2P5/3p1p2/3P1p2/2P1kp2/2K1prp1/1B2RbN1 w - -") { // bm #12
    excludeFrom = "e1 g1 c3 d4 c6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f1 e2 f2 g2 f3 f4 d5 f5 c7";
  } else if (epd == "8/8/8/3B4/NPP5/pP3N2/p1p5/rbk2K2 b - -" || // bm #-11
             epd == "8/8/8/3B4/NP6/pPP2N2/p1p5/rbk2K2 w - -") { // bm #12
    excludeFrom = "f1 b3 f3 b4 d5";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 b1 a2 c2 a3";
  } else if (epd == "4K3/8/2p2p2/1pPk1p2/1P1p1P2/3P1P2/3P4/1N6 b - -" || // bm #-11
             epd == "8/5K2/2p2p2/1pPk1p2/1P1p1P2/3P1P2/3P4/1N6 w - -") { // bm #12
    excludeFrom = "d2 d3 f3 b4 f4 c5";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d4 b5 f5 c6 f6";
  } else if (epd == "4N3/pp2p2B/qk2P2K/bp2R3/1p6/1P6/1P4P1/8 b - -" || // bm #-11
             epd == "4N3/pp2p1KB/qk2P3/bp2R3/1p6/1P6/1P4P1/8 w - -") { // bm #12
    excludeFrom = "b2 g2 b3 e6 e8";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b4 a5 b5 a6 a7 b7 e7";
  } else if (epd == "8/8/2p2p2/2Pk1Pp1/1P4P1/2pPP3/2P1K3/8 b - -" || // bm #-11
             epd == "8/8/2p2p2/2Pk1Pp1/1P4P1/2pPPK2/2P5/8 w - -") {  // bm #12
    excludeFrom = "e3 b4 g4 c5 f5";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c3 g5 c6 f6";
  } else if (epd == "2Q3k1/4p1rp/3p4/4P2K/4P3/3P4/2Pb1P2/5q2 b - -" || // bm #-11
             epd == "6k1/2Q1p1rp/3p4/4P2K/4P3/3P4/2Pb1P2/5q2 w - -") { // bm #12
    excludeFrom = "c2 f2 d3 e4 h5";
    excludePromotionTo = "qrbn";
    excludeToCapturable = true;
    excludeAllowingFrom = "f1 d2 d6 e7 h7";
  } else if (epd == "3k4/b2P1p2/2KP4/1N6/8/8/8/8 b - -" || // bm #-11
             epd == "3k4/b1NP1p2/2KP4/8/8/8/8/8 w - -") {  // bm #12
    excludeFrom = "d6 d7";
    excludeToCapturable = true;
    excludeAllowingFrom = "d8";
  } else if (epd == "4k3/1p4R1/bp1p4/1p1p1pB1/1K1P1p2/5P2/8/8 b - -" ||  // bm #-11
             epd == "4k3/1p4R1/bp1p1B2/1p1p1p2/1K1P1p2/5P2/8/8 w - -") { // bm #12
    excludeFrom = "f3 b4 d4";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f4 b5 d5 f5 a6 b6 d6 b7";
  } else if (epd == "5B2/8/8/8/7R/7P/6p1/4K1kb b - -" || // bm #-11
             epd == "8/8/7B/8/7R/7P/6p1/4K1kb w - -") {  // bm #12
    excludeFrom = "h3";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 g2";
  } else if (epd == "6B1/5p2/5R2/5PK1/5p2/4prp1/4prp1/4Nbk1 b - -" || // bm #-11
             epd == "8/5pP1/5R2/5PK1/5p2/4prp1/4prp1/4Nbk1 w - -") {  // bm #12
    excludeFrom = "e1 f5";
    excludePromotionTo = "qrn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f1 e2 f2 g2 e3 f3 g3 f4";
  } else if (epd == "6N1/8/6b1/6Np/5K1k/8/5P2/8 b - -" || // bm #-11
             epd == "8/8/6bN/6Np/5K1k/8/5P2/8 w - -") {   // bm #12
    excludeFrom = "f4";
    excludeCaptures = true;
    excludeAllowingFrom = "h4 h5";
  } else if (epd == "7K/6B1/8/6p1/6pb/p2p2p1/pqpP2pr/knQ3Rb b - -" || // bm #-11
             epd == "7K/8/8/6p1/6pb/p1Bp2p1/pqpP2pr/knQ3Rb w - -") {  // bm #12
    excludeFrom = "g1 d2";
    excludeAllowingFrom = "a1 b1 h1 a2 c2 g2 d3 g3 g4 h4 g5";
  } else if (epd == "8/2p2K2/2p3Np/2Pp1k1P/5p1P/3p1p2/3P1N2/8 b - -" || // bm #-11
             epd == "7N/2p2K2/2p4p/2Pp1k1P/5p1P/3p1p2/3P1N2/8 w - -") { // bm #12
    excludeFrom = "h4 c5 h5 f7";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d3 f3 f4 c6 h6";
  } else if (epd == "8/2p5/2p2p2/2Pk1Pp1/1P4P1/2pPP3/2P1K3/8 b - -" || // bm #-11
             epd == "8/2p5/2p2p2/2Pk1Pp1/1P4P1/2pPPK2/2P5/8 w - -") {  // bm #12
    excludeFrom = "e3 b4 g4 c5 f5";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c3 g5 c6 f6 c7";
  } else if (epd == "8/3p4/8/Q7/8/1r6/kp1p4/qrbK4 b - -" || // bm #-11
             epd == "8/2Qp4/8/8/8/1r6/kp1p4/qrbK4 w - -") { // bm #12
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 b1 c1 b2 d2";
  } else if (epd == "8/3p4/pp1B4/k7/p1P5/P1bp4/8/2K5 b - -" || // bm #-11
             epd == "8/3p4/pp1B4/k7/p1P5/P1bp4/8/3K4 w - -") { // bm #12
    excludeFrom = "a3 c4";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a4 a5 a6 d7";
  } else if (epd == "8/6K1/p5p1/6p1/B7/3Np3/p1ppP3/rb1k2N1 b - -" || // bm #-11
             epd == "7K/8/p5p1/6p1/B7/3Np3/p1ppP3/rb1k2N1 w - -") {  // bm #12
    excludeFrom = "g1 e2 d3";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 d1 c2 d2 e3";
  } else if (epd == "8/8/6p1/7b/8/8/p1p5/k1K4R b - -" || // bm #-11
             epd == "8/8/6p1/7b/7R/8/p1p5/k1K5 w - -") { // bm #12
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 a2 c2";
  } else if (epd == "8/8/6pB/4K1Pk/3p3p/3p3p/3P3P/8 b - -" || // bm #-11
             epd == "8/8/6pB/6Pk/3p1K1p/3p3p/3P3P/8 w - -") { // bm #12
    excludeFrom = "h2 g5 h6";
    excludePromotionTo = "rbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d3 h3 d4 h4 g6";
  } else if (epd == "8/8/8/3p4/3P2B1/2p1K1B1/2P3pP/6kb b - -" || // bm #-11
             epd == "8/8/8/3p4/3P4/2p1K1B1/2P1B1pP/6kb w - -") { // bm #12
    excludeFrom = "c2 h2 d4";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 g2 c3 d5";
  } else if (epd == "8/8/8/4K1B1/8/1B2P3/Pp6/bk6 b - -" || // bm #-11
             epd == "8/8/3K4/6B1/8/1B2P3/Pp6/bk6 w - -") { // bm #12
    excludeFrom = "a2";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 b2";
  } else if (epd == "K7/Q7/6p1/6pb/6p1/6pr/3N1qpb/3N2kr b - -" ||  // bm #-11
             epd == "K7/8/6p1/6pb/6p1/4Q1pr/3N1qpb/3N2kr w - -") { // bm #12
    excludeFrom = "d2";
    excludeAllowingFrom = "g1 h1 g2 h2 g3 g4 g5 h5 g6";
  } else if (epd == "b7/p4p2/p2K4/P2p4/N2kpP2/1P6/1p6/bB3N2 b - -" || // bm #-11
             epd == "b7/p1K2p2/p7/P2p4/N2kpP2/1P6/1p6/bB3N2 w - -") { // bm #12
    excludeFrom = "b1 b3 a4 f4 a5";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 b2 d4 d5 a6 a7";
  } else if (epd == "k1K5/1p6/1p6/1p6/1P6/8/8/1B6 b - -" || // bm #-11
             epd == "k1K5/1p6/1p6/1p6/8/8/1P6/1B6 w - -") { // bm #12
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b5 b6 b7";
  } else if (epd == "8/8/1p6/1p6/1p6/1p3N2/pppbK3/rbk5 b - -" || // bm #-12
             epd == "8/8/1p6/1p6/1p6/1p6/pppbK3/rbk3N1 w - -") { // bm #13
    excludeFrom = "e2";
    excludeToCapturable = true;
  } else if (epd == "2Nq1nKB/R3kPRB/N3p3/1p1n2p1/2r2p2/1p2b3/P2pb2N/3r4 b - -" ||  // bm #-12
             epd == "3q1nKB/R1P1kPRB/N3p3/1p1n2p1/2r2p2/1p2b3/P2pb2N/3r4 w - -") { // bm #13
    excludeFrom = "a2 h2 f7 h7 g8";
    excludeCaptures = true;
    excludeAllowingFrom = "d1 d2 e2 b3 e3 c4 f4 b5 d5 g5 e6 d8 f8";
  } else if (epd == "5b2/4p1p1/p2pPkP1/P2P4/P3pNK1/P3P3/7P/8 b - -" || // bm #-12
             epd == "5b2/4p1p1/p2pPkP1/P7/P2PpNK1/P3P3/7P/8 w - -") {  // bm #13
    excludeFrom = "a3 e3 a4 g4 a5 e6 g6";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e4 a6 d6 e7 g7 f8";
  } else if (epd == "N4nqb/3p1krr/3Pp1bn/4Pp1p/5PpP/1K4P1/8/R7 b - -" ||  // bm #-12
             epd == "5nqb/3p1krr/1N1Pp1bn/4Pp1p/5PpP/1K4P1/8/R7 w - -") { // bm #13
    excludeFrom = "b3 g3 f4 h4 e5 d6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g4 f5 h5 e6 g6 h6 d7 g7 h7 f8 g8 h8";
  } else if (epd == "R7/8/2p5/2N5/2P5/pNkP4/rp1p4/rn1K3B b - -" ||  // bm #-12
             epd == "1R6/8/2p5/2N5/2P5/pNkP4/rp1p4/rn1K3B w - -") { // bm #13
    excludeFrom = "d1 b3 d3 c4";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 b1 a2 b2 d2 a3 c6";
  } else if (epd == "8/3p4/1N1p1p2/K1kP1p2/5P2/3p4/N2P4/8 b - -" ||  // bm #-12
             epd == "8/3p4/1N1p1p2/K1kP1p2/1N3P2/3p4/3P4/8 w - -") { // bm #13
    excludeFrom = "d2 f4 a5 d5 b6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d3 f5 d6 f6 d7";
  } else if (epd == "8/8/2B1p3/2p1p1p1/2p1p1P1/2p1N1P1/2P1Pp2/2K2N1k b - -" || // bm #-12
             epd == "8/8/2B1p3/2p1p1p1/2p1p1P1/2p1N1P1/2P1Pp2/3K1N1k w - -") { // bm #13
    excludeFrom = "f1 c2 e2 e3 g3 g4 c6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f2 c3 c4 e4 c5 e5 g5 e6";
  } else if (epd == "4N3/8/3KNk2/4N3/4q3/8/3P4/8 b - -" || // bm #-12
             epd == "8/4P3/3KNk2/4N3/4q3/8/3P4/8 w - -") { // bm #13
    excludeFrom = "d2 e5 e6";
    excludeAllowingCapture = true;
  } else if (epd == "r5k1/7p/2pP2pP/1p6/1q3B2/1p2Q1P1/1P6/1rK1R2R w - -" || // bm #-12
             epd == "r5k1/7p/2pP2pP/1p6/1q3B2/1p2Q1P1/1P6/rNK1R2R b - -") { // bm #13
    excludeFrom = "b3 b5 g6 h7 g8";
    excludePromotionTo = "qrbn";
    excludeAllowingFrom = "e1 h1 b2 e3 g3 f4 d6 h6";
  } else if (epd == "8/1p1p4/2q2rb1/7p/8/2Bp3k/4P1NQ/1n5K b - -" ||  // bm #-12
             epd == "8/1p1p4/2q2rb1/7p/8/2Bp3k/4P1N1/1n4QK w - -") { // bm #13
    excludeFrom = "h1 e2 g2 c3";
    excludeAllowingFrom = "b1 d3 h5 c6 f6 g6 b7 d7";
  } else if (epd == "5K2/8/2p2p2/1pPk1p2/1P1p1P2/3P1P1N/3P4/8 b - -" || // bm #-12
             epd == "8/5K2/2p2p2/1pPk1p2/1P1p1P2/3P1P1N/3P4/8 w - -") { // bm #13
    excludeFrom = "d2 d3 f3 b4 f4 c5";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d4 b5 f5 c6 f6";
  } else if (epd == "2k5/R6p/N2p2pr/3p2pb/3p2p1/3P2K1/8/8 b - -" || // bm #-12
             epd == "2k5/6Rp/N2p2pr/3p2pb/3p2p1/3P2K1/8/8 w - -") { // bm #13
    excludeFrom = "d3 g3";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d4 g4 d5 g5 h5 d6 g6 h6 h7";
  } else if (epd == "6kr/P1p4p/2P2KbB/8/8/8/8/8 b - -" || // bm #-12
             epd == "6kr/2p4p/P1P2KbB/8/8/8/8/8 w - -") { // bm #13
    excludeFrom = "c6 h6";
    excludePromotionTo = "qrb";
    excludeToCapturable = true;
    excludeAllowingFrom = "c7 h7 g8 h8";
  } else if (epd == "7K/2p1p1p1/2PpP1Pk/P5pP/1p4P1/8/1P1P4/8 b - -" || // bm #-12
             epd == "7K/2p1p1p1/2PpP1Pk/6pP/Pp4P1/8/1P1P4/8 w - -") {  // bm #13
    excludeFrom = "b2 d2 g4 h5 h8";
    excludePromotionTo = "qrb";
    excludeCaptures = true;
  } else if (epd == "7K/p1p1p1p1/1pP1P1Pk/1P4pB/6P1/3p2P1/1P1P4/8 b - -" || // bm #-12
             epd == "7K/p1p1p1p1/1pP1P1Pk/1P4pB/6P1/3p4/1P1P2P1/8 w - -") { // bm #13
    excludeFrom = "b2 d2 g4 h5 h8";
    excludePromotionTo = "qrb";
  } else if (epd == "8/3p3p/3P3p/1N1Pp2p/1N2P2P/3B4/pp1k1PP1/rrb2K2 b - -" || // bm #-12
             epd == "8/3p3p/3P3p/1N1Pp2p/1N2P2P/8/ppBk1PP1/rrb2K2 w - -") {   // bm #13
    excludeFrom = "f1 f2 e4 h4 d5 d6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 a2 b2 e5 h5 h6 d7 h7";
  } else if (epd == "8/4b2N/8/8/p1K5/kp6/1p6/1B6 b - -" || // bm #-12
             epd == "8/4b2N/8/1K6/p7/kp6/1p6/1B6 b - -" || // bm #-12
             epd == "8/4b2N/8/1K6/p7/kp6/1p6/1B6 w - -" || // bm #13
             epd == "8/4b2N/8/8/p1K5/kp6/1p6/1B6 w - -") { // bm #13
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b2 a3 b3 a4";
  } else if (epd == "8/8/1p3N2/1p2p3/1P1kPp2/5P2/2PB1p1p/5Kbr b - -" || // bm #-12
             epd == "8/3N4/1p6/1p2p3/1P1kPp2/5P2/2PB1p1p/5Kbr w - -") { // bm #13
    excludeFrom = "f1 f3 b4 e4";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g1 h1 f2 h2 f4 b5 e5 b6";
  } else if (epd == "B1K5/8/8/8/1p6/6P1/1P4np/R5bk b - -" || // bm #-12
             epd == "2K5/8/8/8/1p6/5BP1/1P4np/R5bk w - -") { // bm #13
    excludeFrom = "a1 b2 g3";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 h2";
  } else if (epd == "K5Rr/8/8/3B4/1B6/8/8/1r5k b - -" || // bm #-12
             epd == "K5Rr/8/8/8/1B6/1B6/8/1r5k w - -") { // bm #13
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b1 h8";
  } else if (epd == "n1B5/1k2N2p/3K4/4P2P/pp1B1b1P/2ppp3/1N2n1PP/8 b - -" || // bm #-12
             epd == "n7/1k1BN2p/3K4/4P2P/pp1B1b1P/2ppp3/1N2n1PP/8 w - -") {  // bm #13
    excludeFrom = "h4 e5 h5 d6";
    excludeCaptures = true;
    excludeAllowingFrom = "e2 c3 d3 e3 a4 b4 f4 h7 a8";
  } else if (epd == "3n4/B1p3p1/4prB1/n1P5/p1pP1rp1/4b3/1pPNNp1K/1k5Q b - -" ||   // bm #-13
             epd == "3n4/B1p3p1/4prB1/n1P5/p1pP1rp1/4b3/1pP1Np1K/1k3N1Q w - -") { // bm #14
    excludeFrom = "h1 c2 d4 c5 g6";
    excludeCaptures = true;
    excludeAllowingFrom = "b2 f2 e3 a4 c4 f4 g4 a5 e6 f6 c7 g7 d8";
  } else if (epd == "krnb4/qn2p1N1/bp1pK3/1p1BpP2/1p2P3/1N2P3/4P3/8 b - -" || // bm #-13
             epd == "krnbN3/qn2p3/bp1pK3/1p1BpP2/1p2P3/1N2P3/4P3/8 w - -") {  // bm #14
    excludeFrom = "e2 e3 e4 d5 f5 e6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b4 b5 e5 a6 b6 d6 a7 b7 e7 a8 b8 c8";
  } else if (epd == "8/3R4/b2pn2p/2p3nk/1p4p1/p2BP1N1/r2PN1P1/3K4 b - -" || // bm #-13
             epd == "8/3R4/b2pn2p/2p3nk/1p2N1p1/p2BP3/r2PN1P1/3K4 w - -") { // bm #14
    excludeFrom = "d1 d2 g2 d3 e3 d7";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a2 a3 b4 g4 c5 g5 a6 d6 e6 h6";
  } else if (epd == "8/8/p1K5/k2n4/1p1B4/1P6/8/8 b - -" || // bm #-13
             epd == "8/8/p1K5/k2n4/1p6/1P6/5B2/8 w - -") { // bm #14
    excludeFrom = "b3";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b4 a5 a6";
  } else if (epd == "1Q5k/5p2/5K2/8/8/8/4q3/8 b - -" || // bm #-13
             epd == "7k/5p2/1Q3K2/8/8/8/4q3/8 w - -") { // bm #14
    excludeFrom = "f6";
    excludeAllowingCapture = true;
  } else if (epd == "8/6p1/2p3pb/2p3p1/2BkN1K1/N4P2/5P2/8 b - -" || // bm #-13
             epd == "8/6p1/2p3pb/2p3p1/2Bk2K1/N4PN1/5P2/8 w - -") { // bm #14
    excludeFrom = "f2 f3 c4 g4";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c5 g5 c6 g6 h6 g7";
  } else if (epd == "8/8/1K6/p7/kp2b3/1p6/1P6/4N3 b - -" || // bm #-12
             epd == "8/8/1K6/p7/kp6/1p6/1P2b2N/8 b - -" ||  // bm #-12
             epd == "8/8/1K6/p7/kp4b1/1p6/1P6/6N1 b - -" || // bm #-12
             epd == "8/8/8/p1K5/kp2b3/1p6/1P6/4N3 w - -" || // bm #13
             epd == "8/8/8/p1K5/kp6/1p6/1P2b2N/8 w - -" ||  // bm #13
             epd == "8/8/8/p1K5/kp4b1/1p6/1P6/6N1 w - -" || // bm #13
             epd == "8/8/8/p1K5/kp4b1/1p6/1P6/6N1 b - -" || // bm #-13
             epd == "8/8/8/p1K5/kp2b3/1p6/1P6/4N3 b - -" || // bm #-13
             epd == "8/8/1K6/p7/kp4b1/1p6/1P6/6N1 w - -" || // bm #14
             epd == "8/8/1K6/p7/kp2b3/1p6/1P6/4N3 w - -") { // bm #14
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b3 a4 b4 a5";
  } else if (epd == "8/8/2p1p3/2p1Np2/2Pk1P1K/8/3P2p1/6Nb b - -" || // bm #-13
             epd == "8/8/2p1p3/2p1NpK1/2Pk1P2/8/3P2p1/6Nb w - -") { // bm #14
    excludeFrom = "c4 f4 e5";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 g2 c5 f5 c6 e6";
  } else if (epd == "kq4n1/4p3/1P2P3/1K6/4Q3/8/p7/8 b - -" || // bm #-13
             epd == "kq4n1/4p2Q/1P2P3/1K6/8/8/p7/8 w - -") {  // bm #14
    excludeFrom = "b5 b6 e6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a2 e7 g8";
  } else if (epd == "rk4N1/p7/B1K5/6b1/8/8/8/8 b - -" || // bm #-12
             epd == "rk4N1/p2K4/B7/6b1/8/8/8/8 w - -" || // bm #13
             epd == "rk4N1/p2K4/B7/6b1/8/8/8/8 b - -" || // bm #-13
             epd == "rk4N1/p7/B1K5/6b1/8/8/8/8 w - -") { // bm #14
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a7 a8 b8";
  } else if (epd == "8/8/8/6r1/8/8/p1p5/k1KbB3 b - -" ||              // bm #-6
             epd == "8/8/8/6r1/8/6B1/p1p5/k1Kb4 w - -" ||             // bm #7
             epd == "k7/2Q5/2p5/2p5/2p5/6p1/2p1ppp1/2Kbrqrn b - -" || // bm #-14
             epd == "k7/8/1Qp5/2p5/2p5/6p1/2p1ppp1/2Kbrqrn w - -") {  // bm #15
    excludeFrom = "c1";
    excludeToCapturable = true;
  } else if (epd == "8/8/8/2p5/1pp5/brpp3P/1pprp3/qnkbK3 b - -" || // bm #-14
             epd == "8/8/8/2p5/1pp5/brpp4/1pprp2P/qnkbK3 w - -") { // bm #15
    excludeFrom = "e1";
    excludePromotionTo = "qrb";
    excludeToCapturable = true;
  } else if (epd == "4k3/8/5Q2/8/5p2/1p1p1p2/1ppp1p2/nrqrbK2 b - -" || // bm #-14
             epd == "4k3/6Q1/8/8/5p2/1p1p1p2/1ppp1p2/nrqrbK2 w - -") { // bm #15
    excludeFrom = "f1";
    excludeToCapturable = true;
  } else if (epd == "B7/p2p4/rk1P4/bp6/1p2p3/1P2P3/K2RP2B/8 b - -" ||  // bm #-14
             epd == "B7/p2p4/rk1P4/bp6/1p2p3/1P2P3/K1R1P2B/8 w - -") { // bm #15
    excludeFrom = "a2 e2 b3 e3 d6 a8";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b4 e4 a5 b5 a6 a7 d7";
  } else if (epd == "7B/8/3P2N1/3P2R1/3p1P2/1ppP4/1ppkPp2/nrqrbK2 b - -" || // bm #-14
             epd == "7B/8/3P2N1/3P4/3p1P2/1ppP2R1/1ppkPp2/nrqrbK2 w - -") { // bm #15
    excludeFrom = "f1 e2 d3 d5 d6 g6";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 b1 c1 d1 e1 b2 c2 f2 b3 c3 d4";
  } else if (epd == "5brq/4p1pb/4P1kr/7p/3p3B/2pP3P/B1P5/1R4K1 b - -" || // bm #-14
             epd == "5brq/4p1pb/4P1kr/1R5p/3p3B/2pP3P/B1P5/6K1 w - -") { // bm #15
    excludeFrom = "c2 d3 h3 e6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c3 d4 h5 h6 e7 g7 h7 f8 g8 h8";
  } else if (epd == "8/2p1p3/2P1p3/4Nkp1/2P3Nb/1pp1P1p1/1pr3P1/nB3K1R b - -" || // bm #-14
             epd == "8/2p1p3/2P1p3/4Nkp1/2P3Nb/1pp1P1p1/1pr3P1/nB4KR w - -") {  // bm #15
    excludeFrom = "g2 e3 c4 g4 e5 c6";
    excludeAllowingFrom = "b2 c2 b3 c3 g3 h4 g5 e6 c7 e7";
  } else if (epd == "8/5p2/5p2/8/P1p2Pp1/1pPp2Pp/1P1P1K1k/8 b - -" || // bm #-14
             epd == "8/5p2/5p2/8/2p2Pp1/1pPp2Pp/PP1P1K1k/8 w - -") {  // bm #15
    excludeFrom = "b2 d2 f2 g3 f4";
    excludePromotionTo = "bn";
    excludeAllowingCapture = true;
  } else if (epd == "8/8/4p3/P3P3/1pp5/bprp4/qpprp3/1nkbK3 b - -" || // bm #-14
             epd == "8/8/4p3/4P3/Ppp5/bprp4/qpprp3/1nkbK3 w - -") {  // bm #15
    excludePromotionTo = "qrb";
    excludeAllowingFrom = "b1 c1 d1 b2 c2 d2 e2 a3 b3 d3 b4 c4 e6";
  } else if (epd == "8/8/8/2p5/1pp5/brpp4/qpprp2P/1nkbK3 b - -" || // bm #-15
             epd == "8/8/8/2p5/1pp5/brpp4/qpprp2P/1nkbnK2 w - -") { // bm #16
    if (epd.find(" w ") != std::string::npos)
      openingMoves = "f1e1";
    excludeFrom = "e1";
    excludePromotionTo = "qrb";
    excludeToCapturable = true;
  } else if (epd == "8/8/8/2p5/1pp5/brpp4/qpprpK1P/1nkbn3 w - -") { // bm #16
    openingMoves = "f2e1";
    excludeFrom = "e1";
    excludePromotionTo = "qrb";
    excludeToCapturable = true;
  } else if (epd == "8/4N2B/8/1p1PB3/3P1ppP/1P3krb/4pPpq/4K1nr b - -" ||  // bm #-15
             epd == "8/4N3/8/1p1PB3/3P1ppP/1P3krb/4pPpq/1B2K1nr w - -") { // bm #16
    excludeFrom = "e1 f2 b3 d4 h4";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g1 h1 e2 g2 h2 g3 h3 g4";
  } else if (epd == "8/6p1/4p1pb/1p1pN1p1/brpPk1P1/1pP5/1K2BP2/8 b - -" ||  // bm #-15
             epd == "8/6p1/4p1pb/1p1p2p1/brpPk1P1/1pP2N2/1K2BP2/8 w - -") { // bm #16
    excludeFrom = "b2 c3 d4 g4";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b3 a4 b4 c4 b5 d5 g5 g6 h6 g7";
  } else if (epd == "8/8/8/r7/p7/6p1/p1pB4/k1K5 b - -" ||   // bm #-7
             epd == "8/8/8/r7/p4B2/6p1/p1p5/k1K5 w - -" ||  // bm #8
             epd == "8/3p4/3p4/3Q4/3p4/1r6/k7/2K5 b - -" || // bm #-15
             epd == "8/3p1Q2/3p4/8/3p4/1r6/k7/2K5 w - -") { // bm #16
    excludeFrom = "c1";
    excludeAllowingCapture = true;
  } else if (epd == "8/R7/8/2p5/2P5/P5p1/P3K1p1/6k1 b - -" || // bm #-15
             epd == "8/8/8/2p5/R1P5/P5p1/P3K1p1/6k1 w - -") { // bm #16
    excludeFrom = "a2 c4";
    excludePromotionTo = "rbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g2 g3 c5";
  } else if (epd == "8/b1p3p1/1pP2pP1/1p3p2/1P3P2/p1B5/pr3P2/kbR1K3 b - -" || // bm #-15
             epd == "8/b1p3p1/1pP2pP1/1p3p2/1P3P2/p1B5/pr3P2/kbRK4 w - -") {  // bm #16
    excludeFrom = "c1 f2 b4 f4 c6";
    excludePromotionTo = "rbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 b1 a2 b2 a3 b5 f5 b6 f6 c7 g7";
  } else if (epd == "8/pp6/qp6/rk1P4/bP6/Kp1RP1P1/1P2P3/7B b - -" || // bm #-16
             epd == "8/pp6/qp6/rk1P4/bP6/KpR1P1P1/1P2P3/7B w - -") { // bm #17
    excludeFrom = "b2 e2 a3 e3 g3 b4 d5";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b3 a4 a5 a6 b6 a7 b7";
  } else if (epd == "qn6/rp1p4/pPkP1R2/K7/P1P5/2PP4/4P3/2B5 b - -" || // bm #-16
             epd == "qn6/rp1p4/pPkP1R2/K7/P1P5/B1PP4/4P3/8 w - -") {  // bm #17
    excludeFrom = "c3 d3 a4 c4 a5 b6 d6";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a6 a7 b7 d7 a8 b8";
  } else if (epd == "7k/1nppK2p/7P/4Q3/8/8/3pprr1/8 b - -" || // bm #-16
             epd == "7k/1nppK2p/7P/7Q/8/8/3pprr1/8 w - -") {  // bm #17
    excludeFrom = "h6 e7";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d2 e2 c7 h7";
  } else if (epd == "8/R7/8/1p1p1p1p/k7/5p1b/pBK2P2/8 b - -" ||  // bm #-16
             epd == "8/6R1/8/1p1p1p1p/k7/5p1b/pBK2P2/8 w - -") { // bm #17
    excludeFrom = "c2 f2";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a2 f3 h3 b5 d5 f5 h5";
  } else if (epd == "5bk1/p3p1p1/p3P1P1/p7/p7/8/P3K3/8 b - -" || // bm #-16
             epd == "5bk1/p3p1p1/p3P1P1/p7/p7/8/P7/5K2 w - -") { // bm #17
    excludeFrom = "e6 g6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e7 g7 f8";
  } else if (epd == "4bkrb/1K2p1r1/4P2B/8/8/8/8/R7 b - -" || // bm #-16
             epd == "4bkrb/1K2p1r1/4P2B/8/8/8/8/7R w - -") { // bm #17
    excludeFrom = "e6 h6";
    excludeToCapturable = true;
    excludeAllowingFrom = "e7 g7 g8 h8";
  } else if (epd == "5K2/8/8/8/8/4N3/3p4/2kN4 b - -" ||  // bm #-16
             epd == "5K2/8/8/8/8/4N3/1N1p4/2k5 w - -") { // bm #17
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d2";
  } else if (epd == "6r1/R4p2/4p1q1/1R1p4/1pB5/8/1p2N3/kb1K4 b - -" ||  // bm #-16
             epd == "6r1/1R3p2/4p1q1/1R1p4/1pB5/8/1p2N3/kb1K4 w - -") { // bm #17
    excludeFrom = "d1";
    excludeAllowingFrom = "b2 d5 e6 g6 f7 g8";
  } else if (epd == "8/6KB/1pR5/4P1P1/pp1P2P1/qp3P2/pk6/b7 b - -" ||   // bm #-16
             epd == "8/6K1/1pR5/4P1P1/pp1P2P1/qp1B1P2/pk6/b7 w - -") { // bm #17
    excludeFrom = "f3 d4 g4 e5 g5";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 a2 a3 b3 a4 b4";
  } else if (epd == "qn6/rp1p4/pPkP1R2/K7/2P5/2pP4/2P1P3/2B5 b - -" || // bm #-16
             epd == "qn6/rp1p4/pPkP1R2/K7/2P5/B1pP4/2P1P3/8 w - -") {  // bm #17
    excludeFrom = "c2 d3 c4 a5 b6 d6";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c3 a6 a7 b7 d7 a8 b8";
  } else if (epd == "8/p7/8/8/8/3p1b1N/pp1K4/qk6 b - -" ||  // bm #-17
             epd == "8/p7/8/8/8/3p1b2/pp1K1N2/qk6 w - -") { // bm #18
    excludeFrom = "d2";
    excludeToCapturable = true;
  } else if (epd == "R6k/4pBp1/1p2r1pq/3p1p2/R1p3n1/1r3bP1/p4P2/Q5K1 b - -" || // bm #-17
             epd == "7k/4pBp1/1p2r1pq/R2p1p2/R1p3n1/1r3bP1/p4P2/Q5K1 w - -") { // bm #18
    excludeFrom = "g1 f2 g3";
    excludeAllowingFrom = "a2 b3 c4 g4 d5 f5 b6 e6 g6 h6 e7 g7";
  } else if (epd == "8/2p1p3/B1P1Pp2/2Qp1P2/1p1P1N2/brn2P2/qrkP4/nb2K3 b - -" || // bm #-17
             epd == "8/2p1p3/B1P1Pp2/2Qp1P2/1p1P4/brnN1P2/qrkP4/nb2K3 w - -") {  // bm #18
    excludeFrom = "e1 d2 f3 d4 c5 f5 c6 e6";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 b1 a2 b2 a3 b3 c3 b4 d5 f6 c7 e7";
  } else if (epd == "8/8/8/2b3p1/5pPk/4pPp1/4PpP1/N4K2 b - -" ||  // bm #-17
             epd == "8/8/8/2b3p1/5pPk/4pPp1/2N1PpP1/5K2 w - -") { // bm #18
    excludeFrom = "f1 e2 g2 f3 g4";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "f2 e3 g3 f4 h4 g5";
  } else if (epd == "5bk1/p3p1p1/p3P1P1/p7/p7/p7/P5K1/8 b - -" || // bm #-17
             epd == "5bk1/p3p1p1/p3P1P1/p7/p7/p7/P7/7K w - -") {  // bm #18
    excludeFrom = "e6 g6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a3 a4 a5 a6 a7 e7 g7 f8";
  } else if (epd == "2Q5/pr6/k7/1p6/1P6/8/1K6/8 b - -" || // bm #-13
             epd == "2Q5/pr6/k7/1p6/1P6/8/8/K7 w - -" ||  // bm #14
             epd == "2Q5/pr6/k7/1p6/1P6/8/8/K7 b - -" ||  // bm #-17
             epd == "7Q/pr6/k7/1p6/1P6/8/8/K7 w - -") {   // bm #18
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b5 a7";
  } else if (epd == "8/3B4/5N2/8/7p/6pk/6p1/1K5b b - -" || // bm #-17
             epd == "8/8/2B2N2/8/7p/6pk/6p1/1K5b w - -") { // bm #18
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 g2 g3";
  } else if (epd == "8/8/8/p2P1P2/pp3R2/rk1PB3/qrp1P3/nbK5 b - -" || // bm #-17
             epd == "8/8/8/p2P1P2/pp3R2/rk1P4/qrpBP3/nbK5 w - -") {  // bm #18
    excludeFrom = "c1 e2 d3 d5 f5";
    excludeCaptures = true;
    excludeAllowingFrom = "a1 b1 a2 b2 c2 a3 a4 a5";
  } else if (epd == "7b/1p4p1/bp4P1/1p4P1/kp4PK/1p4P1/1P4P1/8 b - -" || // bm #-18
             epd == "7b/1p4p1/bp4P1/1p4PK/kp4P1/1p4P1/1P4P1/8 w - -") { // bm #19
    excludeFrom = "b2 g2 g3 g4 g5";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b3 b4 b5 a6 b6 b7 g7 h8";
  } else if (epd == "K7/7B/p7/p7/p2p4/P2p4/3P2np/R5bk b - -" ||  // bm #-18
             epd == "K5B1/8/p7/p7/p2p4/P2p4/3P2np/R5bk w - -") { // bm #19
    excludeFrom = "a1 d2 a3";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g1 h1 h2 d3 a4";
  } else if (epd == "b6b/rp4p1/1p4P1/1p4P1/kp4PK/1p4P1/1P4P1/8 b - -" || // bm #-18
             epd == "b6b/rp4p1/1p4P1/1p4PK/kp4P1/1p4P1/1P4P1/8 w - -") { // bm #19
    excludeFrom = "b2 g2 g3 g4 g5";
    excludePromotionTo = "rbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b3 b4 b5 b6 b7 g7 a8 h8";
  } else if (epd == "K4bkR/1r4p1/1qb2pn1/3p2NR/7r/7R/1p5n/QRRRRRRR b - -" || // bm #-19
             epd == "K4bk1/1r4p1/1qb2pnR/3p2NR/7r/7R/1p5n/QRRRRRRR w - -") { // bm #20
    excludeFrom = "g5 a8";
    excludeAllowingFrom = "b2 h2 d5 b6 c6 f6 b7 g7 f8";
  } else if (epd == "3nn3/2p2p2/6p1/n1PB3n/3BK3/1P3RP1/3pNPnk/3n1N1b b - -" || // bm #-19
             epd == "3nn3/2p2p2/6p1/n1PB3n/3BK3/1P2NRP1/3pNPnk/3n3b w - -") {  // bm #20
    excludeFrom = "f2 b3 f3 g3 d4 e4 c5 d5";
    excludeCaptures = true;
    excludeAllowingFrom = "d1 h1 d2 g2 a5 h5 g6 c7 f7 e8";
  } else if (epd == "3nn3/2p2p2/6p1/n1PB3n/3BK3/1P4P1/3pNPnk/3n1N1b b - -" || // bm #-19
             epd == "3nn3/2p2p2/6p1/n1PB3n/3BK3/1P2N1P1/3pNPnk/3n3b w - -") { // bm #20
    excludeFrom = "f2 b3 g3 d4 e4 c5 d5";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d1 h1 d2 g2 a5 h5 g6 c7 f7 d8 e8";
  } else if (epd == "7b/5p2/4pN1k/1p2P2P/1Pp2B2/K1p5/2P5/8 b - -" || // bm #-19
             epd == "7b/5p2/4pN1k/1p2P2P/1Pp5/K1p5/2P4B/8 w - -") {  // bm #20
    excludeFrom = "c2 b4 e5 f6";
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c3 c4 b5 e6 f7 h8";
  } else if (epd == "5B2/1p2K3/pp6/rk2N3/b1p5/1pP5/1P2P3/8 b - -" || // bm #-19
             epd == "3K1B2/1p6/pp6/rk2N3/b1p5/1pP5/1P2P3/8 w - -") { // bm #20
    excludePromotionTo = "qrbn";
    excludeCaptures = true;
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b3 a4 c4 a5 a6 b6 b7";
  } else if (epd == "5Q2/6p1/bp4p1/kp6/8/1P6/6p1/6Kb b - -" || // bm #-19
             epd == "Q7/6p1/bp4p1/kp6/8/1P6/6p1/6Kb w - -") {  // bm #20
    excludeFrom = "g1 b3";
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "6b1/5p2/1p3Bp1/1P3pP1/5P2/pp1B4/pr4PK/k7 b - -" ||   // bm #-18
             epd == "6b1/5p2/1p3Bp1/1P3pP1/5P2/pp1B2K1/pr4P1/k7 w - -" || // bm #19
             epd == "6b1/5p2/1p3Bp1/1P3pP1/5P2/pp1B1K2/pr4P1/k7 b - -" || // bm #-20
             epd == "6b1/5p2/1p3Bp1/1P3pP1/5P2/pp1BK3/pr4P1/k7 w - -") {  // bm #21
    excludeFrom = "g2 f4 b5 g5";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 a2 b2 a3 b3 f5 b6 g6 f7";
  } else if (epd == "7B/7p/6pP/5pP1/4pP2/p1KpP3/pn1N4/k7 b - -" || // bm #-20
             epd == "8/7p/6pP/5pP1/3BpP2/p1KpP3/pn1N4/k7 w - -") { // bm #21
    excludeFrom = "e3 f4 g5 h6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 a2 a3 d3 e4 f5 g6 h7";
  } else if (epd == "8/8/p6p/P3p2p/1B2P2p/1B3p1P/Pp3P2/bk2K3 b - -" || // bm #-20
             epd == "8/8/p6p/P3p2p/4P2p/1B3p1P/Pp1B1P2/bk2K3 w - -") { // bm #21
    excludeFrom = "a2 f2 h3 e4 a5";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 b2 f3 h4 e5 a6 h6";
  } else if (epd == "k7/p7/N2N4/4np2/p2p1p2/P2p1p2/3P1P2/5K1B b - -" || // bm #-20
             epd == "k7/p7/N2N4/4np2/p2p1p2/P2p1p2/3P1P2/6KB w - -") {  // bm #21
    excludeFrom = "d2 f2 a3 a6 d6";
    excludeAllowingCapture = true;
  } else if (epd == "1r5r/6Q1/8/8/8/p7/p7/k2K4 b - -" || // bm #-21
             epd == "1r5r/8/8/8/8/p7/p5Q1/k2K4 w - -") { // bm #22
    excludeFrom = "d1";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a2 a3 h8";
  } else if (epd == "5bk1/4p1p1/p3P1P1/p6p/p7/p7/P6K/8 b - -" || // bm #-21
             epd == "5bk1/4p1p1/p3P1P1/p6p/p7/p7/P7/7K w - -") { // bm #22
    excludeFrom = "e6 g6";
    excludePromotionTo = "qbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a3 a4 a5 a6 e7 g7 f8";
  } else if (epd == "R2n1R2/1pn2kBp/4p3/P2pKNr1/4N3/1pP5/6p1/3br3 b - -" || // bm #-22
             epd == "R2n3R/1pn2kBp/4p3/P2pKNr1/4N3/1pP5/6p1/3br3 w - -") {  // bm #23
    excludeFrom = "c3 e4 a5 e5 f5 g7 a8";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "d1 e1 g2 b3 d5 g5 e6 b7 c7 h7 d8";
  } else if (epd == "8/1p5B/7p/2RPP2p/pp1P3P/qp3P2/pk1K4/b7 b - -" ||  // bm #-23
             epd == "8/1p6/7p/2RPP2p/pp1P3P/qp1B1P2/pk1K4/b7 w - -") { // bm #24
    excludeFrom = "f3 d4 h4 d5 e5";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 a2 a3 b3 a4 b4 h5";
  } else if (epd == "8/3K3R/1p4p1/1Pp3Bb/2Pp1Pkr/3Pp1pq/4Pprp/5Bbn b - -" || // bm #-24
             epd == "8/7R/1p2K1p1/1Pp3Bb/2Pp1Pkr/3Pp1pq/4Pprp/5Bbn w - -") { // bm #25
    excludeFrom = "d3 c4 f4 b5 g5";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g1 h1 f2 g2 h2 e3 g3 h3 d4 h4 c5 h5 b6 g6";
  } else if (epd == "k7/8/8/1Q6/8/6p1/1p1pppp1/1Kbrqbrn b - -" || // bm #-25
             epd == "k7/8/1Q6/8/8/6p1/1p1pppp1/1Kbrqbrn w - -") { // bm #26
    excludeFrom = "b1";
    excludeToCapturable = true;
  } else if (epd == "8/p7/p7/p7/8/Bp6/bnpNK3/2k5 b - -" ||  // bm #-25
             epd == "8/p7/p7/p7/8/Bp6/bnpN4/2k1K3 w - -") { // bm #26
    excludeFrom = "a3";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "c1 b2 c2 b3";
  } else if (epd == "8/8/2p5/2p5/p1p5/rbp5/p1p1Q3/n1K4k b - -" || // bm #-25
             epd == "8/8/2p5/2p5/p1p5/rbp5/p1p2Q2/n1K4k w - -" || // bm #26
             epd == "8/2p5/2p5/8/p1p5/rbp3Q1/p1p5/n1K4k b - -" || // bm #-27
             epd == "8/2p5/2p5/8/p1p5/rbp5/p1p2Q2/n1K4k w - -") { // bm #28
    excludeFrom = "c1";
    excludeTo = "a3 c3";
    excludeToCapturable = true;
  } else if (epd == "8/p1N1b3/6p1/p7/Bp6/kP6/8/1K6 b - -" || // bm #-27
             epd == "4N3/p3b3/6p1/p7/Bp6/kP6/8/1K6 w - -") { // bm #28
    excludeFrom = "b3 a4";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a3 b4 a5";
  } else if (epd == "4k3/2Q5/8/5p2/5p2/1p3p2/1ppp1p2/nrqrbK2 b - -" || // bm #-16
             epd == "4k3/6Q1/8/5p2/5p2/1p3p2/1ppp1p2/nrqrbK2 w - -" || // bm #17
             epd == "4k3/8/5Q2/8/8/1p3p2/1ppp1p2/nrqrbK2 b - -" || // bm #-17
             epd == "4k3/6Q1/8/8/8/1p3p2/1ppp1p2/nrqrbK2 w - -" || // bm #18
             epd == "8/7p/4k3/2Q2p2/5p2/5p2/5p1p/5Kbr b - -" ||    // bm #-29
             epd == "8/7p/4k3/5p2/3Q1p2/5p2/5p1p/5Kbr w - -") {    // bm #30
    excludeFrom = "f1";
    excludeTo = "h1";
    excludeToCapturable = true;
  } else if (epd == "r2B1b1K/nR6/3ppppn/1k1B1q2/1p1Pp3/1P1b1p2/6p1/7r b - -" || // bm #-29
             epd == "r2B1b1K/n6R/3ppppn/1k1B1q2/1p1Pp3/1P1b1p2/6p1/7r w - -") { // bm #30
    excludeFrom = "b3 d4 d8 h8";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 g2 d3 f3 b4 e4 f5 d6 e6 f6 g6 h6 a7 a8";
  } else if (epd == "5b2/8/1p6/pPp1p1p1/p1PpPpP1/K2P1P2/3N4/k1N5 b - -" || // bm #-29
             epd == "5b2/8/1p6/pPp1p1p1/p1PpPpP1/K2P1P2/N2N4/k7 w - -") {  // bm #30
    excludeFrom = "d2 a3 d3 f3 c4 e4 g4 b5";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 a4 d4 f4 c5 e5 g5 b6";
  } else if (epd == "8/8/8/8/8/4p2k/4P3/6RK b - -" ||               // bm #-7
             epd == "8/8/3K4/1p6/pP1k1p2/P3pP1B/1P2P3/8 b - -" ||   // bm #-7
             epd == "1B6/1R1p4/3p4/3p4/3p4/k2p4/3P4/1K6 b - -" ||   // bm #-7
             epd == "6R1/8/8/8/8/4p2k/4P3/7K w - -" ||              // bm #8
             epd == "8/8/3K4/1p6/pP1k1p2/P3pP2/1P2P3/5B2 w - -" ||  // bm #8
             epd == "1B6/3p4/3p4/3p4/3p4/k2p4/1R1P4/1K6 w - -" ||   // bm #8
             epd == "KBk3B1/3p4/3P4/8/8/8/8/8 b - -" ||             // bm #-8
             epd == "8/8/7p/1p2p2P/1P2P3/P1k2P2/2P5/2K3N1 b - -" || // bm #-8
             epd == "8/p7/p7/P7/6BK/8/2N3k1/8 b - -" ||             // bm #-8
             epd == "K1k3B1/2Bp4/3P4/8/8/8/8/8 w - -" ||            // bm #9
             epd == "8/8/7p/1p2p2P/1P2P3/P1k2P2/2P5/3K2N1 w - -" || // bm #9
             epd == "8/p7/p7/P7/6BK/N7/6k1/8 w - -" ||              // bm #9
             epd == "8/7p/7p/7P/5K2/5B1N/8/5k2 b - -" ||            // bm #-9
             epd == "8/7p/7p/7P/8/5BKN/8/5k2 w - -" ||              // bm #10
             epd == "8/1p6/k7/1p6/1P6/1B6/8/1K4B1 b - -" ||         // bm #-11
             epd == "3k4/B3N3/3K4/8/2p5/2p5/2P5/8 b - -" ||         // bm #-11
             epd == "8/1p6/k7/1p6/BP6/8/8/1K4B1 w - -" ||           // bm #12
             epd == "3k4/B7/3K4/3N4/2p5/2p5/2P5/8 w - -" ||         // bm #12
             epd == "8/6B1/8/8/1B2p3/4p3/B3p1pp/2k1Kbrr b - -" ||   // bm #-30
             epd == "8/6B1/4B3/8/1B2p3/4p3/4p1pp/2k1Kbrr w - -") {  // bm #31
    excludeCaptures = true;
    excludeAllowingCapture = true;
  } else if (epd == "5n2/B3K3/2p3p1/4k3/7P/3bNNP1/2Prn1P1/1q6 b - -" ||   // bm #-21
             epd == "5n2/B3K3/2p3p1/4k1N1/7P/3bN1P1/2Prn1P1/1q6 w - -" || // bm #22
             epd == "5n2/B3K3/2p2Np1/4k3/6NP/3b2P1/2Prn1P1/1q6 b - -" ||  // bm #-29
             epd == "5n2/B3K3/2p2Np1/4k3/7P/3bN1P1/2Prn1P1/1q6 w - -" ||  // bm #30
             epd == "5n2/B3K3/2p3p1/4k3/6NP/3bN1P1/2Prn1P1/1q6 b - -" ||  // bm #-31
             epd == "5n2/B3K3/2p3p1/4k3/7P/3bN1P1/2PrnNP1/1q6 w - -") {   // bm #32
    excludeFrom = "g2 g3 h4 a7 e7";
    excludePromotionTo = "qrbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b1 d2 e2 d3 c6 g6 f8";
  } else if (epd == "8/p1Q5/8/2p5/1p6/8/ppPp2p1/qrbK2kb b - -" || // bm #-32
             epd == "8/p7/8/Q1p5/1p6/8/ppPp2p1/qrbK2kb w - -") {  // bm #33
    excludePromotionTo = "rbn";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 b1 c1 h1 a2 b2 d2 g2";
  } else if (epd == "8/8/6Q1/8/6k1/8/3p1pp1/3Kbrrb b - -" ||       // bm #-8
             epd == "8/8/8/8/6k1/8/2Qp1pp1/3Kbrrb w - -" ||        // bm #9
             epd == "8/2Q5/8/2kp4/8/1p1p4/pp1p4/rrbK4 b - -" ||    // bm #-11
             epd == "8/3Q4/8/2kp4/8/1p1p4/pp1p4/rrbK4 w - -" ||    // bm #12
             epd == "8/8/1Q6/6k1/8/8/3p1pp1/3Kbrrb b - -" ||       // bm #-11
             epd == "8/8/8/6k1/3Q4/8/3p1pp1/3Kbrrb w - -" ||       // bm #12
             epd == "k7/8/8/1Q6/3p4/1p1p4/pp1p4/rrbK4 b - -" ||    // bm #-13
             epd == "k7/8/8/2Q5/3p4/1p1p4/pp1p4/rrbK4 w - -" ||    // bm #14
             epd == "7k/8/8/8/8/2Q3p1/3p1pp1/3Kbrrn b - -" ||      // bm #-15
             epd == "7k/8/8/8/8/5Qp1/3p1pp1/3Kbrrn w - -" ||       // bm #16
             epd == "6k1/4Q3/8/8/8/8/3p1pp1/3Kbrrb b - -" ||       // bm #-16
             epd == "6k1/8/5Q2/8/8/8/3p1pp1/3Kbrrb w - -" ||       // bm #17
             epd == "8/4Q1k1/8/8/8/8/3p1pp1/3Kbrrb b - -" ||       // bm #-17
             epd == "4Q3/6k1/8/8/8/8/3p1pp1/3Kbrrb w - -" ||       // bm #18
             epd == "5k2/3Q4/8/8/8/8/3p1pp1/3Kbrrb b - -" ||       // bm #-17
             epd == "5k2/8/4Q3/8/8/8/3p1pp1/3Kbrrb w - -" ||       // bm #18
             epd == "5k2/8/4Q3/8/8/8/3p1pp1/3Kbrrb b - -" ||       // bm #-17
             epd == "5k2/3Q4/8/8/8/8/3p1pp1/3Kbrrb w - -" ||       // bm #18
             epd == "6k1/8/8/8/8/3Q4/3p1pp1/3Kbrrb w - -" ||       // bm #18
             epd == "8/8/8/1p6/1k1Q4/8/pp1p4/rrbK4 b - -" ||       // bm #-17
             epd == "8/8/8/1p6/1k6/3Q4/pp1p4/rrbK4 w - -" ||       // bm #18
             epd == "4k3/2Q5/8/8/8/8/3p1pp1/3Kbrrb b - -" ||       // bm #-18
             epd == "4k3/8/3Q4/8/8/8/3p1pp1/3Kbrrb w - -" ||       // bm #19
             epd == "4k3/8/3Q4/8/8/8/3p1pp1/3Kbrrb b - -" ||       // bm #-19
             epd == "4k3/2Q5/8/8/8/8/3p1pp1/3Kbrrb w - -" ||       // bm #20
             epd == "8/8/8/2Q5/8/3k4/3p1pp1/3Kbrrb b - -" ||       // bm #-19
             epd == "8/8/8/8/1Q6/3k4/3p1pp1/3Kbrrb w - -" ||       // bm #20
             epd == "8/8/6k1/1Q6/8/8/3p1pp1/3Kbrrb b - -" ||       // bm #-19
             epd == "8/8/6k1/Q7/8/8/3p1pp1/3Kbrrb w - -" ||        // bm #20
             epd == "5Q2/8/2k5/8/3p4/1p1p4/pp1p4/rrbK4 b - -" ||   // bm #-19
             epd == "8/8/2k5/8/3p4/Qp1p4/pp1p4/rrbK4 w - -" ||     // bm #20
             epd == "8/3k1Q2/3p4/8/8/1p1p4/pp1p4/rrbK4 b - -" ||   // bm #-22
             epd == "8/3k4/3p1Q2/8/8/1p1p4/pp1p4/rrbK4 w - -" ||   // bm #23
             epd == "8/1p6/8/Q7/2kp4/3p4/pp1p4/rrbK4 b - -" ||     // bm #-25
             epd == "8/1p6/1Q6/8/2kp4/3p4/pp1p4/rrbK4 w - -" ||    // bm #26
             epd == "8/5Qp1/8/6k1/8/8/3p1pp1/3Kbrrb b - -" ||      // bm #-28
             epd == "8/6p1/4Q3/6k1/8/8/3p1pp1/3Kbrrb w - -" ||     // bm #29
             epd == "2k5/3p4/8/2Q5/8/1p1p4/pp1p4/rrbK4 b - -" ||   // bm #-29
             epd == "2k5/3p4/1Q6/8/8/1p1p4/pp1p4/rrbK4 w - -" ||   // bm #30
             epd == "4k3/3p2Q1/8/8/8/1p1p4/pp1p4/rrbK4 b - -" ||   // bm #-29
             epd == "4k3/3p4/5Q2/8/8/1p1p4/pp1p4/rrbK4 w - -" ||   // bm #30
             epd == "Q7/8/8/8/k7/8/3p1pp1/3Kbrrb b - -" ||         // bm #-31
             epd == "3Q4/8/8/8/k7/8/3p1pp1/3Kbrrb w - -" ||        // bm #32
             epd == "8/7Q/8/8/1k1p4/4p1p1/3prpp1/3Kbbrn b - -" ||  // bm #-33
             epd == "8/2Q5/8/8/1k1p4/4p1p1/3prpp1/3Kbbrn w - -") { // bm #34
    excludeFrom = "d1";
    excludeAllowingCapture = true;
  } else if (epd == "8/6p1/6p1/6p1/1p2N1p1/bP4p1/5pPk/1K3B1n b - -" || // bm #-33
             epd == "8/6p1/6p1/2N3p1/1p4p1/bP4p1/5pPk/1K3B1n w - -") { // bm #34
    excludeFrom = "f1 g2 b3";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h1 f2 g3 b4 g4";
  } else if (epd == "8/8/8/1p6/6k1/1Q6/p1p1p3/rbrbK3 b - -" ||   // bm #-35
             epd == "8/8/8/1p6/6k1/1p2Q3/p1p1p3/rbrbK3 w - -") { // bm #36
    excludeFrom = "e1";
    excludeTo = "a1 c1";
    excludeToAttacked = true;
  } else if (epd == "8/8/7P/5P2/6k1/4p3/ppp1p3/brrbK3 b - -" || // bm #-38
             epd == "8/8/8/5P1P/6k1/4p3/ppp1p3/brrbK3 w - -") { // bm #39
    excludeFrom = "e1";
    excludePromotionTo = "rbn";
    excludeToCapturable = true;
  } else if (epd == "7k/8/5p2/8/8/8/P2p1pp1/3Kbrrb b - -" || // bm #-42
             epd == "7k/8/5p2/8/8/8/P1Kp1pp1/4brrb w - -") { // bm #43
    if (epd.find(" w ") != std::string::npos)
      openingMoves = "c2d1";
    excludeFrom = "d1";
    excludeToAttacked = true;
  } else if (epd == "k7/1p6/8/7P/1p6/8/ppp1p3/rqrbK3 b - -" || // bm #-42
             epd == "k7/1p6/8/7P/1p6/8/pppKp3/rqrb4 w - -") {  // bm #43
    if (epd.find(" w ") != std::string::npos)
      openingMoves = "d2e1";
    excludeFrom = "e1";
    excludePromotionTo = "rbn";
    excludeToCapturable = true;
  } else if (epd == "8/8/8/2K5/p7/1p5p/rp3ppr/bQ1N1bqk b - -" ||    // bm #-10
             epd == "8/8/8/8/pK6/1p5p/rp3ppr/bQ1N1bqk w - -" ||     // bm #11
             epd == "8/5p2/7p/2K5/p7/1p5p/rp3ppr/bQ1N1bqk b - -" || // bm #-42
             epd == "8/5p2/7p/8/pK6/1p5p/rp3ppr/bQ1N1bqk w - -") {  // bm #43
    excludeFrom = "b1";
    excludeTo = "a1 a2 a4 b2 b3 f1 g2 h3 h2";
    excludeAllowingFrom = "a1 b2 f2 g1 g2 h1 h2 h3";
    excludeAllowingCapture = true;
  } else if (epd == "8/1p6/8/3p3k/3p1Q2/8/pp1p4/rrbK4 b - -" || // bm #-45
             epd == "8/1p6/8/3p3k/3p4/6Q1/pp1p4/rrbK4 w - -") { // bm #46
    excludeFrom = "d1";
    excludeCaptures = true;
    excludeToAttacked = true;
  } else if (epd == "4Kb1r/p3p1p1/4P1Pp/7P/8/p2N2p1/P4brp/2NRnkqn b - -" || // bm #-45
             epd == "5b1r/p3pKp1/4P1Pp/7P/8/p2N2p1/P4brp/2NRnkqn w - -") {  // bm #46
    excludeFrom = "c1 a2 d3 h5 e6 g6";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e1 f1 g1 h1 g2 h2 a3 g3 h6 e7 g7 f8";
  } else if (epd == "8/5Q2/7k/8/8/6p1/4p1pb/4Kbrr b - -" ||    // bm #-11
             epd == "6Q1/8/7k/8/8/6p1/4p1pb/4Kbrr w - -" ||    // bm #12
             epd == "8/k1Q5/8/8/8/8/1pp1p3/brrbK3 b - -" ||    // bm #-15
             epd == "2Q5/k7/8/8/8/8/1pp1p3/brrbK3 w - -" ||    // bm #16
             epd == "8/8/3p4/3Q4/8/2k5/ppp1p3/brrbK3 b - -" || // bm #-21
             epd == "8/8/3p4/1Q6/8/2k5/ppp1p3/brrbK3 w - -" || // bm #22
             epd == "8/1p2k3/8/8/8/8/pppQp3/qrrbK3 b - -" ||   // bm #-49
             epd == "8/1p2k3/8/8/5Q2/8/ppp1p3/qrrbK3 w - -" || // bm #50
             epd == "8/1p2k3/8/8/8/8/pppQp3/bqrbK3 b - -" ||   // bm #-49
             epd == "8/1p2k3/8/8/5Q2/8/ppp1p3/bqrbK3 w - -") { // bm #50
    excludeFrom = "e1";
    excludeAllowingCapture = true;
  } else if (epd == "8/5p1p/7K/8/p7/rp5p/1p3ppr/bQ1N1bqk b - -" ||  // bm #-50
             epd == "8/5p1p/8/6K1/p7/rp5p/1p3ppr/bQ1N1bqk w - -" || // bm #51
             epd == "8/5p1p/8/8/p6K/rp5p/1p3ppr/bQ1N1bqk b - -" ||  // bm #-52
             epd == "8/5p1p/8/8/p3Q2K/rp5p/1p3ppr/b2N1bqk w - -") { // bm #53
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 g1 h1 b2 f2 g2 h2 b3 h3";
  } else if (epd == "8/7p/7p/7p/1p5p/1Kp5/nppr4/qrk2Q2 b - -" || // bm #-53
             epd == "8/7p/7p/7p/1p3Q1p/1Kp5/nppr4/qrk5 w - -") { // bm #54
    excludeFrom = "b3";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b1 h1";
    excludeAllowingMoves = "c3c2";
  } else if (epd == "8/1pQ5/4k3/8/3p4/3p4/pp1p4/rrbK4 b - -" ||     // bm #-55
             epd == "8/1p6/4k3/8/3p1Q2/3p4/pp1p4/rrbK4 w - -" ||    // bm #56
             epd == "8/6pp/5p2/k7/2Qp4/4p3/3prpp1/3Kbqrb b - -" ||  // bm #-56
             epd == "8/6pp/5p2/k7/3p4/1Q2p3/3prpp1/3Kbqrb w - -") { // bm #57
    excludeFrom = "d1";
    excludeToAttacked = true;
  } else if (epd == "8/p1p1Q3/p1p5/6rp/7k/6p1/p1p3P1/rbK5 b - -" || // bm #-59 (finds #-61)
             epd == "5Q2/p1p5/p1p5/6rp/7k/6p1/p1p3P1/rbK5 w - -") { // bm #60 (finds #62)
    excludeFrom = "c1 g2";
    excludeTo = "a1 g3";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h5";
  } else if (epd == "7K/7p/3p1pp1/6pr/4n1pb/3N1bk1/4prn1/4B1RR b - -" || // bm #-60
             epd == "7K/7p/3p1pp1/B5pr/4n1pb/3N1bk1/4prn1/6RR w - -") {  // bm #61
    if (epd.find(" w ") != std::string::npos)
      openingMoves = "a5e1";
    excludeFrom = "g1 h1";
    excludeTo = "h2 g3 g5 f6 f5 f4 d6 d5 d4";
    excludeAllowingFrom = "e2 f2 f3 g2 g3 g4 h4";
    excludeAllowingTo = "d3";
    excludeToCapturable = true;
  } else if (epd == "4R3/1n1p4/3n4/8/8/p4p2/7p/5K1k w - -" ||     // bm #20
             epd == "4R3/1n1p1p2/3n4/8/8/p4p2/7p/5K1k w - -" ||   // bm #32
             epd == "8/pn1p1p1p/p2n4/8/8/p4p2/7p/4RK1k b - -" ||  // bm #-68
             epd == "4R3/pn1p1p1p/p2n4/8/8/p4p2/7p/5K1k w - -") { // bm #69
    if (epd.find(" w ") != std::string::npos)
      openingMoves = "e8e1 d6e4 e1e4 f3f2 f1f2 * e4e1, e8e1 d6e4 e1e4 * e4e1, "
                     "e8e1 * f1f2";
    else
      openingMoves = "d6e4 e1e4 f3f2 f1f2 * e4e1, d6e4 e1e4 * e4e1, * f1f2";
    excludeSANs = "Ra2 Ra3 Ra4 Ra5 Ra6 Ra7 Ra8 "
                  "Rb2 Rb3 Rb4 Rb5 Rb6 Rb7 Rb8 "
                  "Rc2 Rc3 Rc4 Rc5 Rc6 Rc7 Rc8 "
                  "Rd2 Rd3 Rd4 Rd5 Rd6 Rd7 Rd8 "
                  "Re2 Re3 Re4 Re5 Re6 Re7 Re8 "
                  "Rf2 Rf3 Rf4 Rf5 Rf6 Rf7 Rf8 "
                  "Rg2 Rg3 Rg4 Rg5 Rg6 Rg7 Rg8 "
                  "Rh2 Rh3 Rh4 Rh5 Rh6 Rh7 Rh8 ";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 d1 f1 h1";
  } else if (epd == "7K/5p1p/3p2p1/6pr/4n1pb/3N1bk1/4prn1/4B1RR b - -" || // bm #-69
             epd == "7K/5p1p/3p2p1/B5pr/4n1pb/3N1bk1/4prn1/6RR w - -") {  // bm #70
    excludeFrom = "g1 h1 d3";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "e2 f2 g2 f3 g3 g4 h4 g5 g6";
  } else if (epd == "6Q1/1p5p/1p6/1p6/1p5p/5r1k/5p1p/5Kbr b - -" || // bm #-71
             epd == "8/1p4Pp/1p6/1p6/1p5p/5r1k/5p1p/5Kbr w - -") {  // bm #72
    if (epd.find(" w ") != std::string::npos)
      openingMoves = "g7g8q";
    excludeFrom = "f1";
    excludeTo = "h1";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b3 h5 h4";
  } else if (epd == "8/K5p1/1p6/1p6/1p5p/p2NN1p1/rb4pr/qkn3Rb b - -" || // bm #-71
             epd == "K7/6p1/1p6/1p6/1p5p/p2NN1p1/rb4pr/qkn3Rb w - -") { // bm #72
    excludeFrom = "d3 e3";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 b1 c1 h1 a2 g2 a3 g3";
  } else if (epd == "8/6Pp/8/8/7p/5r2/4Kpkp/6br w - -" ||          // bm #19
             epd == "6Q1/1p5p/1p6/1p6/1p5p/5r2/4Kpkp/6br b - -" || // bm #-76
             epd == "8/1p4Pp/1p6/1p6/1p5p/5r2/4Kpkp/6br w - -") {  // bm #77
    if (epd.find(" w ") != std::string::npos)
      openingMoves =
        "g7g8q g2h3 e2f1, g7g8q f3g3 g8d5 g3f3 d5f3, g7g8q f3g3 g8d5 g2h3 "
        "d5e6 g3g4 e2f1, g7g8q f3g3 g8d5 g2h3 d5e6 h3g2 e6e4 g3f3 e4f3, "
        "g7g8q f3g3 g8d5 g2h3 d5e6 h3g2 e6e4 g2h3 e2f1";
    else
      openingMoves =
        "g2h3 e2f1, f3g3 g8d5 g3f3 d5f3, f3g3 g8d5 g2h3 d5e6 g3g4 e2f1, "
        "f3g3 g8d5 g2h3 d5e6 h3g2 e6e4 g3f3 e4f3, "
        "f3g3 g8d5 g2h3 d5e6 h3g2 e6e4 g2h3 e2f1";
    excludeFrom = "f1";
    excludeTo = "h1";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b3 h5 h4";
  } else if (epd == "8/8/8/8/NK6/1B1N4/2rpn1pp/2bk1brq w - -" ||      // bm #7
             epd == "8/7p/8/8/NK6/1B1N4/2rpn1pp/2bk1brq w - -" ||     // bm #27
             epd == "8/5ppp/5p2/K7/N7/1B1N4/2rpn1pp/2bk1brq b - -" || // bm #-86
             epd == "8/5ppp/5p2/8/NK6/1B1N4/2rpn1pp/2bk1brq w - -") { // bm #87
    excludeSANs = "Nb6 Nb5 Nc4";
    excludeFrom = "a4 b3 d3";
    excludeAllowingCapture = true;
    std::cout << "\n!! WARNING: An engine may be needed (not implemented yet).\n\n";
  } else if (epd == "8/5P2/8/8/8/n7/1pppp2K/br1r1kn1 w - -" ||     // bm #10
             epd == "8/3p1P2/8/8/8/n7/1pppp2K/br1r1kn1 w - -" ||   // bm #28
             epd == "8/2pp1P2/8/8/8/n7/1pppp2K/br1r1kn1 w - -" ||  // bm #48
             epd == "5Q2/pppp4/8/8/8/n7/1pppp2K/br1r1kn1 b - -" || // bm #-92
             epd == "8/pppp1P2/8/8/8/n7/1pppp2K/br1r1kn1 w - -") { // bm #93
    if (epd.find(" w ") != std::string::npos)
      openingMoves =
        "f7f8q g1f3 f8f3 f1e1 f3g3 e1f1 g3g1, "
        "f7f8q f1e1 f8a3 g1f3 a3f3 * f3g3 e1f1 g3g1, "
        "f7f8q f1e1 f8a3 g1h3 a3h3 e1f2 h3g3 f2f1 g3g1, "
        "f7f8q f1e1 f8a3 g1h3 a3h3 * h3g3 e1f1 g3g1, "
        "f7f8q f1e1 f8a3 e1f1 a3f8 g1f3 f8f3 f1e1 f3g3 e1f1 g3g1, "
        "f7f8q f1e1 f8a3 e1f1 a3f8 f1e1 f8c5 g1f3 h2g3 d1c1 c5f2 e1d1 f2f3 "
        "d1e1 f3h1, "
        "f7f8q f1e1 f8a3 e1f1 a3f8 f1e1 f8c5 g1f3 h2g3 f3d4 c5d4 e1f1 d4f2, "
        "f7f8q f1e1 f8a3 e1f1 a3f8 f1e1 f8c5 g1f3 h2g3 f3d4 c5d4 * d4g1, "
        "f7f8q f1e1 f8a3 e1f1 a3f8 f1e1 f8c5 g1f3 h2g3 * c5f2, "
        "f7f8q f1e1 f8a3 e1f1 a3f8 f1e1 f8c5 g1h3 h2h3 e1f1 c5f5 f1g1 f5g4 "
        "g1f2 g4g3 f2f1 g3g2 f1e1 g2g1, "
        "f7f8q f1e1 f8a3 e1f1 a3f8 f1e1 f8c5 g1h3 h2h3 e1f1 c5f5 f1e1 f5g6 "
        "e1f2 g6g3 f2f1 g3g2 f1e1 g2g1, "
        "f7f8q f1e1 f8a3 e1f1 a3f8 f1e1 f8c5 g1h3 h2h3 e1f1 c5f5 f1e1 f5g6 "
        "e1f1 g6g2 f1e1 g2g1, "
        "f7f8q f1e1 f8a3 e1f1 a3f8 f1e1 f8c5 g1h3 h2h3 e1f1 c5f5 f1e1 f5g6 * "
        "g6g1, "
        "f7f8q f1e1 f8a3 e1f1 a3f8 f1e1 f8c5 g1h3 h2h3 * c5g1, "
        "f7f8q f1e1 f8a3 e1f1 a3f8 f1e1 f8c5 * c5g1, "
        "f7f8q f1e1 f8a3 e1f2 a3g3, "
        "f7f8q f1e1 f8a3 d1c1 a3g3, "
        "f7f8q f1e1 f8a3 b1c1 a3g3, "
        "f7f8q f1e1 f8a3 * a3g3 e1f1 g3g1";
    else
      openingMoves =
        "g1f3 f8f3 f1e1 f3g3 e1f1 g3g1, "
        "f1e1 f8a3 g1f3 a3f3 * f3g3 e1f1 g3g1, "
        "f1e1 f8a3 g1h3 a3h3 e1f2 h3g3 f2f1 g3g1, "
        "f1e1 f8a3 g1h3 a3h3 * h3g3 e1f1 g3g1, "
        "f1e1 f8a3 e1f1 a3f8 g1f3 f8f3 f1e1 f3g3 e1f1 g3g1, "
        "f1e1 f8a3 e1f1 a3f8 f1e1 f8c5 g1f3 h2g3 d1c1 c5f2 e1d1 f2f3 d1e1 f3h1, "
        "f1e1 f8a3 e1f1 a3f8 f1e1 f8c5 g1f3 h2g3 f3d4 c5d4 e1f1 d4f2, "
        "f1e1 f8a3 e1f1 a3f8 f1e1 f8c5 g1f3 h2g3 f3d4 c5d4 * d4g1, "
        "f1e1 f8a3 e1f1 a3f8 f1e1 f8c5 g1f3 h2g3 * c5f2, "
        "f1e1 f8a3 e1f1 a3f8 f1e1 f8c5 g1h3 h2h3 e1f1 c5f5 f1g1 f5g4 "
        "g1f2 g4g3 f2f1 g3g2 f1e1 g2g1, "
        "f1e1 f8a3 e1f1 a3f8 f1e1 f8c5 g1h3 h2h3 e1f1 c5f5 f1e1 f5g6 "
        "e1f2 g6g3 f2f1 g3g2 f1e1 g2g1, "
        "f1e1 f8a3 e1f1 a3f8 f1e1 f8c5 g1h3 h2h3 e1f1 c5f5 f1e1 f5g6 "
        "e1f1 g6g2 f1e1 g2g1, "
        "f1e1 f8a3 e1f1 a3f8 f1e1 f8c5 g1h3 h2h3 e1f1 c5f5 f1e1 f5g6 * g6g1, "
        "f1e1 f8a3 e1f1 a3f8 f1e1 f8c5 g1h3 h2h3 * c5g1, "
        "f1e1 f8a3 e1f1 a3f8 f1e1 f8c5 * c5g1, "
        "f1e1 f8a3 e1f2 a3g3, "
        "f1e1 f8a3 d1c1 a3g3, "
        "f1e1 f8a3 b1c1 a3g3, "
        "f1e1 f8a3 * a3g3 e1f1 g3g1";
    excludeSANs = "Kh1 Kg1 Kg2 Kg3 Kg4 Kh4";
    excludeTo = "b2 c2 d2 e2";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b2 c2 d2 e2";
    excludeAllowingSANs = "Ke3 Kf3 Kh1 Kg2 Kh2";
  } else if (epd == "7K/8/8/8/4n3/pp1N3p/rp2N1br/bR3n1k w - -" ||          // bm #3
             epd == "7K/8/8/7p/p3n3/1p1N3p/rp2N1br/bR3n1k w - -" ||        // bm #31
             epd == "8/3p2K1/4p3/1p5p/p3n3/1p1N3p/rp2N1br/bR3n1k b - -" || // bm #-95
             epd == "7K/3p4/4p3/1p5p/p3n3/1p1N3p/rp2N1br/bR3n1k w - -") {  // bm #96
    excludeFrom = "d3 e2";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b2 h2 h1";
    excludeAllowingSANs = "Be4 Bd5 Bc6 Bb7 Ba8 Bg4 Bh5";
    std::cout << "\n!! WARNING: An engine may be needed (not implemented yet).\n\n";
  } else if (epd == "8/8/6p1/6Pb/p3P1k1/P1p1PNnr/2P1PKRp/7B w - -" ||          // bm #12
             epd == "8/4p3/6p1/6Pb/p3P1k1/P1p1PNnr/2P1PKRp/7B w - -" ||        // bm #34
             epd == "8/p1p1p3/2p3p1/6Pb/p3P1k1/P1p1PNnr/2P1P1Rp/4K2B b - -" || // bm #-99
             epd == "8/p1p1p3/2p3p1/6Pb/p3P1k1/P1p1PNnr/2P1PKRp/7B w - -") {   // bm #100
    excludeSANs = "Rf2";
    excludeFrom = "f3 e4";
    excludeAllowingCapture = true;
  } else if (epd == "n1K5/bNp5/1pP5/1k4p1/1N2pnp1/PP2p1p1/4rpP1/5B2 w - -" ||   // bm #16
             epd == "n1K5/bNp1p3/1pP5/1k4p1/1N3np1/PP2p1p1/4rpP1/5B2 w - -" ||  // bm #35
             epd == "n1K5/bNp1p1p1/1pP5/1k6/1N3np1/PP2p1p1/4rpP1/5B2 w - -" ||  // bm #57
             epd == "n7/bNpKp1p1/1pP3p1/1k2p3/1N3n2/PP4p1/4rpP1/5B2 b - -" ||   // bm #-100
             epd == "n1K5/bNp1p1p1/1pP3p1/1k2p3/1N3n2/PP4p1/4rpP1/5B2 w - -") { // bm #101
    excludeFrom = "a3 b3 b4 b7 c6 g2";
    excludeAllowingFrom = "a8 b5 b6 c7 e2 f1 g3 g2 d3";
    excludeTo = "a8";
    excludeToCapturable = true;
    excludeMoves = "f1c4 e2c4 e2d1 e2f3 e2g4 e2h5 f1g2 f1h3 d3c2 d3b1 d3e4 "
                   "d3f5 d3g6 d3h7";
    std::cout << "\n!! WARNING: An engine may be needed (not implemented yet).\n\n";
  } else if (epd == "8/8/8/3p2p1/p2np1K1/p3N1pp/rb1N2pr/k1n3Rb w - -" ||  // bm #4
             epd == "8/8/8/3p2p1/p2np1Kp/p3N1p1/rb1N2pr/k1n3Rb w - -" ||  // bm #35
             epd == "8/4p3/3p4/p5pK/3n3p/p3N1p1/rb1N2pr/k1n3Rb b - -" ||  // bm #-101
             epd == "8/4p3/3p4/p5p1/3n2Kp/p3N1p1/rb1N2pr/k1n3Rb w - -") { // bm #102
    excludeFrom = "d2 e3 g1";
    excludeTo = "g3";
    excludeAllowingFrom = "a1 a2 d5";
    excludeAllowingCapture = true;
    std::cout << "\n!! WARNING: An engine may be needed (not implemented yet).\n\n";
  } else if (epd == "2RN1qN1/5P2/3p1P2/3P4/1K6/1p1p1pp1/1p1p1np1/bk1b2Q1 w - -" ||  // bm #5
             epd == "2RN1qN1/5P2/3p1P2/3P4/8/Kp1p1pp1/1p1p1np1/bk1b2Q1 w - -" ||    // bm #21
             epd == "3N1qN1/1Kn2P2/3p1Pp1/3P1pp1/R7/1p1p4/1p1p1n2/bk1b2Q1 w - -" || // bm #107
             epd == "3N1qN1/1Kn2P2/1Q1p1Pp1/3P1pp1/R7/1p1p4/kp1p4/b2b3n b - -" ||   // bm #-108
             epd == "3N1qN1/1Kn2P2/1Q1p1Pp1/3P1pp1/1R6/1p1p4/kp1p4/b2b3n w - -") {  // bm #109 (not yet)
    if (epd == "3N1qN1/1Kn2P2/1Q1p1Pp1/3P1pp1/1R6/1p1p4/kp1p4/b2b3n w - -")
      openingMoves = "b4a4 * b6g1";
    else if (epd == "3N1qN1/1Kn2P2/1Q1p1Pp1/3P1pp1/R7/1p1p4/kp1p4/b2b3n b - -")
      openingMoves = "* b6g1";
    excludeFrom = "d5 e7 g7 e8";
    excludeTo = "d6 a1 b2 b3 d1 d2 d3";
    excludeSANs = "Qxf2 Qxf3 Qxf4 Qxf5 Qxf6 Qxf7 Qxg8 Qxg2 Qxg3 Qxg4 Qxg5 "
                  "Qxg6 Qxg7 Qxg8 Qxh1 Qxh1+ Rb1 Rb2 Rb3 Rb4 Rb5 Rb6 Rb7 Rb8 "
                  "Rd1 Rd2 Rd3 Rd4 Rd5 Rd6 Rd7 Rd8 Re1 Re2 Re3 Re4 Re5 Re6 "
                  "Re7 Re8 Rf1 Rf2 Rf3 Rf4 Rf5 Rf6 Rf7 Rf8 Rg1 Rg2 Rg3 Rg4 "
                  "Rg5 Rg6 Rg7 Rg8 Rh1 Rh2 Rh3 Rh4 Rh5 Rh6 Rh7 Rh8";
    excludeMoves = "d8e6 d8c6 d8b7 f7h8 f7h6 f7g5 f7e5 f7d6 g8f6 g8e7 h6g4 "
                   "h6f5 h6f7 f7f8n";
    excludeToCapturable = true;
    excludePromotionTo = "qrb";
    excludeAllowingFrom =
        "c7 a1 b2 b3 d1 d2 d3 g7 h6 f7 g8 e8 d8 e7 h8 c8 b8 a8";
    excludeAllowingTo = "f1 g1 f6 d5";
    excludeAllowingMoves = "a2a3 c2c3";
    excludeAllowingSANs = "Nxf7 Nxf6 Nxf7+ Nxf6+";
    std::cout << "\n!! WARNING: An engine may be needed (not implemented yet).\n\n";
  } else if (epd == "8/p7/8/p7/b3Q3/K7/p1r5/rk6 w - -" ||      // bm #10
             epd == "8/p7/8/p7/b3Q3/K6p/p1r5/rk6 w - -" ||     // bm #22
             epd == "8/p6p/7p/p6p/b6p/K6p/p1r5/rk2Q3 b - -" || // bm #-119
             epd == "8/p6p/7p/p6p/b3Q2p/K6p/p1r5/rk6 w - -") { // bm #120
    excludeFrom = "a3";
    excludeTo = "a1";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 h1";
    excludeAllowingSANs = "Kb1 Kc2 Kd1 Kd2";
  } else if (epd == "r1b5/1pKp4/pP1P4/P6B/3pn3/1P1k4/1P6/5N1N w - -" ||        // bm #4
             epd == "r1b5/1pKp4/pP1P4/P6B/3pn2p/1P1k4/1P6/5N1N w - -" ||       // bm #26
             epd == "r1b5/1pKp4/pP1P1p1p/P4p2/3pn2p/1P1k4/1P6/3B1N1N b - -" || // bm #-120
             epd == "r1b5/1pKp4/pP1P1p1p/P4p1B/3pn2p/1P1k4/1P6/5N1N w - -") {  // bm #121
    if (epd.find(" w ") != std::string::npos)
      openingMoves = "h5d1";
    excludeFrom = "d1 f1 h1 b2 b3 a5 b6 d6";
    excludeTo = "c8";
    excludeAllowingFrom = "d3 d4 a6 b7 c8 d7";
    excludeAllowingTo = "d1 f1 h1";
    std::cout << "\n!! WARNING: An engine may be needed (not implemented yet).\n\n";
  } else if (epd == "8/1p1p4/3p2p1/5pP1/1p3P1k/1P1p1P1p/1P1P1P2/6KB b - -" || // bm #-120
             epd == "8/1p1p4/3p2p1/5pP1/1p3P1k/1P1p1P1p/1P1P1P1K/7B w - -") { // bm #121
    excludeCaptures = true;
    excludeFrom = "h1";
    std::cout << "\n!! WARNING: An engine may be needed (not implemented yet).\n\n";
  } else if (epd == "n7/b1p1K3/1pP5/1P6/7p/1p4Pn/1P2N1br/3NRn1k w - -" ||     // bm #6
             epd == "n7/b1p1K3/1pP5/1P6/6pp/1p4Pn/1P2N1br/3NRn1k w - -" ||    // bm #9
             epd == "n7/b1p1K3/1pP5/1P4p1/6pp/1p4Pn/1P2N1br/3NRn1k w - -" ||  // bm #92
             epd == "n7/b1p5/1pP2K1p/1P4p1/6p1/1p4Pn/1P2N1br/3NRn1k b - -" || // bm #-125
             epd == "n7/b1p1K3/1pP4p/1P4p1/6p1/1p4Pn/1P2N1br/3NRn1k w - -") { // bm #126
    excludeFrom = "b2 d1 e1 b5 c6";
    excludeTo = "a8 b6 c7 b3";
    excludeMoves = "e2g1 e2c1 e2c3 e2d4 e2f4 g3h1 g3h5 g3f5 g3e4 g3f1";
    excludeToCapturable = true;
    excludePromotionTo = "qrbn";
    excludeAllowingFrom = "a8 b6 c7 h2 f1";
    std::cout << "\n!! WARNING: An engine may be needed (not implemented yet).\n\n";
  }
  // clang-format on
}

inline void Options::print(std::ostream &os) const {
  os << "--epd \"" << epdStr << "\" ";
  if (depth < MAX_DEPTH)
    os << "--depth " << depth << " ";
  if (maxNodes)
    os << "--maxNodes " << maxNodes << " ";
  if (!openingMoves.empty())
    os << "--openingMoves " << enclosed_string(openingMoves) << " ";
  if (!excludeMoves.empty())
    os << "--excludeMoves " << enclosed_string(excludeMoves) << " ";
  if (!excludeSANs.empty())
    os << "--excludeSANs " << enclosed_string(excludeSANs) << " ";
  if (!restrictTo.empty())
    os << "--restrictTo " << enclosed_string(restrictTo) << " ";
  if (!excludeFrom.empty())
    os << "--excludeFrom " << enclosed_string(excludeFrom) << " ";
  if (!excludeTo.empty())
    os << "--excludeTo " << enclosed_string(excludeTo) << " ";
  if (excludeCaptures)
    os << "--excludeCaptures ";
  if (!excludeCapturesOf.empty())
    os << "--excludeCapturesOf " << enclosed_string(excludeCapturesOf) << " ";
  if (excludeToAttacked)
    os << "--excludeToAttacked ";
  if (excludeToCapturable)
    os << "--excludeToCapturable ";
  if (!excludePromotionTo.empty())
    os << "--excludePromotionTo " << enclosed_string(excludePromotionTo) << " ";
  if (excludeAllowingCapture)
    os << "--excludeAllowingCapture ";
  if (!excludeAllowingFrom.empty())
    os << "--excludeAllowingFrom " << enclosed_string(excludeAllowingFrom)
       << " ";
  if (!excludeAllowingTo.empty())
    os << "--excludeAllowingTo " << enclosed_string(excludeAllowingTo) << " ";
  if (!excludeAllowingMoves.empty())
    os << "--excludeAllowingMoves " << enclosed_string(excludeAllowingMoves)
       << " ";
  if (!excludeAllowingSANs.empty())
    os << "--excludeAllowingSANs " << enclosed_string(excludeAllowingSANs)
       << " ";
  if (concurrency)
    os << "--concurrency " << concurrency << " ";
  if (!outFile.empty())
    os << "--outFile " << enclosed_string(outFile) << " ";
}

inline std::ostream &operator<<(std::ostream &os, const Options &opt) {
  opt.print(os);
  return os;
}
