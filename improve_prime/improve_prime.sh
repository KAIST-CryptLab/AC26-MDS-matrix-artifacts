#!/usr/bin/env bash

# Long-running prime-field coefficient experiments.
# Build first with:
#   cd improve_prime && make

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="$SCRIPT_DIR/improve_prime"
OUT="$SCRIPT_DIR/improve_prime_experiment.txt"

if [ ! -x "$BIN" ]; then
    echo "Missing executable: $BIN"
    echo "Run: cd \"$SCRIPT_DIR\" && make"
    exit 1
fi

echo "Improving prime coefficient"
"$BIN" --t 4 --attempts 10000000 --values "1,1,1,2,2" >> "$OUT"
"$BIN" --t 5 --attempts 10000000 --values "1,1,1,2,2" >> "$OUT"
"$BIN" --t 6 --attempts 10000000 --values "1,1,1,2,2" >> "$OUT"
"$BIN" --t 7 --attempts 10000000 --values "1,1,1,1,2,2,2,3" --seed 26 >> "$OUT"
"$BIN" --t 8 --attempts 10000000 --values "1,1,1,1,2,2,2,3" --seed 54 >> "$OUT"
echo "Wrote $OUT"
