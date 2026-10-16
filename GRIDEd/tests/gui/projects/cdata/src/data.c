#include <stdio.h>

struct point { int x; int y; };
struct box { struct point corner[2]; char label[8]; double area; };

int sum(const int *v, int n)
{
    int i, s = 0;
    for (i = 0; i < n; i++)
        s += v[i];
    return s;
}

int main(void)
{
    int primes[5] = { 2, 3, 5, 7, 11 };
    int grid[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    struct point p = { 3, -4 };
    struct box b = { { { 0, 0 }, { 6, 2 } }, "box", 12.5 };
    const char *name = "cdata";
    int total = sum(primes, 5);
    printf("%s: total %d, p (%d,%d), %s %g, grid %d\n", name, total, p.x, p.y, b.label, b.area, grid[1][2]);
    return total;
}
