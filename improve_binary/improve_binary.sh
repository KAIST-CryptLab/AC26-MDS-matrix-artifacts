#!/usr/bin/env bash

# Long-running binary-field coefficient experiments.
# Build first with:
#   cd improve_binary && make

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="$SCRIPT_DIR/improve_binary"

if [ ! -x "$BIN" ]; then
    echo "Missing executable: $BIN"
    echo "Run: cd \"$SCRIPT_DIR\" && make"
    exit 1
fi

read -r -p "Select t(4,5,5a,6,7,7a,8,aux1,aux2): " choice || {
    echo "No choice supplied."
    exit 1
}

case $choice in
    4|5|5a|6|7|7a|8|aux1|aux2)
        ;;
    *)
        echo "Wrong input: $choice"
        exit 1
        ;;
esac

count=0

while true
do
    case $choice in
        4)
            "$BIN" --t 4 --max-k 64 --stage1-attempts 10000000 \
                --l-attempts 10000000 --value-limit 1 --base-k 4 --base-poly 0x13 --seed $RANDOM \
                >> "$SCRIPT_DIR/improve_binary_4.txt"
            ;;
        5)
            "$BIN" --t 5 --max-k 64 --stage1-attempts 10000000 --reduced-L \
                --value-limit 1 --base-k 4 --base-poly 0x13 --seed $RANDOM --greedy-descent \
                --adaptive-sum-filter --initial-sum-threshold 9 --threads 10 \
                >> "$SCRIPT_DIR/improve_binary_5_0x13.txt"
            ;;
        5a)
            "$BIN" --t 5 --max-k 64 --stage1-attempts 10000000 --reduced-L \
                --value-limit 1 --base-k 8 --base-poly 0x187 --seed $RANDOM --greedy-descent \
                --adaptive-sum-filter --initial-sum-threshold 5 --threads 10 \
                >> "$SCRIPT_DIR/improve_binary_5_0x187.txt"
            ;;
        6)
            "$BIN" --t 6 --max-k 64 --stage1-attempts 100000000 --reduced-L \
                --value-limit 2 --base-k 8 --base-poly 0x187 --seed $RANDOM --greedy-descent \
                --adaptive-sum-filter --initial-sum-threshold 10 --threads 20 \
                >> "$SCRIPT_DIR/improve_binary_6.txt"
            ;;
        7)
            "$BIN" --t 7 --max-k 64 --stage1-attempts 100000000 --reduced-L \
                --value-limit 4 --base-k 8 --base-poly 0x187 --seed $RANDOM --greedy-descent \
                --adaptive-sum-filter --initial-sum-threshold 32 \
                >> "$SCRIPT_DIR/improve_binary_7.txt"
            ;;
        7a)
            "$BIN" --t 7 --max-k 64 --stage1-attempts 100000000 --reduced-L \
                --value-limit 5 --base-k 16 --seed $RANDOM --greedy-descent \
                --adaptive-sum-filter --initial-sum-threshold 20 \
                >> "$SCRIPT_DIR/improve_binary_7a.txt"
            ;;
        8)
            "$BIN" --t 8 --max-k 64 --stage1-attempts 10000000 --reduced-L \
                --value-limit 5 --base-k 16 --seed $RANDOM --greedy-descent \
                --adaptive-sum-filter --initial-sum-threshold 33 \
                >> "$SCRIPT_DIR/improve_binary_8.txt"
            ;;
        aux1)
            "$BIN" --t 6 --max-k 4 --stage1-attempts 10000000 --reduced-L \
                --values 0,3,-1,-1,1,-2,0,3,-1,-1,1,-2,-5 --base-k 4 --base-poly 0x13 --seed $RANDOM \
                >> "$SCRIPT_DIR/improve_binary_aux1.txt"
            ;;
        aux2)
            "$BIN" --t 7 --max-k 4 --stage1-attempts 10000000 --reduced-L \
                --value-limit 7 --base-k 4 --base-poly 0x13 --seed $RANDOM \
                >> "$SCRIPT_DIR/improve_binary_aux2.txt"
            ;;
    esac

    count=$((count+1))
    echo "DONE $count loop"
done
