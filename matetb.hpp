#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "external/argparse.hpp"
#include "external/chess.hpp"

using namespace chess;

using score_t = std::int16_t;
using index_t = std::uint32_t;
using book_t = std::map<std::string, std::string>;

constexpr score_t VALUE_NONE = 30001;
constexpr score_t VALUE_MATE = 30000;
constexpr int MAX_DEPTH = std::numeric_limits<int>::max();

template <typename T> void split(const std::string &s, char delim, T result) {
  std::istringstream iss(s);
  std::string item;
  while (std::getline(iss, item, delim)) {
    if (!item.empty())
      *result++ = item;
  }
}

std::vector<std::string> split(const std::string &s, char delim = ' ') {
  std::vector<std::string> elems;
  split(s, delim, std::back_inserter(elems));
  return elems;
}

template <typename T>
std::string join(T begin, T end, std::string const &separator = " ") {
  std::ostringstream result;
  if (begin != end)
    result << *begin++;
  while (begin != end)
    result << separator << *begin++;
  return result.str();
}

std::string enclosed_string(const std::string &s) {
  if (s.find(" ") != std::string::npos)
    return "\"" + s + "\"";
  return s;
}

std::string cdb_link(const std::string &root_pos, const std::string &pv_str) {
  auto s = "https://chessdb.cn/queryc_en/?" + root_pos + " moves " + pv_str;
  return std::regex_replace(s, std::regex(" "), "_");
}

class Options {
public:
  std::string epdStr, openingMoves, excludeMoves, excludeSANs, excludeFrom,
      excludeTo, excludeCapturesOf, excludePromotionTo, excludeAllowingFrom,
      excludeAllowingTo, excludeAllowingMoves, excludeAllowingSANs, outFile;
  bool excludeCaptures, excludeToAttacked, excludeToCapturable,
      excludeAllowingCapture;
  int depth, verbose;
  Options()
      : epdStr(""), openingMoves(""), excludeMoves(""), excludeSANs(""),
        excludeFrom(""), excludeTo(""), excludeCapturesOf(""),
        excludePromotionTo(""), excludeAllowingFrom(""), excludeAllowingTo(""),
        excludeAllowingMoves(""), excludeAllowingSANs(""), outFile(""),
        excludeCaptures(false), excludeToAttacked(false),
        excludeToCapturable(false), excludeAllowingCapture(false),
        depth(MAX_DEPTH), verbose(0) {}
  Options(int argc, char **argv);
  void fill_exclude_options();
  void print(std::ostream &os) const;
};

Options::Options(int argc, char **argv) : Options() {
  argparse::ArgumentParser args("matetb");
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
      .help("Specify the verbosity level. E.g. --verbose 1 shows PVs for all "
            "legal moves, and --verbose 2 also links to chessdb.cn.");
  try {
    args.parse_args(argc, argv);
  } catch (const std::runtime_error &err) {
    std::cerr << err.what() << std::endl;
    std::cerr << args;
    std::exit(1);
  }
  epdStr = args.get("epd");
  depth = args.get<int>("depth");
  openingMoves = args.get("openingMoves");
  excludeMoves = args.get("excludeMoves");
  excludeSANs = args.get("excludeSANs");
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
  fill_exclude_options();
}

void Options::fill_exclude_options() {
  // For some known EPDs, this defines the right exclude commands.
  if (!openingMoves.empty() || !excludeMoves.empty() || !excludeSANs.empty() ||
      !excludeFrom.empty() || !excludeTo.empty() || excludeCaptures ||
      !excludeCapturesOf.empty() || excludeToAttacked || excludeToCapturable ||
      !excludePromotionTo.empty() || excludeAllowingCapture ||
      !excludeAllowingFrom.empty() || !excludeAllowingTo.empty() ||
      !excludeAllowingMoves.empty() || !excludeAllowingSANs.empty())
    return;

  auto parts = split(epdStr);
  parts.resize(4);
  std::string epd = join(parts.begin(), parts.end(), " ");

  if (epd == "8/8/7p/5K1k/R7/8/8/8 w - -") { // bm #6
    excludeAllowingCapture = true;
    excludeAllowingMoves = "h2h1q";
    if (depth == MAX_DEPTH)
      depth = 11;
  } else if (epd == "8/4p2p/8/8/8/8/6p1/2B1K1kb w - -") { // bm #7
    excludeAllowingCapture = true;
    excludeAllowingFrom = "g1";
    excludeAllowingMoves = "e6e5 e5e4";
  } else if (epd == "8/8/7P/8/pp6/kp6/1p6/1Kb5 w - -") { // bm #7
    excludeFrom = "b1";
    excludeCaptures = true;
    excludePromotionTo = "qrb";
    excludeToCapturable = true;
  } else if (epd == "8/6Q1/8/7k/8/6p1/6p1/6Kb w - -" || // bm #7
             epd == "8/8/8/8/Q7/5kp1/6p1/6Kb w - -") {  // bm #7
    excludeFrom = "g1";
    excludeToCapturable = true;
    if (depth == MAX_DEPTH)
      depth = 13;
  } else if (epd == "8/3Q4/8/1r6/kp6/bp6/1p6/1K6 w - -") { // bm #8
    excludeFrom = "b1";
    excludeTo = "b3";
    excludeToCapturable = true;
    if (depth == MAX_DEPTH)
      depth = 15;
  } else if (epd == "k7/2Q5/8/2p5/1pp5/1pp5/prp5/nbK5 w - -") { // bm #11
    excludeFrom = "c1";
    excludeTo = "b2";
    excludeToCapturable = true;
  } else if (epd == "8/2P5/8/8/8/1p2k1p1/1p1pppp1/1Kbrqbrn w - -") { // bm #12
    openingMoves = "c7c8q";
    excludeFrom = "b1";
    excludeToCapturable = true;
  } else if (epd == "8/8/1p6/1p6/1p6/1p6/pppbK3/rbk3N1 w - -") { // bm #13
    excludeFrom = "e2";
    excludeToCapturable = true;
  } else if (epd == "8/8/8/6r1/8/6B1/p1p5/k1Kb4 w - -" ||            // bm #7
             epd == "k7/8/1Qp5/2p5/2p5/6p1/2p1ppp1/2Kbrqrn w - -") { // bm #15
    excludeFrom = "c1";
    excludeToCapturable = true;
  } else if (epd == "8/8/8/2p5/1pp5/brpp4/1pprp2P/qnkbK3 w - -") { // bm #15
    excludeFrom = "e1";
    excludePromotionTo = "qrb";
    excludeToCapturable = true;
  } else if (epd == "4k3/6Q1/8/8/5p2/1p1p1p2/1ppp1p2/nrqrbK2 w - -") { // bm #15
    excludeFrom = "f1";
    excludeToCapturable = true;
  } else if (epd == "8/8/8/2p5/1pp5/brpp4/qpprp2P/1nkbnK2 w - -") { // bm #16
    openingMoves = "f1e1";
    excludeFrom = "e1";
    excludePromotionTo = "qrb";
    excludeToCapturable = true;
  } else if (epd == "8/8/8/2p5/1pp5/brpp4/qpprpK1P/1nkbn3 w - -") { // bm #16
    openingMoves = "f2e1";
    excludeFrom = "e1";
    excludePromotionTo = "qrb";
    excludeToCapturable = true;
  } else if (epd == "8/p7/8/8/8/3p1b2/pp1K1N2/qk6 w - -") { // bm #18
    excludeFrom = "d2";
    excludeToCapturable = true;
  } else if (epd == "k7/8/1Q6/8/8/6p1/1p1pppp1/1Kbrqbrn w - -") { // bm #26
    excludeFrom = "b1";
    excludeToCapturable = true;
  } else if (epd == "8/8/2p5/2p5/p1p5/rbp5/p1p2Q2/n1K4k w - -" || // bm #26
             epd == "8/2p5/2p5/8/p1p5/rbp5/p1p2Q2/n1K4k w - -") { // bm #28
    excludeFrom = "c1";
    excludeTo = "a3 c3";
    excludeToCapturable = true;
  } else if (epd == "4k3/6Q1/8/5p2/5p2/1p3p2/1ppp1p2/nrqrbK2 w - -" || // bm #17
             epd == "4k3/6Q1/8/8/8/1p3p2/1ppp1p2/nrqrbK2 w - -" ||     // bm #18
             epd == "8/7p/4k3/5p2/3Q1p2/5p2/5p1p/5Kbr w - -") {        // bm #30
    excludeFrom = "f1";
    excludeTo = "h1";
    excludeToCapturable = true;
  } else if (epd == "8/8/8/8/6k1/8/2Qp1pp1/3Kbrrb w - -" ||        // bm #9
             epd == "8/3Q4/8/2kp4/8/1p1p4/pp1p4/rrbK4 w - -" ||    // bm #12
             epd == "8/8/8/6k1/3Q4/8/3p1pp1/3Kbrrb w - -" ||       // bm #12
             epd == "k7/8/8/2Q5/3p4/1p1p4/pp1p4/rrbK4 w - -" ||    // bm #14
             epd == "7k/8/8/8/8/5Qp1/3p1pp1/3Kbrrn w - -" ||       // bm #16
             epd == "6k1/8/5Q2/8/8/8/3p1pp1/3Kbrrb w - -" ||       // bm #17
             epd == "4Q3/6k1/8/8/8/8/3p1pp1/3Kbrrb w - -" ||       // bm #18
             epd == "5k2/8/4Q3/8/8/8/3p1pp1/3Kbrrb w - -" ||       // bm #18
             epd == "6k1/8/8/8/8/3Q4/3p1pp1/3Kbrrb w - -" ||       // bm #18
             epd == "8/8/8/1p6/1k6/3Q4/pp1p4/rrbK4 w - -" ||       // bm #18
             epd == "4k3/8/3Q4/8/8/8/3p1pp1/3Kbrrb w - -" ||       // bm #19
             epd == "4k3/2Q5/8/8/8/8/3p1pp1/3Kbrrb w - -" ||       // bm #20
             epd == "8/8/8/8/1Q6/3k4/3p1pp1/3Kbrrb w - -" ||       // bm #20
             epd == "8/8/6k1/Q7/8/8/3p1pp1/3Kbrrb w - -" ||        // bm #20
             epd == "8/8/2k5/8/3p4/Qp1p4/pp1p4/rrbK4 w - -" ||     // bm #20
             epd == "8/3k4/3p1Q2/8/8/1p1p4/pp1p4/rrbK4 w - -" ||   // bm #23
             epd == "8/1p6/1Q6/8/2kp4/3p4/pp1p4/rrbK4 w - -" ||    // bm #26
             epd == "8/6p1/4Q3/6k1/8/8/3p1pp1/3Kbrrb w - -" ||     // bm #29
             epd == "2k5/3p4/1Q6/8/8/1p1p4/pp1p4/rrbK4 w - -" ||   // bm #30
             epd == "4k3/3p4/5Q2/8/8/1p1p4/pp1p4/rrbK4 w - -" ||   // bm #30
             epd == "3Q4/8/8/8/k7/8/3p1pp1/3Kbrrb w - -" ||        // bm #32
             epd == "8/2Q5/8/8/1k1p4/4p1p1/3prpp1/3Kbbrn w - -") { // bm #34
    excludeFrom = "d1";
    excludeAllowingCapture = true;
  } else if (epd == "8/8/8/1p6/6k1/1Q6/p1p1p3/rbrbK3 b - -" ||   // bm #-35
             epd == "8/8/8/1p6/6k1/1p2Q3/p1p1p3/rbrbK3 w - -") { // bm #36
    excludeFrom = "e1";
    excludeTo = "a1 c1";
    excludeToAttacked = true;
  } else if (epd == "7k/8/5p2/8/8/8/P1Kp1pp1/4brrb w - -") { // bm #43
    openingMoves = "c2d1";
    excludeFrom = "d1";
    excludeToAttacked = true;
  } else if (epd == "8/1p6/8/3p3k/3p4/6Q1/pp1p4/rrbK4 w - -") { // bm #46
    excludeFrom = "d1";
    excludeCaptures = true;
    excludeToAttacked = true;
  } else if (epd == "6Q1/8/7k/8/8/6p1/4p1pb/4Kbrr w - -" ||    // bm #12
             epd == "2Q5/k7/8/8/8/8/1pp1p3/brrbK3 w - -" ||    // bm #16
             epd == "8/8/3p4/1Q6/8/2k5/ppp1p3/brrbK3 w - -" || // bm #22
             epd == "8/1p2k3/8/8/5Q2/8/ppp1p3/qrrbK3 w - -" || // bm #50
             epd == "8/1p2k3/8/8/5Q2/8/ppp1p3/bqrbK3 w - -") { // bm #50
    excludeFrom = "e1";
    excludeAllowingCapture = true;
  } else if (epd == "8/7p/7p/7p/1p3Q1p/1Kp5/nppr4/qrk5 w - -") { // bm #54
    excludeFrom = "b3";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b1 h1";
    excludeAllowingMoves = "c3c2";
  } else if (epd == "8/1p6/4k3/8/3p1Q2/3p4/pp1p4/rrbK4 w - -" ||    // bm #56
             epd == "8/6pp/5p2/k7/3p4/1Q2p3/3prpp1/3Kbqrb w - -") { // bm #57
    excludeFrom = "d1";
    excludeToAttacked = true;
  } else if (epd ==
             "5Q2/p1p5/p1p5/6rp/7k/6p1/p1p3P1/rbK5 w - -") { // bm #60 (finds
                                                             // #62)
    excludeFrom = "c1 g2";
    excludeTo = "a1 g3";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "h5";
  } else if (epd == "4R3/1n1p4/3n4/8/8/p4p2/7p/5K1k w - -" ||     // bm #20
             epd == "4R3/1n1p1p2/3n4/8/8/p4p2/7p/5K1k w - -" ||   // bm #32
             epd == "4R3/pn1p1p1p/p2n4/8/8/p4p2/7p/5K1k w - -") { // bm #69
    openingMoves = "e8e1 d6e4 e1e4 f3f2 f1f2 * e4e1, e8e1 d6e4 e1e4 * e4e1, "
                   "e8e1 * f1f2";
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
  } else if (epd == "8/1p4Pp/1p6/1p6/1p5p/5r1k/5p1p/5Kbr w - -") { // bm #72
    openingMoves = "g7g8q";
    excludeFrom = "f1";
    excludeTo = "h1";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b3 h5 h4";
  } else if (epd == "8/6Pp/8/8/7p/5r2/4Kpkp/6br w - -" ||         // bm #19
             epd == "8/1p4Pp/1p6/1p6/1p5p/5r2/4Kpkp/6br w - -") { // bm #77
    openingMoves =
        "g7g8q g2h3 e2f1, g7g8q f3g3 g8d5 g3f3 d5f3, g7g8q f3g3 g8d5 g2h3 "
        "d5e6 g3g4 e2f1, g7g8q f3g3 g8d5 g2h3 d5e6 h3g2 e6e4 g3f3 e4f3, "
        "g7g8q f3g3 g8d5 g2h3 d5e6 h3g2 e6e4 g2h3 e2f1";
    excludeFrom = "f1";
    excludeTo = "h1";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b3 h5 h4";
  } else if (epd == "8/8/8/8/NK6/1B1N4/2rpn1pp/2bk1brq w - -" ||      // bm #7
             epd == "8/7p/8/8/NK6/1B1N4/2rpn1pp/2bk1brq w - -" ||     // bm #27
             epd == "8/5ppp/5p2/8/NK6/1B1N4/2rpn1pp/2bk1brq w - -") { // bm #87
    excludeSANs = "Nb6 Nb5 Nc4";
    excludeFrom = "a4 b3 d3";
    excludeAllowingCapture = true;
    std::cout
        << "\n!! WARNING: An engine may be needed (not implemented yet).\n\n";
  } else if (epd == "8/5P2/8/8/8/n7/1pppp2K/br1r1kn1 w - -" ||     // bm #10
             epd == "8/3p1P2/8/8/8/n7/1pppp2K/br1r1kn1 w - -" ||   // bm #28
             epd == "8/2pp1P2/8/8/8/n7/1pppp2K/br1r1kn1 w - -" ||  // bm #48
             epd == "8/pppp1P2/8/8/8/n7/1pppp2K/br1r1kn1 w - -") { // bm #93
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
    excludeSANs = "Kh1 Kg1 Kg2 Kg3 Kg4 Kh4";
    excludeTo = "b2 c2 d2 e2";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b2 c2 d2 e2";
    excludeAllowingSANs = "Ke3 Kf3 Kh1 Kg2 Kh2";
  } else if (epd == "7K/8/8/8/4n3/pp1N3p/rp2N1br/bR3n1k w - -" ||   // bm #3
             epd == "7K/8/8/7p/p3n3/1p1N3p/rp2N1br/bR3n1k w - -" || // bm #31
             epd == "7K/3p4/4p3/1p5p/p3n3/1p1N3p/rp2N1br/bR3n1k w - -") { // bm
                                                                          // #96
    excludeFrom = "d3 e2";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "b2 h2 h1";
    excludeAllowingSANs = "Be4 Bd5 Bc6 Bb7 Ba8 Bg4 Bh5";
    std::cout
        << "\n!! WARNING: An engine may be needed (not implemented yet).\n\n";
  } else if (
      epd == "8/8/6p1/6Pb/p3P1k1/P1p1PNnr/2P1PKRp/7B w - -" ||        // bm #12
      epd == "8/4p3/6p1/6Pb/p3P1k1/P1p1PNnr/2P1PKRp/7B w - -" ||      // bm #34
      epd == "8/p1p1p3/2p3p1/6Pb/p3P1k1/P1p1PNnr/2P1PKRp/7B w - -") { // bm #100
    excludeSANs = "Rf2";
    excludeFrom = "f3 e4";
    excludeAllowingCapture = true;
  } else if (
      epd == "n1K5/bNp5/1pP5/1k4p1/1N2pnp1/PP2p1p1/4rpP1/5B2 w - -" || // bm #16
      epd ==
          "n1K5/bNp1p3/1pP5/1k4p1/1N3np1/PP2p1p1/4rpP1/5B2 w - -" || // bm #35
      epd ==
          "n1K5/bNp1p1p1/1pP5/1k6/1N3np1/PP2p1p1/4rpP1/5B2 w - -" || // bm #57
      epd == "n1K5/bNp1p1p1/1pP3p1/1k2p3/1N3n2/PP4p1/4rpP1/5B2 w - -") { // bm
                                                                         // #101
    excludeFrom = "a3 b3 b4 b7 c6 g2";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a8 b5 b6 c7 e2 f1 g3 g2 d3";
    excludeTo = "a8";
    excludeToCapturable = true;
    excludeMoves = "f1c4 e2c4 e2d1 e2f3 e2g4 e2h5 f1g2 f1h3 d3c2 d3b1 d3e4 "
                   "d3f5 d3g6 d3h7";
    std::cout
        << "\n!! WARNING: An engine may be needed (not implemented yet).\n\n";
  } else if (epd ==
                 "8/8/8/3p2p1/p2np1K1/p3N1pp/rb1N2pr/k1n3Rb w - -" || // bm #4
             epd == "8/8/8/3p2p1/p2np1Kp/p3N1p1/rb1N2pr/k1n3Rb w - -" || // bm
                                                                         // #35
             epd ==
                 "8/4p3/3p4/p5p1/3n2Kp/p3N1p1/rb1N2pr/k1n3Rb w - -") { // bm
                                                                       // #102
    excludeFrom = "d2 e3 g1";
    excludeTo = "g3";
    excludeAllowingFrom = "a1 a2 d5";
    excludeAllowingCapture = true;
    std::cout
        << "\n!! WARNING: An engine may be needed (not implemented yet).\n\n";
  } else if (
      epd ==
          "2RN1qN1/5P2/3p1P2/3P4/1K6/1p1p1pp1/1p1p1np1/bk1b2Q1 w - -" || // bm
                                                                         // #5
      epd ==
          "2RN1qN1/5P2/3p1P2/3P4/8/Kp1p1pp1/1p1p1np1/bk1b2Q1 w - -" || // bm #21
      epd ==
          "3N1qN1/1Kn2P2/3p1Pp1/3P1pp1/R7/1p1p4/1p1p1n2/bk1b2Q1 w - -" || // bm
                                                                          // #107
      epd ==
          "3N1qN1/1Kn2P2/1Q1p1Pp1/3P1pp1/1R6/1p1p4/kp1p4/b2b3n w - -") { // bm
                                                                         // #109
                                                                         // (not
                                                                         // yet)
    if (epd == "3N1qN1/1Kn2P2/1Q1p1Pp1/3P1pp1/1R6/1p1p4/kp1p4/b2b3n w - -")
      openingMoves = "b4a4 * b6g1";
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
    std::cout << "\n!! WARNING: An engine may be needed (not implemented "
                 "yet).\n\n";
  } else if (epd == "8/p7/8/p7/b3Q3/K7/p1r5/rk6 w - -" ||      // bm #10
             epd == "8/p7/8/p7/b3Q3/K6p/p1r5/rk6 w - -" ||     // bm #22
             epd == "8/p6p/7p/p6p/b3Q2p/K6p/p1r5/rk6 w - -") { // bm #120
    excludeFrom = "a3";
    excludeTo = "a1";
    excludeAllowingCapture = true;
    excludeAllowingFrom = "a1 h1";
    excludeAllowingSANs = "Kb1 Kc2 Kd1 Kd2";
  } else if (
      epd == "r1b5/1pKp4/pP1P4/P6B/3pn3/1P1k4/1P6/5N1N w - -" ||       // bm #4
      epd == "r1b5/1pKp4/pP1P4/P6B/3pn2p/1P1k4/1P6/5N1N w - -" ||      // bm #26
      epd == "r1b5/1pKp4/pP1P1p1p/P4p1B/3pn2p/1P1k4/1P6/5N1N w - -") { // bm
                                                                       // #121
    openingMoves = "h5d1";
    excludeFrom = "d1 f1 h1 b2 b3 a5 b6 d6";
    excludeTo = "c8";
    excludeAllowingFrom = "d3 d4 a6 b7 c8 d7";
    excludeAllowingTo = "d1 f1 h1";
    std::cout << "\n!! WARNING: An engine may be needed (not implemented "
                 "yet).\n\n";
  } else if (epd == "8/1p1p4/3p2p1/5pP1/1p3P1k/1P1p1P1p/1P1P1P1K/7B w - "
                    "-") { // bm
                           // #121
    excludeCaptures = true;
    excludeFrom = "h1";
    std::cout << "\n!! WARNING: An engine may be needed (not implemented "
                 "yet).\n\n";
  } else if (
      epd == "n7/b1p1K3/1pP5/1P6/7p/1p4Pn/1P2N1br/3NRn1k w - -" ||     // bm #6
      epd == "n7/b1p1K3/1pP5/1P6/6pp/1p4Pn/1P2N1br/3NRn1k w - -" ||    // bm #9
      epd == "n7/b1p1K3/1pP5/1P4p1/6pp/1p4Pn/1P2N1br/3NRn1k w - -" ||  // bm #92
      epd == "n7/b1p1K3/1pP4p/1P4p1/6p1/1p4Pn/1P2N1br/3NRn1k w - -") { // bm
                                                                       // #126
    excludeFrom = "b2 d1 e1 b5 c6";
    excludeTo = "a8 b6 c7 b3";
    excludeMoves = "e2g1 e2c1 e2c3 e2d4 e2f4 g3h1 g3h5 g3f5 g3e4 g3f1";
    excludeToCapturable = true;
    excludePromotionTo = "qrbn";
    excludeAllowingFrom = "a8 b6 c7 h2 f1";
    std::cout << "\n!! WARNING: An engine may be needed (not implemented "
                 "yet).\n\n";
  }
}

void Options::print(std::ostream &os) const {
  os << "--epd \"" << epdStr << "\" ";
  if (depth < MAX_DEPTH)
    os << "--depth " << depth << " ";
  if (!openingMoves.empty())
    os << "--openingMoves " << enclosed_string(openingMoves) << " ";
  if (!excludeMoves.empty())
    os << "--excludeMoves " << enclosed_string(excludeMoves) << " ";
  if (!excludeSANs.empty())
    os << "--excludeSANs " << enclosed_string(excludeSANs) << " ";
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
  if (!outFile.empty())
    os << "--outFile " << enclosed_string(outFile) << " ";
}

std::ostream &operator<<(std::ostream &os, const Options &opt) {
  opt.print(os);
  return os;
}

void prepare_opening_book(std::string root_pos, Color mating_side,
                          const std::string &openingMoves, int verbose,
                          book_t &openingBook) {
  std::vector<std::vector<std::string>> lines;
  std::string line;
  std::istringstream iss(openingMoves);
  while (std::getline(iss, line, ',')) {
    int star = std::count(line.begin(), line.end(), '*');
    if (star > 1) {
      std::cout << "More than one '*' in line " << line << "." << std::endl;
      std::exit(1);
    }
    std::string before_star(line), after_star;
    if (star) {
      size_t star_pos = line.find('*');
      before_star = line.substr(0, star_pos);
      after_star = line.substr(star_pos + 1);
    }
    auto moves = split(before_star);
    if (star) {
      auto after_star_moves = split(after_star);
      Board board(root_pos);
      for (const auto &m : moves)
        board.makeMove(uci::uciToMove(board, m));
      Movelist legal_moves;
      movegen::legalmoves(legal_moves, board);
      for (const Move &move : legal_moves) {
        bool already_present = false;
        for (const auto &existing_line : lines) {
          if (existing_line.size() < moves.size() + 1)
            continue;
          bool match = true;
          for (size_t i = 0; i < moves.size(); ++i)
            if (existing_line[i] != moves[i]) {
              match = false;
              break;
            }
          if (match && existing_line[moves.size()] == uci::moveToUci(move)) {
            already_present = true;
            break;
          }
        }
        if (!already_present) {
          std::vector<std::string> new_line(moves);
          new_line.push_back(uci::moveToUci(move));
          new_line.insert(new_line.end(), after_star_moves.begin(),
                          after_star_moves.end());
          lines.push_back(new_line);
        }
      }
    } else
      lines.push_back(moves);
  }
  for (const auto &moves : lines) {
    if (verbose >= 3) {
      auto pv_str = join(moves.begin(), moves.end());
      std::cout << "Processing line " << pv_str << " ..." << std::endl;
      if (verbose >= 4)
        std::cout << cdb_link(root_pos, pv_str) << std::endl;
    }
    Board board(root_pos);
    for (const auto &move_str : moves) {
      if (board.sideToMove() == mating_side) {
        std::string fen = board.getFen(false);
        if (openingBook.count(fen) && openingBook[fen] != move_str) {
          std::cout << "Cannot specify both " << move_str << " and "
                    << openingBook[fen] << " for position " << fen << "."
                    << std::endl;
          std::exit(1);
        } else
          openingBook[fen] = move_str;
      }
      Movelist legal_moves;
      movegen::legalmoves(legal_moves, board);
      Move m = uci::uciToMove(board, move_str);
      if (std::find(legal_moves.begin(), legal_moves.end(), m) ==
          legal_moves.end()) {
        std::string fen = board.getFen(false);
        std::cout << "Illegal move " << uci::moveToUci(m) << " in position "
                  << fen << "." << std::endl;
        std::exit(1);
      }
      board.makeMove<true>(m);
    }
  }
}
