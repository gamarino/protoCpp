// 02_smallint_fast_path.cpp — SmallInt tagged-pointer arithmetic without
// crossing the protoCore shared-library boundary.
//
// protoCore exposes `proto::isSmallInt`, `proto::asSmallInt`, and
// `proto::makeSmallInt` as `static inline` helpers in `protoCore.h`.
// They operate on the raw bit pattern of the SmallInt tag (signed
// 54-bit value at bits 10-63), so callers can short-circuit the
// common SmallInt+SmallInt case in a hot loop without a single
// function call into protoCore. This is the fastest possible numeric
// path — everything an embedder can do from C++ without modifying
// the kernel.

#include "protoCore.h"
#include <cstdio>

using namespace proto;

int main() {
    ProtoSpace space;
    ProtoContext* ctx = space.rootContext;

    const ProtoObject* a = ctx->fromLong(40);
    const ProtoObject* b = ctx->fromLong(2);

    // Path A — public arithmetic. Calls into protoCore's add() which
    // checks tags, promotes to LargeInteger on overflow, etc.
    const ProtoObject* sum = a->add(ctx, b);
    std::printf("via add():           %lld\n", sum->asLong(ctx));

    // Path B — embedder-side fast path. We verified both are SmallInt;
    // unwrap, add, re-wrap. No cross-DSO call, no overflow check.
    if (isSmallInt(a) && isSmallInt(b)) {
        long long aVal = asSmallInt(a);
        long long bVal = asSmallInt(b);
        long long sumVal = aVal + bVal;
        if (smallIntInRange(sumVal)) {
            const ProtoObject* fastSum = makeSmallInt(sumVal);
            std::printf("via SmallInt helpers: %lld\n", fastSum->asLong(ctx));
        } else {
            std::printf("overflow, promote needed\n");
        }
    }
    return 0;
}
