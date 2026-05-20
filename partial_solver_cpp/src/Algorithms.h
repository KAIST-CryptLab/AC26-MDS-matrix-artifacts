#pragma once

#include "Graph.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace solver_cpp {

using Size2ProgressCallback = std::function<void(std::uint64_t, std::uint64_t, std::size_t)>;

std::string canonical_graph_hash(Graph graph);
std::vector<Graph> expand_sc_by_one_input(const Graph& source_graph);
std::vector<Graph> expand_sc_by_one_output(const Graph& source_graph);
std::vector<Graph> expand_sc_by_internal_nodes(const Graph& source_graph, int alpha);
std::vector<Graph> gen_sc_size_2(
  int num_gates,
  const Size2ProgressCallback& progress_callback = {}
);

} // namespace solver_cpp
