# solver_cpp

Implementation of Algorithm 6: GenSC(n,m,c)

Build:

```bash
make
```

Run:

```bash
./solver_cpp 3 3 5
```

By default, generated data is written under `solver_cpp/data`.

The generated files use a compact binary canonical-hash record format:

```text
data/n_m_c/data.bin
```

Some precomputed `data.bin` files are larger than GitHub's 100 MiB file limit, so they are stored as numbered chunks:

```text
data1.bin
data2.bin
...
```

To restore the original file, concatenate the chunks in numeric order and write the result as `data.bin`. For example:

```bash
cat data1.bin data2.bin data3.bin > data.bin
```

On Windows `cmd.exe`, use:

```bat
copy /b data1.bin+data2.bin+data3.bin data.bin
```

Flags:

- `--data-root PATH`: choose the data directory. If omitted, `solver_cpp/data` is used.
- `--force`: regenerate the requested target even when `data.bin` already exists.
- `--quiet`: reduce progress output.

Source layout:

- `src/Node.h`: node constants, node data, hash primitive types.
- `src/Graph.h`, `src/Graph.cpp`: graph structure, graph validation, hashing, max-flow SC checks.
- `src/Utils.h`, `src/Utils.cpp`: compact graph parsing/writing, fixed input hash values, path helpers.
- `src/Algorithms.h`, `src/Algorithms.cpp`: canonical hash, expansion routines, `gen_SC_size_2`.
- `src/MainParallel.h`: `GenSC` orchestration logic.
- `src/main.cpp`: command-line entry point.
