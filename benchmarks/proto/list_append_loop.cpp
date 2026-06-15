// list_append_loop — protoCpp version.  Grow a ProtoList by 10K
// appendLast calls.  Each call returns a NEW ProtoList sharing
// structure with the old; no in-place mutation.  This is the cost
// of the persistent-structure backbone every protoCore-hosted
// language pays.

#include "protoCore.h"
#include <cstdio>

using namespace proto;

int main() {
    constexpr int N = 10000;
    ProtoSpace space;
    ProtoContext* ctx = space.rootContext;

    const ProtoList* lst = ctx->newList();
    for (int i = 0; i < N; ++i) {
        lst = lst->appendLast(ctx, ctx->fromLong(i));
    }
    std::printf("%lu\n", lst->getSize(ctx));
    return 0;
}
