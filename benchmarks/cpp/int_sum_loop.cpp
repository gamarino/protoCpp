// int_sum_loop — pure C++ floor.  sum(0..99999), no protoCore.
//
// IMPORTANT: -O3 trivially proves the loop computes N*(N-1)/2 and
// folds it to a single mov of the constant — leaving the benchmark as
// `printf("4999950000")`. The asm volatile barrier after the += forces
// the compiler to materialise `total` on every iteration so the loop is
// preserved as a measurable arithmetic-loop floor, not a constant-fold.
#include <cstdint>
#include <cstdio>

int main() {
    // N bumped to 10,000,000 so the inner loop dominates the ~3 ms binary
    // startup floor. The protoCpp + protoPython counterparts mirror this.
    constexpr int64_t N = 10000000;
    int64_t total = 0;
    for (int64_t i = 0; i < N; ++i) {
        total += i;
        asm volatile("" : "+r"(total) :: "memory");
    }
    std::printf("%lld\n", (long long)total);
    return 0;
}
