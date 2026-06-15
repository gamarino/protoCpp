// 05_threads.cpp — Spawn a protoCore-managed worker thread.
//
// `ProtoSpace::newThread` creates a real OS thread that participates
// in the GC quorum. The thread runs a ProtoMethod (a C function
// pointer with a specific signature), receives positional args via a
// ProtoList, and returns a ProtoObject. No GIL; native threading
// model is the kernel's model.

#include "protoCore.h"
#include <cstdio>

using namespace proto;

// Worker entry. Receives a "shared result cell" (a mutable
// ProtoObject) and an input SmallInt via args, writes square(input)
// onto the result cell's `value` attribute, and returns. Workers
// communicate their result via a shared mutable attribute because
// `ProtoThread::join` is void — protoCore is deliberately minimal here.
static const ProtoObject* workerMain(
        ProtoContext* ctx,
        const ProtoObject* /*self*/,
        const ParentLink*,
        const ProtoList* args,
        const ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 2) return PROTO_NONE;
    auto* resultCell = const_cast<ProtoObject*>(args->getAt(ctx, 0));
    long long x = args->getAt(ctx, 1)->asLong(ctx);
    const ProtoString* valueKey = ProtoString::createSymbol(ctx, "value");
    resultCell->setAttribute(ctx, valueKey, ctx->fromLong(x * x));
    return PROTO_NONE;
}

int main() {
    ProtoSpace space;
    ProtoContext* ctx = space.rootContext;

    // The shared result cell.
    auto* resultCell = const_cast<ProtoObject*>(
        space.objectPrototype->newChild(ctx, /*isMutable=*/true));
    const ProtoString* valueKey = ProtoString::createSymbol(ctx, "value");

    const ProtoString* name = ProtoString::createSymbol(ctx, "demo-worker");
    const ProtoList* args = ctx->newList()
        ->appendLast(ctx, resultCell)
        ->appendLast(ctx, ctx->fromLong(7));

    const ProtoThread* t = space.newThread(ctx, name, &workerMain, args, nullptr);
    const_cast<ProtoThread*>(t)->join(ctx);

    const ProtoObject* result = resultCell->getAttribute(ctx, valueKey);
    std::printf("worker returned: %lld\n", result->asLong(ctx));
    return 0;
}
