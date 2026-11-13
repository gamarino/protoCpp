// list_ops_kernel — the cost of every ProtoList operation in today's kernel.
//
// Each measurement runs in child ProtoContexts (steady state, run with
// PROTOCORE_ADAPTIVE_HEAP=1), best of 5 batches. For every list size N a
// list of 0..N-1 is built once in the root context and every operation is
// applied to it (immutable: the base list never changes).
// Output: RES op=<op> N=<n> ns=<ns per call> cells=<cells per call> [per=element]

#include "protoCore.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <vector>

using namespace proto;
static const void* volatile g_sink;
static ProtoSpace* S;

static double nowNs() {
    return std::chrono::duration<double, std::nano>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// `calls` calls per repetition; `per` divides the time again (elements per call)
static void measure(const char* op, long n, long reps, long calls, double per,
                    const std::function<void(ProtoContext*)>& body) {
    double best = 1e300, cells = 0;
    for (int batch = 0; batch < 6; ++batch) {
        double t0 = nowNs(); proto_ulong c = 0;
        for (long r = 0; r < reps; ++r) {
            ProtoContext ctx(S, S->rootContext);
            body(&ctx);
            c += ctx.allocatedCellsCount;
        }
        double t = (nowNs() - t0) / double(reps * calls);
        if (batch > 0) best = std::min(best, t);
        cells = double(c) / double(reps * calls);
    }
    if (per > 1) std::printf("RES op=%s N=%ld ns=%.2f cells=%.3f per=element\n", op, n, best / per, cells / per);
    else std::printf("RES op=%s N=%ld ns=%.1f cells=%.2f\n", op, n, best, cells);
    std::fflush(stdout);
}

int main() {
    ProtoSpace space; S = &space;
    ProtoContext* root = space.rootContext;
    const ProtoObject* missing = root->fromLong(-1);

    for (long n : {1000L, 100000L}) {
        const ProtoList* base = root->newList();
        for (long i = 0; i < n; ++i) base = base->appendLast(root, root->fromLong(i));
        const ProtoList* half = root->newList();
        for (long i = 0; i < n / 2; ++i) half = half->appendLast(root, root->fromLong(i));
        const long K = 20000;                       // calls per repetition for O(log n) operations
        auto idx = [n](long i) { return int((i * 7919) % n); };

        measure("getSize", n, 20, K, 1, [&](ProtoContext* c) { for (long i = 0; i < K; ++i) g_sink = (const void*)(uintptr_t)base->getSize(c); });
        measure("getLast", n, 20, K, 1, [&](ProtoContext* c) { for (long i = 0; i < K; ++i) g_sink = base->getLast(c); });
        measure("getAt_random", n, 20, K, 1, [&](ProtoContext* c) { for (long i = 0; i < K; ++i) g_sink = base->getAt(c, idx(i)); });
        measure("setAt_random", n, 20, K, 1, [&](ProtoContext* c) { for (long i = 0; i < K; ++i) g_sink = base->setAt(c, idx(i), missing); });
        measure("appendLast_on_N", n, 20, K, 1, [&](ProtoContext* c) { for (long i = 0; i < K; ++i) g_sink = base->appendLast(c, missing); });
        measure("appendFirst", n, 20, K, 1, [&](ProtoContext* c) { for (long i = 0; i < K; ++i) g_sink = base->appendFirst(c, missing); });
        measure("insertAt_middle", n, 20, K, 1, [&](ProtoContext* c) { for (long i = 0; i < K; ++i) g_sink = base->insertAt(c, int(n / 2), missing); });
        measure("removeAt_middle", n, 20, K, 1, [&](ProtoContext* c) { for (long i = 0; i < K; ++i) g_sink = base->removeAt(c, int(n / 2)); });
        measure("removeFirst", n, 20, K, 1, [&](ProtoContext* c) { for (long i = 0; i < K; ++i) g_sink = base->removeFirst(c); });
        measure("removeLast", n, 20, K, 1, [&](ProtoContext* c) { for (long i = 0; i < K; ++i) g_sink = base->removeLast(c); });
        const long KB = std::max(1L, 2000000 / n);  // calls for O(n)-ish operations
        measure("extend_two_halves", n, 5, KB, 1, [&](ProtoContext* c) { for (long i = 0; i < KB; ++i) g_sink = half->extend(c, half); });
        measure("getSlice_middle_half", n, 5, KB, 1, [&](ProtoContext* c) { for (long i = 0; i < KB; ++i) g_sink = base->getSlice(c, int(n / 4), int(3 * n / 4)); });
        measure("splitFirst_half", n, 5, KB, 1, [&](ProtoContext* c) { for (long i = 0; i < KB; ++i) g_sink = base->splitFirst(c, int(n / 2)); });
        measure("iterate_all", n, 5, KB, double(n), [&](ProtoContext* c) {
            for (long i = 0; i < KB; ++i) for (auto* it = base->getIterator(c); it->hasNext(c); it = it->advance(c)) g_sink = it->next(c); });
        measure("getAt_sequential_all", n, 5, KB, double(n), [&](ProtoContext* c) {
            for (long i = 0; i < KB; ++i) for (long j = 0; j < n; ++j) g_sink = base->getAt(c, int(j)); });
        measure("has_missing_scan", n, 5, KB, double(n), [&](ProtoContext* c) { for (long i = 0; i < KB; ++i) g_sink = (const void*)(uintptr_t)base->has(c, missing); });
    }
    return 0;
}