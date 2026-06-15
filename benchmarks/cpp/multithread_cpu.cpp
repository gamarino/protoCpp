// multithread_cpu — pure C++ floor.  4 OS threads × 2M-iter accumulator.
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

constexpr long long CHUNK = 2'000'000;
constexpr int N_THREADS = 4;

int main() {
    std::vector<long long> results(N_THREADS, 0);
    std::vector<std::thread> threads;
    threads.reserve(N_THREADS);
    for (int t = 0; t < N_THREADS; ++t) {
        threads.emplace_back([t, &results]() {
            long long total = 0;
            for (long long i = 0; i < CHUNK; ++i) total += i;
            results[t] = total;
        });
    }
    for (auto& th : threads) th.join();
    long long grand = 0;
    for (long long r : results) grand += r;
    std::printf("%lld\n", grand);
    return 0;
}
