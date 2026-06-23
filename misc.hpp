#ifndef MISC_H_INCLUDED
#define MISC_H_INCLUDED

#include <limits>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using score_t = std::int16_t;
using index_t = std::uint32_t;
using book_t = std::map<std::string, std::string>;

constexpr score_t VALUE_NONE = 30001;
constexpr score_t VALUE_MATE = 30000;
constexpr int MAX_DEPTH = std::numeric_limits<int>::max() - 1;

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

#endif
