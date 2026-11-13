// lab_common.h — the simulated cell heap and the immutable list designs shared
// by list_lab.cpp and list_ops_lab.cpp. Header-only, no protoCore.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace lab {

using Value = const void*;
inline const void* volatile g_sink;

inline double nowNs() {
    return std::chrono::duration<double, std::nano>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------- the heap
struct Arena {
    char* base = nullptr; size_t cap = 0, used = 0, allocated = 0;
    explicit Arena(size_t bytes) : cap(bytes) {
        base = static_cast<char*>(std::aligned_alloc(64, bytes));
        std::memset(base, 0, bytes);            // fault the pages in once
    }
    ~Arena() { std::free(base); }
    void* alloc(size_t bytes) {                 // bytes: a multiple of 64
        if (used + bytes > cap) { std::fprintf(stderr, "arena full\n"); std::exit(1); }
        void* p = base + used; used += bytes; allocated += bytes;
        std::memset(p, 0, bytes);               // allocCell zeroes the cell
        return p;
    }
    void reset() { used = 0; }
};


// ---------------------------------------------------------------- AVL list
struct AvlNode {                                // 64 B with a 16 B header
    uint64_t hdr[2];
    Value value;
    const AvlNode* left;
    const AvlNode* right;
    uint32_t size; uint32_t height;
    uint64_t pad[2];
};
static_assert(sizeof(AvlNode) == 64, "AvlNode must be one line");

struct AvlList {
    Arena& a;
    explicit AvlList(Arena& ar) : a(ar) {}
    static uint32_t sz(const AvlNode* n) { return n ? n->size : 0; }
    static uint32_t ht(const AvlNode* n) { return n ? n->height : 0; }
    const AvlNode* mk(Value v, const AvlNode* l, const AvlNode* r) {
        auto* n = static_cast<AvlNode*>(a.alloc(64));
        n->value = v; n->left = l; n->right = r;
        n->size = sz(l) + sz(r) + 1; n->height = std::max(ht(l), ht(r)) + 1;
        return n;
    }
    const AvlNode* rotR(const AvlNode* y) { const AvlNode* x = y->left;
        return mk(x->value, x->left, mk(y->value, x->right, y->right)); }
    const AvlNode* rotL(const AvlNode* x) { const AvlNode* y = x->right;
        return mk(y->value, mk(x->value, x->left, y->left), y->right); }
    const AvlNode* bal(const AvlNode* n) {
        int b = int(ht(n->left)) - int(ht(n->right));
        if (b > 1) {
            if (ht(n->left->left) < ht(n->left->right)) n = mk(n->value, rotL(n->left), n->right);
            return rotR(n);
        }
        if (b < -1) {
            if (ht(n->right->right) < ht(n->right->left)) n = mk(n->value, n->left, rotR(n->right));
            return rotL(n);
        }
        return n;
    }
    const AvlNode* append(const AvlNode* n, Value v) {
        if (!n) return mk(v, nullptr, nullptr);
        return bal(mk(n->value, n->left, append(n->right, v)));
    }
    static Value get(const AvlNode* n, uint32_t i) {
        while (n) {
            uint32_t ls = sz(n->left);
            if (i < ls) n = n->left;
            else if (i == ls) return n->value;
            else { i -= ls + 1; n = n->right; }
        }
        return nullptr;
    }
    static size_t liveBytes(const AvlNode* n) { return n ? 64 + liveBytes(n->left) + liveBytes(n->right) : 0; }
};

// ---------------------------------------------------------------- radix trie with tail
// FL values per leaf (and in the tail), FI children per internal node, H
// header bytes per node; all compile-time constants, as they would be in the
// kernel, so the per-level digit arithmetic is multiplications. FL == FI is
// the plain trie; FL < FI is the hybrid: small leaves keep the tail copy of an
// append cheap, wide internal nodes keep the tree shallow.
template <unsigned FL, unsigned FI, unsigned H>
struct Trie {
    Arena& a;
    static constexpr size_t leafBytes = (H + 8ull * FL + 63) / 64 * 64;
    static constexpr size_t innerBytes = (H + 8ull * FI + 63) / 64 * 64;
    uint64_t cap[24];                           // cap[L] = elements a tree with L inner levels holds
    explicit Trie(Arena& ar) : a(ar) {
        cap[0] = FL; for (int i = 1; i < 24; ++i) cap[i] = cap[i - 1] * FI;
    }
    Value* slots(void* node) const { return reinterpret_cast<Value*>(static_cast<char*>(node) + H); }
    const Value* slots(const void* node) const { return reinterpret_cast<const Value*>(static_cast<const char*>(node) + H); }
    void* newLeaf() { return a.alloc(leafBytes); }
    void* newInner() { return a.alloc(innerBytes); }
    void* copyLeaf(const void* src) { void* n = a.alloc(leafBytes); std::memcpy(n, src, leafBytes); return n; }
    void* copyInner(const void* src) { void* n = a.alloc(innerBytes); std::memcpy(n, src, innerBytes); return n; }

    // The list handle: one 64 B cell (header + count, levels, root, tail).
    struct Root { uint64_t hdr[2]; uint64_t count; uint32_t levels; uint32_t pad; const void* tree; const void* tail; uint64_t pad2[2]; };
    static_assert(sizeof(Root) == 64, "Root must be one line");

    const Root* empty() { return static_cast<Root*>(a.alloc(64)); }
    uint64_t tailOffset(const Root* r) const { return r->count < FL ? 0 : ((r->count - 1) / FL) * FL; }
    static unsigned digit(uint64_t leafIdx, unsigned level) {      // level >= 1
        uint64_t x = leafIdx; for (unsigned l = 1; l < level; ++l) x /= FI; return unsigned(x % FI);
    }
    // Path copy that hangs a full tail (leaf number `li`) under inner `node` of `level`.
    const void* pushTail(const void* node, unsigned level, uint64_t li, const void* tail) {
        void* n = node ? copyInner(node) : newInner();
        unsigned d = digit(li, level);
        slots(n)[d] = (level == 1) ? tail : pushTail(node ? slots(node)[d] : nullptr, level - 1, li, tail);
        return n;
    }
    const Root* append(const Root* r, Value v) {
        auto* nr = static_cast<Root*>(a.alloc(64));
        *nr = *r; nr->count = r->count + 1;
        uint64_t tailLen = r->count - tailOffset(r);
        if (r->count == 0 || tailLen < FL) {                // room in the tail: copy it
            void* t = r->tail ? copyLeaf(r->tail) : newLeaf();
            slots(t)[r->count == 0 ? 0 : tailLen] = v;
            nr->tail = t;
            return nr;
        }
        uint64_t idx = tailOffset(r), li = idx / FL;         // the full tail is leaf number li
        if (r->tree == nullptr) {
            nr->tree = r->tail; nr->levels = 0;
        } else if (idx >= cap[r->levels]) {                  // tree full: grow one level
            unsigned L = r->levels + 1;
            void* top = newInner();
            slots(top)[0] = r->tree;
            slots(top)[digit(li, L)] = (L == 1) ? r->tail : pushTail(nullptr, L - 1, li, r->tail);
            nr->tree = top; nr->levels = L;
        } else {
            nr->tree = pushTail(r->tree, r->levels, li, r->tail);
        }
        void* t = newLeaf(); slots(t)[0] = v; nr->tail = t;
        return nr;
    }
    Value get(const Root* r, uint64_t i) const {
        uint64_t to = tailOffset(r);
        if (i >= to) return slots(r->tail)[i - to];
        unsigned dig[24]; uint64_t x = i / FL;
        for (unsigned l = 1; l <= r->levels; ++l) { dig[l] = unsigned(x % FI); x /= FI; }
        const void* n = r->tree;
        for (unsigned l = r->levels; l > 0; --l) n = slots(n)[dig[l]];
        return slots(n)[i % FL];
    }
    size_t liveBytes(const void* n, unsigned level) const {
        if (!n) return 0;
        if (level == 0) return leafBytes;
        size_t b = innerBytes;
        for (unsigned k = 0; k < FI; ++k) b += liveBytes(slots(n)[k], level - 1);
        return b;
    }
    size_t liveBytes(const Root* r) const { return 64 + liveBytes(r->tree, r->levels) + (r->tail ? leafBytes : 0); }
};

}  // namespace lab