#!/usr/bin/env sage
from itertools import combinations
import operator
import sys


def term(name, exp=0):
    return (name, int(exp))


def shift_rows(k, first_row):
    return [first_row] + [[i] for i in range(1, k)]


def special_8_rows():
    return [[8], [1, 2], [2, 8]] + [[i] for i in range(3, 8)]


def rows_t7_k16():
    return [[13, 16]] + [[i] for i in range(1, 11)] + [[9, 11]] + [[i] for i in range(12, 16)]


C5_4_8 = [
    ("w1",  [term("x2"),      term("x5")]),
    ("w2",  [term("x4"),      term("w1")]),
    ("w3",  [term("x3", -1),  term("w2")]),
    ("w4",  [term("x1"),      term("w3")]),
    ("w5",  [term("x3"),      term("w4", -2)]),
    ("y4",  [term("w1", -1),  term("w4")]),
    ("w6",  [term("x5"),      term("w5")]),
    ("y1",  [term("x4"),      term("w6")]),
    ("y5",  [term("w3"),      term("w6", -2)]),
    ("w7",  [term("y4"),      term("y5")]),
    ("y2",  [term("w2", 1),   term("w7")]),
    ("y3",  [term("w5"),      term("w7")]),
]


C5_16_32_64 = [
    ("w1",  [term("x2"),      term("x5")]),
    ("w2",  [term("x3"),      term("w1", -1)]),
    ("w3",  [term("x4"),      term("w1")]),
    ("w4",  [term("x1"),      term("w3")]),
    ("y4",  [term("w2"),      term("w4")]),
    ("w5",  [term("w3", 1),   term("y4")]),
    ("w6",  [term("x5"),      term("w5", 1)]),
    ("w7",  [term("x3"),      term("w6", 1)]),
    ("y1",  [term("w2"),      term("w6")]),
    ("y2",  [term("x4"),      term("w7")]),
    ("y5",  [term("w4", -1),  term("w7")]),
    ("y3",  [term("w5"),      term("y5")]),
]


C6_8_32_64 = [
    ("w1",  [term("x1"),      term("x4")]),
    ("w2",  [term("x2"),      term("x6")]),
    ("w3",  [term("x5"),      term("w2")]),
    ("w4",  [term("w1"),      term("w3", 2)]),
    ("w5",  [term("x6"),      term("w4")]),
    ("w6",  [term("x3"),      term("w5")]),
    ("w7",  [term("x4"),      term("w6", -1)]),
    ("y5",  [term("w3"),      term("w6", 1)]),
    ("w8",  [term("x5"),      term("w7", -2)]),
    ("w9",  [term("x3"),      term("w8", -1)]),
    ("y1",  [term("w4"),      term("w8")]),
    ("w10", [term("w1", 1),   term("w9", -2)]),
    ("y2",  [term("w9"),      term("y5")]),
    ("y3",  [term("w7"),      term("w10")]),
    ("y6",  [term("w2"),      term("w10")]),
    ("y4",  [term("w5"),      term("y6")]),
]


C6_16 = [
    ("w1",  [term("x2"),      term("x6")]),
    ("w2",  [term("x5"),      term("w1")]),
    ("w3",  [term("x1"),      term("w2", 1)]),
    ("w4",  [term("x4"),      term("w2", 1)]),
    ("w5",  [term("x3"),      term("w4")]),
    ("w6",  [term("w3", 1),   term("w5")]),
    ("w7",  [term("x5", -1),  term("w6", 1)]),
    ("w8",  [term("x6"),      term("w7")]),
    ("y1",  [term("w4"),      term("w7")]),
    ("w9",  [term("x3"),      term("w8", 1)]),
    ("y2",  [term("x4"),      term("w9")]),
    ("y5",  [term("w3"),      term("w9", 2)]),
    ("w10", [term("w1", -1),  term("y5")]),
    ("y3",  [term("w8"),      term("w10")]),
    ("y6",  [term("w5", 2),   term("w10")]),
    ("y4",  [term("w6"),      term("y6")]),
]


C7_8 = [
    ("w1",  [term("x2"),      term("x4")]),
    ("w2",  [term("x6"),      term("x7")]),
    ("w3",  [term("w2", -4),  term("x5")]),
    ("w4",  [term("x2"),      term("w3")]),
    ("w5",  [term("x3"),      term("w4", -1)]),
    ("w6",  [term("x1"),      term("w5", -1)]),
    ("w7",  [term("x4"),      term("w6", -2)]),
    ("w8",  [term("w6"),      term("x7")]),
    ("w9",  [term("w1"),      term("w8")]),
    ("w10", [term("w9", 5),   term("w4", 1)]),
    ("w11", [term("w7"),      term("w10")]),
    ("y1",  [term("w10"),     term("w5")]),
    ("y2",  [term("w11"),     term("w2")]),
    ("w12", [term("y2"),      term("x3")]),
    ("w13", [term("w12"),     term("w1")]),
    ("y3",  [term("w7", -4),  term("w12")]),
    ("w14", [term("w13", -1), term("x5")]),
    ("y4",  [term("w10", 1),  term("w13")]),
    ("y5",  [term("w11", 2),  term("w14", -1)]),
    ("y6",  [term("w14"),     term("w8")]),
    ("y7",  [term("y5"),      term("w9")]),
]


C7_16_32_64 = [
    ("w1",  [term("x2"),      term("x4")]),
    ("w2",  [term("x2"),      term("x6", -1)]),
    ("w3",  [term("x5"),      term("x7")]),
    ("w4",  [term("w3", -1),  term("w2")]),
    ("w5",  [term("x3"),      term("w4", -1)]),
    ("w6",  [term("x1"),      term("w5")]),
    ("w7",  [term("x4"),      term("w6", -2)]),
    ("w8",  [term("w6"),      term("x7")]),
    ("w9",  [term("w1"),      term("w8")]),
    ("w10", [term("w9", 2),   term("w4")]),
    ("w11", [term("w7", -2),  term("w10")]),
    ("y1",  [term("w10"),     term("w5")]),
    ("y2",  [term("w11"),     term("w3")]),
    ("w12", [term("y2"),      term("x3")]),
    ("w13", [term("w12"),     term("w1", -3)]),
    ("y3",  [term("w7"),      term("w12", 1)]),
    ("w14", [term("w13"),     term("x6", 1)]),
    ("y4",  [term("w10"),     term("w13", -3)]),
    ("y5",  [term("w11", -1), term("w14", 1)]),
    ("y6",  [term("w14"),     term("w8")]),
    ("y7",  [term("y5"),      term("w9")]),
]


T5_MATRIX_A = [
    [[-2], [-2], [-3, 0], [-2, 0], [-2, 0]],
    [[-4, 0], [-4, -1, 1], [-5, -2], [-4, 1], [-4, -2, -1, 1]],
    [[-4, -2, 0], [-4, -2, -1], [-5, -3, -2, 0], [-4, -2], [-4, -1]],
    [[0], [-1, 0], [-1], [0], [-1, 0]],
    [[-4], [-4, 0], [-5, -2, -1], [-4, 0], [-4, -2, 0]],
]


T5_MATRIX_B = [
    [[1], [-1, 0, 1, 2], [0, 1], [1, 2], [-1, 1, 2]],
    [[2], [1, 2, 3], [0, 2], [0, 2, 3], [2, 3]],
    [[-1, 0, 2], [0, 2, 3], [2], [-1, 0, 1, 2, 3], [0, 1, 2, 3]],
    [[0], [-1, 0], [0], [0], [-1, 0]],
    [[-1, 2], [-1, 1, 2, 3], [0, 2], [-1, 2, 3], [-1, 2, 3]],
]


CASES = [
    {"key": (5, 4),  "L": [[4], [1, 4], [2], [3]],                 "constraints": C5_4_8,       "expected_total": 55,   "expected_matrix": T5_MATRIX_A},
    {"key": (5, 8),  "L": shift_rows(8, [6, 8]),                   "constraints": C5_4_8,       "expected_total": 103,  "expected_matrix": T5_MATRIX_A},
    {"key": (5, 16), "L": shift_rows(16, [1, 16]),                 "constraints": C5_16_32_64,  "expected_total": 197,  "expected_matrix": T5_MATRIX_B},
    {"key": (5, 32), "L": shift_rows(32, [11, 32]),                "constraints": C5_16_32_64,  "expected_total": 389,  "expected_matrix": T5_MATRIX_B},
    {"key": (5, 64), "L": shift_rows(64, [21, 64]),                "constraints": C5_16_32_64,  "expected_total": 773,  "expected_matrix": T5_MATRIX_B},
    {"key": (6, 8),  "L": special_8_rows(),                        "constraints": C6_8_32_64,   "expected_total": 148},
    {"key": (6, 16), "L": shift_rows(16, [15, 16]),                "constraints": C6_16,        "expected_total": 267},
    {"key": (6, 32), "L": shift_rows(32, [21, 32]),                "constraints": C6_8_32_64,   "expected_total": 522},
    {"key": (6, 64), "L": shift_rows(64, [21, 64]),                "constraints": C6_8_32_64,   "expected_total": 1034},
    {"key": (7, 8),  "L": special_8_rows(),                        "constraints": C7_8,         "expected_total": 214},
    {"key": (7, 16), "L": rows_t7_k16(),                           "constraints": C7_16_32_64,  "expected_total": 374},
    {"key": (7, 32), "L": shift_rows(32, [21, 32]),                "constraints": C7_16_32_64,  "expected_total": 691},
    {"key": (7, 64), "L": shift_rows(64, [21, 64]),                "constraints": C7_16_32_64,  "expected_total": 1363},
]


def supports_to_matrix(k, supports):
    rows = []
    for support in supports:
        row = 0
        for col in support:
            row = operator.xor(int(row), int(1 << (col - 1)))
        rows.append(row)
    return rows


def identity_matrix(k):
    return [1 << i for i in range(k)]


def zero_matrix(k):
    return [0] * k


def matrix_mul(left, right):
    rows = []
    for row in left:
        value = 0
        current = int(row)
        while current:
            bit = int(current & -current).bit_length() - 1
            value = operator.xor(int(value), int(right[bit]))
            current &= current - 1
        rows.append(value)
    return rows


def matrix_inverse(matrix):
    k = len(matrix)
    work = [[matrix[row], 1 << row] for row in range(k)]
    for col in range(k):
        pivot = None
        for row in range(col, k):
            if work[row][0] & (1 << col):
                pivot = row
                break
        if pivot is None:
            raise ValueError("singular L")
        work[col], work[pivot] = work[pivot], work[col]
        for row in range(k):
            if row != col and (work[row][0] & (1 << col)):
                work[row][0] = operator.xor(int(work[row][0]), int(work[col][0]))
                work[row][1] = operator.xor(int(work[row][1]), int(work[col][1]))
    return [right for _left, right in work]


def direct_xor_cost(matrix):
    return sum(max(0, int(row).bit_count() - 1) for row in matrix)


class PowerRing:
    def __init__(self, k, supports):
        self.k = k
        self.L = supports_to_matrix(k, supports)
        self.L_inv = matrix_inverse(self.L)
        self.one = identity_matrix(k)
        self.power_cache = {0: self.one}
        self.cost_cache = {}

    def power(self, exponent):
        exponent = int(exponent)
        if exponent in self.power_cache:
            return self.power_cache[exponent]
        base = self.L if exponent > 0 else self.L_inv
        value = abs(exponent)
        result = self.one
        while value:
            if value & 1:
                result = matrix_mul(result, base)
            base = matrix_mul(base, base)
            value >>= 1
        self.power_cache[exponent] = result
        return result

    def symmetric_xor_cost(self, matrix):
        return min(direct_xor_cost(matrix), direct_xor_cost(matrix_inverse(matrix)))

    def multiplier_xor_cost(self, exponent):
        exponent = int(exponent)
        if exponent in self.cost_cache:
            return self.cost_cache[exponent]
        direct = self.symmetric_xor_cost(self.power(exponent))
        cost = direct
        if exponent != 0:
            step = self.L if exponent > 0 else self.L_inv
            cost = min(direct, abs(exponent) * self.symmetric_xor_cost(step))
        self.cost_cache[exponent] = cost
        return cost


def add_matrix(left, right):
    return [operator.xor(int(a), int(b)) for a, b in zip(left, right)]


def transfer_matrix(t, constraints, ring):
    values = {}
    for i in range(t):
        values["x{}".format(i + 1)] = [ring.one if j == i else zero_matrix(ring.k) for j in range(t)]
    for dst, terms in constraints:
        src0, exp0 = terms[0]
        src1, exp1 = terms[1]
        c0 = ring.power(exp0)
        c1 = ring.power(exp1)
        values[dst] = [
            add_matrix(matrix_mul(c0, values[src0][i]), matrix_mul(c1, values[src1][i]))
            for i in range(t)
        ]
    return [values["y{}".format(i + 1)] for i in range(t)]


def symbolic_matrix(t, constraints):
    values = {}
    for i in range(t):
        values["x{}".format(i + 1)] = [{0} if j == i else set() for j in range(t)]
    for dst, terms in constraints:
        src0, exp0 = terms[0]
        src1, exp1 = terms[1]
        row = []
        for i in range(t):
            entry = set(e + exp0 for e in values[src0][i])
            for e in values[src1][i]:
                ep = e + exp1
                if ep in entry:
                    entry.remove(ep)
                else:
                    entry.add(ep)
            row.append(entry)
        values[dst] = row
    return [values["y{}".format(i + 1)] for i in range(t)]


def xor_shifted_block(row, block, offset):
    chunk = offset // 64
    shift = offset % 64
    row[chunk] = operator.xor(int(row[chunk]), int(block << shift))
    if shift != 0 and chunk + 1 < len(row):
        row[chunk + 1] = operator.xor(int(row[chunk + 1]), int(block >> (64 - shift)))


def rank_packed(rows, column_count):
    basis = {}
    rank = 0
    for original in rows:
        row = list(original)
        while True:
            pivot = -1
            for chunk in range(len(row) - 1, -1, -1):
                if row[chunk]:
                    pivot = chunk * 64 + row[chunk].bit_length() - 1
                    break
            if pivot < 0:
                break
            if pivot not in basis:
                basis[pivot] = row
                rank += 1
                break
            existing = basis[pivot]
            for i in range(len(row)):
                row[i] = operator.xor(int(row[i]), int(existing[i]))
    return rank


def first_singular_minor(block_matrix, k):
    t = len(block_matrix)
    for size in range(1, t + 1):
        column_count = size * k
        chunks = (column_count + 63) // 64
        for row_set in combinations(range(t), size):
            for col_set in combinations(range(t), size):
                packed_rows = []
                for row_index in row_set:
                    for bit_row in range(k):
                        packed = [0] * chunks
                        for block_col, col_index in enumerate(col_set):
                            xor_shifted_block(packed, block_matrix[row_index][col_index][bit_row], block_col * k)
                        packed_rows.append(packed)
                if rank_packed(packed_rows, column_count) != column_count:
                    return row_set, col_set
    return None


def extra_cost(constraints, ring):
    total = 0
    for _dst, terms in constraints:
        for _src, exp in terms:
            total += ring.multiplier_xor_cost(exp)
    return total


def normalize_expected_symbolic(raw):
    return [[set(entry) for entry in row] for row in raw]


def main():
    failures = 0
    for case in CASES:
        t, k = case["key"]
        ring = PowerRing(k, case["L"])
        matrix = transfer_matrix(t, case["constraints"], ring)
        singular = first_singular_minor(matrix, k)
        base = len(case["constraints"]) * k
        extra = extra_cost(case["constraints"], ring)
        total = base + extra
        cost_ok = (total == case["expected_total"])
        matrix_ok = "omitted"
        if "expected_matrix" in case:
            matrix_ok = (symbolic_matrix(t, case["constraints"]) == normalize_expected_symbolic(case["expected_matrix"]))
        ok = singular is None and cost_ok and matrix_ok is not False
        print("{} t={} k={} mds={} cost={}({}+{}) expected={} matrix_match={}".format(
            "ok" if ok else "FAIL",
            t,
            k,
            singular is None,
            total,
            base,
            extra,
            case["expected_total"],
            matrix_ok,
        ))
        if singular is not None:
            print("  singular minor rows={} cols={}".format(singular[0], singular[1]))
        failures += 0 if ok else 1
    sys.exit(int(1 if failures else 0))


if __name__ == "__main__":
    main()
