#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <vector>

#include "external/chess.hpp"
#include "matetb.hpp"

using namespace chess;

using PackedBoard = std::array<std::uint8_t, 24>;

namespace std {
template <> struct hash<PackedBoard> {
  size_t operator()(const PackedBoard pbfen) const {
    std::string_view sv(reinterpret_cast<const char *>(pbfen.data()),
                        pbfen.size());
    return std::hash<std::string_view>{}(sv);
  }
};
} // namespace std

score_t score2mate(score_t score) {
  if (score > 0)
    return (VALUE_MATE - score + 1) / 2;
  if (score < 0)
    return -(VALUE_MATE + score) / 2;
  return VALUE_NONE;
}

class MateTB {
  std::unordered_map<PackedBoard, index_t>
      fen2index; // maps FENs from game tree to their index idx
  std::vector<std::pair<score_t, std::vector<index_t>>>
      tb; // tb[idx] = {score, children}, children a vector of indices
  book_t openingBook; // maps FENs to unique move
  bool allowed_move(Board &board, Move move);
  void initialize_tb();
  void connect_children();
  void generate_tb();
  score_t probe_tb(const std::string &fen);
  std::vector<std::string> obtain_pv(Board board);
  Color mating_side;
  std::string root_pos, excludeCapturesOf, excludePromotionTo;
  std::vector<std::string> excludeSANs, excludeMoves, excludeAllowingMoves,
      excludeAllowingSANs;
  Bitboard BBexcludeFrom, BBexcludeTo, BBexcludeAllowingFrom,
      BBexcludeAllowingTo;
  bool excludeCaptures, excludeToAttacked, excludeToCapturable,
      excludeAllowingCapture, needToGenerateResponses;
  int max_depth, verbose;

public:
  MateTB(const Options &options);
  void create_tb();
  void output();
  void write_tb(const std::string &filename);
};

MateTB::MateTB(const Options &options) {
  std::vector<std::string> parts = split(options.epdStr);
  if (parts.size() < 4) {
    std::cout << "EPD \"" << options.epdStr << "\" is too short." << std::endl;
    std::exit(1);
  }
  root_pos = join(parts.begin(), parts.begin() + 4);
  max_depth = options.depth;
  mating_side = (parts[1] == "b" ? Color::BLACK : Color::WHITE);
  for (size_t i = 4; i < parts.size(); ++i)
    if (parts[i] == "bm" && parts[i + 1].find("#-") != std::string::npos) {
      mating_side = !mating_side;
      break;
    }
  std::cout << "Restrict moves for "
            << (mating_side == Color::WHITE ? "WHITE" : "BLACK") << " side."
            << std::endl;
  excludeSANs = split(options.excludeSANs);
  excludeMoves = split(options.excludeMoves);
  for (const std::string &sq : split(options.excludeFrom))
    BBexcludeFrom |= Bitboard::fromSquare(Square(sq));
  for (const std::string &sq : split(options.excludeTo))
    BBexcludeTo |= Bitboard::fromSquare(Square(sq));
  excludeCaptures = options.excludeCaptures;
  excludeCapturesOf = options.excludeCapturesOf;
  excludeToAttacked = options.excludeToAttacked;
  excludeToCapturable = options.excludeToCapturable;
  excludePromotionTo = options.excludePromotionTo;
  excludeAllowingCapture = options.excludeAllowingCapture;
  for (const std::string &sq : split(options.excludeAllowingFrom))
    BBexcludeAllowingFrom |= Bitboard::fromSquare(Square(sq));
  for (const std::string &sq : split(options.excludeAllowingTo))
    BBexcludeAllowingTo |= Bitboard::fromSquare(Square(sq));
  excludeAllowingMoves = split(options.excludeAllowingMoves);
  excludeAllowingSANs = split(options.excludeAllowingSANs);
  needToGenerateResponses = excludeToCapturable || excludeAllowingCapture ||
                            BBexcludeAllowingFrom || BBexcludeAllowingTo ||
                            !excludeAllowingMoves.empty() ||
                            !excludeAllowingSANs.empty();
  verbose = options.verbose;
  if (!options.openingMoves.empty()) {
    std::cout << "Preparing the opening book ..." << std::endl;
    prepare_opening_book(root_pos, mating_side, options.openingMoves, verbose,
                         openingBook);
    std::cout << "Done. The opening book contains " << openingBook.size()
              << " positions/moves." << std::endl;
    if (verbose >= 4) {
      std::cout << "Opening book: ";
      for (const auto &entry : openingBook)
        std::cout << entry.first << ": " << entry.second << ", ";
      std::cout << std::endl;
    }
  }
}

bool MateTB::allowed_move(Board &board, Move move) {
  // restrict the mating side's candidate moves, to reduce the overall tree size
  if (board.sideToMove() != mating_side)
    return true;
  std::string uci = uci::moveToUci(move);
  if (std::find(excludeMoves.begin(), excludeMoves.end(), uci) !=
      excludeMoves.end())
    return false;
  if (std::find(excludeSANs.begin(), excludeSANs.end(),
                uci::moveToSan(board, move)) != excludeSANs.end())
    return false;
  if (BBexcludeFrom & Bitboard::fromSquare(move.from()))
    return false;
  if (BBexcludeTo & Bitboard::fromSquare(move.to()))
    return false;
  if (excludeCaptures) {
    if (board.isCapture(move))
      return false;
  } else if (!excludeCapturesOf.empty()) {
    if (board.isCapture(move) && excludeCapturesOf.find(tolower(
                                     board.at(move.to()))) != std::string::npos)
      return false;
  }
  if (excludeToAttacked && board.isAttacked(move.to(), ~board.sideToMove()))
    return false;
  if (!excludePromotionTo.empty()) {
    if (uci.length() == 5 &&
        excludePromotionTo.find(tolower(uci[4])) != std::string::npos)
      return false;
  }
  if (needToGenerateResponses) {
    board.makeMove(move);
    Movelist legal_moves;
    movegen::legalmoves(legal_moves, board);
    for (const Move &m : legal_moves) {
      if ((excludeToCapturable && board.isCapture(m) && m.to() == move.to()) ||
          (excludeAllowingCapture && board.isCapture(m)) ||
          (BBexcludeAllowingFrom & Bitboard::fromSquare(m.from())) ||
          (BBexcludeAllowingTo & Bitboard::fromSquare(m.to())) ||
          (std::find(excludeAllowingMoves.begin(), excludeAllowingMoves.end(),
                     uci::moveToUci(m)) != excludeAllowingMoves.end()) ||
          (std::find(excludeAllowingSANs.begin(), excludeAllowingSANs.end(),
                     uci::moveToSan(board, m)) != excludeAllowingSANs.end())) {
        board.unmakeMove(move);
        return false;
      }
    }
    board.unmakeMove(move);
  }
  return true;
}

void MateTB::create_tb() {
  initialize_tb();
  connect_children();
  generate_tb();
}

void MateTB::initialize_tb() {
  auto tic = std::chrono::high_resolution_clock::now();
  std::cout << "Create the allowed part of the game tree ..." << std::endl;
  int count = 0, depth = 0;
  std::queue<std::pair<PackedBoard, int>> q;
  q.push(std::pair<PackedBoard, int>(Board::Compact::encode(root_pos), depth));
  while (!q.empty()) {
    auto pair = q.front();
    const PackedBoard &pfen = pair.first;
    depth = pair.second;
    if (depth > max_depth) {
      depth--;
      break;
    }
    q.pop();
    if (fen2index.count(pfen)) // is pfen already a key in the map?
      continue;
    fen2index[pfen] = count++;
    if (count % 1000 == 0)
      std::cout << "Progress: " << count << " (d" << depth << ")\r"
                << std::flush;
    auto board = Board::Compact::decode(pfen);
    Movelist legal_moves;
    movegen::legalmoves(legal_moves, board);
    score_t score =
        legal_moves.size() == 0 && board.inCheck() ? -VALUE_MATE : 0;
    tb.emplace_back(score, std::vector<index_t>());
    if (score)
      continue;
    std::string onlyMove;
    if (!openingBook.empty()) {
      std::string fen = board.getFen(false);
      if (openingBook.count(fen))
        onlyMove = openingBook[fen];
      if (verbose >= 3 && !onlyMove.empty()) {
        std::cout << "Picked move " << onlyMove << " for " << fen << "."
                  << std::endl;
        if (verbose >= 4) {
          std::cout << "Remaining book: ";
          for (const auto &entry : openingBook)
            std::cout << entry.first << ": " << entry.second << ", ";
          std::cout << std::endl;
        }
      }
    }
    for (const Move &move : legal_moves) {
      if (!onlyMove.empty() && move != uci::uciToMove(board, onlyMove))
        continue;
      if (onlyMove.empty() && !allowed_move(board, move))
        continue;
      board.makeMove<true>(move);
      q.push(std::pair<PackedBoard, int>(Board::Compact::encode(board),
                                         depth + 1));
      board.unmakeMove(move);
    }
  }
  auto toc = std::chrono::high_resolution_clock::now();
  double duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(toc - tic).count() /
      1000.0;
  std::cout << "Found " << count << " positions to depth " << depth << " in "
            << std::fixed << std::setprecision(2) << duration << "s"
            << std::endl;
}

void MateTB::connect_children() {
  auto tic = std::chrono::high_resolution_clock::now();
  std::cout << "Connect child nodes ..." << std::endl;
  size_t dim = fen2index.size(), count = 0;
  for (const auto &entry : fen2index) {
    const PackedBoard &pfen = entry.first;
    index_t idx = entry.second;
    count++;
    if (tb[idx].first) // do not add children to mate nodes
      continue;
    auto board = Board::Compact::decode(pfen);
    Movelist legal_moves;
    movegen::legalmoves(legal_moves, board);
    for (const Move &move : legal_moves) {
      board.makeMove<true>(move);
      auto child_pfen = Board::Compact::encode(board);
      auto it = fen2index.find(child_pfen);
      if (it != fen2index.end()) {
        index_t childidx = it->second;
        tb[idx].second.push_back(childidx);
      }
      board.unmakeMove(move);
    }
    if (count % 10000 == 0)
      std::cout << "Progress: " << count << "/" << dim << "\r" << std::flush;
  }
  auto toc = std::chrono::high_resolution_clock::now();
  double duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(toc - tic).count() /
      1000.0;
  std::cout << "Connected " << tb.size() << " positions in " << std::fixed
            << std::setprecision(2) << duration << "s" << std::endl;
}

void MateTB::generate_tb() {
  auto tic = std::chrono::high_resolution_clock::now();
  std::cout << "Generate tablebase ..." << std::endl;
  int iteration = 0, changed = 1;
  while (changed) {
    changed = 0;
    for (int i = tb.size() - 1; i >= 0; --i) {
      score_t best_score = VALUE_NONE;
      for (index_t child : tb[i].second) {
        score_t score = tb[child].first;
        if (score)
          score = -score + (score > 0 ? 1 : -1);
        if (best_score == VALUE_NONE || score > best_score)
          best_score = score;
      }
      if (best_score != VALUE_NONE && tb[i].first != best_score) {
        tb[i].first = best_score;
        changed++;
      }
    }
    iteration++;
    std::cout << "Iteration " << iteration << ", changed " << std::setw(9)
              << changed << " scores\r" << std::flush;
  }
  auto toc = std::chrono::high_resolution_clock::now();
  double duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(toc - tic).count() /
      1000.0;
  std::cout << "Tablebase generated with " << iteration << " iterations in "
            << std::fixed << std::setprecision(2) << duration << "s"
            << std::endl;
}

void MateTB::write_tb(const std::string &filename) {
  std::ofstream f(filename);
  for (const auto &entry : fen2index) {
    auto board = Board::Compact::decode(entry.first);
    std::string fen = board.getFen(false);
    index_t idx = entry.second;
    score_t s = tb[idx].first;
    std::string bmstr = (s == VALUE_NONE || s == 0)
                            ? ""
                            : " bm #" + std::to_string(score2mate(s)) + ";";
    f << fen << bmstr << std::endl;
  }
  f.close();
  std::cout << "Wrote TB to " << filename << "." << std::endl;
}

score_t MateTB::probe_tb(const std::string &fen) {
  auto it = fen2index.find(Board::Compact::encode(fen));
  if (it != fen2index.end())
    return tb[it->second].first;
  return VALUE_NONE;
}

std::vector<std::string> MateTB::obtain_pv(Board board) {
  if (board.isGameOver().second == GameResult::DRAW)
    return {};
  if (board.sideToMove() != mating_side && board.isHalfMoveDraw() &&
      board.getHalfMoveDrawType().second == GameResult::DRAW)
    return {"; draw by 50mr"};
  std::vector<std::pair<score_t, Move>> moves;
  Movelist legal_moves;
  movegen::legalmoves(legal_moves, board);
  for (const Move &move : legal_moves) {
    board.makeMove<true>(move);
    score_t score = probe_tb(board.getFen(false));
    if (score != VALUE_NONE && score != 0)
      score = -score + (score > 0 ? 1 : -1);
    moves.emplace_back(score, move);
    board.unmakeMove(move);
  }
  if (moves.empty())
    return {};
  auto best_move_it = std::max_element(moves.begin(), moves.end(),
                                       [](const auto &a, const auto &b) {
                                         if (a.first == VALUE_NONE)
                                           return true;
                                         if (b.first == VALUE_NONE)
                                           return false;
                                         return a.first < b.first;
                                       });
  Move bestmove = best_move_it->second;
  board.makeMove<true>(bestmove);
  std::vector<std::string> next_pv = obtain_pv(board);
  board.unmakeMove(bestmove);

  std::vector<std::string> pv;
  pv.push_back(uci::moveToUci(bestmove));
  pv.insert(pv.end(), next_pv.begin(), next_pv.end());
  return pv;
}

void MateTB::output() {
  Board board(root_pos);
  std::vector<std::pair<score_t, std::vector<std::string>>> sp;
  Movelist legal_moves;
  movegen::legalmoves(legal_moves, board);
  for (const Move &move : legal_moves) {
    board.makeMove<true>(move);
    score_t score = probe_tb(board.getFen(false));
    if (score != VALUE_NONE && score != 0)
      score = -score + (score > 0 ? 1 : -1);
    std::vector<std::string> pv;
    if (score != VALUE_NONE && score != 0) {
      Board copy_board = board;
      pv = obtain_pv(copy_board);
    }
    pv.insert(pv.begin(), uci::moveToUci(move));
    sp.emplace_back(score, pv);
    board.unmakeMove(move);
  }
  std::sort(sp.begin(), sp.end(), [](const auto &a, const auto &b) {
    if (a.first == VALUE_NONE)
      return false;
    if (b.first == VALUE_NONE)
      return true;
    return a.first > b.first;
  });
  score_t score = sp[0].first;
  std::string pv_str = join(sp[0].second.begin(), sp[0].second.end());
  if (score != VALUE_NONE && score != 0) {
    std::cout << "\nMatetrack:" << std::endl;
    std::cout << root_pos << " bm #" << score2mate(score) << "; PV: " << pv_str
              << ";" << std::endl;
  } else
    std::cout << "No mate found." << std::endl;
  if (verbose == 0)
    return;
  std::cout << "\nMultiPV:" << std::endl;
  for (size_t count = 0; count < sp.size(); ++count) {
    score_t score = sp[count].first;
    if (score == VALUE_NONE) {
      std::cout << "multipv " << count + 1 << " score None" << std::endl;
      continue;
    }
    std::string score_str = "cp " + std::to_string(score);
    std::string pv_str = join(sp[count].second.begin(), sp[count].second.end());
    if (score != 0)
      score_str += " mate " + std::to_string(score2mate(score));
    if (!pv_str.empty() && pv_str.back() == ';')
      pv_str.pop_back();
    std::cout << "multipv " << count + 1 << " score " << score_str << " pv "
              << pv_str << std::endl;
    if (verbose >= 2)
      std::cout << cdb_link(root_pos, pv_str) << "\n\n";
  }
}

int main(int argc, char **argv) {
  Options options(argc, argv);
  std::cout << "Running with options " << options << std::endl;
  MateTB mtb(options);
  mtb.create_tb();
  mtb.output();
  if (!options.outFile.empty())
    mtb.write_tb(options.outFile);
  return 0;
}
