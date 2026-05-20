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

std::string read_text(const fs::path& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open " + path.string());
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) lines.push_back(line);
  return lines;
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

long long abs_ll(long long value) {
  return value < 0 ? -value : value;
}

long long determinant(std::vector<std::vector<long long>> matrix) {
  int n = static_cast<int>(matrix.size());
  if (n == 0) return 1;
  int sign = 1;
  long long previous = 1;
  for (int col = 0; col < n - 1; ++col) {
    int pivot = -1;
    for (int row = col; row < n; ++row) {
      if (matrix[row][col] != 0) {
        pivot = row;
        break;
      }
    }
    if (pivot < 0) return 0;
    if (pivot != col) {
      std::swap(matrix[pivot], matrix[col]);
      sign = -sign;
    }
    long long pivot_value = matrix[col][col];
    for (int row = col + 1; row < n; ++row) {
      for (int target_col = col + 1; target_col < n; ++target_col) {
        __int128 value = static_cast<__int128>(matrix[row][target_col]) * pivot_value
          - static_cast<__int128>(matrix[row][col]) * matrix[col][target_col];
        value /= previous;
        matrix[row][target_col] = static_cast<long long>(value);
      }
    }
    previous = pivot_value;
    for (int row = col + 1; row < n; ++row) matrix[row][col] = 0;
  }
  return sign * matrix[n - 1][n - 1];
}

struct MinorStats {
  bool ok = true;
  int checked = 0;
  std::optional<long long> min_abs_det;
  long long max_abs_det = 0;
  std::vector<int> zero_rows;
  std::vector<int> zero_cols;
};

MinorStats check_prime_minors(const std::vector<std::vector<long long>>& matrix) {
  MinorStats stats;
  int t = static_cast<int>(matrix.size());
  std::vector<int> all(t);
  std::iota(all.begin(), all.end(), 0);
  for (int size = 1; size <= t; ++size) {
    auto row_sets = combinations(all, size);
    auto col_sets = combinations(all, size);
    for (const auto& rows : row_sets) {
      for (const auto& cols : col_sets) {
        std::vector<std::vector<long long>> sub(size, std::vector<long long>(size));
        for (int i = 0; i < size; ++i) {
          for (int j = 0; j < size; ++j) sub[i][j] = matrix[rows[i]][cols[j]];
        }
        long long det = determinant(sub);
        ++stats.checked;
        if (det == 0) {
          stats.ok = false;
          stats.zero_rows = rows;
          stats.zero_cols = cols;
          return stats;
        }
        long long abs_det = abs_ll(det);
        stats.min_abs_det = stats.min_abs_det ? std::min(*stats.min_abs_det, abs_det) : abs_det;
        stats.max_abs_det = std::max(stats.max_abs_det, abs_det);
      }
    }
  }
  return stats;
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

std::vector<std::vector<long long>> prime_transfer_matrix(const Graph& graph, const std::vector<std::pair<int, int>>& coeffs) {
  std::map<int, std::pair<int, int>> coeff_by_node;
  for (int i = 0; i < graph.c; ++i) coeff_by_node[graph.n + i] = coeffs[i];
  std::map<int, std::vector<long long>> memo;
  std::set<int> visiting;
  auto vec = [&](auto&& self, int node) -> std::vector<long long> {
    if (memo.count(node)) return memo[node];
    if (node < graph.n) {
      std::vector<long long> basis(graph.n, 0);
      basis[node] = 1;
      memo[node] = basis;
      return basis;
    }
    if (visiting.count(node)) throw std::runtime_error("cycle in prime transfer");
    visiting.insert(node);
    auto left = self(self, graph.pre[node][0]);
    auto right = self(self, graph.pre[node][1]);
    auto [a, b] = coeff_by_node[node];
    std::vector<long long> result(graph.n);
    for (int i = 0; i < graph.n; ++i) result[i] = a * left[i] + b * right[i];
    visiting.erase(node);
    memo[node] = result;
    return result;
  };
  std::vector<std::vector<long long>> matrix;
  for (int output : graph.output_indices()) matrix.push_back(vec(vec, output));
  return matrix;
}

std::pair<long long, long long> entry_stats(const std::vector<std::vector<long long>>& matrix) {
  long long max_entry = 0;
  long long sum = 0;
  for (const auto& row : matrix) {
    for (long long value : row) {
      max_entry = std::max(max_entry, abs_ll(value));
      sum += abs_ll(value);
    }
  }
  return {max_entry, sum};
}

std::vector<int> parse_int_bag(std::string raw) {
  std::replace(raw.begin(), raw.end(), ',', ' ');
  std::stringstream ss(raw);
  std::vector<int> values;
  int value = 0;
  while (ss >> value) values.push_back(value);
  return values;
}

int command_improve_prime(const Options& options) {
  int t = options.get_int("t", -1);
  int attempts = options.get_int("attempts", 10000);
  int seed = options.get_int("seed", 1);
  fs::path graph_root = options.get("graph-root", "graph_data");
  if (options.values.count("graph-file") && options.values.count("graph-c")) {
    throw std::runtime_error("--graph-file and --graph-c cannot be combined");
  }
  fs::path graph_data_path = options.values.count("graph-file")
    ? fs::path(options.values.at("graph-file"))
    : graph_path(graph_root, t, options.get_optional_int("graph-c"));
  auto values = parse_int_bag(options.get("values", "1,1,1,2,2,3"));
  auto graphs = read_graphs_auto(graph_data_path);
  if (graphs.empty()) throw std::runtime_error("no graph templates found for t=" + std::to_string(t));
  std::cout << "graph file=" << graph_data_path.string()
            << " graphs=" << graphs.size()
            << " c=" << graphs.front().c << "\n";
  std::optional<int> fixed_graph_index = options.get_optional_int("graph-index");
  if (fixed_graph_index && (*fixed_graph_index < 1 || *fixed_graph_index > static_cast<int>(graphs.size()))) {
    throw std::runtime_error("--graph-index outside 1.." + std::to_string(graphs.size()));
  }
  std::mt19937_64 rng(seed);
  std::uniform_int_distribution<int> graph_dist(1, static_cast<int>(graphs.size()));
  bool has_best = false;
  std::tuple<long long, long long, long long> best_score;
  std::vector<std::vector<long long>> best_matrix;
  std::vector<std::pair<int, int>> best_coeffs;
  int best_graph_index = 0;
  if (fixed_graph_index) {
    std::cout << "using fixed graph_index=" << *fixed_graph_index << "/" << graphs.size() << "\n";
  } else {
    std::cout << "sampling graph_index uniformly from 1.." << graphs.size() << "\n";
  }
  for (int attempt = 1; attempt <= attempts; ++attempt) {
    int graph_index = fixed_graph_index ? *fixed_graph_index : graph_dist(rng);
    const auto& graph = graphs.at(graph_index - 1);
    auto coeffs = random_coeffs(graph.c, values, rng);
    auto matrix = prime_transfer_matrix(graph, coeffs);
    auto stats = check_prime_minors(matrix);
    if (!stats.ok) continue;
    auto [max_entry, sum_entry] = entry_stats(matrix);
    auto score = std::make_tuple(max_entry, sum_entry, stats.max_abs_det);
    if (!has_best || score < best_score) {
      has_best = true;
      best_score = score;
      best_matrix = matrix;
      best_coeffs = coeffs;
      best_graph_index = graph_index;
      std::cout << "best attempt=" << attempt << " max_entry=" << max_entry
                << " sum_entry=" << sum_entry << " max_det=" << stats.max_abs_det
                << " graph_index=" << graph_index << "\n";
    }
  }
  if (!has_best) {
    std::cout << "no MDS candidate found\n";
    return 1;
  }
  std::cout << "matrix:\n";
  for (const auto& row : best_matrix) {
    std::cout << "  [";
    for (long long value : row) std::cout << " " << value;
    std::cout << "]\n";
  }
  std::cout << "graph_index=" << best_graph_index << "\n";
  std::cout << "coeffs=" << format_coeffs(best_coeffs) << "\n";
  return 0;
}

void usage() {
  std::cerr
    << "usage: improve_prime [options]\n";
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
    return command_improve_prime(options);
  } catch (const std::exception& error) {
    usage();
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
}
