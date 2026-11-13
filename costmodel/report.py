"""Turns a costmodel/run.sh run directory into one markdown report.

Every program ran once per round; within a program each figure is already the
median of its batches. Here each figure is the median across rounds, and the
spread is the lowest and highest per-round value (wall clock, ns).
"""
import os, re, statistics, sys

out = sys.argv[1]


def parse(path):
    rows = {}
    for line in open(path):
        if line.startswith("RES "):
            d = dict(kv.split("=", 1) for kv in line.split()[1:])
            key = d.get("op") or d.get("variant")
            if "N" in d:
                key = f'{key}@{d["N"]}'
            rows[key] = d
    return rows


def rounds(name):
    files = sorted(f for f in os.listdir(out) if re.fullmatch(rf"{name}\.\d+\.txt", f))
    return [parse(os.path.join(out, f)) for f in files]


def agg(runs):
    """key -> field -> (median, low, high) over rounds, for numeric fields."""
    res = {}
    for key in runs[0]:
        fields = {}
        for f, v in runs[0][key].items():
            try:
                vals = [float(r[key][f]) for r in runs if key in r]
            except ValueError:
                fields[f] = (runs[0][key][f], None, None)
                continue
            fields[f] = (statistics.median(vals), min(vals), max(vals))
        res[key] = fields
    return res


def val(d, key, f):
    x = d.get(key, {}).get(f)
    return None if x is None else x[0]


def fmt(x, nd=1):
    if x is None:
        return "–"
    if isinstance(x, str):
        return x
    return f"{x:,.{nd}f}".replace(",", " ")


def spread(d, key, f="ns"):
    x = d.get(key, {}).get(f)
    if x is None or x[1] is None:
        return "–"
    return f"{fmt(x[1])}–{fmt(x[2])}"


def ratio(a, b):
    return "–" if a is None or b is None or b == 0 else f"{a / b:.2f}x"


env = dict(l.rstrip("\n").split("=", 1) for l in open(os.path.join(out, "env.txt")))
loads = [dict(kv.split("=", 1) for kv in l.split()[1:]) for l in open(os.path.join(out, "load.txt"))]
max_load = float(env["max_load"])
loaded = [l for l in loads if float(l["before"]) > max_load or float(l["after"]) > max_load + 1.0]

kernel, capi, interp = agg(rounds("kernel")), agg(rounds("capi")), agg(rounds("interp"))
list_ops, list_ops_lab = agg(rounds("list_ops")), agg(rounds("list_ops_lab"))
lab, slab = agg(rounds("lab")), agg(rounds("string_lab"))

print("# protoCore cost model\n")
print(f'- Date: {env["date"]}; rounds: {env["rounds"]}; heap: {env["heap"]}')
print(f'- CPU: {env["cpu"]}, {env["cpus"]} logical CPUs; governor {env["governor"]}, EPP {env["epp"]}, boost {env["boost"]}; Linux {env["host_kernel"]}')
print(f'- protoCore: `{env["protocore_lib"]}` (sha256 {env["protocore_lib_sha256"][:12]}), a fresh copy per round')
print(f'- CPython: {env["python"]} (GIL enabled: {env["python_gil"]})')
print(f'- Compilers: {env["cxx"]}; {env["cc"]}')
lb = [float(l["before"]) for l in loads]
print(f'- Load average (1 min) before each program: {min(lb):.2f}–{max(lb):.2f} (gate {max_load})')
if loaded:
    print(f'- **WARNING: {len(loaded)} of {len(loads)} program runs started or ended above the load gate; treat the figures as noisy.**')
print()

ROWS = [
    ("int_add", "Integer add (SmallInt / small int)"),
    ("double_add", "Double add"),
    ("fib20_step", "fib(20) step: compare + 2 sub + add (kernel and C API: a C/C++ recursion, no call machinery; interpreter: a real Python call)"),
    ("getattr", "Read attribute (3 attributes)"),
    ("setattr_mutable", "Write attribute, mutable object"),
    ("new_object_4_attrs", "New object + 4 attributes"),
    ("mutable_write_16_attrs_1_live", "Mutable write, 16 attrs, 1 mutable live"),
    ("mutable_write_16_attrs_1000_live", "Mutable write, 16 attrs, 1 k mutables live"),
    ("mutable_write_16_attrs_100000_live", "Mutable write, 16 attrs, 100 k mutables live"),
    ("list_append_to_1000", "List append, building to 1 k"),
    ("list_append_to_10000", "List append, building to 10 k"),
    ("list_append_to_100000", "List append, building to 100 k"),
    ("list_getat_random_100k", "List index, random, 100 k"),
    ("list_getat_seq_100k", "List index, sequential, 100 k"),
    ("str_append_to_2000", "String append 1 char, building to 2 k"),
    ("str_append_to_10000", "String append 1 char, building to 10 k"),
]

print("## Kernel against CPython (ns per operation, in-process)\n")
print("Three ways to run the same operation: protoCore's C++ API from C++ (kernel), CPython's C API from C (the fair twin: "
      "a library primitive against a library primitive) and CPython interpreted code. `wall` is the median wall-clock time; "
      "`CPU` is process CPU time over all threads, which also counts protoCore's concurrent collector thread. "
      "The fair ratio is kernel CPU / C API. Spread: lowest–highest per-round median.\n")
print("| Operation | Kernel wall | spread | Kernel CPU | cells/op | C API | spread | Interp. | Interp. net | Kernel CPU / C API | Kernel wall / C API | Kernel wall / Interp. |")
print("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
for key, label in ROWS:
    kw, kc, cells = val(kernel, key, "ns"), val(kernel, key, "cpu"), val(kernel, key, "cells")
    cw, iw, inet = val(capi, key, "ns"), val(interp, key, "ns"), val(interp, key, "net_ns")
    print(f"| {label} | {fmt(kw)} | {spread(kernel, key)} | {fmt(kc)} | {fmt(cells, 2)} | {fmt(cw)} | {spread(capi, key)} | "
          f"{fmt(iw)} | {fmt(inet)} | {ratio(kc, cw)} | {ratio(kw, cw)} | {ratio(kw, iw)} |")
base = val(interp, "empty_loop_iteration", "ns")
print(f"\nInterpreter empty loop iteration: {fmt(base)} ns (`Interp. net` subtracts it where the loop runs once per operation).\n")

print("## Every `ProtoList` operation in the kernel (AVL)\n")
print("| Operation | N | wall ns | spread | CPU ns | cells |")
print("|---|---:|---:|---:|---:|---:|")
for key, f in list_ops.items():
    op, n = key.split("@")
    per = " (per element)" if "per" in f else ""
    print(f'| {op}{per} | {n} | {fmt(f["ns"][0])} | {spread(list_ops, key)} | {fmt(f["cpu"][0])} | {fmt(f["cells"][0], 2)} |')

print("\n## Candidate trie, every operation (lab, simulated 64 B cell heap, no collector)\n")
print("| Operation | N | ns | spread | cells |")
print("|---|---:|---:|---:|---:|")
for key, f in list_ops_lab.items():
    op, n = key.split("@")
    per = " (per element)" if "per" in f else ""
    print(f'| {op}{per} | {n} | {fmt(f["ns"][0])} | {spread(list_ops_lab, key)} | {fmt(f["cells"][0], 2)} |')

print("\n## Immutable list designs (structure lab, simulated 64 B cell heap)\n")
print("| Variant | N | append ns | spread | cells/append | getAt random ns | spread | getAt seq ns | live B/elem | depth |")
print("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
for key, f in lab.items():
    v, n = key.split("@")
    print(f'| {v} | {n} | {fmt(f["append_ns"][0])} | {spread(lab, key, "append_ns")} | {fmt(f["cells_per_append"][0], 2)} | '
          f'{fmt(f["get_random_ns"][0])} | {spread(lab, key, "get_random_ns")} | {fmt(f["get_seq_ns"][0])} | '
          f'{fmt(f["live_bytes_per_elem"][0])} | {fmt(f["depth"][0], 0)} |')

print("\n## Immutable string designs (structure lab, today's leaf: 36 B of UTF-8 per 64 B cell)\n")
print("| Variant | N chars | append 1 char ns | spread | cells/append | charAt random ns | live B/char | concat two halves ns | concat cells |")
print("|---|---:|---:|---:|---:|---:|---:|---:|---:|")
for key, f in slab.items():
    v, n = key.split("@")
    print(f'| {v} | {n} | {fmt(f["append_ns"][0])} | {spread(slab, key, "append_ns")} | {fmt(f["cells_per_append"][0], 2)} | '
          f'{fmt(f["charAt_random_ns"][0])} | {fmt(f["live_bytes_per_char"][0], 2)} | {fmt(f["concat_halves_ns"][0], 0)} | '
          f'{fmt(f["concat_halves_cells"][0], 0)} |')
