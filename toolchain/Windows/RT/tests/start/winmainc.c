/* winmainc.c - a program whose entry is WinMain, which link makes a windows program, by c90; winmain.cpp is the same
   program by cpp11 */
#include "entry.h"

static const struct Row rows[] = { ROW_ARGUMENTS, ROW_SHOW };

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR line, int show)
{
    int i, answer;
    if (!getenv("GRT_ENTRY")) return parent(rows, ROWS(rows));
    if (strcmp(getenv("GRT_ENTRY"), "quiet") != 0) {
        printf("instance %s, previous %s, show %d\n", (char *)instance == __ImageBase ? "the image" : "other", previous ? "set" : "null", show);
        show_narrow("line", line);
        printf("__argc %d, __argv %s, __wargv %s\n", __argc, __argv ? "set" : "null", __wargv ? "set" : "null");
        for (i = 0; i < __argc; i++) show_narrow_argument("__argv", __argv[i]);
    }
    answer = child();
    if (answer) return answer;
}
