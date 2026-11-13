// list_ops_lab — every ProtoList operation on the candidate design (radix trie,
// 6 values per 64 B cell, open tail), on the simulated cell heap of lab_common.h.
//
// Operations the trie does natively (path copy): setAt, removeLast, getLast,
// iteration. Operations it cannot do in O(log n) without relaxed (RRB) nodes —
// appendFirst, insertAt, removeAt, removeFirst, extend, getSlice, splitFirst —
// are measured two ways:
//   rebuild   : collect the elements and build a new trie in bulk (O(n), but
//               6 elements per cell instead of AVL's one)
//   to_avl    : convert the trie to today's AVL once (O(n)), after which the
//               AVL algorithms apply (O(log n), measured by list_ops_kernel)
// Output: RES op=<op> N=<n> ns=<ns per call> cells=<cells per call> [per=element]

#include "lab_common.h"

namespace {
using namespace lab;
using T6 = Trie<6, 6, 16>;
using Root = T6::Root;

inline Value val(uint64_t i) { return reinterpret_cast<Value>((i << 6) | 0x1); }

// ---------------------------------------------------------------- native trie operations
struct Ops {
    Arena& a; T6& t;

    const void* setIn(const void* node, unsigned level, uint64_t i, Value v) {
        if (level == 0) { void* n = t.copyLeaf(node); t.slots(n)[i % 6] = v; return n; }
        void* n = t.copyInner(node);
        unsigned d = T6::digit(i / 6, level);
        t.slots(n)[d] = setIn(t.slots(node)[d], level - 1, i, v);
        return n;
    }
    const Root* setAt(const Root* r, uint64_t i, Value v) {
        auto* nr = static_cast<Root*>(a.alloc(64)); *nr = *r;
        uint64_t to = t.tailOffset(r);
        if (i >= to) { void* tl = t.copyLeaf(r->tail); t.slots(tl)[i - to] = v; nr->tail = tl; }
        else nr->tree = setIn(r->tree, r->levels, i, v);
        return nr;
    }
    const void* leafOf(const Root* r, uint64_t li) const {
        const void* n = r->tree;
        for (unsigned l = r->levels; l > 0; --l) n = t.slots(n)[T6::digit(li, l)];
        return n;
    }
    // Path copy that removes leaf number li (the last one); nullptr when the node empties.
    const void* popLeaf(const void* node, unsigned level, uint64_t li) {
        unsigned d = T6::digit(li, level);
        const void* child = (level == 1) ? nullptr : popLeaf(t.slots(node)[d], level - 1, li);
        if (child == nullptr && d == 0) return nullptr;
        void* n = t.copyInner(node);
        t.slots(n)[d] = child;
        return n;
    }
    const Root* removeLast(const Root* r) {
        auto* nr = static_cast<Root*>(a.alloc(64)); *nr = *r; nr->count = r->count - 1;
        uint64_t tailLen = r->count - t.tailOffset(r);
        if (tailLen > 1) { void* tl = t.copyLeaf(r->tail); t.slots(tl)[tailLen - 1] = nullptr; nr->tail = tl; return nr; }
        if (r->count == 1) { nr->tail = nullptr; return nr; }
        uint64_t li = (r->count - 2) / 6;                    // the tree's last leaf becomes the tail
        nr->tail = leafOf(r, li);
        if (r->levels == 0) { nr->tree = nullptr; return nr; }
        const void* tree = popLeaf(r->tree, r->levels, li);
        unsigned levels = r->levels;
        while (levels > 0 && tree && t.slots(tree)[1] == nullptr) { tree = t.slots(tree)[0]; --levels; }
        nr->tree = tree; nr->levels = levels;
        return nr;
    }
    template <class Fn> void walk(const void* n, unsigned level, Fn& fn) const {
        if (!n) return;
        if (level == 0) { for (unsigned k = 0; k < 6; ++k) fn(t.slots(n)[k]); return; }
        for (unsigned k = 0; k < 6; ++k) walk(t.slots(n)[k], level - 1, fn);
    }
    // Every element in order: the leaves of the tree, then the tail.
    template <class Fn> void forEach(const Root* r, Fn fn) const {
        uint64_t treeElems = t.tailOffset(r); uint64_t seen = 0;
        auto f = [&](Value v) { if (seen++ < treeElems) fn(v); };
        walk(r->tree, r->levels, f);
        uint64_t tl = r->count - t.tailOffset(r);
        for (uint64_t k = 0; k < tl; ++k) fn(t.slots(r->tail)[k]);
    }
    // Bulk build: leaves filled directly, inner levels bottom-up.
    const Root* fromArray(const Value* v, uint64_t n) {
        auto* r = static_cast<Root*>(a.alloc(64)); r->count = n;
        if (n == 0) return r;
        uint64_t tailLen = (n - 1) % 6 + 1, inTree = n - tailLen;
        void* tl = t.newLeaf(); std::memcpy(t.slots(tl), v + inTree, tailLen * sizeof(Value)); r->tail = tl;
        if (inTree == 0) return r;
        std::vector<const void*> level;
        for (uint64_t i = 0; i < inTree; i += 6) { void* lf = t.newLeaf(); std::memcpy(t.slots(lf), v + i, 6 * sizeof(Value)); level.push_back(lf); }
        unsigned levels = 0;
        while (level.size() > 1) {
            std::vector<const void*> up;
            for (size_t i = 0; i < level.size(); i += 6) {
                void* in = t.newInner();
                for (size_t k = 0; k < 6 && i + k < level.size(); ++k) t.slots(in)[k] = level[i + k];
                up.push_back(in);
            }
            level.swap(up); ++levels;
        }
        r->tree = level[0]; r->levels = levels;
        return r;
    }
};


// ---------------------------------------------------------------- option: chunked AVL with tail
// Today's AVL join/split algebra, but each node points to a leaf of up to 6
// values, plus an open tail leaf in the root (the string design applied to
// lists). Structural operations stay O(log n) with the existing join/split.
struct CNode { uint64_t hdr[2]; const void* leaf; const CNode* left; const CNode* right; uint32_t height; uint32_t leafLen; uint64_t count; uint64_t pad; };
static_assert(sizeof(CNode) == 64, "CNode must be one line");
struct Chunked {
    struct CRoot { uint64_t hdr[2]; const CNode* tree; const void* tail; uint64_t count; uint32_t tailLen; uint32_t pad; uint64_t pad2[2]; };
    static_assert(sizeof(CRoot) == 64, "CRoot must be one line");
    Arena& a; T6& t;
    static uint32_t ht(const CNode* n) { return n ? n->height : 0; }
    static uint64_t cnt(const CNode* n) { return n ? n->count : 0; }
    const CNode* mk(const void* leaf, uint32_t len, const CNode* l, const CNode* r) {
        auto* n = static_cast<CNode*>(a.alloc(64)); n->leaf = leaf; n->leafLen = len; n->left = l; n->right = r;
        n->height = std::max(ht(l), ht(r)) + 1; n->count = cnt(l) + cnt(r) + len; return n; }
    const CNode* rotR(const CNode* y) { auto x = y->left; return mk(x->leaf, x->leafLen, x->left, mk(y->leaf, y->leafLen, x->right, y->right)); }
    const CNode* rotL(const CNode* x) { auto y = x->right; return mk(y->leaf, y->leafLen, mk(x->leaf, x->leafLen, x->left, y->left), y->right); }
    const CNode* bal(const CNode* n) {
        int b = int(ht(n->left)) - int(ht(n->right));
        if (b > 1) { if (ht(n->left->left) < ht(n->left->right)) n = mk(n->leaf, n->leafLen, rotL(n->left), n->right); return rotR(n); }
        if (b < -1) { if (ht(n->right->right) < ht(n->right->left)) n = mk(n->leaf, n->leafLen, n->left, rotR(n->right)); return rotL(n); }
        return n; }
    const CNode* appendLeaf(const CNode* n, const void* leaf) { if (!n) return mk(leaf, 6, nullptr, nullptr); return bal(mk(n->leaf, n->leafLen, n->left, appendLeaf(n->right, leaf))); }
    const CRoot* empty() { return static_cast<CRoot*>(a.alloc(64)); }
    const CRoot* append(const CRoot* r, Value v) {
        auto* nr = static_cast<CRoot*>(a.alloc(64)); *nr = *r; nr->count = r->count + 1;
        if (r->tail && r->tailLen == 6) { nr->tree = appendLeaf(r->tree, r->tail); void* tl = t.newLeaf(); t.slots(tl)[0] = v; nr->tail = tl; nr->tailLen = 1; }
        else { void* tl = r->tail ? t.copyLeaf(r->tail) : t.newLeaf(); t.slots(tl)[r->tailLen] = v; nr->tail = tl; nr->tailLen = r->tailLen + 1; }
        return nr; }
    Value get(const CRoot* r, uint64_t i) const {
        uint64_t tc = cnt(r->tree);
        if (i >= tc) return t.slots(r->tail)[i - tc];
        const CNode* n = r->tree;
        while (true) { uint64_t lc = cnt(n->left);
            if (i < lc) { n = n->left; continue; } i -= lc;
            if (i < n->leafLen) return t.slots(n->leaf)[i];
            i -= n->leafLen; n = n->right; } }
};

// ---------------------------------------------------------------- driver
template <class Body>
void measure(const char* op, uint64_t n, Arena& a, uint64_t calls, double per, Body body) {
    double best = 1e300; double cells = 0;
    for (int batch = 0; batch < 5; ++batch) {
        size_t before = a.allocated; size_t used = a.used;
        double t0 = nowNs();
        for (uint64_t i = 0; i < calls; ++i) body(i);
        double t = (nowNs() - t0) / double(calls);
        if (batch > 0) best = std::min(best, t);
        cells = double(a.allocated - before) / 64 / double(calls);
        a.used = used;                                  // drop the garbage of this batch
    }
    if (per > 1) std::printf("RES op=%s N=%llu ns=%.2f cells=%.3f per=element\n", op, (unsigned long long)n, best / per, cells / per);
    else std::printf("RES op=%s N=%llu ns=%.1f cells=%.2f\n", op, (unsigned long long)n, best, cells);
    std::fflush(stdout);
}

}  // namespace

int main() {
    Arena arena(size_t(3) << 30);
    for (uint64_t n : {1000ull, 100000ull}) {
        arena.reset();
        T6 t(arena); Ops ops{arena, t};
        const Root* base = t.empty();
        for (uint64_t i = 0; i < n; ++i) base = t.append(base, val(i));
        std::vector<Value> elems; ops.forEach(base, [&](Value v) { elems.push_back(v); });
        for (uint64_t i = 0; i < n; ++i) if (elems[i] != val(i) || t.get(base, i) != val(i)) { std::fprintf(stderr, "bad base\n"); return 1; }
        // checks of the native operations
        { auto r = ops.setAt(base, n / 2, val(7)); if (t.get(r, n / 2) != val(7) || t.get(base, n / 2) != val(n / 2)) { std::fprintf(stderr, "bad setAt\n"); return 1; } }
        { auto r = base; for (uint64_t k = 0; k < n - 1; ++k) { r = ops.removeLast(r);
              if (r->count != n - 1 - k || t.get(r, r->count - 1) != val(r->count - 1)) { std::fprintf(stderr, "bad removeLast at %llu\n", (unsigned long long)k); return 1; } } }
        { auto r = ops.fromArray(elems.data(), n); for (uint64_t i = 0; i < n; ++i) if (t.get(r, i) != val(i)) { std::fprintf(stderr, "bad fromArray\n"); return 1; } }

        const uint64_t K = 20000;
        auto idx = [n](uint64_t i) { return (i * 7919) % n; };
        Value x = val(n + 1);
        measure("getLast", n, arena, K, 1, [&](uint64_t) { g_sink = t.get(base, base->count - 1); });
        measure("getAt_random", n, arena, K, 1, [&](uint64_t i) { g_sink = t.get(base, idx(i)); });
        measure("setAt_random", n, arena, K, 1, [&](uint64_t i) { g_sink = ops.setAt(base, idx(i), x); });
        measure("appendLast_on_N", n, arena, K, 1, [&](uint64_t) { g_sink = t.append(base, x); });
        measure("removeLast", n, arena, K, 1, [&](uint64_t) { g_sink = ops.removeLast(base); });
        const uint64_t KB = std::max<uint64_t>(4, 400000 / n);
        measure("iterate_all_leafwalk", n, arena, KB, double(n), [&](uint64_t) { ops.forEach(base, [](Value v) { g_sink = v; }); });
        measure("getAt_sequential_all", n, arena, KB, double(n), [&](uint64_t) { for (uint64_t j = 0; j < n; ++j) g_sink = t.get(base, j); });
        measure("extend_small_8_by_append", n, arena, K / 4, 1, [&](uint64_t) { auto r = base; for (int k = 0; k < 8; ++k) r = t.append(r, x); g_sink = r; });
        std::vector<Value> buf(n + 1);
        auto collect = [&](const Root* r, Value* out) { uint64_t k = 0; ops.forEach(r, [&](Value v) { out[k++] = v; }); return k; };
        measure("rebuild_extend_two_halves", n, arena, KB, 1, [&](uint64_t) {
            uint64_t h = n / 2; for (uint64_t j = 0; j < h; ++j) buf[j] = buf[h + j] = elems[j]; g_sink = ops.fromArray(buf.data(), 2 * h); });
        measure("rebuild_getSlice_middle_half", n, arena, KB, 1, [&](uint64_t) { g_sink = ops.fromArray(elems.data() + n / 4, n / 2); });
        measure("rebuild_insertAt_middle", n, arena, KB, 1, [&](uint64_t) {
            uint64_t k = collect(base, buf.data()); std::memmove(buf.data() + n / 2 + 1, buf.data() + n / 2, (k - n / 2) * sizeof(Value));
            buf[n / 2] = x; g_sink = ops.fromArray(buf.data(), k + 1); });
        measure("rebuild_appendFirst", n, arena, KB, 1, [&](uint64_t) {
            buf[0] = x; uint64_t k = collect(base, buf.data() + 1); g_sink = ops.fromArray(buf.data(), k + 1); });
        {   // option: chunked AVL with tail — build cost, random read
            Chunked ch{arena, t};
            const uint64_t reps = std::max<uint64_t>(1, 400000 / n);
            measure("chunked_avl_tail_build_per_append", n, arena, reps, double(n), [&](uint64_t) {
                auto r = ch.empty(); for (uint64_t i = 0; i < n; ++i) r = ch.append(r, val(i)); g_sink = r; });
            auto cr = ch.empty(); for (uint64_t i = 0; i < n; ++i) cr = ch.append(cr, val(i));
            for (uint64_t i = 0; i < n; ++i) if (ch.get(cr, i) != val(i)) { std::fprintf(stderr, "bad chunked\n"); return 1; }
            measure("chunked_avl_tail_getAt_random", n, arena, K, 1, [&](uint64_t i) { g_sink = ch.get(cr, idx(i)); });
            measure("trie_build_per_append", n, arena, reps, double(n), [&](uint64_t) {
                auto r = t.empty(); for (uint64_t i = 0; i < n; ++i) r = t.append(r, val(i)); g_sink = r; });
        }
        AvlList avl(arena);
        measure("to_avl_conversion", n, arena, KB, 1, [&](uint64_t) {
            uint64_t k = collect(base, buf.data());
            // balanced build bottom-up, one 64 B node per element (as buildBalancedFromArray)
            struct B { AvlList& L; const AvlNode* build(const Value* v, uint64_t m) {
                if (!m) return nullptr;
                const uint64_t mid = m / 2;
                const AvlNode* l = build(v, mid); const AvlNode* r = build(v + mid + 1, m - mid - 1);
                return L.mk(v[mid], l, r); } } b{avl};
            g_sink = b.build(buf.data(), k); });
    }
    return 0;
}