// kernel_costmodel — steady-state cost of protoCore primitives, measured in-process.
// Output lines: RES op=<name> ns=<best ns/op> cells=<cells/op>; the runner
// (costmodel/run.sh) pairs them with cpython_costmodel.py and list_lab.
// Each repetition runs in its own child ProtoContext (as a runtime's calls do),
// so the young cells are handed to the collector and recycled.  Run with
// PROTOCORE_ADAPTIVE_HEAP=1.  Reports ns/op (best of 5 batches) and cells/op.
#include "protoCore.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include <algorithm>

using namespace proto;
static const void* volatile g_sink;
static ProtoSpace* S;

static double now_ns() {
    return std::chrono::duration<double, std::nano>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// body(ctx) performs `opsPerRep` operations inside a fresh child context.
static void measure(const char* name, long reps, long opsPerRep,
                    const std::function<void(ProtoContext*)>& body) {
    double best = 1e300; double cells = 0;
    for (int batch = 0; batch < 6; ++batch) {
        double t0 = now_ns(); proto_ulong c = 0;
        for (long r = 0; r < reps; ++r) {
            ProtoContext ctx(S, S->rootContext);
            body(&ctx);
            c += ctx.allocatedCellsCount;
        }
        double t = (now_ns() - t0) / (double)(reps * opsPerRep);
        if (batch > 0) best = std::min(best, t);        // batch 0 = warm-up
        cells = (double)c / (reps * opsPerRep);
    }
    std::string op(name); for (char& ch : op) if (ch == ' ') ch = '_';
    std::printf("RES op=%s ns=%.1f cells=%.2f\n", op.c_str(), best, cells);
    std::fflush(stdout);
}

static const ProtoObject *ONE, *TWO;
static long g_calls;
static const ProtoObject* fib(ProtoContext* ctx, const ProtoObject* n) {
    ++g_calls;
    if (n->compare(ctx, TWO) < 0) return n;
    return fib(ctx, n->subtract(ctx, ONE))->add(ctx, fib(ctx, n->subtract(ctx, TWO)));
}

int main() {
    ProtoSpace space; S = &space;
    ProtoContext* root = space.rootContext;
    ONE = root->fromLong(1); TWO = root->fromLong(2);
    std::vector<const ProtoString*> nm;
    for (int i = 0; i < 16; ++i) nm.push_back(ProtoString::createSymbol(root, ("a" + std::to_string(i)).c_str()));

    measure("int add (generic add, SmallInt)", 200, 100000, [](ProtoContext* c) {
        const ProtoObject* t = c->fromLong(0);
        for (long i = 0; i < 100000; ++i) t = t->add(c, c->fromLong(i));
        g_sink = t; });

    measure("double add (generic add)", 200, 10000, [](ProtoContext* c) {
        const ProtoObject* t = c->fromDouble(0.0); const ProtoObject* h = c->fromDouble(0.5);
        for (long i = 0; i < 10000; ++i) t = t->add(c, h);
        g_sink = t; });

    { g_calls = 0; ProtoContext c0(S, root); fib(&c0, c0.fromLong(20)); long calls = g_calls;
      measure("fib(20): per call (compare+2 sub+add)", 20, calls, [](ProtoContext* c) {
          g_sink = fib(c, c->fromLong(20)); }); }

    const ProtoObject* mobj = space.objectPrototype->newChild(root, true);
    for (int k = 0; k < 3; ++k) mobj->setAttribute(root, nm[k], root->fromLong(k));
    measure("getAttribute, mutable obj, 3 attrs", 200, 30000, [&](ProtoContext* c) {
        for (long i = 0; i < 10000; ++i) for (int k = 0; k < 3; ++k) g_sink = mobj->getAttribute(c, nm[k]); });

    measure("setAttribute mutable (4 attrs, few mutables)", 200, 10000, [&](ProtoContext* c) {
        for (long i = 0; i < 10000; ++i) mobj->setAttribute(c, nm[i & 3], c->fromLong(i)); });

    measure("new object + 4 attrs (instance creation)", 200, 2000, [&](ProtoContext* c) {
        for (long i = 0; i < 2000; ++i) { const ProtoObject* o = space.objectPrototype->newChild(c, false);
            for (int k = 0; k < 4; ++k) o = o->setAttribute(c, nm[k], c->fromLong(k)); g_sink = o; } });

    for (long m : {1L, 1000L, 100000L}) {           // mutable write vs. live mutables
        std::vector<const ProtoObject*> keep;
        for (long j = 0; j < m; ++j) { const ProtoObject* x = root->newObject(true); x->setAttribute(root, nm[0], root->fromLong(j)); keep.push_back(x); }
        const ProtoObject* target = root->newObject(true);
        for (int k = 0; k < 16; ++k) target->setAttribute(root, nm[k], root->fromLong(k));
        char lbl[80]; std::snprintf(lbl, sizeof lbl, "mutable write 16 attrs with %ld mutables live", m);
        measure(lbl, 100, 5000, [&](ProtoContext* c) {
            for (long i = 0; i < 5000; ++i) target->setAttribute(c, nm[i & 15], c->fromLong(i)); });
        g_sink = keep.data();
    }

    for (long n : {1000L, 10000L, 100000L}) {
        char lbl[64]; std::snprintf(lbl, sizeof lbl, "list appendLast, build to %ld", n);
        measure(lbl, std::max(2L, 2000000 / n), n, [n](ProtoContext* c) {
            const ProtoList* l = c->newList();
            for (long i = 0; i < n; ++i) l = l->appendLast(c, c->fromLong(i));
            g_sink = l; });
    }
    const ProtoList* big = root->newList();
    for (long i = 0; i < 100000; ++i) big = big->appendLast(root, root->fromLong(i));
    measure("list getAt random, 100k list", 20, 100000, [&](ProtoContext* c) {
        for (long i = 0; i < 100000; ++i) g_sink = big->getAt(c, (int)((i * 7919) % 100000)); });
    measure("list getAt sequential, 100k list", 20, 100000, [&](ProtoContext* c) {
        for (long i = 0; i < 100000; ++i) g_sink = big->getAt(c, (int)i); });

    for (long n : {2000L, 10000L}) {
        char lbl[64]; std::snprintf(lbl, sizeof lbl, "string += 1 char, build to %ld", n);
        measure(lbl, std::max(2L, 400000 / n), n, [n](ProtoContext* c) {
            const ProtoString* s = ProtoString::fromUTF8(c, "");
            const ProtoString* x = ProtoString::fromUTF8(c, "x");
            for (long i = 0; i < n; ++i) s = s->appendLast(c, x);
            g_sink = s; });
    }
    return 0;
}