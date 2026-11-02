// rethrow.cpp - catches that rethrow, and catches nested inside catches (docs\Runtime_Design.md 8.4): a throw; leaving
// one catch for an outer try, a rethrow from a function called by a catch, and both inside a catch whose own exception
// must survive them. Run by probe.ps1 against the UCRT's transcript (rethrow.expected).
#include <stdio.h>

static void rethrow() { throw; }

struct Noisy {
    int n;
    explicit Noisy(int k) : n(k) {}
    ~Noisy() { printf("  ~Noisy %d\n", n); }
};

static void shape(int which)
{
    printf("shape %d\n", which);
    switch (which) {
    case 1:
        try { try { throw 2; } catch (int) { printf("  inner\n"); throw; } }
        catch (int b) { printf("  outer b%d\n", b); }
        break;
    case 2:
        try { throw 1; }
        catch (int a) {
            try { throw 2; } catch (int b) { printf("  b%d\n", b); }
            printf("  a%d\n", a);
        }
        break;
    case 3:
        try { throw 1; }
        catch (int a) {
            try { rethrow(); } catch (int c) { printf("  c%d\n", c); }
            printf("  a%d\n", a);
        }
        break;
    case 4:
        try { throw 1; }
        catch (int a) {
            try { try { throw 2; } catch (int) { printf("  inner\n"); throw; } }
            catch (int b) { printf("  b%d\n", b); }
            printf("  a%d\n", a);
        }
        break;
    case 5:
        try { throw 1; }
        catch (int a) {
            try { throw 2; } catch (int b) { printf("  b%d\n", b); }
            try { rethrow(); } catch (int c) { printf("  c%d\n", c); }
            printf("  a%d\n", a);
        }
        break;
    case 6:
        try { throw 1; }
        catch (int a) {
            try { throw 2; } catch (int b) { printf("  b%d\n", b); }
            try { throw; } catch (int c) { printf("  c%d\n", c); }
            printf("  a%d\n", a);
        }
        break;
    case 7:
        try { throw 1; }
        catch (int a) {
            try { try { throw 2; } catch (int) { throw; } } catch (int b) { printf("  b%d\n", b); }
            try { throw; } catch (int c) { printf("  c%d\n", c); }
            printf("  a%d\n", a);
        }
        break;
    case 8:
        try {
            Noisy outer(1);
            try { Noisy inner(2); throw Noisy(3); }
            catch (const Noisy &x) { printf("  caught %d\n", x.n); Noisy during(4); throw; }
        }
        catch (Noisy &x) { printf("  caught again %d\n", x.n); }
        break;
    }
}

int main()
{
    for (int i = 1; i <= 8; i++) shape(i);
    printf("end\n");
    return 0;
}
