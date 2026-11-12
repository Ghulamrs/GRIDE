/* fopenmodes.c - the runtimes review's finding 12: fopen's modes as the UCRT reads them - one '+', and one of 'b' and
   't'. "r+b+" and "rbt" opened the file on grt; they are refused now (null, EINVAL), and so is "q". Where the UCRT
   ends the program on an invalid mode (its invalid-parameter handler), GRIDE's runtimes answer null and EINVAL: the
   recorded deviation (docs\Runtime_Design.md section 16). So this transcript is GRIDE's, the same on grt and the
   C6747; the valid modes are the UCRT's too. */
#include <errno.h>
#include <stdio.h>

static void try_mode(const char *mode)
{
    FILE *f;
    errno = 0;
    f = fopen("fopenmodes.tmp", mode);
    printf("fopen(\"%s\"): %s, errno %s\n", mode, f ? "opened" : "null", errno == EINVAL ? "EINVAL" : errno == 0 ? "0" : "another");
    if (f) fclose(f);
}

int main(void)
{
    FILE *f = fopen("fopenmodes.tmp", "w");
    fputs("x", f);
    fclose(f);
    try_mode("r");
    try_mode("rb");
    try_mode("r+b");
    try_mode("rb+");
    try_mode("r+b+");
    try_mode("rbb");
    try_mode("r++");
    try_mode("q");
    try_mode("rw");
    remove("fopenmodes.tmp");
    return 0;
}