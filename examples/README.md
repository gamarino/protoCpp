# protoCpp examples

Each file is a single-translation-unit demo of one protoCore feature, kept short on purpose (30 to 98 lines). The top-level `CMakeLists.txt` builds every `examples/*.cpp` file as an executable named `example_<file stem>`:

```bash
cmake -B build_release -S . && cmake --build build_release
./build_release/example_01_hello
```

| File | Executable | What it shows |
|------|------------|---|
| `01_hello.cpp` | `example_01_hello` | Create a `ProtoSpace`, build a `ProtoList` of SmallInts with `appendLast`, and print it. The minimal program. |
| `02_smallint_fast_path.cpp` | `example_02_smallint_fast_path` | Adds two SmallInts twice: once through the public `ProtoObject::add`, and once with the inline helpers `isSmallInt` / `asSmallInt` / `smallIntInRange` / `makeSmallInt` from `protoCore.h`, which avoid a call into the shared library. |
| `03_atom_cas.cpp` | `example_03_atom_cas` | Lock-free compare-and-swap on a mutable object's attribute with `setAttributeIfEqual`, in a retry loop, as used for atom-style shared state. |
| `04_structural_sharing.cpp` | `example_04_structural_sharing` | `ProtoList::appendLast` returns a new list that shares most of the original's tree; the original list is unchanged. |
| `05_threads.cpp` | `example_05_threads` | `ProtoSpace::newThread` starts an OS thread managed by protoCore; the worker writes its result to a shared mutable attribute and the main thread joins it. No GIL. |
| `06_actor_manual.cpp` | `example_06_actor_manual` | A one-actor system built directly on the kernel: a `ProtoList` mailbox updated by compare-and-swap, a drainer thread, and a count of processed messages (1,000 messages). |
