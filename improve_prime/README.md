# improve_prime

C++ search tool for improving prime-field coefficient choices.

## Build

```sh
cd improve_prime
make
```

After compiling, run the long-running experiment script from this directory:

```sh
./improve_prime.sh
```

The script runs `./improve_prime`, reads graph templates from the repository
`graph_data/` directory,
and appends logs to `improve_prime_experiment.txt`.  The collected best results
are stored in `improve_prime_result.txt`.

The examples below assume your current directory is `improve_prime/`.

## Basic Commands

- Random graph search:

```sh
./improve_prime --t 5 --attempts 10000 --seed 1
```

- Fixed graph search:

```sh
./improve_prime --t 5 --graph-index 37 --attempts 10000
```

- Custom coefficient sampling bag:

```sh
./improve_prime --t 7 --attempts 1000000 --values "1,1,1,1,2,2,2,3" --seed 26
```

- Use a non-default graph-size file:

```sh
./improve_prime --t 7 --graph-c 22 --attempts 100000
```

- Use an explicit graph file:

```sh
./improve_prime --t 7 --graph-file ../graph_data/7_7_22.bin --attempts 100000
```

## Options

- `--t T`: target matrix size. Required in practice. If omitted, the program tries `t=-1` and fails when loading graph data.

- `--attempts N`: number of random coefficient attempts. Default: `10000`.

- `--seed N`: RNG seed. Default: `1`.

- `--values LIST`: coefficient sampling bag. The list may be comma-separated or space-separated. Default: `"1,1,1,2,2,3"`.

- `--graph-root PATH`: optional override for the graph template directory. By default, the program automatically finds the repository `graph_data/` directory.

- `--graph-c C`: use graph file with non-default gate count `C`, resolved under `--graph-root`.

- `--graph-file PATH`: use this exact graph file. Cannot be combined with `--graph-c`.

- `--graph-index I`: fix the graph template index to `I`. Without this option, each attempt samples `graph_index` uniformly from all available graph templates.

## Output

- `best attempt=...`: printed whenever a better MDS matrix is found.

- `matrix:`: final best prime-field matrix.

- `graph_index=...`: graph template index used by the final best matrix.

- `coeffs=[...]`: coefficient pairs for each gate, in graph-node order.

The score minimized is lexicographic:

1. maximum absolute matrix entry,
2. sum of absolute matrix entries,
3. maximum absolute determinant among all square minors.
