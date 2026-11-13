// list_ops_kernel — the cost of every ProtoList operation in today's kernel.
//
// Each measurement runs in child ProtoContexts (steady state, run with
// PROTOCORE_ADAPTIVE_HEAP=1), one warm-up batch and COSTMODEL_BATCHES measured
// batches, wall clock and process CPU time (bench_common.h). For every list
// size N a list of 0..N-1 is built once in the root context and every
// operation is applied to it (immutable: the base list never changes). Each
// operation checks the result of its last call; a mismatch exits with status 2.
// Output: RES op=<op> N=<n> ns=<median ns per call> min= max= cpu= cpu_max= cells=<cells per call> [per=element]

#include "protoCore.h"
#include "bench_common.h"
#include <algorithm>
#include <cstdio>
#include <functional>
#include <vector>

using namespace proto;
static const void* volatile g_sink;
static ProtoSpace* S;

// `calls` calls per repetition; `per` divides the time again (elements per call)
static void measure(const char* op, long n, long reps, long calls, double per,
                    const std::function<void(ProtoContext*)>& body) {
    cm_samples s{}; double cells = 0;
    for (int batch = 0; batch <= COSTMODEL_BATCHES; ++batch) {
        double w0 = cm_wall_ns(), c0 = cm_cpu_ns(); proto_ulong c = 0;
        for (long r = 0; r < reps; ++r) {
            ProtoContext ctx(S, S->rootContext);
            body(&ctx);
            c += ctx.allocatedCellsCount;
        }
        double k = double(reps) * double(calls);
        if (batch > 0) cm_add(&s, (cm_wall_ns() - w0) / k, (cm_cpu_ns() - c0) / k);
        cells = double(c) / k;
    }
    std::printf("RES op=%s N=%ld", op, n);
    cm_print_times(&s, per > 1 ? per : 1.0);
    if (per > 1) std::printf(" cells=%.3f per=element\n", cells / per);
    else std::printf(" cells=%.2f\n", cells);
    std::fflush(stdout);
}

static void expectList(ProtoContext* c, const ProtoList* l, long size, long first, long last, const char* what) {
    if ((long)l->getSize(c) != size) cm_fail(what);
    if (size > 0 && (l->getAt(c, 0)->asLong(c) != first || l->getLast(c)->asLong(c) != last)) cm_fail(what);
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
        const int last = idx(K - 1);

        measure("getSize", n, 20, K, 1, [&](ProtoContext* c) {
            proto_ulong z = 0; for (long i = 0; i < K; ++i) z = base->getSize(c);
            if ((long)z != n) cm_fail("getSize"); });
        measure("getLast", n, 20, K, 1, [&](ProtoContext* c) {
            const ProtoObject* v = nullptr; for (long i = 0; i < K; ++i) v = base->getLast(c);
            if (v->asLong(c) != n - 1) cm_fail("getLast"); });
        measure("getAt_random", n, 20, K, 1, [&](ProtoContext* c) {
            const ProtoObject* v = nullptr; for (long i = 0; i < K; ++i) v = base->getAt(c, idx(i));
            if (v->asLong(c) != last) cm_fail("getAt_random"); });
        measure("setAt_random", n, 20, K, 1, [&](ProtoContext* c) {
            const ProtoList* r = nullptr; for (long i = 0; i < K; ++i) r = base->setAt(c, idx(i), missing);
            if (r->getAt(c, last)->asLong(c) != -1 || (long)r->getSize(c) != n) cm_fail("setAt_random"); });
        measure("appendLast_on_N", n, 20, K, 1, [&](ProtoContext* c) {
            const ProtoList* r = nullptr; for (long i = 0; i < K; ++i) r = base->appendLast(c, missing);
            expectList(c, r, n + 1, 0, -1, "appendLast_on_N"); });
        measure("appendFirst", n, 20, K, 1, [&](ProtoContext* c) {
            const ProtoList* r = nullptr; for (long i = 0; i < K; ++i) r = base->appendFirst(c, missing);
            expectList(c, r, n + 1, -1, n - 1, "appendFirst"); });
        measure("insertAt_middle", n, 20, K, 1, [&](ProtoContext* c) {
            const ProtoList* r = nullptr; for (long i = 0; i < K; ++i) r = base->insertAt(c, int(n / 2), missing);
            expectList(c, r, n + 1, 0, n - 1, "insertAt_middle");
            if (r->getAt(c, int(n / 2))->asLong(c) != -1) cm_fail("insertAt_middle position"); });
        measure("removeAt_middle", n, 20, K, 1, [&](ProtoContext* c) {
            const ProtoList* r = nullptr; for (long i = 0; i < K; ++i) r = base->removeAt(c, int(n / 2));
            expectList(c, r, n - 1, 0, n - 1, "removeAt_middle"); });
        measure("removeFirst", n, 20, K, 1, [&](ProtoContext* c) {
            const ProtoList* r = nullptr; for (long i = 0; i < K; ++i) r = base->removeFirst(c);
            expectList(c, r, n - 1, 1, n - 1, "removeFirst"); });
        measure("removeLast", n, 20, K, 1, [&](ProtoContext* c) {
            const ProtoList* r = nullptr; for (long i = 0; i < K; ++i) r = base->removeLast(c);
            expectList(c, r, n - 1, 0, n - 2, "removeLast"); });
        const long KB = std::max(1L, 2000000 / n);  // calls for O(n)-ish operations
        measure("extend_two_halves", n, 5, KB, 1, [&](ProtoContext* c) {
            const ProtoList* r = nullptr; for (long i = 0; i < KB; ++i) r = half->extend(c, half);
            expectList(c, r, 2 * (n / 2), 0, n / 2 - 1, "extend_two_halves"); });
        measure("getSlice_middle_half", n, 5, KB, 1, [&](ProtoContext* c) {
            const ProtoList* r = nullptr; for (long i = 0; i < KB; ++i) r = base->getSlice(c, int(n / 4), int(3 * n / 4));
            expectList(c, r, 3 * n / 4 - n / 4, n / 4, 3 * n / 4 - 1, "getSlice_middle_half"); });
        measure("splitFirst_half", n, 5, KB, 1, [&](ProtoContext* c) {
            const ProtoList* r = nullptr; for (long i = 0; i < KB; ++i) r = base->splitFirst(c, int(n / 2));
            expectList(c, r, n / 2, 0, n / 2 - 1, "splitFirst_half"); });
        measure("iterate_all", n, 5, KB, double(n), [&](ProtoContext* c) {
            long sum = 0, count = 0;
            for (long i = 0; i < KB; ++i) {
                sum = 0; count = 0;
                for (auto* it = base->getIterator(c); it->hasNext(c); it = it->advance(c)) {
                    const ProtoObject* v = it->next(c); g_sink = v; ++count;
                    if (count == n) sum = v->asLong(c);    // one check per pass, not per element
                }
            }
            if (count != n || sum != n - 1) cm_fail("iterate_all"); });
        measure("getAt_sequential_all", n, 5, KB, double(n), [&](ProtoContext* c) {
            const ProtoObject* v = nullptr;
            for (long i = 0; i < KB; ++i) for (long j = 0; j < n; ++j) v = base->getAt(c, int(j));
            if (v->asLong(c) != n - 1) cm_fail("getAt_sequential_all"); });
        measure("has_missing_scan", n, 5, KB, double(n), [&](ProtoContext* c) {
            bool h = true; for (long i = 0; i < KB; ++i) h = base->has(c, missing);
            if (h) cm_fail("has_missing_scan"); });
    }
    return 0;
}
