// perthread.cpp - the state the Microsoft runtime keeps for each thread (docs\Runtime_Design.md 9.6): errno,
// strtok's place, rand's state, gmtime's and localtime's result, asctime's text; and thread_local objects made and
// destroyed in each thread, the main thread's at exit before the static objects. Run by probe.ps1 against the
// UCRT's transcript (perthread.expected).
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern "C" void *CreateThread(void *, unsigned long long, unsigned long (*)(void *), void *, unsigned long,
                              unsigned long *);
extern "C" unsigned long WaitForSingleObject(void *, unsigned long);
extern "C" int CloseHandle(void *);

static void inThread(unsigned long (*fn)(void *), void *arg)
{
    void *h = CreateThread(0, 0, fn, arg, 0, 0);
    WaitForSingleObject(h, 0xFFFFFFFFu);
    CloseHandle(h);
}

struct Noisy {
    const char *name;
    explicit Noisy(const char *n) : name(n) { printf("make %s\n", name); }
    ~Noisy() { printf("end %s\n", name); }
};

Noisy forever("static");
thread_local Noisy perThread("thread_local");
thread_local int counter = 100;

static unsigned long worker(void *)
{
    printf("thread: errno %d\n", errno);
    errno = 7;
    char text[] = "x,y";
    char *t = strtok(text, ",");
    while (t) { printf("thread: token %s\n", t); t = strtok(0, ","); }
    int a = rand(), b = rand();
    printf("thread: rand %d %d\n", a, b);
    time_t later = 86400 * 365;
    struct tm *g = gmtime(&later);
    printf("thread: gmtime year %d\n", g->tm_year);
    printf("thread: asctime %s", asctime(g));
    printf("thread: %s, counter %d\n", perThread.name, ++counter);
    return 0;
}

int main()
{
    errno = 5;
    char text[] = "a b c";
    char *t = strtok(text, " ");
    printf("main: token %s\n", t);
    int a = rand();
    printf("main: rand %d\n", a);
    time_t start = 0;
    struct tm *g = gmtime(&start);
    char *line = asctime(g);
    printf("main: counter %d\n", ++counter);
    inThread(worker, 0);
    printf("main: errno %d\n", errno);
    while ((t = strtok(0, " ")) != 0) printf("main: token %s\n", t);
    a = rand();
    printf("main: rand %d\n", a);
    printf("main: gmtime year %d, same result %d\n", g->tm_year, g == gmtime(&start));
    printf("main: asctime %s", line);
    srand(5);
    a = rand();
    printf("main: srand(5) rand %d\n", a);
    printf("main: %s, counter %d\n", perThread.name, ++counter);
    return 0;
}
