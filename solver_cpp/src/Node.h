#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace solver_cpp {

constexpr int INPUT_NODE = 1;
constexpr int INTER_NODE = 2;
constexpr int OUTPUT_NODE = 3;

using HashValue = std::array<std::int64_t, 4>;

constexpr HashValue HASH_PRIMES = {
  2147483647LL,
  2147483629LL,
  2147483587LL,
  2147483579LL,
};

struct HashValueHasher {
  std::size_t operator()(const HashValue& value) const noexcept {
    std::size_t seed = 1469598103934665603ULL;
    for (auto item : value) {
      seed ^= static_cast<std::size_t>(item);
      seed *= 1099511628211ULL;
    }
    return seed;
  }
};

inline std::string hash_to_string(const HashValue& value) {
  std::ostringstream out;
  out << value[0] << "," << value[1] << "," << value[2] << "," << value[3];
  return out.str();
}

inline bool vector_contains(const std::vector<int>& values, int target) {
  return std::find(values.begin(), values.end(), target) != values.end();
}

inline void erase_one(std::vector<int>& values, int target) {
  auto it = std::find(values.begin(), values.end(), target);
  if (it == values.end()) {
    throw std::runtime_error("edge endpoint not found");
  }
  values.erase(it);
}

inline std::pair<int, int> node_name_key(const std::string& name) {
  if (name.empty()) {
    return {3, 0};
  }

  int prefix_order = 3;
  if (name[0] == 'x') {
    prefix_order = 0;
  } else if (name[0] == 'w') {
    prefix_order = 1;
  } else if (name[0] == 'y') {
    prefix_order = 2;
  }

  int number = 0;
  if (name.size() > 1) {
    try {
      number = std::stoi(name.substr(1));
    } catch (...) {
      number = 0;
    }
  }
  return {prefix_order, number};
}

struct Node {
  int type = INTER_NODE;
  std::string name;
  std::vector<int> pre;
  std::vector<int> succ;
};

} // namespace solver_cpp
