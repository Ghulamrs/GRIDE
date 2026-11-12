// mapstring.cpp - the runtimes review's finding 9: std::map<std::string, int>::operator[] did not link for the C6747
// (a local label of pair's piecewise constructor referenced and never defined); cpp11 1.62 put it right.
#include <cstdio>
#include <map>
#include <string>

int main()
{
    std::map<std::string, int> m;
    m["a"] = 1;
    m["b"] += 2;
    m["a"] += 10;
    std::printf("a %d, b %d, %u keys\n", m["a"], m["b"], static_cast<unsigned>(m.size()));
    return 0;
}