#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <span>
#include <vector>

#include "external/chess.hpp"
#include "external/parallel_hashmap/phmap.h"
#include "external/threadpool.hpp"
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

// unordered map to map FENs from game tree to their index idx
using index_map_t = phmap::parallel_flat_hash_map<
    PackedBoard, index_t, std::hash<PackedBoard>, std::equal_to<PackedBoard>,
    std::allocator<std::pair<PackedBoard, index_t>>, 8, std::mutex>;

// a vector with idx -> {score, children}, children being a vector of indices
using tb_t = std::vector<std::pair<score_t, std::vector<index_t>>>;

score_t score2mate(score_t score) {
  if (score > 0)
    return (VALUE_MATE - score + 1) / 2;
  if (score < 0)
    return -(VALUE_MATE + score) / 2;
  return VALUE_NONE;
}

class MateTB {
  index_map_t fen2index;
  tb_t tb;
  book_t openingBook; // maps FENs to unique moves
  bool allowed_move(Board &board, Move move);
  score_t spawn_allowed_children(const PackedBoard &pfen,
                                 std::vector<PackedBoard> &children);
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
  int max_depth, verbose, concurrency;

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
  concurrency = options.concurrency;
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

score_t MateTB::spawn_allowed_children(const PackedBoard &pfen,
                                       std::vector<PackedBoard> &children) {
  auto board = Board::Compact::decode(pfen);
  Movelist legal_moves;
  movegen::legalmoves(legal_moves, board);
  score_t score = legal_moves.size() == 0 && board.inCheck() ? -VALUE_MATE : 0;
  if (score)
    return score;
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
    children.push_back(Board::Compact::encode(board));
    board.unmakeMove(move);
  }
  return score;
}

void MateTB::initialize_tb() {
  auto tic = std::chrono::high_resolution_clock::now();
  std::cout << "Create the allowed part of the game tree ..." << std::endl;
  std::vector<PackedBoard> current_level = {Board::Compact::encode(root_pos)};
  std::vector<std::pair<PackedBoard, score_t>> mate_score;
  std::mutex mate_score_mutex;
  int depth = 0;
  std::atomic<size_t> count = 0;
  for (; !current_level.empty() && depth <= max_depth; depth++) {
    std::vector<PackedBoard> next_level;
    std::mutex next_level_mutex;
    ThreadPool pool(concurrency);
    size_t batch_size =
        std::max(size_t(128), current_level.size() / (concurrency * 8));
    for (size_t i = 0; i < current_level.size(); i += batch_size) {
      size_t batch_end = std::min(i + batch_size, current_level.size());
      std::span<PackedBoard> batch(current_level.begin() + i,
                                   current_level.begin() + batch_end);
      pool.enqueue([this, batch, &next_level, &next_level_mutex, &mate_score,
                    &mate_score_mutex, &count, depth]() {
        std::vector<PackedBoard> local_next_level;
        std::vector<std::pair<PackedBoard, score_t>> local_mate_score;
        for (const auto &pfen : batch) {
          size_t count_check;
          bool is_new_entry = fen2index.lazy_emplace_l(
              pfen, [](index_map_t::value_type &) {},
              [&pfen, &count,
               &count_check](const index_map_t::constructor &ctor) {
                ctor(std::move(pfen), count_check = count++);
              });
          if (!is_new_entry)
            continue;
          std::vector<PackedBoard> children;
          score_t score = spawn_allowed_children(pfen, children);
          if (score) {
            local_mate_score.push_back({pfen, score});
          } else {
            local_next_level.insert(local_next_level.end(), children.begin(),
                                    children.end());
          }
          if (count_check % 10000 == 0) {
            std::stringstream ss;
            ss << "Progress: " << count_check << " (d" << depth << ")\r";
            std::cout << ss.str() << std::flush;
          }
        }
        if (!local_mate_score.empty()) {
          std::lock_guard<std::mutex> lock(mate_score_mutex);
          mate_score.insert(mate_score.end(), local_mate_score.begin(),
                            local_mate_score.end());
        }
        if (!local_next_level.empty()) {
          std::lock_guard<std::mutex> lock(next_level_mutex);
          next_level.insert(next_level.end(), local_next_level.begin(),
                            local_next_level.end());
        }
      });
    }
    pool.wait();
    current_level = std::move(next_level);
  }
  auto toc = std::chrono::high_resolution_clock::now();
  double duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(toc - tic).count() /
      1000.0;
  std::cout << "Found " << count << " positions to depth " << depth - 1
            << " in " << std::fixed << std::setprecision(2) << duration << "s  "
            << std::endl;
  std::cout << "Seed the mate scores ...\r" << std::flush;
  tb.assign(count, {0, {}});
  for (const auto &entry : mate_score)
    tb[fen2index[entry.first]].first = entry.second;
}

// The multi-threaded implementation of connect_children() does not need a lock
// because of the bijection between pfen and idx. Each thread is assigned a
// different pfen, and the only writes are to tb[idx].second.
void MateTB::connect_children() {
  auto tic = std::chrono::high_resolution_clock::now();
  std::cout << "Connect child nodes ... " << std::endl;
  size_t dim = fen2index.size();
  std::atomic<size_t> count = 0;
  ThreadPool pool(concurrency);
  for (const auto &[pfen, idx] : fen2index) {
    pool.enqueue([this, &pfen, idx, &count, dim]() {
      if (tb[idx].first) // do not add children to mate nodes
        return;
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
      const size_t count_check = ++count;
      if (count_check % 10000 == 0) {
        std::stringstream ss;
        ss << "Progress: " << count_check << "/" << dim << "\r";
        std::cout << ss.str() << std::flush;
      }
    });
  }
  pool.wait();
  auto toc = std::chrono::high_resolution_clock::now();
  double duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(toc - tic).count() /
      1000.0;
  std::cout << "Connected " << tb.size() << " positions in " << std::fixed
            << std::setprecision(2) << duration << "s" << std::endl;
}

// The multi-threaded implementation of generate_tb() allows data races.
// But these can only occur if threadA writes tb[j].first while threadB reads
// tb[child].first (with j == child), and the former ensures changed > 0.
// Hence only a race-free iteration without changes can signal convergence.
void MateTB::generate_tb() {
  auto tic = std::chrono::high_resolution_clock::now();
  std::cout << "Generate tablebase ..." << std::endl;
  int iteration = 0;
  std::atomic<int> changed = 1;
  while (changed) {
    changed = 0;
    ThreadPool pool(concurrency);
    int batch_size = std::max(128, int(tb.size() / (concurrency * 32)));
    for (int i = tb.size() - 1; i >= 0; i -= batch_size) {
      int batch_start = std::max(0, i - batch_size + 1);
      int batch_end = i + 1;
      pool.enqueue([this, batch_start, batch_end, &changed]() {
        int batch_changed = 0;
        for (int j = batch_end - 1; j >= batch_start; --j) {
          score_t best_score = VALUE_NONE;
          for (index_t child : tb[j].second) {
            score_t score = tb[child].first;
            if (score)
              score = -score + (score > 0 ? 1 : -1);
            if (best_score == VALUE_NONE || score > best_score)
              best_score = score;
          }
          if (best_score != VALUE_NONE && tb[j].first != best_score) {
            tb[j].first = best_score;
            batch_changed++;
          }
        }
        changed += batch_changed;
      });
    }
    pool.wait();
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
  for (const auto &[pfen, idx] : fen2index) {
    auto board = Board::Compact::decode(pfen);
    std::string fen = board.getFen(false);
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
  Options options(argc, argv, true /* use_concurrency */);
  std::cout << "Running with options " << options << std::endl;
  MateTB mtb(options);
  mtb.create_tb();
  mtb.output();
  if (!options.outFile.empty())
    mtb.write_tb(options.outFile);
  return 0;
}
