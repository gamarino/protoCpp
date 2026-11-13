// kernel_costmodel — steady-state cost of protoCore primitives, measured in-process.
//
// Output lines (see bench_common.h):
//   RES op=<key> ns=<median wall ns/op> min= max= cpu=<median process-CPU ns/op> cpu_max= cells=<cells/op>
// costmodel/run.sh pairs them with capi_costmodel (CPython's C API, the fair
// twin: a library primitive called from compiled code on both sides) and
// cpython_costmodel.py (the CPython interpreter).
//
// Each repetition runs in its own child ProtoContext (as a runtime's calls do),
// so the young cells are handed to the collector and recycled. Run with
// PROTOCORE_ADAPTIVE_HEAP=1 for the steady-state figures; the runner records
// the heap mode. Every operation checks its computed result and the program
// exits with status 2 on a mismatch, so a silent failure never looks fast.
#include "protoCore.h"
#include "bench_common.h"
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace proto;
static const void* volatile g_sink;
static ProtoSpace* S;

// body(ctx) performs `opsPerRep` operations inside a fresh child context.
static void measure(const char* key, long reps, long opsPerRep,
                    const std::function<void(ProtoContext*)>& body) {
    cm_samples s{}; double cells = 0;
    for (int batch = 0; batch <= COSTMODEL_BATCHES; ++batch) {
        double w0 = cm_wall_ns(), c0 = cm_cpu_ns(); proto_ulong c = 0;
        for (long r = 0; r < reps; ++r) {
            ProtoContext ctx(S, S->rootContext);
            body(&ctx);
            c += ctx.allocatedCellsCount;
        }
        double ops = double(reps) * double(opsPerRep);
        double w = (cm_wall_ns() - w0) / ops, cpu = (cm_cpu_ns() - c0) / ops;
        if (batch > 0) cm_add(&s, w, cpu);              // batch 0 = warm-up
        cells = double(c) / ops;
    }
    std::printf("RES op=%s", key);
    cm_print_times(&s, 1.0);
    std::printf(" cells=%.2f\n", cells);
    std::fflush(stdout);
}

static const ProtoObject *ONE, *TWO;
static long g_calls;
// A C++ recursion over protoCore arithmetic: compare, two subtractions and an
// addition per step. It does NOT go through protoCore's call machinery (no
// method lookup, no context per call), so it is not "a call" in a runtime's sense.
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

    measure("int_add", 200, 100000, [](ProtoContext* c) {
        const ProtoObject* t = c->fromLong(0);
        for (long i = 0; i < 100000; ++i) t = t->add(c, c->fromLong(i));
        if (t->asLong(c) != 100000LL * 99999 / 2) cm_fail("int_add sum");
        g_sink = t; });

    measure("double_add", 200, 10000, [](ProtoContext* c) {
        const ProtoObject* t = c->fromDouble(0.0); const ProtoObject* h = c->fromDouble(0.5);
        for (long i = 0; i < 10000; ++i) t = t->add(c, h);
        if (t->asDouble(c) != 5000.0) cm_fail("double_add sum");
        g_sink = t; });

    { g_calls = 0; ProtoContext c0(S, root); fib(&c0, c0.fromLong(20)); long calls = g_calls;
      measure("fib20_step", 20, calls, [](ProtoContext* c) {
          const ProtoObject* r = fib(c, c->fromLong(20));
          if (r->asLong(c) != 6765) cm_fail("fib(20)");
          g_sink = r; }); }

    const ProtoObject* mobj = space.objectPrototype->newChild(root, true);
    for (int k = 0; k < 3; ++k) mobj->setAttribute(root, nm[k], root->fromLong(k));
    measure("getattr", 200, 30000, [&](ProtoContext* c) {
        const ProtoObject* v = nullptr;
        for (long i = 0; i < 10000; ++i) for (int k = 0; k < 3; ++k) v = mobj->getAttribute(c, nm[k]);
        if (v->asLong(c) != 2) cm_fail("getattr value");
        g_sink = v; });

    measure("setattr_mutable", 200, 10000, [&](ProtoContext* c) {
        for (long i = 0; i < 10000; ++i) mobj->setAttribute(c, nm[i & 3], c->fromLong(i));
        if (mobj->getAttribute(c, nm[3])->asLong(c) != 9999) cm_fail("setattr value"); });

    measure("new_object_4_attrs", 200, 2000, [&](ProtoContext* c) {
        const ProtoObject* o = nullptr;
        for (long i = 0; i < 2000; ++i) {
            o = space.objectPrototype->newChild(c, false);
            for (int k = 0; k < 4; ++k) o = o->setAttribute(c, nm[k], c->fromLong(k));
            g_sink = o;
        }
        if (o->getAttribute(c, nm[3])->asLong(c) != 3) cm_fail("new object attrs"); });

    for (long m : {1L, 1000L, 100000L}) {           // mutable write vs. live mutables
        std::vector<const ProtoObject*> keep;
        for (long j = 0; j < m; ++j) { const ProtoObject* x = root->newObject(true); x->setAttribute(root, nm[0], root->fromLong(j)); keep.push_back(x); }
        const ProtoObject* target = root->newObject(true);
        for (int k = 0; k < 16; ++k) target->setAttribute(root, nm[k], root->fromLong(k));
        char key[64]; std::snprintf(key, sizeof key, "mutable_write_16_attrs_%ld_live", m);
        measure(key, 100, 5000, [&](ProtoContext* c) {
            for (long i = 0; i < 5000; ++i) target->setAttribute(c, nm[i & 15], c->fromLong(i));
            if (target->getAttribute(c, nm[4999 & 15])->asLong(c) != 4999) cm_fail("mutable write value"); });
        g_sink = keep.data();
    }

    for (long n : {1000L, 10000L, 100000L}) {
        char key[64]; std::snprintf(key, sizeof key, "list_append_to_%ld", n);
        measure(key, std::max(2L, 2000000 / n), n, [n](ProtoContext* c) {
            const ProtoList* l = c->newList();
            for (long i = 0; i < n; ++i) l = l->appendLast(c, c->fromLong(i));
            if ((long)l->getSize(c) != n || l->getLast(c)->asLong(c) != n - 1) cm_fail("list append");
            g_sink = l; });
    }
    const ProtoList* big = root->newList();
    for (long i = 0; i < 100000; ++i) big = big->appendLast(root, root->fromLong(i));
    measure("list_getat_random_100k", 20, 100000, [&](ProtoContext* c) {
        const ProtoObject* v = nullptr;
        for (long i = 0; i < 100000; ++i) v = big->getAt(c, (int)((i * 7919) % 100000));
        if (v->asLong(c) != (99999L * 7919) % 100000) cm_fail("getAt random");
        g_sink = v; });
    measure("list_getat_seq_100k", 20, 100000, [&](ProtoContext* c) {
        const ProtoObject* v = nullptr;
        for (long i = 0; i < 100000; ++i) v = big->getAt(c, (int)i);
        if (v->asLong(c) != 99999) cm_fail("getAt sequential");
        g_sink = v; });

    for (long n : {2000L, 10000L}) {
        char key[64]; std::snprintf(key, sizeof key, "str_append_to_%ld", n);
        measure(key, std::max(2L, 400000 / n), n, [n](ProtoContext* c) {
            const ProtoString* s = ProtoString::fromUTF8(c, "");
            const ProtoString* x = ProtoString::fromUTF8(c, "x");
            for (long i = 0; i < n; ++i) s = s->appendLast(c, x);
            if ((long)s->getSize(c) != n) cm_fail("string append size");
            g_sink = s; });
    }
    return 0;
}
