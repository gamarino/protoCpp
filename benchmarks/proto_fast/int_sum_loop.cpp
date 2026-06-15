// int_sum_loop — protoCpp fast-path version.
//
// Uses the inline SmallInt helpers (`proto::isSmallInt`, `proto::asSmallInt`,
// `proto::makeSmallInt`, `proto::smallIntInRange`) to short-circuit the
// cross-DSO call into `ProtoObject::add`. Same observable semantics, same
// final value — this measures how much of the kernel cost is the function-
// call boundary into libprotoCore vs the actual arithmetic.
//
// Falls back to the public `add` API if either operand is NOT a SmallInt
// or if the sum overflows the 56-bit tag range, exactly the same way
// protoPython's INPLACE_ADD opcode does.

#include "protoCore.h"
#include <cstdio>

using namespace proto;

int main() {
    constexpr long long N = 100000;
    ProtoSpace space;
    ProtoContext* ctx = space.rootContext;

    const ProtoObject* total = ctx->fromLong(0);
    for (long long i = 0; i < N; ++i) {
        const ProtoObject* iv = ctx->fromLong(i);
        // Fast path — both are SmallInt, sum fits in the tag range:
        // no cross-DSO call, no virtual dispatch, no overflow check
        // (we did the check ourselves; the branch is well-predicted).
        if (isSmallInt(total) && isSmallInt(iv)) {
            long long s = asSmallInt(total) + asSmallInt(iv);
            if (smallIntInRange(s)) {
                total = makeSmallInt(s);
                continue;
            }
        }
        // Slow path — anything else routes through the public API.
        total = total->add(ctx, iv);
    }
    std::printf("%lld\n", total->asLong(ctx));
    return 0;
}
