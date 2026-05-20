#include "Graph.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <queue>
#include <vector>

namespace solver_cpp {

using IntSignature = std::vector<int>;

std::vector<int> compress_color_signatures(const std::vector<IntSignature>& signatures) {
  std::vector<IntSignature> palette = signatures;
  std::sort(palette.begin(), palette.end());
  palette.erase(std::unique(palette.begin(), palette.end()), palette.end());

  std::map<IntSignature, int> color_index;
  for (int i = 0; i < static_cast<int>(palette.size()); ++i) {
    color_index[palette[i]] = i;
  }

  std::vector<int> colors(signatures.size());
  for (int i = 0; i < static_cast<int>(signatures.size()); ++i) {
    colors[i] = color_index[signatures[i]];
  }
  return colors;
}

std::vector<int> node_depths_from_inputs_for_key(const Graph& graph) {
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

std::vector<int> node_depths_to_outputs_for_key(const Graph& graph) {
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

std::vector<int> refined_colors_for_key(const Graph& graph) {
  auto depths_from_inputs = node_depths_from_inputs_for_key(graph);
  auto depths_to_outputs = node_depths_to_outputs_for_key(graph);

  std::vector<IntSignature> signatures(graph.nodes.size());
  for (int i = 0; i < static_cast<int>(graph.nodes.size()); ++i) {
    signatures[i] = {
      graph.nodes[i].type,
      static_cast<int>(graph.nodes[i].succ.size()),
      static_cast<int>(graph.nodes[i].pre.size()),
      depths_from_inputs[i],
      depths_to_outputs[i],
    };
  }

  auto colors = compress_color_signatures(signatures);
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

    auto next_colors = compress_color_signatures(refined);
    if (next_colors == colors) {
      return colors;
    }
    colors = std::move(next_colors);
  }
}

std::vector<std::vector<int>> canonical_node_classes_for_key(const Graph& graph) {
  auto colors = refined_colors_for_key(graph);
  std::vector<std::vector<int>> ordered_classes;

  for (int type : {INPUT_NODE, INTER_NODE, OUTPUT_NODE}) {
    std::map<int, std::vector<int>> classes;
    for (int i = 0; i < static_cast<int>(graph.nodes.size()); ++i) {
      if (graph.nodes[i].type == type) {
        classes[colors[i]].push_back(i);
      }
    }

    for (auto& [color, nodes] : classes) {
      (void)color;
      std::sort(nodes.begin(), nodes.end(), [&](int lhs, int rhs) {
        auto lhs_key = node_name_key(graph.nodes[lhs].name);
        auto rhs_key = node_name_key(graph.nodes[rhs].name);
        if (lhs_key != rhs_key) {
          return lhs_key < rhs_key;
        }
        return lhs < rhs;
      });
      ordered_classes.push_back(nodes);
    }
  }

  return ordered_classes;
}

std::vector<std::vector<int>> Graph::canonical_key() {
  if (!reorder(false)) {
    throw std::runtime_error("canonical_key requires a DAG");
  }

  auto node_classes = canonical_node_classes_for_key(*this);
  std::vector<std::vector<int>> current_classes(node_classes.size());
  std::vector<std::vector<int>> best_key;
  bool has_best = false;

  std::function<void(int)> recurse = [&](int class_index) {
    if (class_index == static_cast<int>(node_classes.size())) {
      std::vector<int> ordered_nodes;
      ordered_nodes.reserve(nodes.size());
      for (const auto& node_class : current_classes) {
        ordered_nodes.insert(ordered_nodes.end(), node_class.begin(), node_class.end());
      }

      std::vector<int> node_index(nodes.size(), -1);
      for (int i = 0; i < static_cast<int>(ordered_nodes.size()); ++i) {
        node_index[ordered_nodes[i]] = i;
      }

      std::vector<std::vector<int>> key;
      key.reserve(ordered_nodes.size());
      for (int node : ordered_nodes) {
        std::vector<int> pre;
        pre.reserve(nodes[node].pre.size());
        for (int predecessor : nodes[node].pre) {
          pre.push_back(node_index[predecessor]);
        }
        std::sort(pre.begin(), pre.end());
        key.push_back(std::move(pre));
      }

      if (!has_best || key < best_key) {
        best_key = std::move(key);
        has_best = true;
      }
      return;
    }

    auto block = node_classes[class_index];
    std::sort(block.begin(), block.end());
    do {
      current_classes[class_index] = block;
      recurse(class_index + 1);
    } while (std::next_permutation(block.begin(), block.end()));
  };

  recurse(0);
  if (!has_best) {
    throw std::runtime_error("failed to compute canonical key");
  }
  return best_key;
}

std::string Graph::canonical_hash() {
  auto pre_lists = canonical_key();
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

struct Dinic {
  struct Edge {
    int to;
    int rev;
    int cap;
    int initial_cap;
  };

  explicit Dinic(int n) : graph(n), level(n), it(n) {}

  int add_edge(int from, int to, int cap) {
    int edge_index = static_cast<int>(graph[from].size());
    Edge fwd{to, static_cast<int>(graph[to].size()), cap, cap};
    Edge rev{from, edge_index, 0, 0};
    graph[from].push_back(fwd);
    graph[to].push_back(rev);
    return edge_index;
  }

  void reset_caps() {
    for (auto& edges : graph) {
      for (auto& edge : edges) {
        edge.cap = edge.initial_cap;
      }
    }
  }

  void set_edge_cap(int from, int edge_index, int cap) {
    graph[from][edge_index].cap = cap;
  }

  bool bfs(int source, int sink) {
    std::fill(level.begin(), level.end(), -1);
    std::queue<int> queue;
    level[source] = 0;
    queue.push(source);
    while (!queue.empty()) {
      int current = queue.front();
      queue.pop();
      for (const auto& edge : graph[current]) {
        if (edge.cap > 0 && level[edge.to] < 0) {
          level[edge.to] = level[current] + 1;
          queue.push(edge.to);
        }
      }
    }
    return level[sink] >= 0;
  }

  int dfs(int vertex, int sink, int flow) {
    if (vertex == sink) {
      return flow;
    }
    for (int& i = it[vertex]; i < static_cast<int>(graph[vertex].size()); ++i) {
      Edge& edge = graph[vertex][i];
      if (edge.cap <= 0 || level[edge.to] != level[vertex] + 1) {
        continue;
      }
      int pushed = dfs(edge.to, sink, std::min(flow, edge.cap));
      if (pushed == 0) {
        continue;
      }
      edge.cap -= pushed;
      graph[edge.to][edge.rev].cap += pushed;
      return pushed;
    }
    return 0;
  }

  int max_flow(int source, int sink, int limit) {
    int flow = 0;
    constexpr int INF = std::numeric_limits<int>::max() / 4;
    while (flow < limit && bfs(source, sink)) {
      std::fill(it.begin(), it.end(), 0);
      while (flow < limit) {
        int pushed = dfs(source, sink, INF);
        if (pushed == 0) {
          break;
        }
        flow += pushed;
      }
    }
    return flow;
  }

  std::vector<std::vector<Edge>> graph;
  std::vector<int> level;
  std::vector<int> it;
};

template <typename Fn>
void combinations_rec(const std::vector<int>& items, int need, int start, std::vector<int>& current, Fn&& fn) {
  if (static_cast<int>(current.size()) == need) {
    fn(current);
    return;
  }
  for (int i = start; i <= static_cast<int>(items.size()) - (need - static_cast<int>(current.size())); ++i) {
    current.push_back(items[i]);
    combinations_rec(items, need, i + 1, current, fn);
    current.pop_back();
  }
}

template <typename Fn>
void for_each_combination(const std::vector<int>& items, int need, Fn&& fn) {
  std::vector<int> current;
  combinations_rec(items, need, 0, current, std::forward<Fn>(fn));
}

struct VertexDisjointPathChecker {
  explicit VertexDisjointPathChecker(const Graph& graph)
    : n(static_cast<int>(graph.nodes.size())),
      source(2 * n),
      sink(2 * n + 1),
      dinic(2 * n + 2),
      source_edge_indices(n, -1),
      sink_edge_indices(n, -1) {
    int inf_cap = n + 5;

    for (int i = 0; i < n; ++i) {
      dinic.add_edge(2 * i, 2 * i + 1, 1);
    }
    for (int u = 0; u < n; ++u) {
      for (int v : graph.nodes[u].succ) {
        if (v >= 0 && v < n) {
          dinic.add_edge(2 * u + 1, 2 * v, inf_cap);
        }
      }
    }
    for (int i = 0; i < n; ++i) {
      source_edge_indices[i] = dinic.add_edge(source, 2 * i, 0);
      sink_edge_indices[i] = dinic.add_edge(2 * i + 1, sink, 0);
    }
  }

  bool check(const std::vector<int>& xs, const std::vector<int>& ys, int k) {
    dinic.reset_caps();
    for (int x : xs) {
      dinic.set_edge_cap(source, source_edge_indices[x], 1);
    }
    for (int y : ys) {
      dinic.set_edge_cap(2 * y + 1, sink_edge_indices[y], 1);
    }
    return dinic.max_flow(source, sink, k) >= k;
  }

  int n;
  int source;
  int sink;
  Dinic dinic;
  std::vector<int> source_edge_indices;
  std::vector<int> sink_edge_indices;
};

bool Graph::is_superconcentrator(
  const std::vector<int>& essential_inputs,
  const std::vector<int>& essential_outputs
) const {
  if (!is_valid_graph()) {
    return false;
  }
  auto inputs = input_nodes();
  auto outputs = output_nodes();
  int max_k = std::min(inputs.size(), outputs.size());
  bool check_all = essential_inputs.empty() && essential_outputs.empty();
  VertexDisjointPathChecker path_checker(*this);

  auto contains_any = [](const std::vector<int>& values, const std::vector<int>& essentials) {
    for (int essential : essentials) {
      if (vector_contains(values, essential)) {
        return true;
      }
    }
    return false;
  };

  for (int k = 1; k <= max_k; ++k) {
    bool ok = true;
    for_each_combination(inputs, k, [&](const std::vector<int>& xs) {
      if (!ok) {
        return;
      }
      for_each_combination(outputs, k, [&](const std::vector<int>& ys) {
        bool necessary = check_all ||
                         contains_any(xs, essential_inputs) ||
                         contains_any(ys, essential_outputs);
        if (ok && necessary && !path_checker.check(xs, ys, k)) {
          ok = false;
        }
      });
    });
    if (!ok) {
      return false;
    }
  }
  return true;
}

} // namespace solver_cpp
