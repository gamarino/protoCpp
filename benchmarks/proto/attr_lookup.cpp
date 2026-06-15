// attr_lookup — protoCpp version.  Read 3 attributes off a mutable
// ProtoObject N times.  Exercises the AttributeCache hot path —
// protoCore's per-thread 1024-entry cache that short-circuits the
// AVL walk for repeated reads of the same (snapshot-pointer, key)
// pair.

#include "protoCore.h"
#include <cstdio>

using namespace proto;

int main() {
    constexpr long long N = 5000000;
    ProtoSpace space;
    ProtoContext* ctx = space.rootContext;

    auto* obj = const_cast<ProtoObject*>(
        space.objectPrototype->newChild(ctx, /*isMutable=*/true));
    const ProtoString* aKey = ProtoString::createSymbol(ctx, "a");
    const ProtoString* bKey = ProtoString::createSymbol(ctx, "b");
    const ProtoString* cKey = ProtoString::createSymbol(ctx, "c");
    obj->setAttribute(ctx, aKey, ctx->fromLong(1));
    obj->setAttribute(ctx, bKey, ctx->fromLong(2));
    obj->setAttribute(ctx, cKey, ctx->fromLong(3));

    const ProtoObject* total = ctx->fromLong(0);
    for (long long i = 0; i < N; ++i) {
        total = total->add(ctx, obj->getAttribute(ctx, aKey));
        total = total->add(ctx, obj->getAttribute(ctx, bKey));
        total = total->add(ctx, obj->getAttribute(ctx, cKey));
    }
    std::printf("%lld\n", total->asLong(ctx));
    return 0;
}
