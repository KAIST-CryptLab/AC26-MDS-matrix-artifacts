# partial_solver_cpp

Partial-search implementation for finding selected elements of SC(n,m,c), especially the search path used for SC(8,8,25).

This program is derived from `solver_cpp`, but it does not try to enumerate every graph in a target family. Instead, each target directory keeps two binary queues:

```text
data/n_m_c/data.bin
data/n_m_c/data_done.bin
```

`data.bin` stores graphs that may still be used to generate the next level. `data_done.bin` stores graphs that have already been consumed as sources. Records are deduplicated by canonical hash.

Build:

```bash
make
```

Run:

```bash
./partial_solver_cpp 8 8 25
```

An optional fourth positional argument or `--num NUM` asks the program to keep generating until at least `NUM` new target graphs are stored:

```bash
./partial_solver_cpp 8 8 25 10
./partial_solver_cpp 8 8 25 --num 10
```

By default, generated data is written under `partial_solver_cpp/data`.

The generated files use the compact binary canonical-hash record format. For a graph with size parameter `c`, each record has

```text
(10*c + 7) / 8 bytes
```

Flags:

- `--data-root PATH`: choose the data directory. If omitted, `partial_solver_cpp/data` is used.
- `--force`: regenerate the requested target even when data already exists.
- `--quiet`: reduce progress output.

Algorithm notes:

- One-input partial generation uses the current paper version of `ExpSCByInput`: with `alpha = 2`, the candidate predecessor set is all nodes in the first reverse step and the input nodes in the last reverse step.
- In the one-output step, this partial-search implementation only extends graphs from `SC(n,m-1,c-1)` by adding one output node.

Source layout:

- `src/Node.h`: node constants, node data, hash primitive types.
- `src/Graph.h`, `src/Graph.cpp`: graph structure, graph validation, hashing, max-flow SC checks.
- `src/Utils.h`, `src/Utils.cpp`: compact graph parsing/writing, canonical binary record helpers, fixed input hash values.
- `src/Algorithms.h`, `src/Algorithms.cpp`: canonical hash, expansion routines, `gen_SC_size_2`.
- `src/MainParallel.h`: partial `GenSC` orchestration logic.
- `src/main.cpp`: command-line entry point.
