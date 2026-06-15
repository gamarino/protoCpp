// call_recursion — protoCpp fast-path version.
//
// fib(25) with the same recursive shape as the public-API version, but
// every `+`, `-`, and `<=` uses the inline SmallInt helpers when both
// operands fit. Models exactly what protoPython's bytecode interpreter
// does in its INPLACE_ADD / INPLACE_SUBTRACT / COMPARE_OP hot paths.

#include "protoCore.h"
#include <cstdio>

using namespace proto;

static const ProtoObject* ZERO = nullptr;
static const ProtoObject* ONE  = nullptr;
static const ProtoObject* TWO  = nullptr;

static inline const ProtoObject*
add_fast(ProtoContext* ctx, const ProtoObject* a, const ProtoObject* b) {
    if (isSmallInt(a) && isSmallInt(b)) {
        long long s = asSmallInt(a) + asSmallInt(b);
        if (smallIntInRange(s)) return makeSmallInt(s);
    }
    return a->add(ctx, b);
}
static inline const ProtoObject*
sub_fast(ProtoContext* ctx, const ProtoObject* a, const ProtoObject* b) {
    if (isSmallInt(a) && isSmallInt(b)) {
        long long s = asSmallInt(a) - asSmallInt(b);
        if (smallIntInRange(s)) return makeSmallInt(s);
    }
    return a->subtract(ctx, b);
}
static inline bool
lt_fast(ProtoContext* ctx, const ProtoObject* a, const ProtoObject* b) {
    if (isSmallInt(a) && isSmallInt(b)) {
        return asSmallInt(a) < asSmallInt(b);
    }
    return a->compare(ctx, b) < 0;
}

static const ProtoObject* fib(ProtoContext* ctx, const ProtoObject* n) {
    if (lt_fast(ctx, n, TWO)) return n;
    const ProtoObject* n1 = sub_fast(ctx, n, ONE);
    const ProtoObject* n2 = sub_fast(ctx, n, TWO);
    return add_fast(ctx, fib(ctx, n1), fib(ctx, n2));
}

int main() {
    ProtoSpace space;
    ProtoContext* ctx = space.rootContext;
    ZERO = ctx->fromLong(0);
    ONE  = ctx->fromLong(1);
    TWO  = ctx->fromLong(2);
    const ProtoObject* r = fib(ctx, ctx->fromLong(25));
    std::printf("%lld\n", r->asLong(ctx));
    return 0;
}
