# costmodel — what each protoCore primitive costs, and which structure to build

protoCpp's `benchmarks/` time whole processes with small N, so start-up hides
the work. `costmodel/` measures the work alone:

| File | What it measures |
|---|---|
| `kernel_costmodel.cpp` | protoCore primitives through the public API: ns and cells per operation, each repetition in a child `ProtoContext`, adaptive heap (steady state), best of 5 batches |
| `cpython_costmodel.py` | the same operations in CPython, timed in-process |
| `list_lab.cpp` | immutable list designs on a simulated 64 B cell heap, without protoCore: today's AVL (calibrates against the kernel) and radix tries with an open tail, F children per node, H header bytes |
| `string_lab.cpp` | immutable string designs with today's leaf (36 B of UTF-8 per cell): today's binary rope, the rope with an open last leaf, and leaves under a radix trie |
| `lab_common.h` | the simulated cell heap and the list designs shared by the list labs |
| `list_ops_kernel.cpp` | every `ProtoList` operation in today's kernel (1 k and 100 k elements) |
| `list_ops_lab.cpp` | every operation on the candidate trie (native, by bulk rebuild, by conversion to the AVL) and the chunked-AVL option |
| `run.sh` + `report.py` | runs all three and prints one markdown report |

```bash
cmake -B build_release -S . && cmake --build build_release --target costmodel_kernel costmodel_list_lab costmodel_string_lab
costmodel/run.sh build_release python3 > report.md
```

Reading the lab: its AVL row reproduces the kernel's cells per append exactly
(11.94 / 15.36 / 18.69), so the cells column transfers to the kernel. Its
times do not include protoCore's own cost per cell (allocCell, constructor,
collection); estimate a kernel time as `lab ns + cells/op x (kernel ns - lab ns)
/ kernel cells` taken from the AVL rows.

Add a variant to the lab before changing the kernel; change the kernel only
for the variant the lab and the kernel model agree on.

The string lab's `rope_avl` is an approximation of today's rope (a balanced
tree over the leaves): it creates fewer cells per append than the kernel
(6-15 against 12-14), so compare the variants with each other, and the kernel
with `kernel_costmodel`.