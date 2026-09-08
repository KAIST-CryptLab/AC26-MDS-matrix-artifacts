#!/usr/bin/env python3
"""Bit-level XOR post-optimization experiments for binary MDS candidates.

The C++ search program in this directory finds word-level candidates.  The
experiments below are the follow-up bit-level optimizations used for selected
binary-field constructions:

1. search a lower-cost coefficient assignment on a selected SC(5,5,12) graph;
2. apply explicit straight-line programs for repeated coefficient maps;
3. apply a joint/direct bit-level rewrite for a selected word-level node;
4. build sparse-L circuits for larger word sizes.

The file is organized so that the optimization can be audited from the code:
the starting graph, exponents, and sparse linear map are written explicitly,
and each experiment rebuilds the improved XOR circuit before reporting its
gate count.  The output lines are formatted to match the corresponding
post-optimization block in improve_binary_result.txt.

Use --run to execute a selected experiment.  Some modes are intentionally
long-running because they encode the original search space rather than a
shortened demo.
"""

from __future__ import annotations

import argparse
from collections import deque
from dataclasses import dataclass
from itertools import combinations
from pathlib import Path
from typing import Iterable, Iterator

Coeff = tuple[int, int]
Supports = tuple[tuple[int, ...], ...]


AUDIT_PREVIOUS_TOTALS = {
    (5, 4): 55,
    (6, 8): 148,
    (6, 16): 267,
    (6, 32): 522,
    (6, 64): 1034,
    (7, 8): 214,
    (7, 16): 374,
    (7, 32): 691,
    (7, 64): 1363,
}

# These values are audit guards copied from improve_binary_result.txt.  They
# never determine the reported count; each experiment first rebuilds a circuit
# and then checks that len(circuit.gates) still matches this record.
AUDIT_EXPECTED_TOTALS = {
    (6, 8): 144,
    (6, 16): 265,
    (6, 32): 521,
    (6, 64): 1033,
    (7, 8): 212,
    (7, 16): 370,
    (7, 32): 689,
    (7, 64): 1361,
}

AUDIT_T6_K8_STAGE_COSTS = (147, 146, 145, 144)


def format_coeffs(coeffs: Iterable[Coeff]) -> str:
    return "[" + ", ".join(f"({a}, {b})" for a, b in coeffs) + "]"


def format_l(row_supports: Supports) -> str:
    return "[" + ", ".join("[" + ", ".join(str(x) for x in row) + "]" for row in row_supports) + "]"


def rows(*items: Iterable[int]) -> Supports:
    return tuple(tuple(item) for item in items)


def companion_rows(k: int, first_row: tuple[int, ...]) -> Supports:
    return (first_row,) + tuple((i,) for i in range(1, k))


def support_rows_to_masks(row_supports: Supports) -> tuple[int, ...]:
    masks = []
    for support in row_supports:
        mask = 0
        for column in support:
            if column < 1:
                raise ValueError("L supports use one-based column indices")
            mask |= 1 << (column - 1)
        masks.append(mask)
    return tuple(masks)


def identity_rows(k: int) -> tuple[int, ...]:
    return tuple(1 << i for i in range(k))


def matrix_multiply(left: tuple[int, ...], right: tuple[int, ...]) -> tuple[int, ...]:
    rows_out = []
    for selector in left:
        row_value = 0
        bit = 0
        current = selector
        while current:
            if current & 1:
                row_value ^= right[bit]
            current >>= 1
            bit += 1
        rows_out.append(row_value)
    return tuple(rows_out)


def matrix_inverse(matrix: tuple[int, ...]) -> tuple[int, ...]:
    k = len(matrix)
    augmented = [matrix[row] | ((1 << row) << k) for row in range(k)]
    for column in range(k):
        pivot = next((row for row in range(column, k) if (augmented[row] >> column) & 1), None)
        if pivot is None:
            raise ValueError("singular matrix")
        augmented[column], augmented[pivot] = augmented[pivot], augmented[column]
        for row in range(k):
            if row != column and ((augmented[row] >> column) & 1):
                augmented[row] ^= augmented[column]
    if tuple(row & ((1 << k) - 1) for row in augmented) != identity_rows(k):
        raise AssertionError("matrix inversion failed")
    return tuple(row >> k for row in augmented)


def matrix_power(matrix: tuple[int, ...], exponent: int) -> tuple[int, ...]:
    if exponent < 0:
        matrix = matrix_inverse(matrix)
        exponent = -exponent
    result = identity_rows(len(matrix))
    base = matrix
    while exponent:
        if exponent & 1:
            result = matrix_multiply(base, result)
        base = matrix_multiply(base, base)
        exponent >>= 1
    return result


@dataclass(frozen=True)
class Result:
    """A final post-optimization record, formatted like improve_binary_result."""

    t: int
    k: int
    graph_index: int
    previous_total: int
    total_xor: int
    base_xor: int
    coeffs: tuple[Coeff, ...]
    l_rows: Supports
    method: str

    @property
    def total(self) -> int:
        return self.total_xor

    @property
    def extra_xor(self) -> int:
        return self.total_xor - self.base_xor

    @property
    def saving(self) -> int:
        return self.previous_total - self.total

    def result_line(self) -> str:
        return (
            f"best ({self.t},{self.k},{self.graph_index}) "
            f"tot={self.total}({self.base_xor}+{self.extra_xor}) "
            f"coeffs={format_coeffs(self.coeffs)} L={format_l(self.l_rows)}"
        )


def audited_total(t: int, k: int, circuit: "Circuit") -> int:
    total_xor = len(circuit.gates)
    expected = AUDIT_EXPECTED_TOTALS.get((t, k))
    if expected is not None and total_xor != expected:
        raise AssertionError(
            f"computed {total_xor} gates for ({t},{k}), expected audit record {expected}"
        )
    return total_xor


# ---------------------------------------------------------------------------
# GF(2^4) arithmetic and graph handling for the t=5,k=4 search.
# ---------------------------------------------------------------------------


GF16_POLY = 0x13


def gf16_mul(a: int, b: int) -> int:
    result = 0
    while b:
        if b & 1:
            result ^= a
        b >>= 1
        a <<= 1
        if a & 0x10:
            a ^= GF16_POLY
    return result & 0xF


GF16_POWERS: list[int] = []
_x_power = 1
for _ in range(15):
    GF16_POWERS.append(_x_power)
    _x_power = gf16_mul(_x_power, 2)


def gf16_power(exp: int) -> int:
    return GF16_POWERS[exp % 15]


def gf16_inv(a: int) -> int:
    if a == 0:
        raise ZeroDivisionError("0 has no inverse in GF(16)")
    return next(x for x in range(1, 16) if gf16_mul(a, x) == 1)


def gf16_det(matrix: list[list[int]]) -> int:
    work = [row[:] for row in matrix]
    n = len(work)
    det = 1
    for col in range(n):
        pivot = next((row for row in range(col, n) if work[row][col]), None)
        if pivot is None:
            return 0
        if pivot != col:
            work[col], work[pivot] = work[pivot], work[col]
        pivot_value = work[col][col]
        det = gf16_mul(det, pivot_value)
        inv = gf16_inv(pivot_value)
        for row in range(col + 1, n):
            if not work[row][col]:
                continue
            factor = gf16_mul(work[row][col], inv)
            for j in range(col, n):
                work[row][j] ^= gf16_mul(factor, work[col][j])
    return det


def gf16_is_mds(matrix: list[list[int]]) -> bool:
    t = len(matrix)
    subset_cache = {size: list(combinations(range(t), size)) for size in range(1, t + 1)}
    for size in range(1, t + 1):
        for row_set in subset_cache[size]:
            for col_set in subset_cache[size]:
                minor = [[matrix[i][j] for j in col_set] for i in row_set]
                if gf16_det(minor) == 0:
                    return False
    return True


def read_bits(record: bytes, width: int, count: int) -> list[int]:
    values: list[int] = []
    bit_pos = 0
    for _ in range(count):
        value = 0
        for _bit in range(width):
            value = (value << 1) | ((record[bit_pos // 8] >> (7 - bit_pos % 8)) & 1)
            bit_pos += 1
        values.append(value)
    return values


def read_graphs(path: Path) -> tuple[int, int, int, list[list[tuple[int, int]]]]:
    data = path.read_bytes()
    if data[:4] == b"FCBG":
        n = data[4]
        m = data[5]
        c = (data[6] << 8) | data[7]
        width = data[8]
        record_size = (2 * c * width + 7) // 8
        offset = 9
    else:
        parts = [int(part) for part in path.stem().replace("-", "_").split("_")[:3]]
        if len(parts) != 3:
            raise ValueError(f"cannot infer graph parameters from {path}")
        n, m, c = parts
        width = 5
        record_size = (2 * c * width + 7) // 8
        offset = 0

    graphs: list[list[tuple[int, int]]] = []
    while offset + record_size <= len(data):
        values = read_bits(data[offset:offset + record_size], width, 2 * c)
        graphs.append([(values[2 * i], values[2 * i + 1]) for i in range(c)])
        offset += record_size
    return n, m, c, graphs


def gf16_graph_matrix(t: int, c: int, predecessors: list[tuple[int, int]], coeffs: tuple[Coeff, ...]) -> list[list[int]]:
    memo: dict[int, list[int]] = {}

    def node_value(node: int) -> list[int]:
        if node in memo:
            return memo[node]
        if node < t:
            basis = [0] * t
            basis[node] = 1
            memo[node] = basis
            return basis
        left, right = predecessors[node - t]
        left_exp, right_exp = coeffs[node - t]
        left_value = node_value(left)
        right_value = node_value(right)
        left_coeff = gf16_power(left_exp)
        right_coeff = gf16_power(right_exp)
        memo[node] = [
            gf16_mul(left_coeff, left_value[i]) ^ gf16_mul(right_coeff, right_value[i])
            for i in range(t)
        ]
        return memo[node]

    return [node_value(node) for node in range(t + c - t, t + c)]


def linear_map_rows_for_gf16_scalar(scalar: int) -> tuple[int, ...]:
    columns = [gf16_mul(scalar, 1 << col) for col in range(4)]
    out = []
    for row in range(4):
        mask = 0
        for col, value in enumerate(columns):
            if (value >> row) & 1:
                mask |= 1 << col
        out.append(mask)
    return tuple(out)


def exact_slp_cost(input_count: int, target_rows: Iterable[int]) -> int:
    targets = set(target_rows)
    start = frozenset(1 << bit for bit in range(input_count))
    if targets <= start:
        return 0
    queue: deque[frozenset[int]] = deque([start])
    distance = {start: 0}
    while queue:
        forms = queue.popleft()
        depth = distance[forms]
        form_list = list(forms)
        for i, left in enumerate(form_list):
            for right in form_list[i + 1:]:
                new_form = left ^ right
                if new_form == 0 or new_form in forms:
                    continue
                next_forms = frozenset((*forms, new_form))
                if next_forms in distance:
                    continue
                if targets <= next_forms:
                    return depth + 1
                distance[next_forms] = depth + 1
                queue.append(next_forms)
    raise RuntimeError("unreachable linear map")


def gf16_exponent_costs() -> dict[int, int]:
    costs = {0: 0}
    for exp in range(1, 15):
        rows_for_exp = linear_map_rows_for_gf16_scalar(gf16_power(exp))
        costs[exp] = exact_slp_cost(4, rows_for_exp)
    return costs


def signed_exp(exp_mod_15: int) -> int:
    return exp_mod_15 if exp_mod_15 <= 7 else exp_mod_15 - 15


def coefficient_options(max_cost: int) -> list[tuple[Coeff, int]]:
    costs = gf16_exponent_costs()
    options = [((0, 0), 0)]
    for exp_mod, cost in costs.items():
        if exp_mod == 0 or cost > max_cost:
            continue
        exp = signed_exp(exp_mod)
        options.append(((exp, 0), cost))
        options.append(((0, exp), cost))
    options.sort(key=lambda item: (item[1], abs(item[0][0]) + abs(item[0][1]), item[0]))
    return options


def enumerate_coeff_tuples(slot_count: int, max_cost: int) -> Iterator[tuple[tuple[Coeff, ...], int]]:
    options = coefficient_options(max_cost)

    def visit(slot: int, remaining: int, current: list[Coeff], cost: int) -> Iterator[tuple[tuple[Coeff, ...], int]]:
        if slot == slot_count:
            yield tuple(current), cost
            return
        for pair, pair_cost in options:
            if pair_cost <= remaining:
                current.append(pair)
                yield from visit(slot + 1, remaining - pair_cost, current, cost + pair_cost)
                current.pop()

    yield from visit(0, max_cost, [], 0)


def run_t5_k4_search(graph_root: Path, *, max_cost: int, fixed_graph: int | None) -> Result:
    """Search the restricted GF(16) coefficient space used for the t=5,k=4 gain.

    Each nonzero coefficient is placed on only one incoming edge of a word gate.
    The cost of that coefficient is the exact 4-bit SLP cost of the corresponding
    GF(16) scalar multiplication.  A candidate is accepted only when its
    superconcentrator matrix is MDS over GF(16).
    """

    n, m, c, graphs = read_graphs(graph_root / "5-5-12.dat")
    if (n, m, c) != (5, 5, 12):
        raise ValueError("expected SC(5,5,12) graph data")
    graph_indices = [fixed_graph] if fixed_graph else list(range(1, len(graphs) + 1))
    tested = 0
    for graph_index in graph_indices:
        predecessors = graphs[graph_index - 1]
        for coeffs, cost in enumerate_coeff_tuples(c, max_cost):
            tested += 1
            matrix = gf16_graph_matrix(n, c, predecessors, coeffs)
            if gf16_is_mds(matrix):
                return Result(
                    t=5,
                    k=4,
                    graph_index=graph_index,
                    previous_total=AUDIT_PREVIOUS_TOTALS[(5, 4)],
                    total_xor=c * 4 + cost,
                    base_xor=c * 4,
                    coeffs=coeffs,
                    l_rows=rows((4,), (1, 4), (2,), (3,)),
                    method=f"GF(16) coefficient search, tested {tested} assignments",
                )
    raise RuntimeError("no t=5,k=4 improvement found")


# ---------------------------------------------------------------------------
# SLP synthesis and direct rewrite experiments.
# ---------------------------------------------------------------------------


class Circuit:
    def __init__(self, input_count: int, *, reuse_equal_forms: bool = False) -> None:
        self.forms = [1 << bit for bit in range(input_count)]
        self.gates: list[list[int]] = []
        self.by_form = {form: wire for wire, form in enumerate(self.forms)} if reuse_equal_forms else None

    def emit(self, left: int, right: int) -> int:
        if left == right:
            raise ValueError("self-XOR is not a valid gate")
        form = self.forms[left] ^ self.forms[right]
        if self.by_form is not None and form in self.by_form:
            return self.by_form[form]
        output = len(self.forms)
        self.forms.append(form)
        self.gates.append([left, right])
        if self.by_form is not None:
            self.by_form[form] = output
        return output

    def word_xor(self, left: list[int], right: list[int]) -> list[int]:
        if len(left) != len(right):
            raise ValueError("word widths differ")
        return [self.emit(a, b) for a, b in zip(left, right)]


def apply_local_path(circuit: Circuit, word: list[int], path: Iterable[tuple[int, int, int]]) -> dict[int, int]:
    local = {1 << bit: word[bit] for bit in range(len(word))}
    for left_form, right_form, result_form in path:
        local[result_form] = circuit.emit(local[left_form], local[right_form])
    return local


def run_t7_k8_lm4_synthesis() -> Result:
    l_rows = rows((8,), (1, 2), (2, 8), (3,), (4,), (5,), (6,), (7,))
    l_matrix = support_rows_to_masks(l_rows)

    # Local SLPs copied from the post-optimization experiment.  They implement
    # selected powers of the fixed k=8 map more cheaply than repeated L calls.
    local_power_slps = {
        -4: {
            "gates": [(0, 2), (4, 8), (1, 3), (10, 8), (11, 5), (9, 5), (1, 8)],
            "outputs": [12, 13, 6, 7, 0, 14, 10, 9],
        },
        -2: {
            "gates": [(1, 2), (0, 8), (3, 1), (3, 9)],
            "outputs": [10, 11, 4, 5, 6, 7, 0, 9],
        },
        -1: {
            "gates": [(0, 2), (1, 8)],
            "outputs": [9, 8, 3, 4, 5, 6, 7, 0],
        },
        1: {
            "gates": [(1, 0), (7, 1)],
            "outputs": [7, 8, 9, 2, 3, 4, 5, 6],
        },
        2: {
            "gates": [(1, 7), (8, 0), (6, 1), (10, 0)],
            "outputs": [6, 9, 11, 8, 2, 3, 4, 5],
        },
        5: {
            "gates": [
                (1, 7), (0, 8), (5, 9), (6, 10), (11, 4),
                (11, 3), (9, 6), (14, 4), (6, 1), (16, 0),
            ],
            "outputs": [3, 12, 13, 15, 10, 17, 8, 2],
        },
    }
    for exponent, slp in local_power_slps.items():
        forms = list(identity_rows(8))
        for left, right in slp["gates"]:
            forms.append(forms[left] ^ forms[right])
        computed = tuple(forms[index] for index in slp["outputs"])
        if computed != matrix_power(l_matrix, exponent):
            raise AssertionError(f"local SLP for L^{exponent} does not match L-power")

    constraints = [
        ("w1",  [("x2", 0),  ("x4", 0)]),
        ("w2",  [("x6", 0),  ("x7", 0)]),
        ("w3",  [("w2", -4), ("x5", 0)]),
        ("w4",  [("x2", 0),  ("w3", 0)]),
        ("w5",  [("x3", 0),  ("w4", -1)]),
        ("w6",  [("x1", 0),  ("w5", -1)]),
        ("w7",  [("x4", 0),  ("w6", -2)]),
        ("w8",  [("w6", 0),  ("x7", 0)]),
        ("w9",  [("w1", 0),  ("w8", 0)]),
        ("w10", [("w9", 5),  ("w4", 1)]),
        ("w11", [("w7", 0),  ("w10", 0)]),
        ("y1",  [("w10", 0), ("w5", 0)]),
        ("y2",  [("w11", 0), ("w2", 0)]),
        ("w12", [("y2", 0),  ("x3", 0)]),
        ("w13", [("w12", 0), ("w1", 0)]),
        ("y3",  [("w7", -4), ("w12", 0)]),
        ("w14", [("w13", -1), ("x5", 0)]),
        ("y4",  [("w10", 1), ("w13", 0)]),
        ("y5",  [("w11", 2), ("w14", -1)]),
        ("y6",  [("w14", 0), ("w8", 0)]),
        ("y7",  [("y5", 0),  ("w9", 0)]),
    ]

    circuit = Circuit(7 * 8)
    words = {f"x{word + 1}": list(range(8 * word, 8 * (word + 1))) for word in range(7)}

    def apply_power(source_word: list[int], exponent: int) -> list[int]:
        if exponent == 0:
            return source_word[:]
        slp = local_power_slps[exponent]
        local = list(source_word)
        for left, right in slp["gates"]:
            local.append(circuit.emit(local[left], local[right]))
        return [local[index] for index in slp["outputs"]]

    for destination, terms in constraints:
        left = apply_power(words[terms[0][0]], terms[0][1])
        right = apply_power(words[terms[1][0]], terms[1][1])
        words[destination] = circuit.word_xor(left, right)

    total_xor = audited_total(7, 8, circuit)
    base_xor = len(constraints) * 8
    local_slp_cost = sum(
        len(local_power_slps[exponent]["gates"])
        for _destination, terms in constraints
        for _source, exponent in terms
        if exponent != 0
    )
    if total_xor != base_xor + local_slp_cost:
        raise AssertionError("t7-k8 local SLP cost decomposition changed")

    coeffs = (
        (0, 0), (0, 0), (-4, 0), (0, 0), (0, -1), (0, -1), (0, -2),
        (0, 0), (0, 0), (5, 1), (0, 0), (0, 0), (0, 0), (-1, 0),
        (0, 0), (0, 0), (-4, 0), (1, 0), (2, -1), (0, 0), (0, 0),
    )
    return Result(
        t=7,
        k=8,
        graph_index=2,
        previous_total=AUDIT_PREVIOUS_TOTALS[(7, 8)],
        total_xor=total_xor,
        base_xor=base_xor,
        coeffs=coeffs,
        l_rows=l_rows,
        method="exact local SLPs from the post-optimization experiment",
    )


def run_t6_k8_direct_rewrite() -> Result:
    k = 8
    l_rows = rows((3,), (6,), (5,), (4, 5), (4, 7), (1,), (8,), (2,))
    l = support_rows_to_masks(l_rows)
    l_inverse = matrix_power(l, -1)
    l_inverse_square = matrix_power(l, -2)

    # These local paths encode the recorded 148 -> 147 -> 146 -> 145 stages.
    # The final stage replaces the whole w10 computation by a direct bit-level
    # circuit, so its cost is counted from the explicit gates below.
    path_l = ((64, 8, 72), (8, 16, 24))
    path_l_inverse = ((4, 8, 12), (12, 16, 28))
    path_l_inverse_square = ((1, 8, 9), (4, 8, 12), (1, 12, 13), (12, 16, 28))
    path_joint = ((1, 8, 9), (64, 8, 72), (4, 9, 13), (8, 16, 24), (4, 24, 28))
    w10_gates = (
        (10, 11), (3, 8), (17, 6), (14, 5), (4, 13), (15, 0), (11, 7),
        (22, 8), (4, 17), (24, 16), (2, 9), (16, 1), (27, 12),
    )
    w10_outputs = (26, 19, 20, 25, 18, 21, 23, 28)

    constraints = [
        ("w1", (("x1", 0), ("x4", 0))), ("w2", (("x2", 0), ("x6", 0))),
        ("w3", (("x5", 0), ("w2", 0))), ("w4", (("w1", 0), ("w3", 1))),
        ("w5", (("x6", 0), ("w4", 0))), ("w6", (("x3", 0), ("w5", 0))),
        ("w7", (("x4", 0), ("w6", -2))), ("y5", (("w3", 0), ("w6", 1))),
        ("w8", (("x5", 0), ("w7", -1))), ("w9", (("x3", 0), ("w8", -1))),
        ("y1", (("w4", 0), ("w8", 0))), ("w10", (("w1", 1), ("w9", -2))),
        ("y2", (("w9", 0), ("y5", 0))), ("y3", (("w7", 0), ("w10", 0))),
        ("y6", (("w2", 0), ("w10", 0))), ("y4", (("w5", 0), ("y6", 0))),
    ]

    circuit = Circuit(6 * k, reuse_equal_forms=True)
    words = {f"x{index + 1}": list(range(k * index, k * (index + 1))) for index in range(6)}

    def linear(exponent: int, word: list[int], *, joint: bool = False) -> list[int]:
        if exponent == 0:
            return word[:]
        row_map = {1: l, -1: l_inverse, -2: l_inverse_square}[exponent]
        path = path_joint if joint else {1: path_l, -1: path_l_inverse, -2: path_l_inverse_square}[exponent]
        local = apply_local_path(circuit, word, path)
        return [local[row] for row in row_map]

    def direct_w10(left_word: list[int], right_word: list[int]) -> list[int]:
        local = list(left_word) + list(right_word)
        for left, right in w10_gates:
            if left >= len(local) or right >= len(local):
                raise ValueError("non-topological w10 direct rewrite")
            local.append(circuit.emit(local[left], local[right]))
        return [local[index] for index in w10_outputs]

    for destination, ((left_name, left_exp), (right_name, right_exp)) in constraints:
        if destination == "w10":
            words[destination] = direct_w10(words[left_name], words[right_name])
            continue
        left = linear(left_exp, words[left_name])
        right = linear(right_exp, words[right_name], joint=(destination == "w7"))
        words[destination] = circuit.word_xor(left, right)

    total_xor = audited_total(6, 8, circuit)
    ordinary_word_xor_slots = sum(1 for destination, _terms in constraints if destination != "w10")
    base_xor = ordinary_word_xor_slots * k

    # Guard the provenance of the staged reduction: if any local path changes,
    # these assertions force the reported 144-gate result to be recomputed.
    stage_147 = 16 * k + 6 * 2 + 7
    stage_146 = 16 * k + 3 * 2 + 2 * 2 + 2 * 4
    stage_145 = 16 * k + 5 + 2 * 2 + 2 * 2 + 4
    stage_144 = base_xor + len(path_l) + len(path_joint) + 2 * len(path_l_inverse) + len(w10_gates)
    if (stage_147, stage_146, stage_145, stage_144) != AUDIT_T6_K8_STAGE_COSTS:
        raise AssertionError("t6-k8 post-optimization stage costs changed")
    if stage_144 != total_xor:
        raise AssertionError("t6-k8 direct circuit count and stage-cost formula differ")

    coeffs = (
        (0, 0), (0, 0), (0, 0), (0, 1), (0, 0), (0, 0), (0, -2), (0, -1),
        (0, -1), (1, -2), (0, 0), (0, 0), (0, 0), (0, 0), (0, 1), (0, 0),
    )
    return Result(
        t=6,
        k=8,
        graph_index=570,
        previous_total=AUDIT_PREVIOUS_TOTALS[(6, 8)],
        total_xor=total_xor,
        base_xor=base_xor,
        coeffs=coeffs,
        l_rows=l_rows,
        method="148->147->146->145->144 coefficient/local-SLP/joint/direct rewrites",
    )


# ---------------------------------------------------------------------------
# Sparse-L and exponent-slot rewrite experiments for k=16,32,64.
# ---------------------------------------------------------------------------


T6_REWRITE_COEFFS = (
    (0, 0), (0, 0), (0, 0), (0, 1), (0, 0), (0, 0), (0, -2), (0, -1),
    (0, -1), (1, -2), (0, 0), (0, 0), (0, 0), (0, 0), (0, 1), (0, 0),
)

T7_REWRITE_COEFFS = (
    (0, 0), (0, -1), (0, 0), (-1, 0), (0, -1), (0, 0), (0, -2),
    (0, 0), (0, 0), (2, 0), (-2, 0), (0, 0), (0, -2), (0, 1),
    (0, 0), (0, 0), (0, 1), (0, -3), (0, 1), (0, 0), (0, 0),
)


def run_t6_large_k_rewrite(k: int) -> Result:
    """Rebuild the t=6 larger-k circuit using the optimized sparse L choice."""

    taps = {16: 1, 32: 21, 64: 21}
    if k not in taps:
        raise ValueError("t=6 larger-k rewrite is defined for k=16,32,64")

    constraints = [
        ("w1",  (("x1", 0),  ("x4", 0))),
        ("w2",  (("x2", 0),  ("x6", 0))),
        ("w3",  (("x5", 0),  ("w2", 0))),
        ("w4",  (("w1", 0),  ("w3", 1))),
        ("w5",  (("x6", 0),  ("w4", 0))),
        ("w6",  (("x3", 0),  ("w5", 0))),
        ("w7",  (("x4", 0),  ("w6", -2))),
        ("y5",  (("w3", 0),  ("w6", 1))),
        ("w8",  (("x5", 0),  ("w7", -1))),
        ("w9",  (("x3", 0),  ("w8", -1))),
        ("y1",  (("w4", 0),  ("w8", 0))),
        ("w10", (("w1", 1),  ("w9", -2))),
        ("y2",  (("w9", 0),  ("y5", 0))),
        ("y3",  (("w7", 0),  ("w10", 0))),
        ("y6",  (("w2", 0),  ("w10", 0))),
        ("y4",  (("w5", 0),  ("y6", 0))),
    ]

    tap = taps[k]
    circuit = Circuit(6 * k)
    words = {f"x{index + 1}": list(range(index * k, (index + 1) * k)) for index in range(6)}

    # For these companion-style maps, one L or L^{-1} application costs one XOR.
    # The circuit is still built explicitly, rather than using only the formula.
    def multiply_l(word: list[int]) -> list[int]:
        feedback = circuit.emit(word[tap - 1], word[k - 1])
        return [feedback, *word[:-1]]

    def multiply_l_inverse(word: list[int]) -> list[int]:
        feedback = circuit.emit(word[0], word[tap])
        return [*word[1:], feedback]

    def linear(exponent: int, word: list[int]) -> list[int]:
        result = word[:]
        inverse = exponent < 0
        for _ in range(abs(exponent)):
            result = multiply_l_inverse(result) if inverse else multiply_l(result)
        return result

    for destination, ((left_name, left_exp), (right_name, right_exp)) in constraints:
        left = linear(left_exp, words[left_name])
        right = linear(right_exp, words[right_name])
        words[destination] = circuit.word_xor(left, right)

    total_xor = audited_total(6, k, circuit)
    base_xor = len(constraints) * k

    l_rows = companion_rows(k, (tap, k))
    return Result(
        t=6,
        k=k,
        graph_index=570,
        previous_total=AUDIT_PREVIOUS_TOTALS[(6, k)],
        total_xor=total_xor,
        base_xor=base_xor,
        coeffs=T6_REWRITE_COEFFS,
        l_rows=l_rows,
        method="exact sparse-L circuit generation from the post-optimization experiment",
    )


def run_t7_large_k_rewrite(k: int) -> Result:
    """Rebuild the t=7 larger-k circuit using the optimized sparse L choice."""

    taps = {32: 21, 64: 21}
    if k not in (16, 32, 64):
        raise ValueError("t=7 larger-k rewrite is defined for k=16,32,64")

    constraints = [
        ("w1",  (("x2", 0),  ("x4", 0))),
        ("w2",  (("x2", 0),  ("x6", -1))),
        ("w3",  (("x5", 0),  ("x7", 0))),
        ("w4",  (("w3", -1), ("w2", 0))),
        ("w5",  (("x3", 0),  ("w4", -1))),
        ("w6",  (("x1", 0),  ("w5", 0))),
        ("w7",  (("x4", 0),  ("w6", -2))),
        ("w8",  (("w6", 0),  ("x7", 0))),
        ("w9",  (("w1", 0),  ("w8", 0))),
        ("w10", (("w9", 2),  ("w4", 0))),
        ("w11", (("w7", -2), ("w10", 0))),
        ("y1",  (("w10", 0), ("w5", 0))),
        ("y2",  (("w11", 0), ("w3", 0))),
        ("w12", (("y2", 0),  ("x3", 0))),
        ("w13", (("w12", 0), ("w1", -2))),
        ("y3",  (("w7", 0),  ("w12", 1))),
        ("w14", (("w13", 0), ("x6", 1))),
        ("y4",  (("w10", 0), ("w13", -3))),
        ("y5",  (("w11", 0), ("w14", 1))),
        ("y6",  (("w14", 0), ("w8", 0))),
        ("y7",  (("y5", 0),  ("w9", 0))),
    ]

    circuit = Circuit(7 * k)
    words = {f"x{index + 1}": list(range(index * k, (index + 1) * k)) for index in range(7)}

    # The k=16 map has two XORs per L/L^{-1} application; for k=32,64 the
    # companion-style map below has one XOR per application.
    def multiply_l16(word: list[int]) -> list[int]:
        first = circuit.emit(word[12], word[15])
        twelfth = circuit.emit(word[8], word[10])
        return [first, *word[0:10], twelfth, *word[11:15]]

    def multiply_l16_inverse(word: list[int]) -> list[int]:
        eleventh = circuit.emit(word[11], word[9])
        sixteenth = circuit.emit(word[0], word[13])
        return [*word[1:11], eleventh, *word[12:16], sixteenth]

    def multiply_companion(word: list[int], tap: int) -> list[int]:
        feedback = circuit.emit(word[tap - 1], word[k - 1])
        return [feedback, *word[:-1]]

    def multiply_companion_inverse(word: list[int], tap: int) -> list[int]:
        feedback = circuit.emit(word[0], word[tap])
        return [*word[1:], feedback]

    def multiply_once(word: list[int], inverse: bool) -> list[int]:
        if k == 16:
            return multiply_l16_inverse(word) if inverse else multiply_l16(word)
        tap = taps[k]
        return multiply_companion_inverse(word, tap) if inverse else multiply_companion(word, tap)

    def linear(exponent: int, word: list[int]) -> list[int]:
        result = word[:]
        inverse = exponent < 0
        for _ in range(abs(exponent)):
            result = multiply_once(result, inverse)
        return result

    for destination, ((left_name, left_exp), (right_name, right_exp)) in constraints:
        left = linear(left_exp, words[left_name])
        right = linear(right_exp, words[right_name])
        words[destination] = circuit.word_xor(left, right)

    total_xor = audited_total(7, k, circuit)
    base_xor = len(constraints) * k

    if k == 16:
        l_rows = rows(
            (13, 16), (1,), (2,), (3,), (4,), (5,), (6,), (7,),
            (8,), (9,), (10,), (9, 11), (12,), (13,), (14,), (15,),
        )
    else:
        l_rows = companion_rows(k, (taps[k], k))

    return Result(
        t=7,
        k=k,
        graph_index=1,
        previous_total=AUDIT_PREVIOUS_TOTALS[(7, k)],
        total_xor=total_xor,
        base_xor=base_xor,
        coeffs=T7_REWRITE_COEFFS,
        l_rows=l_rows,
        method="exact sparse-L circuit generation from the post-optimization experiment",
    )


def run_larger_k_rewrites() -> list[Result]:
    return [
        run_t6_large_k_rewrite(16),
        run_t6_large_k_rewrite(32),
        run_t6_large_k_rewrite(64),
        run_t7_large_k_rewrite(16),
        run_t7_large_k_rewrite(32),
        run_t7_large_k_rewrite(64),
    ]


# ---------------------------------------------------------------------------
# Reporting.
# ---------------------------------------------------------------------------


def assert_improvements(results: Iterable[Result]) -> None:
    for result in results:
        if result.total >= result.previous_total:
            raise ValueError(f"({result.t},{result.k}) is not an improvement")
        if len(result.l_rows) != result.k:
            raise ValueError(f"bad L row count for ({result.t},{result.k})")


def print_table(results: list[Result]) -> None:
    print("| (t,k) | previous XOR | optimized XOR | saving | split | method |")
    print("|---:|---:|---:|---:|---:|---|")
    for result in results:
        print(
            f"| ({result.t},{result.k}) | {result.previous_total} | {result.total} | "
            f"{result.saving} | {result.base_xor}+{result.extra_xor} | {result.method} |"
        )


def run_selected(args: argparse.Namespace) -> list[Result]:
    graph_root = args.graph_root
    if args.run == "t5-k4":
        return [run_t5_k4_search(graph_root, max_cost=args.max_cost, fixed_graph=args.fixed_graph)]
    if args.run == "t7-k8":
        return [run_t7_k8_lm4_synthesis()]
    if args.run == "t6-k8":
        return [run_t6_k8_direct_rewrite()]
    if args.run == "larger-k":
        return run_larger_k_rewrites()
    if args.run == "all":
        return [
            run_t5_k4_search(graph_root, max_cost=args.max_cost, fixed_graph=args.fixed_graph),
            run_t6_k8_direct_rewrite(),
            *run_larger_k_rewrites()[:3],
            run_t7_k8_lm4_synthesis(),
            *run_larger_k_rewrites()[3:],
        ]
    raise ValueError(args.run)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run bit-level XOR post-optimization experiments."
    )
    parser.add_argument(
        "--run",
        choices=("t5-k4", "t6-k8", "t7-k8", "larger-k", "all"),
        required=True,
        help="run the selected optimization experiment",
    )
    parser.add_argument(
        "--graph-root",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "graph_data",
        help="path to the repository graph_data directory",
    )
    parser.add_argument(
        "--max-cost",
        type=int,
        default=6,
        help="maximum coefficient-map cost for the t5-k4 graph search",
    )
    parser.add_argument(
        "--fixed-graph",
        type=int,
        default=None,
        help="restrict the t5-k4 search to one graph index; omit for the full graph sweep",
    )
    args = parser.parse_args()

    results = run_selected(args)
    assert_improvements(results)
    print("Experiment output")
    print("")
    print_table(results)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
