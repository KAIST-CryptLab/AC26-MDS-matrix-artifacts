#include "Algorithms.h"

#include "Utils.h"

#include <algorithm>
#include <functional>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace solver_cpp {

using IntSignature = std::vector<int>;

std::vector<int> compress_signatures(const std::vector<IntSignature>& signatures) {
  std::vector<IntSignature> palette = signatures;
  std::sort(palette.begin(), palette.end());
  palette.erase(std::unique(palette.begin(), palette.end()), palette.end());

  std::map<IntSignature, int> index;
  for (int i = 0; i < static_cast<int>(palette.size()); ++i) {
    index[palette[i]] = i;
  }

  std::vector<int> colors(signatures.size());
  for (int i = 0; i < static_cast<int>(signatures.size()); ++i) {
    colors[i] = index[signatures[i]];
  }
  return colors;
}

std::vector<int> node_depths_from_inputs(const Graph& graph) {
  std::vector<int> depths(graph.nodes.size(), -1);
  std::function<int(int)> dfs = [&](int index) -> int {
    if (depths[index] >= 0) {
      return depths[index];
    }
    if (graph.nodes[index].type == INPUT_NODE) {
      depths[index] = 0;
      return 0;
    }
    int best = 0;
    for (int pre : graph.nodes[index].pre) {
      best = std::max(best, dfs(pre) + 1);
    }
    depths[index] = best;
    return best;
  };

  for (int i = 0; i < static_cast<int>(graph.nodes.size()); ++i) {
    dfs(i);
  }
  return depths;
}

std::vector<int> node_depths_to_outputs(const Graph& graph) {
  std::vector<int> depths(graph.nodes.size(), -1);
  std::function<int(int)> dfs = [&](int index) -> int {
    if (depths[index] >= 0) {
      return depths[index];
    }
    if (graph.nodes[index].type == OUTPUT_NODE || graph.nodes[index].succ.empty()) {
      depths[index] = 0;
      return 0;
    }
    int best = 0;
    for (int succ : graph.nodes[index].succ) {
      best = std::max(best, dfs(succ) + 1);
    }
    depths[index] = best;
    return best;
  };

  for (int i = 0; i < static_cast<int>(graph.nodes.size()); ++i) {
    dfs(i);
  }
  return depths;
}

std::string sorted_ints_signature(std::vector<int> values) {
  std::sort(values.begin(), values.end());
  std::ostringstream out;
  out << "[";
  for (int i = 0; i < static_cast<int>(values.size()); ++i) {
    if (i != 0) {
      out << ",";
    }
    out << values[i];
  }
  out << "]";
  return out.str();
}

std::vector<int> refined_node_colors(Graph& graph) {
  graph.reorder(false);
  auto depth_from_inputs = node_depths_from_inputs(graph);
  auto depth_to_outputs = node_depths_to_outputs(graph);

  std::vector<IntSignature> signatures(graph.nodes.size());
  for (int i = 0; i < static_cast<int>(graph.nodes.size()); ++i) {
    signatures[i] = {
      graph.nodes[i].type,
      static_cast<int>(graph.nodes[i].succ.size()),
      static_cast<int>(graph.nodes[i].pre.size()),
      depth_from_inputs[i],
      depth_to_outputs[i],
    };
  }

  auto colors = compress_signatures(signatures);
  while (true) {
    std::vector<IntSignature> refined(graph.nodes.size());
    for (int i = 0; i < static_cast<int>(graph.nodes.size()); ++i) {
      std::vector<int> pre_colors;
      std::vector<int> succ_colors;
      for (int pre : graph.nodes[i].pre) {
        pre_colors.push_back(colors[pre]);
      }
      for (int succ : graph.nodes[i].succ) {
        succ_colors.push_back(colors[succ]);
      }
      std::sort(pre_colors.begin(), pre_colors.end());
      std::sort(succ_colors.begin(), succ_colors.end());

      IntSignature sig;
      sig.reserve(3 + pre_colors.size() + succ_colors.size());
      sig.push_back(colors[i]);
      sig.push_back(-1);
      sig.insert(sig.end(), pre_colors.begin(), pre_colors.end());
      sig.push_back(-2);
      sig.insert(sig.end(), succ_colors.begin(), succ_colors.end());
      refined[i] = std::move(sig);
    }

    auto next_colors = compress_signatures(refined);
    if (next_colors == colors) {
      return colors;
    }
    colors = std::move(next_colors);
  }
}

std::vector<std::vector<int>> ordered_input_classes(Graph& graph) {
  auto colors = refined_node_colors(graph);
  std::map<int, std::vector<int>> classes;
  for (int index : graph.input_nodes()) {
    classes[colors[index]].push_back(index);
  }

  std::vector<std::vector<int>> ret;
  for (auto& [color, items] : classes) {
    (void)color;
    std::sort(items.begin(), items.end(), [&](int lhs, int rhs) {
      auto lhs_key = node_name_key(graph.nodes[lhs].name);
      auto rhs_key = node_name_key(graph.nodes[rhs].name);
      if (lhs_key != rhs_key) {
        return lhs_key < rhs_key;
      }
      return lhs < rhs;
    });
    ret.push_back(items);
  }
  return ret;
}

std::string canonical_graph_hash(Graph graph) {
  return graph.canonical_hash();
}

bool has_directed_path(const Graph& graph, int source, int target) {
  std::vector<char> visited(graph.nodes.size(), 0);
  std::vector<int> stack{source};
  visited[source] = 1;
  while (!stack.empty()) {
    int current = stack.back();
    stack.pop_back();
    if (current == target) {
      return true;
    }
    for (int succ : graph.nodes[current].succ) {
      if (!visited[succ]) {
        visited[succ] = 1;
        stack.push_back(succ);
      }
    }
  }
  return false;
}

bool would_create_cycle_with_new_predecessors(
  const Graph& graph,
  int current_node,
  const std::vector<int>& predecessors
) {
  if (graph.nodes[current_node].succ.empty()) {
    return false;
  }
  for (int predecessor : predecessors) {
    if (has_directed_path(graph, current_node, predecessor)) {
      return true;
    }
  }
  return false;
}

template <typename Fn>
void subset_rec(const std::vector<int>& items, int need, int start, std::vector<int>& current, Fn&& fn) {
  if (static_cast<int>(current.size()) == need) {
    fn(current);
    return;
  }
  for (int i = start; i <= static_cast<int>(items.size()) - (need - static_cast<int>(current.size())); ++i) {
    current.push_back(items[i]);
    subset_rec(items, need, i + 1, current, fn);
    current.pop_back();
  }
}

template <typename Fn>
void for_each_subset(const std::vector<int>& items, Fn&& fn) {
  std::vector<int> current;
  for (int size = 0; size <= static_cast<int>(items.size()); ++size) {
    subset_rec(items, size, 0, current, fn);
  }
}

std::vector<std::pair<Graph, std::string>> add_one_node_to_sc(
  Graph& graph,
  const std::vector<int>& predecessor_candidates,
  const std::string& new_node_name
) {
  std::vector<std::pair<Graph, std::string>> ret;

  for (int predecessor : predecessor_candidates) {
    std::vector<int> predecessor_successors = graph.nodes[predecessor].succ;
    for_each_subset(predecessor_successors, [&](const std::vector<int>& successor_subset) {
      std::vector<int> remaining_indegree_1_successors;
      for (int successor : predecessor_successors) {
        if (!vector_contains(successor_subset, successor) && graph.nodes[successor].pre.size() == 1) {
          remaining_indegree_1_successors.push_back(successor);
        }
      }

      for_each_subset(remaining_indegree_1_successors, [&](const std::vector<int>& extra_successor_subset) {
        int new_node = graph.add_node(INTER_NODE, new_node_name);
        graph.change_succs(predecessor, new_node, successor_subset);
        graph.add_succ(new_node, extra_successor_subset);

        Graph graph_copy = graph;
        ret.push_back({graph_copy, graph_copy.nodes.back().name});

        if (graph.nodes[predecessor].type == OUTPUT_NODE) {
          graph.change_type(predecessor, INTER_NODE);
          graph.change_type(new_node, OUTPUT_NODE);
          Graph swapped_copy = graph;
          ret.push_back({swapped_copy, swapped_copy.nodes.back().name});
          graph.change_type(new_node, INTER_NODE);
          graph.change_type(predecessor, OUTPUT_NODE);
        }

        graph.remove_succ(new_node, extra_successor_subset);
        graph.change_succs_revert(predecessor, new_node, successor_subset);
        graph.remove_last_node(new_node);
      });
    });
  }

  return ret;
}

std::vector<Graph> expand_sc_by_one_input(const Graph& source_graph, int alpha) {
  if (alpha < 2) {
    throw std::runtime_error("alpha must be >= 2");
  }

  std::vector<std::pair<Graph, std::vector<std::string>>> expansion_states;
  expansion_states.push_back({source_graph, {}});

  for (int step = 1; step <= alpha; ++step) {
    std::vector<std::pair<Graph, std::vector<std::string>>> next_states;
    for (auto& [current_graph, added_node_names] : expansion_states) {
      std::vector<int> predecessor_candidates;
      if (step == alpha) {
        predecessor_candidates = current_graph.input_nodes();
      } else {
        predecessor_candidates.resize(current_graph.nodes.size());
        std::iota(predecessor_candidates.begin(), predecessor_candidates.end(), 0);
      }

      auto expanded = add_one_node_to_sc(
        current_graph,
        predecessor_candidates,
        current_graph.next_internal_name()
      );
      for (auto& [expanded_graph, added_node_name] : expanded) {
        auto next_names = added_node_names;
        next_names.push_back(added_node_name);
        next_states.push_back({std::move(expanded_graph), std::move(next_names)});
      }
    }
    expansion_states = std::move(next_states);
  }

  std::vector<Graph> expanded_graphs;
  for (auto& [expanded_graph, added_node_names] : expansion_states) {
    std::vector<std::string> target_names;
    for (const auto& node_name : added_node_names) {
      int index = expanded_graph.find_node_by_name(node_name);
      if (expanded_graph.nodes[index].pre.size() == 1) {
        target_names.push_back(node_name);
      }
    }
    if (target_names.size() < 2) {
      continue;
    }

    Graph candidate = expanded_graph;
    int new_input_index = candidate.add_node(
      INPUT_NODE,
      "x" + std::to_string(static_cast<int>(candidate.input_nodes().size()) + 1)
    );
    std::vector<int> targets;
    for (const auto& node_name : target_names) {
      targets.push_back(candidate.find_node_by_name(node_name));
    }
    candidate.add_succ(new_input_index, targets);
    expanded_graphs.push_back(std::move(candidate));
  }

  return expanded_graphs;
}

std::vector<Graph> expand_sc_by_one_output(const Graph& source_graph) {
  std::vector<Graph> output_expanded_graphs;
  std::string output_name = source_graph.next_output_name();
  int source_node_count = static_cast<int>(source_graph.nodes.size());
  for (int idx1 = 0; idx1 < source_node_count; ++idx1) {
    for (int idx2 = idx1 + 1; idx2 < source_node_count; ++idx2) {
      Graph candidate = source_graph;
      int new_output = candidate.add_node(OUTPUT_NODE, output_name);
      candidate.add_pre(new_output, {idx1, idx2});
      if (!candidate.is_valid_graph()) {
        continue;
      }
      if (!candidate.is_dag()) {
        continue;
      }
      auto outputs = candidate.output_nodes();
      if (outputs.empty() || !candidate.is_superconcentrator({}, {outputs.back()})) {
        continue;
      }
      output_expanded_graphs.push_back(std::move(candidate));
    }
  }
  return output_expanded_graphs;
}

std::vector<Graph> expand_sc_by_internal_nodes(const Graph& source_graph, int alpha) {
  if (alpha < 0) {
    throw std::runtime_error("alpha must be >= 0");
  }

  std::vector<Graph> expanded_graphs;
  std::function<void(Graph, int)> backtrack = [&](Graph current_graph, int remaining_internal_count) {
    if (remaining_internal_count == 0) {
      expanded_graphs.push_back(std::move(current_graph));
      return;
    }

    std::string internal_name = current_graph.next_internal_name();
    int node_count = static_cast<int>(current_graph.nodes.size());
    for (int idx1 = 0; idx1 < node_count; ++idx1) {
      for (int idx2 = idx1 + 1; idx2 < node_count; ++idx2) {
        Graph next_graph = current_graph;
        int new_internal = next_graph.add_node(INTER_NODE, internal_name);
        next_graph.add_pre(new_internal, {idx1, idx2});
        backtrack(std::move(next_graph), remaining_internal_count - 1);
      }
    }
  };

  backtrack(source_graph, alpha);
  return expanded_graphs;
}

Graph make_subgraph(const Graph& graph, const std::vector<int>& selected) {
  std::vector<int> remap(graph.nodes.size(), -1);
  std::vector<Node> nodes;
  nodes.reserve(selected.size());
  for (int old_index : selected) {
    remap[old_index] = static_cast<int>(nodes.size());
    nodes.push_back(Node{graph.nodes[old_index].type, graph.nodes[old_index].name, {}, {}});
  }

  for (int old_index : selected) {
    int new_index = remap[old_index];
    for (int pre : graph.nodes[old_index].pre) {
      if (pre >= 0 && pre < static_cast<int>(remap.size()) && remap[pre] >= 0) {
        nodes[new_index].pre.push_back(remap[pre]);
      }
    }
    for (int succ : graph.nodes[old_index].succ) {
      if (succ >= 0 && succ < static_cast<int>(remap.size()) && remap[succ] >= 0) {
        nodes[new_index].succ.push_back(remap[succ]);
      }
    }
  }

  return Graph(std::move(nodes));
}

std::string degree_signature(const Graph& graph) {
  std::vector<std::string> parts;
  for (const auto& node : graph.nodes) {
    std::vector<int> pre_types;
    std::vector<int> succ_types;
    for (int pre : node.pre) {
      pre_types.push_back(graph.nodes[pre].type);
    }
    for (int succ : node.succ) {
      succ_types.push_back(graph.nodes[succ].type);
    }
    std::ostringstream item;
    item << "(" << node.type << "," << node.pre.size() << "," << node.succ.size()
         << "," << sorted_ints_signature(pre_types)
         << "," << sorted_ints_signature(succ_types) << ")";
    parts.push_back(item.str());
  }
  std::sort(parts.begin(), parts.end());

  std::ostringstream out;
  for (const auto& part : parts) {
    out << part;
  }
  return out.str();
}

std::string size_2_partial_state_signature(
  const Graph& graph,
  const std::vector<int>& input_nodes,
  const std::vector<int>& inter_nodes,
  const std::vector<int>& output_nodes,
  int next_index
) {
  int assigned_internal_count = std::min(std::max(next_index - 2, 0), static_cast<int>(inter_nodes.size()));
  std::vector<int> active_nodes = input_nodes;
  for (int i = 0; i < assigned_internal_count; ++i) {
    active_nodes.push_back(inter_nodes[i]);
  }
  active_nodes.push_back(output_nodes[0]);

  Graph partial = make_subgraph(graph, active_nodes);
  HashValue h12 = std::min(partial.get_hash({1, 2}), partial.get_hash({2, 1}));
  HashValue h17 = std::min(partial.get_hash({17, 31}), partial.get_hash({31, 17}));

  std::ostringstream out;
  out << next_index << "|"
      << hash_to_string(h12) << "|"
      << hash_to_string(h17) << "|"
      << degree_signature(partial);
  return out.str();
}

std::vector<Graph> gen_sc_size_2(int num_gates) {
  if (num_gates < 2) {
    throw std::runtime_error("num_gates must be >= 2");
  }

  if (num_gates == 2) {
    Graph g1;
    int x1 = g1.add_node(INPUT_NODE, "x1");
    int x2 = g1.add_node(INPUT_NODE, "x2");
    int y1 = g1.add_node(OUTPUT_NODE, "y1");
    int y2 = g1.add_node(OUTPUT_NODE, "y2");
    g1.add_pre(y1, {x1, x2});
    g1.add_pre(y2, {x1, x2});

    Graph g2;
    x1 = g2.add_node(INPUT_NODE, "x1");
    x2 = g2.add_node(INPUT_NODE, "x2");
    y1 = g2.add_node(OUTPUT_NODE, "y1");
    y2 = g2.add_node(OUTPUT_NODE, "y2");
    g2.add_pre(y1, {x1, x2});
    g2.add_pre(y2, {x1, y1});
    return {g1, g2};
  }

  int total_nodes = num_gates + 2;
  std::vector<std::vector<int>> pre_lists(total_nodes);
  std::vector<Graph> graphs;
  std::unordered_set<std::string> hashes;

  auto make_candidate = [&](const std::vector<int>& output_indices) {
    std::vector<char> is_output(total_nodes, 0);
    for (int index : output_indices) {
      is_output[index] = 1;
    }

    std::vector<Node> nodes;
    nodes.reserve(total_nodes);
    int input_count = 0;
    int inter_count = 0;
    int output_count = 0;
    for (int index = 0; index < total_nodes; ++index) {
      if (index < 2) {
        nodes.push_back(Node{INPUT_NODE, "x" + std::to_string(++input_count), {}, {}});
      } else if (is_output[index]) {
        nodes.push_back(Node{OUTPUT_NODE, "y" + std::to_string(++output_count), {}, {}});
      } else {
        nodes.push_back(Node{INTER_NODE, "w" + std::to_string(++inter_count), {}, {}});
      }
    }

    for (int index = 0; index < total_nodes; ++index) {
      nodes[index].pre = pre_lists[index];
    }
    for (int index = 0; index < total_nodes; ++index) {
      for (int pre : nodes[index].pre) {
        nodes[pre].succ.push_back(index);
      }
    }

    return Graph(std::move(nodes));
  };

  std::function<void(int)> backtrack = [&](int index) {
    if (index == total_nodes) {
      for (int first_output = 2; first_output < total_nodes; ++first_output) {
        for (int second_output = first_output + 1; second_output < total_nodes; ++second_output) {
          Graph candidate = make_candidate({first_output, second_output});
          if (!candidate.is_valid_graph()) {
            continue;
          }
          if (!candidate.is_dag()) {
            continue;
          }
          if (!candidate.is_superconcentrator()) {
            continue;
          }
          std::string graph_hash = canonical_graph_hash(candidate);
          if (hashes.insert(graph_hash).second) {
            graphs.push_back(std::move(candidate));
          }
        }
      }
      return;
    }

    for (int first_pre = 0; first_pre < index; ++first_pre) {
      for (int second_pre = first_pre + 1; second_pre < index; ++second_pre) {
        pre_lists[index] = {first_pre, second_pre};
        backtrack(index + 1);
        pre_lists[index].clear();
      }
    }
  };

  backtrack(2);
  return graphs;
}

} // namespace solver_cpp
