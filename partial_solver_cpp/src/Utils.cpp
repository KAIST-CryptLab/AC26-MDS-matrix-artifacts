#include "Utils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace solver_cpp {
namespace fs = std::filesystem;

class CompactParser {
public:
  explicit CompactParser(std::string text_) : text(std::move(text_)) {}

  std::vector<std::vector<int>> parse_compact_graph() {
    auto pre_lists = parse_int_array_array();
    skip_ws();
    if (pos != text.size()) {
      throw std::runtime_error("trailing characters in compact graph");
    }
    return pre_lists;
  }

  std::vector<int> parse_int_array() {
    skip_ws();
    expect('[');
    std::vector<int> ret;
    skip_ws();
    if (peek() == ']') {
      ++pos;
      return ret;
    }
    while (true) {
      ret.push_back(parse_int());
      skip_ws();
      char ch = peek();
      if (ch == ',') {
        ++pos;
        continue;
      }
      if (ch == ']') {
        ++pos;
        return ret;
      }
      throw std::runtime_error("expected ',' or ']'");
    }
  }

private:
  std::vector<std::vector<int>> parse_int_array_array() {
    skip_ws();
    expect('[');
    std::vector<std::vector<int>> ret;
    skip_ws();
    if (peek() == ']') {
      ++pos;
      return ret;
    }
    while (true) {
      ret.push_back(parse_int_array());
      skip_ws();
      char ch = peek();
      if (ch == ',') {
        ++pos;
        continue;
      }
      if (ch == ']') {
        ++pos;
        return ret;
      }
      throw std::runtime_error("expected ',' or ']'");
    }
  }

  int parse_int() {
    skip_ws();
    int sign = 1;
    if (peek() == '-') {
      sign = -1;
      ++pos;
    }
    if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos]))) {
      throw std::runtime_error("expected integer");
    }
    int value = 0;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
      value = value * 10 + (text[pos] - '0');
      ++pos;
    }
    return sign * value;
  }

  void skip_ws() {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
      ++pos;
    }
  }

  char peek() const {
    if (pos >= text.size()) {
      return '\0';
    }
    return text[pos];
  }

  void expect(char expected) {
    skip_ws();
    if (peek() != expected) {
      throw std::runtime_error(std::string("expected '") + expected + "'");
    }
    ++pos;
  }

  std::string text;
  std::size_t pos = 0;
};

std::string trim_copy(std::string line) {
  if (line.size() >= 3 &&
      static_cast<unsigned char>(line[0]) == 0xEF &&
      static_cast<unsigned char>(line[1]) == 0xBB &&
      static_cast<unsigned char>(line[2]) == 0xBF) {
    line.erase(0, 3);
  }
  auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
  line.erase(line.begin(), std::find_if(line.begin(), line.end(), not_space));
  line.erase(std::find_if(line.rbegin(), line.rend(), not_space).base(), line.end());
  return line;
}

std::size_t find_matching(const std::string& text, std::size_t open_pos, char open_ch, char close_ch) {
  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (std::size_t i = open_pos; i < text.size(); ++i) {
    char ch = text[i];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        in_string = false;
      }
      continue;
    }
    if (ch == '"') {
      in_string = true;
    } else if (ch == open_ch) {
      ++depth;
    } else if (ch == close_ch) {
      --depth;
      if (depth == 0) {
        return i;
      }
    }
  }
  throw std::runtime_error("unterminated JSON fragment");
}

std::optional<std::size_t> key_colon(const std::string& text, const std::string& key) {
  std::string needle = "\"" + key + "\"";
  std::size_t key_pos = text.find(needle);
  if (key_pos == std::string::npos) {
    return std::nullopt;
  }
  std::size_t colon = text.find(':', key_pos + needle.size());
  if (colon == std::string::npos) {
    throw std::runtime_error("malformed object key: " + key);
  }
  return colon;
}

std::optional<std::vector<int>> parse_array_after_key(const std::string& object, const std::string& key) {
  auto colon = key_colon(object, key);
  if (!colon) {
    return std::nullopt;
  }
  std::size_t array_start = object.find('[', *colon + 1);
  if (array_start == std::string::npos) {
    throw std::runtime_error("expected array for key: " + key);
  }
  std::size_t array_end = find_matching(object, array_start, '[', ']');
  CompactParser parser(object.substr(array_start, array_end - array_start + 1));
  return parser.parse_int_array();
}

int parse_int_after_key(const std::string& object, const std::string& key) {
  auto colon = key_colon(object, key);
  if (!colon) {
    throw std::runtime_error("missing integer key: " + key);
  }
  std::size_t pos = *colon + 1;
  while (pos < object.size() && std::isspace(static_cast<unsigned char>(object[pos]))) {
    ++pos;
  }
  int sign = 1;
  if (object[pos] == '-') {
    sign = -1;
    ++pos;
  }
  if (pos >= object.size() || !std::isdigit(static_cast<unsigned char>(object[pos]))) {
    throw std::runtime_error("expected integer for key: " + key);
  }
  int value = 0;
  while (pos < object.size() && std::isdigit(static_cast<unsigned char>(object[pos]))) {
    value = value * 10 + (object[pos] - '0');
    ++pos;
  }
  return sign * value;
}

std::string parse_string_after_key(const std::string& object, const std::string& key) {
  auto colon = key_colon(object, key);
  if (!colon) {
    throw std::runtime_error("missing string key: " + key);
  }
  std::size_t start = object.find('"', *colon + 1);
  if (start == std::string::npos) {
    throw std::runtime_error("expected string for key: " + key);
  }
  std::string ret;
  bool escaped = false;
  for (std::size_t i = start + 1; i < object.size(); ++i) {
    char ch = object[i];
    if (escaped) {
      ret.push_back(ch);
      escaped = false;
    } else if (ch == '\\') {
      escaped = true;
    } else if (ch == '"') {
      return ret;
    } else {
      ret.push_back(ch);
    }
  }
  throw std::runtime_error("unterminated string for key: " + key);
}

Graph graph_from_compact_data(
  const std::vector<std::vector<int>>& pre_lists,
  int n,
  int m,
  int c
) {
  int total_nodes = static_cast<int>(pre_lists.size());
  if (total_nodes != n + c) {
    throw std::runtime_error("compact graph node count mismatch");
  }
  std::set<int> output_set;
  for (int index = n + c - m; index < n + c; ++index) {
    output_set.insert(index);
  }
  std::vector<Node> nodes;
  nodes.reserve(total_nodes);

  int input_count = 0;
  int inter_count = 0;
  int output_count = 0;
  for (int index = 0; index < total_nodes; ++index) {
    if (index < n) {
      nodes.push_back(Node{INPUT_NODE, "x" + std::to_string(++input_count), {}, {}});
    } else if (output_set.count(index)) {
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
      if (pre < 0 || pre >= total_nodes) {
        throw std::runtime_error("predecessor index out of range");
      }
      nodes[pre].succ.push_back(index);
    }
  }
  return Graph(std::move(nodes));
}

Graph graph_from_dict_line(const std::string& line) {
  std::size_t nodes_key = line.find("\"nodes\"");
  if (nodes_key == std::string::npos) {
    throw std::runtime_error("missing nodes key");
  }
  std::size_t array_start = line.find('[', nodes_key);
  if (array_start == std::string::npos) {
    throw std::runtime_error("missing nodes array");
  }
  std::size_t array_end = find_matching(line, array_start, '[', ']');
  std::string nodes_fragment = line.substr(array_start + 1, array_end - array_start - 1);

  struct ParsedNode {
    int type;
    std::string name;
    std::optional<std::vector<int>> pre;
    std::optional<std::vector<int>> succ;
  };

  std::vector<ParsedNode> parsed;
  std::size_t pos = 0;
  while (pos < nodes_fragment.size()) {
    while (pos < nodes_fragment.size() &&
           (std::isspace(static_cast<unsigned char>(nodes_fragment[pos])) || nodes_fragment[pos] == ',')) {
      ++pos;
    }
    if (pos >= nodes_fragment.size()) {
      break;
    }
    if (nodes_fragment[pos] != '{') {
      throw std::runtime_error("expected node object");
    }
    std::size_t object_end = find_matching(nodes_fragment, pos, '{', '}');
    std::string object = nodes_fragment.substr(pos, object_end - pos + 1);
    auto pre = parse_array_after_key(object, "pre");
    if (!pre) {
      pre = parse_array_after_key(object, "children");
    }
    auto succ = parse_array_after_key(object, "succ");
    if (!succ) {
      succ = parse_array_after_key(object, "parents");
    }
    parsed.push_back(ParsedNode{
      parse_int_after_key(object, "type"),
      parse_string_after_key(object, "name"),
      pre,
      succ,
    });
    pos = object_end + 1;
  }

  std::vector<Node> nodes;
  nodes.reserve(parsed.size());
  for (const auto& item : parsed) {
    nodes.push_back(Node{item.type, item.name, {}, {}});
  }

  bool has_any_pre = false;
  bool has_any_succ = false;
  for (int i = 0; i < static_cast<int>(parsed.size()); ++i) {
    if (parsed[i].pre) {
      has_any_pre = true;
      nodes[i].pre = *parsed[i].pre;
    }
    if (parsed[i].succ) {
      has_any_succ = true;
      nodes[i].succ = *parsed[i].succ;
    }
  }

  if (!has_any_succ && has_any_pre) {
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
      for (int pre : nodes[i].pre) {
        nodes[pre].succ.push_back(i);
      }
    }
  }
  if (!has_any_pre && has_any_succ) {
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
      for (int succ : nodes[i].succ) {
        nodes[succ].pre.push_back(i);
      }
    }
  }

  return Graph(std::move(nodes));
}

Graph graph_from_line(const std::string& raw_line, int n, int m, int c) {
  std::string line = trim_copy(raw_line);
  if (line.empty()) {
    throw std::runtime_error("empty graph line");
  }
  if (line[0] == '{') {
    return graph_from_dict_line(line);
  }
  CompactParser parser(line);
  auto pre_lists = parser.parse_compact_graph();
  return graph_from_compact_data(pre_lists, n, m, c);
}

std::string graph_to_line(Graph graph) {
  auto pre_lists = graph.canonical_key();

  std::ostringstream out;
  out << "[";
  for (int i = 0; i < static_cast<int>(pre_lists.size()); ++i) {
    if (i != 0) {
      out << ",";
    }
    out << "[";
    for (int j = 0; j < static_cast<int>(pre_lists[i].size()); ++j) {
      if (j != 0) {
        out << ",";
      }
      out << pre_lists[i][j];
    }
    out << "]";
  }
  out << "]";
  return out.str();
}

std::size_t canonical_hash_record_size(int c) {
  if (c < 0) {
    throw std::runtime_error("canonical hash record size requires c >= 0");
  }
  return (static_cast<std::size_t>(10) * static_cast<std::size_t>(c) + 7) / 8;
}

Graph graph_from_canonical_hash_record(const std::string& record, int n, int m, int c) {
  std::size_t expected_size = canonical_hash_record_size(c);
  if (record.size() != expected_size) {
    throw std::runtime_error("canonical hash record size mismatch");
  }

  std::vector<std::vector<int>> pre_lists(n + c);
  std::size_t bit_pos = 0;
  auto read_five_bits = [&]() {
    int value = 0;
    for (int i = 0; i < 5; ++i) {
      std::size_t byte_index = bit_pos / 8;
      int shift = 7 - static_cast<int>(bit_pos % 8);
      value = (value << 1) |
              ((static_cast<unsigned char>(record[byte_index]) >> shift) & 1);
      ++bit_pos;
    }
    return value;
  };

  for (int index = n; index < n + c; ++index) {
    pre_lists[index].push_back(read_five_bits());
    pre_lists[index].push_back(read_five_bits());
  }

  return graph_from_compact_data(pre_lists, n, m, c);
}

std::string graph_to_canonical_hash_record(Graph graph) {
  return graph.canonical_hash();
}

std::vector<std::int64_t> get_input_node_values(int count) {
  static const std::vector<std::int64_t> hash_input_node_values = {
    1431485613LL,
    2146637703LL,
    1314045854LL,
    1950891239LL,
    1917371935LL,
    1614743513LL,
    1778992941LL,
    2084809913LL,
    1252230688LL,
    1848586369LL,
    1115475484LL,
    1781188117LL,
    1572278185LL,
    1877236130LL,
    1508471047LL,
    1677540211LL,
  };

  if (count > static_cast<int>(hash_input_node_values.size())) {
    throw std::runtime_error("only up to 16 input hash values are defined");
  }
  return std::vector<std::int64_t>(
    hash_input_node_values.begin(),
    hash_input_node_values.begin() + count
  );
}

bool natural_less(const fs::path& lhs_path, const fs::path& rhs_path) {
  std::string lhs = lhs_path.filename().string();
  std::string rhs = rhs_path.filename().string();
  std::size_t i = 0;
  std::size_t j = 0;

  while (i < lhs.size() && j < rhs.size()) {
    if (std::isdigit(static_cast<unsigned char>(lhs[i])) &&
        std::isdigit(static_cast<unsigned char>(rhs[j]))) {
      std::size_t i0 = i;
      std::size_t j0 = j;
      while (i < lhs.size() && lhs[i] == '0') {
        ++i;
      }
      while (j < rhs.size() && rhs[j] == '0') {
        ++j;
      }
      std::size_t in = i;
      std::size_t jn = j;
      while (in < lhs.size() && std::isdigit(static_cast<unsigned char>(lhs[in]))) {
        ++in;
      }
      while (jn < rhs.size() && std::isdigit(static_cast<unsigned char>(rhs[jn]))) {
        ++jn;
      }
      std::string lhs_num = lhs.substr(i, in - i);
      std::string rhs_num = rhs.substr(j, jn - j);
      if (lhs_num.size() != rhs_num.size()) {
        return lhs_num.size() < rhs_num.size();
      }
      if (lhs_num != rhs_num) {
        return lhs_num < rhs_num;
      }
      if ((in - i0) != (jn - j0)) {
        return (in - i0) < (jn - j0);
      }
      i = in;
      j = jn;
      continue;
    }
    if (lhs[i] != rhs[j]) {
      return lhs[i] < rhs[j];
    }
    ++i;
    ++j;
  }
  return lhs.size() < rhs.size();
}

} // namespace solver_cpp
