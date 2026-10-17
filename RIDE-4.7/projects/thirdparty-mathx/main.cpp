// main.cpp - mathx-demo, a program that uses the third-party mathx library.
//
// The project file says where the library is, and RIDE does the rest:
//   "include":   third_party/mathx/include          -> -I on every compile
//   "libraries": third_party/mathx/libs/<platform>/... -> handed to the link
// Open mathx-macos.pro on a Mac and mathx-windows.pro on Windows: the two differ
// only in which prebuilt binary they name.
#include <cstdio>
#include "mathx.h"

int main() {
    char name[32]="Gpt";
    std::printf("using %s\n", mathx_version());
    std::printf("gcd(1071, 462) = %d\n", mathx_gcd(1071, 462));
    std::printf("10! = %lld\n", mathx_factorial(10));
    std::fgets(name, sizeof(name), stdin);
    const double marks[] = { 72.5, 88.0, 91.5, 64.0 };
    std::printf("mean of 4 marks = %.3f\n", mathx_mean(marks, 4));
    std::printf("sqrt(2) = %.10f\n", mathx_sqrt(2.0));
    std::printf("Name: %s\n", name);
    return 0;
}
