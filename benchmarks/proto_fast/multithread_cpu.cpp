// multithread_cpu — protoCpp fast-path version.
//
// Same 4-thread × 2M-iteration accumulator as the public-API version,
// but each worker uses the inline SmallInt helpers for the inner add.
// Tests whether per-iteration kernel overhead OR cross-thread
// coordination dominates on this workload.

#include "protoCore.h"
#include <cstdio>
#include <vector>

using namespace proto;

constexpr long long CHUNK = 2'000'000;
constexpr int N_THREADS = 4;

static const ProtoObject* workerMain(
        ProtoContext* ctx,
        const ProtoObject*,
        const ParentLink*,
        const ProtoList* args,
        const ProtoSparseList*) {
    auto* cell = const_cast<ProtoObject*>(args->getAt(ctx, 0));
    const ProtoObject* total = ctx->fromLong(0);
    for (long long i = 0; i < CHUNK; ++i) {
        const ProtoObject* iv = ctx->fromLong(i);
        if (isSmallInt(total) && isSmallInt(iv)) {
            long long s = asSmallInt(total) + asSmallInt(iv);
            if (smallIntInRange(s)) {
                total = makeSmallInt(s);
                continue;
            }
        }
        total = total->add(ctx, iv);
    }
    const ProtoString* valueKey = ProtoString::createSymbol(ctx, "value");
    cell->setAttribute(ctx, valueKey, total);
    return PROTO_NONE;
}

int main() {
    ProtoSpace space;
    ProtoContext* ctx = space.rootContext;

    const ProtoString* valueKey = ProtoString::createSymbol(ctx, "value");
    const ProtoString* name     = ProtoString::createSymbol(ctx, "worker");

    std::vector<const ProtoThread*> threads;
    std::vector<ProtoObject*> cells;
    threads.reserve(N_THREADS);
    cells.reserve(N_THREADS);

    for (int t = 0; t < N_THREADS; ++t) {
        auto* cell = const_cast<ProtoObject*>(
            space.objectPrototype->newChild(ctx, true));
        cells.push_back(cell);
        const ProtoList* args = ctx->newList()->appendLast(ctx, cell);
        threads.push_back(
            space.newThread(ctx, name, &workerMain, args, nullptr));
    }
    for (auto* t : threads) const_cast<ProtoThread*>(t)->join(ctx);

    long long grand = 0;
    for (auto* cell : cells) {
        grand += cell->getAttribute(ctx, valueKey)->asLong(ctx);
    }
    std::printf("%lld\n", grand);
    return 0;
}
