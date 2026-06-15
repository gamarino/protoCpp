// list_append_loop — pure C++ floor.  std::vector<long long> growth.
#include <cstdio>
#include <vector>

int main() {
    constexpr int N = 10000;
    std::vector<long long> lst;
    for (int i = 0; i < N; ++i) lst.push_back(i);
    std::printf("%lu\n", lst.size());
    return 0;
}
