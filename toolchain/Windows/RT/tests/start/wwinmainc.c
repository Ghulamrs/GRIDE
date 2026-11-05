/* wwinmainc.c - a program whose entry is wWinMain, which link makes a windows program, by c90; wwinmain.cpp is the same
   program by cpp11 */
#include "entry.h"

static const struct Row rows[] = { ROW_ARGUMENTS, ROW_SHOW };

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR line, int show)
{
    int i, answer;
    if (!getenv("GRT_ENTRY")) return parent(rows, ROWS(rows));
    if (strcmp(getenv("GRT_ENTRY"), "quiet") != 0) {
        printf("instance %s, previous %s, show %d\n", (char *)instance == __ImageBase ? "the image" : "other", previous ? "set" : "null", show);
        show_wide("line", line);
        printf("__argc %d, __wargv %s, __argv %s\n", __argc, __wargv ? "set" : "null", __argv ? "set" : "null");
        for (i = 0; i < __argc; i++) show_argument("__wargv", __wargv[i]);
    }
    answer = child();
    if (answer) return answer;
}
