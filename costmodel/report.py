"""Turns the three RES outputs of costmodel/run.sh into one markdown report."""
import os, platform, sys

def parse(path):
    rows = []
    for line in open(path):
        if line.startswith("RES "):
            rows.append(dict(kv.split("=", 1) for kv in line.split()[1:]))
    return rows

out, pyver = sys.argv[1], sys.argv[2]
kernel = parse(os.path.join(out, "kernel.txt"))
cpy = {r["op"]: r for r in parse(os.path.join(out, "cpython.txt"))}
lab = parse(os.path.join(out, "lab.txt"))
slab = parse(os.path.join(out, "string_lab.txt"))

# kernel op -> CPython op of the same work
PAIRS = {
    "int_add_(generic_add,_SmallInt)": "int_add",
    "double_add_(generic_add)": "double_add",
    "fib(20):_per_call_(compare+2_sub+add)": "fib(20):_per_call",
    "getAttribute,_mutable_obj,_3_attrs": "getattr_x3_(per_read)",
    "setAttribute_mutable_(4_attrs,_few_mutables)": "setattr",
    "new_object_+_4_attrs_(instance_creation)": "new_object_+_4_attrs",
    "list_appendLast,_build_to_1000": "list_append,_build_to_1000",
    "list_appendLast,_build_to_10000": "list_append,_build_to_10000",
    "list_appendLast,_build_to_100000": "list_append,_build_to_100000",
    "list_getAt_random,_100k_list": "list_index_random_100k_(incl._mul/mod)",
    "list_getAt_sequential,_100k_list": "list_index_sequential_100k",
    "string_+=_1_char,_build_to_2000": "str_+=_'x',_build_to_2000",
    "string_+=_1_char,_build_to_10000": "str_+=_'x',_build_to_10000",
}

print(f"# protoCore cost model\n\nMachine: {platform.processor() or platform.machine()}, {os.cpu_count()} CPUs. {pyver}.\n")
print("## Kernel against CPython (ns per operation, in-process, steady state)\n")
print("| Operation | Kernel ns | Kernel cells/op | CPython ns | Kernel / CPython |")
print("|---|---:|---:|---:|---:|")
for r in kernel:
    op = r["op"]; k = float(r["ns"])
    c = cpy.get(PAIRS.get(op, ""))
    cn = f'{float(c["ns"]):.1f}' if c else "–"
    ratio = f'{k / float(c["ns"]):.2f}x' if c else "–"
    print(f'| {op.replace("_", " ")} | {k:.1f} | {r["cells"]} | {cn} | {ratio} |')

print("\n## Immutable list designs (structure lab, simulated 64 B cell heap)\n")
print("| Variant | N | append ns | cells/append | getAt random ns | getAt seq ns | live B/elem | depth |")
print("|---|---:|---:|---:|---:|---:|---:|---:|")
for r in lab:
    print(f'| {r["variant"]} | {r["N"]} | {r["append_ns"]} | {r["cells_per_append"]} | {r["get_random_ns"]} | {r["get_seq_ns"]} | {r["live_bytes_per_elem"]} | {r["depth"]} |')

print("\n## Immutable string designs (structure lab, same leaf as today: 36 B of UTF-8 per 64 B cell)\n")
print("| Variant | N chars | append 1 char ns | cells/append | charAt random ns | live B/char | concat two halves ns | concat cells |")
print("|---|---:|---:|---:|---:|---:|---:|---:|")
for r in slab:
    print(f'| {r["variant"]} | {r["N"]} | {r["append_ns"]} | {r["cells_per_append"]} | {r["charAt_random_ns"]} | {r["live_bytes_per_char"]} | {r["concat_halves_ns"]} | {r["concat_halves_cells"]} |')