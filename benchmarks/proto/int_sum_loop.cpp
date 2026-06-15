// int_sum_loop — protoCpp version.  Same semantics as the pure-C++
// baseline (sum 0..99999) but the running total is a ProtoObject and
// every addition goes through `ProtoObject::add`. No language runtime,
// no bytecode interpreter — just the kernel arithmetic API.

#include "protoCore.h"
#include <cstdio>

using namespace proto;

int main() {
    constexpr long long N = 10000000;
    ProtoSpace space;
    ProtoContext* ctx = space.rootContext;

    const ProtoObject* total = ctx->fromLong(0);
    for (long long i = 0; i < N; ++i) {
        total = total->add(ctx, ctx->fromLong(i));
    }
    std::printf("%lld\n", total->asLong(ctx));
    return 0;
}
