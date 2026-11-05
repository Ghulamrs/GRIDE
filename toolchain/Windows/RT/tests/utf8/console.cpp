// console.cpp - a program on a real console, for screen.c: text past the console's code page written every way a
// program writes, a character cut in two by the end of a write, a line long enough to cross the runs a console is
// written in, and lines typed at it, one of them a file's name. console utf8 asks for UTF-8; console plain does not.
#include <fcntl.h>
#include <io.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Latin, Cyrillic, Japanese and a character past one UTF-16 unit
static const char Text[] = "Gr\xC3\xBC\xC3\x9F" "e \xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xF0\x9F\x98\x80";

static void chomp(char *line)
{
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
}

static void hex(const char *what, const char *s)
{
    printf("%s", what);
    for (; *s; s++) printf(" %02x", (unsigned char)*s);
    printf("\n");
}

int main(int argc, char **argv)
{
    int asked = argc > 1 && strcmp(argv[1], "utf8") == 0;
    if (asked && !setlocale(LC_CTYPE, ".UTF-8")) return 3;

    printf("printf %s\n", Text);
    printf("putchar ");
    for (const char *p = Text; *p; p++) putchar(*p);
    putchar('\n');
    fputs("fwrite ", stdout);
    fwrite(Text, 1, 3, stdout);
    fflush(stdout);
    fwrite(Text + 3, 1, strlen(Text) - 3, stdout);
    fputc('\n', stdout);
    fprintf(stderr, "stderr %s\n", Text);
    puts(Text);

    static char longLine[2810];
    longLine[0] = 'x';
    for (int i = 0; i < 1400; i++) memcpy(longLine + 1 + 2 * i, "\xD0\xB6", 2);
    printf("%s\n", longLine);

    char line[256];
    if (fgets(line, sizeof line, stdin)) {
        chomp(line);
        printf("fgets %s\n", line);
        hex("bytes", line);
        if (asked) {
            FILE *f = fopen(line, "w");
            printf("fopen %d\n", f != 0);
            if (f) {
                fputs("typed\n", f);
                fclose(f);
            }
        }
    }
    int c, n = 0;
    while ((c = getchar()) != EOF && c != '\n') line[n++] = (char)c;
    line[n] = 0;
    hex("getchar", line);
    if (!fgets(line, sizeof line, stdin)) printf("the end %d\n", feof(stdin) != 0);

    fflush(stdout);
    _setmode(1, _O_BINARY);
    fputs("binary\r\n", stdout);
    return 0;
}
