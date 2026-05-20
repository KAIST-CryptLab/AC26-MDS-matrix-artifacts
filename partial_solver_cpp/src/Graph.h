#pragma once

#include "Node.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace solver_cpp {

class Graph {
public:
  std::vector<Node> nodes;

  Graph() = default;
  explicit Graph(std::vector<Node> nodes_) : nodes(std::move(nodes_)) {}

  std::vector<int> input_nodes() const { return nodes_of_type(INPUT_NODE); }
  std::vector<int> inter_nodes() const { return nodes_of_type(INTER_NODE); }
  std::vector<int> output_nodes() const { return nodes_of_type(OUTPUT_NODE); }

  int add_node(int type, const std::string& name) {
    if (!is_node_type(type)) {
      throw std::runtime_error("invalid node type");
    }
    nodes.push_back(Node{type, name, {}, {}});
    return static_cast<int>(nodes.size()) - 1;
  }

  void remove_last_node(int index) {
    if (index != static_cast<int>(nodes.size()) - 1) {
      throw std::runtime_error("only last-node removal is supported");
    }
    if (!nodes[index].pre.empty() || !nodes[index].succ.empty()) {
      throw std::runtime_error("cannot remove a linked node");
    }
    nodes.pop_back();
  }

  void change_type(int index, int new_type) {
    if (!is_node_type(new_type)) {
      throw std::runtime_error("invalid node type");
    }
    nodes[index].type = new_type;
  }

  void add_edge(int pre, int succ) {
    if (pre == succ) {
      throw std::runtime_error("self edge is not allowed");
    }
    if (vector_contains(nodes[pre].succ, succ) || vector_contains(nodes[succ].pre, pre)) {
      throw std::runtime_error("duplicate edge");
    }
    nodes[pre].succ.push_back(succ);
    nodes[succ].pre.push_back(pre);
  }

  void remove_edge(int pre, int succ) {
    erase_one(nodes[pre].succ, succ);
    erase_one(nodes[succ].pre, pre);
  }

  void add_pre(int node, const std::vector<int>& predecessors) {
    for (int pre : predecessors) {
      add_edge(pre, node);
    }
  }

  void remove_pre(int node, const std::vector<int>& predecessors) {
    for (int pre : predecessors) {
      remove_edge(pre, node);
    }
  }

  void add_succ(int node, const std::vector<int>& successors) {
    for (int succ : successors) {
      add_edge(node, succ);
    }
  }

  void remove_succ(int node, const std::vector<int>& successors) {
    for (int succ : successors) {
      remove_edge(node, succ);
    }
  }

  void change_succs(int cur, int nxt, const std::vector<int>& succ_subset) {
    add_edge(cur, nxt);
    for (int succ : succ_subset) {
      remove_edge(cur, succ);
    }
    for (int succ : succ_subset) {
      add_edge(nxt, succ);
    }
  }

  void change_succs_revert(int cur, int nxt, const std::vector<int>& succ_subset) {
    remove_edge(cur, nxt);
    for (int succ : succ_subset) {
      add_edge(cur, succ);
    }
    for (int succ : succ_subset) {
      remove_edge(nxt, succ);
    }
  }

  int find_node_by_name(const std::string& name) const {
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
      if (nodes[i].name == name) {
        return i;
      }
    }
    throw std::runtime_error("node not found: " + name);
  }

  std::string next_internal_name() const {
    return "w" + std::to_string(static_cast<int>(inter_nodes().size() + output_nodes().size()) + 1);
  }

  std::string next_output_name() const {
    return "y" + std::to_string(static_cast<int>(output_nodes().size()) + 1);
  }

  bool is_valid_graph() const {
    for (const auto& node : nodes) {
      if (!is_node_type(node.type)) {
        return false;
      }
    }

    for (int index : input_nodes()) {
      if (!nodes[index].pre.empty()) {
        return false;
      }
    }
    for (int index : inter_nodes()) {
      if (nodes[index].pre.size() != 2 || nodes[index].pre[0] == nodes[index].pre[1]) {
        return false;
      }
    }
    for (int index : output_nodes()) {
      if (nodes[index].pre.size() != 2 || nodes[index].pre[0] == nodes[index].pre[1]) {
        return false;
      }
    }

    for (int index = 0; index < static_cast<int>(nodes.size()); ++index) {
      std::unordered_set<int> seen;
      for (int pre : nodes[index].pre) {
        if (pre < 0 || pre >= static_cast<int>(nodes.size())) {
          return false;
        }
        if (!seen.insert(pre).second) {
          return false;
        }
        if (!vector_contains(nodes[pre].succ, index)) {
          return false;
        }
      }
      for (int succ : nodes[index].succ) {
        if (succ < 0 || succ >= static_cast<int>(nodes.size())) {
          return false;
        }
        if (!seen.insert(succ).second) {
          return false;
        }
        if (!vector_contains(nodes[succ].pre, index)) {
          return false;
        }
      }
    }

    return true;
  }

  bool reorder(bool only_dag_check = false) {
    std::queue<int> queue;
    std::unordered_map<int, int> indeg;
    auto inputs = input_nodes();

    for (int i = 0; i < static_cast<int>(inputs.size()); ++i) {
      if (!only_dag_check) {
        nodes[inputs[i]].name = "x" + std::to_string(i + 1);
      }
      queue.push(inputs[i]);
    }

    int inter_index = 1;
    int output_index = 1;
    int visited_count = 0;

    while (!queue.empty()) {
      int cur = queue.front();
      queue.pop();
      ++visited_count;
      for (int nxt : nodes[cur].succ) {
        int seen = ++indeg[nxt];
        if (seen != static_cast<int>(nodes[nxt].pre.size())) {
          continue;
        }
        if (!only_dag_check) {
          if (nodes[nxt].type == INTER_NODE) {
            nodes[nxt].name = "w" + std::to_string(inter_index++);
          } else {
            nodes[nxt].name = "y" + std::to_string(output_index++);
          }
        }
        queue.push(nxt);
      }
    }

    if (visited_count != static_cast<int>(nodes.size())) {
      return false;
    }

    sort_nodes_by_name();
    return true;
  }

  bool is_dag() { return reorder(true); }

  std::vector<std::vector<int>> canonical_key();
  std::string canonical_hash();
  
  HashValue get_hash(const std::vector<std::int64_t>& input_values) const {
    constexpr std::int64_t m1 = 31;
    constexpr std::int64_t m2 = 991;

    auto inputs = input_nodes();
    if (input_values.size() != inputs.size()) {
      throw std::runtime_error("input hash value count mismatch");
    }

    std::vector<HashValue> memo(nodes.size());
    std::vector<char> has_value(nodes.size(), 0);

    auto normalize = [](std::int64_t value) {
      HashValue ret{};
      for (int i = 0; i < 4; ++i) {
        std::int64_t modded = value % HASH_PRIMES[i];
        if (modded < 0) {
          modded += HASH_PRIMES[i];
        }
        ret[i] = modded;
      }
      return ret;
    };

    for (int i = 0; i < static_cast<int>(inputs.size()); ++i) {
      memo[inputs[i]] = normalize(input_values[i]);
      has_value[inputs[i]] = 1;
    }

    std::function<HashValue(int)> node_value = [&](int index) -> HashValue {
      if (has_value[index]) {
        return memo[index];
      }
      std::vector<HashValue> pre_values;
      pre_values.reserve(nodes[index].pre.size());
      for (int pre : nodes[index].pre) {
        pre_values.push_back(node_value(pre));
      }
      HashValue ret = combine_values(pre_values, m1);
      memo[index] = ret;
      has_value[index] = 1;
      return ret;
    };

    for (int index : inter_nodes()) {
      node_value(index);
    }
    for (int index : output_nodes()) {
      node_value(index);
    }

    std::vector<HashValue> all_values;
    for (int index : input_nodes()) {
      all_values.push_back(node_value(index));
    }
    for (int index : inter_nodes()) {
      all_values.push_back(node_value(index));
    }
    for (int index : output_nodes()) {
      all_values.push_back(node_value(index));
    }
    return combine_values(all_values, m2);
  }

  bool is_superconcentrator(
    const std::vector<int>& essential_inputs = {},
    const std::vector<int>& essential_outputs = {}
  ) const;

private:
  static bool is_node_type(int type) {
    return type == INPUT_NODE || type == INTER_NODE || type == OUTPUT_NODE;
  }

  std::vector<int> nodes_of_type(int type) const {
    std::vector<int> ret;
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
      if (nodes[i].type == type) {
        ret.push_back(i);
      }
    }
    return ret;
  }

  static HashValue combine_values(std::vector<HashValue> values, std::int64_t multiplier) {
    std::sort(values.begin(), values.end());
    HashValue ret{0, 0, 0, 0};
    for (const auto& value : values) {
      for (int i = 0; i < 4; ++i) {
        ret[i] = (multiplier * ret[i] + value[i]) % HASH_PRIMES[i];
      }
    }
    return ret;
  }

  void sort_nodes_by_name() {
    std::vector<int> order(nodes.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
      auto lhs_key = node_name_key(nodes[lhs].name);
      auto rhs_key = node_name_key(nodes[rhs].name);
      if (lhs_key != rhs_key) {
        return lhs_key < rhs_key;
      }
      if (nodes[lhs].name != nodes[rhs].name) {
        return nodes[lhs].name < nodes[rhs].name;
      }
      return lhs < rhs;
    });

    std::vector<int> remap(nodes.size());
    for (int new_index = 0; new_index < static_cast<int>(order.size()); ++new_index) {
      remap[order[new_index]] = new_index;
    }

    std::vector<Node> sorted_nodes(nodes.size());
    for (int new_index = 0; new_index < static_cast<int>(order.size()); ++new_index) {
      sorted_nodes[new_index] = nodes[order[new_index]];
    }

    for (auto& node : sorted_nodes) {
      for (int& pre : node.pre) {
        pre = remap[pre];
      }
      for (int& succ : node.succ) {
        succ = remap[succ];
      }
    }

    nodes = std::move(sorted_nodes);
  }
};

} // namespace solver_cpp
