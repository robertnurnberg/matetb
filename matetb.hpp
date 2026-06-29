#pragma once

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "external/chess.hpp"
#include "misc.hpp"
#include "options.hpp"

using namespace chess;

using PackedBoard = std::array<std::uint8_t, 24>;

struct PackedBoardHash {
  size_t operator()(const PackedBoard &pbfen) const {
    std::string_view sv(reinterpret_cast<const char *>(pbfen.data()),
                        pbfen.size());
    return std::hash<std::string_view>{}(sv);
  }
};

// a vector with idx -> {score, children}, children being a vector of indices
using tb_t = std::vector<std::pair<score_t, std::vector<index_t>>>;

inline score_t score2mate(score_t score) {
  if (score > 0)
    return (VALUE_MATE - score + 1) / 2;
  if (score < 0)
    return -(VALUE_MATE + score) / 2;
  return VALUE_NONE;
}

inline void prepare_opening_book(std::string root_pos, Color mating_side,
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

template <typename T> class MateTbBase {
protected:
  T fen2index;
  tb_t tb;
  book_t openingBook; // maps FENs to unique moves
  Color mating_side;
  bool mating_side_to_move;
  std::string root_pos, excludeCapturesOf, excludePromotionTo;
  std::vector<std::string> excludeSANs, excludeMoves, excludeAllowingMoves,
      excludeAllowingSANs;
  Bitboard BBrestrictTo, BBexcludeFrom, BBexcludeTo, BBexcludeAllowingFrom,
      BBexcludeAllowingTo;
  bool excludeCaptures, excludeToAttacked, excludeToCapturable,
      excludeAllowingCapture, needToGenerateResponses;
  int max_depth, verbose;

  bool allowed_move(Board &board, Move move) {
    // restrict the mating side's candidate moves, to reduce overall tree size
    if (board.sideToMove() != mating_side)
      return true;
    std::string uci = uci::moveToUci(move);
    if (std::find(excludeMoves.begin(), excludeMoves.end(), uci) !=
        excludeMoves.end())
      return false;
    if (std::find(excludeSANs.begin(), excludeSANs.end(),
                  uci::moveToSan(board, move)) != excludeSANs.end())
      return false;
    if (!BBrestrictTo.empty() &&
        !(BBrestrictTo & Bitboard::fromSquare(move.to())))
      return false;
    if (BBexcludeFrom & Bitboard::fromSquare(move.from()))
      return false;
    if (BBexcludeTo & Bitboard::fromSquare(move.to()))
      return false;
    if (excludeCaptures) {
      if (board.isCapture(move))
        return false;
    } else if (!excludeCapturesOf.empty()) {
      if (board.isCapture(move) &&
          excludeCapturesOf.find(tolower(board.at(move.to()))) !=
              std::string::npos)
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
        if ((excludeToCapturable && board.isCapture(m) &&
             m.to() == move.to()) ||
            (excludeAllowingCapture && board.isCapture(m)) ||
            (BBexcludeAllowingFrom & Bitboard::fromSquare(m.from())) ||
            (BBexcludeAllowingTo & Bitboard::fromSquare(m.to())) ||
            (std::find(excludeAllowingMoves.begin(), excludeAllowingMoves.end(),
                       uci::moveToUci(m)) != excludeAllowingMoves.end()) ||
            (std::find(excludeAllowingSANs.begin(), excludeAllowingSANs.end(),
                       uci::moveToSan(board, m)) !=
             excludeAllowingSANs.end())) {
          board.unmakeMove(move);
          return false;
        }
      }
      board.unmakeMove(move);
    }
    return true;
  }

  virtual void initialize_tb() = 0;
  virtual void connect_children() = 0;
  virtual void generate_tb() = 0;

  score_t probe_tb(const std::string &fen) {
    auto it = fen2index.find(Board::Compact::encode(fen));
    if (it != fen2index.end())
      return tb[it->second].first;
    return VALUE_NONE;
  }

  std::vector<std::string> obtain_pv(Board board) {
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

public:
  MateTbBase(const Options &options) {
    std::vector<std::string> parts = split(options.epdStr);
    if (parts.size() < 4) {
      std::cout << "EPD \"" << options.epdStr << "\" is too short."
                << std::endl;
      std::exit(1);
    }
    root_pos = join(parts.begin(), parts.begin() + 4);
    max_depth = options.depth;
    mating_side = (parts[1] == "b" ? Color::BLACK : Color::WHITE);
    mating_side_to_move = true;
    for (size_t i = 4; i < parts.size() - 1; ++i)
      if (parts[i] == "bm" && parts[i + 1].find("#-") != std::string::npos) {
        mating_side = !mating_side;
        mating_side_to_move = false;
        break;
      }
    std::cout << "Restrict moves for "
              << (mating_side == Color::WHITE ? "WHITE" : "BLACK") << " side."
              << std::endl;
    excludeSANs = split(options.excludeSANs);
    excludeMoves = split(options.excludeMoves);
    for (const std::string &sq : split(options.restrictTo))
      BBrestrictTo |= Bitboard::fromSquare(Square(sq));
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

  void create_tb() {
    initialize_tb();
    connect_children();
    generate_tb();
  }

  void output() {
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
    auto pv_str = join(sp[0].second.begin(), sp[0].second.end());
    if (score != VALUE_NONE && score != 0) {
      std::cout << "\nMatetrack:" << std::endl;
      std::cout << root_pos << " bm #" << score2mate(score)
                << "; PV: " << pv_str << ";" << std::endl;
    } else
      std::cout << "No mate found." << std::endl;
    if (verbose == 0)
      return;
    std::cout << "\nMultiPV:" << std::endl;
    for (size_t count = 0; count < sp.size(); ++count) {
      score_t score = sp[count].first;
      if (score == VALUE_NONE || (score < 0 && mating_side_to_move)) {
        std::cout << "multipv " << count + 1 << " score None" << std::endl;
        continue;
      }
      std::string score_str = "cp " + std::to_string(score);
      pv_str = join(sp[count].second.begin(), sp[count].second.end());
      if (score != 0)
        score_str += " mate " + std::to_string(score2mate(score));
      std::cout << "multipv " << count + 1 << " score " << score_str << " pv "
                << pv_str << std::endl;
      if (verbose >= 2) {
        std::cout << cdb_link(root_pos, pv_str) << "\n";
        if (score != 0) {
          auto child_pv_str =
              join(sp[count].second.begin() + 1, sp[count].second.end());
          std::cout << "Child FEN: ";
          auto move = uci::uciToMove(board, sp[count].second[0]);
          board.makeMove<true>(move);
          std::cout << board.getFen(false) << " bm #"
                    << score2mate(-score + (score < 0 ? 1 : -1))
                    << "; PV: " << child_pv_str << ";\n";
          board.unmakeMove(move);
        }
        std::cout << "\n";
      }
    }
  }

  void write_tb(const std::string &filename) {
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
};
