# milp_all_appendix_A2_exponents.sage
#
# MILP optimizer for the exponents appearing in Appendix A.2.
#
# The computation graph and the matrix L are fixed. For every node v, introduce
# an integer shift r_v and replace each edge coefficient L^e on u -> v by
#
#     L^{e'}, where e' = e + r_v - r_u.
#
# This is exactly the change induced by node-wise scaling \tilde v = L^{r_v}v.
# Therefore, if the original construction is MDS, every optimized construction
# produced by this script is also MDS, up to invertible diagonal block scalings
# on the input and output blocks. No MDS check is encoded in the MILP.
#
# Usage:
#   sage milp_all_appendix_A2_exponents.sage
#   sage milp_all_appendix_A2_exponents.sage 7 8
#   sage milp_all_appendix_A2_exponents.sage 7 8 --max-abs 4
#   sage milp_all_appendix_A2_exponents.sage --max-abs 3 --no-constraints
#   sage milp_all_appendix_A2_exponents.sage 7 8 --fix-io

from sage.all import *
import argparse
import sys

F2 = GF(2)


def term(name, exp=0):
    return (name, int(exp))


def build_L(k, row_supports):
    """Build a k x k binary matrix from 1-based row supports."""
    L = matrix(F2, k, k)
    for i, cols in enumerate(row_supports):
        for c in cols:
            L[i, c - 1] = 1
    return L


def shift_rows(k, first_row):
    """[[a,b],[1],...,[k-1]] in Appendix A.2 notation."""
    return [first_row] + [[i] for i in range(1, k)]


def special_8_rows():
    """[[8],[1,2],[2,8],[3],...,[7]]."""
    return [[8], [1, 2], [2, 8]] + [[i] for i in range(3, 8)]


def rows_t7_k16():
    """[[13,16],[1],...,[10],[9,11],[12],...,[15]]."""
    return [[13, 16]] + [[i] for i in range(1, 11)] + [[9, 11]] + [[i] for i in range(12, 16)]


def xor_cost_matrix(A):
    """Row-wise XOR cost of a binary linear map."""
    cost = 0
    for i in range(A.nrows()):
        weight = sum(Integer(A[i, j]) for j in range(A.ncols()))
        cost += max(weight - 1, 0)
    return int(cost)


# -----------------------------------------------------------------------------
# Appendix A.2 constraints.
# Each item (dst, [(src,e1), (src,e2)]) encodes
#     dst = L^{e1} src1 \oplus L^{e2} src2.
# Exponent 0 means the identity coefficient.
# -----------------------------------------------------------------------------

C5_4_8 = [
    ("w1",  [term("x2"),      term("x5")]),
    ("w2",  [term("x4"),      term("w1", 1)]),
    ("w3",  [term("x3", -1),  term("w2")]),
    ("w4",  [term("x1"),      term("w3", -1)]),
    ("w5",  [term("x3"),      term("w4", -1)]),
    ("y4",  [term("w1"),      term("w4", 1)]),
    ("w6",  [term("x5"),      term("w5", -1)]),
    ("y1",  [term("x4", -1),  term("w6")]),
    ("y5",  [term("w3"),      term("w6", -1)]),
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
    ("w1",  [term("x2"),      term("x6", 1)]),
    ("w2",  [term("x5", 1),   term("w1")]),
    ("w3",  [term("x1"),      term("w2")]),
    ("w4",  [term("x4"),      term("w2")]),
    ("w5",  [term("x3"),      term("w4")]),
    ("w6",  [term("w3", 1),   term("w5")]),
    ("w7",  [term("x5", -1),  term("w6", 1)]),
    ("w8",  [term("x6"),      term("w7")]),
    ("y1",  [term("w4"),      term("w7")]),
    ("w9",  [term("x3"),      term("w8", 1)]),
    ("y2",  [term("x4"),      term("w9")]),
    ("y5",  [term("w3"),      term("w9", 2)]),
    ("w10", [term("w1", -2),  term("y5")]),
    ("y3",  [term("w8"),      term("w10")]),
    ("y6",  [term("w5", 2),   term("w10")]),
    ("y4",  [term("w6"),      term("y6")]),
]

# Current t=7,k=8 construction in the uploaded linlayer.tex.
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
    ("y5",  [term("w11", -2), term("w14")]),
    ("y6",  [term("w14"),     term("w8")]),
    ("y7",  [term("y5"),      term("w9", -1)]),
]

CASES = {
    (5, 4):  (build_L(4,  [[4], [1, 4], [2], [3]]),                         C5_4_8),
    (5, 8):  (build_L(8,  shift_rows(8, [6, 8])),                            C5_4_8),
    (5, 16): (build_L(16, shift_rows(16, [1, 16])),                          C5_16_32_64),
    (5, 32): (build_L(32, shift_rows(32, [11, 32])),                         C5_16_32_64),
    (5, 64): (build_L(64, shift_rows(64, [21, 64])),                         C5_16_32_64),
    (6, 8):  (build_L(8,  special_8_rows()),                                 C6_8_32_64),
    (6, 16): (build_L(16, shift_rows(16, [15, 16])),                         C6_16),
    (6, 32): (build_L(32, shift_rows(32, [21, 32])),                         C6_8_32_64),
    (6, 64): (build_L(64, shift_rows(64, [21, 64])),                         C6_8_32_64),
    (7, 8):  (build_L(8,  special_8_rows()),                                 C7_8),
    (7, 16): (build_L(16, rows_t7_k16()),                                    C7_16_32_64),
    (7, 32): (build_L(32, shift_rows(32, [21, 32])),                         C7_16_32_64),
    (7, 64): (build_L(64, shift_rows(64, [21, 64])),                         C7_16_32_64),
}


# -----------------------------------------------------------------------------
# MILP model
# -----------------------------------------------------------------------------

def node_sort_key(v):
    prefix = v[0]
    number = int(v[1:])
    rank = {"x": 0, "w": 1, "y": 2}.get(prefix, 3)
    return (rank, number)


def all_nodes(constraints):
    nodes = set()
    for dst, terms in constraints:
        nodes.add(dst)
        for src, _ in terms:
            nodes.add(src)
    return sorted(nodes, key=node_sort_key)


def flatten_edges(constraints):
    edges = []
    for dst, terms in constraints:
        for src, e in terms:
            edges.append((src, dst, int(e)))
    return edges


def connected_components(nodes, edges):
    adj = {v: set() for v in nodes}
    for src, dst, _ in edges:
        adj[src].add(dst)
        adj[dst].add(src)
    seen = set()
    comps = []
    for v in nodes:
        if v in seen:
            continue
        stack = [v]
        seen.add(v)
        comp = []
        while stack:
            u = stack.pop()
            comp.append(u)
            for z in adj[u]:
                if z not in seen:
                    seen.add(z)
                    stack.append(z)
        comps.append(sorted(comp, key=node_sort_key))
    return comps


def original_weight(constraints):
    return sum(abs(int(e)) for _, terms in constraints for _, e in terms)


def optimize_exponents(constraints, max_abs=None, fix_io=False, shift_bound=None, solver=None):
    nodes = all_nodes(constraints)
    edges = flatten_edges(constraints)

    p = MixedIntegerLinearProgram(maximization=False, solver=solver)
    r = p.new_variable(integer=True, nonnegative=False, name="r")
    a = p.new_variable(integer=True, nonnegative=True, name="abs_e")

    # Gauge fixing. Adding a constant to every node shift in a connected
    # component does not change any exponent.
    for comp in connected_components(nodes, edges):
        p.add_constraint(r[comp[0]] == 0)

    if shift_bound is not None:
        for v in nodes:
            p.add_constraint(r[v] <= shift_bound)
            p.add_constraint(r[v] >= -shift_bound)

    if fix_io:
        for v in nodes:
            if v.startswith("x") or v.startswith("y"):
                p.add_constraint(r[v] == 0)

    for j, (src, dst, e) in enumerate(edges):
        new_e = Integer(e) + r[dst] - r[src]
        p.add_constraint(a[j] >= new_e)
        p.add_constraint(a[j] >= -new_e)
        if max_abs is not None:
            p.add_constraint(new_e <= int(max_abs))
            p.add_constraint(new_e >= -int(max_abs))

    p.set_objective(sum(a[j] for j in range(len(edges))))

    opt = int(round(p.solve()))
    r_values_raw = p.get_values(r)
    shifts = {v: int(round(r_values_raw[v])) for v in nodes}

    new_constraints = []
    new_weight = 0
    changed = []
    for dst, terms in constraints:
        new_terms = []
        line_changed = False
        for src, e in terms:
            ep = int(e) + shifts[dst] - shifts[src]
            new_terms.append((src, ep))
            new_weight += abs(ep)
            line_changed = line_changed or (ep != int(e))
        new_constraints.append((dst, new_terms))
        changed.append(line_changed)

    if new_weight != opt:
        raise RuntimeError("internal error: objective and reconstructed weight disagree")

    return opt, shifts, new_constraints, changed


# -----------------------------------------------------------------------------
# Pretty printing
# -----------------------------------------------------------------------------

def var_tex(v):
    return "%s_{%s}" % (v[0], v[1:])


def term_tex(src, e):
    src_tex = var_tex(src)
    if e == 0:
        return src_tex
    if e == 1:
        return "L%s" % src_tex
    if e == -1:
        return "L^{-1}%s" % src_tex
    return "L^{%d}%s" % (e, src_tex)


def constraint_tex(dst, terms):
    rhs = " \\oplus ".join(term_tex(src, e) for src, e in terms)
    return "%s = %s" % (var_tex(dst), rhs)


def print_constraints_latex(constraints, changed=None, only_changed=False, per_line=2):
    parts = []
    for idx, (dst, terms) in enumerate(constraints):
        if only_changed and changed is not None and not changed[idx]:
            continue
        parts.append(constraint_tex(dst, terms))

    print("\\[")
    print("\\text{Constraints} =")
    print("\\begin{cases}")
    for i in range(0, len(parts), per_line):
        print("  " + ", \\quad ".join(parts[i:i + per_line]) + r" \\")
    print("\\end{cases}.")
    print("\\]")


def print_shift_summary(shifts):
    nonzero = [(v, shifts[v]) for v in sorted(shifts, key=node_sort_key) if shifts[v] != 0]
    if not nonzero:
        print("node shifts: all zero")
        return
    print("node shifts r_v:")
    for v, rv in nonzero:
        print("  {:>4s}: {:+d}".format(v, rv))


def solve_case(t, k, args):
    L, constraints = CASES[(t, k)]
    xor_L = xor_cost_matrix(L)
    old_weight = original_weight(constraints)
    old_total = len(constraints) * k + old_weight * xor_L

    opt, shifts, new_constraints, changed = optimize_exponents(
        constraints,
        max_abs=args.max_abs,
        fix_io=args.fix_io,
        shift_bound=args.shift_bound,
        solver=args.solver,
    )
    new_total = len(constraints) * k + opt * xor_L

    print("=" * 78)
    print("case (t,k)=({}, {})".format(t, k))
    print("#constraints = {}".format(len(constraints)))
    print("#XOR(L) = {}".format(xor_L))
    print("original sum |e| = {}".format(old_weight))
    print("optimized sum |e| = {}".format(opt))
    print("original total XOR cost = {} * {} + {} * {} = {}".format(
        len(constraints), k, old_weight, xor_L, old_total
    ))
    print("optimized total XOR cost = {} * {} + {} * {} = {}".format(
        len(constraints), k, opt, xor_L, new_total
    ))
    if args.max_abs is not None:
        print("constraint: |e'| <= {}".format(args.max_abs))
    if args.fix_io:
        print("constraint: r_xi = r_yi = 0")
    print_shift_summary(shifts)

    if not args.no_constraints:
        print()
        print_constraints_latex(
            new_constraints,
            changed=changed,
            only_changed=args.only_changed,
            per_line=args.per_line,
        )

    return opt, new_total


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Optimize Appendix A.2 exponents by node-wise scaling MILP."
    )
    parser.add_argument(
        "case", nargs="*", type=int,
        help="optional pair t k; omitted means all cases"
    )
    parser.add_argument(
        "--max-abs", type=int, default=None,
        help="optional bound |e'| <= MAX_ABS"
    )
    parser.add_argument(
        "--fix-io", action="store_true",
        help="force all input/output shifts to zero; only internal nodes are rescaled"
    )
    parser.add_argument(
        "--shift-bound", type=int, default=None,
        help="optional bound |r_v| <= SHIFT_BOUND"
    )
    parser.add_argument(
        "--solver", type=str, default=None,
        help="optional Sage MILP backend, e.g. GLPK, COIN, Gurobi"
    )
    parser.add_argument(
        "--no-constraints", action="store_true",
        help="print only objective values and shifts, not optimized constraints"
    )
    parser.add_argument(
        "--only-changed", action="store_true",
        help="when printing constraints, print only lines whose exponents changed"
    )
    parser.add_argument(
        "--per-line", type=int, default=2,
        help="number of constraints per LaTeX output line"
    )
    args = parser.parse_args(argv)

    if len(args.case) == 0:
        args.keys = sorted(CASES.keys())
    elif len(args.case) == 2:
        key = (args.case[0], args.case[1])
        if key not in CASES:
            raise ValueError("unknown case {}; known cases are {}".format(key, sorted(CASES.keys())))
        args.keys = [key]
    else:
        raise ValueError("case must be omitted or given as exactly two integers: t k")

    return args


def main(argv):
    args = parse_args(argv)
    summary = []
    for t, k in args.keys:
        opt_weight, opt_xor = solve_case(t, k, args)
        summary.append((t, k, opt_weight, opt_xor))

    if len(summary) > 1:
        print("=" * 78)
        print("summary")
        print("t,k,optimized_sum_abs_e,optimized_total_xor")
        for row in summary:
            print("{},{},{},{}".format(*row))


if __name__ == "__main__":
    main(sys.argv[1:])
