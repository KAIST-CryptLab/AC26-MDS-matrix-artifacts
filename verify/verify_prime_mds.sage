#!/usr/bin/env sage
from sage.all import *
from itertools import combinations
import sys


def term(name, coeff=1):
    return (Integer(coeff), name)


CASES = {
    3: {
        "matrix": [
            [2, 1, 1],
            [1, 2, 1],
            [1, 1, 2],
        ],
        "constraints": [
            ("w1", [term("x1"), term("x2")]),
            ("w2", [term("w1"), term("x3")]),
            ("y1", [term("w2"), term("x1")]),
            ("y2", [term("w2"), term("x2")]),
            ("y3", [term("w2"), term("x3")]),
        ],
    },
    4: {
        "matrix": [
            [2, 1, 1, 3],
            [1, 2, 3, 1],
            [3, 1, 2, 4],
            [1, 3, 4, 2],
        ],
        "constraints": [
            ("w1", [term("x2"), term("x3")]),
            ("w2", [term("x4"), term("x1")]),
            ("w3", [term("x4"), term("w1")]),
            ("w4", [term("w2"), term("x3")]),
            ("y1", [term("w2", 2), term("w3")]),
            ("y2", [term("w4"), term("w1", 2)]),
            ("y3", [term("w4"), term("y1")]),
            ("y4", [term("y2"), term("w3")]),
        ],
    },
    5: {
        "matrix": [
            [4, 6, 5, 5, 3],
            [2, 6, 4, 8, 5],
            [3, 5, 3, 4, 2],
            [1, 5, 4, 6, 4],
            [4, 4, 3, 1, 1],
        ],
        "constraints": [
            ("w1", [term("x4"), term("x5")]),
            ("w2", [term("x1"), term("x2")]),
            ("w3", [term("x3"), term("w2", 2)]),
            ("w4", [term("x3"), term("w1")]),
            ("w5", [term("x2", 2), term("w4", 2)]),
            ("y5", [term("w3", 2), term("w4")]),
            ("w6", [term("x4", 2), term("w5")]),
            ("w7", [term("w2"), term("w6")]),
            ("y1", [term("w6"), term("y5")]),
            ("y2", [term("x5"), term("w7", 2)]),
            ("y3", [term("w3"), term("w7")]),
            ("y4", [term("w5"), term("w7")]),
        ],
    },
    6: {
        "matrix": [
            [1, 14, 16, 12, 13, 19],
            [5, 38, 40, 40, 36, 49],
            [8, 36, 37, 44, 38, 46],
            [1, 5, 7, 4, 5, 8],
            [2, 17, 17, 18, 16, 21],
            [8, 32, 33, 40, 34, 41],
        ],
        "constraints": [
            ("w9", [term("x3"), term("x6")]),
            ("w3", [term("x2"), term("w9")]),
            ("w4", [term("x5"), term("w3")]),
            ("w1", [term("x4", 2), term("w4", 2)]),
            ("w10", [term("x6"), term("w1", 2)]),
            ("w5", [term("w3"), term("w10", 2)]),
            ("w6", [term("x1"), term("w10")]),
            ("w7", [term("w6"), term("w9", 2)]),
            ("w8", [term("x4", 2), term("w6", 2)]),
            ("w2", [term("x5"), term("w8", 2)]),
            ("y4", [term("w4"), term("w7")]),
            ("y5", [term("w5"), term("w8")]),
            ("y1", [term("w5"), term("y4")]),
            ("y2", [term("w7"), term("y5", 2)]),
            ("y6", [term("w2", 2), term("w9")]),
            ("y3", [term("w10"), term("y6")]),
        ],
    },
    7: {
        "matrix": [
            [6, 92, 21, 24, 20, 80, 26],
            [10, 104, 30, 18, 25, 96, 29],
            [26, 256, 82, 38, 62, 240, 70],
            [16, 189, 50, 44, 43, 168, 53],
            [15, 157, 47, 29, 37, 146, 43],
            [12, 121, 38, 20, 29, 114, 35],
            [18, 187, 56, 41, 43, 170, 52],
        ],
        "constraints": [
            ("w1", [term("x2"), term("x4", 2)]),
            ("w2", [term("x2", 2), term("x6", 2)]),
            ("w3", [term("x5"), term("x7")]),
            ("w4", [term("w3"), term("w2", 2)]),
            ("w5", [term("x3", 3), term("w4", 2)]),
            ("w6", [term("x1"), term("w5")]),
            ("w7", [term("x4"), term("w6", 3)]),
            ("w8", [term("w6"), term("x7")]),
            ("w9", [term("w1", 2), term("w8")]),
            ("w10", [term("w9", 2), term("w4", 2)]),
            ("w11", [term("w7"), term("w10")]),
            ("y1", [term("w10", 3), term("w5")]),
            ("y2", [term("w11", 2), term("w3")]),
            ("w12", [term("y2"), term("x3", 2)]),
            ("w13", [term("w12"), term("w1")]),
            ("y3", [term("w7", 2), term("w12", 2)]),
            ("w14", [term("w13"), term("x6", 2)]),
            ("y4", [term("w10", 3), term("w13")]),
            ("y5", [term("w11"), term("w14")]),
            ("y6", [term("w14"), term("w8", 2)]),
            ("y7", [term("y5"), term("w9", 3)]),
        ],
    },
    8: {
        "matrix": [
            [12, 4, 3, 5, 3, 8, 6, 3],
            [36, 11, 12, 26, 16, 12, 20, 18],
            [116, 46, 69, 109, 77, 24, 68, 71],
            [170, 66, 75, 120, 99, 74, 97, 78],
            [378, 158, 195, 320, 235, 124, 221, 202],
            [332, 148, 195, 319, 221, 75, 197, 201],
            [644, 284, 372, 623, 424, 150, 384, 387],
            [388, 168, 214, 355, 248, 107, 229, 221],
        ],
        "constraints": [
            ("w1", [term("x3"), term("x5")]),
            ("w2", [term("x1", 2), term("x7")]),
            ("w3", [term("x4"), term("x8")]),
            ("w4", [term("x6", 2), term("w2")]),
            ("w5", [term("w3", 3), term("w2", 2)]),
            ("w6", [term("x2"), term("w4", 2)]),
            ("w7", [term("w1", 3), term("w5")]),
            ("w8", [term("x2", 2), term("w7")]),
            ("w9", [term("x4", 2), term("w8")]),
            ("w10", [term("x7"), term("w9", 2)]),
            ("y1", [term("w6", 2), term("w9")]),
            ("w11", [term("x5", 2), term("w10")]),
            ("w12", [term("w6", 3), term("w11", 2)]),
            ("w13", [term("w12"), term("x8")]),
            ("y2", [term("w5", 2), term("w12")]),
            ("w14", [term("w10", 3), term("w13")]),
            ("w15", [term("w1"), term("w13", 2)]),
            ("w16", [term("w14", 2), term("x6")]),
            ("y3", [term("w14", 2), term("w7", 3)]),
            ("y4", [term("w15", 3), term("w4")]),
            ("w17", [term("w16", 3), term("w11")]),
            ("y5", [term("w16", 2), term("y4")]),
            ("w18", [term("w17"), term("w15")]),
            ("y6", [term("w17"), term("w8", 3)]),
            ("y7", [term("w17", 2), term("w5")]),
            ("y8", [term("y1"), term("w18")]),
        ],
    },
}


def matrix_from_constraints(t, constraints):
    values = {}
    for i in range(t):
        values["x{}".format(i + 1)] = vector(ZZ, [1 if j == i else 0 for j in range(t)])
    for dst, terms in constraints:
        total = vector(ZZ, [0] * t)
        for coeff, src in terms:
            total += coeff * values[src]
        values[dst] = total
    return matrix(ZZ, [list(values["y{}".format(i + 1)]) for i in range(t)])


def minor_stats(M):
    t = M.nrows()
    checked = 0
    min_abs = None
    max_abs = 0
    for size in range(1, t + 1):
        for rows in combinations(range(t), size):
            for cols in combinations(range(t), size):
                det = M.matrix_from_rows_and_columns(rows, cols).det()
                checked += 1
                if det == 0:
                    return False, checked, rows, cols, min_abs, max_abs
                ad = abs(Integer(det))
                min_abs = ad if min_abs is None else min(min_abs, ad)
                max_abs = max(max_abs, ad)
    return True, checked, None, None, min_abs, max_abs


def main():
    failures = 0
    for t in sorted(CASES):
        expected = matrix(ZZ, CASES[t]["matrix"])
        computed = matrix_from_constraints(t, CASES[t]["constraints"])
        matrix_match = (computed == expected)
        ok_mds, checked, bad_rows, bad_cols, min_abs, max_abs = minor_stats(computed)
        ok = matrix_match and ok_mds
        print("{} t={} matrix_match={} mds={} checked={} min_abs_det={} max_abs_det={}".format(
            "ok" if ok else "FAIL",
            t,
            matrix_match,
            ok_mds,
            checked,
            min_abs,
            max_abs,
        ))
        if not ok_mds:
            print("  singular minor rows={} cols={}".format(bad_rows, bad_cols))
        if not matrix_match:
            print("  computed matrix:")
            print(computed)
            print("  expected matrix:")
            print(expected)
        failures += 0 if ok else 1
    sys.exit(int(1 if failures else 0))


if __name__ == "__main__":
    main()
