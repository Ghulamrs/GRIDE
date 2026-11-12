/* atexitcap.c - the runtimes review's finding 2 (docs\reviews\runtimes critical review - Fable 5.1.md): 5,000 atexit
   registrations, every one kept and run. The list was a fixed 4,096 on Windows (64 on the C6747) and refused the
   rest; RTS6x's exit\Termination now grows by chunks. The function registered first runs last and counts. */
#include <stdio.h>
#include <stdlib.h>

static int ran;

static void one(void) { ran++; }
static void last(void) { printf("%d of 5000 ran\n", ran); }

int main(void)
{
    int i, refused = 0;
    atexit(last);
    for (i = 0; i < 5000; i++)
        if (atexit(one) != 0) refused++;
    printf("5000 registered, %d refused\n", refused);
    return 0;
}