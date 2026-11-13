// list_lab — design-space lab for protoCore's immutable list, without protoCore.
//
// Every variant is a persistent (immutable, path-copying) list built on a
// simulated cell heap: memory is handed out in 64-byte lines from an arena,
// zeroed like ProtoContext::allocCell, and the arena is reset between
// repetitions (the stand-in for the collector reclaiming the garbage).
// The point is to compare STRUCTURES under the same allocator, so the
// numbers rank the alternatives; the kernel's own cost per cell (11-13 ns
// steady state, measured by kernel_costmodel) adds on top of these.
//
// Variants
//   avl        : today's ProtoList — balanced binary tree, one value per 64 B node
//   hybrid     : leaves (and the tail) of 64 B, wider internal nodes
//   trie F/H   : radix trie with F children per node and an open tail
//                (Clojure's PersistentVector generalised to any F),
//                node header H bytes (16 = vtable + next_and_flags today,
//                8 = header without vtable), node size rounded up to 64 B lines
//
// Output: one line per (variant, N) with
//   append ns/op, bytes allocated per append, random getAt ns,
//   sequential getAt ns, live bytes per element, tree depth.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "lab_common.h"

namespace {
using namespace lab;

// ---------------------------------------------------------------- driver
Value val(uint64_t i) { return reinterpret_cast<Value>((i << 6) | 0x1); }  // tagged, never dereferenced

struct Result { double appendNs, bytesPerAppend, randNs, seqNs, liveBytesPerElem; unsigned depth; };

template <class BuildFn, class GetFn, class LiveFn>
Result run(Arena& a, uint64_t n, BuildFn build, GetFn get, LiveFn live, unsigned depth) {
    Result res{};
    const uint64_t reps = std::max<uint64_t>(1, 2000000 / n);
    double best = 1e300; size_t bytes = 0;
    for (int batch = 0; batch < 4; ++batch) {
        a.reset(); a.allocated = 0;
        double t0 = nowNs();
        for (uint64_t r = 0; r < reps; ++r) { a.reset(); g_sink = build(n); }
        double t = (nowNs() - t0) / double(reps * n);
        if (batch > 0) best = std::min(best, t);
        bytes = a.allocated / reps;
    }
    res.appendNs = best; res.bytesPerAppend = double(bytes) / n;
    a.reset();
    auto list = build(n);                       // a list to read, kept until the end
    for (uint64_t i = 0; i < n; ++i)
        if (get(list, i) != val(i)) { std::fprintf(stderr, "WRONG value at %llu\n", (unsigned long long)i); std::exit(1); }
    res.liveBytesPerElem = double(live(list)) / n;
    const uint64_t R = 1000000;
    double t0 = nowNs();
    for (uint64_t i = 0; i < R; ++i) g_sink = get(list, (i * 7919) % n);
    res.randNs = (nowNs() - t0) / R;
    t0 = nowNs();
    for (uint64_t i = 0; i < R; ++i) g_sink = get(list, i % n);
    res.seqNs = (nowNs() - t0) / R;
    res.depth = depth;
    return res;
}

void print(const char* name, uint64_t n, const Result& r) {
    std::printf("RES variant=%s N=%llu append_ns=%.1f bytes_per_append=%.0f cells_per_append=%.2f "
                "get_random_ns=%.1f get_seq_ns=%.1f live_bytes_per_elem=%.1f depth=%u\n",
                name, (unsigned long long)n, r.appendNs, r.bytesPerAppend, r.bytesPerAppend / 64,
                r.randNs, r.seqNs, r.liveBytesPerElem, r.depth);
    std::fflush(stdout);
}

template <unsigned FL, unsigned FI, unsigned H>
void runTrie(const char* name, Arena& arena, uint64_t n) {
    using T = Trie<FL, FI, H>;
    T t(arena);
    unsigned d = 0;
    Result rt = run(arena, n,
        [&](uint64_t m) { const typename T::Root* l = t.empty(); for (uint64_t i = 0; i < m; ++i) l = t.append(l, val(i)); return l; },
        [&](const typename T::Root* l, uint64_t i) { return t.get(l, i); },
        [&](const typename T::Root* l) { d = l->levels + 1; return t.liveBytes(l); }, 0);
    rt.depth = d;
    print(name, n, rt);
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<uint64_t> sizes = {1000, 10000, 100000, 1000000};
    if (argc > 1) { sizes.clear(); for (int i = 1; i < argc; ++i) sizes.push_back(std::strtoull(argv[i], nullptr, 10)); }
    Arena arena(size_t(3) << 30);               // 3 GiB of address space, faulted once

    for (uint64_t n : sizes) {
        AvlList avl(arena);
        unsigned depth = 0;
        Result r = run(arena, n,
            [&](uint64_t m) { const AvlNode* l = nullptr; for (uint64_t i = 0; i < m; ++i) l = avl.append(l, val(i)); return l; },
            [&](const AvlNode* l, uint64_t i) { return AvlList::get(l, uint32_t(i)); },
            [&](const AvlNode* l) { depth = l->height; return AvlList::liveBytes(l); }, 0);
        r.depth = depth;
        print("avl_1per64B", n, r);

        runTrie<6, 6, 16>("trie_F6_H16_64B", arena, n);
        runTrie<7, 7, 8>("trie_F7_H8_64B", arena, n);
        runTrie<14, 14, 16>("trie_F14_H16_128B", arena, n);
        runTrie<30, 30, 16>("trie_F30_H16_256B", arena, n);
        runTrie<6, 14, 16>("hybrid_L6x64B_I14x128B", arena, n);
        runTrie<6, 30, 16>("hybrid_L6x64B_I30x256B", arena, n);
        runTrie<7, 15, 8>("hybrid_L7x64B_I15x128B_H8", arena, n);
    }
    return 0;
}