// call_recursion — protoCpp version.  fib(25) with the recursive arg
// and accumulator as ProtoObjects throughout. Every `+` and `<=`
// crosses the public protoCore API.
//
// Note: this is intentionally a "naive" port — no fast-path
// shortcuts. It measures what an embedder pays when they hold their
// numbers as ProtoObjects across function calls.

#include "protoCore.h"
#include <cstdio>

using namespace proto;

static const ProtoObject* TWO  = nullptr;
static const ProtoObject* ZERO = nullptr;
static const ProtoObject* ONE  = nullptr;

static const ProtoObject* fib(ProtoContext* ctx, const ProtoObject* n) {
    // n <= 1  →  n
    if (n->compare(ctx, TWO) < 0) return n;
    const ProtoObject* n1 = n->subtract(ctx, ONE);
    const ProtoObject* n2 = n->subtract(ctx, TWO);
    return fib(ctx, n1)->add(ctx, fib(ctx, n2));
}

int main() {
    ProtoSpace space;
    ProtoContext* ctx = space.rootContext;

    ZERO = ctx->fromLong(0);
    ONE  = ctx->fromLong(1);
    TWO  = ctx->fromLong(2);

    const ProtoObject* result = fib(ctx, ctx->fromLong(25));
    std::printf("%lld\n", result->asLong(ctx));
    return 0;
}
