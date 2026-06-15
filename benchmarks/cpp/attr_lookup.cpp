// attr_lookup — pure C++ floor.  Read 3 fields of an object N times.
#include <cstdio>

struct FastObject {
    long long a, b, c;
};

int main() {
    constexpr long long N = 100000;
    FastObject obj{1, 2, 3};
    long long total = 0;
    for (long long i = 0; i < N; ++i) {
        total += obj.a;
        total += obj.b;
        total += obj.c;
    }
    std::printf("%lld\n", total);
    return 0;
}
