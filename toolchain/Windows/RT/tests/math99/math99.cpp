// math99.cpp - the C99 functions of <math.h> that stage 2 adds (docs\Runtime_Design.md 16), against the UCRT's: each
// result as its 64 bits and the errno it leaves, for the special values and a spread of ordinary ones. math99.ps1
// compares the transcripts: the exact functions bit for bit, the others within 1 ulp.
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned long long bitsOf(double d)
{
    unsigned long long b;
    memcpy(&b, &d, sizeof b);
    return b;
}

static double fromBits(unsigned long long b)
{
    double d;
    memcpy(&d, &b, sizeof d);
    return d;
}

static unsigned long long state = 88172645463325252ULL;
static double random(double low, double high)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return low + (high - low) * (double)(state >> 11) / 9007199254740992.0;
}

static double specials[] = { 0.0, -0.0, 1.0, -1.0, 2.0, -2.0, 0.5, -0.5, 3.0, -3.0, 1e-300, -1e-300, 1e300, -1e300,
                             1.5, -1.5, 2.5, -2.5, 0.1, -0.1, 10.0, -10.0, 100.0, 1000.0, 709.0, 710.0, -745.0,
                             -746.0, 1023.0, 1024.0, -1074.0, -1075.0, 171.0, 172.0, -171.5, 0.99999999, 1.00000001,
                             4503599627370495.5, 4503599627370496.0, 9007199254740993.0, 2147483647.5, 2147483648.0,
                             -2147483648.5, 9.2233720368547758e18, -9.2233720368547758e18, 1e-20, -1e-20, 6.0, 27.0,
                             26.5, -6.0, 0.0 };
static const int specialCount = sizeof specials / sizeof specials[0];

typedef double (*One)(double);
typedef double (*Two)(double, double);

static void one(const char *name, One f, double x)
{
    errno = 0;
    double r = f(x);
    printf("%s %016llx = %016llx %d\n", name, bitsOf(x), bitsOf(r), errno);
}

static void two(const char *name, Two f, double x, double y)
{
    errno = 0;
    double r = f(x, y);
    printf("%s %016llx %016llx = %016llx %d\n", name, bitsOf(x), bitsOf(y), bitsOf(r), errno);
}

static void ones(const char *name, One f, double low, double high, int count)
{
    for (int i = 0; i < specialCount; i++) one(name, f, specials[i]);
    one(name, f, INFINITY);
    one(name, f, -INFINITY);
    one(name, f, NAN);
    one(name, f, fromBits(1));
    one(name, f, -fromBits(1));
    one(name, f, fromBits(0x000FFFFFFFFFFFFFULL));
    one(name, f, fromBits(0x7FEFFFFFFFFFFFFFULL));
    for (int i = 0; i < count; i++) one(name, f, random(low, high));
}

static void twos(const char *name, Two f, int count)
{
    double some[] = { 0.0, -0.0, 1.0, -1.0, 2.5, -2.5, 3.0, 1e300, -1e-300, INFINITY, -INFINITY, NAN, 7.0, 0.75 };
    int n = sizeof some / sizeof some[0];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) two(name, f, some[i], some[j]);
    for (int i = 0; i < count; i++) two(name, f, random(-100, 100), random(-10, 10));
    for (int i = 0; i < count; i++) two(name, f, random(-1e10, 1e10), random(-1e-5, 1e-5));
}

static double viaIlogb(double x) { return (double)ilogb(x); }
static double viaLrint(double x) { return (double)lrint(x); }
static double viaLlrint(double x) { return (double)llrint(x); }
static double viaLround(double x) { return (double)lround(x); }
static double viaLlround(double x) { return (double)llround(x); }
static double viaScalbn(double x) { return scalbn(x, 3) + scalbn(x, -1080) + scalbn(x, 2000); }
static double viaScalbnOnly(double x) { return scalbn(x, -1060); }
static double viaScalbln(double x) { return scalbln(x, 1000000000L) + scalbln(x, -60); }
static double viaNexttoward(double x, double y) { return nexttoward(x, y); }
static double viaFma1(double x, double y) { return fma(x, y, -x * y); }
static double viaFma2(double x, double y) { return fma(x, y, 1.0); }
static double viaFma3(double x, double y) { return fma(x, x, y * 1e-17); }

int main()
{
    ones("exp2", exp2, -1100, 1100, 300);
    ones("expm1", expm1, -50, 720, 300);
    ones("expm1s", expm1, -1e-3, 1e-3, 200);
    ones("log1p", log1p, -1, 1e10, 300);
    ones("log1ps", log1p, -1e-3, 1e-3, 200);
    ones("asinh", asinh, -1e5, 1e5, 300);
    ones("asinhs", asinh, -2, 2, 200);
    ones("acosh", acosh, 1, 1e6, 300);
    ones("acoshs", acosh, 1, 1.5, 200);
    ones("atanh", atanh, -1, 1, 400);
    ones("erf", erf, -7, 7, 400);
    ones("erfc", erfc, -7, 28, 500);
    ones("tgamma", tgamma, -180, 180, 400);
    ones("tgammas", tgamma, 0, 5, 200);
    ones("lgamma", lgamma, -180, 1e6, 300);
    ones("lgammas", lgamma, -5, 5, 400);
    ones("logb", logb, -1e10, 1e10, 50);
    ones("ilogb", viaIlogb, -1e10, 1e10, 50);
    ones("rint", rint, -1e6, 1e6, 100);
    ones("nearbyint", nearbyint, -100, 100, 100);
    ones("lrint", viaLrint, -1e10, 1e10, 100);
    ones("llrint", viaLlrint, -1e19, 1e19, 100);
    ones("lround", viaLround, -1e10, 1e10, 100);
    ones("llround", viaLlround, -1e19, 1e19, 100);
    ones("scalbn", viaScalbn, -1e5, 1e5, 50);
    ones("scalbn2", viaScalbnOnly, -1e5, 1e5, 50);
    ones("scalbln", viaScalbln, -1e5, 1e5, 50);
    twos("copysign", copysign, 20);
    twos("fmax", fmax, 20);
    twos("fmin", fmin, 20);
    twos("fdim", fdim, 20);
    twos("nextafter", nextafter, 20);
    twos("nexttoward", viaNexttoward, 20);
    twos("remainder", remainder, 100);
    {
        double some[] = { 0.0, -0.0, 1.0, -1.0, 2.5, -2.5, 3.0, 1e300, -1e-300, INFINITY, -INFINITY, NAN, 7.0, 0.75 };
        for (int i = 0; i < 14; i++)
            for (int j = 0; j < 14; j++) {
                int q = 99;
                errno = 0;
                double r = remquo(some[i], some[j], &q);
                printf("remquo %016llx %016llx = %016llx q %d %d\n", bitsOf(some[i]), bitsOf(some[j]), bitsOf(r), q, errno);
            }
        for (int i = 0; i < 200; i++) {
            double x = random(-1e6, 1e6), y = random(-10, 10);
            int q = 99;
            double r = remquo(x, y, &q);
            printf("remquo %016llx %016llx = %016llx q %d\n", bitsOf(x), bitsOf(y), bitsOf(r), q);
        }
    }
    two("fdim", fdim, 1e308, -1e308);
    two("fdim", fdim, -1e308, 1e308);
    two("fma2", viaFma2, 1e300, 1e300);
    two("fma2", viaFma2, 1e-300, 1e-300);
    two("fma3", viaFma3, 1e200, 0.0);
    twos("fma1", viaFma1, 100);
    twos("fma2", viaFma2, 100);
    twos("fma3", viaFma3, 100);
    const char *texts[] = { "", "0", "1", "0x10", "abc", "123456789012345678901234567890", "-5" };
    for (int i = 0; i < 7; i++) printf("nan \"%s\" = %016llx\n", texts[i], bitsOf(nan(texts[i])));
    for (int i = -3; i <= 3; i++) {
        double x = i * 2.5 + 0.25, y = 1.5;
        int q = 99;
        double r = remquo(x, y, &q);
        printf("remquo %g %g = %g q %d\n", x, y, r, q);
        q = 99;
        r = remquo(-1e300, 3.0 * (i + 4), &q);
        printf("remquo big %d = %016llx q %d\n", i, bitsOf(r), q);
    }
    return 0;
}
