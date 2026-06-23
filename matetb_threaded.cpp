#include <algorithm>
#include <atomic>
#include <chrono>
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

// unordered map to map FENs from game tree to their index idx
using index_map_t = phmap::parallel_flat_hash_map<
    PackedBoard, index_t, std::hash<PackedBoard>, std::equal_to<PackedBoard>,
    std::allocator<std::pair<PackedBoard, index_t>>, 8, std::mutex>;

class MateTB : public MateTbBase<index_map_t> {
  score_t spawn_allowed_children(const PackedBoard &pfen,
                                 std::vector<PackedBoard> &children);
  int concurrency;
  void initialize_tb();
  void connect_children();
  void generate_tb();

public:
  MateTB(const Options &options)
      : MateTbBase<index_map_t>(options), concurrency(options.concurrency) {}
};

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
