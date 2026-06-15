// str_concat_loop — protoCpp version.  Build a ProtoString by 2000
// rope-style appendLast operations.  ProtoString is a rope, so each
// concat is O(log N) sharing rather than O(N) copy — the persistent-
// string analogue of the persistent-list workload.

#include "protoCore.h"
#include <cstdio>

using namespace proto;

int main() {
    constexpr int N = 2000;
    ProtoSpace space;
    ProtoContext* ctx = space.rootContext;

    const ProtoString* x =
        reinterpret_cast<const ProtoString*>(ctx->fromUTF8String("x"));
    const ProtoString* s =
        reinterpret_cast<const ProtoString*>(ctx->fromUTF8String(""));
    for (int i = 0; i < N; ++i) {
        s = s->appendLast(ctx, x);
    }
    std::printf("%lu\n", s->getSize(ctx));
    return 0;
}
