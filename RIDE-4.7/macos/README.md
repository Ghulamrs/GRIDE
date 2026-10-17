# RIDE on macOS - the window

The third front end over RIDE's one core. The terminal editor is `../Makefile`'s,
the Windows Forms window is `../winforms/`, and this is an AppKit window written
in Objective-C++ (`.mm`: C++ and Objective-C in one file). Like the Windows
window it compiles `../src`'s core and `../winforms/bridge.cpp` - the C interface
both windows speak - and none of the terminal's drawing. `../src` is not touched.

```
+------------------------------------------------------------------+
| RIDE 4.5 - hello - hello.c                          (title bar)  |
| File Edit View Project Build Target Option Help     (menu bar)   |
+---------------+--------------------------------------------------+
| HELLO         | hello  >  src/hello.c                       c90  |
|  v Sources    |  1  #include <stdio.h>                           |
|     hello.c   |  2  int main(void)                               |
|  v Headers    |  3  {                                            |
| OPEN FILES    |  4      printf("hi\n");                          |
|   hello.c     |  5      return 0;                                |
|  (navigator)  |  6  }                   (editor, line numbers)  |
|               +--------------------------------------------------+
|               | [Errors (1)] [Progress] [Output]   (bottom 1/4)  |
|               | x  expected ';'            hello.c   4:20        |
+---------------+--------------------------------------------------+
| (spinner) 1 issue - hello.c:4:20 ...   C  debug  c90*  arm64  Ln 4|
+------------------------------------------------------------------+
```

## Building

```
cd macos
make            # ../../build/RIDE-4.7/RIDE.app
make run        # build and open it
```

or `open Window.xcodeproj` and Run. The Xcode project is generated from this
Makefile's source lists by `python3 make-xcodeproj.py`; run it again after
adding or removing a file.

Either way the compilers in `../bin` (c90.exe, cpp11.exe, shalimar.exe, c2s.exe,
masm, link, lnk6x, ...) and the Shalimar runtime in `../bin/lib` are copied into
`RIDE.app/Contents/MacOS`, beside the editor, which is where it looks first -
after the `C90`, `CPP11` and `SHALIMAR` environment variables. The manual goes to
`Contents/Resources/help`.

## The files

| file | what it is |
| --- | --- |
| `main.mm` | the application: arguments, the Dock, quitting |
| `WindowController.mm` | the window - navigator, editor, panel, status bar, the eight menus and every action |
| `CodeView.mm` | the text: lays C, C++ and Shalimar out as it is typed, through the core's indent rules |
| `LineNumbers.mm` | the gutter: line numbers, and red/orange marks where the last build complained |
| `Text.h` | NSString to UTF-8 and back, in one place |

## What it does

* **Navigator** (left) - the project's groups and files, then the open files.
  Right-click for New, Add, Rename, Move to Group, Remove, Delete, Show in Finder.
* **Editor** (middle) - one text view, each open file's text swapped into it, so
  each file keeps its own undo and place. Layout as you type, Tab to re-lay a
  line, Ctrl-I to re-indent a selection or the file, colouring from the core's
  highlighter, applied as layout attributes so it never touches undo.
* **Bottom panel** - a quarter of the window, three tabs:
  * **Errors** - every diagnostic the build printed, read line by line with the
    core's own parser; click one to go there.
  * **Progress** - each step with its time, a bar, and how long the build took.
  * **Output** - the command, what the compiler said, the program's output and
    return value, and the assembly after a compile.
* **Status line** (bottom) - the last thing that happened; what the next build
  will use (language, debug/release, compiler, target); line and column.
* Builds run off the main thread; the window stays live and the Build and
  Target menus wait until it is done - as do Open, the recent lists and every
  Project item that changes the project, since a build reads it as it goes.

## Running a program that reads

**Run File and Run Project run the program with an input of its own.** Its
output comes into Output as it is written, not when it ends, and the line under
Output is its keyboard: Return sends what is typed there and a newline,
Control-D ends its input (its next read sees end of file). The program's input
and output are a pseudo-terminal, so `printf("a number? "); scanf(...)` shows
the prompt before it waits, as at a shell, and the terminal echoes what is sent -
the window does not echo it again. What it writes to stderr comes in red, and
the build's own lines before it in grey.

**Build > Stop (Command-.) ends it**, and everything it started. During a build
it ends the compiler or linker running, and the build fails saying it was
stopped. Quitting stops both. `../README.md`, "Input, and Stop", has what the
core does and what differs on Windows.

## The file as it was

A file is read as UTF-8, or as Latin-1 when it is not (the status line says so),
and a BOM and CRLF or CR line endings are remembered: Save writes it back the
way it came, so a file shared with the Windows editor is not rewritten to LF.
Inside the window every line ends in `\n`, which is what the core counts - a
row is what the core calls one, and U+0085 or U+2028 in a Latin-1 file do not
split it. Revert and Reload read through the same reader as Open.

Save asks before it writes over a file that changed on the disk since it was
read here; coming back to the window reads such a file again when it has no
changes of its own, and asks when it has. A build that could not save a file
does not run.

## Colour, and large files

The colours are the layout manager's temporary attributes, not the text, so
colouring never enters undo and never marks a file changed. They are made again
only where an edit can have changed them: from the first row it touched, past
the last, until a row starts in the lexer state it started in before. The rows
themselves come from an index of where each begins, kept as the text is edited,
so the caret's row, the gutter and the colouring never count newlines from the
top of the file. Enter and a typed `}`, `#` or `:` hand the core the rows above
the caret and not the whole file, since those are all the layout reads.

## Menus

| menu | holds |
| --- | --- |
| File | New, Open, Open Recent, Close, Save, Save As, Save All, Revert, Show in Finder |
| Edit | Undo/Redo, Cut/Copy/Paste, Find (the find bar), Go to Line, Re-indent, Shift Left/Right, Comment |
| View | Navigator, Bottom Panel, Errors/Progress/Output, Line Numbers, font size, next/previous file, Full Screen |
| Project | New, Open, Recent, Save As, Close; New File, Add File, Remove, Rename, Delete and the include paths and libraries, offered only while a project is open |
| Build | Compile File (Cmd-B), Run File (Cmd-R), Build Project (Shift-Cmd-B), Run Project (Shift-Cmd-R), Stop (Cmd-.), Clean, Debug/Release, Convert C to/from Shalimar, next issue |
| Target | the four targets, Compiler (by language, c90, cpp11, shalimar, host c++), Language |
| Option | Font, header directories, shared include paths and libraries, the assembler, linkers and TI compiler, the tools in use |
| Help | the manual, Keys, the Shalimar reference, About |

## Where the keys differ from the other two

The terminal and Windows editors use Control keys throughout; here a menu item
takes Command, because Control-K, Control-T and their neighbours are the text
view's own Emacs keys in every Mac text field. So Next Target is Command-Option-T
and Next Compiler Command-Option-K. Bigger Font is Command-= (Command-+ works
too). Re-indent keeps Control-I and the panel tabs Control-1 to 3, which nothing
in a text view uses.

**The menus are the Mac's menu bar alone.** Until 02-10-2026 a row inside the
window repeated them, one button per menu, for users coming from Windows and
Linux; it was removed at the user's request with the 4.7 audit (finding 30), as
the 27-09 review's L1 had suggested.

## Signing

`make` signs ad hoc, for this Mac: each Mach-O helper on its own and then the
app, not `--deep`. For another's, name a Developer ID and the hardened runtime,
then notarize:

```
make SIGN="Developer ID Application: ..." HARDENED=--options=runtime
xcrun notarytool submit RIDE.zip --keychain-profile ... --wait
```

`ARCHS="arm64 x86_64"` builds a universal app, and `DEBUG=1` adds `-g` and a
`.dSYM` beside it. Compilers are found in the bundle first, then - when the app
sits in a checkout - the checkout's `bin/`, then on PATH; never a directory the
app merely happens to be in.

## Not here yet

The debugger (breakpoints, stepping, locals, the stack) that the Windows window
has; `bridge.h` already carries everything it needs.
