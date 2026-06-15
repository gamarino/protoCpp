// 06_actor_manual.cpp — Build a one-actor system on protoCore by hand.
//
// No protoClojure / protoST helpers — just the kernel. Demonstrates
// the moving parts: a mailbox (ProtoList CAS-append), a worker thread
// that drains it, and a "messages processed" counter on a mutable
// attribute. The same shape every language runtime in the family
// builds on top of.

#include "protoCore.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace proto;

struct Mailbox {
    ProtoObject* anchor;
    const ProtoString* msgsKey;
    const ProtoString* doneKey;
    std::atomic<bool> done{false};
    std::atomic<long long> processed{0};  // worker writes here; main reads
};

static const ProtoObject* drainerMain(
        ProtoContext* ctx,
        const ProtoObject*,
        const ParentLink*,
        const ProtoList* args,
        const ProtoSparseList*) {
    auto* mb = reinterpret_cast<Mailbox*>(
        args->getAt(ctx, 0)->asLong(ctx));

    long long count = 0;
    while (!mb->done.load(std::memory_order_acquire)) {
        const ProtoObject* msgsObj =
            mb->anchor->getAttribute(ctx, mb->msgsKey);
        const ProtoList* msgs = msgsObj->asList(ctx);
        if (msgs->getSize(ctx) > 0) {
            // CAS-pop the head: replace mailbox with its tail.
            const ProtoList* tail =
                msgs->getSlice(ctx, 1, (int)msgs->getSize(ctx));
            const ProtoObject* tailObj = tail->asObject(ctx);
            if (mb->anchor->setAttributeIfEqual(
                    ctx, mb->msgsKey, msgsObj, tailObj)) {
                ++count;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }
    mb->processed.store(count, std::memory_order_release);
    return PROTO_NONE;
}

int main() {
    ProtoSpace space;
    ProtoContext* ctx = space.rootContext;

    Mailbox mb;
    mb.anchor = const_cast<ProtoObject*>(
        space.objectPrototype->newChild(ctx, /*isMutable=*/true));
    mb.msgsKey = ProtoString::createSymbol(ctx, "msgs");
    mb.doneKey = ProtoString::createSymbol(ctx, "done");
    mb.anchor->setAttribute(ctx, mb.msgsKey, ctx->newList()->asObject(ctx));

    // Spawn the drainer.
    const ProtoString* name = ProtoString::createSymbol(ctx, "actor-drainer");
    const ProtoList* args = ctx->newList()
        ->appendLast(ctx, ctx->fromLong((long long)&mb));
    const ProtoThread* t =
        space.newThread(ctx, name, &drainerMain, args, nullptr);

    // Send 1000 "messages" — each is just a SmallInt for this demo.
    for (int i = 0; i < 1000; ++i) {
        for (;;) {
            const ProtoObject* cur = mb.anchor->getAttribute(ctx, mb.msgsKey);
            const ProtoList* msgs = cur->asList(ctx);
            const ProtoList* nxt = msgs->appendLast(ctx, ctx->fromLong(i));
            const ProtoObject* nxtObj = nxt->asObject(ctx);
            if (mb.anchor->setAttributeIfEqual(
                    ctx, mb.msgsKey, cur, nxtObj)) break;
        }
    }

    // Wait until the mailbox is empty, then signal done.
    while (true) {
        const ProtoObject* cur = mb.anchor->getAttribute(ctx, mb.msgsKey);
        if (cur->asList(ctx)->getSize(ctx) == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    mb.done.store(true, std::memory_order_release);

    const_cast<ProtoThread*>(t)->join(ctx);
    std::printf("processed: %lld\n",
        mb.processed.load(std::memory_order_acquire));
    return 0;
}
