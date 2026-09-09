# improve_binary

C++ search tool for improving binary-field coefficient choices and sparse
linear maps `L`.

## Build

```sh
cd improve_binary
make
```

After compiling, run the long-running experiment script from this directory:

```sh
./improve_binary.sh
```

The script repeatedly runs `./improve_binary`, reads graph templates from the
repository `graph_data/` directory, and appends logs such as
`improve_binary_7.txt` inside this directory.  The collected best results are stored in
`improve_binary_result.txt`.

The MILP post-optimizer is also kept here:

```sh
sage milp_all_appendix_A2_exponents.sage
```

Its constraints are manually hard-coded from the pre-MILP constructions listed
in `improve_binary_result.txt`, i.e. the entries above the line
`### Further optimization using MILP (only reduced case) ###` having the
minimum XOR count for each `(t,k)`.

The bit-level XOR post-optimization experiments are implemented in
`xor_post_optimization.py`.

These modes rerun the optimization without assuming the final answer.  They
enumerate or sample coefficient assignments and sparse linear maps, synthesize
local/joint/direct bit-level XOR circuits, test the MDS condition, and print a
`BEST_SEARCH` line whenever a new best candidate is found.  A long randomized
run can therefore find a candidate different from, or better than, the current
record.

To rerun the post-optimization search bundle with the parameters used for the
recorded results:

```sh
python3 xor_post_optimization.py --all-records
```

This is a long-running command.  It is intended to show the search process that
leads to the recorded results, not just to print the final rows.

This runs the following search stages:

- `(5,4)`: exact graph/coefficient enumeration over `graph_data/5-5-12.dat`
  with coefficient overhead bound 6.  To target the recorded graph directly,
  add `--fixed-graph 220`.

- `(5,8)`, `(5,16)`, `(5,32)`, `(5,64)`: restricted lift checks confirming
  that the tested family did not improve the current records.

- `(6,8)`: fixed-`L` scalar reassignment with scalar budget 18 and 100
  local Paar seeds, followed by changed sparse-`L` search using the recorded
  randomized seed and stopping threshold.

- `(6,16)`, `(6,32)`, `(6,64)`: search over the one-XOR companion `L` family
  using the exponent tuple found for `(6,8)`.

- `(7,8)`: local coefficient-map resynthesis with 100 Paar seeds and
  pair-replacement cap 4.

- `(7,16)`, `(7,32)`, `(7,64)`: exponent reassignment on the `k=16` instance
  with single-slot range 9 and two-slot range 3, then lifting the result to
  the larger word sizes.

These numerical parameters are fixed inside the script because they are part
of the recorded experiment.

Individual searches are selected by the matrix size `--t` and word size `--k`:

```sh
python3 xor_post_optimization.py --t 5 --k 4 --fixed-graph 220
python3 xor_post_optimization.py --t 6 --k 8
python3 xor_post_optimization.py --t 6 --k 32
python3 xor_post_optimization.py --t 7 --k 8
python3 xor_post_optimization.py --t 7 --k 16
python3 xor_post_optimization.py --t 5 --k 8
```

The cases `(5,8)`, `(5,16)`, `(5,32)`, and `(5,64)` run the restricted lift
check used to confirm that the tested family did not improve the current
records.

For example, these commands reproduce the sparse-`L` choices in the current
records:

```sh
python3 xor_post_optimization.py --t 6 --k 16
python3 xor_post_optimization.py --t 6 --k 32
python3 xor_post_optimization.py --t 6 --k 64
python3 xor_post_optimization.py --t 7 --k 16
python3 xor_post_optimization.py --t 7 --k 32
python3 xor_post_optimization.py --t 7 --k 64
```

The examples below assume your current directory is `improve_binary/`.

## Basic Commands

- Standard two-stage search:

```sh
./improve_binary --t 6 --max-k 64 --base-k 8 --base-poly 0x187
```

- Search one target word size:

```sh
./improve_binary --t 6 --k 8 --base-k 8 --base-poly 0x187
```

- Parallel search:

```sh
./improve_binary --t 6 --k 32 --base-k 8 --base-poly 0x187 --threads 16
```

- Reduced `L` enumeration:

```sh
./improve_binary --t 6 --max-k 16 --base-k 8 --base-poly 0x187 --reduced-L
```

- Greedy descent after finding an MDS stage-1 candidate:

```sh
./improve_binary --t 6 --k 8 --base-k 8 --base-poly 0x187 --greedy-descent
```

- Adaptive coefficient-sum filtering:

```sh
./improve_binary --t 6 --k 8 --base-k 8 --base-poly 0x187 --adaptive-sum-filter
```

- Rerun selected bit-level XOR post-optimization experiments:

```sh
python3 xor_post_optimization.py --all-records
python3 xor_post_optimization.py --t 6 --k 8
python3 xor_post_optimization.py --t 7 --k 8
python3 xor_post_optimization.py --t 6 --k 32
```

- Manual stage-1 coefficients:

```sh
./improve_binary --t 4 --max-k 8 --base-k 4 --base-poly 0x13 --graph-index 1 --coeffs "[(0, 0), (0, 0), (0, 0), (0, -1), (0, 1), (0, 1), (0, 0), (0, 0)]" --reduced-L
```

- Randomized stage-1 `L` for larger base word sizes:

```sh
./improve_binary --t 7 --max-k 64 --base-k 16 --reduced-L --value-limit 5
```

- To use graph templates with a non-default gate count, add `--graph-c C`
  together with `--k` or `--max-k`. This is mainly for long-running
  stochastic experiments, so the required search budget depends on the case.

## Search Structure

- Stage 1 searches for a graph and exponent assignment over a base word size.

- Stage 2 fixes the selected graph and exponents, then searches for a sparse
  cyclic `L` at each target word size.

- If `--base-k` is `16`, `32`, or `64` without `--base-poly`, stage 1 also
  samples `L` randomly on each attempt. The best coefficient/`L` pair is used
  as the stage-1 pattern.

## Target Options

- `--t T`: target matrix size. Required in practice.

- `--k K`: search one target word size. Valid range: `1..64`.

- `--max-k K`: sweep standard word sizes `{4,8,16,32,64}` from `--base-k` up to `K`.

- `--attempts N`: fallback attempt count used by `--stage1-attempts` and `--l-attempts` when those are not set. Default: `10000`.

- `--stage1-attempts N`: number of accepted stage-1 coefficient attempts. Default: `--attempts`.

- `--l-attempts N`: number of randomized stage-2 `L` attempts when `--reduced-L` is not used. Default: `--attempts`.

- `--seed N`: RNG seed. Default: `1`.

- `--threads N`: worker threads for stage 1 and stage 2. Default: hardware CPU count.

## Base Field and Exponent Options

- `--base-k K`: base word size for stage 1. Allowed values: `4`, `8`, `16`, `32`, `64`. Default: `4`.

- `--base-poly P`: fixed base-field polynomial, parsed with C-style integer syntax such as `0x13` or `0x187`.

- `--value-limit V`: builds the default weighted exponent bag from `-V..V`, favoring small absolute values. Default: `3`.

- `--values LIST`: explicit exponent sampling bag. The list may be comma-separated or space-separated. Overrides `--value-limit`.

- `--coeffs "[(a,b), ...]"`: manually provide all stage-1 gate exponent pairs. Requires a single `--graph-index`. Cannot be combined with `--adaptive-sum-filter`, `--greedy-descent`, or randomized stage-1 `L`.

- `--greedy-descent`: after a stage-1 MDS candidate is found, locally tries lower-cost exponent pairs while preserving MDS.

## L-Search Options

- `--reduced-L`: enumerate the reduced cyclic `L` family. This places one extra `1` in the first row and optionally a second extra `1` in another row. In this mode, `--l-attempts` is ignored because all candidates are checked.

- `--l-extra-min N`: minimum number of random extra ones for full-random cyclic `L`. Default: `1`.

- `--l-extra-max N`: maximum number of random extra ones for full-random cyclic `L`. Default: `3`.

## Graph Options

- `--graph-root PATH`: optional override for the graph template directory. By default, the program automatically finds the repository `graph_data/` directory.

- `--graph-c C`: use graph file with non-default gate count `C`, resolved under `--graph-root`.

- `--graph-file PATH`: use this exact graph file. Cannot be combined with `--graph-c`.

- `--graph-index I`: fix stage 1 to one graph template.

- Without `--graph-index`, stage 1 samples uniformly from all loaded graph templates.

## Adaptive Sum Filter Options

- `--adaptive-sum-filter`: after an MDS stage-1 candidate is found, set the coefficient absolute-sum threshold to the candidate's sum. Later samples are drawn under the improved threshold.

- `--initial-sum-threshold N`: start adaptive filtering from a manual threshold. Default: no finite threshold.

## Output

- `stage 1 selected ...`: graph, cost, coefficients, and base `L` selected by stage 1.

- `best (t,k,graph_index) tot=TOTAL(BASE+EXTRA) seed=SEED coeffs=[...] L=[...]`: final result line for one target word size.

- `TOTAL`: total XOR cost.

- `BASE`: graph base cost, equal to `number_of_gates * k`.

- `EXTRA`: multiplier cost from the chosen powers of `L`.

- `xor_post_optimization.py` reports the final post-optimized totals and the
  savings against the best pre-post-optimization candidates.
