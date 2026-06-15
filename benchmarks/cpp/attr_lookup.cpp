// attr_lookup — pure C++ floor.  Read 3 fields of an object N times.
//
// IMPORTANT: -O3 trivially proves obj.a/b/c are loop-invariant constants
// and folds the entire loop to `total = N * (a+b+c)`. That would make
// this "floor" a `printf("constant")` benchmark with no attribute work.
// The asm volatile barrier after each += forces the compiler to
// materialise `total` as if it were used externally on every iteration,
// which keeps the load + add visible to the measurement.
#include <cstdio>

struct FastObject {
    long long a, b, c;
};

int main() {
    // N bumped to 5,000,000 so the inner loop dominates the ~3 ms binary
    // startup floor: at the previous 100 K, the wall-clock ratio between
    // any two binaries was driven by startup, not the work being measured.
    // The protoCpp + protoPython counterparts mirror this N.
    constexpr long long N = 5000000;
    FastObject obj{1, 2, 3};
    long long total = 0;
    for (long long i = 0; i < N; ++i) {
        total += obj.a;
        asm volatile("" : "+r"(total) :: "memory");
        total += obj.b;
        asm volatile("" : "+r"(total) :: "memory");
        total += obj.c;
        asm volatile("" : "+r"(total) :: "memory");
    }
    std::printf("%lld\n", total);
    return 0;
}
