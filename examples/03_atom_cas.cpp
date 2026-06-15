// 03_atom_cas.cpp — Compare-and-swap on a mutable ProtoObject attribute.
//
// `setAttributeIfEqual` is the primitive every runtime in the family
// uses to implement atoms / atomics / actor mailboxes / promise
// completion. It is lock-free, kernel-supported, and atomic across
// threads.

#include "protoCore.h"
#include <cstdio>

using namespace proto;

int main() {
    ProtoSpace space;
    ProtoContext* ctx = space.rootContext;

    // A mutable child of objectPrototype is the simplest "atom" shape.
    ProtoObject* atom = const_cast<ProtoObject*>(
        space.objectPrototype->newChild(ctx, /*isMutable=*/true));

    const ProtoString* valueKey = ProtoString::createSymbol(ctx, "value");

    atom->setAttribute(ctx, valueKey, ctx->fromLong(0));
    std::printf("initial: %lld\n",
        atom->getAttribute(ctx, valueKey)->asLong(ctx));

    // CAS-retry loop: increment until success. Mirrors swap! in
    // protoClojure / value:ifCurrent: in protoST.
    for (int attempt = 0; attempt < 100; ++attempt) {
        const ProtoObject* old = atom->getAttribute(ctx, valueKey);
        long long oldVal = old->asLong(ctx);
        const ProtoObject* neu = ctx->fromLong(oldVal + 1);
        if (atom->setAttributeIfEqual(ctx, valueKey, old, neu)) break;
    }

    std::printf("after CAS inc: %lld\n",
        atom->getAttribute(ctx, valueKey)->asLong(ctx));
    return 0;
}
