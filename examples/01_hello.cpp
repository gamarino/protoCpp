// 01_hello.cpp — Minimal protoCore usage from C++.
//
// Creates a ProtoSpace (the kernel root), builds a small ProtoList,
// and prints each element. No language runtime, no interpreter — just
// the kernel directly.

#include "protoCore.h"
#include <cstdio>

using namespace proto;

int main() {
    ProtoSpace space;
    ProtoContext* ctx = space.rootContext;

    // Build [1, 2, 3]. Each integer becomes a tagged-pointer SmallInt
    // — no heap allocation for the int values themselves.
    const ProtoList* lst = ctx->newList();
    for (int i = 1; i <= 3; ++i) {
        lst = lst->appendLast(ctx, ctx->fromLong(i));
    }

    std::printf("[");
    for (unsigned long i = 0; i < lst->getSize(ctx); ++i) {
        if (i > 0) std::printf(", ");
        std::printf("%lld", lst->getAt(ctx, (int)i)->asLong(ctx));
    }
    std::printf("]\n");
    return 0;
}
