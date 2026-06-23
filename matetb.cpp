#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
#include <queue>
#include <vector>

#include "external/chess.hpp"
#include "matetb.hpp"

using namespace chess;

// unordered map to map FENs from game tree to their index idx
using index_map_t = std::unordered_map<PackedBoard, index_t>;

class MateTB : public MateTbBase<index_map_t> {
  void initialize_tb();
  void connect_children();
  void generate_tb();
public:
  MateTB(const Options &options) : MateTbBase<index_map_t>(options) {}
};

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
  for (const auto &[pfen, idx] : fen2index) {
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
