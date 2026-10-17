/* mathx.c - the library's source, kept for reference only: the project links
 * the prebuilt binaries in ../libs, built from this by each platform's own
 * compiler (see BUILD.txt), exactly as a vendor would ship them. */
#include "mathx.h"

const char *mathx_version(void) { return "mathx 1.0"; }

int mathx_gcd(int a, int b)
{
    while (b != 0) { int r = a % b; a = b; b = r; }
    return a < 0 ? -a : a;
}

long long mathx_factorial(int n)
{
    long long f = 1;
    while (n > 1) f *= n--;
    return f;
}

double mathx_mean(const double *values, int count)
{
    double sum = 0.0;
    int i;
    for (i = 0; i < count; i++) sum += values[i];
    return count > 0 ? sum / count : 0.0;
}

double mathx_sqrt(double x)
{
    double r = x > 1.0 ? x : 1.0, last = 0.0;
    if (x <= 0.0) return 0.0;
    while (r - last > 1e-12 || last - r > 1e-12) { last = r; r = 0.5 * (r + x / r); }
    return r;
}
