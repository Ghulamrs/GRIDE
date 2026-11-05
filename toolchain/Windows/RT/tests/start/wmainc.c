/* wmainc.c - a program whose entry is wmain, by c90; wmain.cpp is the same program by cpp11, where the name must stay
   wmain though nothing declares it */
#include "entry.h"

static const struct Row rows[] = { ROW_ARGUMENTS };

int wmain(int argc, wchar_t **argv, wchar_t **envp)
{
    int i, answer;
    if (!getenv("GRT_ENTRY")) return parent(rows, ROWS(rows));
    if (strcmp(getenv("GRT_ENTRY"), "quiet") != 0) {
        printf("argc %d, __argc %d, __wargv %s, __argv %s\n", argc, __argc, __wargv == argv ? "argv" : "other", __argv ? "set" : "null");
        for (i = 0; i < argc; i++) show_argument("argv", argv[i]);
        printf("argv[argc] %s\n", argv[argc] ? "set" : "null");
        for (i = 0; envp[i]; i++) show_wide("envp", envp[i]);
    }
    answer = child();
    if (answer) return answer;
}
