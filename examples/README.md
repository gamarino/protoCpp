# protoCpp examples

Each file is a single-translation-unit demo of one protoCore feature, kept short on purpose. Build everything with the top-level `CMakeLists.txt`; binaries appear as `build_release/example_<NN>_<name>`.

| File | What it shows |
|------|---|
| `01_hello.cpp` | Create a `ProtoSpace`, build a `ProtoList`, read it back. The absolute minimum. |
| `02_smallint_fast_path.cpp` | Tagged-pointer SmallInt arithmetic without a single call into protoCore — the inline helpers `isSmallInt` / `asSmallInt` / `makeSmallInt`. |
| `03_atom_cas.cpp` | Lock-free CAS on a mutable attribute via `setAttributeIfEqual` — the kernel primitive every runtime's atoms / futures / actor mailboxes are built on. |
| `04_structural_sharing.cpp` | `ProtoList::appendLast` returns a new list that shares most of the old one's storage. Persistent data structures at kernel level. |
| `05_threads.cpp` | `ProtoSpace::newThread` spawns a real OS thread that participates in the concurrent GC. No GIL. |
| `06_actor_manual.cpp` | Build a tiny one-actor system from scratch on the kernel: mailbox via CAS, drainer thread, message counter. The shape every higher-level runtime starts from. |
