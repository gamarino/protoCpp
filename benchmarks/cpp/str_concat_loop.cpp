// str_concat_loop — pure C++ floor.  s = s + "x" N times (O(N^2)
// reallocation pattern, mirrors the protoPython source intent).
#include <cstdio>
#include <string>

int main() {
    constexpr int N = 2000;
    std::string s;
    for (int i = 0; i < N; ++i) s = s + "x";
    std::printf("%lu\n", s.size());
    return 0;
}
