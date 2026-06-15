// call_recursion — pure C++ floor.  fib(25), no protoCore.
#include <cstdio>

static long long fib(long long n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

int main() {
    long long r = fib(25);
    std::printf("%lld\n", r);
    return 0;
}
