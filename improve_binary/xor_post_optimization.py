#!/usr/bin/env python3
"""Bit-level XOR post-optimization experiments for binary MDS candidates.

The C++ search program in this directory finds word-level candidates.  The
experiments below are the follow-up bit-level optimizations used for selected
binary-field constructions:

1. search a lower-cost coefficient assignment on a selected SC(5,5,12) graph;
2. apply explicit straight-line programs for repeated coefficient maps;
3. apply a joint/direct bit-level rewrite for a selected word-level node;
4. build sparse-L circuits for larger word sizes.

The search modes do not assume the final answer: they sample coefficient
assignments or sparse linear maps, synthesize local/joint/direct XOR circuits,
test the MDS condition, and report a new best candidate whenever one is found.
Thus repeated long runs may report candidates different from, or better than,
the records in improve_binary_result.txt.
"""

from __future__ import annotations

import argparse
import random
import time
from collections import Counter, deque
from dataclasses import dataclass
from itertools import combinations
from pathlib import Path
from typing import Iterable, Iterator

Coeff = tuple[int, int]
Supports = tuple[tuple[int, ...], ...]


AUDIT_PREVIOUS_TOTALS = {
    (5, 4): 55,
    (5, 8): 103,
    (5, 16): 197,
    (5, 32): 389,
    (5, 64): 773,
    (6, 8): 148,
    (6, 16): 267,
    (6, 32): 522,
    (6, 64): 1034,
    (7, 8): 214,
    (7, 16): 374,
    (7, 32): 691,
    (7, 64): 1363,
}

# For the fixed 16-node t=6,k=8 DAG and the seven exponent slots used below,
# symbolic-minor analysis in the original search showed that MDS candidates can
# only occur for these characteristic polynomials.  Search modes use this as a
# speed filter by default, but it can be disabled from the command line.
T6_K8_ALLOWED_CHARPOLYS = {
    283, 301, 333, 351, 355, 357, 375, 379, 391, 395, 397, 419, 433, 445,
    451, 463, 471, 477, 487, 501,
}


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


def matrix_add(left: tuple[int, ...], right: tuple[int, ...]) -> tuple[int, ...]:
    return tuple(a ^ b for a, b in zip(left, right))


def rank_binary(rows: Iterable[int]) -> int:
    basis: dict[int, int] = {}
    for original in rows:
        row = original
        while row:
            pivot = row.bit_length() - 1
            if pivot in basis:
                row ^= basis[pivot]
            else:
                basis[pivot] = row
                break
    return len(basis)


def block_matrix_to_packed_rows(blocks: list[list[tuple[int, ...]]], k: int) -> list[int]:
    t = len(blocks)
    return [
        sum(blocks[block_row][block_column][bit_row] << (k * block_column)
            for block_column in range(t))
        for block_row in range(t)
        for bit_row in range(k)
    ]


def minor_specs(t: int):
    return [
        (size, row_words, column_words)
        for size in range(1, t + 1)
        for row_words in combinations(range(t), size)
        for column_words in combinations(range(t), size)
    ]


def packed_minor_rows(matrix_rows: list[int], k: int, row_words, column_words) -> list[int]:
    rows_out = []
    for block_row in row_words:
        for bit_row in range(k):
            source = matrix_rows[k * block_row + bit_row]
            packed = 0
            for local, block_column in enumerate(column_words):
                packed |= ((source >> (k * block_column)) & ((1 << k) - 1)) << (k * local)
            rows_out.append(packed)
    return rows_out


def blocks_invertible_packed(matrix_rows: list[int], t: int, k: int) -> bool:
    for block_row in range(t):
        for block_column in range(t):
            block = [
                (matrix_rows[k * block_row + bit] >> (k * block_column)) & ((1 << k) - 1)
                for bit in range(k)
            ]
            if rank_binary(block) != k:
                return False
    return True


def full_mds_packed(matrix_rows: list[int], t: int, k: int) -> bool:
    for size, row_words, column_words in minor_specs(t):
        if rank_binary(packed_minor_rows(matrix_rows, k, row_words, column_words)) != k * size:
            return False
    return True


def closure_path(required: frozenset[int], k: int) -> tuple[tuple[int, int, int], ...] | None:
    available = set(identity_rows(k))
    remaining = set(required) - available
    path: list[tuple[int, int, int]] = []
    while remaining:
        made = None
        forms = list(available)
        for ai, left in enumerate(forms):
            for right in forms[ai + 1:]:
                candidate = left ^ right
                if candidate in remaining:
                    made = (left, right, candidate)
                    break
            if made is not None:
                break
        if made is None:
            return None
        path.append(made)
        available.add(made[2])
        remaining.remove(made[2])
    return tuple(path)


def synth_at_most(target_rows: tuple[int, ...], k: int, limit: int) -> tuple[tuple[int, int, int], ...] | None:
    targets = frozenset(target_rows)
    inputs = set(identity_rows(k))
    noninputs = targets - inputs
    minimum = len(noninputs)
    if minimum > limit:
        return None
    universe = [value for value in range(1, 1 << k) if value not in inputs | targets]
    for cost in range(minimum, limit + 1):
        for extras in combinations(universe, cost - minimum):
            path = closure_path(frozenset(noninputs | set(extras)), k)
            if path is not None and targets <= inputs | {result for _left, _right, result in path}:
                return path
    return None


def randomized_paar(ninputs: int, targets: list[int], seed: int, window: int = 4):
    """Randomized common-subexpression synthesis for linear XOR targets."""

    rng = random.Random(seed)
    expressions = [{bit for bit in range(ninputs) if (target >> bit) & 1} for target in targets]
    forms = [1 << bit for bit in range(ninputs)]
    lookup = {form: wire for wire, form in enumerate(forms)}
    gates: list[tuple[int, int]] = []

    while True:
        counts: Counter[tuple[int, int]] = Counter()
        for expression in expressions:
            terms = list(expression)
            for i, left in enumerate(terms):
                for right in terms[:i]:
                    counts[(min(left, right), max(left, right))] += 1
        if not counts:
            break
        best = max(counts.values())
        if best < 2:
            break
        choices = [
            (pair, count)
            for pair, count in counts.items()
            if count >= max(2, best - window + 1)
        ]
        (left, right), _count = rng.choices(
            choices, weights=[(count - 1) ** 3 for _pair, count in choices], k=1
        )[0]
        form = forms[left] ^ forms[right]
        output = lookup.get(form)
        if output is None:
            output = len(forms)
            forms.append(form)
            lookup[form] = output
            gates.append((left, right))
        for expression in expressions:
            if left in expression and right in expression:
                expression ^= {left, right, output}

    outputs = [-1] * len(expressions)
    order = list(range(len(expressions)))
    rng.shuffle(order)
    for row in order:
        terms = list(expressions[row])
        rng.shuffle(terms)
        while len(terms) > 1:
            reusable = None
            for i, left in enumerate(terms):
                for j in range(i):
                    right = terms[j]
                    form = forms[left] ^ forms[right]
                    if form in lookup:
                        reusable = i, j, lookup[form]
                        break
                if reusable is not None:
                    break
            if reusable is None:
                left = terms.pop()
                right = terms.pop()
                form = forms[left] ^ forms[right]
                output = lookup.get(form)
                if output is None:
                    output = len(forms)
                    forms.append(form)
                    lookup[form] = output
                    gates.append((left, right))
            else:
                i, j, output = reusable
                for index in sorted((i, j), reverse=True):
                    terms.pop(index)
            terms.append(output)
        outputs[row] = terms[0]

    if [forms[wire] for wire in outputs] != targets:
        raise AssertionError("Paar synthesis produced wrong target forms")
    return tuple(gates), tuple(outputs)


def best_randomized_paar(
    ninputs: int,
    targets: list[int],
    *,
    seeds: int,
    seed_base: int,
    window: int,
) -> tuple[tuple[tuple[int, int], ...], tuple[int, ...]]:
    best = None
    for offset in range(seeds):
        candidate = randomized_paar(ninputs, targets, seed_base + offset, window=window)
        if best is None or len(candidate[0]) < len(best[0]):
            best = candidate
    if best is None:
        raise ValueError("at least one Paar seed is required")
    return best


def legacy_paar(target_rows: tuple[int, ...], seed: int):
    """Paar-style local synthesis used by the original scalar-record searches."""

    rng = random.Random(seed)
    k = len(target_rows)
    signals = [1 << bit for bit in range(k)]
    expressions = [
        {index for index, signal in enumerate(signals) if row & signal}
        for row in target_rows
    ]
    gates: list[tuple[int, int]] = []

    while True:
        counts: dict[tuple[int, int], int] = {}
        for expression in expressions:
            for left, right in combinations(sorted(expression), 2):
                counts[left, right] = counts.get((left, right), 0) + 1
        best = max(counts.values(), default=1)
        if best < 2:
            break
        left, right = rng.choice([pair for pair, count in counts.items() if count == best])
        output = len(signals)
        signals.append(signals[left] ^ signals[right])
        gates.append((left, right))
        for expression in expressions:
            if left in expression and right in expression:
                expression.remove(left)
                expression.remove(right)
                expression.add(output)

    known = {form: wire for wire, form in enumerate(signals)}
    outputs = []
    for expression in expressions:
        wires = list(expression)
        while len(wires) > 1:
            left = wires.pop()
            right = wires.pop()
            form = signals[left] ^ signals[right]
            output = known.get(form)
            if output is None:
                output = len(signals)
                signals.append(form)
                known[form] = output
                gates.append((left, right))
            wires.append(output)
        outputs.append(wires[0])

    if [signals[wire] for wire in outputs] != list(target_rows):
        raise AssertionError("legacy Paar synthesis produced wrong target forms")
    return tuple(gates), tuple(outputs)


def best_legacy_paar(
    target_rows: tuple[int, ...],
    *,
    seeds: int,
    seed_base: int = 0,
) -> tuple[tuple[tuple[int, int], ...], tuple[int, ...]]:
    best = None
    for offset in range(seeds):
        candidate = legacy_paar(target_rows, seed_base + offset)
        if best is None or len(candidate[0]) < len(best[0]):
            best = candidate
    if best is None:
        raise ValueError("at least one Paar seed is required")
    return best


def charpoly8(matrix: tuple[int, ...]) -> int:
    """Characteristic polynomial of an 8x8 binary matrix, encoded as a bitset."""

    dp = {0: 1}
    for row in range(8):
        next_dp: dict[int, int] = {}
        for mask, value in dp.items():
            for column in range(8):
                if (mask >> column) & 1:
                    continue
                entry = (matrix[row] >> column) & 1
                if row == column:
                    product = value << 1
                    if entry:
                        product ^= value
                elif entry:
                    product = value
                else:
                    continue
                new_mask = mask | (1 << column)
                next_dp[new_mask] = next_dp.get(new_mask, 0) ^ product
        dp = next_dp
    return dp.get(255, 0)


def rank_k(rows_in: tuple[int, ...], k: int) -> int:
    return rank_binary(row & ((1 << k) - 1) for row in rows_in)


def random_cost2_general(rng: random.Random, k: int = 8):
    """Sample an invertible map implementable by a permutation plus two XORs."""

    if k != 8:
        raise ValueError("the changed-L search currently samples k=8 maps")
    first_left, first_right = rng.sample(range(k), 2)
    forms = list(identity_rows(k))
    forms.append(forms[first_left] ^ forms[first_right])
    while True:
        second_left, second_right = rng.sample(range(k + 1), 2)
        second = forms[second_left] ^ forms[second_right]
        if second and second not in forms:
            break
    forms.append(second)
    for _ in range(20):
        chosen = rng.sample(range(k + 1), k - 1) + [k + 1]
        row_values = [forms[index] for index in chosen]
        if rank_k(tuple(row_values), k) == k:
            rng.shuffle(row_values)
            return tuple(row_values), ((first_left, first_right), (second_left, second_right))
    return None


def constraints_with_slot_exponents(constraints, slot_exponents: Iterable[int]):
    exponent_iterator = iter(slot_exponents)
    output = []
    for destination, terms in constraints:
        changed_terms = []
        for source, exponent in terms:
            changed_terms.append((source, next(exponent_iterator) if exponent else 0))
        output.append((destination, tuple(changed_terms)))
    try:
        next(exponent_iterator)
    except StopIteration:
        return output
    raise ValueError("too many slot exponents")


def constraints_to_coeffs(constraints) -> tuple[Coeff, ...]:
    return tuple((int(terms[0][1]), int(terms[1][1])) for _destination, terms in constraints)


def matrix_rows_to_supports(matrix: tuple[int, ...]) -> Supports:
    return tuple(
        tuple(column + 1 for column in range(len(matrix)) if (row >> column) & 1)
        for row in matrix
    )


def transfer_blocks_from_constraints(t: int, l_matrix: tuple[int, ...], constraints) -> list[list[tuple[int, ...]]]:
    k = len(l_matrix)
    powers: dict[int, tuple[int, ...]] = {0: identity_rows(k)}

    def power(exponent: int) -> tuple[int, ...]:
        if exponent not in powers:
            powers[exponent] = matrix_power(l_matrix, exponent)
        return powers[exponent]

    zero = tuple(0 for _ in range(k))
    one = identity_rows(k)
    values = {
        f"x{row + 1}": [one if row == column else zero for column in range(t)]
        for row in range(t)
    }
    for destination, terms in constraints:
        result = [zero] * t
        for source, exponent in terms:
            coefficient = power(exponent)
            result = [
                matrix_add(old, matrix_multiply(coefficient, value))
                for old, value in zip(result, values[source])
            ]
        values[destination] = result
    return [values[f"y{row + 1}"] for row in range(t)]


def constraints_are_mds(t: int, l_matrix: tuple[int, ...], constraints) -> bool:
    k = len(l_matrix)
    packed = block_matrix_to_packed_rows(transfer_blocks_from_constraints(t, l_matrix, constraints), k)
    return full_mds_packed(packed, t, k)


def exponent_tuples_by_weight(slot_count: int, budget: int) -> Iterator[tuple[int, ...]]:
    domain = range(-budget, budget + 1)

    def visit(slot: int, remaining: int, current: list[int]) -> Iterator[tuple[int, ...]]:
        if slot == slot_count:
            yield tuple(current)
            return
        for exponent in domain:
            cost = abs(exponent)
            if cost <= remaining:
                current.append(exponent)
                yield from visit(slot + 1, remaining - cost, current)
                current.pop()

    yield from visit(0, budget, [])


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
# Sparse-L and exponent-slot search data for k=16,32,64.
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

T7_K8_RESULT_COEFFS = (
    (0, 0), (0, 0), (-4, 0), (0, 0), (0, -1), (0, -1), (0, -2),
    (0, 0), (0, 0), (5, 1), (0, 0), (0, 0), (0, 0), (-1, 0),
    (0, 0), (0, 0), (-4, 0), (1, 0), (2, -1), (0, 0), (0, 0),
)


T5_K4_8_BASE_CONSTRAINTS = [
    ("w1",  (("x2", 0),  ("x5", 0))),
    ("w2",  (("x4", 0),  ("w1", 0))),
    ("w3",  (("x3", -1), ("w2", 0))),
    ("w4",  (("x1", 0),  ("w3", 0))),
    ("w5",  (("x3", 0),  ("w4", -2))),
    ("y4",  (("w1", -1), ("w4", 0))),
    ("w6",  (("x5", 0),  ("w5", 0))),
    ("y1",  (("x4", 0),  ("w6", 0))),
    ("y5",  (("w3", 0),  ("w6", -2))),
    ("w7",  (("y4", 0),  ("y5", 0))),
    ("y2",  (("w2", 1),  ("w7", 0))),
    ("y3",  (("w5", 0),  ("w7", 0))),
]

T5_LARGE_BASE_CONSTRAINTS = [
    ("w1",  (("x2", 0),  ("x5", 0))),
    ("w2",  (("x3", 0),  ("w1", -1))),
    ("w3",  (("x4", 0),  ("w1", 0))),
    ("w4",  (("x1", 0),  ("w3", 0))),
    ("y4",  (("w2", 0),  ("w4", 0))),
    ("w5",  (("w3", 1),  ("y4", 0))),
    ("w6",  (("x5", 0),  ("w5", 1))),
    ("w7",  (("x3", 0),  ("w6", 1))),
    ("y1",  (("w2", 0),  ("w6", 0))),
    ("y2",  (("x4", 0),  ("w7", 0))),
    ("y5",  (("w4", -1), ("w7", 0))),
    ("y3",  (("w5", 0),  ("y5", 0))),
]


T6_K8_OLD_L_ROWS = rows((8,), (1, 2), (2, 8), (3,), (4,), (5,), (6,), (7,))

T6_K8_REWRITE_CONSTRAINTS = [
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

T6_K8_SLOT_LAYOUT = (
    (3, 1),
    (6, 1),
    (7, 1),
    (8, 1),
    (9, 1),
    (11, 0),
    (11, 1),
)

T6_K8_REWRITE_SLOT_EXPONENTS = (1, -2, 1, -1, -1, 1, -2)


T7_K8_BASE_CONSTRAINTS = [
    ("w1",  (("x2", 0),   ("x4", 0))),
    ("w2",  (("x6", 0),   ("x7", 0))),
    ("w3",  (("w2", -4),  ("x5", 0))),
    ("w4",  (("x2", 0),   ("w3", 0))),
    ("w5",  (("x3", 0),   ("w4", -1))),
    ("w6",  (("x1", 0),   ("w5", -1))),
    ("w7",  (("x4", 0),   ("w6", -2))),
    ("w8",  (("w6", 0),   ("x7", 0))),
    ("w9",  (("w1", 0),   ("w8", 0))),
    ("w10", (("w9", 5),   ("w4", 1))),
    ("w11", (("w7", 0),   ("w10", 0))),
    ("y1",  (("w10", 0),  ("w5", 0))),
    ("y2",  (("w11", 0),  ("w2", 0))),
    ("w12", (("y2", 0),   ("x3", 0))),
    ("w13", (("w12", 0),  ("w1", 0))),
    ("y3",  (("w7", -4),  ("w12", 0))),
    ("w14", (("w13", -1), ("x5", 0))),
    ("y4",  (("w10", 1),  ("w13", 0))),
    ("y5",  (("w11", 2),  ("w14", -1))),
    ("y6",  (("w14", 0),  ("w8", 0))),
    ("y7",  (("y5", 0),   ("w9", 0))),
]

T7_K8_BASELINE_EXPONENTS = (-4, -1, -1, -2, 5, 1, -4, -1, 1, 2, -1)

T7_LARGE_BASE_CONSTRAINTS = [
    ("w1",  (("x2", 0),   ("x4", 0))),
    ("w2",  (("x2", 0),   ("x6", -1))),
    ("w3",  (("x5", 0),   ("x7", 0))),
    ("w4",  (("w3", -1),  ("w2", 0))),
    ("w5",  (("x3", 0),   ("w4", -1))),
    ("w6",  (("x1", 0),   ("w5", 0))),
    ("w7",  (("x4", 0),   ("w6", -2))),
    ("w8",  (("w6", 0),   ("x7", 0))),
    ("w9",  (("w1", 0),   ("w8", 0))),
    ("w10", (("w9", 2),   ("w4", 0))),
    ("w11", (("w7", -2),  ("w10", 0))),
    ("y1",  (("w10", 0),  ("w5", 0))),
    ("y2",  (("w11", 0),  ("w3", 0))),
    ("w12", (("y2", 0),   ("x3", 0))),
    ("w13", (("w12", 0),  ("w1", -3))),
    ("y3",  (("w7", 0),   ("w12", 1))),
    ("w14", (("w13", 0),  ("x6", 1))),
    ("y4",  (("w10", 0),  ("w13", -3))),
    ("y5",  (("w11", -1), ("w14", 1))),
    ("y6",  (("w14", 0),  ("w8", 0))),
    ("y7",  (("y5", 0),   ("w9", 0))),
]

T7_LARGE_BASELINE_EXPONENTS = (-1, -1, -1, -2, 2, -2, -3, 1, 1, -3, -1, 1)
T7_LARGE_FINAL_EXPONENTS = (-1, -1, -1, -2, 2, -2, -2, 1, 1, -3, 0, 1)


def signed_exponent_mod_order(exponent: int, order: int) -> int:
    return exponent if exponent <= order // 2 else exponent - order


def t6_slot_exponents_to_coeffs(slot_exponents: Iterable[int]) -> tuple[Coeff, ...]:
    coeffs = [[0, 0] for _ in range(len(T6_K8_REWRITE_CONSTRAINTS))]
    for exponent, (node_index, side) in zip(slot_exponents, T6_K8_SLOT_LAYOUT):
        coeffs[node_index][side] = exponent
    return tuple((left, right) for left, right in coeffs)


def t6_transfer_blocks_from_coeffs(
    t: int,
    l_matrix: tuple[int, ...],
    coeffs: tuple[Coeff, ...],
) -> list[list[tuple[int, ...]]]:
    k = len(l_matrix)
    powers: dict[int, tuple[int, ...]] = {0: identity_rows(k)}

    def power(exponent: int) -> tuple[int, ...]:
        if exponent not in powers:
            powers[exponent] = matrix_power(l_matrix, exponent)
        return powers[exponent]

    zero = tuple(0 for _ in range(k))
    values = {
        f"x{row + 1}": [identity_rows(k) if row == column else zero for column in range(t)]
        for row in range(t)
    }
    for (destination, ((left_name, _left_old), (right_name, _right_old))), (left_exp, right_exp) in zip(
        T6_K8_REWRITE_CONSTRAINTS, coeffs
    ):
        left_power = power(left_exp)
        right_power = power(right_exp)
        values[destination] = [
            matrix_add(
                matrix_multiply(left_power, values[left_name][column]),
                matrix_multiply(right_power, values[right_name][column]),
            )
            for column in range(t)
        ]
    return [values[f"y{row + 1}"] for row in range(t)]


def t6_transfer_blocks_from_slot_exponents(
    l_matrix: tuple[int, ...],
    slot_exponents: Iterable[int],
) -> list[list[tuple[int, ...]]]:
    return t6_transfer_blocks_from_coeffs(6, l_matrix, t6_slot_exponents_to_coeffs(slot_exponents))


def t6_k8_search_scalar_slots(args: argparse.Namespace) -> list[Result]:
    """Search the seven scalar slots on the fixed old t=6,k=8 map."""

    started = time.time()
    k = 8
    l_matrix = support_rows_to_masks(T6_K8_OLD_L_ROWS)
    costs: dict[int, int] = {0: 0}
    print("building observed SLP cost table", flush=True)
    for exponent_mod in range(1, 255):
        exponent = signed_exponent_mod_order(exponent_mod, 255)
        targets = list(matrix_power(l_matrix, exponent))
        gates, _outputs = best_legacy_paar(
            tuple(targets),
            seeds=args.paar_seeds,
            seed_base=args.seed,
        )
        costs[exponent_mod] = len(gates)

    max_power_cost = args.max_power_cost if args.max_power_cost is not None else args.scalar_budget
    domain = [
        exponent_mod
        for exponent_mod, cost in costs.items()
        if cost <= max_power_cost and cost <= args.scalar_budget
    ]
    domain.sort(key=lambda exponent_mod: (
        costs[exponent_mod],
        min(exponent_mod, 255 - exponent_mod),
        exponent_mod,
    ))
    print(
        "cost table",
        {cost: sum(1 for value in costs.values() if value == cost) for cost in sorted(set(costs.values()))},
        "domain",
        len(domain),
        flush=True,
    )

    tested = 0
    best_result: Result | None = None
    selected: list[int] = []

    def visit(remaining: int) -> bool:
        nonlocal tested, best_result
        if len(selected) == len(T6_K8_SLOT_LAYOUT):
            tested += 1
            cost = sum(costs[exponent] for exponent in selected)
            signed = tuple(signed_exponent_mod_order(exponent, 255) for exponent in selected)
            packed = block_matrix_to_packed_rows(t6_transfer_blocks_from_slot_exponents(l_matrix, signed), k)
            if full_mds_packed(packed, 6, k):
                result = Result(
                    t=6,
                    k=8,
                    graph_index=570,
                    previous_total=AUDIT_PREVIOUS_TOTALS[(6, 8)],
                    total_xor=16 * k + cost,
                    base_xor=16 * k,
                    coeffs=(
                        T6_REWRITE_COEFFS
                        if signed == T6_K8_REWRITE_SLOT_EXPONENTS
                        else t6_slot_exponents_to_coeffs(signed)
                    ),
                    l_rows=T6_K8_OLD_L_ROWS,
                    method=f"searched seven scalar slots, tested {tested} assignments",
                )
                if best_result is None or result.total < best_result.total:
                    best_result = result
                    print("BEST_SEARCH", result.result_line(), "elapsed", time.time() - started, flush=True)
                if args.stop_first:
                    return True
            if tested % args.report == 0:
                best = best_result.total if best_result else None
                print("progress", tested, "best", best, "elapsed", time.time() - started, flush=True)
            return False

        for exponent in domain:
            cost = costs[exponent]
            if cost > remaining:
                break
            selected.append(exponent)
            should_stop = visit(remaining - cost)
            selected.pop()
            if should_stop:
                return True
        return False

    visit(args.scalar_budget)
    if best_result is None:
        print(
            "NO_SOLUTION",
            {"tested": tested, "budget": args.scalar_budget, "elapsed": time.time() - started},
            flush=True,
        )
        return []
    return [best_result]


def t6_k8_search_changed_l(args: argparse.Namespace) -> list[Result]:
    """Randomly search sparse L maps and synthesize joint/direct bit circuits."""

    rng = random.Random(args.seed)
    started = time.time()
    seen: set[tuple[int, ...]] = set()
    valid_charpoly = cheap_inverse = cheap_square = mds_count = structured = 0
    best_result: Result | None = None

    for trial in range(1, args.search_trials + 1):
        generated = random_cost2_general(rng, 8)
        if generated is None:
            continue
        l_matrix, generator = generated
        if l_matrix in seen:
            continue
        seen.add(l_matrix)
        if not args.allow_any_charpoly and charpoly8(l_matrix) not in T6_K8_ALLOWED_CHARPOLYS:
            continue
        valid_charpoly += 1

        l_path = synth_at_most(l_matrix, 8, args.l_limit)
        if l_path is None:
            continue
        l_inverse = matrix_inverse(l_matrix)
        linv_path = synth_at_most(l_inverse, 8, args.inverse_limit)
        if linv_path is None:
            continue
        cheap_inverse += 1
        l_inverse_square = matrix_multiply(l_inverse, l_inverse)
        lm2_path = synth_at_most(l_inverse_square, 8, args.square_limit)
        if lm2_path is None:
            continue
        cheap_square += 1

        constraint_coeffs = t6_slot_exponents_to_coeffs(T6_K8_REWRITE_SLOT_EXPONENTS)
        packed = block_matrix_to_packed_rows(t6_transfer_blocks_from_coeffs(6, l_matrix, constraint_coeffs), 8)
        if not blocks_invertible_packed(packed, 6, 8):
            continue
        if not full_mds_packed(packed, 6, 8):
            continue
        mds_count += 1

        joint_path = synth_at_most(l_matrix + l_inverse_square, 8, args.joint_limit)
        if joint_path is None:
            continue
        structured += 1

        targets = [l_matrix[row] | (l_inverse_square[row] << 8) for row in range(8)]
        direct_gates, direct_outputs = best_randomized_paar(
            16,
            targets,
            seeds=args.paar_seeds,
            seed_base=args.seed * 10_000_019 + trial * 101,
            window=args.paar_window,
        )
        direct_cost = len(direct_gates)
        total = (
            15 * 8
            + len(l_path)
            + len(joint_path)
            + 2 * len(linv_path)
            + direct_cost
        )

        if best_result is None or total < best_result.total:
            l_rows = tuple(
                tuple(column + 1 for column in range(8) if (row >> column) & 1)
                for row in l_matrix
            )
            best_result = Result(
                t=6,
                k=8,
                graph_index=570,
                previous_total=AUDIT_PREVIOUS_TOTALS[(6, 8)],
                total_xor=total,
                base_xor=15 * 8,
                coeffs=T6_REWRITE_COEFFS,
                l_rows=l_rows,
                method=(
                    "searched sparse L with joint/direct SLPs "
                    f"trial={trial}, direct={direct_cost}, generator={generator}, "
                    f"direct_outputs={direct_outputs}"
                ),
            )
            print("BEST_SEARCH", best_result.result_line(), "elapsed", time.time() - started, flush=True)

        if args.stop_at is not None and best_result is not None and best_result.total <= args.stop_at:
            break
        if trial % args.report == 0:
            print(
                "progress",
                {
                    "trial": trial,
                    "unique": len(seen),
                    "valid_charpoly": valid_charpoly,
                    "cheap_inverse": cheap_inverse,
                    "cheap_square": cheap_square,
                    "mds": mds_count,
                    "structured": structured,
                    "best": best_result.total if best_result else None,
                    "elapsed": time.time() - started,
                },
                flush=True,
            )

    if best_result is None:
        print(
            "NO_SOLUTION",
            {
                "trials": args.search_trials,
                "unique": len(seen),
                "valid_charpoly": valid_charpoly,
                "cheap_inverse": cheap_inverse,
                "cheap_square": cheap_square,
                "mds": mds_count,
                "structured": structured,
                "elapsed": time.time() - started,
            },
            flush=True,
        )
        return []
    return [best_result]


def t5_search_no_better_lift(args: argparse.Namespace) -> list[Result]:
    """Exhaust the restricted t=5 larger-word lift checks."""

    k = args.k or 8
    if k not in (8, 16, 32, 64):
        raise ValueError("t5 restricted search is defined for k=8,16,32,64")
    constraints = T5_K4_8_BASE_CONSTRAINTS if k == 8 else T5_LARGE_BASE_CONSTRAINTS
    budget = args.t5_budget if args.t5_budget is not None else (6 if k == 8 else 4)
    slot_count = sum(1 for _destination, terms in constraints for _source, exponent in terms if exponent)
    tap_candidates = [args.tap] if args.tap is not None else list(range(1, k))
    exponent_tuples = list(exponent_tuples_by_weight(slot_count, budget))
    started = time.time()
    tested = passed_one_by_one = 0
    best_result: Result | None = None

    for tap in tap_candidates:
        l_rows = companion_rows(k, (tap, k))
        l_matrix = support_rows_to_masks(l_rows)
        tap_one_by_one = tap_mds = 0
        for exponents in exponent_tuples:
            tested += 1
            changed = constraints_with_slot_exponents(constraints, exponents)
            blocks = transfer_blocks_from_constraints(5, l_matrix, changed)
            if any(rank_binary(block) != k for block_row in blocks for block in block_row):
                continue
            passed_one_by_one += 1
            tap_one_by_one += 1
            packed = block_matrix_to_packed_rows(blocks, k)
            if not full_mds_packed(packed, 5, k):
                continue
            tap_mds += 1
            total = 12 * k + sum(abs(exponent) for exponent in exponents)
            result = Result(
                t=5,
                k=k,
                graph_index=126 if k > 8 else 246,
                previous_total=AUDIT_PREVIOUS_TOTALS[(5, k)],
                total_xor=total,
                base_xor=12 * k,
                coeffs=constraints_to_coeffs(changed),
                l_rows=l_rows,
                method=f"restricted t5 lift search tap={tap}, tested={tested}",
            )
            if best_result is None or result.total < best_result.total:
                best_result = result
                print("BEST_SEARCH", result.result_line(), "elapsed", time.time() - started, flush=True)
        print("tap", tap, "one_by_one", tap_one_by_one, "MDS", tap_mds, flush=True)

    if best_result is None:
        print(
            "NO_IMPROVEMENT_IN_RESTRICTED_FAMILY",
            {
                "t": 5,
                "k": k,
                "budget": budget,
                "taps": len(tap_candidates),
                "assignments_per_tap": len(exponent_tuples),
                "tested": tested,
                "passed_one_by_one": passed_one_by_one,
                "elapsed": time.time() - started,
            },
            flush=True,
        )
        return []
    return [best_result]


def t6_large_search_sparse_l(args: argparse.Namespace) -> list[Result]:
    """Search the sparse one-XOR companion L family for the t=6 large-word records."""

    record_taps = {16: 1, 32: 21, 64: 21}
    sizes = [args.k] if args.k else [16, 32, 64]
    results: list[Result] = []
    started = time.time()
    for k in sizes:
        if k not in (16, 32, 64):
            raise ValueError("t6 large sparse-L search is defined for k=16,32,64")
        if args.tap is not None:
            tap_candidates = [args.tap]
        elif args.record_taps:
            tap_candidates = [record_taps[k]]
        else:
            tap_candidates = list(range(1, k))
        best_result: Result | None = None
        tested = 0
        for tap in tap_candidates:
            tested += 1
            l_rows = companion_rows(k, (tap, k))
            l_matrix = support_rows_to_masks(l_rows)
            constraints = constraints_with_slot_exponents(
                T6_K8_REWRITE_CONSTRAINTS,
                T6_K8_REWRITE_SLOT_EXPONENTS,
            )
            if not constraints_are_mds(6, l_matrix, constraints):
                continue
            total = 16 * k + sum(abs(exponent) for exponent in T6_K8_REWRITE_SLOT_EXPONENTS)
            result = Result(
                t=6,
                k=k,
                graph_index=570,
                previous_total=AUDIT_PREVIOUS_TOTALS[(6, k)],
                total_xor=total,
                base_xor=16 * k,
                coeffs=T6_REWRITE_COEFFS,
                l_rows=l_rows,
                method=f"searched one-XOR companion L tap={tap}, tested={tested}",
            )
            if best_result is None or result.total < best_result.total:
                best_result = result
                print("BEST_SEARCH", result.result_line(), "elapsed", time.time() - started, flush=True)
            if args.stop_first:
                break
        if best_result is None:
            print("NO_SOLUTION", {"t": 6, "k": k, "tested_taps": tested}, flush=True)
        else:
            results.append(best_result)
    return results


def t7_k8_search_local_slps(args: argparse.Namespace) -> list[Result]:
    """Re-run the t=7,k=8 scalar-slot/local-SLP search from the provenance record."""

    started = time.time()
    k = 8
    l_rows = T6_K8_OLD_L_ROWS
    l_matrix = support_rows_to_masks(l_rows)
    print("building observed SLP cost table", flush=True)
    synthesis_costs: dict[int, int] = {}
    for exponent_mod in range(255):
        exponent = signed_exponent_mod_order(exponent_mod, 255)
        gates, _outputs = best_legacy_paar(
            matrix_power(l_matrix, exponent),
            seeds=args.paar_seeds,
            seed_base=args.seed,
        )
        synthesis_costs[exponent_mod] = len(gates)

    baseline_mod = tuple(exponent % 255 for exponent in T7_K8_BASELINE_EXPONENTS)
    baseline_overhead = sum(synthesis_costs[exponent] for exponent in baseline_mod)
    baseline_constraints = constraints_with_slot_exponents(T7_K8_BASE_CONSTRAINTS, T7_K8_BASELINE_EXPONENTS)
    if not constraints_are_mds(7, l_matrix, baseline_constraints):
        raise AssertionError("t7-k8 baseline is not MDS")

    best_overhead = baseline_overhead
    best_constraints = baseline_constraints
    best_slot_exponents = T7_K8_BASELINE_EXPONENTS
    best_kind = "same_exponents_multiplier_resynthesis"
    print(
        "BASELINE_SEARCH",
        {
            "old_record_overhead": 46,
            "resynthesized_overhead": baseline_overhead,
            "costs": [synthesis_costs[exponent] for exponent in baseline_mod],
        },
        flush=True,
    )

    tested_single = tested_pair = 0
    for position in range(len(baseline_mod)):
        for exponent_mod in sorted(range(255), key=lambda exponent: (synthesis_costs[exponent], exponent)):
            candidate = list(baseline_mod)
            candidate[position] = exponent_mod
            overhead = sum(synthesis_costs[exponent] for exponent in candidate)
            if overhead >= best_overhead:
                continue
            tested_single += 1
            signed = tuple(signed_exponent_mod_order(exponent, 255) for exponent in candidate)
            changed = constraints_with_slot_exponents(T7_K8_BASE_CONSTRAINTS, signed)
            if not constraints_are_mds(7, l_matrix, changed):
                continue
            best_overhead = overhead
            best_constraints = changed
            best_slot_exponents = signed
            best_kind = f"single_slot position={position}"
            print("FOUND_SEARCH", best_kind, "overhead", overhead, flush=True)

    domain = [
        exponent_mod
        for exponent_mod in range(255)
        if synthesis_costs[exponent_mod] <= args.pair_cost_cap
    ]
    if best_overhead == baseline_overhead:
        for first, second in combinations(range(len(baseline_mod)), 2):
            found = False
            for left in domain:
                for right in domain:
                    candidate = list(baseline_mod)
                    candidate[first] = left
                    candidate[second] = right
                    overhead = sum(synthesis_costs[exponent] for exponent in candidate)
                    if overhead >= best_overhead:
                        continue
                    tested_pair += 1
                    signed = tuple(signed_exponent_mod_order(exponent, 255) for exponent in candidate)
                    changed = constraints_with_slot_exponents(T7_K8_BASE_CONSTRAINTS, signed)
                    if not constraints_are_mds(7, l_matrix, changed):
                        continue
                    best_overhead = overhead
                    best_constraints = changed
                    best_slot_exponents = signed
                    best_kind = f"two_slot positions=({first},{second})"
                    print("FOUND_SEARCH", best_kind, "overhead", overhead, flush=True)
                    found = True
                    break
                if found:
                    break
            if found:
                break

    result = Result(
        t=7,
        k=8,
        graph_index=2,
        previous_total=AUDIT_PREVIOUS_TOTALS[(7, 8)],
        total_xor=21 * k + best_overhead,
        base_xor=21 * k,
        coeffs=(
            T7_K8_RESULT_COEFFS
            if best_slot_exponents == T7_K8_BASELINE_EXPONENTS
            else constraints_to_coeffs(best_constraints)
        ),
        l_rows=l_rows,
        method=(
            f"searched local SLP costs and scalar slots; kind={best_kind}, "
            f"single={tested_single}, pair={tested_pair}"
        ),
    )
    print("BEST_SEARCH", result.result_line(), "elapsed", time.time() - started, flush=True)
    return [result]


def t7_large_search_exponents(args: argparse.Namespace) -> list[Result]:
    """Search the t=7,k=16 exponent normalization improvement, then lift it."""

    started = time.time()
    k = 16
    l_rows = rows(
        (13, 16), (1,), (2,), (3,), (4,), (5,), (6,), (7,),
        (8,), (9,), (10,), (9, 11), (12,), (13,), (14,), (15,),
    )
    l_matrix = support_rows_to_masks(l_rows)
    baseline_constraints = constraints_with_slot_exponents(
        T7_LARGE_BASE_CONSTRAINTS,
        T7_LARGE_BASELINE_EXPONENTS,
    )
    if not constraints_are_mds(7, l_matrix, baseline_constraints):
        raise AssertionError("t7 large baseline is not MDS at k=16")

    best_weight = sum(abs(exponent) for exponent in T7_LARGE_BASELINE_EXPONENTS)
    best_exponents = T7_LARGE_BASELINE_EXPONENTS
    tested_single = tested_pair = 0
    single_domain = sorted(
        range(-args.single_limit, args.single_limit + 1),
        key=lambda exponent: (abs(exponent), exponent),
    )
    for position in range(len(best_exponents)):
        for exponent in single_domain:
            candidate = list(T7_LARGE_BASELINE_EXPONENTS)
            candidate[position] = exponent
            weight = sum(abs(value) for value in candidate)
            if weight >= best_weight:
                continue
            tested_single += 1
            changed = constraints_with_slot_exponents(T7_LARGE_BASE_CONSTRAINTS, candidate)
            if not constraints_are_mds(7, l_matrix, changed):
                continue
            best_weight = weight
            best_exponents = tuple(candidate)
            print("BEST_WEIGHT", best_weight, "kind", "single", "position", position, flush=True)

    if best_weight == sum(abs(exponent) for exponent in T7_LARGE_BASELINE_EXPONENTS):
        pair_domain = range(-args.pair_limit, args.pair_limit + 1)
        for first, second in combinations(range(len(T7_LARGE_BASELINE_EXPONENTS)), 2):
            found = False
            for left in pair_domain:
                for right in pair_domain:
                    candidate = list(T7_LARGE_BASELINE_EXPONENTS)
                    candidate[first] = left
                    candidate[second] = right
                    weight = sum(abs(value) for value in candidate)
                    if weight >= best_weight:
                        continue
                    tested_pair += 1
                    changed = constraints_with_slot_exponents(T7_LARGE_BASE_CONSTRAINTS, candidate)
                    if not constraints_are_mds(7, l_matrix, changed):
                        continue
                    best_weight = weight
                    best_exponents = tuple(candidate)
                    print("BEST_WEIGHT", best_weight, "kind", "pair", "positions", (first, second), flush=True)
                    found = True
                    break
                if found:
                    break
            if found:
                break

    results = []
    for target_k in ([args.k] if args.k else [16, 32, 64]):
        if target_k == 16:
            target_rows = l_rows
            unit_cost = 2
        elif target_k in (32, 64):
            target_rows = companion_rows(target_k, (21, target_k))
            unit_cost = 1
        else:
            raise ValueError("t7 large exponent search reports k=16,32,64")
        target_constraints = constraints_with_slot_exponents(T7_LARGE_BASE_CONSTRAINTS, best_exponents)
        target_l = support_rows_to_masks(target_rows)
        if not constraints_are_mds(7, target_l, target_constraints):
            raise AssertionError(f"best t7 exponents do not lift to k={target_k}")
        result = Result(
            t=7,
            k=target_k,
            graph_index=1,
            previous_total=AUDIT_PREVIOUS_TOTALS[(7, target_k)],
            total_xor=21 * target_k + unit_cost * best_weight,
            base_xor=21 * target_k,
            coeffs=(
                T7_REWRITE_COEFFS
                if best_exponents == T7_LARGE_FINAL_EXPONENTS
                else constraints_to_coeffs(target_constraints)
            ),
            l_rows=target_rows,
            method=(
                f"searched k=16 exponent weight; weight={best_weight}, "
                f"single={tested_single}, pair={tested_pair}"
            ),
        )
        print("BEST_SEARCH", result.result_line(), "elapsed", time.time() - started, flush=True)
        results.append(result)
    return results


# ---------------------------------------------------------------------------
# Search bundles.
# ---------------------------------------------------------------------------


def copied_args(args: argparse.Namespace, **updates) -> argparse.Namespace:
    values = vars(args).copy()
    values.update(updates)
    return argparse.Namespace(**values)


def t6_k8_search(args: argparse.Namespace) -> list[Result]:
    """Run the full two-part t=6,k=8 search used for the recorded record."""

    print("== search: (6,8) fixed-L scalar reassignment ==", flush=True)
    scalar_results = t6_k8_search_scalar_slots(
        copied_args(
            args,
            scalar_budget=18,
            max_power_cost=None,
            paar_seeds=100,
            seed=0,
            stop_first=False,
        )
    )

    print("== search: (6,8) changed sparse L and direct w10 synthesis ==", flush=True)
    changed_l_results = t6_k8_search_changed_l(
        copied_args(
            args,
            search_trials=1_000_000,
            seed=7781,
            paar_seeds=20,
            stop_at=144,
        )
    )

    return changed_l_results or scalar_results


def run_record_searches(args: argparse.Namespace) -> list[Result]:
    """Run the search families that produced the recorded post-optimization rows."""

    results: list[Result] = []

    print("== record search: (5,4) graph/coefficient enumeration ==", flush=True)
    results.append(run_t5_k4_search(args.graph_root, max_cost=6, fixed_graph=args.fixed_graph))

    for k, budget in ((8, 6), (16, 4), (32, 4), (64, 4)):
        print(f"== restricted t=5 no-improvement check: (5,{k}) ==", flush=True)
        t5_search_no_better_lift(copied_args(args, k=k, t5_budget=budget, tap=None))

    results.extend(t6_k8_search(args))

    print("== record search: (6,16),(6,32),(6,64) sparse-L family ==", flush=True)
    results.extend(t6_large_search_sparse_l(copied_args(args, k=None, tap=None)))

    print("== record search: (7,8) local coefficient-map synthesis ==", flush=True)
    results.extend(
        t7_k8_search_local_slps(
            copied_args(args, paar_seeds=100, pair_cost_cap=4, seed=0)
        )
    )

    print("== record search: (7,16),(7,32),(7,64) exponent reassignment ==", flush=True)
    results.extend(
        t7_large_search_exponents(
            copied_args(args, k=None, single_limit=9, pair_limit=3)
        )
    )

    return results


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
    if args.all_records:
        return run_record_searches(args)

    if args.t == 5 and args.k == 4:
        return [run_t5_k4_search(graph_root, max_cost=6, fixed_graph=args.fixed_graph)]
    if args.t == 5 and args.k in (8, 16, 32, 64):
        return t5_search_no_better_lift(args)
    if args.t == 6 and args.k == 8:
        return t6_k8_search(args)
    if args.t == 6 and args.k in (16, 32, 64):
        return t6_large_search_sparse_l(args)
    if args.t == 7 and args.k == 8:
        return t7_k8_search_local_slps(
            copied_args(args, paar_seeds=100, pair_cost_cap=4, seed=0)
        )
    if args.t == 7 and args.k in (16, 32, 64):
        return t7_large_search_exponents(copied_args(args, single_limit=9, pair_limit=3))

    raise ValueError("supported cases are t=5,k=4/8/16/32/64; t=6,k=8/16/32/64; t=7,k=8/16/32/64")


def add_internal_defaults(args: argparse.Namespace) -> argparse.Namespace:
    """Attach fixed record-search parameters without exposing them as CLI knobs."""

    defaults = {
        "graph_root": Path(__file__).resolve().parents[1] / "graph_data",
        "seed": 0,
        "search_trials": 100_000,
        "scalar_budget": 18,
        "max_power_cost": None,
        "l_limit": 2,
        "inverse_limit": 2,
        "square_limit": 4,
        "joint_limit": 5,
        "paar_seeds": 20,
        "paar_window": 4,
        "report": 10_000,
        "stop_first": False,
        "stop_at": None,
        "allow_any_charpoly": False,
        "tap": None,
        "record_taps": True,
        "t5_budget": None,
        "single_limit": 9,
        "pair_limit": 3,
        "pair_cost_cap": 4,
    }
    for key, value in defaults.items():
        if not hasattr(args, key):
            setattr(args, key, value)
    return args


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run bit-level XOR post-optimization experiments."
    )
    parser.add_argument(
        "--all-records",
        action="store_true",
        help="run the post-optimization search bundle used for the recorded results",
    )
    parser.add_argument(
        "--t",
        type=int,
        default=None,
        help="target matrix size for a single post-optimization search",
    )
    parser.add_argument(
        "--fixed-graph",
        type=int,
        default=None,
        help="restrict the t=5,k=4 search to one graph index; omit for the full graph sweep",
    )
    parser.add_argument(
        "--k",
        type=int,
        default=None,
        help="target word size for a single post-optimization search",
    )
    args = parser.parse_args()
    if args.all_records:
        if args.t is not None or args.k is not None:
            parser.error("--all-records cannot be combined with --t or --k")
    elif args.t is None or args.k is None:
        parser.error("provide either --all-records or both --t and --k")
    args = add_internal_defaults(args)

    results = run_selected(args)
    assert_improvements(results)
    print("Experiment output")
    print("")
    print_table(results)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
