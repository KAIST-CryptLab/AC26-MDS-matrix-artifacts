# MDS-matrix

Current working layout:

- `graph_data/`: graph template data used by `improve_prime` and `improve_binary`.
- `solver_cpp/`: C++ implementation of Algorithm 6, `GenSC(n,m,c)`, with precomputed `SC(n,m,c)` graph data under `solver_cpp/data`.
- `partial_solver_cpp/`: C++ partial-search implementation derived from `solver_cpp`, used for selected large `SC(n,m,c)` targets such as the `SC(8,8,25)` search path.
- `improve_prime/`: C++ search tool, long-running experiment script, and collected result file for prime-field coefficient improvement.
- `improve_binary/`: C++ search tool, long-running experiment script, collected result file, and MILP post-optimizer for binary-field coefficient and sparse `L` improvement.
- `verify/`: standalone Sage scripts for prime-field and k-bit word MDS verification.
