// rrb_lab — design study: ProtoList as an RRB tree (relaxed radix balanced,
// Bagwell & Rompf 2011; L'orange 2014) on the simulated 64-byte cell heap of
// lab_common.h. No protoCore.
//
// Why: the trie of perf/list-trie (phase 1) converts the whole list to the AVL
// form for every structural operation, so deriving many versions from one
// list costs O(n) each (x280..x13000 per call, measured). An RRB tree does
// concatenation, slicing, insertion and removal anywhere in O(log n) on the
// same structure, with no second form.
//
// Layout (every node is 64 B: 16 B header + 48 B payload):
//   leaf          one cell, up to 6 elements; its length is known from the parent
//   strict inner  one cell, 6 children; dense: every child full except the
//                 last, every leaf below it full; indexed by base-6 digits
//   relaxed inner two cells: R = { 6 cumulative sizes, 48 bits each (36 B),
//                 child count, pointer to C }, C = { 6 children }; indexed by
//                 the size table, from a radix guess
//   root          one cell: size, tree, height, open tail (1..6 elements)
// In a parent, bit 0 of a child pointer marks a relaxed child.
//
// Modes:
//   rrb_lab check [seed] [steps]   randomised model check against std::vector (E = 2, 1, 0)
//   rrb_lab bench [E...]           RES lines, as list_ops_lab (default E = 2 1 0)
//   rrb_lab                        both (check first; any mismatch exits 2)

#include "lab_common.h"

#include <functional>
#include <random>
#include <string>

namespace {
using namespace lab;

constexpr unsigned M = 6;
unsigned gE = 2;                  // extra nodes allowed over the optimum per concat seam
constexpr unsigned kInv = 1;      // nodes with at least M - kInv slots are not redistributed
uint64_t gRedistributions = 0;
uint64_t gRepacks = 0;
bool gBStar = true;               // native inserts share a split with a neighbour (B* tree)           // rebalances that fell back to the full repack

[[noreturn]] void fail(const std::string& why) {
    std::fprintf(stderr, "rrb_lab: CHECK FAILED: %s\n", why.c_str());
    std::exit(2);
}

struct Node { uint64_t hdr[2]; const void* s[M]; };
static_assert(sizeof(Node) == 64, "Node must be one cell");
struct Relaxed { uint64_t hdr[2]; uint8_t cum[36]; uint8_t count; uint8_t pad[3]; const Node* kids; };
static_assert(sizeof(Relaxed) == 64, "Relaxed must be one cell");

uint64_t cap[24];                 // cap[h]: elements a full subtree of height h holds (6^(h+1))

inline bool isRel(uintptr_t r) { return r & 1u; }
inline const Node* nodeOf(uintptr_t r) { return reinterpret_cast<const Node*>(r); }
inline const Relaxed* relOf(uintptr_t r) { return reinterpret_cast<const Relaxed*>(r & ~uintptr_t(1)); }
inline uint64_t cumAt(const Relaxed* r, unsigned k) {
    uint64_t v; std::memcpy(&v, r->cum + 6 * k, 8); return v & 0xFFFFFFFFFFFFull;
}
inline void setCum(Relaxed* r, unsigned k, uint64_t v) { std::memcpy(r->cum + 6 * k, &v, 6); }

struct Sub { uintptr_t ref; uint64_t size; };
struct Kids { Sub v[16]; unsigned n = 0; void push(Sub s) { v[n++] = s; } };

struct List {                     // the root cell
    uint64_t hdr[2];
    uint64_t size, treeSize;
    uintptr_t tree;
    const Node* tail;
    uint32_t h, tailLen;
    uint64_t pad;
};
static_assert(sizeof(List) == 64, "List must be one cell");

struct Rrb {
    Arena& a;
    explicit Rrb(Arena& ar) : a(ar) {}

    // ------------------------------------------------------------ builders
    Sub mkLeaf(const Value* v, unsigned n) {
        auto* x = static_cast<Node*>(a.alloc(64));
        for (unsigned i = 0; i < n; ++i) x->s[i] = v[i];
        return {reinterpret_cast<uintptr_t>(x), n};
    }
    // An inner node of height h over kids (height h-1): strict when it can be.
    Sub mkInner(const Sub* k, unsigned n, unsigned h) {
        const uint64_t full = cap[h - 1];
        bool strict = true; uint64_t total = 0;
        for (unsigned i = 0; i < n; ++i) {
            total += k[i].size;
            if (isRel(k[i].ref)) strict = false;
            if (i + 1 < n && k[i].size != full) strict = false;
            if (h == 1 && k[i].size != M) strict = false;          // leaves under a strict node are full
        }
        auto* c = static_cast<Node*>(a.alloc(64));
        for (unsigned i = 0; i < n; ++i) c->s[i] = reinterpret_cast<const void*>(k[i].ref);
        if (strict) return {reinterpret_cast<uintptr_t>(c), total};
        auto* r = static_cast<Relaxed*>(a.alloc(64));
        uint64_t acc = 0;
        for (unsigned i = 0; i < n; ++i) { acc += k[i].size; setCum(r, i, acc); }
        r->count = static_cast<uint8_t>(n); r->kids = c;
        return {reinterpret_cast<uintptr_t>(r) | 1u, total};
    }
    Sub path(unsigned h, Sub leaf) { for (unsigned l = 1; l <= h; ++l) leaf = mkInner(&leaf, 1, l); return leaf; }
    List* root(const List* from) {
        auto* r = static_cast<List*>(a.alloc(64));
        if (from) *r = *from;
        return r;
    }

    // ------------------------------------------------------------ reading
    static Kids children(Sub s, unsigned h) {
        Kids k;
        if (isRel(s.ref)) {
            const Relaxed* r = relOf(s.ref); uint64_t prev = 0;
            for (unsigned i = 0; i < r->count; ++i) {
                uint64_t c = cumAt(r, i);
                k.push({reinterpret_cast<uintptr_t>(r->kids->s[i]), c - prev}); prev = c;
            }
        } else {
            const Node* n = nodeOf(s.ref); const uint64_t c = cap[h - 1];
            uint64_t rem = s.size;
            for (unsigned i = 0; rem > 0; ++i) {
                uint64_t take = std::min(c, rem);
                k.push({reinterpret_cast<uintptr_t>(n->s[i]), take}); rem -= take;
            }
        }
        return k;
    }
    static unsigned slotCount(Sub s, unsigned h) {
        if (h == 0) return unsigned(s.size);
        if (isRel(s.ref)) return relOf(s.ref)->count;
        return unsigned((s.size + cap[h - 1] - 1) / cap[h - 1]);
    }
    static Value getTree(uintptr_t ref, unsigned h, uint64_t i) {
        while (h > 0) {
            if (isRel(ref)) {
                const Relaxed* r = relOf(ref);
                uint64_t q = i; for (unsigned l = 0; l < h; ++l) q /= M;      // lower bound of the child
                unsigned k = unsigned(q);
                while (cumAt(r, k) <= i) ++k;
                if (k) i -= cumAt(r, k - 1);
                ref = reinterpret_cast<uintptr_t>(r->kids->s[k]); --h;
            } else {
                unsigned d[24]; uint64_t x = i;
                for (unsigned l = 0; l <= h; ++l) { d[l] = unsigned(x % M); x /= M; }
                const Node* n = nodeOf(ref);
                for (unsigned l = h; l > 0; --l) n = static_cast<const Node*>(n->s[d[l]]);
                return n->s[d[0]];
            }
        }
        return nodeOf(ref)->s[i];
    }
    static Value get(const List* l, uint64_t i) {
        if (i >= l->treeSize) return l->tail->s[i - l->treeSize];
        return getTree(l->tree, l->h, i);
    }
    // The leaf holding element i of a tree: the leaf and the index of its first element.
    static Sub locate(Sub s, unsigned h, uint64_t i, uint64_t& start) {
        start = 0;
        while (h > 0) {
            Kids k = children(s, h); unsigned j = 0;
            while (i >= k.v[j].size) { i -= k.v[j].size; start += k.v[j].size; ++j; }
            s = k.v[j]; --h;
        }
        return s;
    }
    template <class F> static void walk(Sub s, unsigned h, F& f) {
        if (h == 0) { const Node* n = nodeOf(s.ref); for (uint64_t i = 0; i < s.size; ++i) f(n->s[i]); return; }
        Kids k = children(s, h);
        for (unsigned i = 0; i < k.n; ++i) walk(k.v[i], h - 1, f);
    }
    template <class F> static void forEach(const List* l, F f) {
        if (l->treeSize) walk({l->tree, l->treeSize}, l->h, f);
        for (uint32_t i = 0; i < l->tailLen; ++i) f(l->tail->s[i]);
    }

    // ------------------------------------------------------------ tail operations
    // Hangs a full leaf at the right edge; ref 0 when the subtree has no room.
    Sub push(Sub s, unsigned h, Sub leaf) {
        if (h == 0) return {0, 0};
        if (!isRel(s.ref) && s.size == cap[h]) return {0, 0};
        Kids k = children(s, h);
        if (h > 1) {
            Sub r = push(k.v[k.n - 1], h - 1, leaf);
            if (r.ref) { k.v[k.n - 1] = r; return mkInner(k.v, k.n, h); }
        }
        if (k.n == M) return {0, 0};
        k.push(path(h - 1, leaf));
        return mkInner(k.v, k.n, h);
    }
    void pushLeaf(List* r, Sub leaf) {          // r->tree grows by `leaf` (full or not)
        if (r->treeSize == 0) { r->tree = leaf.ref; r->h = 0; r->treeSize = leaf.size; return; }
        if (leaf.size == M) {
            Sub t = push({r->tree, r->treeSize}, r->h, leaf);
            if (t.ref) { r->tree = t.ref; r->treeSize = t.size; return; }
            Sub two[2] = {{r->tree, r->treeSize}, path(r->h, leaf)};
            Sub t2 = mkInner(two, 2, r->h + 1);
            r->tree = t2.ref; r->treeSize = t2.size; r->h += 1; return;
        }
        auto [t, h] = concatTrees({r->tree, r->treeSize}, r->h, leaf, 0);
        r->tree = t.ref; r->treeSize = t.size; r->h = h;
    }
    const List* empty() { return root(nullptr); }
    const List* single(Value v) {
        List* r = root(nullptr); r->tail = nodeOf(mkLeaf(&v, 1).ref); r->tailLen = 1; r->size = 1; return r;
    }
    const List* appendLast(const List* l, Value v) {
        if (l->size == 0) return single(v);
        List* r = root(l); r->size = l->size + 1;
        if (l->tailLen < M) {
            Value b[M]; std::memcpy(b, l->tail->s, sizeof(Value) * l->tailLen); b[l->tailLen] = v;
            r->tail = nodeOf(mkLeaf(b, l->tailLen + 1).ref); r->tailLen = l->tailLen + 1; return r;
        }
        pushLeaf(r, {reinterpret_cast<uintptr_t>(l->tail), M});
        r->tail = nodeOf(mkLeaf(&v, 1).ref); r->tailLen = 1;
        return r;
    }
    // The tree without its last leaf, and that leaf.
    std::pair<Sub, Sub> popLast(Sub s, unsigned h) {
        if (h == 0) return {{0, 0}, s};
        Kids k = children(s, h);
        auto [rest, leaf] = popLast(k.v[k.n - 1], h - 1);
        if (rest.size == 0) --k.n; else k.v[k.n - 1] = rest;
        if (k.n == 0) return {{0, 0}, leaf};
        return {mkInner(k.v, k.n, h), leaf};
    }
    static void shrink(List* r) {
        while (r->treeSize && r->h > 0) {
            Kids k = children({r->tree, r->treeSize}, r->h);
            if (k.n != 1) break;
            r->tree = k.v[0].ref; r->h -= 1;
        }
        if (r->treeSize == 0) { r->tree = 0; r->h = 0; }
    }
    void treeToTail(List* r) {                  // moves the tree's last leaf into the (empty) tail
        auto [rest, leaf] = popLast({r->tree, r->treeSize}, r->h);
        r->tree = rest.ref; r->treeSize = rest.size;
        r->tail = nodeOf(leaf.ref); r->tailLen = uint32_t(leaf.size);
        shrink(r);
    }
    const List* removeLast(const List* l) {
        if (l->size <= 1) return empty();
        List* r = root(l); r->size = l->size - 1;
        if (l->tailLen > 1) { r->tail = nodeOf(mkLeaf(l->tail->s, l->tailLen - 1).ref); r->tailLen = l->tailLen - 1; return r; }
        treeToTail(r);
        return r;
    }
    Sub setIn(Sub s, unsigned h, uint64_t i, Value v) {
        if (h == 0) { auto* n = static_cast<Node*>(a.alloc(64)); *n = *nodeOf(s.ref); n->s[i] = v; return {reinterpret_cast<uintptr_t>(n), s.size}; }
        if (isRel(s.ref)) {
            const Relaxed* r = relOf(s.ref);
            unsigned k = 0; while (cumAt(r, k) <= i) ++k;
            uint64_t prev = k ? cumAt(r, k - 1) : 0;
            auto* c = static_cast<Node*>(a.alloc(64)); *c = *r->kids;
            c->s[k] = reinterpret_cast<const void*>(setIn({reinterpret_cast<uintptr_t>(r->kids->s[k]), cumAt(r, k) - prev}, h - 1, i - prev, v).ref);
            auto* nr = static_cast<Relaxed*>(a.alloc(64)); *nr = *r; nr->kids = c;
            return {reinterpret_cast<uintptr_t>(nr) | 1u, s.size};
        }
        const uint64_t c = cap[h - 1]; const unsigned k = unsigned(i / c);
        auto* n = static_cast<Node*>(a.alloc(64)); *n = *nodeOf(s.ref);
        n->s[k] = reinterpret_cast<const void*>(setIn({reinterpret_cast<uintptr_t>(nodeOf(s.ref)->s[k]), std::min(c, s.size - k * c)}, h - 1, i - k * c, v).ref);
        return {reinterpret_cast<uintptr_t>(n), s.size};
    }
    const List* setAt(const List* l, uint64_t i, Value v) {
        List* r = root(l);
        if (i >= l->treeSize) {
            auto* t = static_cast<Node*>(a.alloc(64)); *t = *l->tail; t->s[i - l->treeSize] = v; r->tail = t; return r;
        }
        r->tree = setIn({l->tree, l->treeSize}, l->h, i, v).ref;
        return r;
    }

    // ------------------------------------------------------------ concatenation
    // The plan of L'orange (2014): while the seam holds more than opt + E
    // nodes, the first node with spare room is poured into the ones after it.
    Kids rebalance(const Kids& all, unsigned lvl) {
        unsigned plan[16]; unsigned n = all.n; uint64_t S = 0;
        for (unsigned i = 0; i < n; ++i) { plan[i] = slotCount(all.v[i], lvl); S += plan[i]; }
        const unsigned opt = unsigned((S + M - 1) / M);
        if (n <= opt + gE) return all;
        ++gRedistributions;
        while (n > opt + gE) {
            unsigned i = 0;
            while (i < n && plan[i] >= M - kInv) ++i;
            unsigned r = i < n ? plan[i] : 0;
            unsigned room = 0; for (unsigned j = i + 1; j < n; ++j) room += M - plan[j];
            if (i >= n || room < r) {
                // The nodes after the first one with room cannot absorb it:
                // keep the full prefix and repack the rest into full nodes,
                // which reaches the optimum (ceil(S / M) nodes) exactly.
                unsigned f = 0; uint64_t rest = S;
                while (f < n && plan[f] == M) { rest -= M; ++f; }
                n = f;
                while (rest > 0) { unsigned t = unsigned(std::min<uint64_t>(rest, M)); plan[n++] = t; rest -= t; }
                ++gRepacks;
                break;
            }
            for (unsigned j = i + 1; r > 0; ++j) { unsigned t = std::min(r, M - plan[j]); plan[j] += t; r -= t; }
            for (unsigned j = i; j + 1 < n; ++j) plan[j] = plan[j + 1];
            --n;
        }
        return cut(all, lvl, plan, n);
    }
    // Streams the slots of every node of `all` (height lvl) and cuts them into
    // n nodes of plan[k] slots; a cut that is exactly one original node reuses it.
    Kids cut(const Kids& all, unsigned lvl, const unsigned* plan, unsigned n) {
        Kids out;
        if (lvl == 0) {
            Value items[16 * M]; unsigned origStart[16], at = 0;
            for (unsigned i = 0; i < all.n; ++i) {
                origStart[i] = at;
                const Node* nd = nodeOf(all.v[i].ref);
                for (uint64_t e = 0; e < all.v[i].size; ++e) items[at++] = nd->s[e];
            }
            unsigned pos = 0;
            for (unsigned k = 0; k < n; ++k) {
                Sub reuse{0, 0};
                for (unsigned i = 0; i < all.n; ++i) if (origStart[i] == pos && all.v[i].size == plan[k]) reuse = all.v[i];
                out.push(reuse.ref ? reuse : mkLeaf(items + pos, plan[k]));
                pos += plan[k];
            }
        } else {
            Sub items[16 * M]; unsigned origStart[16], origLen[16], at = 0;
            for (unsigned i = 0; i < all.n; ++i) {
                origStart[i] = at;
                Kids k = children(all.v[i], lvl);
                origLen[i] = k.n;
                for (unsigned e = 0; e < k.n; ++e) items[at++] = k.v[e];
            }
            unsigned pos = 0;
            for (unsigned k = 0; k < n; ++k) {
                Sub reuse{0, 0};
                for (unsigned i = 0; i < all.n; ++i) if (origStart[i] == pos && origLen[i] == plan[k]) reuse = all.v[i];
                out.push(reuse.ref ? reuse : mkInner(items + pos, plan[k], lvl));
                pos += plan[k];
            }
        }
        return out;
    }
    Kids pack(const Kids& all, unsigned h) {      // nodes of height h-1 into 1 or 2 nodes of height h
        Kids out;
        if (all.n <= M) { out.push(mkInner(all.v, all.n, h)); return out; }
        out.push(mkInner(all.v, M, h));
        out.push(mkInner(all.v + M, all.n - M, h));
        return out;
    }
    // left ++ right as one or two nodes of height max(hl, hr).
    Kids merge(Sub L, unsigned hl, Sub R, unsigned hr) {
        if (hl > hr) {
            Kids kl = children(L, hl);
            Kids mid = merge(kl.v[kl.n - 1], hl - 1, R, hr);
            Kids all; for (unsigned i = 0; i + 1 < kl.n; ++i) all.push(kl.v[i]);
            for (unsigned i = 0; i < mid.n; ++i) all.push(mid.v[i]);
            return pack(rebalance(all, hl - 1), hl);
        }
        if (hl < hr) {
            Kids kr = children(R, hr);
            Kids mid = merge(L, hl, kr.v[0], hr - 1);
            Kids all; for (unsigned i = 0; i < mid.n; ++i) all.push(mid.v[i]);
            for (unsigned i = 1; i < kr.n; ++i) all.push(kr.v[i]);
            return pack(rebalance(all, hr - 1), hr);
        }
        Kids out;
        if (hl == 0) {
            if (L.size + R.size <= M) {
                Value b[M]; std::memcpy(b, nodeOf(L.ref)->s, sizeof(Value) * L.size);
                std::memcpy(b + L.size, nodeOf(R.ref)->s, sizeof(Value) * R.size);
                out.push(mkLeaf(b, unsigned(L.size + R.size)));
            } else { out.push(L); out.push(R); }
            return out;
        }
        Kids kl = children(L, hl), kr = children(R, hr);
        Kids mid = merge(kl.v[kl.n - 1], hl - 1, kr.v[0], hr - 1);
        Kids all; for (unsigned i = 0; i + 1 < kl.n; ++i) all.push(kl.v[i]);
        for (unsigned i = 0; i < mid.n; ++i) all.push(mid.v[i]);
        for (unsigned i = 1; i < kr.n; ++i) all.push(kr.v[i]);
        return pack(rebalance(all, hl - 1), hl);
    }
    std::pair<Sub, unsigned> concatTrees(Sub L, unsigned hl, Sub R, unsigned hr) {
        Kids v = merge(L, hl, R, hr);
        unsigned h = std::max(hl, hr);
        if (v.n == 1) return {v.v[0], h};
        return {mkInner(v.v, v.n, h + 1), h + 1};
    }
    const List* concat(const List* A, const List* B) {
        if (A->size == 0) return B;
        if (B->size == 0) return A;
        List* r = root(A); r->size = A->size + B->size;
        if (B->treeSize == 0 && A->tailLen + B->tailLen <= M) {
            Value b[M]; std::memcpy(b, A->tail->s, sizeof(Value) * A->tailLen);
            std::memcpy(b + A->tailLen, B->tail->s, sizeof(Value) * B->tailLen);
            r->tail = nodeOf(mkLeaf(b, A->tailLen + B->tailLen).ref); r->tailLen = A->tailLen + B->tailLen;
            return r;
        }
        pushLeaf(r, {reinterpret_cast<uintptr_t>(A->tail), A->tailLen});
        if (B->treeSize) {
            auto [t, h] = concatTrees({r->tree, r->treeSize}, r->h, {B->tree, B->treeSize}, B->h);
            r->tree = t.ref; r->treeSize = t.size; r->h = h;
        }
        r->tail = B->tail; r->tailLen = B->tailLen;
        shrink(r);
        return r;
    }

    // ------------------------------------------------------------ slicing
    Sub takeTree(Sub s, unsigned h, uint64_t k) {    // first k elements; k on a leaf boundary
        Kids kd = children(s, h); uint64_t start = 0; unsigned j = 0;
        while (start + kd.v[j].size < k) { start += kd.v[j].size; ++j; }
        kd.n = j + 1;
        if (k - start < kd.v[j].size) kd.v[j] = takeTree(kd.v[j], h - 1, k - start);
        return mkInner(kd.v, kd.n, h);
    }
    Sub dropTree(Sub s, unsigned h, uint64_t k) {    // without the first k elements
        if (h == 0) return mkLeaf(nodeOf(s.ref)->s + k, unsigned(s.size - k));
        Kids kd = children(s, h); uint64_t start = 0; unsigned j = 0;
        while (start + kd.v[j].size <= k) { start += kd.v[j].size; ++j; }
        Kids out;
        out.push(k > start ? dropTree(kd.v[j], h - 1, k - start) : kd.v[j]);
        for (unsigned i = j + 1; i < kd.n; ++i) out.push(kd.v[i]);
        return mkInner(out.v, out.n, h);
    }
    const List* take(const List* l, uint64_t k) {    // first k elements
        if (k >= l->size) return l;
        if (k == 0) return empty();
        List* r = root(l); r->size = k;
        if (k > l->treeSize) { r->tail = nodeOf(mkLeaf(l->tail->s, unsigned(k - l->treeSize)).ref); r->tailLen = uint32_t(k - l->treeSize); return r; }
        if (k == l->treeSize) { treeToTail(r); return r; }
        uint64_t leafStart; Sub leaf = locate({l->tree, l->treeSize}, l->h, k - 1, leafStart);
        r->tail = nodeOf(k - leafStart == leaf.size ? leaf.ref : mkLeaf(nodeOf(leaf.ref)->s, unsigned(k - leafStart)).ref);
        r->tailLen = uint32_t(k - leafStart);
        if (leafStart == 0) { r->tree = 0; r->treeSize = 0; r->h = 0; return r; }
        Sub t = takeTree({l->tree, l->treeSize}, l->h, leafStart);
        r->tree = t.ref; r->treeSize = t.size;
        shrink(r);
        return r;
    }
    const List* drop(const List* l, uint64_t k) {    // without the first k elements
        if (k == 0) return l;
        if (k >= l->size) return empty();
        List* r = root(l); r->size = l->size - k;
        if (k >= l->treeSize) {
            r->tree = 0; r->treeSize = 0; r->h = 0;
            r->tail = nodeOf(mkLeaf(l->tail->s + (k - l->treeSize), unsigned(l->size - k)).ref); r->tailLen = uint32_t(l->size - k);
            return r;
        }
        Sub t = dropTree({l->tree, l->treeSize}, l->h, k);
        r->tree = t.ref; r->treeSize = t.size;
        shrink(r);
        return r;
    }
    const List* slice(const List* l, uint64_t from, uint64_t to) { return drop(take(l, to), from); }
    const List* insertAt(const List* l, uint64_t i, Value v) {
        if (i >= l->size) return appendLast(l, v);
        if (i == 0) return concat(single(v), l);
        return concat(appendLast(take(l, i), v), drop(l, i));
    }
    const List* removeAt(const List* l, uint64_t i) {
        if (i + 1 == l->size) return removeLast(l);
        if (i == 0) return drop(l, 1);
        return concat(take(l, i), drop(l, i + 1));
    }
    const List* removeSlice(const List* l, uint64_t from, uint64_t to) { return concat(take(l, from), drop(l, to)); }

    // ------------------------------------------------------------ native insertion and removal
    // B-tree style on the size tables: one path is copied; a full node splits
    // in two and the split climbs only as far as it has to. The split point
    // leans away from the insertion at either end, so runs of prepends (or of
    // inserts at one place) leave full nodes behind them.
    Kids splitInto(const Sub* all, unsigned n, unsigned h, unsigned at) {   // n == M + 1; `at` = new slot
        Kids out; unsigned l = at == 0 ? 1 : at == n - 1 ? n - 1 : (n + 1) / 2;
        if (h == 0) return out;                                         // not used for leaves
        out.push(mkInner(all, l, h)); out.push(mkInner(all + l, n - l, h));
        return out;
    }
    Kids insTree(Sub s, unsigned h, uint64_t i, Value v) {
        Kids out;
        if (h == 0) {
            const Node* nd = nodeOf(s.ref); const unsigned len = unsigned(s.size);
            Value b[M + 1];
            for (unsigned j = 0, o = 0; j <= len; ++j) b[j] = (j == i) ? v : nd->s[o++];
            if (len < M) { out.push(mkLeaf(b, len + 1)); return out; }
            const unsigned l = i == 0 ? 1 : i == len ? len : (len + 2) / 2;
            out.push(mkLeaf(b, l)); out.push(mkLeaf(b + l, len + 1 - l));
            return out;
        }
        Kids k = children(s, h); unsigned j = 0; uint64_t start = 0;
        while (j + 1 < k.n && i >= start + k.v[j].size) { start += k.v[j].size; ++j; }
        Kids r = insTree(k.v[j], h - 1, i - start, v);
        if (r.n == 2 && gBStar) {
            // B*: a neighbour with room takes part of the split, three nodes
            // become two and this node gains no child.
            int best = -1; unsigned bestSlots = 2 * M + 1;
            for (int nb : {int(j) + 1, int(j) - 1}) {
                if (nb < 0 || nb >= int(k.n)) continue;
                unsigned sl = slotCount(k.v[nb], h - 1);
                if (sl < bestSlots) { bestSlots = sl; best = nb; }
            }
            if (best >= 0) {
                Kids three;
                if (best < int(j)) { three.push(k.v[best]); three.push(r.v[0]); three.push(r.v[1]); }
                else { three.push(r.v[0]); three.push(r.v[1]); three.push(k.v[best]); }
                const unsigned total = slotCount(three.v[0], h - 1) + slotCount(three.v[1], h - 1) + slotCount(three.v[2], h - 1);
                if (total <= 2 * M) {
                    const unsigned plan[2] = {(total + 1) / 2, total / 2};
                    Kids two = cut(three, h - 1, plan, 2);
                    const unsigned first = unsigned(std::min<int>(best, int(j)));
                    k.v[first] = two.v[0]; k.v[first + 1] = two.v[1];
                    out.push(mkInner(k.v, k.n, h));
                    return out;
                }
            }
        }
        Kids all;
        for (unsigned q = 0; q < j; ++q) all.push(k.v[q]);
        for (unsigned q = 0; q < r.n; ++q) all.push(r.v[q]);
        for (unsigned q = j + 1; q < k.n; ++q) all.push(k.v[q]);
        if (all.n <= M) { out.push(mkInner(all.v, all.n, h)); return out; }
        // the new slot is the smaller half of the split child: j (prepend side) or j + 1
        const unsigned at = (r.v[0].size <= r.v[1].size) ? j : j + 1;
        return splitInto(all.v, all.n, h, at);
    }
    const List* insertNative(const List* l, uint64_t i, Value v) {
        if (i >= l->size) return appendLast(l, v);
        List* r = root(l); r->size = l->size + 1;
        if (i >= l->treeSize) {                     // inside the tail
            const unsigned ti = unsigned(i - l->treeSize), len = l->tailLen;
            Value b[M + 1];
            for (unsigned j = 0, o = 0; j <= len; ++j) b[j] = (j == ti) ? v : l->tail->s[o++];
            if (len < M) { r->tail = nodeOf(mkLeaf(b, len + 1).ref); r->tailLen = len + 1; return r; }
            pushLeaf(r, mkLeaf(b, M));
            r->tail = nodeOf(mkLeaf(b + M, 1).ref); r->tailLen = 1;
            return r;
        }
        Kids t = insTree({l->tree, l->treeSize}, l->h, i, v);
        if (t.n == 1) { r->tree = t.v[0].ref; r->treeSize = t.v[0].size; return r; }
        Sub top = mkInner(t.v, 2, l->h + 1);
        r->tree = top.ref; r->treeSize = top.size; r->h = l->h + 1;
        return r;
    }
    Sub mergeNodes(Sub a, Sub b, unsigned h) {      // two neighbours that fit in one node
        if (h == 0) {
            Value buf[M]; std::memcpy(buf, nodeOf(a.ref)->s, sizeof(Value) * a.size);
            std::memcpy(buf + a.size, nodeOf(b.ref)->s, sizeof(Value) * b.size);
            return mkLeaf(buf, unsigned(a.size + b.size));
        }
        Kids ka = children(a, h), kb = children(b, h);
        for (unsigned q = 0; q < kb.n; ++q) ka.push(kb.v[q]);
        return mkInner(ka.v, ka.n, h);
    }
    Sub remTree(Sub s, unsigned h, uint64_t i) {    // ref 0 when the subtree empties
        if (h == 0) {
            if (s.size == 1) return {0, 0};
            Value b[M]; const Node* nd = nodeOf(s.ref);
            for (unsigned j = 0, o = 0; j < s.size; ++j) if (j != i) b[o++] = nd->s[j];
            return mkLeaf(b, unsigned(s.size - 1));
        }
        Kids k = children(s, h); unsigned j = 0; uint64_t start = 0;
        while (i >= start + k.v[j].size) { start += k.v[j].size; ++j; }
        Sub r = remTree(k.v[j], h - 1, i - start);
        if (!r.ref) {
            for (unsigned q = j; q + 1 < k.n; ++q) k.v[q] = k.v[q + 1];
            --k.n;
        } else {
            k.v[j] = r;
            // Underflow: a child at half capacity or less merges with a neighbour when they fit.
            if (slotCount(r, h - 1) <= M / 2) {
                for (int nb : {int(j) - 1, int(j) + 1}) {
                    if (nb < 0 || nb >= int(k.n)) continue;
                    const unsigned a = std::min<unsigned>(j, unsigned(nb)), b = a + 1;
                    if (slotCount(k.v[a], h - 1) + slotCount(k.v[b], h - 1) > M) continue;
                    k.v[a] = mergeNodes(k.v[a], k.v[b], h - 1);
                    for (unsigned q = b; q + 1 < k.n; ++q) k.v[q] = k.v[q + 1];
                    --k.n;
                    break;
                }
            }
        }
        if (k.n == 0) return {0, 0};
        return mkInner(k.v, k.n, h);
    }
    const List* removeNative(const List* l, uint64_t i) {
        if (i + 1 >= l->size) return removeLast(l);
        List* r = root(l); r->size = l->size - 1;
        if (i >= l->treeSize) {
            Value b[M]; const unsigned ti = unsigned(i - l->treeSize);
            for (unsigned j = 0, o = 0; j < l->tailLen; ++j) if (j != ti) b[o++] = l->tail->s[j];
            r->tail = nodeOf(mkLeaf(b, l->tailLen - 1).ref); r->tailLen = l->tailLen - 1;
            return r;
        }
        Sub t = remTree({l->tree, l->treeSize}, l->h, i);
        r->tree = t.ref; r->treeSize = t.size;
        shrink(r);
        return r;
    }

    // ------------------------------------------------------------ structure check
    // Returns the subtree's size; records the worst extra search steps.
    unsigned maxExtra = 0;
    uint64_t checkTree(Sub s, unsigned h, const std::string& at) {
        if (h == 0) {
            if (s.size < 1 || s.size > M) fail(at + ": leaf size " + std::to_string(s.size));
            return s.size;
        }
        if (isRel(s.ref)) {
            const Relaxed* r = relOf(s.ref);
            if (r->count < 1 || r->count > M) fail(at + ": relaxed count");
            uint64_t prev = 0, total = 0;
            for (unsigned i = 0; i < r->count; ++i) {
                uint64_t c = cumAt(r, i);
                if (c <= prev) fail(at + ": cum not increasing");
                if (c - prev > cap[h - 1]) fail(at + ": child larger than capacity");
                total += checkTree({reinterpret_cast<uintptr_t>(r->kids->s[i]), c - prev}, h - 1, at + "/" + std::to_string(i));
                prev = c;
            }
            if (total != s.size || prev != s.size) fail(at + ": relaxed size mismatch");
            for (unsigned i = r->count; i < M; ++i) if (r->kids->s[i]) fail(at + ": slot past count");
            // search steps: how far the radix guess is from the child, worst case over the node's range
            for (uint64_t probe : {uint64_t(0), s.size / 2, s.size - 1}) {
                uint64_t q = probe; for (unsigned l = 0; l < h; ++l) q /= M;
                unsigned k = unsigned(q), steps = 0;
                while (cumAt(r, k) <= probe) { ++k; ++steps; }
                maxExtra = std::max(maxExtra, steps);
            }
            return total;
        }
        if (s.size > cap[h]) fail(at + ": strict node over capacity");
        if (s.size % M) fail(at + ": strict node with a partial leaf");
        Kids k = children(s, h);
        const Node* n = nodeOf(s.ref);
        for (unsigned i = 0; i < M; ++i) if ((i < k.n) != (n->s[i] != nullptr)) fail(at + ": strict slots");
        uint64_t total = 0;
        for (unsigned i = 0; i < k.n; ++i) {
            if (isRel(k.v[i].ref)) fail(at + ": relaxed child under a strict node");
            total += checkTree(k.v[i], h - 1, at + "/" + std::to_string(i));
        }
        return total;
    }
    void check(const List* l, const std::string& what) {
        if (l->size != l->treeSize + l->tailLen) fail(what + ": size != tree + tail");
        if (l->size && (l->tailLen < 1 || l->tailLen > M)) fail(what + ": tail length");
        if (l->treeSize) {
            if (l->h > 0 && children({l->tree, l->treeSize}, l->h).n < 2) fail(what + ": root with one child");
            if (checkTree({l->tree, l->treeSize}, l->h, what) != l->treeSize) fail(what + ": tree size");
        } else if (l->tree) fail(what + ": tree without size");
    }
};

inline Value val(uint64_t i) { return reinterpret_cast<Value>((i << 6) | 0x1); }

// ---------------------------------------------------------------- model check
void modelCheck(uint64_t seed, unsigned steps) {
    Arena arena(size_t(2) << 30);
    Rrb t(arena);
    std::mt19937_64 rng(seed);
    using V = std::vector<Value>;
    auto expect = [&](const List* l, const V& m, const std::string& what) {
        t.check(l, what);
        if (l->size != m.size()) fail(what + ": size " + std::to_string(l->size) + " vs " + std::to_string(m.size()));
        for (uint64_t i = 0; i < m.size(); ++i)
            if (Rrb::get(l, i) != m[i]) fail(what + ": element " + std::to_string(i));
        uint64_t i = 0; bool bad = false;
        Rrb::forEach(l, [&](Value v) { if (i >= m.size() || v != m[i]) bad = true; ++i; });
        if (bad || i != m.size()) fail(what + ": iteration");
    };
    uint64_t next = 1;
    auto fresh = [&]() { return (rng() % 13 == 0) ? Value(nullptr) : val(next++); };
    auto build = [&](uint64_t n, V& m) {           // appends, or concatenated pieces
        const List* l = t.empty(); m.clear();
        if (rng() % 2) { for (uint64_t i = 0; i < n; ++i) { Value v = fresh(); l = t.appendLast(l, v); m.push_back(v); } return l; }
        while (m.size() < n) {
            uint64_t k = std::min<uint64_t>(n - m.size(), 1 + rng() % 40);
            const List* p = t.empty();
            for (uint64_t i = 0; i < k; ++i) { Value v = fresh(); p = t.appendLast(p, v); m.push_back(v); }
            l = t.concat(l, p);
        }
        return l;
    };
    std::vector<std::pair<const List*, V>> kept;
    for (int round = 0; round < 8; ++round) {
        V m; const List* l = build(rng() % 3000, m);
        expect(l, m, "build");
        for (unsigned s = 0; s < steps; ++s) {
            const uint64_t n = m.size();
            const unsigned op = unsigned(rng() % 14);
            const std::string what = "seed " + std::to_string(seed) + " round " + std::to_string(round) + " step " + std::to_string(s) + " op " + std::to_string(op) + " n " + std::to_string(n);
            Value v = fresh();
            switch (op) {
                case 0: case 1: l = t.appendLast(l, v); m.push_back(v); break;
                case 2: if (n) { l = t.removeLast(l); m.pop_back(); } break;
                case 3: if (n) { uint64_t i = rng() % n; l = t.setAt(l, i, v); m[i] = v; } break;
                case 4: { uint64_t i = rng() % (n + 1);
                          l = rng() % 2 ? t.insertAt(l, i, v) : t.insertNative(l, i, v); m.insert(m.begin() + i, v); } break;
                case 5: if (n) { uint64_t i = rng() % n;
                          l = rng() % 2 ? t.removeAt(l, i) : t.removeNative(l, i); m.erase(m.begin() + i); } break;
                case 6: l = rng() % 2 ? t.concat(t.single(v), l) : t.insertNative(l, 0, v); m.insert(m.begin(), v); break;
                case 7: if (n) { l = t.drop(l, 1); m.erase(m.begin()); } break;
                case 8: { V o; const List* p = build(rng() % (rng() % 4 ? 50 : 2000), o);
                          if (rng() % 2) { l = t.concat(l, p); m.insert(m.end(), o.begin(), o.end()); }
                          else { l = t.concat(p, l); m.insert(m.begin(), o.begin(), o.end()); } } break;
                case 9: { uint64_t a = rng() % (n + 1), b = rng() % (n + 1); if (a > b) std::swap(a, b);
                          const List* s2 = t.slice(l, a, b); expect(s2, V(m.begin() + a, m.begin() + b), what + " slice"); } break;
                case 10: { uint64_t a = rng() % (n + 1), b = rng() % (n + 1); if (a > b) std::swap(a, b);
                           l = t.removeSlice(l, a, b); m.erase(m.begin() + a, m.begin() + b); } break;
                case 12: { for (int q = 0; q < 30; ++q) { Value w = fresh(); uint64_t i = rng() % (m.size() + 1);
                           l = t.insertNative(l, i, w); m.insert(m.begin() + i, w); } } break;
                case 13: { for (int q = 0; q < 30 && !m.empty(); ++q) { uint64_t i = rng() % m.size();
                           l = t.removeNative(l, i); m.erase(m.begin() + i); } } break;
                case 11: { uint64_t k = rng() % (n + 1);
                           if (rng() % 2) { l = t.take(l, k); m.resize(k); } else { l = t.drop(l, k); m.erase(m.begin(), m.begin() + k); } } break;
            }
            if (m.size() > 20000) { l = t.take(l, 3000); m.resize(3000); }
            if (s % 7 == 0 || m.size() < 50) expect(l, m, what); else t.check(l, what);
            if (s % 97 == 0) kept.emplace_back(l, m);
        }
    }
    for (size_t k = 0; k < kept.size(); ++k) expect(kept[k].first, kept[k].second, "kept version " + std::to_string(k));
    std::printf("CHECK bstar=%d seed=%llu E=%u ok: %zu kept versions intact, worst extra search steps %u, redistributions %llu (full repacks %llu)\n",
                int(gBStar), (unsigned long long) seed, gE, kept.size(), t.maxExtra, (unsigned long long) gRedistributions, (unsigned long long) gRepacks);
    gRedistributions = gRepacks = 0;
}

// ---------------------------------------------------------------- benchmarks
template <class Body>
void measure(const char* op, const char* shape, uint64_t n, Arena& a, uint64_t calls, double per, Body body) {
    const size_t mark = a.used;
    std::vector<double> ns; double cells = 0;
    for (int b = 0; b <= 7; ++b) {
        a.used = mark; const size_t al0 = a.allocated;
        const double t0 = nowNs();
        for (uint64_t i = 0; i < calls; ++i) body(i);
        const double dt = (nowNs() - t0) / double(calls);
        if (b > 0) ns.push_back(dt);
        cells = double(a.allocated - al0) / 64.0 / double(calls);
    }
    a.used = mark;
    std::sort(ns.begin(), ns.end());
    const double p = per > 1 ? per : 1.0;
    std::printf("RES op=%s shape=%s E=%u N=%llu ns=%.1f min=%.1f max=%.1f cells=%.2f%s\n", op, shape, gE,
                (unsigned long long) n, ns[ns.size() / 2] / p, ns.front() / p, ns.back() / p, cells / p,
                per > 1 ? " per=element" : "");
    std::fflush(stdout);
}

void bench(const std::vector<unsigned>& Es) {
    Arena arena(size_t(3) << 30);
    Rrb t(arena);
    for (unsigned E : Es) {
        gE = E;
        for (uint64_t n : {1000ull, 100000ull}) {
            arena.reset();
            // strict: built by appends; relaxed: concatenated pieces of 1..40 elements
            const List* strict = t.empty();
            for (uint64_t i = 0; i < n; ++i) strict = t.appendLast(strict, val(i));
            std::mt19937_64 rng(42);
            const List* relaxed = t.empty(); uint64_t at = 0;
            while (at < n) {
                uint64_t k = std::min<uint64_t>(n - at, 1 + rng() % 40);
                const List* p = t.empty();
                for (uint64_t i = 0; i < k; ++i) p = t.appendLast(p, val(at + i));
                relaxed = t.concat(relaxed, p); at += k;
            }
            const List* half = t.take(strict, n / 2);
            t.check(strict, "strict"); t.check(relaxed, "relaxed");
            for (uint64_t i = 0; i < n; ++i)
                if (Rrb::get(strict, i) != val(i) || Rrb::get(relaxed, i) != val(i)) fail("bench base contents");
            std::printf("BASE E=%u N=%llu strict h=%u relaxed h=%u relaxed worst extra steps=%u\n", E,
                        (unsigned long long) n, strict->h, relaxed->h, t.maxExtra);
            const uint64_t K = 20000;
            const Value x = val(n + 7);
            auto idx = [n](uint64_t i) { return (i * 7919) % n; };
            for (int s = 0; s < 2; ++s) {
                const List* base = s ? relaxed : strict;
                const char* shape = s ? "relaxed" : "strict";
                measure("getAt_random", shape, n, arena, K, 1, [&](uint64_t i) { g_sink = Rrb::get(base, idx(i)); });
                measure("getAt_sequential_all", shape, n, arena, std::max<uint64_t>(1, 2000000 / n), double(n),
                        [&](uint64_t) { for (uint64_t j = 0; j < n; ++j) g_sink = Rrb::get(base, j); });
                measure("iterate_all", shape, n, arena, std::max<uint64_t>(1, 2000000 / n), double(n),
                        [&](uint64_t) { Rrb::forEach(base, [](Value v) { g_sink = v; }); });
                measure("setAt_random", shape, n, arena, K, 1, [&](uint64_t i) { g_sink = t.setAt(base, idx(i), x); });
                measure("appendLast_on_N", shape, n, arena, K, 1, [&](uint64_t) { g_sink = t.appendLast(base, x); });
                measure("removeLast", shape, n, arena, K, 1, [&](uint64_t) { g_sink = t.removeLast(base); });
                measure("appendFirst", shape, n, arena, K, 1, [&](uint64_t) { g_sink = t.concat(t.single(x), base); });
                measure("insertAt_middle", shape, n, arena, K, 1, [&](uint64_t) { g_sink = t.insertAt(base, n / 2, x); });
                measure("removeAt_middle", shape, n, arena, K, 1, [&](uint64_t) { g_sink = t.removeAt(base, n / 2); });
                measure("removeFirst", shape, n, arena, K, 1, [&](uint64_t) { g_sink = t.drop(base, 1); });
                measure("appendFirst_native", shape, n, arena, K, 1, [&](uint64_t) { g_sink = t.insertNative(base, 0, x); });
                measure("insertAt_middle_native", shape, n, arena, K, 1, [&](uint64_t) { g_sink = t.insertNative(base, n / 2, x); });
                measure("removeAt_middle_native", shape, n, arena, K, 1, [&](uint64_t) { g_sink = t.removeNative(base, n / 2); });
                measure("removeFirst_native", shape, n, arena, K, 1, [&](uint64_t) { g_sink = t.removeNative(base, 0); });
                measure("getSlice_middle_half", shape, n, arena, K, 1, [&](uint64_t) { g_sink = t.slice(base, n / 4, 3 * n / 4); });
                measure("extend_with_itself", shape, n, arena, K, 1, [&](uint64_t) { g_sink = t.concat(base, base); });
            }
            measure("extend_two_halves", "strict", n, arena, K, 1, [&](uint64_t) { g_sink = t.concat(half, half); });
            // Whole-list builds, per element: by appends, by prepends, by inserts in the middle.
            const uint64_t reps = std::max<uint64_t>(1, 200000 / n);
            measure("build_by_appendLast", "-", n, arena, reps, double(n), [&](uint64_t) {
                const List* l = t.empty(); for (uint64_t i = 0; i < n; ++i) l = t.appendLast(l, x); g_sink = l; });
            measure("build_by_appendFirst", "-", n, arena, reps, double(n), [&](uint64_t) {
                const List* l = t.empty(); for (uint64_t i = 0; i < n; ++i) l = t.concat(t.single(x), l); g_sink = l; });
            measure("build_by_insertAt_middle", "-", n, arena, reps, double(n), [&](uint64_t) {
                const List* l = t.empty(); for (uint64_t i = 0; i < n; ++i) l = t.insertAt(l, i / 2, x); g_sink = l; });
            measure("build_by_appendFirst_native", "-", n, arena, reps, double(n), [&](uint64_t) {
                const List* l = t.empty(); for (uint64_t i = 0; i < n; ++i) l = t.insertNative(l, 0, x); g_sink = l; });
            measure("build_by_insertAt_middle_native", "-", n, arena, reps, double(n), [&](uint64_t) {
                const List* l = t.empty(); for (uint64_t i = 0; i < n; ++i) l = t.insertNative(l, i / 2, x); g_sink = l; });
            {
                const List* pn = t.empty(); for (uint64_t i = 0; i < n; ++i) pn = t.insertNative(pn, 0, val(n - 1 - i));
                const List* mn = t.empty(); std::vector<Value> mv;
                for (uint64_t i = 0; i < n; ++i) { mn = t.insertNative(mn, i / 2, val(i)); mv.insert(mv.begin() + i / 2, val(i)); }
                // random removals down to half, natively
                const List* rn = strict; std::vector<Value> rv; for (uint64_t i = 0; i < n; ++i) rv.push_back(val(i));
                std::mt19937_64 rr(9);
                while (rv.size() > n / 2) { uint64_t i = rr() % rv.size(); rn = t.removeNative(rn, i); rv.erase(rv.begin() + i); }
                t.check(pn, "native prepend-built"); t.check(mn, "native insert-built"); t.check(rn, "native removed");
                for (uint64_t i = 0; i < n; ++i) if (Rrb::get(pn, i) != val(i) || Rrb::get(mn, i) != mv[i]) fail("native built contents");
                for (uint64_t i = 0; i < rv.size(); ++i) if (Rrb::get(rn, i) != rv[i]) fail("native removed contents");
                measure("getAt_random", "native_prepend_built", n, arena, K, 1, [&](uint64_t i) { g_sink = Rrb::get(pn, idx(i)); });
                measure("getAt_random", "native_insert_built", n, arena, K, 1, [&](uint64_t i) { g_sink = Rrb::get(mn, idx(i)); });
                const uint64_t half2 = rv.size();
                measure("getAt_random", "native_half_removed", n, arena, K, 1, [&](uint64_t i) { g_sink = Rrb::get(rn, (i * 7919) % half2); });
                std::printf("NATIVE E=%u N=%llu prepend h=%u insert h=%u half-removed h=%u (strict h=%u)\n", E, (unsigned long long) n,
                            pn->h, mn->h, rn->h, strict->h);
            }
            // Reads after a prepend-built and a middle-insert-built list.
            const List* pre = t.empty(); for (uint64_t i = 0; i < n; ++i) pre = t.concat(t.single(val(n - 1 - i)), pre);
            const List* mid = t.empty(); std::vector<Value> mm;
            for (uint64_t i = 0; i < n; ++i) { mid = t.insertAt(mid, i / 2, val(i)); mm.insert(mm.begin() + i / 2, val(i)); }
            t.check(pre, "prepend-built"); t.check(mid, "insert-built");
            for (uint64_t i = 0; i < n; ++i) if (Rrb::get(pre, i) != val(i) || Rrb::get(mid, i) != mm[i]) fail("built contents");
            measure("getAt_random", "prepend_built", n, arena, K, 1, [&](uint64_t i) { g_sink = Rrb::get(pre, idx(i)); });
            measure("getAt_random", "insert_built", n, arena, K, 1, [&](uint64_t i) { g_sink = Rrb::get(mid, idx(i)); });
            std::printf("BUILT E=%u N=%llu prepend h=%u insert h=%u worst extra steps=%u\n", E, (unsigned long long) n,
                        pre->h, mid->h, t.maxExtra);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    cap[0] = M; for (int i = 1; i < 24; ++i) cap[i] = cap[i - 1] * M;
    const std::string mode = argc > 1 ? argv[1] : "all";
    if (const char* b = std::getenv("RRB_BSTAR")) gBStar = std::string(b) != "0";
    if (mode == "check" || mode == "all") {
        const uint64_t seed = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1;
        const unsigned steps = argc > 3 ? unsigned(std::strtoul(argv[3], nullptr, 10)) : 1500;
        for (unsigned E : {2u, 1u, 0u}) { gE = E; modelCheck(seed, steps); }
    }
    std::vector<unsigned> Es = {2u, 1u, 0u};
    if (mode == "bench" && argc > 2) { Es.clear(); for (int i = 2; i < argc; ++i) Es.push_back(unsigned(std::strtoul(argv[i], nullptr, 10))); }
    if (mode == "bench" || mode == "all") bench(Es);
    return 0;
}
