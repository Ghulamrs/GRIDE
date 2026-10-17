# 2. Getting started

## Running it

```
RIDE.exe                          open the project you were last in
RIDE.exe file.c                   open a file
RIDE.exe --project some/dir       open a directory as the project
```

On Windows `RIDE.exe` is the window - which is what somebody there runs -
and `RIDEConsole.exe` is the same editor over the console. Both take the
same arguments as the Unix one.

**With nothing named it opens the project it was last in**, and failing that
it makes one. The first file of the project is opened too, so you arrive
looking at code rather than at an empty buffer.

## The first run

If there is no project to return to, RIDE writes `~/c90-demo` — one C
program with a loop and a call in it, which is enough to set a breakpoint and
stop inside. The demo is projectile motion, and it is deliberately small: it
exists so that `F8` does something on a machine where nothing has been written
yet.

## Opening a directory that has no project

A directory with no project file gets one written from what is already in it — named
after the directory, `work.pro` for a directory called `work` — in
two groups — **Headers** and **Sources**. Nothing is moved on disk; the file
just describes what is there.

**A project file that will not parse is never written over.** That is
somebody's work, and an editor that silently replaced it with its own guess
would be the worst kind of helpful. You get told, and the pane shows the
directory instead.

## Where things land

| | |
| --- | --- |
| the project's program | beside the project file, named by the target |
| a single file's program | a temporary, removed when you are done with it |
| object files | a temporary directory of the editor's own, cleared after |
| the assembly | shown in the Assembly tab, not left on disk |

The distinction that matters: **the project's program is the project's** and
stays where it was built, so it is still there when the editor is not. A
single file's is a scratch thing the editor made in order to run it.

## Two configuration files, two different questions

- **`prime.pro`**, in a directory, says what *that project* is: its name, its
  groups, what it builds, how it indents.
- **`~/.ride/state.json`** says what *this machine* had: the last project
  you were in, whether you are building debug or release, the font the window
  draws code in, and whatever else earns a place. It is state, not
  configuration - the configuration is `settings.json`, in `RIDE 5.17` under your
  home directory on Windows and in the same `.ride` on macOS and Linux, begun from
  the installation's own the first time RIDE starts. In
  your own directory rather than beside the program, because the program's
  directory is build output - it is deleted and rebuilt - and there is more
  than one copy of RIDE on a machine.

The second could not be folded into the first. Every project file on disk would
then claim to have been the most recent one.

## Getting help without leaving

- **`F1`** — the keys, on one screen.
- **`Help ▸ Contents`** — these pages, a line each, in the panel.
- **`Help ▸ About`** — the name, the version, and who wrote it.
