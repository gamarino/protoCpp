// 04_structural_sharing.cpp — Immutable ProtoList "mutations" share
// structure with the original.
//
// ProtoList is an AVL-tree-backed sequence. `appendLast` returns a
// new ProtoList that shares most of the original's tree; the cost is
// O(log N), not O(N), even though the result is fully immutable from
// the caller's perspective. This is the same trick Clojure's
// persistent vectors and Haskell's Data.Sequence use, lowered to a
// kernel primitive.

#include "protoCore.h"
#include <cstdio>

using namespace proto;

int main() {
    ProtoSpace space;
    ProtoContext* ctx = space.rootContext;

    // Build a base list of 0..9.
    const ProtoList* base = ctx->newList();
    for (int i = 0; i < 10; ++i) {
        base = base->appendLast(ctx, ctx->fromLong(i));
    }

    // "Mutate" the base by appending — base itself is untouched.
    const ProtoList* withTen = base->appendLast(ctx, ctx->fromLong(10));

    std::printf("base size:    %lu\n", base->getSize(ctx));
    std::printf("withTen size: %lu\n", withTen->getSize(ctx));
    std::printf("base last:    %lld\n",
        base->getAt(ctx, base->getSize(ctx) - 1)->asLong(ctx));
    std::printf("withTen last: %lld\n",
        withTen->getAt(ctx, withTen->getSize(ctx) - 1)->asLong(ctx));

    // Crucially: building withTen did NOT copy the first 10 elements.
    // It allocated O(log N) new tree nodes that point back into base's
    // tree. base and withTen physically share most of their storage.
    return 0;
}
