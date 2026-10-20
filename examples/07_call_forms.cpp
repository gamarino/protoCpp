// 07_call_forms.cpp — one method, two ways to call it (protoCore 3.0.0).
//
// A ProtoMethod reads its arguments through descriptors: ProtoArguments for
// the positional ones, ProtoKeywordArguments for the named ones. It does not
// know, and does not need to know, how the caller passed them:
//
//   * the list form   — the caller holds a ProtoList and a ProtoSparseList;
//   * the array form  — the caller holds the values in an array it owns and
//                       sizes itself, here its context's automatic locals,
//                       which the collector traces; nothing is allocated;
//   * the descriptor form — a method passes on the descriptors it received.
//
// See protoCore docs/CALLING_CONVENTION.md.

#include "protoCore.h"
#include <cstdio>

using namespace proto;

namespace {

const ProtoString* gScale = nullptr;   // the keyword `scale`, an interned symbol

// sum(a, b, ..., scale = s) = s * (a + b + ...), s = 1 when it is not given.
// The parameters are bound by reading the descriptor, whatever the call form.
const ProtoObject* sum(ProtoContext* ctx, const ProtoObject*, const ParentLink*,
                       const ProtoArguments* args, const ProtoKeywordArguments* keywords) {
    long long total = 0;
    for (unsigned i = 0; i < args->count(); ++i) total += args->getAt(ctx, static_cast<int>(i))->asLong(ctx);
    long long scale = 1;
    if (keywords && keywords->has(ctx, reinterpret_cast<proto_ulong>(gScale)))
        scale = keywords->getAt(ctx, reinterpret_cast<proto_ulong>(gScale))->asLong(ctx);
    return ctx->fromLong(scale * total);
}

// Passes the descriptors it received on to `sum`, unchanged (descriptor form).
const ProtoObject* forward(ProtoContext* ctx, const ProtoObject* self, const ParentLink*,
                           const ProtoArguments* args, const ProtoKeywordArguments* keywords) {
    return self->call(ctx, nullptr, ProtoString::createSymbol(ctx, "sum"), self, *args, keywords);
}

} // namespace

int main() {
    ProtoSpace space;
    ProtoContext* ctx = space.rootContext;
    gScale = ProtoString::createSymbol(ctx, "scale");
    const ProtoString* sumName = ProtoString::createSymbol(ctx, "sum");
    const ProtoString* forwardName = ProtoString::createSymbol(ctx, "forward");

    // One method definition, installed once.
    const ProtoObject* calculator = space.objectPrototype->newChild(ctx)
        ->setAttribute(ctx, sumName, ctx->fromMethod(nullptr, &sum))
        ->setAttribute(ctx, forwardName, ctx->fromMethod(nullptr, &forward));

    // 1. The list form: a ProtoList of arguments and a ProtoSparseList of
    //    keywords keyed by the interned name's pointer.
    const ProtoList* list = ctx->newList()
        ->appendLast(ctx, ctx->fromLong(1))
        ->appendLast(ctx, ctx->fromLong(2))
        ->appendLast(ctx, ctx->fromLong(3));
    const ProtoSparseList* dict = ctx->newSparseList()
        ->setAt(ctx, reinterpret_cast<proto_ulong>(gScale), ctx->fromLong(10));
    std::printf("list form:       %lld\n",
                calculator->call(ctx, nullptr, sumName, calculator, list, dict)->asLong(ctx));

    // 2. The array form: the values in the caller context's automatic locals
    //    (sized by the caller, traced by the collector), the keywords as
    //    (name, value) pairs. The method reads the caller's array in place.
    ProtoContext caller(&space, ctx, nullptr, nullptr, nullptr, nullptr, 3);
    caller.setAutomaticLocal(0, caller.fromLong(1));
    caller.setAutomaticLocal(1, caller.fromLong(2));
    caller.setAutomaticLocal(2, caller.fromLong(3));
    const ProtoKeywordArgument scale[1] = {{gScale, caller.fromLong(10)}};
    std::printf("array form:      %lld\n",
                calculator->call(&caller, nullptr, sumName, calculator,
                                 caller.getAutomaticLocals(), 3, scale, 1)->asLong(&caller));

    // The same call without keywords: `scale` defaults to 1.
    std::printf("no keywords:     %lld\n",
                calculator->call(&caller, nullptr, sumName, calculator,
                                 caller.getAutomaticLocals(), 3)->asLong(&caller));

    // 3. The descriptor form: `forward` passes what it received on to `sum`.
    std::printf("descriptor form: %lld\n",
                calculator->call(&caller, nullptr, forwardName, calculator,
                                 caller.getAutomaticLocals(), 3, scale, 1)->asLong(&caller));
    return 0;
}
