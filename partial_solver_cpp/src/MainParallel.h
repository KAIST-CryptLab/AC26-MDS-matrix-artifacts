#pragma once

#include "Algorithms.h"
#include "Utils.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace solver_cpp {
namespace fs = std::filesystem;

constexpr std::size_t STREAM_GRAPH_FLUSH_SIZE = 10000;
constexpr const char* GRAPH_DATA_EXTENSION = ".bin";

class Solver {
public:
  Solver(fs::path data_root_, bool force_, bool quiet_)
    : data_root(std::move(data_root_)), force(force_), quiet(quiet_) {}

  std::size_t generate_sc(
    int n,
    int m,
    int c,
    std::size_t num = 1,
    bool force_current = false,
    bool require_complete_log = false
  ) {
    (void)require_complete_log;
    if (!(m == n || m == n + 1)) {
      throw std::runtime_error("m must be n or n + 1");
    }
    if (c < minimum_sc_size(n, m)) {
      return 0;
    }

    if (n == 2 && m == 2) {
      if (!force_current && fs::is_regular_file(sc_set_path(n, m, c))) {
        if (should_print_generation(n, m)) {
          std::cout << "GenerateSC(" << n << ", " << m << ", " << c
                    << ") - known data exists\n";
        }
        return 0;
      }
      return generate_base(n, m, c);
    }

    if (m == n + 1) {
      return generate_partial(
        n,
        m,
        c,
        n,
        n,
        c - 1,
        num,
        "OneOutput",
        [](Graph previous_graph, const std::unordered_set<std::string>& known_records) {
          return validate_generated_graphs(
            expand_sc_by_one_output(previous_graph),
            SuperconcentratorCheckMode::NewOutput,
            &known_records
          );
        }
      );
    }

    return generate_partial(
      n,
      m,
      c,
      n - 1,
      m,
      c - 2,
      num,
      "OneInput",
      [](Graph previous_graph, const std::unordered_set<std::string>& known_records) {
        return validate_generated_graphs(
          expand_sc_by_one_input(previous_graph),
          SuperconcentratorCheckMode::NewInput,
          &known_records
        );
      }
    );
  }

  fs::path sc_set_path(int n, int m, int c) const {
    return data_root / (std::to_string(n) + "_" + std::to_string(m) + "_" + std::to_string(c)) / "data.bin";
  }

  fs::path sc_done_path(int n, int m, int c) const {
    return data_root / (std::to_string(n) + "_" + std::to_string(m) + "_" + std::to_string(c)) / "data_done.bin";
  }

private:
  struct CandidateRecord {
    std::string hash;
    std::string record;
  };

  struct SourceBatch {
    fs::path source_path;
    std::uintmax_t original_size = 0;
    std::uintmax_t remaining_size = 0;
    std::vector<std::string> records;
    std::vector<Graph> graphs;
  };

  struct ProgressFrame {
    std::string desc;
    std::size_t completed = 0;
    std::size_t total = 0;
    std::size_t stored_count = 0;
    std::chrono::steady_clock::time_point start;
  };

  enum class SuperconcentratorCheckMode {
    Full,
    NewInput,
    NewOutput,
    Assume,
  };

  struct GenerationUnit {
    int alpha = 0;
    int previous_n = 0;
    int previous_m = 0;
    int previous_c = 0;
    fs::path source_path;
    fs::path output_path;
  };

  struct TaskSelector {
    std::vector<std::pair<int, int>> ranges;
  };

  static int minimum_sc_size(int n, int m) {
    return 2 * n + m - 4;
  }

  bool should_print_generation(int n, int m) const {
    return !quiet && n >= 6 && m >= 6;
  }

  fs::path sc_set_dir(int n, int m, int c) const {
    return sc_set_path(n, m, c).parent_path();
  }

  fs::path sc_split_dir(int n, int m, int c) const {
    return sc_set_dir(n, m, c) / "data_split";
  }

  fs::path sc_split_path(int n, int m, int c, const std::string& filename) const {
    return sc_split_dir(n, m, c) / filename;
  }

  static bool starts_with(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
  }

  static std::string alpha_chunk_filename(int alpha, int chunk_index) {
    return "data_" + std::to_string(alpha) + "_" + std::to_string(chunk_index) + GRAPH_DATA_EXTENSION;
  }

  static int source_split_chunk_index(const fs::path& source_path) {
    std::string filename = source_path.filename().string();
    if (!starts_with(filename, "data_") || !ends_with(filename, GRAPH_DATA_EXTENSION)) {
      return -1;
    }

    std::string index_text = filename.substr(5, filename.size() - 5 - std::string(GRAPH_DATA_EXTENSION).size());
    std::size_t separator = index_text.find('_');
    std::string chunk_text;
    if (separator == std::string::npos) {
      chunk_text = index_text;
    } else {
      chunk_text = index_text.substr(separator + 1);
    }

    if (chunk_text.empty()) {
      return -1;
    }
    for (unsigned char ch : chunk_text) {
      if (!std::isdigit(ch)) {
        return -1;
      }
    }
    return std::stoi(chunk_text);
  }

  static int parse_positive_int_strict(const std::string& value) {
    if (value.empty()) {
      throw std::runtime_error("empty integer");
    }
    for (unsigned char ch : value) {
      if (!std::isdigit(ch)) {
        throw std::runtime_error("invalid integer: " + value);
      }
    }
    return std::stoi(value);
  }

  static std::vector<std::pair<int, int>> parse_generation_range_spec(const std::string& range_spec) {
    std::vector<std::pair<int, int>> ranges;
    std::size_t start_pos = 0;

    while (start_pos <= range_spec.size()) {
      std::size_t comma_pos = range_spec.find(',', start_pos);
      std::string part = range_spec.substr(
        start_pos,
        comma_pos == std::string::npos ? std::string::npos : comma_pos - start_pos
      );
      if (part.empty()) {
        throw std::runtime_error("empty range item");
      }

      std::size_t dash_pos = part.find('-');
      int start = 0;
      int end = 0;
      if (dash_pos == std::string::npos) {
        start = end = parse_positive_int_strict(part);
      } else {
        if (part.find('-', dash_pos + 1) != std::string::npos) {
          throw std::runtime_error("invalid range item: " + part);
        }
        start = parse_positive_int_strict(part.substr(0, dash_pos));
        end = parse_positive_int_strict(part.substr(dash_pos + 1));
      }

      if (start <= 0 || end < start) {
        throw std::runtime_error("range items must satisfy 1 <= start <= end");
      }
      ranges.push_back({start, end});

      if (comma_pos == std::string::npos) {
        break;
      }
      start_pos = comma_pos + 1;
    }

    if (ranges.empty()) {
      throw std::runtime_error("range spec must not be empty");
    }
    return ranges;
  }

  static fs::path generation_log_path(const fs::path& data_path) {
    std::string filename = data_path.filename().string();
    if (!starts_with(filename, "data") || data_path.extension() != GRAPH_DATA_EXTENSION) {
      throw std::runtime_error("unexpected graph data filename: " + data_path.string());
    }
    std::string suffix = filename.substr(4, filename.size() - 4 - std::string(GRAPH_DATA_EXTENSION).size());
    return data_path.parent_path() / ("log" + suffix + ".txt");
  }

  static bool generation_log_exists(const fs::path& data_path) {
    try {
      return fs::is_regular_file(generation_log_path(data_path));
    } catch (const std::exception&) {
      return false;
    }
  }

  static void remove_generation_log(const fs::path& data_path) {
    std::error_code ignored;
    fs::remove(generation_log_path(data_path), ignored);
  }

  static bool ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
  }

  static int split_unit_index(const GenerationUnit& unit) {
    std::string filename = unit.output_path.filename().string();
    if (!starts_with(filename, "data_") || !ends_with(filename, GRAPH_DATA_EXTENSION)) {
      return -1;
    }

    std::string index_text = filename.substr(5, filename.size() - 5 - std::string(GRAPH_DATA_EXTENSION).size());
    std::size_t separator = index_text.find('_');
    if (separator == std::string::npos || separator == 0 || separator + 1 >= index_text.size()) {
      return -1;
    }
    std::string alpha_text = index_text.substr(0, separator);
    std::string chunk_text = index_text.substr(separator + 1);
    for (unsigned char ch : alpha_text) {
      if (!std::isdigit(ch)) {
        return -1;
      }
    }
    for (unsigned char ch : chunk_text) {
      if (!std::isdigit(ch)) {
        return -1;
      }
    }
    if (std::stoi(alpha_text) != unit.alpha) {
      return -1;
    }
    return std::stoi(chunk_text);
  }

  TaskSelector prompt_generation_task_selector(
    int n,
    int m,
    int c,
    const std::string& generation_name
  ) const {
    while (true) {
      std::cout << generation_name << "(" << n << ", " << m << ", " << c
                << ") enter split task (ranges; example: 1-10,15-20,26): "
                << std::flush;

      std::string line;
      if (!std::getline(std::cin, line)) {
        throw std::runtime_error("failed to read split task selection");
      }

      std::istringstream input(line);
      std::vector<std::string> parts;
      std::string part;
      while (input >> part) {
        parts.push_back(part);
      }

      if (parts.size() == 1 || parts.size() == 2) {
        try {
          if (parts.size() == 1) {
            return TaskSelector{parse_generation_range_spec(parts[0])};
          }

          int start = parse_positive_int_strict(parts[0]);
          int end = parse_positive_int_strict(parts[1]);
          if (start <= 0 || end < start) {
            throw std::runtime_error("range items must satisfy 1 <= start <= end");
          }
          return TaskSelector{{{start, end}}};
        } catch (const std::exception&) {
        }
      }

      std::cout << "Invalid input. Use ranges. Example: 1-10,15-20,26\n";
    }
  }

  std::vector<GenerationUnit> select_generation_units(
    const std::vector<GenerationUnit>& units,
    const TaskSelector& selector
  ) const {
    std::vector<GenerationUnit> selected;
    for (const auto& unit : units) {
      int split_index = split_unit_index(unit);
      bool selected_index = false;
      for (const auto& range : selector.ranges) {
        if (split_index >= range.first && split_index <= range.second) {
          selected_index = true;
          break;
        }
      }
      if (selected_index) {
        selected.push_back(unit);
      }
    }
    return selected;
  }

  std::vector<fs::path> sc_source_split_paths(int n, int m, int c) const {
    return split_graph_list_paths(sc_set_dir(n, m, c), "data_split");
  }

  bool has_split_source_for_one_input(int n, int m, int c) const {
    return !sc_source_split_paths(n - 1, m, c - 2).empty();
  }

  bool has_split_source_for_one_output(int n, int m, int c) const {
    return !sc_source_split_paths(n, m - 1, c - 1).empty();
  }

  bool uses_split_generation_output(int n, int m, int c) const {
    if (n == 2 && m == 2) {
      return false;
    }
    if (m == n + 1) {
      return has_split_source_for_one_output(n, m, c);
    }
    if (m == n) {
      return has_split_source_for_one_input(n, m, c);
    }
    return false;
  }

  std::vector<GenerationUnit> one_input_units(int n, int m, int c, bool split_output) const {
    std::vector<GenerationUnit> units;
    int alpha = 2;
    int previous_n = n - 1;
    int previous_m = m;
    int previous_c = c - 2;

    auto split_paths = sc_source_split_paths(previous_n, previous_m, previous_c);
    if (!split_paths.empty()) {
      int fallback_chunk_index = 1;
      for (const auto& source_path : split_paths) {
        int chunk_index = source_split_chunk_index(source_path);
        if (chunk_index < 0) {
          chunk_index = fallback_chunk_index;
        }
        units.push_back(GenerationUnit{
          alpha,
          previous_n,
          previous_m,
          previous_c,
          source_path,
          sc_split_path(n, m, c, alpha_chunk_filename(alpha, chunk_index)),
        });
        ++fallback_chunk_index;
      }
      return units;
    }

    units.push_back(GenerationUnit{
      alpha,
      previous_n,
      previous_m,
      previous_c,
      sc_set_path(previous_n, previous_m, previous_c),
      split_output ? sc_split_path(n, m, c, alpha_chunk_filename(alpha, 1)) : sc_set_path(n, m, c),
    });
    return units;
  }

  std::vector<GenerationUnit> one_output_units(int n, int m, int c, bool split_output) const {
    std::vector<GenerationUnit> units;
    int alpha = 0;
    int previous_n = n;
    int previous_m = m - 1;
    int previous_c = c - 1;

    auto split_paths = sc_source_split_paths(previous_n, previous_m, previous_c);
    if (!split_paths.empty()) {
      int fallback_chunk_index = 1;
      for (const auto& source_path : split_paths) {
        int chunk_index = source_split_chunk_index(source_path);
        if (chunk_index < 0) {
          chunk_index = fallback_chunk_index;
        }
        units.push_back(GenerationUnit{
          alpha,
          previous_n,
          previous_m,
          previous_c,
          source_path,
          sc_split_path(n, m, c, alpha_chunk_filename(alpha, chunk_index)),
        });
        ++fallback_chunk_index;
      }
      return units;
    }

    units.push_back(GenerationUnit{
      alpha,
      previous_n,
      previous_m,
      previous_c,
      sc_set_path(previous_n, previous_m, previous_c),
      split_output ? sc_split_path(n, m, c, alpha_chunk_filename(alpha, 1)) : sc_set_path(n, m, c),
    });
    return units;
  }

  std::vector<GenerationUnit> expected_generation_units(int n, int m, int c) const {
    bool split_output = uses_split_generation_output(n, m, c);
    if (n == 2 && m == 2) {
      return {};
    }
    if (m == n + 1) {
      return one_output_units(n, m, c, split_output);
    }
    if (m == n) {
      return one_input_units(n, m, c, split_output);
    }
    return {};
  }

  bool source_path_exists_for_unit(const fs::path& path) const {
    return fs::is_regular_file(path);
  }

  static std::size_t hardware_batch_size() {
    std::size_t hardware_count = std::thread::hardware_concurrency();
    return hardware_count == 0 ? 1 : hardware_count;
  }

  static std::uintmax_t checked_record_size(int c) {
    std::size_t record_size = canonical_hash_record_size(c);
    if (record_size == 0) {
      throw std::runtime_error("record size must be positive");
    }
    return static_cast<std::uintmax_t>(record_size);
  }

  std::uintmax_t graph_record_count(const fs::path& path, int c) const {
    if (!fs::is_regular_file(path)) {
      return 0;
    }
    std::uintmax_t record_size = checked_record_size(c);
    std::uintmax_t file_size = fs::file_size(path);
    if (file_size % record_size != 0) {
      throw std::runtime_error("graph data file has a partial record: " + path.string());
    }
    return file_size / record_size;
  }

  void load_record_hashes(
    const fs::path& path,
    int c,
    std::unordered_set<std::string>& hashes
  ) const {
    if (!fs::is_regular_file(path)) {
      return;
    }

    std::size_t record_size = canonical_hash_record_size(c);
    if (record_size == 0) {
      return;
    }
    if (fs::file_size(path) % static_cast<std::uintmax_t>(record_size) != 0) {
      throw std::runtime_error("graph data file has a partial record: " + path.string());
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
      throw std::runtime_error("failed to open graph data file: " + path.string());
    }

    std::string record(record_size, '\0');
    while (in.read(record.data(), static_cast<std::streamsize>(record.size()))) {
      hashes.insert(record);
    }
  }

  SourceBatch read_source_batch_from_tail(
    int n,
    int m,
    int c,
    std::size_t max_count
  ) const {
    SourceBatch batch;
    batch.source_path = sc_set_path(n, m, c);
    if (max_count == 0 || !fs::is_regular_file(batch.source_path)) {
      return batch;
    }

    std::size_t record_size = canonical_hash_record_size(c);
    if (record_size == 0) {
      return batch;
    }

    batch.original_size = fs::file_size(batch.source_path);
    if (batch.original_size % static_cast<std::uintmax_t>(record_size) != 0) {
      throw std::runtime_error("graph data file has a partial record: " + batch.source_path.string());
    }

    std::uintmax_t available_count = batch.original_size / static_cast<std::uintmax_t>(record_size);
    std::size_t take_count = static_cast<std::size_t>(std::min<std::uintmax_t>(
      static_cast<std::uintmax_t>(max_count),
      available_count
    ));
    if (take_count == 0) {
      return batch;
    }

    batch.remaining_size =
      batch.original_size - static_cast<std::uintmax_t>(take_count) * static_cast<std::uintmax_t>(record_size);

    std::ifstream in(batch.source_path, std::ios::binary);
    if (!in) {
      throw std::runtime_error("failed to open source graph data: " + batch.source_path.string());
    }
    in.seekg(static_cast<std::streamoff>(batch.remaining_size));

    batch.records.reserve(take_count);
    batch.graphs.reserve(take_count);
    for (std::size_t i = 0; i < take_count; ++i) {
      std::string record(record_size, '\0');
      if (!in.read(record.data(), static_cast<std::streamsize>(record.size()))) {
        throw std::runtime_error("failed to read source graph record: " + batch.source_path.string());
      }
      batch.graphs.push_back(graph_from_canonical_hash_record(record, n, m, c));
      batch.records.push_back(std::move(record));
    }
    return batch;
  }

  void append_unique_records(
    const fs::path& path,
    const std::vector<std::string>& records,
    std::unordered_set<std::string>& hashes
  ) const {
    if (records.empty()) {
      return;
    }

    fs::create_directories(path.parent_path());
    auto out = open_graph_data_for_write_with_retry(path, std::ios::binary | std::ios::app, "append");

    for (const auto& record : records) {
      if (!hashes.insert(record).second) {
        continue;
      }
      out.write(record.data(), static_cast<std::streamsize>(record.size()));
    }
  }

  void move_source_batch_to_done(
    int n,
    int m,
    int c,
    const SourceBatch& batch,
    std::unordered_set<std::string>& done_hashes
  ) const {
    if (batch.records.empty()) {
      return;
    }
    if (fs::file_size(batch.source_path) != batch.original_size) {
      throw std::runtime_error("source graph data changed while generating: " + batch.source_path.string());
    }

    append_unique_records(sc_done_path(n, m, c), batch.records, done_hashes);
    fs::resize_file(batch.source_path, batch.remaining_size);
  }

  bool is_split_generation_complete(int n, int m, int c) const {
    if (!uses_split_generation_output(n, m, c)) {
      return false;
    }
    if (split_graph_list_paths(sc_set_dir(n, m, c), "data_split").empty()) {
      return false;
    }

    bool known_source_unit_found = false;
    for (const auto& unit : expected_generation_units(n, m, c)) {
      if (!source_path_exists_for_unit(unit.source_path)) {
        continue;
      }
      known_source_unit_found = true;
      if (!generation_log_exists(unit.output_path)) {
        return false;
      }
    }
    return known_source_unit_found;
  }

  bool known_sc_exists(int n, int m, int c, bool require_complete_log = false) {
    fs::path data_path = sc_set_path(n, m, c);
    if (fs::is_regular_file(data_path)) {
      return !require_complete_log || generation_log_exists(data_path);
    }

    if (is_split_generation_complete(n, m, c)) {
      return true;
    }

    return false;
  }

  bool import_graph_list(int n, int m, int c, const fs::path& path, std::vector<Graph>& graphs) const {
    if (fs::is_regular_file(path)) {
      std::ifstream in(path, std::ios::binary);
      if (!in) {
        return false;
      }

      std::size_t record_size = canonical_hash_record_size(c);
      if (record_size == 0) {
        return true;
      }

      std::string record(record_size, '\0');
      while (in.read(record.data(), static_cast<std::streamsize>(record.size()))) {
        try {
          graphs.push_back(graph_from_canonical_hash_record(record, n, m, c));
        } catch (const std::exception&) {
          // Keep parity with the Python reader, which skips malformed records.
        }
      }
      return true;
    }

    fs::path folder = path.parent_path();
    if (folder.empty() || !fs::is_directory(folder)) {
      return false;
    }

    auto split_paths = split_graph_list_paths(folder, "data_split");
    if (split_paths.empty()) {
      split_paths = split_graph_list_paths(folder, "data");
    }
    if (split_paths.empty()) {
      return false;
    }

    for (const auto& split_path : split_paths) {
      if (!import_graph_list(n, m, c, split_path, graphs)) {
        return false;
      }
    }
    return true;
  }

  std::vector<fs::path> split_graph_list_paths(const fs::path& folder, const std::string& split_folder_name) const {
    fs::path split_folder = folder / split_folder_name;
    std::vector<fs::path> paths;
    if (!fs::is_directory(split_folder)) {
      return paths;
    }

    for (const auto& entry : fs::directory_iterator(split_folder)) {
      if (entry.is_regular_file() && entry.path().extension() == GRAPH_DATA_EXTENSION) {
        paths.push_back(entry.path());
      }
    }
    std::sort(paths.begin(), paths.end(), natural_less);
    return paths;
  }

  void initialize_graph_list_file(const fs::path& path) const {
    fs::create_directories(path.parent_path());
    auto out = open_graph_data_for_write_with_retry(path, std::ios::binary | std::ios::trunc, "initialize");
  }

  std::ofstream open_graph_data_for_write_with_retry(
    const fs::path& path,
    std::ios::openmode mode,
    const std::string& action
  ) const {
    constexpr int max_attempts = 30;
    constexpr auto retry_delay = std::chrono::seconds(1);

    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
      std::ofstream out(path, mode);
      if (out) {
        return out;
      }
      if (attempt != max_attempts) {
        std::this_thread::sleep_for(retry_delay);
      }
    }

    throw std::runtime_error("failed to open graph data for " + action + ": " + path.string());
  }

  void write_generation_log(const fs::path& data_path, std::size_t graph_count, double elapsed_seconds) const {
    (void)data_path;
    (void)graph_count;
    (void)elapsed_seconds;
  }

  void print_generation_summary(
    std::size_t graph_count,
    const fs::path& path,
    double elapsed_seconds,
    bool enabled = true
  ) const {
    (void)path;
    (void)elapsed_seconds;
    if (quiet || !enabled) {
      return;
    }
    std::cout << "stored graphs: " << graph_count << "\n"
              << "-------------------------\n\n";
  }

  static double elapsed_seconds_since(std::chrono::steady_clock::time_point start) {
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    return elapsed.count();
  }

  static std::string format_progress_time(double seconds) {
    int total_seconds = static_cast<int>(seconds + 0.5);
    int hours = total_seconds / 3600;
    int minutes = (total_seconds / 60) % 60;
    int secs = total_seconds % 60;

    std::ostringstream out;
    out << std::setfill('0');
    if (hours > 0) {
      out << hours << ":" << std::setw(2) << minutes << ":" << std::setw(2) << secs;
    } else {
      out << std::setw(2) << minutes << ":" << std::setw(2) << secs;
    }
    return out.str();
  }

  static std::string make_progress_line(
    const std::string& desc,
    std::size_t completed,
    std::size_t total,
    std::size_t stored_count,
    std::chrono::steady_clock::time_point start
  ) {
    constexpr int bar_width = 30;
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed_duration = now - start;
    double elapsed = elapsed_duration.count();
    double fraction = total == 0 ? 1.0 : static_cast<double>(completed) / static_cast<double>(total);
    fraction = std::max(0.0, std::min(1.0, fraction));
    int filled = static_cast<int>(fraction * bar_width);
    if (completed == total) {
      filled = bar_width;
    }

    double rate = elapsed > 0.0 ? static_cast<double>(completed) / elapsed : 0.0;
    std::string remaining = "?";
    if (completed > 0 && completed < total) {
      double eta = elapsed * static_cast<double>(total - completed) / static_cast<double>(completed);
      remaining = format_progress_time(eta);
    } else if (completed == total) {
      remaining = "00:00";
    }

    std::ostringstream out;
    out << desc << ": "
        << std::setw(3) << static_cast<int>(fraction * 100.0) << "%|";
    for (int i = 0; i < bar_width; ++i) {
      out << (i < filled ? '#' : '-');
    }
    out << "| " << completed << "/" << total
        << " [" << format_progress_time(elapsed) << "<" << remaining << ", ";
    if (rate > 0.0) {
      out << std::fixed << std::setprecision(2) << rate << "task/s";
    } else {
      out << "?task/s";
    }
    out << ", sc=" << stored_count << "]";
    return out.str();
  }

  void render_progress_line(
    const std::string& desc,
    std::size_t completed,
    std::size_t total,
    std::size_t stored_count,
    std::chrono::steady_clock::time_point start,
    std::size_t& last_width,
    bool finish_line,
    bool enabled = true
  ) const {
    if (quiet || !enabled) {
      return;
    }

    std::string line = make_progress_line(desc, completed, total, stored_count, start);
    std::cout << "\r" << line;
    if (last_width > line.size()) {
      std::cout << std::string(last_width - line.size(), ' ');
    }
    if (finish_line) {
      std::cout << "\n";
    }
    std::cout << std::flush;
    last_width = finish_line ? 0 : line.size();
  }

  void clear_progress_line(std::size_t& last_width, bool enabled = true) const {
    if (quiet || !enabled || last_width == 0) {
      return;
    }
    std::cout << "\r" << std::string(last_width, ' ') << "\r" << std::flush;
    last_width = 0;
  }

  void render_progress_stack() {
    if (quiet) {
      return;
    }

    if (rendered_progress_stack_lines != 0) {
      std::cout << "\r\033[" << rendered_progress_stack_lines << "A";
      for (std::size_t index = 0; index < rendered_progress_stack_lines; ++index) {
        std::cout << "\033[2K\r";
        if (index + 1 < rendered_progress_stack_lines) {
          std::cout << "\033[1B";
        }
      }
      if (rendered_progress_stack_lines > 1) {
        std::cout << "\033[" << (rendered_progress_stack_lines - 1) << "A";
      }
    }

    for (std::size_t index = 0; index < progress_stack.size(); ++index) {
      std::cout << "\033[2K\r";
      const auto& frame = progress_stack[index];
      std::cout << make_progress_line(
        frame.desc,
        frame.completed,
        frame.total,
        frame.stored_count,
        frame.start
      );
      std::cout << "\n";
    }
    std::cout << std::flush;
    rendered_progress_stack_lines = progress_stack.size();
  }

  std::size_t push_progress_frame(
    const std::string& desc,
    std::size_t completed,
    std::size_t total,
    std::size_t stored_count,
    std::chrono::steady_clock::time_point start,
    bool enabled
  ) {
    if (!enabled) {
      return std::numeric_limits<std::size_t>::max();
    }
    progress_stack.push_back(ProgressFrame{desc, completed, total, stored_count, start});
    render_progress_stack();
    return progress_stack.size() - 1;
  }

  void update_progress_frame(
    std::size_t index,
    const std::string& desc,
    std::size_t completed,
    std::size_t total,
    std::size_t stored_count
  ) {
    if (index >= progress_stack.size()) {
      return;
    }
    progress_stack[index].desc = desc;
    progress_stack[index].completed = completed;
    progress_stack[index].total = total;
    progress_stack[index].stored_count = stored_count;
    render_progress_stack();
  }

  void pop_progress_frame(std::size_t index) {
    if (index >= progress_stack.size()) {
      return;
    }
    if (index + 1 == progress_stack.size()) {
      progress_stack.pop_back();
    } else {
      progress_stack.erase(progress_stack.begin() + static_cast<std::ptrdiff_t>(index));
    }
    render_progress_stack();
  }

  std::size_t generate_base(int n, int m, int c) {
    auto start = std::chrono::steady_clock::now();
    std::vector<Graph> graphs;
    std::unordered_set<std::string> hashes;
    bool show_progress = should_print_generation(n, m);
    std::size_t progress_last_width = 0;
    std::string progress_desc = "SC(2,2," + std::to_string(c) + ") base";
    auto progress_callback = [&](
      std::uint64_t completed,
      std::uint64_t total,
      std::size_t stored_count
    ) {
      render_progress_line(
        progress_desc,
        static_cast<std::size_t>(completed),
        static_cast<std::size_t>(total),
        stored_count,
        start,
        progress_last_width,
        completed >= total,
        show_progress
      );
    };

    Size2ProgressCallback callback;
    if (show_progress) {
      callback = progress_callback;
    }

    for (auto graph : gen_sc_size_2(c, callback)) {
      graph.reorder(false);
      std::string graph_hash = canonical_graph_hash(graph);
      if (hashes.insert(graph_hash).second) {
        graphs.push_back(std::move(graph));
      }
    }

    fs::path output_path = sc_set_path(n, m, c);
    initialize_graph_list_file(output_path);
    {
      auto out = open_graph_data_for_write_with_retry(output_path, std::ios::binary | std::ios::app, "append");
      for (const auto& graph : graphs) {
        std::string record = graph_to_canonical_hash_record(graph);
        out.write(record.data(), static_cast<std::streamsize>(record.size()));
      }
    }

    double elapsed = elapsed_seconds_since(start);
    write_generation_log(output_path, graphs.size(), elapsed);
    print_generation_summary(graphs.size(), output_path, elapsed, show_progress);
    return graphs.size();
  }

  template <typename Fn>
  std::size_t run_generation(int n, int m, int c, const std::string& generation_name, Fn&& fn) {
    if (!quiet) {
      std::cout << "GenerateSC(" << n << ", " << m << ", " << c << ") ("
                << generation_name << ")\n" << std::flush;
    }
    fs::path output_path = sc_set_path(n, m, c);
    bool output_already_exists = fs::exists(output_path) && generation_log_exists(output_path) && !force;
    last_generation_output_path.clear();
    generation_unit_logs_written = false;
    auto start = std::chrono::steady_clock::now();
    std::size_t graph_count = fn();
    double elapsed = elapsed_seconds_since(start);
    if (!output_already_exists && !generation_unit_logs_written) {
      fs::path log_target_path = last_generation_output_path.empty() ? output_path : last_generation_output_path;
      write_generation_log(log_target_path, graph_count, elapsed);
    }
    print_generation_summary(
      graph_count,
      last_generation_output_path.empty() ? output_path : last_generation_output_path,
      elapsed
    );
    return graph_count;
  }

  static std::string canonical_hash_from_key(const std::vector<std::vector<int>>& pre_lists) {
    unsigned int buffer = 0;
    int bit_count = 0;
    std::string packed;

    for (const auto& pre_list : pre_lists) {
      for (int value : pre_list) {
        if (value < 0 || value >= 32) {
          throw std::runtime_error("canonical_hash requires node indices to fit in 5 bits");
        }

        buffer = (buffer << 5) | static_cast<unsigned int>(value);
        bit_count += 5;

        while (bit_count >= 8) {
          int shift = bit_count - 8;
          packed.push_back(static_cast<char>((buffer >> shift) & 0xffU));
          buffer &= (1U << shift) - 1U;
          bit_count -= 8;
        }
      }
    }

    if (bit_count != 0) {
      packed.push_back(static_cast<char>((buffer << (8 - bit_count)) & 0xffU));
    }

    return packed;
  }

  static std::vector<CandidateRecord> validate_generated_graphs(
    std::vector<Graph> graphs,
    SuperconcentratorCheckMode check_mode = SuperconcentratorCheckMode::Full,
    const std::unordered_set<std::string>* known_records = nullptr
  ) {
    std::vector<CandidateRecord> records;
    for (auto& graph : graphs) {
      if (!graph.is_valid_graph()) {
        continue;
      }
      if (!graph.is_dag()) {
        continue;
      }

      auto canonical_key = graph.canonical_key();
      std::string record = canonical_hash_from_key(canonical_key);
      if (known_records != nullptr && known_records->count(record) != 0) {
        continue;
      }

      std::vector<int> essential_inputs;
      std::vector<int> essential_outputs;
      if (check_mode == SuperconcentratorCheckMode::NewInput) {
        auto inputs = graph.input_nodes();
        if (inputs.empty()) {
          continue;
        }
        essential_inputs.push_back(inputs.back());
      } else if (check_mode == SuperconcentratorCheckMode::NewOutput) {
        auto outputs = graph.output_nodes();
        if (outputs.empty()) {
          continue;
        }
        essential_outputs.push_back(outputs.back());
      }

      if (
        check_mode != SuperconcentratorCheckMode::Assume &&
        !graph.is_superconcentrator(essential_inputs, essential_outputs)
      ) {
        continue;
      }

      records.push_back(CandidateRecord{
        record,
        std::move(record),
      });
    }
    return records;
  }

  static std::size_t worker_count_for(std::size_t task_count) {
    if (task_count <= 1) {
      return task_count;
    }

    std::size_t hardware_count = std::thread::hardware_concurrency();
    if (hardware_count == 0) {
      hardware_count = 1;
    }
    return std::max<std::size_t>(1, std::min(hardware_count, task_count));
  }

  void flush_output_buffer(std::ofstream& out, std::vector<std::string>& output_buffer) const {
    if (output_buffer.empty()) {
      return;
    }

    for (const auto& record : output_buffer) {
      out.write(record.data(), static_cast<std::streamsize>(record.size()));
    }
    out.flush();
    output_buffer.clear();
  }

  void accept_candidate_records(
    std::vector<CandidateRecord> records,
    std::unordered_set<std::string>& hashes,
    std::ofstream& out,
    std::vector<std::string>& output_buffer,
    std::size_t& stored_count
  ) const {
    for (auto& record : records) {
      if (!hashes.insert(std::move(record.hash)).second) {
        continue;
      }

      output_buffer.push_back(std::move(record.record));
      ++stored_count;
      if (output_buffer.size() >= STREAM_GRAPH_FLUSH_SIZE) {
        flush_output_buffer(out, output_buffer);
      }
    }
  }

  static std::string path_identity(const fs::path& path) {
    return fs::absolute(path).lexically_normal().string();
  }

  void load_existing_split_output_hashes(
    int n,
    int m,
    int c,
    std::unordered_set<std::string>& hashes,
    const std::unordered_set<std::string>& excluded_paths = {}
  ) const {
    std::vector<fs::path> load_paths;
    for (const auto& path : split_graph_list_paths(sc_set_dir(n, m, c), "data_split")) {
      if (excluded_paths.count(path_identity(path)) != 0) {
        continue;
      }
      if (!generation_log_exists(path)) {
        continue;
      }
      load_paths.push_back(path);
    }

    if (load_paths.empty()) {
      return;
    }

    std::string progress_desc = "load existing " + std::to_string(n) + "_" +
                                std::to_string(m) + "_" + std::to_string(c);
    std::size_t completed_count = 0;
    std::size_t last_progress_width = 0;
    auto progress_start = std::chrono::steady_clock::now();
    render_progress_line(
      progress_desc,
      completed_count,
      load_paths.size(),
      hashes.size(),
      progress_start,
      last_progress_width,
      false
    );

    for (const auto& path : load_paths) {
      std::ifstream in(path, std::ios::binary);
      if (!in) {
        ++completed_count;
        render_progress_line(
          progress_desc,
          completed_count,
          load_paths.size(),
          hashes.size(),
          progress_start,
          last_progress_width,
          completed_count == load_paths.size()
        );
        continue;
      }
      std::size_t record_size = canonical_hash_record_size(c);
      if (record_size != 0) {
        std::string record(record_size, '\0');
        while (in.read(record.data(), static_cast<std::streamsize>(record.size()))) {
          hashes.insert(record);
        }
      }
      ++completed_count;
      render_progress_line(
        progress_desc,
        completed_count,
        load_paths.size(),
        hashes.size(),
        progress_start,
        last_progress_width,
        completed_count == load_paths.size()
      );
    }
  }

  template <typename WorkerFn>
  void run_parallel_source_graphs(
    const std::string& progress_desc,
    const std::vector<Graph>& source_graphs,
    WorkerFn&& worker,
    std::unordered_set<std::string>& hashes,
    std::ofstream& out,
    std::vector<std::string>& output_buffer,
    std::size_t& stored_count,
    bool show_progress = true
  ) const {
    if (source_graphs.empty()) {
      return;
    }

    std::size_t max_workers = worker_count_for(source_graphs.size());
    std::size_t next_source_index = 0;
    std::size_t completed_count = 0;
    std::size_t last_progress_width = 0;
    auto progress_start = std::chrono::steady_clock::now();
    auto last_render = progress_start;
    std::vector<std::future<std::vector<CandidateRecord>>> futures;
    futures.reserve(max_workers);

    render_progress_line(
      progress_desc,
      completed_count,
      source_graphs.size(),
      stored_count,
      progress_start,
      last_progress_width,
      false,
      show_progress
    );

    auto submit_next = [&]() {
      futures.push_back(std::async(
        std::launch::async,
        worker,
        source_graphs[next_source_index]
      ));
      ++next_source_index;
    };

    while (next_source_index < source_graphs.size() && futures.size() < max_workers) {
      submit_next();
    }

    while (!futures.empty()) {
      std::size_t ready_index = futures.size();
      while (ready_index == futures.size()) {
        for (std::size_t i = 0; i < futures.size(); ++i) {
          if (futures[i].wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            ready_index = i;
            break;
          }
        }
        if (ready_index == futures.size()) {
          auto now = std::chrono::steady_clock::now();
          if (now - last_render >= std::chrono::milliseconds(250)) {
            render_progress_line(
              progress_desc,
              completed_count,
              source_graphs.size(),
              stored_count,
              progress_start,
              last_progress_width,
              false,
              show_progress
            );
            last_render = now;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
      }

      auto records = futures[ready_index].get();
      futures.erase(futures.begin() + static_cast<std::ptrdiff_t>(ready_index));
      accept_candidate_records(std::move(records), hashes, out, output_buffer, stored_count);
      ++completed_count;

      while (next_source_index < source_graphs.size() && futures.size() < max_workers) {
        submit_next();
      }

      bool finished = completed_count == source_graphs.size();
      render_progress_line(
        progress_desc,
        completed_count,
        source_graphs.size(),
        stored_count,
        progress_start,
        last_progress_width,
        finished,
        show_progress
      );
      last_render = std::chrono::steady_clock::now();
    }
  }

  template <typename WorkerFn>
  std::size_t generate_partial(
    int n,
    int m,
    int c,
    int source_n,
    int source_m,
    int source_c,
    std::size_t num,
    const std::string& generation_name,
    WorkerFn worker
  ) {
    if (num == 0) {
      return 0;
    }

    auto start = std::chrono::steady_clock::now();
    bool show_progress = should_print_generation(n, m);
    fs::path output_path = sc_set_path(n, m, c);
    fs::create_directories(output_path.parent_path());

    std::unordered_set<std::string> output_hashes;
    load_record_hashes(output_path, c, output_hashes);
    load_record_hashes(sc_done_path(n, m, c), c, output_hashes);

    std::unordered_set<std::string> source_done_hashes;
    load_record_hashes(sc_done_path(source_n, source_m, source_c), source_c, source_done_hashes);

    std::size_t batch_size = hardware_batch_size();
    std::size_t stored_count = 0;
    std::size_t batch_index = 0;

    auto progress_desc = [&](std::size_t current_batch) {
      return "SC(" + std::to_string(n) + "," +
             std::to_string(m) + "," +
             std::to_string(c) + ") " +
             "batch " +
             std::to_string(current_batch);
    };
    std::size_t progress_index = push_progress_frame(
      progress_desc(batch_index),
      0,
      num,
      stored_count,
      start,
      show_progress
    );

    while (stored_count < num) {
      std::uintmax_t available_source_count = graph_record_count(sc_set_path(source_n, source_m, source_c), source_c);
      while (available_source_count < static_cast<std::uintmax_t>(batch_size)) {
        std::size_t missing_count = static_cast<std::size_t>(
          static_cast<std::uintmax_t>(batch_size) - available_source_count
        );
        std::size_t generated_sources = generate_sc(
          source_n,
          source_m,
          source_c,
          missing_count,
          false,
          false
        );
        std::uintmax_t next_available_source_count =
          graph_record_count(sc_set_path(source_n, source_m, source_c), source_c);

        if (
          next_available_source_count >= static_cast<std::uintmax_t>(batch_size) ||
          (next_available_source_count == available_source_count && generated_sources == 0)
        ) {
          available_source_count = next_available_source_count;
          break;
        }
        available_source_count = next_available_source_count;
      }

      SourceBatch batch = read_source_batch_from_tail(source_n, source_m, source_c, batch_size);
      if (batch.records.empty()) {
        if (show_progress) {
          pop_progress_frame(progress_index);
          std::cout << "No source graphs available for GenerateSC("
                    << n << ", " << m << ", " << c << ").\n";
          progress_index = std::numeric_limits<std::size_t>::max();
        }
        break;
      }

      ++batch_index;
      update_progress_frame(
        progress_index,
        progress_desc(batch_index),
        std::min(stored_count, num),
        num,
        stored_count
      );
      auto out = open_graph_data_for_write_with_retry(output_path, std::ios::binary | std::ios::app, "append");

      std::vector<std::string> output_buffer;
      std::unordered_set<std::string> known_records = output_hashes;
      auto batch_worker = [&](Graph previous_graph) {
        return worker(std::move(previous_graph), known_records);
      };
      run_parallel_source_graphs(
        generation_name + " batch " + std::to_string(batch_index),
        batch.graphs,
        batch_worker,
        output_hashes,
        out,
        output_buffer,
        stored_count,
        false
      );
      flush_output_buffer(out, output_buffer);
      move_source_batch_to_done(source_n, source_m, source_c, batch, source_done_hashes);
      update_progress_frame(
        progress_index,
        progress_desc(batch_index),
        std::min(stored_count, num),
        num,
        stored_count
      );
    }

    double elapsed = elapsed_seconds_since(start);
    write_generation_log(output_path, static_cast<std::size_t>(graph_record_count(output_path, c)), elapsed);
    pop_progress_frame(progress_index);
    return stored_count;
  }

  std::size_t generate_by_one_input(int n, int m, int c) {
    fs::path output_path = sc_set_path(n, m, c);
    bool split_output = has_split_source_for_one_input(n, m, c);
    if (!split_output && fs::exists(output_path) && generation_log_exists(output_path) && !force) {
      return 0;
    }
    if (split_output) {
      fs::create_directories(sc_split_dir(n, m, c));
    }

    std::unordered_set<std::string> hashes;
    std::size_t stored_count = 0;
    bool unsharded_output_initialized = false;
    auto units = one_input_units(n, m, c, split_output);
    if (split_output) {
      std::vector<GenerationUnit> pending_units;
      for (const auto& unit : units) {
        if (source_path_exists_for_unit(unit.source_path) && (force || !generation_log_exists(unit.output_path))) {
          pending_units.push_back(unit);
        }
      }
      if (pending_units.empty()) {
        return 0;
      }
      auto selector = prompt_generation_task_selector(n, m, c, "GenerateSCByOneInput");
      units = select_generation_units(pending_units, selector);

      std::unordered_set<std::string> selected_output_paths;
      for (const auto& unit : units) {
        selected_output_paths.insert(path_identity(unit.output_path));
      }
      load_existing_split_output_hashes(n, m, c, hashes, selected_output_paths);
    }

    for (const auto& unit : units) {
      if (!source_path_exists_for_unit(unit.source_path)) {
        continue;
      }
      if (generation_log_exists(unit.output_path) && !force) {
        continue;
      }

      std::vector<Graph> previous_graphs;
      if (!import_graph_list(unit.previous_n, unit.previous_m, unit.previous_c, unit.source_path, previous_graphs)) {
        continue;
      }

      if (split_output || !unsharded_output_initialized) {
        remove_generation_log(unit.output_path);
        initialize_graph_list_file(unit.output_path);
        unsharded_output_initialized = true;
      }

      auto unit_start = std::chrono::steady_clock::now();
      std::size_t unit_stored_before = stored_count;
      auto out = open_graph_data_for_write_with_retry(unit.output_path, std::ios::binary | std::ios::app, "append");
      std::vector<std::string> output_buffer;
      auto worker = [](Graph previous_graph) {
        return validate_generated_graphs(
          expand_sc_by_one_input(previous_graph),
          SuperconcentratorCheckMode::NewInput
        );
      };
      run_parallel_source_graphs(
        split_output ? unit.output_path.filename().string() : "OneInput",
        previous_graphs,
        worker,
        hashes,
        out,
        output_buffer,
        stored_count
      );
      flush_output_buffer(out, output_buffer);
      last_generation_output_path = unit.output_path;
      if (split_output) {
        write_generation_log(
          unit.output_path,
          stored_count - unit_stored_before,
          elapsed_seconds_since(unit_start)
        );
        generation_unit_logs_written = true;
      }
    }

    return stored_count;
  }

  std::size_t generate_by_one_output(int n, int m, int c) {
    fs::path output_path = sc_set_path(n, m, c);
    bool split_output = has_split_source_for_one_output(n, m, c);
    if (!split_output && fs::exists(output_path) && generation_log_exists(output_path) && !force) {
      return 0;
    }
    if (split_output) {
      fs::create_directories(sc_split_dir(n, m, c));
    }

    std::unordered_set<std::string> hashes;
    std::size_t stored_count = 0;
    bool unsharded_output_initialized = false;
    auto units = one_output_units(n, m, c, split_output);
    if (split_output) {
      std::vector<GenerationUnit> pending_units;
      for (const auto& unit : units) {
        if (source_path_exists_for_unit(unit.source_path) && (force || !generation_log_exists(unit.output_path))) {
          pending_units.push_back(unit);
        }
      }
      if (pending_units.empty()) {
        return 0;
      }
      auto selector = prompt_generation_task_selector(n, m, c, "GenerateSCByOneOutput");
      units = select_generation_units(pending_units, selector);

      std::unordered_set<std::string> selected_output_paths;
      for (const auto& unit : units) {
        selected_output_paths.insert(path_identity(unit.output_path));
      }
      load_existing_split_output_hashes(n, m, c, hashes, selected_output_paths);
    }

    for (const auto& unit : units) {
      if (!source_path_exists_for_unit(unit.source_path)) {
        continue;
      }
      if (generation_log_exists(unit.output_path) && !force) {
        continue;
      }

      std::vector<Graph> previous_graphs;
      if (!import_graph_list(unit.previous_n, unit.previous_m, unit.previous_c, unit.source_path, previous_graphs)) {
        continue;
      }

      if (split_output || !unsharded_output_initialized) {
        remove_generation_log(unit.output_path);
        initialize_graph_list_file(unit.output_path);
        unsharded_output_initialized = true;
      }

      auto unit_start = std::chrono::steady_clock::now();
      std::size_t unit_stored_before = stored_count;
      auto out = open_graph_data_for_write_with_retry(unit.output_path, std::ios::binary | std::ios::app, "append");
      std::vector<std::string> output_buffer;
      auto worker = [](Graph previous_graph) {
        return validate_generated_graphs(
          expand_sc_by_one_output(previous_graph),
          SuperconcentratorCheckMode::NewOutput
        );
      };
      run_parallel_source_graphs(
        split_output ? unit.output_path.filename().string() : "OneOutput",
        previous_graphs,
        worker,
        hashes,
        out,
        output_buffer,
        stored_count
      );
      flush_output_buffer(out, output_buffer);
      last_generation_output_path = unit.output_path;
      if (split_output) {
        write_generation_log(
          unit.output_path,
          stored_count - unit_stored_before,
          elapsed_seconds_since(unit_start)
        );
        generation_unit_logs_written = true;
      }
    }

    return stored_count;
  }

  fs::path data_root;
  fs::path last_generation_output_path;
  std::vector<ProgressFrame> progress_stack;
  std::size_t rendered_progress_stack_lines = 0;
  bool generation_unit_logs_written = false;
  bool force;
  bool quiet;
};

} // namespace solver_cpp
