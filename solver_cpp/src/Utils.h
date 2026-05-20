#pragma once

#include "Graph.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace solver_cpp {

std::string trim_copy(std::string line);
Graph graph_from_line(const std::string& raw_line, int n, int m, int c);
std::string graph_to_line(Graph graph);
std::size_t canonical_hash_record_size(int c);
Graph graph_from_canonical_hash_record(const std::string& record, int n, int m, int c);
std::string graph_to_canonical_hash_record(Graph graph);
std::vector<std::int64_t> get_input_node_values(int count);
bool natural_less(const std::filesystem::path& lhs_path, const std::filesystem::path& rhs_path);

} // namespace solver_cpp
