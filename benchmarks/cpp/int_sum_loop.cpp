// int_sum_loop — pure C++ floor.  sum(0..99999), no protoCore.
#include <cstdint>
#include <cstdio>

int main() {
    constexpr int64_t N = 100000;
    int64_t total = 0;
    for (int64_t i = 0; i < N; ++i) total += i;
    // Guard against the optimiser deleting the loop entirely.
    std::printf("%lld\n", (long long)total);
    return 0;
}
