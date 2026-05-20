#include <algorithm>
#include <atomic>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

struct Graph {
  int n = 0;
  int m = 0;
  int c = 0;
  std::vector<std::vector<int>> pre;
  std::string source;
  int source_index = 0;

  int total_nodes() const { return n + c; }
  std::vector<int> input_indices() const {
    std::vector<int> ret(n);
    std::iota(ret.begin(), ret.end(), 0);
    return ret;
  }
  std::vector<int> output_indices() const {
    std::vector<int> ret(m);
    std::iota(ret.begin(), ret.end(), total_nodes() - m);
    return ret;
  }
  std::vector<int> gate_indices() const {
    std::vector<int> ret(c);
    std::iota(ret.begin(), ret.end(), n);
    return ret;
  }
  std::vector<int> internal_indices() const {
    std::vector<int> ret(std::max(0, c - m));
    std::iota(ret.begin(), ret.end(), n);
    return ret;
  }
};

struct Options {
  std::map<std::string, std::string> values;
  std::set<std::string> flags;

  bool has_flag(const std::string& key) const { return flags.count(key) != 0; }
  std::string get(const std::string& key, const std::string& fallback) const {
    auto it = values.find(key);
    return it == values.end() ? fallback : it->second;
  }
  int get_int(const std::string& key, int fallback) const {
    auto it = values.find(key);
    return it == values.end() ? fallback : std::stoi(it->second);
  }
  std::optional<int> get_optional_int(const std::string& key) const {
    auto it = values.find(key);
    if (it == values.end()) return std::nullopt;
    return std::stoi(it->second);
  }
};

Options parse_options(int argc, char** argv, int start) {
  Options options;
  for (int i = start; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.rfind("--", 0) != 0) {
      throw std::runtime_error("unexpected positional argument: " + arg);
    }
    std::string key = arg.substr(2);
    if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
      options.values[key] = argv[++i];
    } else {
      options.flags.insert(key);
    }
  }
  return options;
}

fs::path infer_graph_root(const char* argv0) {
  std::vector<fs::path> candidates;
  fs::path exe_path(argv0);
  if (exe_path.has_parent_path()) {
    fs::path exe_dir = exe_path.parent_path();
    candidates.push_back(exe_dir / "graph_data");
    candidates.push_back(exe_dir / ".." / "graph_data");
  }
  candidates.push_back("graph_data");
  candidates.push_back("../graph_data");
  for (const auto& candidate : candidates) {
    if (fs::exists(candidate)) return candidate;
  }
  return "graph_data";
}

std::string trim(std::string value) {
  auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

std::vector<int> read_bits(const std::vector<unsigned char>& record, int width, int count) {
  std::vector<int> values;
  values.reserve(count);
  int bit_pos = 0;
  for (int i = 0; i < count; ++i) {
    int value = 0;
    for (int bit = 0; bit < width; ++bit) {
      int byte_index = bit_pos / 8;
      int shift = 7 - (bit_pos % 8);
      value = (value << 1) | ((record[byte_index] >> shift) & 1);
      ++bit_pos;
    }
    values.push_back(value);
  }
  return values;
}

void validate_graph(const Graph& graph) {
  if (static_cast<int>(graph.pre.size()) != graph.total_nodes()) {
    throw std::runtime_error(graph.source + ": node count mismatch");
  }
  for (int i = 0; i < graph.n; ++i) {
    if (!graph.pre[i].empty()) throw std::runtime_error(graph.source + ": input has predecessor");
  }
  for (int i = graph.n; i < graph.total_nodes(); ++i) {
    if (graph.pre[i].size() != 2) throw std::runtime_error(graph.source + ": non-input indegree is not 2");
    if (graph.pre[i][0] == graph.pre[i][1]) throw std::runtime_error(graph.source + ": duplicate predecessor");
    for (int parent : graph.pre[i]) {
      if (parent < 0 || parent >= graph.total_nodes()) {
        throw std::runtime_error(graph.source + ": predecessor out of range");
      }
    }
  }
}

std::tuple<int, int, int> parse_solver_triplet(const fs::path& path) {
  std::regex re(R"((\d+)_(\d+)_(\d+))");
  std::smatch match;
  std::string name = path.parent_path().filename().string();
  if (!std::regex_match(name, match, re)) {
    name = path.stem().string();
  }
  if (!std::regex_match(name, match, re)) {
    throw std::runtime_error("cannot infer n,m,c from " + path.string());
  }
  return {std::stoi(match[1]), std::stoi(match[2]), std::stoi(match[3])};
}

Graph graph_from_values(int n, int m, int c, const std::vector<int>& values, const fs::path& path, int source_index) {
  Graph graph;
  graph.n = n;
  graph.m = m;
  graph.c = c;
  graph.source = path.string();
  graph.source_index = source_index;
  graph.pre.assign(n + c, {});
  int offset = 0;
  for (int index = n; index < n + c; ++index) {
    graph.pre[index] = {values[offset], values[offset + 1]};
    offset += 2;
  }
  validate_graph(graph);
  return graph;
}

std::vector<Graph> read_solver_graphs(const fs::path& path, std::optional<int> limit = std::nullopt) {
  auto [n, m, c] = parse_solver_triplet(path);
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open " + path.string());
  std::size_t record_size = (10ULL * static_cast<std::size_t>(c) + 7) / 8;
  std::vector<Graph> graphs;
  int index = 0;
  while (!limit || static_cast<int>(graphs.size()) < *limit) {
    std::vector<unsigned char> record(record_size);
    in.read(reinterpret_cast<char*>(record.data()), static_cast<std::streamsize>(record.size()));
    if (in.gcount() == 0) break;
    if (static_cast<std::size_t>(in.gcount()) != record_size) throw std::runtime_error(path.string() + ": partial record");
    ++index;
    graphs.push_back(graph_from_values(n, m, c, read_bits(record, 5, 2 * c), path, index));
  }
  return graphs;
}

std::vector<Graph> read_fcbg_graphs(const fs::path& path, std::optional<int> limit = std::nullopt) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open " + path.string());
  char magic[4];
  in.read(magic, 4);
  if (std::string(magic, magic + 4) != "FCBG") throw std::runtime_error(path.string() + ": not FCBG");
  unsigned char n_raw = 0, m_raw = 0, width = 0;
  unsigned char c_hi = 0, c_lo = 0;
  in.read(reinterpret_cast<char*>(&n_raw), 1);
  in.read(reinterpret_cast<char*>(&m_raw), 1);
  in.read(reinterpret_cast<char*>(&c_hi), 1);
  in.read(reinterpret_cast<char*>(&c_lo), 1);
  in.read(reinterpret_cast<char*>(&width), 1);
  int n = n_raw;
  int m = m_raw;
  int c = (static_cast<int>(c_hi) << 8) | c_lo;
  std::size_t record_size = (2ULL * static_cast<std::size_t>(c) * width + 7) / 8;
  std::vector<Graph> graphs;
  int index = 0;
  while (!limit || static_cast<int>(graphs.size()) < *limit) {
    std::vector<unsigned char> record(record_size);
    in.read(reinterpret_cast<char*>(record.data()), static_cast<std::streamsize>(record.size()));
    if (in.gcount() == 0) break;
    if (static_cast<std::size_t>(in.gcount()) != record_size) throw std::runtime_error(path.string() + ": partial record");
    ++index;
    graphs.push_back(graph_from_values(n, m, c, read_bits(record, width, 2 * c), path, index));
  }
  return graphs;
}

std::vector<Graph> read_graphs_auto(const fs::path& path, std::optional<int> limit = std::nullopt) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open " + path.string());
  char magic[4] = {0, 0, 0, 0};
  in.read(magic, 4);
  if (std::string(magic, magic + 4) == "FCBG") return read_fcbg_graphs(path, limit);
  return read_solver_graphs(path, limit);
}

unsigned int default_thread_count() {
  unsigned int count = std::thread::hardware_concurrency();
  return count == 0 ? 1 : count;
}

std::vector<std::vector<int>> combinations(const std::vector<int>& items, int size) {
  std::vector<std::vector<int>> ret;
  std::vector<int> current;
  auto rec = [&](auto&& self, int offset, int need) -> void {
    if (need == 0) {
      ret.push_back(current);
      return;
    }
    for (int i = offset; i <= static_cast<int>(items.size()) - need; ++i) {
      current.push_back(items[i]);
      self(self, i + 1, need - 1);
      current.pop_back();
    }
  };
  rec(rec, 0, size);
  return ret;
}

using Matrix = std::vector<std::uint64_t>;

Matrix identity_matrix(int k) {
  Matrix matrix(k);
  for (int i = 0; i < k; ++i) matrix[i] = 1ULL << i;
  return matrix;
}

Matrix zero_matrix(int k) {
  return Matrix(k, 0);
}

Matrix matrix_add(const Matrix& left, const Matrix& right) {
  Matrix result(left.size());
  for (std::size_t i = 0; i < left.size(); ++i) result[i] = left[i] ^ right[i];
  return result;
}

Matrix matrix_mul(const Matrix& left, const Matrix& right) {
  Matrix rows;
  rows.reserve(left.size());
  for (std::uint64_t row : left) {
    std::uint64_t value = 0;
    while (row) {
      int bit = __builtin_ctzll(row);
      value ^= right[bit];
      row &= row - 1;
    }
    rows.push_back(value);
  }
  return rows;
}

int direct_xor_cost(const Matrix& matrix) {
  int total = 0;
  for (auto row : matrix) total += std::max(0, __builtin_popcountll(row) - 1);
  return total;
}

Matrix supports_to_matrix(int k, const std::vector<std::vector<int>>& supports) {
  if (static_cast<int>(supports.size()) != k) throw std::runtime_error("bad L support row count");
  Matrix matrix;
  matrix.reserve(k);
  for (const auto& support : supports) {
    std::uint64_t row = 0;
    for (int column : support) {
      if (column < 1 || column > k) throw std::runtime_error("L support column out of range");
      row |= 1ULL << (column - 1);
    }
    matrix.push_back(row);
  }
  return matrix;
}

Matrix matrix_inverse(const Matrix& matrix) {
  int k = static_cast<int>(matrix.size());
  std::vector<std::pair<std::uint64_t, std::uint64_t>> work;
  for (int row = 0; row < k; ++row) work.push_back({matrix[row], 1ULL << row});
  for (int col = 0; col < k; ++col) {
    int pivot = -1;
    for (int row = col; row < k; ++row) {
      if (work[row].first & (1ULL << col)) {
        pivot = row;
        break;
      }
    }
    if (pivot < 0) throw std::runtime_error("matrix is not invertible");
    if (pivot != col) std::swap(work[pivot], work[col]);
    for (int row = 0; row < k; ++row) {
      if (row != col && (work[row].first & (1ULL << col))) {
        work[row].first ^= work[col].first;
        work[row].second ^= work[col].second;
      }
    }
  }
  Matrix inverse;
  for (auto [left, right] : work) {
    (void)left;
    inverse.push_back(right);
  }
  return inverse;
}

struct MatrixPowerRing {
  int k = 0;
  Matrix l_matrix;
  Matrix l_inverse;
  Matrix zero;
  Matrix one;
  std::map<int, Matrix> power_cache;
  std::map<int, int> cost_cache;

  MatrixPowerRing(int k_, const std::vector<std::vector<int>>& supports) : k(k_) {
    l_matrix = supports_to_matrix(k, supports);
    l_inverse = matrix_inverse(l_matrix);
    zero = zero_matrix(k);
    one = identity_matrix(k);
    power_cache[0] = one;
  }

  Matrix power(int exponent) {
    auto cached = power_cache.find(exponent);
    if (cached != power_cache.end()) return cached->second;
    Matrix base = exponent > 0 ? l_matrix : l_inverse;
    int value = std::abs(exponent);
    Matrix result = one;
    while (value) {
      if (value & 1) result = matrix_mul(result, base);
      base = matrix_mul(base, base);
      value >>= 1;
    }
    power_cache[exponent] = result;
    return result;
  }

  int symmetric_xor_cost(const Matrix& matrix) {
    return std::min(direct_xor_cost(matrix), direct_xor_cost(matrix_inverse(matrix)));
  }

  int multiplier_xor_cost(int exponent) {
    auto cached = cost_cache.find(exponent);
    if (cached != cost_cache.end()) return cached->second;
    int direct = symmetric_xor_cost(power(exponent));
    int cost = direct;
    if (exponent != 0) {
      Matrix step = exponent > 0 ? l_matrix : l_inverse;
      cost = std::min(direct, std::abs(exponent) * symmetric_xor_cost(step));
    }
    cost_cache[exponent] = cost;
    return cost;
  }
};

struct BinaryWitness {
  int t = 0;
  int k = 0;
  int graph_index = 0;
  int total_cost = 0;
  int base_cost = 0;
  int extra_cost = 0;
  int seed = 0;
  std::vector<std::pair<int, int>> coeffs;
  std::vector<std::vector<int>> supports;
};

std::vector<std::pair<int, int>> parse_int_pairs(const std::string& text) {
  std::vector<std::pair<int, int>> pairs;
  std::regex pair_re(R"(\((-?\d+),\s*(-?\d+)\))");
  for (std::sregex_iterator it(text.begin(), text.end(), pair_re), end; it != end; ++it) {
    pairs.push_back({std::stoi((*it)[1]), std::stoi((*it)[2])});
  }
  return pairs;
}

std::vector<std::vector<Matrix>> binary_transfer_matrix(const Graph& graph, const BinaryWitness& witness, MatrixPowerRing& ring) {
  std::map<int, std::pair<int, int>> coeff_by_node;
  for (int i = 0; i < graph.c; ++i) coeff_by_node[graph.n + i] = witness.coeffs[i];
  std::map<int, int> input_position;
  for (int i = 0; i < graph.n; ++i) input_position[i] = i;
  std::map<int, std::vector<Matrix>> memo;
  std::set<int> visiting;
  auto vec = [&](auto&& self, int node) -> std::vector<Matrix> {
    auto cached = memo.find(node);
    if (cached != memo.end()) return cached->second;
    if (input_position.count(node)) {
      std::vector<Matrix> basis(graph.n, ring.zero);
      basis[input_position[node]] = ring.one;
      memo[node] = basis;
      return basis;
    }
    if (visiting.count(node)) throw std::runtime_error("cycle in binary transfer");
    visiting.insert(node);
    int left = graph.pre[node][0];
    int right = graph.pre[node][1];
    auto [left_exp, right_exp] = coeff_by_node[node];
    Matrix left_coeff = ring.power(left_exp);
    Matrix right_coeff = ring.power(right_exp);
    auto left_vector = self(self, left);
    auto right_vector = self(self, right);
    std::vector<Matrix> result;
    for (int i = 0; i < graph.n; ++i) {
      result.push_back(matrix_add(matrix_mul(left_coeff, left_vector[i]), matrix_mul(right_coeff, right_vector[i])));
    }
    visiting.erase(node);
    memo[node] = result;
    return result;
  };
  std::vector<std::vector<Matrix>> matrix;
  for (int output : graph.output_indices()) matrix.push_back(vec(vec, output));
  return matrix;
}

int rank_packed(std::vector<std::vector<std::uint64_t>> rows, int column_count) {
  std::map<int, std::vector<std::uint64_t>> basis;
  int rank = 0;
  for (auto row : rows) {
    while (true) {
      int pivot = -1;
      for (int chunk = static_cast<int>(row.size()) - 1; chunk >= 0; --chunk) {
        if (row[chunk]) {
          pivot = chunk * 64 + 63 - __builtin_clzll(row[chunk]);
          break;
        }
      }
      if (pivot < 0) break;
      if (pivot >= column_count) throw std::runtime_error("rank pivot outside column count");
      auto it = basis.find(pivot);
      if (it == basis.end()) {
        basis[pivot] = row;
        ++rank;
        break;
      }
      for (std::size_t i = 0; i < row.size(); ++i) row[i] ^= it->second[i];
    }
  }
  return rank;
}

void xor_shifted_block(std::vector<std::uint64_t>& row, std::uint64_t block, int offset) {
  int chunk = offset / 64;
  int shift = offset % 64;
  row[chunk] ^= block << shift;
  if (shift != 0 && chunk + 1 < static_cast<int>(row.size())) row[chunk + 1] ^= block >> (64 - shift);
}

std::optional<std::pair<std::vector<int>, std::vector<int>>> first_singular_binary_minor(
  const std::vector<std::vector<Matrix>>& matrix,
  int k
) {
  int t = static_cast<int>(matrix.size());
  std::vector<int> all(t);
  std::iota(all.begin(), all.end(), 0);
  for (int size = 1; size <= t; ++size) {
    auto row_sets = combinations(all, size);
    auto col_sets = combinations(all, size);
    int column_count = size * k;
    int chunks = (column_count + 63) / 64;
    for (const auto& rows : row_sets) {
      for (const auto& cols : col_sets) {
        std::vector<std::vector<std::uint64_t>> binary_rows;
        for (int row_index : rows) {
          for (int bit_row = 0; bit_row < k; ++bit_row) {
            std::vector<std::uint64_t> packed(chunks, 0);
            for (int block_col = 0; block_col < size; ++block_col) {
              std::uint64_t block = matrix[row_index][cols[block_col]][bit_row];
              xor_shifted_block(packed, block, block_col * k);
            }
            binary_rows.push_back(std::move(packed));
          }
        }
        if (rank_packed(binary_rows, column_count) != column_count) return std::make_pair(rows, cols);
      }
    }
  }
  return std::nullopt;
}

std::tuple<int, int, int> binary_cost(const BinaryWitness& witness, const Graph& graph, MatrixPowerRing& ring) {
  int extra = 0;
  for (auto [left, right] : witness.coeffs) extra += ring.multiplier_xor_cost(left) + ring.multiplier_xor_cost(right);
  int base = graph.c * witness.k;
  return {base + extra, base, extra};
}

std::map<int, int> default_graph_sizes() {
  return {{4, 8}, {5, 12}, {6, 16}, {7, 21}, {8, 26}};
}

fs::path graph_path(const fs::path& root, int t, std::optional<int> c = std::nullopt) {
  auto sizes = default_graph_sizes();
  int graph_c = c.value_or(sizes.at(t));
  fs::path fcbg_path = root / (std::to_string(t) + "-" + std::to_string(t) + "-" + std::to_string(graph_c) + ".dat");
  if (fs::exists(fcbg_path)) return fcbg_path;
  fs::path solver_path = root / (std::to_string(t) + "_" + std::to_string(t) + "_" + std::to_string(graph_c) + ".bin");
  if (fs::exists(solver_path)) return solver_path;
  return fcbg_path;
}

std::vector<std::vector<int>> cyclic_base_supports(int k) {
  std::vector<std::vector<int>> supports(k);
  supports[0].push_back(k);
  for (int row = 1; row < k; ++row) supports[row].push_back(row);
  return supports;
}

int parse_int_auto_base(const std::string& raw) {
  std::size_t consumed = 0;
  int value = std::stoi(raw, &consumed, 0);
  if (consumed != raw.size()) throw std::runtime_error("bad integer: " + raw);
  return value;
}

std::vector<std::vector<int>> finite_field_l_supports(int k, int polynomial) {
  if (!(polynomial & (1 << k))) {
    throw std::runtime_error("field polynomial must have degree k");
  }
  if (!(polynomial & 1)) {
    throw std::runtime_error("field polynomial must have constant term 1");
  }
  if (k == 4 && polynomial == 0x13) {
    return {{4}, {1, 4}, {2}, {3}};
  }
  if (k == 8 && polynomial == 0x187) {
    return {{8}, {1, 2}, {2, 8}, {3}, {4}, {5}, {6}, {7}};
  }
  if (k == 8 && polynomial == 0x1c3) {
    return {{8}, {1, 3}, {1, 2, 3}, {3}, {4}, {5}, {6}, {7}};
  }

  // Fallback for simple trinomial shift-form choices. The known field
  // polynomials above use the paper's companion convention explicitly.
  auto supports = cyclic_base_supports(k);
  for (int bit = 1; bit < k; ++bit) {
    if (polynomial & (1 << bit)) supports[bit].push_back(k);
  }
  return supports;
}

std::vector<std::vector<int>> random_cyclic_l_supports(
  int k,
  int extra_min,
  int extra_max,
  std::mt19937_64& rng
) {
  auto supports = cyclic_base_supports(k);
  std::vector<std::pair<int, int>> remaining;
  for (int row = 0; row < k; ++row) {
    for (int col = 1; col <= k; ++col) {
      if (std::find(supports[row].begin(), supports[row].end(), col) == supports[row].end()) {
        remaining.push_back({row, col});
      }
    }
  }
  if (remaining.empty()) return supports;
  extra_min = std::max(0, extra_min);
  extra_max = std::max(extra_min, extra_max);
  extra_max = std::min(extra_max, static_cast<int>(remaining.size()));
  std::uniform_int_distribution<int> count_dist(extra_min, extra_max);
  int extra_count = count_dist(rng);
  std::shuffle(remaining.begin(), remaining.end(), rng);
  for (int i = 0; i < extra_count; ++i) {
    supports[remaining[i].first].push_back(remaining[i].second);
  }
  for (auto& row : supports) std::sort(row.begin(), row.end());
  return supports;
}

std::uint64_t reduced_cyclic_l_count(int k) {
  if (k <= 1) return 0;
  std::uint64_t choices = static_cast<std::uint64_t>(k - 1);
  return choices + choices * choices * choices;
}

int reduced_l_remaining_row_col(int k, int row, std::uint64_t choice) {
  int col = static_cast<int>(choice) + 1;
  if (col >= row) ++col;
  if (col < 1 || col > k) throw std::runtime_error("bad reduced-L column choice");
  return col;
}

std::vector<std::vector<int>> reduced_cyclic_l_supports_at(
  int k,
  std::uint64_t index
) {
  auto supports = cyclic_base_supports(k);
  std::uint64_t choices = static_cast<std::uint64_t>(k - 1);
  std::uint64_t candidate_count = reduced_cyclic_l_count(k);
  if (index >= candidate_count) throw std::runtime_error("bad reduced-L candidate index");

  if (index < choices) {
    supports[0].push_back(static_cast<int>(index) + 1);
  } else {
    std::uint64_t offset = index - choices;
    std::uint64_t first_choice = offset / (choices * choices);
    std::uint64_t second_choice = offset % (choices * choices);
    int row = 1 + static_cast<int>(second_choice / choices);
    int col = reduced_l_remaining_row_col(k, row, second_choice % choices);
    supports[0].push_back(static_cast<int>(first_choice) + 1);
    supports[row].push_back(col);
  }

  for (auto& row : supports) std::sort(row.begin(), row.end());
  return supports;
}

std::vector<std::vector<int>> random_stage1_l_supports(
  int k,
  bool reduced_l,
  int extra_min,
  int extra_max,
  std::mt19937_64& rng
) {
  if (reduced_l) {
    std::uint64_t count = reduced_cyclic_l_count(k);
    if (count == 0) throw std::runtime_error("no reduced-L candidates for k=" + std::to_string(k));
    std::uniform_int_distribution<std::uint64_t> dist(0, count - 1);
    return reduced_cyclic_l_supports_at(k, dist(rng));
  }
  return random_cyclic_l_supports(k, extra_min, extra_max, rng);
}

std::string format_supports(const std::vector<std::vector<int>>& supports) {
  std::ostringstream out;
  out << "[";
  for (std::size_t i = 0; i < supports.size(); ++i) {
    if (i) out << ", ";
    out << "[";
    for (std::size_t j = 0; j < supports[i].size(); ++j) {
      if (j) out << ", ";
      out << supports[i][j];
    }
    out << "]";
  }
  out << "]";
  return out.str();
}

std::string format_coeffs(const std::vector<std::pair<int, int>>& coeffs) {
  std::ostringstream out;
  out << "[";
  for (std::size_t i = 0; i < coeffs.size(); ++i) {
    if (i) out << ", ";
    out << "(" << coeffs[i].first << ", " << coeffs[i].second << ")";
  }
  out << "]";
  return out.str();
}

int coeff_abs_sum(const std::vector<std::pair<int, int>>& coeffs) {
  int total = 0;
  for (auto [left, right] : coeffs) total += std::abs(left) + std::abs(right);
  return total;
}

std::vector<std::pair<int, int>> random_coeffs(int count, const std::vector<int>& values, std::mt19937_64& rng) {
  std::uniform_int_distribution<int> dist(0, static_cast<int>(values.size()) - 1);
  std::vector<std::pair<int, int>> coeffs;
  for (int i = 0; i < count; ++i) coeffs.push_back({values[dist(rng)], values[dist(rng)]});
  return coeffs;
}

class AbsSumBudgetSampler {
public:
  AbsSumBudgetSampler(int slot_count, const std::vector<int>& values, int budget)
    : slot_count_(slot_count), values_(values), budget_(budget) {
    if (slot_count_ < 0 || budget_ < 0 || values_.empty()) return;
    counts_.assign(
      static_cast<std::size_t>(slot_count_ + 1) * static_cast<std::size_t>(budget_ + 1),
      0.0L
    );
    for (int remaining_budget = 0; remaining_budget <= budget_; ++remaining_budget) {
      at(0, remaining_budget) = 1.0L;
    }
    for (int slots = 1; slots <= slot_count_; ++slots) {
      for (int remaining_budget = 0; remaining_budget <= budget_; ++remaining_budget) {
        long double total = 0.0L;
        for (int value : values_) {
          int cost = std::abs(value);
          if (cost <= remaining_budget) {
            total += at(slots - 1, remaining_budget - cost);
          }
        }
        at(slots, remaining_budget) = total;
      }
    }
  }

  bool feasible() const {
    return budget_ >= 0 && !counts_.empty() && at(slot_count_, budget_) > 0.0L;
  }

  bool sample(std::mt19937_64& rng, std::vector<std::pair<int, int>>& coeffs, int& sum) const {
    if (!feasible()) return false;
    std::vector<int> slots;
    slots.reserve(slot_count_);
    int remaining_budget = budget_;
    sum = 0;
    for (int position = 0; position < slot_count_; ++position) {
      int remaining_slots = slot_count_ - position - 1;
      long double total_weight = 0.0L;
      for (int value : values_) {
        int cost = std::abs(value);
        if (cost <= remaining_budget) {
          total_weight += at(remaining_slots, remaining_budget - cost);
        }
      }
      if (total_weight <= 0.0L) return false;
      std::uniform_real_distribution<long double> pick_dist(0.0L, total_weight);
      long double pick = pick_dist(rng);
      long double cumulative = 0.0L;
      int chosen = -1;
      for (int index = 0; index < static_cast<int>(values_.size()); ++index) {
        int cost = std::abs(values_[index]);
        if (cost > remaining_budget) continue;
        cumulative += at(remaining_slots, remaining_budget - cost);
        if (pick <= cumulative) {
          chosen = index;
          break;
        }
      }
      if (chosen < 0) {
        for (int index = static_cast<int>(values_.size()) - 1; index >= 0; --index) {
          if (std::abs(values_[index]) <= remaining_budget) {
            chosen = index;
            break;
          }
        }
      }
      if (chosen < 0) return false;
      int value = values_[chosen];
      slots.push_back(value);
      int cost = std::abs(value);
      sum += cost;
      remaining_budget -= cost;
    }
    coeffs.clear();
    coeffs.reserve(static_cast<std::size_t>(slot_count_ / 2));
    for (int index = 0; index < slot_count_; index += 2) {
      coeffs.push_back({slots[index], slots[index + 1]});
    }
    return true;
  }

private:
  long double& at(int slots, int budget) {
    return counts_[static_cast<std::size_t>(slots) * static_cast<std::size_t>(budget_ + 1) +
                   static_cast<std::size_t>(budget)];
  }

  long double at(int slots, int budget) const {
    return counts_[static_cast<std::size_t>(slots) * static_cast<std::size_t>(budget_ + 1) +
                   static_cast<std::size_t>(budget)];
  }

  int slot_count_ = 0;
  const std::vector<int>& values_;
  int budget_ = -1;
  std::vector<long double> counts_;
};

std::vector<int> parse_int_bag(std::string raw) {
  std::replace(raw.begin(), raw.end(), ',', ' ');
  std::stringstream ss(raw);
  std::vector<int> values;
  int value = 0;
  while (ss >> value) values.push_back(value);
  return values;
}

std::vector<int> weighted_signed_exponent_bag(int value_limit) {
  std::vector<int> values;
  for (int abs_value = 0; abs_value <= value_limit; ++abs_value) {
    int weight = value_limit + 1 - abs_value;
    for (int repeat = 0; repeat < weight; ++repeat) {
      if (abs_value == 0) {
        values.push_back(0);
      } else {
        values.push_back(-abs_value);
        values.push_back(abs_value);
      }
    }
  }
  return values;
}

std::string format_binary_witness_line(const BinaryWitness& witness) {
  std::ostringstream out;
  out << "best (" << witness.t << "," << witness.k << "," << witness.graph_index << ") "
      << "tot=" << witness.total_cost << "(" << witness.base_cost << "+" << witness.extra_cost
      << ") seed=" << witness.seed << " coeffs=" << format_coeffs(witness.coeffs)
      << " L=" << format_supports(witness.supports);
  return out.str();
}

bool binary_witness_is_mds(
  const BinaryWitness& witness,
  const Graph& graph,
  MatrixPowerRing& ring
) {
  auto matrix = binary_transfer_matrix(graph, witness, ring);
  return !first_singular_binary_minor(matrix, witness.k);
}

std::vector<std::pair<int, int>> greedy_binary_descent(
  BinaryWitness witness,
  const Graph& graph,
  MatrixPowerRing& ring,
  const std::vector<int>& exponents
) {
  std::vector<std::pair<int, int>> ordered_pairs;
  std::map<std::pair<int, int>, int> pair_cost;
  for (int left : exponents) {
    for (int right : exponents) {
      std::pair<int, int> pair{left, right};
      ordered_pairs.push_back(pair);
      pair_cost[pair] = ring.multiplier_xor_cost(left) + ring.multiplier_xor_cost(right);
    }
  }
  std::sort(ordered_pairs.begin(), ordered_pairs.end(), [&](const auto& lhs, const auto& rhs) {
    return std::make_tuple(pair_cost[lhs], std::abs(lhs.first) + std::abs(lhs.second), lhs)
      < std::make_tuple(pair_cost[rhs], std::abs(rhs.first) + std::abs(rhs.second), rhs);
  });

  bool improved = true;
  while (improved) {
    improved = false;
    for (std::size_t index = 0; index < witness.coeffs.size(); ++index) {
      int old_cost = pair_cost[witness.coeffs[index]];
      for (const auto& new_pair : ordered_pairs) {
        if (pair_cost[new_pair] >= old_cost) break;
        BinaryWitness trial = witness;
        trial.coeffs[index] = new_pair;
        auto [total, base, extra] = binary_cost(trial, graph, ring);
        trial.total_cost = total;
        trial.base_cost = base;
        trial.extra_cost = extra;
        if (binary_witness_is_mds(trial, graph, ring)) {
          witness = trial;
          improved = true;
          break;
        }
      }
      if (improved) break;
    }
  }
  return witness.coeffs;
}

std::vector<int> binary_target_ks(const Options& options, int base_k) {
  static const std::vector<int> standard_ks = {4, 8, 16, 32, 64};
  auto max_k = options.get_optional_int("max-k");
  int explicit_k = options.get_int("k", -1);
  if (max_k) {
    if (*max_k < base_k) throw std::runtime_error("--max-k must be at least --base-k");
    std::vector<int> targets;
    for (int candidate : standard_ks) {
      if (candidate >= base_k && candidate <= *max_k) targets.push_back(candidate);
    }
    if (targets.empty()) throw std::runtime_error("no standard target k values selected by --max-k");
    return targets;
  }
  if (explicit_k < 1 || explicit_k > 64) throw std::runtime_error("--k must be in 1..64, or use --max-k");
  return {explicit_k};
}

int command_improve_binary(const Options& options) {
  int t = options.get_int("t", -1);
  int attempts = options.get_int("attempts", 10000);
  int seed = options.get_int("seed", 1);
  int requested_threads = options.get_int(
    "threads",
    static_cast<int>(default_thread_count())
  );
  unsigned int thread_count = static_cast<unsigned int>(std::max(1, requested_threads));
  unsigned int max_thread_count = thread_count;
  int value_limit = options.get_int("value-limit", 3);
  int base_k = options.get_int("base-k", 4);
  bool has_base_poly = options.values.count("base-poly");
  bool randomized_base_l = !has_base_poly && (base_k == 16 || base_k == 32 || base_k == 64);
  int base_poly = has_base_poly
    ? parse_int_auto_base(options.values.at("base-poly"))
    : (base_k == 4 ? 0x13 : 0x1c3);
  int stage1_attempts = options.get_int("stage1-attempts", attempts);
  int l_attempts = options.get_int("l-attempts", attempts);
  int l_extra_min = options.get_int("l-extra-min", 1);
  int l_extra_max = options.get_int("l-extra-max", 3);
  bool reduced_l = options.has_flag("reduced-L");
  bool manual_coeffs = options.values.count("coeffs");
  bool adaptive_sum_filter = options.has_flag("adaptive-sum-filter");
  int initial_sum_threshold = options.get_int("initial-sum-threshold", std::numeric_limits<int>::max());
  fs::path graph_root = options.get("graph-root", "graph_data");
  if (base_k != 4 && base_k != 8 && base_k != 16 && base_k != 32 && base_k != 64) {
    throw std::runtime_error("--base-k must be one of 4,8,16,32,64");
  }
  if (!has_base_poly && base_k != 4 && base_k != 8 && !randomized_base_l) {
    throw std::runtime_error("--base-k without --base-poly is only supported for 4,8,16,32,64");
  }
  if (l_extra_min < 0 || l_extra_max < l_extra_min) throw std::runtime_error("bad L extra-one range");
  if (stage1_attempts < 1) throw std::runtime_error("--stage1-attempts must be positive");
  if (initial_sum_threshold < 0) throw std::runtime_error("--initial-sum-threshold must be nonnegative");
  if (manual_coeffs && adaptive_sum_filter) {
    throw std::runtime_error("--adaptive-sum-filter cannot be combined with --coeffs");
  }
  if (manual_coeffs && randomized_base_l) {
    throw std::runtime_error("--coeffs with randomized stage-1 L is not supported; provide --base-poly or omit --coeffs");
  }
  if (manual_coeffs && options.has_flag("greedy-descent")) {
    throw std::runtime_error("--coeffs cannot be combined with --greedy-descent");
  }
  auto target_ks = binary_target_ks(options, base_k);
  if (options.values.count("graph-file") && options.values.count("graph-c")) {
    throw std::runtime_error("--graph-file and --graph-c cannot be combined");
  }
  std::cout << "target k values:";
  for (int target_k : target_ks) std::cout << " " << target_k;
  std::cout << "\n";
  fs::path graph_data_path = options.values.count("graph-file")
    ? fs::path(options.values.at("graph-file"))
    : graph_path(graph_root, t, options.get_optional_int("graph-c"));
  auto graphs = read_graphs_auto(graph_data_path);
  if (graphs.empty()) throw std::runtime_error("no graph templates found for t=" + std::to_string(t));
  std::cout << "graph file=" << graph_data_path.string()
            << " graphs=" << graphs.size()
            << " c=" << graphs.front().c << "\n";
  std::optional<int> fixed_graph_index = options.get_optional_int("graph-index");
  if (fixed_graph_index && (*fixed_graph_index < 1 || *fixed_graph_index > static_cast<int>(graphs.size()))) {
    throw std::runtime_error("--graph-index outside 1.." + std::to_string(graphs.size()));
  }
  if (manual_coeffs && !fixed_graph_index) {
    throw std::runtime_error("--coeffs requires a single --graph-index");
  }
  bool random_graph = !fixed_graph_index;
  std::vector<int> exponent_bag = options.values.count("values")
    ? parse_int_bag(options.values.at("values"))
    : weighted_signed_exponent_bag(value_limit);
  if (exponent_bag.empty()) throw std::runtime_error("--values must contain at least one integer");
  std::vector<int> exponent_domain = exponent_bag;
  std::sort(exponent_domain.begin(), exponent_domain.end());
  exponent_domain.erase(std::unique(exponent_domain.begin(), exponent_domain.end()), exponent_domain.end());
  std::mt19937_64 rng(seed);

  if (random_graph) {
    std::cout << "sampling graph_index uniformly from 1.." << graphs.size()
              << " during stage 1\n";
  } else {
    std::cout << "using fixed graph_index=" << *fixed_graph_index << "/" << graphs.size() << "\n";
  }

  std::vector<std::vector<int>> base_supports;
  if (!randomized_base_l) base_supports = finite_field_l_supports(base_k, base_poly);
  auto better_binary_witness = [](const BinaryWitness& lhs, const std::optional<BinaryWitness>& rhs) {
    if (!rhs) return true;
    return std::make_tuple(lhs.total_cost, lhs.extra_cost, lhs.graph_index, lhs.coeffs)
      < std::make_tuple(rhs->total_cost, rhs->extra_cost, rhs->graph_index, rhs->coeffs);
  };

  std::cout << "stage 1: base_k=" << base_k;
  if (randomized_base_l) {
    std::cout << " base_L=random-cyclic"
              << " mode=" << (reduced_l ? "reduced-L" : "full-random")
              << " extra_ones=" << (reduced_l ? "1..2" : std::to_string(l_extra_min) + ".." + std::to_string(l_extra_max));
  } else {
    std::cout << " base_poly=0x" << std::hex << base_poly << std::dec
              << " L=" << format_supports(base_supports);
  }
  if (manual_coeffs) {
    std::cout << " mode=manual";
  } else {
    std::cout << " attempts=" << stage1_attempts
              << " exponent_bag_size=" << exponent_bag.size()
              << " exponent_domain_size=" << exponent_domain.size()
              << " threads=" << thread_count;
    if (adaptive_sum_filter) {
      std::cout << " adaptive_sum_filter=on";
      if (initial_sum_threshold != std::numeric_limits<int>::max()) {
        std::cout << " initial_sum_threshold=" << initial_sum_threshold;
      }
    }
  }
  std::cout << "\n";

  auto evaluate_stage1_coeffs = [&](int graph_index, const Graph& local_graph, const std::vector<std::pair<int, int>>& coeffs) {
    MatrixPowerRing local_base_ring(base_k, base_supports);
    BinaryWitness witness;
    witness.t = t;
    witness.k = base_k;
    witness.graph_index = graph_index;
    witness.seed = seed;
    witness.supports = base_supports;
    witness.coeffs = coeffs;
    auto [total, base, extra] = binary_cost(witness, local_graph, local_base_ring);
    auto matrix = binary_transfer_matrix(local_graph, witness, local_base_ring);
    if (first_singular_binary_minor(matrix, base_k)) return std::optional<BinaryWitness>{};
    witness.total_cost = total;
    witness.base_cost = base;
    witness.extra_cost = extra;
    return std::optional<BinaryWitness>{witness};
  };

  std::optional<BinaryWitness> stage1_best;
  if (manual_coeffs) {
    const auto& local_graph = graphs.at(*fixed_graph_index - 1);
    auto coeffs = parse_int_pairs(options.values.at("coeffs"));
    if (coeffs.size() != static_cast<std::size_t>(local_graph.c)) {
      throw std::runtime_error(
        "--coeffs has " + std::to_string(coeffs.size()) +
        " pairs, but graph_index=" + std::to_string(*fixed_graph_index) +
        " expects " + std::to_string(local_graph.c)
      );
    }
    stage1_best = evaluate_stage1_coeffs(*fixed_graph_index, local_graph, coeffs);
    if (!stage1_best) {
      std::cout << "manual coeffs are not MDS for the stage 1 base field\n";
      return 1;
    }
  } else {
    unsigned int stage1_thread_count = std::min<unsigned int>(max_thread_count, std::max(1, stage1_attempts));
    std::atomic<int> shared_sum_threshold(initial_sum_threshold);
    int min_coeff_sum = 0;
    int max_coeff_sum = 0;
    if (!exponent_bag.empty()) {
      int min_abs_exponent = std::numeric_limits<int>::max();
      int max_abs_exponent = 0;
      for (int value : exponent_bag) {
        min_abs_exponent = std::min(min_abs_exponent, std::abs(value));
        max_abs_exponent = std::max(max_abs_exponent, std::abs(value));
      }
      int slot_count = 2 * graphs.front().c;
      min_coeff_sum = slot_count * min_abs_exponent;
      max_coeff_sum = slot_count * max_abs_exponent;
    }
    std::atomic<bool> stop_sum_filter(false);
    std::atomic<long long> sum_filter_accepted(0);
    std::atomic<long long> sum_filter_rejects(0);
    std::atomic<long long> sum_filter_updates(0);
    std::vector<std::future<std::optional<BinaryWitness>>> stage1_futures;
    stage1_futures.reserve(stage1_thread_count);
    for (unsigned int worker = 0; worker < stage1_thread_count; ++worker) {
      stage1_futures.push_back(std::async(std::launch::async, [&, worker]() -> std::optional<BinaryWitness> {
        std::mt19937_64 local_rng(static_cast<std::uint64_t>(seed) + 0x9e3779b97f4a7c15ULL * (worker + 1));
        std::uniform_int_distribution<int> local_graph_dist(1, static_cast<int>(graphs.size()));
        std::unique_ptr<MatrixPowerRing> fixed_base_ring;
        if (!randomized_base_l) fixed_base_ring = std::make_unique<MatrixPowerRing>(base_k, base_supports);
        std::optional<BinaryWitness> local_best;
        int cached_sum_budget = std::numeric_limits<int>::min();
        std::unique_ptr<AbsSumBudgetSampler> sum_sampler;

        auto update_shared_sum_threshold = [&](const std::vector<std::pair<int, int>>& coeffs) {
          int new_threshold = coeff_abs_sum(coeffs);
          int old_threshold = shared_sum_threshold.load(std::memory_order_relaxed);
          while (new_threshold < old_threshold &&
                 !shared_sum_threshold.compare_exchange_weak(
                   old_threshold,
                   new_threshold,
                   std::memory_order_relaxed,
                   std::memory_order_relaxed
                 )) {}
          if (new_threshold < old_threshold) {
            sum_filter_updates.fetch_add(1, std::memory_order_relaxed);
            if (new_threshold <= min_coeff_sum) {
              stop_sum_filter.store(true, std::memory_order_relaxed);
            }
          }
        };

        auto check_coeffs = [&](
          int graph_index,
          const Graph& local_graph,
          const std::vector<std::pair<int, int>>& coeffs,
          const std::vector<std::vector<int>>& supports,
          MatrixPowerRing& local_base_ring
        ) {
          BinaryWitness witness;
          witness.t = t;
          witness.k = base_k;
          witness.graph_index = graph_index;
          witness.seed = seed;
          witness.supports = supports;
          witness.coeffs = coeffs;
          auto [total, base, extra] = binary_cost(witness, local_graph, local_base_ring);
          if (local_best && total >= local_best->total_cost && !adaptive_sum_filter) return;
          auto matrix = binary_transfer_matrix(local_graph, witness, local_base_ring);
          if (first_singular_binary_minor(matrix, base_k)) return;
          witness.total_cost = total;
          witness.base_cost = base;
          witness.extra_cost = extra;
          if (options.has_flag("greedy-descent")) {
            witness.coeffs = greedy_binary_descent(witness, local_graph, local_base_ring, exponent_domain);
            auto updated = binary_cost(witness, local_graph, local_base_ring);
            witness.total_cost = std::get<0>(updated);
            witness.base_cost = std::get<1>(updated);
            witness.extra_cost = std::get<2>(updated);
          }
          if (adaptive_sum_filter) update_shared_sum_threshold(witness.coeffs);
          if (local_best && witness.total_cost >= local_best->total_cost) return;
          if (better_binary_witness(witness, local_best)) local_best = witness;
        };

        int worker_quota = stage1_attempts / static_cast<int>(stage1_thread_count);
        if (static_cast<int>(worker) < stage1_attempts % static_cast<int>(stage1_thread_count)) ++worker_quota;
        int accepted = 0;
        while (accepted < worker_quota && !stop_sum_filter.load(std::memory_order_relaxed)) {
          int graph_index = random_graph ? local_graph_dist(local_rng) : *fixed_graph_index;
          const auto& local_graph = graphs.at(graph_index - 1);
          std::vector<std::pair<int, int>> coeffs;
          int sum = 0;
          if (adaptive_sum_filter) {
            int threshold = shared_sum_threshold.load(std::memory_order_relaxed);
            if (threshold != std::numeric_limits<int>::max() &&
                threshold <= min_coeff_sum) {
              stop_sum_filter.store(true, std::memory_order_relaxed);
              break;
            }
            if (threshold != std::numeric_limits<int>::max()) {
              int budget = threshold - 1;
              if (budget < min_coeff_sum) {
                stop_sum_filter.store(true, std::memory_order_relaxed);
                break;
              }
              if (budget < max_coeff_sum) {
                if (budget != cached_sum_budget) {
                  sum_sampler = std::make_unique<AbsSumBudgetSampler>(2 * local_graph.c, exponent_bag, budget);
                  cached_sum_budget = budget;
                }
                if (!sum_sampler || !sum_sampler->feasible()) {
                  stop_sum_filter.store(true, std::memory_order_relaxed);
                  break;
                }
                if (!sum_sampler->sample(local_rng, coeffs, sum)) {
                  stop_sum_filter.store(true, std::memory_order_relaxed);
                  break;
                }
              }
            }
          }
          if (coeffs.empty()) {
            coeffs = random_coeffs(local_graph.c, exponent_bag, local_rng);
            sum = coeff_abs_sum(coeffs);
          }
          ++accepted;
          if (adaptive_sum_filter) sum_filter_accepted.fetch_add(1, std::memory_order_relaxed);
          if (randomized_base_l) {
            try {
              auto supports = random_stage1_l_supports(base_k, reduced_l, l_extra_min, l_extra_max, local_rng);
              MatrixPowerRing sampled_ring(base_k, supports);
              check_coeffs(graph_index, local_graph, coeffs, supports, sampled_ring);
            } catch (const std::exception&) {
              continue;
            }
          } else {
            check_coeffs(graph_index, local_graph, coeffs, base_supports, *fixed_base_ring);
          }
        }
        return local_best;
      }));
    }

    for (auto& future : stage1_futures) {
      auto local = future.get();
      if (local && better_binary_witness(*local, stage1_best)) stage1_best = *local;
    }
    if (adaptive_sum_filter) {
      std::cout << "adaptive sum filter: final_threshold="
                << shared_sum_threshold.load(std::memory_order_relaxed)
                << " accepted=" << sum_filter_accepted.load(std::memory_order_relaxed)
                << " rejected=" << sum_filter_rejects.load(std::memory_order_relaxed)
                << " updates=" << sum_filter_updates.load(std::memory_order_relaxed)
                << "\n";
    }
  }
  if (!stage1_best) {
    std::cout << "stage 1 found no MDS exponent pattern\n";
    return 1;
  }

  const auto& pattern = *stage1_best;
  const auto& graph = graphs.at(pattern.graph_index - 1);
  std::cout << "stage 1 selected graph_index=" << pattern.graph_index
            << " cost=" << pattern.total_cost
            << " coeffs=" << format_coeffs(pattern.coeffs)
            << " L=" << format_supports(pattern.supports) << "\n";

  int success_count = 0;
  int missing_targets = 0;
  for (int target_k : target_ks) {
    std::optional<BinaryWitness> best_witness;
    if (target_k == base_k) {
      best_witness = pattern;
    }
    if (target_k != base_k || randomized_base_l) {
      std::uint64_t reduced_candidate_count = reduced_l ? reduced_cyclic_l_count(target_k) : 0;
      unsigned int l_thread_count = reduced_l
        ? std::min<unsigned int>(
            max_thread_count,
            static_cast<unsigned int>(std::max<std::uint64_t>(1, reduced_candidate_count))
          )
        : std::min<unsigned int>(max_thread_count, std::max(1, l_attempts));
      std::cout << "stage 2: cyclic L search for k=" << target_k
                << " mode=" << (reduced_l ? "reduced-L" : "full-random")
                << " extra_ones=" << (reduced_l ? "1..2" : std::to_string(l_extra_min) + ".." + std::to_string(l_extra_max))
                << (reduced_l ? " candidates=" + std::to_string(reduced_candidate_count)
                              : " attempts=" + std::to_string(l_attempts))
                << " threads=" << l_thread_count << "\n";
      std::vector<std::future<std::optional<BinaryWitness>>> l_futures;
      l_futures.reserve(l_thread_count);
      for (unsigned int worker = 0; worker < l_thread_count; ++worker) {
        l_futures.push_back(std::async(std::launch::async, [&, worker]() -> std::optional<BinaryWitness> {
          std::mt19937_64 local_rng(
            static_cast<std::uint64_t>(seed)
            + 0xd1b54a32d192ed03ULL * (worker + 1)
            + 0x94d049bb133111ebULL * static_cast<std::uint64_t>(target_k)
          );
          std::optional<BinaryWitness> local_best;
          std::optional<int> local_best_total;
          auto check_supports = [&](const std::vector<std::vector<int>>& supports) {
            MatrixPowerRing ring(target_k, supports);
            BinaryWitness witness;
            witness.t = t;
            witness.k = target_k;
            witness.graph_index = pattern.graph_index;
            witness.seed = seed;
            witness.supports = supports;
            witness.coeffs = pattern.coeffs;
            auto [total, base, extra] = binary_cost(witness, graph, ring);
            if (local_best_total && total >= *local_best_total) return;
            auto matrix = binary_transfer_matrix(graph, witness, ring);
            if (first_singular_binary_minor(matrix, target_k)) return;
            witness.total_cost = total;
            witness.base_cost = base;
            witness.extra_cost = extra;
            if (better_binary_witness(witness, local_best)) {
              local_best = witness;
              local_best_total = witness.total_cost;
            }
          };

          if (reduced_l) {
            for (
              std::uint64_t candidate = worker;
              candidate < reduced_candidate_count;
              candidate += l_thread_count
            ) {
              try {
                check_supports(reduced_cyclic_l_supports_at(target_k, candidate));
              } catch (const std::exception&) {
                continue;
              }
            }
          } else {
            for (int attempt = static_cast<int>(worker) + 1; attempt <= l_attempts; attempt += static_cast<int>(l_thread_count)) {
              try {
                check_supports(random_cyclic_l_supports(target_k, l_extra_min, l_extra_max, local_rng));
              } catch (const std::exception&) {
                continue;
              }
            }
          }
          return local_best;
        }));
      }

      for (auto& future : l_futures) {
        auto local = future.get();
        if (!local) continue;
        if (better_binary_witness(*local, best_witness)) {
          best_witness = *local;
        }
      }
    }

    if (!best_witness) {
      std::cout << "stage 2 found no improved MDS L for k=" << target_k << "\n";
      ++missing_targets;
      continue;
    }
    std::string line = format_binary_witness_line(*best_witness);
    ++success_count;
    std::cout << line << "\n";
  }

  if (success_count == 0) return 1;
  return missing_targets ? 1 : 0;
}

void usage() {
  std::cerr
    << "usage: improve_binary [options]\n";
}

int main(int argc, char** argv) {
  try {
    if (argc == 1) {
      usage();
      return 2;
    }
    Options options = parse_options(argc, argv, 1);
    if (!options.values.count("graph-root")) {
      options.values["graph-root"] = infer_graph_root(argv[0]).string();
    }
    return command_improve_binary(options);
  } catch (const std::exception& error) {
    usage();
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
}
