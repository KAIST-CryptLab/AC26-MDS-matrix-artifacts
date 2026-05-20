#include "MainParallel.h"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path default_data_root(const char* argv0) {
  try {
    fs::path exe_dir = fs::absolute(argv0).parent_path();
    if (!exe_dir.empty()) {
      return exe_dir / "data";
    }
  } catch (...) {
  }

  return fs::path("data");
}

struct Args {
  int n = -1;
  int m = -1;
  int c = -1;
  std::size_t num = 1;
  fs::path data_root;
  bool force = false;
  bool quiet = false;
};

void print_usage(const char* argv0) {
  std::cerr << "Usage: " << argv0 << " n m c [num] [--num NUM] [--data-root PATH] [--force] [--quiet]\n";
}

std::size_t parse_positive_size(const std::string& value, const std::string& name) {
  if (value.empty()) {
    throw std::runtime_error(name + " must not be empty");
  }
  for (unsigned char ch : value) {
    if (ch < '0' || ch > '9') {
      throw std::runtime_error(name + " must be a positive integer");
    }
  }

  std::size_t parsed = static_cast<std::size_t>(std::stoull(value));
  if (parsed == 0) {
    throw std::runtime_error(name + " must be positive");
  }
  return parsed;
}

Args parse_args(int argc, char** argv) {
  Args args;
  args.data_root = default_data_root(argv[0]);

  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    std::string value = argv[i];
    if (value == "--data-root") {
      if (i + 1 >= argc) {
        throw std::runtime_error("--data-root requires a path");
      }
      args.data_root = argv[++i];
    } else if (value == "--num") {
      if (i + 1 >= argc) {
        throw std::runtime_error("--num requires a value");
      }
      args.num = parse_positive_size(argv[++i], "num");
    } else if (value == "--force") {
      args.force = true;
    } else if (value == "--quiet") {
      args.quiet = true;
    } else if (value == "--help" || value == "-h") {
      print_usage(argv[0]);
      std::exit(0);
    } else {
      positional.push_back(value);
    }
  }

  if (positional.size() != 3 && positional.size() != 4) {
    print_usage(argv[0]);
    throw std::runtime_error("expected n m c [num]");
  }

  args.n = std::stoi(positional[0]);
  args.m = std::stoi(positional[1]);
  args.c = std::stoi(positional[2]);
  if (positional.size() == 4) {
    args.num = parse_positive_size(positional[3], "num");
  }
  return args;
}

} // namespace

int main(int argc, char** argv) {
  try {
    Args args = parse_args(argc, argv);
    solver_cpp::Solver solver(args.data_root, args.force, args.quiet);
    if (!args.quiet && args.n >= 6 && args.m >= 6) {
      std::cout << "data root: " << args.data_root.string() << "\n";
    }
    solver.generate_sc(args.n, args.m, args.c, args.num, args.force, true);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
}
