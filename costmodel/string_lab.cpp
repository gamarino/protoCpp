// string_lab — design-space lab for protoCore's immutable string, without protoCore.
//
// Same simulated cell heap as list_lab (64-byte lines, zeroed on allocation,
// arena reset between repetitions). Every variant keeps today's leaf: one
// 64 B cell with 36 bytes of UTF-8 and an 8 byte content hash. What changes is
// how leaves are joined:
//
//   rope_avl         : today's shape — a balanced binary tree over the leaves;
//                      a short append copies the last leaf and the right spine
//   rope_avl_tail    : the same tree, plus an open last leaf kept in a root
//                      cell; a short append copies that leaf and the root only
//   trie_F<k>_tail   : the leaves under a radix trie with k children per inner
//                      node (64 B for k = 6, 256 B for k = 30), open last leaf
//
// Operations, for strings of N bytes (ASCII, so byte = character):
//   append 1 char   (s = s + "x", N times)       ns and cells per append
//   charAt random                                  ns
//   concat two halves of N/2                       ns and cells
//   live bytes per character

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

const void* volatile g_sink;
volatile char g_char;

double nowNs() {
    return std::chrono::duration<double, std::nano>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct Arena {
    char* base = nullptr; size_t cap = 0, used = 0, allocated = 0;
    explicit Arena(size_t bytes) : cap(bytes) {
        base = static_cast<char*>(std::aligned_alloc(64, bytes));
        std::memset(base, 0, bytes);
    }
    ~Arena() { std::free(base); }
    void* alloc(size_t bytes) {
        if (used + bytes > cap) { std::fprintf(stderr, "arena full\n"); std::exit(1); }
        void* p = base + used; used += bytes; allocated += bytes;
        std::memset(p, 0, bytes);
        return p;
    }
    void reset() { used = 0; }
};

// ---------------------------------------------------------------- the leaf (as today)
constexpr unsigned LB = 36;
struct Leaf { uint64_t hdr[2]; uint8_t len; uint8_t pad[3]; char bytes[LB]; uint64_t hash; };
static_assert(sizeof(Leaf) == 64, "Leaf must be one line");

const Leaf* leafWith(Arena& a, const Leaf* old, char c) {
    auto* l = static_cast<Leaf*>(a.alloc(64));
    if (old) std::memcpy(l, old, 64);
    l->bytes[l->len++] = c;
    l->hash = l->hash * 31 + uint8_t(c);
    return l;
}

// ---------------------------------------------------------------- binary rope (AVL over leaves)
struct RNode {
    uint64_t hdr[2];
    const Leaf* leaf; const RNode* left; const RNode* right;
    uint32_t height; uint32_t pad; uint64_t bytes;
    uint64_t pad2;
};
static_assert(sizeof(RNode) == 64, "RNode must be one line");

struct Rope {
    Arena& a;
    explicit Rope(Arena& ar) : a(ar) {}
    static uint32_t ht(const RNode* n) { return n ? n->height : 0; }
    static uint64_t by(const RNode* n) { return n ? n->bytes : 0; }
    const RNode* mk(const Leaf* lf, const RNode* l, const RNode* r) {
        auto* n = static_cast<RNode*>(a.alloc(64));
        n->leaf = lf; n->left = l; n->right = r;
        n->height = std::max(ht(l), ht(r)) + 1; n->bytes = by(l) + by(r) + (lf ? lf->len : 0);
        return n;
    }
    const RNode* rotR(const RNode* y) { const RNode* x = y->left; return mk(x->leaf, x->left, mk(y->leaf, x->right, y->right)); }
    const RNode* rotL(const RNode* x) { const RNode* y = x->right; return mk(y->leaf, mk(x->leaf, x->left, y->left), y->right); }
    const RNode* bal(const RNode* n) {
        int b = int(ht(n->left)) - int(ht(n->right));
        if (b > 1) { if (ht(n->left->left) < ht(n->left->right)) n = mk(n->leaf, rotL(n->left), n->right); return rotR(n); }
        if (b < -1) { if (ht(n->right->right) < ht(n->right->left)) n = mk(n->leaf, n->left, rotR(n->right)); return rotL(n); }
        return n;
    }
    const RNode* appendLeaf(const RNode* n, const Leaf* lf) {
        if (!n) return mk(lf, nullptr, nullptr);
        return bal(mk(n->leaf, n->left, appendLeaf(n->right, lf)));
    }
    static const Leaf* lastLeaf(const RNode* n) { while (n->right) n = n->right; return n->leaf; }
    const RNode* replaceLast(const RNode* n, const Leaf* lf) {    // path copy down the right spine
        if (!n->right) return mk(lf, n->left, nullptr);
        return mk(n->leaf, n->left, replaceLast(n->right, lf));
    }
    // s + c, today's way: grow the last leaf while it has room, else add a leaf.
    const RNode* appendChar(const RNode* t, char c) {
        if (t) { const Leaf* last = lastLeaf(t); if (last->len < LB) return replaceLast(t, leafWith(a, last, c)); }
        return appendLeaf(t, leafWith(a, nullptr, c));
    }
    static char charAt(const RNode* n, uint64_t i) {
        while (n) {
            uint64_t lb = by(n->left);
            if (i < lb) { n = n->left; continue; }
            i -= lb;
            if (i < n->leaf->len) return n->leaf->bytes[i];
            i -= n->leaf->len; n = n->right;
        }
        return 0;
    }
    // s1 + s2 with trees of equal height: one new node (the general join adds
    // at most |h1 - h2| rotations).
    const RNode* concat(const RNode* x, const RNode* y) {
        const Leaf* lf = lastLeaf(x);                   // reuse x's last leaf as the joining node's leaf
        const RNode* xl = removeLast(x);
        return mk(lf, xl, y);
    }
    const RNode* removeLast(const RNode* n) {
        if (!n->right) return n->left;
        return mk(n->leaf, n->left, removeLast(n->right));
    }
    static size_t live(const RNode* n) { return n ? 128 + live(n->left) + live(n->right) : 0; }  // node + its leaf
};

// The same rope with an open last leaf in a root cell.
struct RopeTail {
    struct Root { uint64_t hdr[2]; const RNode* tree; const Leaf* tail; uint64_t bytes; uint64_t pad[3]; };
    static_assert(sizeof(Root) == 64, "Root must be one line");
    Arena& a; Rope rope;
    explicit RopeTail(Arena& ar) : a(ar), rope(ar) {}
    const Root* empty() { return static_cast<Root*>(a.alloc(64)); }
    const Root* appendChar(const Root* r, char c) {
        auto* nr = static_cast<Root*>(a.alloc(64)); *nr = *r; nr->bytes = r->bytes + 1;
        if (r->tail && r->tail->len == LB) { nr->tree = rope.appendLeaf(r->tree, r->tail); nr->tail = leafWith(a, nullptr, c); }
        else nr->tail = leafWith(a, r->tail, c);
        return nr;
    }
    char charAt(const Root* r, uint64_t i) const {
        uint64_t tb = Rope::by(r->tree);
        return i < tb ? Rope::charAt(r->tree, i) : r->tail->bytes[i - tb];
    }
    const Root* concat(const Root* x, const Root* y) {   // x's tail joins the trees; y keeps its tail
        auto* nr = static_cast<Root*>(a.alloc(64));
        nr->tree = rope.concat(rope.appendLeaf(x->tree, x->tail), y->tree);
        nr->tail = y->tail; nr->bytes = x->bytes + y->bytes;
        return nr;
    }
    size_t live(const Root* r) const { return 64 + Rope::live(r->tree) + (r->tail ? 64 : 0); }
};

// ---------------------------------------------------------------- leaves under a radix trie
template <unsigned F, unsigned H>
struct LeafTrie {
    static constexpr size_t innerBytes = (H + 8ull * F + 63) / 64 * 64;
    struct Root { uint64_t hdr[2]; const void* tree; const Leaf* tail; uint64_t bytes; uint64_t leaves; uint32_t levels; uint32_t pad; uint64_t pad2; };
    static_assert(sizeof(Root) == 64, "Root must be one line");
    Arena& a;
    uint64_t cap[24];                                    // cap[L] = leaves a tree with L inner levels holds
    explicit LeafTrie(Arena& ar) : a(ar) { cap[0] = 1; for (int i = 1; i < 24; ++i) cap[i] = cap[i - 1] * F; }
    static const void** slots(void* n) { return reinterpret_cast<const void**>(static_cast<char*>(n) + H); }
    static const void* const* slots(const void* n) { return reinterpret_cast<const void* const*>(static_cast<const char*>(n) + H); }
    static unsigned digit(uint64_t li, unsigned level) { uint64_t x = li; for (unsigned l = 1; l < level; ++l) x /= F; return unsigned(x % F); }
    const void* pushLeaf(const void* node, unsigned level, uint64_t li, const Leaf* lf) {
        void* n = a.alloc(innerBytes);
        if (node) std::memcpy(n, node, innerBytes);
        unsigned d = digit(li, level);
        slots(n)[d] = (level == 1) ? static_cast<const void*>(lf) : pushLeaf(node ? slots(node)[d] : nullptr, level - 1, li, lf);
        return n;
    }
    // A full leaf joins the tree as leaf number r->leaves (fields of `nr` updated).
    void addLeaf(Root* nr, const Leaf* lf) {
        uint64_t li = nr->leaves;
        if (!nr->tree) { nr->tree = lf; nr->levels = 0; }
        else if (li >= cap[nr->levels]) {
            unsigned L = nr->levels + 1;
            void* top = a.alloc(innerBytes);
            slots(top)[0] = nr->tree;
            slots(top)[digit(li, L)] = (L == 1) ? static_cast<const void*>(lf) : pushLeaf(nullptr, L - 1, li, lf);
            nr->tree = top; nr->levels = L;
        } else nr->tree = pushLeaf(nr->tree, nr->levels, li, lf);
        nr->leaves = li + 1;
    }
    const Root* empty() { return static_cast<Root*>(a.alloc(64)); }
    const Root* appendChar(const Root* r, char c) {
        auto* nr = static_cast<Root*>(a.alloc(64)); *nr = *r; nr->bytes = r->bytes + 1;
        if (r->tail && r->tail->len == LB) { addLeaf(nr, r->tail); nr->tail = leafWith(a, nullptr, c); }
        else nr->tail = leafWith(a, r->tail, c);
        return nr;
    }
    char charAt(const Root* r, uint64_t i) const {        // leaves in the tree are full
        uint64_t tb = r->leaves * LB;
        if (i >= tb) return r->tail->bytes[i - tb];
        uint64_t li = i / LB;
        unsigned dig[24]; uint64_t x = li;
        for (unsigned l = 1; l <= r->levels; ++l) { dig[l] = unsigned(x % F); x /= F; }
        const void* n = r->tree;
        for (unsigned l = r->levels; l > 0; --l) n = slots(n)[dig[l]];
        return static_cast<const Leaf*>(n)->bytes[i % LB];
    }
    // No relaxed nodes: x + y re-hangs y's leaves one by one (O(n / 36)).
    // A relaxed (RRB) node would make it O(log n); that is the price measured here.
    const void* leafAt(const Root* r, uint64_t li) const {
        unsigned dig[24]; uint64_t x = li;
        for (unsigned l = 1; l <= r->levels; ++l) { dig[l] = unsigned(x % F); x /= F; }
        const void* n = r->tree;
        for (unsigned l = r->levels; l > 0; --l) n = slots(n)[dig[l]];
        return n;
    }
    const Root* concat(const Root* x, const Root* y) {    // assumes x's tail is full (N/2 a multiple of 36)
        auto* nr = static_cast<Root*>(a.alloc(64)); *nr = *x;
        if (x->tail) { addLeaf(nr, x->tail); nr->tail = nullptr; }
        for (uint64_t li = 0; li < y->leaves; ++li) addLeaf(nr, static_cast<const Leaf*>(leafAt(y, li)));
        nr->tail = y->tail; nr->bytes = x->bytes + y->bytes;
        return nr;
    }
    size_t liveInner(const void* n, unsigned level) const {
        if (!n) return 0;
        if (level == 0) return 64;
        size_t b = innerBytes;
        for (unsigned k = 0; k < F; ++k) b += liveInner(slots(n)[k], level - 1);
        return b;
    }
    size_t live(const Root* r) const { return 64 + liveInner(r->tree, r->levels) + (r->tail ? 64 : 0); }
};

// ---------------------------------------------------------------- driver
template <class S, class Build, class At, class Concat, class Live>
void run(const char* name, Arena& a, uint64_t n, Build build, At at, Concat concat, Live live) {
    const uint64_t reps = std::max<uint64_t>(1, 2000000 / n);
    double best = 1e300; size_t bytes = 0;
    for (int batch = 0; batch < 4; ++batch) {
        a.allocated = 0;
        double t0 = nowNs();
        for (uint64_t r = 0; r < reps; ++r) { a.reset(); g_sink = build(n); }
        double t = (nowNs() - t0) / double(reps * n);
        if (batch > 0) best = std::min(best, t);
        bytes = a.allocated / reps;
    }
    a.reset();
    auto s = build(n);
    for (uint64_t i = 0; i < n; ++i)
        if (at(s, i) != char('a' + i % 26)) { std::fprintf(stderr, "%s: WRONG char at %llu\n", name, (unsigned long long)i); std::exit(1); }
    const uint64_t R = 1000000;
    double t0 = nowNs();
    for (uint64_t i = 0; i < R; ++i) g_char = at(s, (i * 7919) % n);
    double randNs = (nowNs() - t0) / R;
    double liveB = double(live(s)) / n;
    // concat: two halves, each a multiple of 36 bytes
    uint64_t half = (n / 2) / LB * LB;
    auto x = build(half); auto y = build(half);
    size_t before = a.allocated;
    t0 = nowNs();
    auto z = concat(x, y);
    double concatNs = nowNs() - t0;
    double concatCells = double(a.allocated - before) / 64;
    for (uint64_t i = 0; i < 2 * half; i += 997)
        if (at(z, i) != char('a' + (i % half) % 26)) { std::fprintf(stderr, "%s: WRONG concat char at %llu\n", name, (unsigned long long)i); std::exit(1); }
    std::printf("RES variant=%s N=%llu append_ns=%.1f cells_per_append=%.2f charAt_random_ns=%.1f "
                "live_bytes_per_char=%.2f concat_halves_ns=%.0f concat_halves_cells=%.0f\n",
                name, (unsigned long long)n, best, double(bytes) / n / 64, randNs, liveB, concatNs, concatCells);
    std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<uint64_t> sizes = {2000, 10000, 100000, 1000000};
    if (argc > 1) { sizes.clear(); for (int i = 1; i < argc; ++i) sizes.push_back(std::strtoull(argv[i], nullptr, 10)); }
    Arena arena(size_t(3) << 30);
    auto ch = [](uint64_t i) { return char('a' + i % 26); };

    for (uint64_t n : sizes) {
        { Rope r(arena);
          run<Rope>("rope_avl", arena, n,
            [&](uint64_t m) { const RNode* t = nullptr; for (uint64_t i = 0; i < m; ++i) t = r.appendChar(t, ch(i)); return t; },
            [&](const RNode* t, uint64_t i) { return Rope::charAt(t, i); },
            [&](const RNode* x, const RNode* y) { return r.concat(x, y); },
            [&](const RNode* t) { return Rope::live(t); }); }
        { RopeTail r(arena);
          run<RopeTail>("rope_avl_tail", arena, n,
            [&](uint64_t m) { auto t = r.empty(); for (uint64_t i = 0; i < m; ++i) t = r.appendChar(t, ch(i)); return t; },
            [&](const RopeTail::Root* t, uint64_t i) { return r.charAt(t, i); },
            [&](const RopeTail::Root* x, const RopeTail::Root* y) { return r.concat(x, y); },
            [&](const RopeTail::Root* t) { return r.live(t); }); }
        { LeafTrie<6, 16> r(arena);
          using R = LeafTrie<6, 16>::Root;
          run<R>("trie_F6_64B_tail", arena, n,
            [&](uint64_t m) { auto t = r.empty(); for (uint64_t i = 0; i < m; ++i) t = r.appendChar(t, ch(i)); return t; },
            [&](const R* t, uint64_t i) { return r.charAt(t, i); },
            [&](const R* x, const R* y) { return r.concat(x, y); },
            [&](const R* t) { return r.live(t); }); }
        { LeafTrie<30, 16> r(arena);
          using R = LeafTrie<30, 16>::Root;
          run<R>("trie_F30_256B_tail", arena, n,
            [&](uint64_t m) { auto t = r.empty(); for (uint64_t i = 0; i < m; ++i) t = r.appendChar(t, ch(i)); return t; },
            [&](const R* t, uint64_t i) { return r.charAt(t, i); },
            [&](const R* x, const R* y) { return r.concat(x, y); },
            [&](const R* t) { return r.live(t); }); }
    }
    return 0;
}