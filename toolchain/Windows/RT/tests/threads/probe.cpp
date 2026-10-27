// probe.cpp - the runtime's locks under load (docs\Runtime_Design.md 9.6), run by threads.ps1: THREADS threads at
// once, each opening, writing with fprintf and putc, reading back and closing files of its own (the descriptor and
// FILE tables), writing whole lines to one shared FILE with fprintf and fputs (each call one piece of the output),
// and making function-local statics with destructors (atexit from several threads). What it prints does not depend
// on how the threads interleave, so it is compared with what Microsoft's CRT printed (threads.expected).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

enum { THREADS = 8, ROUNDS = 300, LINES = 40 };

static FILE *shared;
static LONG made;
static int bads[THREADS];

struct Counted {
    int id;
    explicit Counted(int i) : id(i) { InterlockedIncrement(&made); }
    ~Counted() { if (id == 0) printf("statics destroyed\n"); }
};

static int localStatic(int k)
{
    switch (k % 8) {
    case 0: { static Counted a(0); return a.id; }
    case 1: { static Counted b(1); return b.id; }
    case 2: { static Counted c(2); return c.id; }
    case 3: { static Counted d(3); return d.id; }
    case 4: { static Counted e(4); return e.id; }
    case 5: { static Counted f(5); return f.id; }
    case 6: { static Counted g(6); return g.id; }
    default: { static Counted h(7); return h.id; }
    }
}

static DWORD WINAPI worker(LPVOID p)
{
    int t = (int)(size_t)p, bad = 0;
    char name[32], line[128];
    localStatic(t);
    for (int r = 0; r < ROUNDS; r++) {
        sprintf(name, "thr-%d.txt", t);
        FILE *f = fopen(name, "w");
        if (!f) { bad++; continue; }
        for (int i = 0; i < LINES; i++) {
            if (i & 1) {
                sprintf(line, "%d %d %d\n", t, r, i);
                for (const char *q = line; *q; q++) putc(*q, f);
            } else {
                fprintf(f, "%d %d %d\n", t, r, i);
            }
        }
        fclose(f);
        f = fopen(name, "r");
        if (!f) { bad++; continue; }
        int n = 0;
        while (fgets(line, sizeof line, f)) {
            int a, b, c;
            if (sscanf(line, "%d %d %d", &a, &b, &c) != 3 || a != t || b != r || c != n) bad++;
            n++;
        }
        fclose(f);
        if (n != LINES) bad++;
        fprintf(shared, "fprintf %d %d the line written by one call\n", t, r);
        fputs("fputs a whole line by one call\n", shared);
    }
    remove(name);
    bads[t] = bad;
    return 0;
}

int main(void)
{
    shared = fopen("thr-shared.txt", "w");
    HANDLE h[THREADS];
    for (int t = 0; t < THREADS; t++) h[t] = CreateThread(0, 0, worker, (LPVOID)(size_t)t, 0, 0);
    WaitForMultipleObjects(THREADS, h, TRUE, INFINITE);
    int bad = 0;
    for (int t = 0; t < THREADS; t++) {
        bad += bads[t];
        CloseHandle(h[t]);
    }
    fclose(shared);
    FILE *f = fopen("thr-shared.txt", "r");
    char line[128];
    int kinds[2] = { 0, 0 }, torn = 0;
    while (fgets(line, sizeof line, f)) {
        int a, b;
        if (sscanf(line, "fprintf %d %d", &a, &b) == 2 && strstr(line, " the line written by one call\n")) kinds[0]++;
        else if (!strcmp(line, "fputs a whole line by one call\n")) kinds[1]++;
        else torn++;
    }
    fclose(f);
    remove("thr-shared.txt");
    printf("own files: %d bad lines or rounds\n", bad);
    printf("shared: %d fprintf, %d fputs lines, %d torn\n", kinds[0], kinds[1], torn);
    printf("statics made: %ld\n", (long)made);
    return 0;
}
