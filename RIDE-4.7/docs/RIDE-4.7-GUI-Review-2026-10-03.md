# RIDE 4.7 GUI front ends - review of 03-10-2026

A review only: nothing was changed, committed, pushed or resealed. The tree reviewed is RIDE-4.7
`main` at `9f9ec81` (clean). The focus is the Windows window, RIDEGui (`winforms/`), read in full
with the bridge it calls (`winforms/bridge.cpp`, `bridge.h`) and the core behind the calls that
matter (`src/project.cpp`, `settings.cpp`, `compile.cpp`, `find.cpp`, `ccs/ccsworkspace.cpp`,
`process.cpp`). The macOS window (`macos/WindowController.mm`) was read for parity: where its
behaviour differs from Windows, and where one of its 02-10 fixes has no Windows counterpart.

**What was measured on the Windows box** (`ssh windows`, build tree `C:\Users\GRA\source\RIDE-4.7`,
relayed from this tree by `tools/to-windows.sh gui`): the window builds clean (`build.bat gui`),
`RIDE.exe --version` answers `RIDE 4.7`, the unit suite reads **1267 checks, 0 failed** and the
session suite **321 checks, 0 failed** (`build.bat check`, with cdb's checks skipped as always on
that box); the installed `C:\Program Files\RIDE 4.7\bin\RIDE.exe` (1,484,800 bytes, 16:20 today) is
byte-for-byte the size of the one just built; the user's `C:\Users\GRA\RIDE 4.7\settings.json`,
`C:\Users\GRA\.ride\state.json` and `%TEMP%\RIDE.log` were read. The log holds no handler exception
and no unhandled exception for any 4.7 run (the three `NullReferenceException` lines in it are from
27-09, RIDE 4.5); `RIDE-fault.log` is from 27-09 too, the heap-corruption-before-main that
`settings.cpp` records as fixed. **The window itself was not driven from here**: another Claude
session on that box was asked to drive the installed window item by item on the same desktop, and
two drivers on one desktop corrupt each other's measurements. Its report
(`C:\Users\GRA\Developer\Claude\gui-review-2026-10-03\REPORT.md`, announced through
`NOTE-TO-MAC.md`) **had not appeared when this was written**; see "The Windows session's
walk-through" at the end. So every finding below that says *reading* is a code-read finding with
the reproduction written out, and the ones a live walk would settle are marked.

IDs: `W-` Windows only, `B-` both windows, `M-` macOS only, `R-` robustness and code quality,
`X-` menu and shortcut mismatches (reported, not judged - the Project menu's entries change only
when the user asks).

## Summary

| ID | Severity | Window | One line |
| --- | --- | --- | --- |
| W-01 | high | Windows | File > Open (even cancelled) or File > New replaces the project tree with the open-files list, with no way back short of reopening the project |
| W-02 | high | Windows | Target and Tools menus on a CCS project say "written to settings.json" and the choice is lost at the next build |
| W-03 | medium | Windows | Close Project leaves the closed project's folder in the status bar and as the "where" of later operations |
| W-04 | medium | Windows | Project > Remove is enabled by the tree's selection and acts on the file in front |
| W-05 | medium | Windows | A program's output is appended one message per chunk with no cap: a runaway program fills memory and stalls the window |
| W-06 | medium | Windows | A file changed on disk is never re-read while open: Convert again, a generated file, an outside editor all show stale text |
| B-01 | medium | both | Find in Files runs on the UI thread with no way to stop it |
| W-07 | low | Windows | Undo back to the saved text keeps the modified mark and the close prompt |
| W-08 | low | Windows | File > Open, Recent and Save As are disabled for as long as a program runs |
| W-09 | low | Windows | The Console input line takes the keyboard the moment Run is pressed, before the compile |
| W-10 | low | Windows | Help > Environment and Find in Files overwrite a running program's Console |
| W-11 | low | Windows | Recent files show bare names, and only files opened by File > Open or Save As are remembered |
| W-12 | low | Windows | No right-click menu on the project tree; Move to Group is unreachable |
| W-13 | low | Windows | Build > Clean is allowed while the debugger is stopped at a breakpoint |
| W-14 | low | Windows | Three file dialogs are shown without an owner |
| R-01..R-05 | low | Windows | Robustness: nested message pump, worker-thread MessageBox, blocking send, index-numbered menu items, one shared bridge buffer |
| X-01..X-04 | - | both | Menu and shortcut differences, listed for the record |

Counts by severity: **high 2, medium 5, low 8** defects (plus 5 robustness notes and 4 mismatch
lists). By window: **Windows-only 14** (W-01..W-14), **both 1** (B-01), **macOS-only 0** - the
02-10 audit's fixes were re-read and hold; what they left is that none of them was carried to
Windows (W-03, W-05, W-06, W-07, W-08, W-09, W-10, W-11 are exactly audit items 3, 4, 23/24, 17,
10, 15, 21 and 26 on the other window).

## Findings, most severe first

### W-01. File > Open or File > New hides the project tree for good - high, Windows - reading

`winforms/MainForm.h:2830-2862` (`FillTree`): the pane shows *either* the open files *or* the
project's groups - `if (paneMode_ == PaneMode::PaneFiles || ride_project_loaded(project_) == 0)`
lists the sheets, else the groups. `paneMode_` is set to `PaneFiles` by `OnOpenFile`
(`MainForm.h:3694`, **before** the dialog is shown, so Cancel sets it too) and by `OnNewBuffer`
(`:3024`), and set back to `PaneProject` only by `LoadProject` (`:2781`), `OnNewProject` (`:3515`)
and the constructor (`:471`). Nothing else restores it.

Reproduction: open any project (the tree shows its groups). File > Open... and open a header, or a
file from another folder - or just Cancel and then click any file in the tree. The next
`PaneFollowsTabs`/`FillTree` (`:1904`, `:3742`) rebuilds the pane as a flat list of open tabs. The
project's groups are gone until Project > Recent reopens the project.

The macOS window shows both at once - a project section and an OPEN FILES section
(`macos/WindowController.mm`, `fillNavigator`). Mend: either do the same on Windows (two root
nodes, as the navigator has), or never leave `PaneProject` while a project is loaded, and never
change the mode before a dialog has been confirmed.

### W-02. Target and Tools on a CCS project: reported written, then lost - high, Windows - reading

`MainForm.h:5333-5344` (`OnTarget`) and `:5345-5355` (`ChooseTool`) call `ride_project_set_arch` /
`ride_project_set_toolchain` for any loaded project and append `WrittenToProject(...)` - " -
written to settings.json" for a CCS project, since `OutcomePath()` is the settings file. In the
core, `Project::save` (`src/project.cpp`, the `ccs_` branch) writes only the project's `"open"` file
to `settings.json` and nothing of `arch_` or `toolchain_`; `ride_project_target_ready`
(`bridge.cpp:1613-1637`) reloads a CCS project from its own files before every build
(`reloadIfCcs`), so the choice is overwritten. The status line claims the opposite.

The 02-10 audit found this on macOS (item 9) and the macOS window now refuses `chooseArch:`,
`chooseTool:`, `nextTarget:` and `nextCompiler:` for a CCS project (`WindowController.mm:3903-3915`).
On Windows `RefreshProjectMenu` (`MainForm.h:3405-3434`) disables the *file* items for a CCS project
but the Target and Tools menus, and their Ctrl+T / Ctrl+K keys (`ProcessCmdKey`, `:494-496`), are
untouched.

Reproduction: Project > Recent > Sample (the CCS sample), Target > x86_64-windows: status says
"target: x86_64-windows - written to settings.json"; Build project: the build is for tms6747 again.
Mend: gate the Target and Tools items and the two keys on `ride_project_is_ccs`, as macOS does,
saying "change it in CCS"; or make `Project::save` keep `arch`/`toolchain` in the CCS project's
state and `reloadIfCcs` apply them.

### W-03. Close Project keeps the closed project's folder - medium, Windows - reading

`OnCloseProject` (`MainForm.h:3590-3634`) calls `ride_project_close` and
`TakeInstallationSettings`, but never clears `projectDirectory_`. `RootNow()` (`:1433-1437`) falls
back to `projectDirectory_` when the core's root is empty, so `SayWhere()` (`:1496-1499`, called
from `AfterSheetsGone`) puts the closed project's folder back in the status bar's root label, and
`OnNewFile` (`:2909-2913`) sees a non-empty root and takes the in-project branch (`ride_create_file`
on an unloaded project, refused with the core's message) instead of the "new program under
Documents" branch - though the menu item is disabled, so only the path shows. The same field also
survives a failed load on purpose (`:2768-2771`), which is fine; the close is the leak. This is
audit item 3, fixed on macOS, not on Windows.

Reproduction: open a project, Project > Close: the status bar's right-hand label still names the
project's folder. Mend: `projectDirectory_ = nullptr` in `OnCloseProject` before `AfterSheetsGone`.

### W-04. Remove is enabled by the tree's selection and acts on the file in front - medium, Windows - reading

`RefreshProjectMenu` (`MainForm.h:3410-3425`) enables `projRemove_` when `TargetFile()` - the
tree's selected node, else the file in front - is held by the project. `OnRemoveFromProject`
(`:3168-3179`) ignores `TargetFile()` and removes `path_`, the file in front. Rename and Delete use
`TargetFile()` (`:3038`, `:3092`), so the three differ.

Reproduction: a project with `a.c` and `b.c`; click `b.c` in the tree (it opens and is selected),
Ctrl+PageDown to `a.c`'s tab (the tree still highlights `b.c`). Project > Remove: `a.c` leaves the
project while the tree's highlight says `b.c`. With `a.c` a file outside the project the item is
enabled and the core answers that it is not in the project. Mend: remove `TargetFile()`, and enable
on the same test - the macOS window does both (`removeFromProject:` and its validation use
`targetFile`).

### W-05. Unbounded Console output, one UI message per chunk - medium, Windows - reading

`OutputToWindow` (`MainForm.h:5486-5496`) copies every chunk and `Post`s it; `Post` (`:510-515`)
does one `BeginInvoke` per chunk; `Heard` (`:4197-4206`) decodes and `Say` (`:3970-3974`) appends it
to the Console and scrolls. There is no cap on the Console and no coalescing. A program that prints
without end - `for (;;) printf("hi\n");` - queues BeginInvokes faster than the window drains them;
the TextBox grows without bound, every append re-measures a longer text, and Build > Stop's own
BeginInvoke queues behind what is already posted. Audit item 4 on macOS, fixed there by collecting
under a lock, draining on a 40 ms timer and capping Output near 2 MB (`drainOutput`,
`WindowController.mm:3003-3035`).

Mend: the same shape - the worker appends to a locked string, one timer drains it, the Console is
capped at its last 1-2 MB, and Stop clears the queue.

### W-06. An open file changed on disk is never re-read - medium, Windows - reading

`OpenPath` (`MainForm.h:3712-3718`): a path already open just selects its tab. No sheet keeps a
modification stamp and nothing re-reads on activation. So: Language > Convert run a second time
opens the already-open converted file and shows the first conversion's text (`OnConvert`, `:5440`);
a file edited outside RIDE (CCS, a generator, a `git checkout`) is shown stale and a save from RIDE
writes the stale text over it; an error that opens a header shows what was read earlier. Audit items
23 and 24 on macOS, fixed there with a per-sheet stamp (`changedOnDisk:`,
`WindowController.mm:1439-1443`, re-read in `openPath:` when unmodified).

Mend: record the file's write time in `Sheet` on read and write; in `OpenPath` re-read an
unmodified sheet whose stamp moved; on `Activated` offer to reload a modified one.

### B-01. Find in Files runs on the UI thread with no Stop - medium, both - reading

Windows `OnFindInFiles` (`MainForm.h:4117-4122`) and macOS `findInFiles:`
(`WindowController.mm:3344`) both call `ride_find_in_files` synchronously on the window's thread
under a wait cursor. `editor::findInFiles` (`src/find.cpp:194-214`, `walk`) stops at 2000 hits but
otherwise walks the whole tree; a search for a rare word with Look in set to `C:\` or `/`, or a
project folder holding a build tree, freezes the window until it ends, and Build > Stop is not
reachable. Mend: run it as a `Job` on the worker (`WhileBusy` already pumps the window) with
`stopped` honoured by the walk, or at least a file count and cancellation through the existing
`ride_cancel_builds` path.

### W-07. Undo back to the saved text keeps the modified mark - low, Windows - reading

`MarkTab` (`MainForm.h:3861-3866`) and `MayDiscard` (`:3870-3888`) read the Rich Edit `Modified`
flag, which stays set once anything was typed. Type a character, Ctrl+Z: the tab still shows `*`
and closing asks to save an unchanged file. Audit item 17, fixed on macOS by comparing with the
text last on disk. Mend: keep the saved text (or its hash) on the `Sheet` and compare in `MarkTab`.

### W-08. Open, Recent and Save As are refused while a program runs - low, Windows - reading

`Lay` (`MainForm.h:862-863`) adds File > Open... without putting it in `live_`, so `GateBelow`
(`:4498-4505`) gates it, and `SetBusy(true)` (`:4490-4496`) disables it; `StartedRunning`
(`:4191`) sets busy for the whole run of a program. The Recent submenus are gated the same way
(`RefreshProjectMenu`, `:3433` for projects; `GateBelow` for files), and `OnSaveAs` (`:3821`)
refuses with "still working". A program waiting on `scanf` can run for minutes; nothing of these
touches the core the run holds. Audit item 10 on macOS, fixed there: a file opens while a program
runs, not while a build reads (`validateMenuItem`, `WindowController.mm:3986`). Mend: a `running_
!= nullptr && job_ == nullptr` state in `SetBusy` that leaves Open, Recent files and Save As enabled.

### W-09. The input line takes the keyboard before the compile - low, Windows - reading

`StartedRunning` (`MainForm.h:4192-4194`) enables and focuses `input_` as soon as Run is pressed,
while the compiler is still running; keys meant for the editor go to the program's input. Audit item
15, fixed on macOS by handing the keyboard over when the program first prints (`:4204-4205` here
already changes the status line on first output, so the hook exists). Mend: focus `input_` in
`Heard` on the first non-build chunk.

### W-10. Help > Environment and Find in Files overwrite a running program's Console - low, Windows - reading

Help and Edit stay live while a program runs (`live_`, `MainForm.h:1224-1226`). `OnEnvironment`
(`:3956`) and `OnFindInFiles` (`:4123`) assign `console_->Text`, discarding the program's output so
far; the next chunk the program prints is appended under the report. Audit item 21, fixed on macOS
by appending. Mend: append under a running program, or refuse with the status line.

### W-11. Recent files: bare names, and only two ways in - low, Windows - reading

`RefreshRecentFiles` (`MainForm.h:3465`) shows `GetFileName(where)` with the full path only in the
tooltip, so two `main.c` entries read the same (audit item 26; macOS shows the name and its two
folders). And `ride_remember_file` is called from `OnOpenFile` (`:3707`) and `OnSaveAs` (`:3856`)
only: a file opened from the tree, the command line, a Console error line or the Recent menu itself
is not remembered or moved up, where macOS remembers in `openPath:` (`WindowController.mm:1477`).

### W-12. No right-click menu on the project tree - low, Windows - reading

The macOS navigator has a context menu - New File, Add Files, Rename, Move to Group, Remove,
Delete, Show in Finder (`WindowController.mm:802-811`). The Windows `tree_` has none
(`MainForm.h:1252-1267`); `OnMoveToGroup` (`:3124-3139`) exists and is reachable from nothing. A
mismatch rather than a fault, listed here because a whole handler is dead code.

### W-13. Clean is allowed while the debugger is stopped - low, Windows - reading

`OnClean` (`MainForm.h:4899-4915`) checks `busy_` only; a debugger stopped at a breakpoint is not
busy (`WhileBusy` returned). Clean then removes the target program the debugger is running
(`ride_project_clean` → `cleanBuilt`, `src/compile.cpp:993-1040`), empties the Debug tab that
holds the stop, and reports "Clean succeeded" - on Windows the running exe is locked so the delete
fails silently and is left out of the count, on an emulated target the `.vm` folder goes. The same
`busy_`-only check is on macOS (`cleanBuild:`, `WindowController.mm:2508`) - a `B-` item in effect,
listed under Windows because the debugger is what Windows has. Mend: refuse while
`ride_debugger_running`.

### W-14. Dialogs without an owner - low, Windows - reading

`OnOpenProjectFile` (`MainForm.h:3531`), `OnSaveProjectAs` (`:3568`) and `OnOpenFile` (`:3699`)
call `pick->ShowDialog()` with no owner, where every other dialog in the file passes `this`. An
unowned common dialog can open behind the window and is not centred on it. Mend: `ShowDialog(this)`.

## Robustness and code quality, Windows

**R-01. The worker is waited for with a nested message pump.** `WhileBusy`
(`MainForm.h:4460-4482`) loops `Application::DoEvents()` until the thread joins. Everything not
gated runs re-entrantly inside a build - tab closes, saves, the context menu, `OnFormClosing`,
Help. The gating (`live_`, `gated_`, `GatedKey`) is careful and nothing read here slips through,
but the shape means every new menu item has to be classified, and a missed one reaches the core
under the worker. A `BackgroundWorker`/`Task` with completion marshalled back, as the run path
already does with `Post`, would retire the classification.

**R-02. The native-tools question is a MessageBox on the worker thread.** `AskNativeInWindow`
(`MainForm.h:45-50`) is called by the core from the build thread (`bridge.cpp:938-947`). It works -
a MessageBox pumps its own loop - but it is modal to nothing, can open behind the window, and the
main thread's `DoEvents` loop keeps the window live under it. Marshal the question with `Invoke`.

**R-03. Enter in the input line can block the UI thread.** `ride_running_send`
(`bridge.cpp:2087-2097`) holds the `input` mutex through `Process::send`; a program that has
stopped reading with a full pipe blocks the UI thread in `OnInputKey` (`MainForm.h:4217`) until
Stop - which is a menu item on that same blocked thread. Low likelihood with a pseudo-console's
buffer; worth a send with a timeout or a queue.

**R-04. The editor's context menu is addressed by index.** `OnEditMenuOpening`
(`MainForm.h:1826-1831`) enables items 2, 3, 5, 6, 7 and 9 by position; inserting one item breaks
all of them silently. Keep the items in fields.

**R-05. One shared answer buffer across the seam.** `scratch()` (`bridge.cpp:114-117`) is the
return buffer of some fifty bridge functions, and `ride_parse_diagnostic` keeps two more statics.
The header says one thread at a time per `RIDEProject`; nothing today calls a `scratch()` user from
the worker, but `ride_find_header` and `ride_recent_*` are called on the main thread while the
worker builds, so the first worker-side `scratch()` call added later is a race with no symptom but
a wrong string. A thread_local, or per-caller buffers, costs nothing.

**Also noted, not faults:** `RememberedFont` defaults to Courier New 14 where macOS keeps 18 - a
decision recorded in `8e0734a`, not a drift. `Program.cpp` opens a new window per launch (no single
instance), so a second `.pro` double-clicked is a second RIDE; macOS is one process by nature.

## Menu and shortcut mismatches, for the record

**X-01. Items one window has and the other does not.** macOS File: Save All, Revert to Saved, Show
in Finder; Edit: Find and Replace (one dialog), Use Selection for Find, Go to Line, Shift Left,
Shift Right, Comment Selection; View: Bigger/Smaller Font, Actual Size, Enter Full Screen; Build:
Convert C ⇄ Shalimar, Jump to Next Issue, Clear Issues; Option: Show Tools in Use; a Window menu.
Windows only: Tools > Settings file..., Locate vcvars64.bat... (Windows needs it), Help > Contents;
Language > Convert (c2s / s2c) where macOS has it under Build; Debug > Step out, Up/Down the
stack, Watch expression (macOS's Debug menu was not compared line by line).

**X-02. Clean clears different panes.** Windows empties Console, Debug and Assembly
(`MainForm.h:4903-4905`); macOS empties Output and the progress log and clears the issues
(`WindowController.mm:2512-2514`) and leaves the assembly.

**X-03. Recent lists.** Both windows keep three entries (the core's cap); the macOS menus are
filled on open and skip entries whose file is gone (`menuNeedsUpdate:`); Windows fills them on
load/save (`RefreshRecent`) and the core already drops missing ones. Equivalent.

**X-04. Shortcuts** follow each platform's convention (Ctrl+B / Cmd+B Compile, F5 / Cmd+R Run, F4
/ Cmd+Shift+B Build, Ctrl+Break / Cmd+. Stop) and were not counted as mismatches; the Keys window
on each lists its own.

## The 02-10 macOS audit, re-read

The thirty-three items and their "State after the fixes" table were checked against the code at
`9f9ec81`. Items 1 (the build thread no longer reloads a CCS project - `prepared` in
`bridge.cpp:247-250`, `:1613-1637`, `:1730-1735`), 3, 4, 5, 6, 7, 8, 9, 10, 17, 21, 23, 24, 28 and
32 are implemented as the table says. No neighbour was found broken by them on macOS; the Windows
window, which shares the bridge, took the bridge halves (item 1, item 6's "load on a project of its
own", `LoadProject`, `MainForm.h:2754-2779`) and none of the window-side halves - that is the
list W-03, W-05, W-06, W-07, W-08, W-09, W-10, W-11 above. Two bridge details worth keeping: item
7's Save As rule is enforced on Windows by refusing Save As while busy (`:3821`) rather than by
adopting afterwards, which is stricter and safe; and `ride_project_target_ready` is called on the
window thread before every project build and debug on Windows (`:4271`, `:4607`), which is the
order item 1 needs.

## What was checked and found sound

- **Start and faults.** `EarlyWatch` (`bridge.cpp:354-363`) watches from the first native
  initialiser; `Program.cpp` logs each stage to `%TEMP%\RIDE.log`, routes handler exceptions to a
  message and the log, keeps 32 KB of stack for the fault handler; `--version` exits before any
  window. Measured: the log shows every 4.7 run reaching "window built, running" and the user's
  runs "closed cleanly".
- **Threads and the core.** The worker never reads window fields (`Job` pins everything first,
  `MainForm.h:102-138`); `busy_` gates every item that reaches the core; `RefreshTitle` keeps the
  name read before the build; `FillTree` is deferred while busy (`treeStale_`); a program's output
  goes through `Post` → `BeginInvoke` and never waits on the window; `EndedRunning` waits and
  joins after the worker has signalled, not from inside the callback; `ride_running_stop` takes
  `state` and never waits; the two mutexes are taken in one order.
- **Build, run, stop.** Every dirty named file is saved before a project build; a part that the
  chosen compiler cannot compile is refused by name before anything starts; a build that fails
  with a diagnostic opens the file it names (relative to the source, then the project) and goes to
  the line; Stop ends builds through `cancelBuilds` and programs through a job object
  (`src/process.cpp:716-720`), and the debugger's children through `StopChildren`; closing while
  busy asks, stops, and closes when idle; "Build succeeded"/"Compilation succeeded" lines name the
  program and the count.
- **Project open paths.** A `.pro`, a folder, a CCS project folder or any of its three files, a
  file inside a CCS workspace, and a workspace folder from Recent all reach `LoadProject`; a
  workspace asks which project through `Pick` and writes `<workspace>\<project>.pro` once; a load
  is tried on a project of its own so a failed one leaves the open project alone; the CCS report
  is kept on its own Console line; `recentProjects()` folds a CCS project's three files into its
  folder (`settings.cpp`, `projectEntry`) and never shows a blank entry.
- **settings.json and state.** Per user at `C:\Users\<you>\RIDE 4.7\settings.json`, begun from
  the installation's and the previous release's (`writeInstallFileIfAbsent`); state in
  `~\.ride\state.json`. Measured: both exist on the box with sane contents, recent lists of three,
  the CCS projects' `open`/`config` kept.
- **Files.** `TextFile` keeps encoding, BOM and line ending, writes beside the file and replaces;
  a NUL file is refused; one tab per path; Save As refuses a name another tab holds; the first
  untitled sheet is reused or dropped so an empty start has no phantom tab.
- **Find/replace, Console navigation, headers.** `Seek` uses byte columns through the core and
  maps them back to characters; Replace is one undo step; Enter or double-click on a Console line
  goes to `file:line:col`, `file(line,col)` and to a bare path (Find files); the editor's
  right-click "Open "x.h"" resolves through the file's folder, the project, its include paths, the
  shared ones and the installation's.
- **Command-line arguments.** The Build submenu's box commits on Enter and on close, to the `.pro`
  or to the CCS project's state; the run line is shown before the program runs; a word naming a
  file under the base goes by its full name.
- **Suites.** On the box: unit 1267/0, session 321/0, the window builds with 0 warnings.

## The Windows session's walk-through

The other Claude session on the Windows box was asked (through `NOTE-FROM-MAC.md`) to drive the
installed RIDE 4.7 window item by item with screenshots and to write
`C:\Users\GRA\Developer\Claude\gui-review-2026-10-03\REPORT.md`, announced in `NOTE-TO-MAC.md`.
At the time of writing (17:55 on the box) the folder exists and is empty - made at 17:51, no
`REPORT.md`, no pictures - and there is no `NOTE-TO-MAC.md` (checked over ssh, twice); the
window's log shows RIDE being started from `C:\WINDOWS\system32` at 16:17, 16:20, 16:20 and twice
at 17:13-17:14 today, which is that session's scripted driving under way. Its findings are
therefore not merged here; they should be read against this list when they land. When its report lands,
each of its findings should be set against the list above: W-01, W-02, W-03, W-04, W-09 and W-10
are the ones a live walk would see first, and a disagreement with any of them is a reason to doubt
the reading here rather than the walk.

What that session found earlier, in its RIDE 4.51 audit
(`C:\Users\GRA\Developer\Claude\RIDE-4.5-ERROR-REPORT-2026-10-02.md`, credited here): E2 -
`settings.json` written under Program Files - is closed by the per-user file above (measured);
E1 (the two Windows-only unit checks) is closed - 1267/0 today; E3 (the installer's HKCU PATH
under admin), E4 (the Store `python` stub), E5 (`NoDefaultCurrentDirectoryInExePath`) and E6
(`--version` on masm/link/lnk6x) are installer and tool items outside this review's windows and
were not re-measured; E7-E10 were compiler fixes, since merged.
