# 10. Keys, menus and the command line

## Every key

| | | | |
| --- | --- | --- | --- |
| `F1` | the keys | `Ctrl-B` | compile this file |
| `F10` | the menu | `F5` | run this file |
| `F2` / `F3` | previous / next file | `F4` | build the project |
| `F9` | breakpoint | `F8` | debug — start or continue |
| `F7` / `F6` | step over / into | `Ctrl-D` | debug or release |
| `Ctrl-Up` / `Ctrl-Down` | up / down the stack | `Ctrl-T` | next target |
| `Ctrl-K` | next compiler | `Ctrl-L` | line numbers |
| `Ctrl-W` | next pane | `Ctrl-P` | project pane |
| `Ctrl-E` | bottom panel | `Tab` | lay this line out |
| `Ctrl-A` | re-indent selection | `Ctrl-F` | find |
| `Ctrl-G` | find next | `Ctrl-R` | replace |
| `Ctrl-Z` / `Ctrl-Y` | undo / redo | `Ctrl-S` | save |
| `Ctrl-C` / `Ctrl-X` / `Ctrl-V` | copy / cut / paste | `Ctrl-Q` | leave |
| shift + arrows | select | | |

In the project pane, enter opens. In the panel, left and right change tab;
**shift with up or down makes the panel taller or shorter**, which is where
shift and an arrow do nothing else; on Console, enter goes to the line the
compiler named; on Debug, enter on a frame looks at it and enter on the top
line goes back.

**`RIDE.exe` on Windows differs on ten keys deliberately** — `Ctrl+PageDown` and
`Ctrl+PageUp` move between files there, because `F2` and `F3` are Rename and
Find next in a Windows application and pretending otherwise would be worse.

## The mouse

The console editor takes the mouse on every machine - macOS's Terminal and
iTerm, a Linux terminal or an ssh session, Windows Terminal and the Windows
console. Every key above still works; the mouse is a second way in.

- **Menu bar** - a click opens a menu, a click on an item does it, a click
  anywhere else closes it.
- **File tabs** - a click brings that file to the front.
- **Project pane** - a click picks an entry, a double click opens a file or
  folds a folder; a click on a folder's `+` or `-` folds it at once.
- **Gutter** - a click sets or clears a breakpoint on that line.
- **Text** - a click puts the cursor there, Shift-click extends the selection,
  a double click takes the word, a drag selects.
- **Panel** - a click on Console, Debug or Assembly shows it; a double click
  on a line goes to the error, or to the frame.
- **Lists** (Project > Open and the other questions) - a click picks, a double
  click takes it.
- **The wheel** scrolls the text, the project pane or the panel, whichever is
  under it.

While RIDE has the mouse, the terminal's own text selection wants a modifier:
Shift in Windows Terminal and most Linux terminals, Option in macOS's Terminal
and iTerm.

## The menus

**File** — New, Open, Save, Save As, Close, Next file, Previous file, Quit.
**Edit** — Undo, Redo, Cut, Copy, Paste, Select all, Find, Find next, Find
previous, Replace, Re-indent, Project pane, Bottom panel, Line numbers, Plain
frame.
**Project** — New, Open, Save, Save as, Close, then New File, Add File, Remove
File. No item here repeats the word "project": the column says it. The five
above the rule are the project itself; the three below are its list of files,
and none of those three touches the disk except New File, which makes one.
**Build** — Compile file, Run file, Build project, Run project, Debug, Release, Clean,
Emulate on vm6747, Verify. On tms6747 Run file and Run project build the TI
program and run the linked `.out` on `sim6747`; the last two are tms6747's:
Emulate on vm6747 runs the assembly on `vm6747` and links no `.out`, and Verify
runs both and compares the two outputs ([page 6](06-the-project.md)).
**Debug** — Start / continue, Debug project, Toggle breakpoint, Step over, Step
into, Step out, Up the stack, Down the stack, Watch expression, Stop debugging.
**Language** — By extension, C, C++, Shalimar, JSON, Plain text.
**Tools** — By language, c90, cpp11, shalimar, MSVC (cl), C++ (host). Ours first,
then the machine's. `Ctrl-K` walks them in that order. Below them, where a
tool is found: Locate vcvars64.bat, Assembler for x86_64-windows, Linker for
x86_64-windows, TI compiler for tms6747, Linker for tms6747.
**Target** — the four architectures: `x86_64-windows`, `x86_64-linux`,
`arm64-darwin`, `tms6747`.

Those last three are one chain and sit in that order: what the file **is**,
which **compiler** reads it, and which **machine** the output runs on. Target
is the most downstream of the three, which is why it comes last.
**Help** — Contents, Keys, About.

## The command line

```
RIDE.exe [file] [--project dir] [--toolchain auto|c90|cpp11|msvc|shalimar|c++]
    [--config debug|release] [--c90 path] [--cpp11 path] [--cl path] [--shalimar path]
    [--cxx path] [--c2s path] [--width n] [--tabs] [--case-indent] [--plain]
```

| | |
| --- | --- |
| `--toolchain` | use one compiler for everything; it says so where it cannot take the file |
| `--config` | `debug` (the default) or `release` |
| `--c90`, `--cpp11`, `--cl`, `--shalimar`, `--cxx`, `--c2s` | the programs to run |
| `--project` | what the pane on the left shows |
| `--width n` | columns per indent step (4) |
| `--tabs` | indent with tabs instead of spaces |
| `--case-indent` | `case` one step inside its `switch` rather than in its own column |
| `--plain` | frame the screen with `-`, `|` and `+` |

`$C90`, `$CPP11`, `$SHALIMAR`, `$CXX` and `$C2S` name them when the flags do not,
and a `c90`, `cpp11`, `shalimar` or `c2s` beside the editor is found before PATH is
asked; `cl` is also found through Visual Studio 2022 itself, so no Developer
Command Prompt is needed.

**Settings in the project are what that project always does; anything on the
command line is applied after and wins.**

## The three machines

RIDE is built and checked on three, and each says something the others
cannot.

| | |
| --- | --- |
| a Mac | where it is written; `arm64-darwin` natively; clang++ |
| the Linux box | real g++, which is the only thing that can say whether the sources are ISO C++14; `x86_64-linux`; the only place a C++ group goes to g++ |
| the Windows box | the only machine with `cl`, and so the only one where a C and C++ target meets that linker; MSVC at `/W4 /WX` |

`tools/to-linux.sh` and `tools/to-windows.sh` relay this working tree to the
other two and build it from clean. Neither uses the checkout that may already
be on those machines: a clone only has what has been pushed, and those scripts
exist to check what is in front of you before it is committed.

## Building RIDE itself

```
make                                     the terminal editor
make check CC1=$HOME/... SHC=$HOME/...   both suites
build.bat check                          the same on Windows
build.bat gui                            the window, as RIDE.exe
```

Spell those paths with `$HOME` and never `~`: make hands a literal `~` to the
test process, where nothing expands it, and the build then fails with a
compiler that works perfectly when you run it by hand.
