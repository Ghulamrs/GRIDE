# 3. The screen

```
 File   Edit   Project   Build   Debug   Target   Language   Tools   Help
┌──────────────────────┬─ main.c ─ sum.c ──────────────────────────────────┐
│- Sources             │  1 #include <stdio.h>                             │
│    src/main.c        │  2                                                │
│    src/sum.c         │  3 int main(void)                                 │
│- Headers             │  4 {                                              │
│    src/sum.h         │  5     printf("answer %d\n", addUp(2, 40));       │
│                      │  6     return 0;                                  │
│                      │  7 }                                              │
├──────────────────────┴─ Console ─ Debug ─ Assembly ───────────── 8 lines ─┤
│$ c90 2 sources -o sums                                                    │
│[built /home/you/work/sums]                                                │
└───────────────────────────────────────────────────────────────────────────┘
 main.c  7 lines            C  debug  c90* arm64-darwin  5/7  col 5  [text]
```

## The four regions

**The menu bar**, across the top. `F10` opens it; left and right move between
columns, up and down between items, enter chooses, escape leaves. The menu
**reopens on the column it was left on**, which is worth knowing when you are
driving it by muscle memory.

**A dot marks where you already are.** Language, Tools, Target, debug and
release, and the switches on the Edit menu are all states rather than commands,
so the item naming the one you are in carries a `•` (a `*` under `--plain`):

```
┌─────────────────────┐
│• By language  Ctrl-K │
│  c90                 │
│  shalimar                 │
└─────────────────────┘
```

The status bar carries some of the same news and not all of it. `c90*` says the
language chose that compiler and `c90` says you did — but the language field
reads `C` for a `.c` file whether that came from its name or from your picking
`Language ▸ C` by hand, and the menu is the only place that distinguishes them.

**The project pane**, down the left. The groups of the project and the files in
them — the project's own arrangement, not the directory's. `Ctrl-P` shows and
hides it, enter opens the file under the cursor.

**What you have open is listed above the project**, under a heading of its own,
and that part moves: a file opened appears there, a file closed goes, and the
heading itself is only there while something is open. The project's groups
below it do not move, because a project says what the work *is* and opening a
file is not a change to that. The two questions have two answers and the pane
gives both.

Each file is shown **by its own name**, not by the path the project file writes:
the project file says `src/first.c` and the pane says `first.c`. The grouping is
what a project has instead of directories, so the prefix said nothing the pane
was not already saying — and repeated down a column it read as though the files
lived somewhere they do not. The full path is on the status bar the moment the
file is opened.

With no project it shows the files you have open instead, in the order you
opened them, and nothing at all when none are. `Project ▸ Close` is what
puts it in that state: it closes the *view* of the project, leaving the project file
exactly as it was and every open file open.

**Questions are asked in a box** of their own, in the middle of the text — not
on the message line, which is where the editor answers back. When the question
is *which file*, what is in the directory is listed under the line you type
into: typing narrows the list, up and down walk it, tab fills the line with
what is picked, and enter takes it. A directory is offered with a `/` on the
end, and picking one lists what is inside it instead of opening it. A name that
matches nothing on the list is still taken as typed, which is how a file that
does not exist yet gets its name.

**The edit view**, in the middle. Its tab strip names the open files; the
current one is highlighted. `F3` and `F2` move to the next and previous file.

**The panel**, along the bottom, with three tabs — Console, Debug, Assembly.
`Ctrl-E` shows and hides it, and left and right change tab when the cursor is
in it. Page 9 is about what each tab holds.

`Ctrl-W` moves the cursor between the three: text, project pane, panel. The
status bar's last field says where it is.

## The status bar

```
 main.c  7 lines            C  debug  c90* arm64-darwin  5/7  col 5  [text]
```

| field | means |
| --- | --- |
| `main.c` | the file, with a `*` after it when it has unsaved changes |
| `7 lines` | how long it is |
| `C` | the language, which decides the colouring and the compiler |
| `debug` | the configuration — `Ctrl-D` toggles it with `release` |
| `c90*` | the compiler; the `*` means it was chosen by language, not by you |
| `arm64-darwin` | the target — `Ctrl-T` moves to the next one |
| `5/7` | the line the caret is on, of how many |
| `col 5` | the column, counted in characters and not in bytes |
| `[text]` | which region has the cursor: `text`, `tree` or `panel` |

**The `*` on the compiler is the useful one.** Without it, somebody named that
compiler by hand — with `Ctrl-K`, the Tools menu, `--toolchain`, or a group in
the project file — and the file's own language is not deciding.

## The message line

Under the status bar, one line wide. It says what just happened: what was
saved, what was built, where the program stopped, or why something was
refused. **It is one line and it clips**, so anything that needs more room says
the short version here and the rest in the Console tab.

## Drawing

The frame uses box-drawing characters. Some consoles take those from a second
font and break the lines at every join; `Edit ▸ Plain frame` (or `--plain`)
draws it with `-`, `|` and `+` instead.
