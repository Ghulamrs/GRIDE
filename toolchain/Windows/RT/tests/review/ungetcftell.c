/* ungetcftell.c - the runtimes review's finding 11: ftell after ungetc at the start of a file answered -1 on grt and
   0 on the UCRT. A byte pushed back before the first is at position 0 now, on both runtimes and the C6747. Once that
   byte is read again the UCRT calls the position 1 and grt 0 - not asked here, and recorded in section 16 of
   docs\Runtime_Design.md; the next byte read puts both at 1. */
#include <stdio.h>

int main(void)
{
    FILE *f = fopen("ungetcftell.tmp", "wb");
    long at;
    int c;
    fputs("abc", f);
    fclose(f);
    f = fopen("ungetcftell.tmp", "rb");
    printf("ungetc: %c\n", ungetc('Z', f));
    at = ftell(f);
    printf("ftell after ungetc at 0: %ld\n", at);
    c = getc(f);
    printf("getc: %c\n", c);
    c = getc(f);
    printf("getc: %c, ftell %ld\n", c, ftell(f));
    fclose(f);
    remove("ungetcftell.tmp");
    return 0;
}
