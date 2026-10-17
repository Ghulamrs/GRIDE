# RIDE 4.5 — WinForms editor (C++/CLI) code review

**Scope:** `winforms/MainForm.h` (4215 lines), `winforms/bridge.h`, `winforms/bridge.cpp`, `winforms/Program.cpp`, with `README.md` as the statement of intended behaviour. Read-only review; nothing was modified. The native core (`compile.cpp`, `syntax.cpp`, `project.cpp`, …) was not available, so anything that depends on its behaviour is marked **Suspected**.

Line references are `file:line` against the copies reviewed.

---

## Executive summary

The window is a thin C++/CLI shell over a C bridge, and the bridge itself is clean: consistent naming, bounds checks on every indexed accessor, `static_assert`s pinning enum drift, and a deliberate (and well-commented) avoidance of destructor-bearing statics in the mixed-mode image. The form does most of what the README says it does, and the undo-suspension trick around colouring, the canonical-path tab reuse and the deferred recolour are all real, working solutions to real problems.

The serious problems are structural rather than local:

1. **Threading is unsafe by construction.** `WhileBusy` runs debugger/build work on a background thread while pumping `Application::DoEvents()` on the UI thread. Only three handlers check `busy_`; every other menu item, key and the window's close button remain live. Toggling a breakpoint, pressing *Stop debugging* or closing the window while the program is running under the debugger touches the same native `RIDEDebugger` from two threads, and closing the form frees `project_`/`debugger_` under the worker.
2. **Ordinary builds and runs are synchronous on the UI thread with no cancellation.** F5 on a program with an infinite loop (or one that reads stdin from the hidden console) freezes the IDE until Task Manager.
3. **Save is lossy.** Ctrl+B/F5/F8 save unconditionally, and save always writes UTF-8/LF, so any CRLF, Latin-1, BOM'd or binary file that is merely *opened* is silently rewritten the first time it is built.
4. **Undo is not what the UI promises.** Replace-all and Re-indent go through the `RichTextBox::Text` setter, which discards the undo stack; the status bar then says "Ctrl-Z puts them back".
5. **Editing performance is O(file) per keystroke** on large files: the lexer-state cache is invalidated by the caret-move event that every keystroke raises, and several handlers materialise the whole document (`Text`, `Lines`) on every key.

**Overall verdict: not ready for release as-is.** The core-facing design is sound and most fixes are local, but the two threading findings (one of which is a use-after-free on a normal user action) and the lossy save need to be fixed before this is put in front of users. Estimated: the Critical/High list is a few days of focused work; the Medium list is a further one to two weeks.

**Counts:** Critical 2 · High 5 · Medium 15 · Low 14.

---

## Findings, most severe first

### CRITICAL

#### C1. Nested message loop lets the UI race the debugger worker; closing the window frees native objects under it
**Where:** `MainForm.h:3388-3404` (`WhileBusy`), `3422-3445` (`OnToggleBreak`), `3651-3672` (`OnDebugStop`/`EndDebugging`), `118-126` (`OnFormClosing`), `160-179` (`!MainForm`), `2641-2680` (`OnCloseProject`).

**What is wrong.** `WhileBusy` starts a `Thread` running `DoPendingWork` and then spins `while (!worker->Join(50)) Application::DoEvents();`. During that loop the whole window is live. `busy_` is only checked in `OnCompile`, `OnRun`, `BuildProject`, `OnConvert` and `WhileBusy` itself. Everything else runs concurrently with the worker:

- `OnToggleBreak` → `ride_debugger_break(debugger_, …)` while the worker is inside `ride_debugger_run/step_*` on the same `RIDEDebugger` (two threads writing gdb's stdin / reading its stdout).
- `OnDebugStop` → `EndDebugging` → `ride_debugger_stop` (kills the session, clears `locals`/`stack` vectors) while the worker's `afterMoving` (`bridge.cpp:1044-1058`) is assigning those same vectors. Concurrent `std::vector` assignment/clear is undefined behaviour.
- `OnCloseProject` → `ride_project_close` and `OnSheetChanged` → `ride_project_name` (writes `project->answer`) while the worker's `ride_build_target` → `ride_project_target_ready` mutates `project->parts/sources/why/program`.
- `OnExit`/close box → `OnFormClosing` → form disposed → `!MainForm` frees `project_`, `built_`, `targetBuilt_`, `debugger_` while the worker still holds them. When `Join` finally returns, `Debug()` continues into `ShowStop()` touching disposed controls.
- `DoPendingWork` reads managed fields (`path_`, `cc1_`, `arch_`, `workProgram_`) from the worker thread with no synchronisation; `OnSheetChanged` on the UI thread rewrites `path_`.

**Failure scenario.** F8 on a program that loops or waits. The status bar says "starting the program…", the UI is responsive. The user, quite reasonably, chooses *Debug ▸ Stop debugging* (or presses F9, or closes the window). Result ranges from a hung gdb pipe to heap corruption to an access violation in `RIDEGui-fault.log`.

**Fix.** Either (a) make the debugger session genuinely asynchronous (worker thread + `BeginInvoke` back to the UI, with the UI disabled to a defined subset while a step is in flight), or (b) keep the nested loop but gate *every* command on `busy_` and refuse `OnFormClosing` while busy (`e->Cancel = true` with a message). In both cases stop the debugger in `OnFormClosing`, not in the finalizer, and never free native handles the worker can still reach. Long-term, the bridge needs an explicit "session" object with a cancel entry point (see M12).

#### C2. Compile, run, build-project, run-built and convert block the UI thread with no cancel, timeout or stdin handling
**Where:** `MainForm.h:3023` (`ride_build`), `3122` (`ride_run`), `3229` (`ride_build_target`), `3287` (`ride_run_built`), `4172` (`ride_convert`); `Program.cpp:37-48` (`QuietConsoleForChildren`).

**What is wrong.** Each of these calls a blocking core function directly from the event handler after a single `Application::DoEvents()`. The window stops repainting (Windows shows "Not Responding") until the child exits. There is no kill button, no timeout, and the child inherits the process's hidden console: `freopen_s` on the CRT `FILE*`s does not change the Win32 standard handles, so a program that calls `scanf`/`getchar` blocks on a console nobody can see or type into. The README says of the macOS window "Builds run off the main thread"; the Windows window does not.

**Failure scenario.** F5 on `while(1);` or on any program that prompts for input → the IDE is frozen; unsaved work in other tabs is lost when the user kills it from Task Manager. (Ctrl+B autosaved only the file in front.)

**Fix.** Run all five through the same worker path as the debugger (once C1 is fixed) with a *Stop* menu item that kills the child process tree; give the child a real (or `NUL`) stdin via `STARTUPINFO` rather than the inherited console; stream output back with `BeginInvoke` rather than assigning `console_->Text` once at the end.

### HIGH

#### H1. Saving is unconditional on build/run/debug and always rewrites the file as UTF-8 + LF; non-UTF-8, CRLF, BOM'd and binary files are silently altered
**Where:** `MainForm.h:2745` (`ReadAllText`), `2819-2836` (`OnSave`), `2976`, `3070`, `3511`, `4139` (unconditional `OnSave` from Compile/Run/Debug/Convert), `3298-3311` (`SaveEveryDirty`).

**What is wrong.**
- `OnSave` never checks `Modified`; `OnCompile`/`OnRun`/`Debug`/`OnConvert` call it unconditionally. Merely opening a file and pressing Ctrl+B rewrites it.
- `File::ReadAllText(path)` decodes with BOM detection defaulting to UTF-8. A Windows-1252 source with `é` in a comment becomes U+FFFD in the box; `WriteAllText` then writes the replacement character back. A UTF-16 file is read correctly and written back as UTF-8; a UTF-8 BOM is dropped.
- `Replace("\r\n","\n")` before writing means every CRLF file becomes LF on first save — a whole-file diff in version control.
- Any file with a NUL byte (a `.obj` picked from the *All files* filter) is truncated at the NUL by the RichTextBox and then written back truncated.

**Failure scenario.** Open `legacy.c` (CP1252, CRLF), press F5 to see what it does. The file on disk is now LF with `?`-boxes where accented characters were, and `git status` shows every line changed.

**Fix.** Save only when `Modified` (or when the build explicitly needs it *and* it is modified). Detect encoding and line-ending style on load, remember them on the `Sheet`, and reproduce them on save; refuse (or warn on) files containing NUL. Write via temp file + `File::Replace` so a failed write does not truncate the original.

#### H2. Replace-all and Re-indent replace the whole document through `Text =`, which discards the undo history — contradicting the status bar and README *(Suspected: depends on RichTextBox `Text` setter semantics, which are documented to clear the undo buffer)*
**Where:** `MainForm.h:1394`, `1404` (`OnLayOut`), `1811-1816` (`OnReplace`: "… Ctrl-Z puts them back"); README lines 322-325.

**What is wrong.** Assigning `RichTextBox::Text` streams the text in (`EM_STREAMIN` without `SFF_SELECTION`), which empties the rich-edit undo stack. After Re-indent or Replace, `CanUndo` is false and *Edit ▸ Undo* says "nothing to undo". `OnLayOut` also loses the caret's scroll position, the stopped-line background and per-run colours outside the viewport. `UseFont` (`2125-2135`) has a milder version: `WM_SETFONT` on a rich edit reformats all text.

**Failure scenario.** Ctrl+H, replace `i` with `index` across a 2000-line file, realise it also hit `if`/`int`/`while`. Ctrl+Z: "nothing to undo". Same after a Re-indent that the user disagrees with.

**Fix.** Replace text with `SelectAll(); SelectedText = …` (one undoable step), or for the selection branch, select the affected line range and replace only it. Wrap in the existing `BeginColouring`-style scroll/redraw preservation. Add a regression check that `CanUndo` is true after each operation.

#### H3. `OnKeyUp` re-indents the current line on *any* key release, including arrow keys, and on every `:` typed anywhere
**Where:** `MainForm.h:1842-1860`.

**What is wrong.** The handler never inspects `e->KeyCode`. It looks at the character *before the caret* and, if it is `}` or `#` at the start of the line, or `:` anywhere, calls `Realign(row)`, which rewrites the line's leading whitespace to the core's opinion and moves the caret. So:
- Pressing → to move past a `}` re-indents that line. Pressing Shift, Ctrl, Alt or releasing a modifier after Ctrl+V does the same check.
- Typing `:` inside a string (`"http://"`), a ternary, a C++ `::`, a JSON key, or an assembler label re-indents the line. There is no leading-whitespace check for `:` (line 1854).
- Files edited under the *Plain text* / *JSON* language still go through the C indenter (`DialectNow()` only distinguishes Shalimar).

**Failure scenario.** Open a JSON file, use the arrow keys to move through `"key": value` lines. Lines whose indentation differs from what the C indenter would produce are rewritten and the tab gains a `*` without anything having been typed. In C, a deliberately aligned continuation line ending in `? a : b` snaps back when the user types the `:`.

**Fix.** Handle auto-indent in `KeyPress` (which gives the typed character), only for `}`, `#` and `:`, only when the language is C/C++/Shalimar, and for `:` only when the line's first token is `case`/`default`/a label. Never realign on navigation keys.

#### H4. Per-keystroke cost is O(file size): lexer-state cache is defeated, and several handlers materialise the whole document on every key
**Where:** `MainForm.h:1213-1233` (`OnTextChanged`), `1624-1648` (`RecolourLine`), `1862-1866` (`OnCaretMoved` sets `stateGood_ = false`), `1842-1851` (`OnKeyUp`: `text_->Text[caret-1]`, `text_->Lines[row]`), `1035-1041` (`LeadingOf`), `1261` (`OnGutterPaint`: `box->Lines->Length`), `1438`, `1826` (`WholeText()` on Enter / Tab / `}` / `:`).

**What is wrong.**
- README §"While you type" says the lexer state at the start of the typed line is remembered. It is (`stateAt_`), but `OnCaretMoved` clears `stateGood_` whenever `SelectionChanged` fires outside a colouring pass — and every typed character moves the caret. Whether `TextChanged` or `SelectionChanged` fires first, the cache is invalid by the next keystroke, so `RecolourLine` re-lexes every line above the caret every time, allocating two managed arrays and one UTF-8 conversion per line.
- `OnTextChanged` calls `sheet->box->Lines->Length` twice (each builds a fresh `String[]` of the entire document). `OnGutterPaint` (invalidated on every text change *and* every caret move) calls it again. `OnKeyUp` fetches `text_->Text` (entire document as one string) and `text_->Lines` (entire document as an array) on every key-up. `CaretRow()`/`LeadingOf` re-fetch `Lines`.
- Enter, Tab-in-leading-space, and typed `}`/`#`/`:` serialise the whole document to UTF-8 and hand it to `ride_indent_*`, which splits it into a `vector<string>` again.

**Failure scenario.** A 30 000-line generated header: each keystroke performs ~5 full-document copies (tens of MB of allocation), a 30 000-line native lex with 60 000 managed allocations, and a gutter repaint that does it again. Typing lags visibly; the GC runs continuously.

**Fix.** Invalidate `stateGood_` only when the *text* above `stateRow_` changes (compare `row < stateRow_` in `OnTextChanged`), not on caret movement. Cache `Lines->Length` on the `Sheet` and update it from `TextChanged`. In `OnKeyUp`/`KeyPress` use `GetLineFromCharIndex` + `text_->Lines[row]` only when the typed char is one of the three triggers. Give the bridge an incremental indent entry that takes the lines above the caret, or at least a windowed one. Consider `RichTextBox::GetLineText`-style access via `EM_GETLINE` instead of `Lines`.

#### H5. Null-dereference crashes when no sheet is open; no `ThreadException` handler, so they surface as the WinForms crash dialog
**Where:** `MainForm.h:3756-3764` + `3970-3981` (`GoToError`/`GoTo` use `text_`), `3671` (`EndDebugging`: `Current()->gutter`), `3703`, `3736` (`ShowStop`), `3962` (`LookAt`), `2781-2800` (`CloseSheet` sets `text_ = nullptr` but never `ForgetError()`); `Program.cpp:69-70` (only `UnhandledException` is hooked).

**What is wrong.** `CloseSheet` legitimately leaves the environment with `text_ == nullptr`, but several handlers assume a current sheet. `errorMessage_` survives closing the tab, so `GoToError` calls `GoTo`, which dereferences `text_`. `EndDebugging`/`ShowStop`/`LookAt` call `Current()->gutter` where `Current()` returns `nullptr`.

**Failure scenario.** Ctrl+B on a file with an error → Ctrl+W to close it → Ctrl+1 → Enter: `NullReferenceException` in `GoTo`. Or: start debugging, close every tab while the program runs (allowed, see C1), *Stop debugging* → NRE in `EndDebugging`. With no `Application::ThreadException` handler the user sees the generic "Unhandled exception… Continue/Quit" dialog.

**Fix.** `ForgetError()` and clear stop state in `CloseSheet`; guard `GoTo`, `EndDebugging`, `ShowStop`, `LookAt` on `text_ != nullptr`/`Current() != nullptr`; install `Application::ThreadException` in `Program.cpp` so a handler exception is logged and reported without the default dialog.

### MEDIUM

#### M1. The Language menu override is one global, not per file
**Where:** `MainForm.h:1358-1365` (`languageChoice_`), `4116-4122` (`ChooseLanguage`), `1176-1193` (`OnSheetChanged` does not touch it).
README: "The suffix decides, and the Language menu overrides it." Choose *Shalimar* for `weird.txt`, switch to `main.c`: `main.c` is coloured, status-barred and *compiled* as Shalimar (`LanguageNow()` is used by `OnCompile`). Store the override on `Sheet` and read it through `LanguageNow()`.

#### M2. `FillTree()` re-derives `toolKind_`, `config_`, `arch_` and indent settings on every tab open/close, so *Tools ▸ By language* silently reverts, and it clobbers the status message of whatever called it
**Where:** `MainForm.h:2008-2018`; callers `1966-1971` (`PaneFollowsTabs` ← `MakeSheet`, `OpenPath`, `CloseSheet`, rename, delete), `2227`, `2262`, `2279`, `2301`, `2319`, `2867`.
Line 2011: `toolKind_ = project toolchain != AUTO ? project : ride_default_compiler()`. Choosing *By language* writes AUTO to the `.pro`, and the next `FillTree` maps AUTO back to the installation default (say c90), so the menu tick and the compiler change under the user the moment they open another file. Line 2017 then sets `what_->Text = "ready - …"`, overwriting the outcome messages `Did()` just set in `OnRenameFile`, `OnMoveToGroup`, `OnAddThisFile`, `OnRemoveFromProject` and `OnSaveAs` ("… written" is never seen). `Nodes->Clear()`+`ExpandAll()` on every tab switch also loses the tree's selection and scroll. Split tree refresh from settings load; keep AUTO as AUTO.

#### M3. Debugger stop location is matched by file *leaf name* only, and stopping in a file that is not in front does not bring it forward
**Where:** `MainForm.h:1280-1282`, `1292-1294` (gutter), `1505-1506` (`PlaceStopBar`), `3725-3729` (`ShowStop`).
`src/util.c` and `tests/util.c` both show the green arrow and blue bar; `GoTo` fires in whichever is current. And unlike `LookAt` (which does `OpenPath`), `ShowStop` only navigates when `path_` already has the same leaf name — a breakpoint in another file leaves the editor showing nothing while the Debug tab reports the stop. README: "F8 puts the file in front of you." Compare with `SamePath` and `OpenPath` the stop file when it exists.

#### M4. The stopped-line background is painted into whichever box is current, and un-painted from whichever box is current later
**Where:** `MainForm.h:1461-1477` (`PaintRow` uses `text_`), `1479-1486`, `3670`, `3702`.
`highlightRow_` is one form-wide integer with no record of which sheet it was painted in. Stop in `main.c` line 10, click the `util.c` tab, *Stop debugging*: `PaintRow(9,false)` clears line 10 of `util.c`; `main.c` keeps a permanent blue line (Recolour only resets `SelectionColor`, not background). Record the sheet with the row, or paint the highlight in the gutter/stop bar only.

#### M5. Single-file Compile/Run ignore the error's file, so an error in an included header lands on the wrong line of the wrong file; `RIDERan` has no error-file accessor at all
**Where:** `MainForm.h:3033-3043` (`OnCompile` never calls `ride_build_error_file`), `3132-3142` (`OnRun`); `bridge.h:280-283` (no `ride_ran_error_file`).
`#include "util.h"` with a syntax error at `util.h:7` → the caret jumps to line 7 of `main.c` and the status bar says `7:3: error: …`. `BuildProject` already handles this correctly (`3246-3257`); reuse that code and add the missing accessor to the bridge.

#### M6. Console: only the first diagnostic is navigable, Enter/double-click ignore the caret's line, output is appended with `Text +=`, and CRLF output is probably doubled *(last point Suspected)*
**Where:** `MainForm.h:3766-3772`, `3756-3764`, `3030`, `3129`, `3239`, `3288`, `3568`, `4177`.
README says "Enter on the Console goes to the error it is about" — it goes to the single remembered error regardless of which line the caret is on. `console_->Text += …` reassigns the whole text (quadratic; resets caret/scroll). `->Replace("\n","\r\n")` on output that already contains `\r\n` (cl.exe, link.exe, most Windows tools) yields `\r\r\n` — blank line between every line; the `Lines()` helper at 934 handles this and is used only in `ShowStop`. Parse the console line under the caret (`file:line:col`), use `AppendText`, and normalise with `Lines()` everywhere.

#### M7. `OnSaveAs` mutates the sheet's path before the write, allows two tabs on one path, and records the file as recent/adopted even when the write failed
**Where:** `MainForm.h:2853-2867`.
`sheet->path = pick->FileName` precedes `OnSave`; if `WriteAllText` throws, the tab now claims a file that does not exist (and `MarkTab` is skipped, so the tab title still shows the old name). No `SheetFor(pick->FileName)` check, so "save as" onto an already-open file leaves two tabs for the same path and breaks the one-tab-per-file invariant. `ride_adopt_saved`/`ride_remember_file`/`FillTree` run regardless of success. Write first, then update state; refuse or merge when the target is open.

#### M8. `LoadProject` failure paths corrupt state: the `.pro` *file* path becomes the project root, the previous project is half-replaced, and a file named on the command line becomes a project
**Where:** `MainForm.h:1885-1932` (esp. 1929 uses `pinned` = `where`, not `directory`), `310-354`, `Program.cpp:79-82`.
- On load failure with a message, `ride_project_set_root(project_, where)` is passed the file path when `where` was a `.pro` file. `RootNow()` and the "It will be made in …" prompt then name a file as the directory.
- `tree_->Nodes->Clear()` and `projectDirectory_ = directory` happen before the load; if a project was already open and the new one fails, the tree is empty, the status bar names the failed directory, and `set_root` has redirected the *old* project's relative paths. *(Suspected: depends on whether `Project::load` clears itself on failure.)*
- `Program.cpp` treats `argv[1]` as the project directory unconditionally. `RIDEGui main.c` (file association, drag-and-drop) → `LoadProject("…\main.c")` → load fails with an empty message → `ride_begin_from_what_is_there(dir)` silently starts a project from whatever is in that directory *(Suspected: may write a `.pro`)*, and `main.c` itself is not opened as a file.
- `bridge.h:66` documents the parameter as `directory`, yet the window passes a file path from *Project ▸ Open…*; document which the core accepts.

#### M9. Paste accepts RTF, images and OLE objects
**Where:** `MainForm.h:1717-1726`.
`CanPaste(Text)` is checked but `text_->Paste()` pastes the richest available format. Copy from Word/browser → fonts, colours and embedded pictures in the code box; the saved file has plain text but the display does not match, and colouring fights the pasted formatting. Use `Paste(DataFormats::GetFormat(DataFormats::UnicodeText))`.

#### M10. Closed sheets are never disposed; GDI objects and dialogs leak
**Where:** `MainForm.h:2785-2786`, `2663-2664`, `2251-2252` (tab removed, `Sheet` dropped, nothing disposed), `1145` (per-sheet `ContextMenuStrip`), `1257-1290` (`OnGutterPaint` allocates 2 `Pen` + 4 `SolidBrush` per paint, never `Dispose`d), `1031`, `2951` (`ShowDialog` forms not disposed), `403`, `708` (timers never disposed).
Each closed file leaves a `RichTextBox` HWND, a `Gutter` HWND, a `ContextMenuStrip` and their event subscriptions to `this` alive until the finalizer thread gets to them. The gutter repaints on every keystroke and caret move, so GDI handle churn is continuous. Dispose the `TabPage` (which disposes its children) and the menu in `CloseSheet`; keep the pens/brushes as fields or `static`; wrap dialogs in `msclr::auto_handle`/explicit `delete`.

#### M11. `BeginColouring`/`EndColouring` and `WM_SETREDRAW` are not exception-safe; one exception leaves the editor frozen with undo suspended
**Where:** `MainForm.h:1449-1459`, `1535-1617` (`Recolour`), `1624-1688`, `1461-1477`.
`colouring_ = true`, `ride_undo_suspend`, `WM_SETREDRAW(false)`; any exception between (e.g. `ArgumentOutOfRange` from `Select` after a concurrent edit, `IndexOutOfRange` on `all[row]` if `Lines` changed) skips `EndColouring`. From then on `OnTextChanged`/`Recolour` return immediately (no colouring, no dirty marking, no gutter resize), undo recording stays suspended, and the box never repaints. Use `try/finally`.

#### M12. Bridge API: lifetimes, thread-safety, error reporting and side effects are undocumented and inconsistent — this matters more for the macOS consumer than for this window
**Where:** `bridge.h` throughout; `bridge.cpp:92-98` (`scratch()`), `484-515` (`project->answer`), `717-726` (`ride_ask_native`), `936-940` (`ride_program_free`), `1323-1338` (`ride_project_target_ready`).
- Every `const char*` result points either at one process-wide `scratch()` string or at a per-object `answer` string, both overwritten by the next call. `ride_project_group_name` invalidates the result of `ride_project_file`. Not thread-safe: a UI thread calling `ride_project_name` while a build thread calls `ride_project_target_ready` (exactly what C1 allows, and what "builds off the main thread" on macOS implies) races on `RIDEProject`. Document "copy before the next call; one thread at a time", or return into caller buffers.
- Errors come back four ways: `char* error,int size` (`ride_project_load`, `_allows`, `_save_as`), `ride_outcome_message(project)`, `ride_project_target_why`, or bare `0` with no message (`ride_set_includes`, `ride_remember_*`, `ride_project_set_arch`). Pick one.
- `ride_program_free` **deletes the built executable from disk**; the header does not say so. `ride_project_target_ready` must be called before the `ride_project_target_*` getters and is silently re-run inside `ride_build_target` and `ride_project_debug_plan`; the two-phase protocol is not stated.
- `ride_ask_native` takes a bare function pointer with no user-data argument and is invoked on whichever thread builds; the macOS consumer must reach its window through a global and marshal to the main thread itself.
- Build/run/debug are blocking with no cancel, progress or output-streaming entry, which is what forces C2 and shapes C1.
- `ride_highlight` carries only two bits of lexer state (`comment`, `string`) across lines *(Suspected: if `SyntaxState` grows a raw-string delimiter or line-continuation flag, it is dropped)*.
- Null checks are inconsistent (`ride_build_error_file` checks `built`, its siblings do not; `ride_project_name` dereferences unchecked while `ride_project_holds` checks).
- `ride_undo_suspend(void*)` is Win32-only in a header shared with macOS; acceptable, but say so in the header.

#### M13. The vectored fault handler is unsafe for the stack-overflow case it explicitly opts into, and logs every handled managed exception
**Where:** `bridge.cpp:106-184`, `116-119` (accepts `EXCEPTION_STACK_OVERFLOW` and `0xE0434352`).
On stack overflow the handler runs on the exhausted stack, allocates ~1 KB of locals (`room[…+512]`, `frames[62]`), then calls `SymInitialize`/`SymFromAddr` (dbghelp takes the loader lock, allocates, loads PDBs) — a second fault inside the handler, with `inside == true`, means no log at all. Every first-chance CLR exception (all the `catch (Exception^)` blocks in `MainForm.h`) appends a line to a log that is never rotated. Reserve stack with `SetThreadStackGuarantee`, or skip symbolisation for stack overflow; drop the managed-exception logging or rate-limit it.

#### M14. Process launching relies on bare executable names and unquoted user paths *(Suspected: quoting lives in the core, which was not available)*
**Where:** `MainForm.h:939-948` (`Named`: `"cl"`, `"c90"`, … fall back to bare names), `318-321`, `2721`/`2846` (default directories under `Documents`), `2408-2423` (user-chosen `vcvars64.bat`).
A bare name handed to `CreateProcess` is searched in the application directory and the *current directory* before `PATH`; if the core sets the cwd to the project directory, a `cl.exe` dropped into a downloaded project runs on Ctrl+B. The default program/project location is under the user profile, which very commonly contains a space (`C:\Users\First Last\Documents\RIDE\programs`) — if `compile.cpp` builds command lines by concatenation, most users' first build fails or, worse, runs the wrong thing. Resolve tools to full paths once (`Named` already does for the beside-the-exe case; extend it to `PATH` lookup), pass `lpApplicationName`, and audit `compile.cpp`/`debugger.cpp` for `CommandLineToArgvW`-compatible quoting of every path and of the `vcvars64.bat` invocation (`cmd /c "…"`, where `&`, `^`, `%` in a path are live).

#### M15. README claims the window does not meet
**Where:** README 1046-1047, 1114 (`Ctrl+PageDown`/`Ctrl+PageUp` "move between the open files"; "`F2` is Rename here") — `OnNextFile`/`OnPreviousFile`/`StepFile` (`MainForm.h:2166-2174`) exist but nothing binds them; there is no `F2` on *Rename File…* (484). README 811 (`Help ▸ Contents`) — the Help menu has *Keys* and *About* only (686-691). README 1156-1159 ("New file and new project are on the File menu as well") — *File* has *New* (empty buffer) and no *New project*. README 803-806 (Errors/Progress/Output tabs, builds off the main thread) is written for macOS; the Windows window has Console/Debug/Assembly, no per-diagnostic error list, and builds on the UI thread (C2) — worth saying explicitly in the Windows section so the two are not compared as equals. README 1086 ("goes to the error it is about") — see M6.

### LOW

- **L1. Dead code.** Three `DllImport`s (`GetWindowLong`, `SetWindowLong`, `SetLayeredWindowAttributes`, 950-956) unused; `OnOpenProject` (1876) unbound; `StepFile` family unbound (M15); `PrettyCompiler` (4020) is the identity; `CaretLine()` (3418) duplicates `CaretRow()+1`; `Lines()` used once.
- **L2. Duplication / structure.** One 4215-line header with every method inline. The five-tool `Utf8Of`+`pin_ptr` block is repeated six times (2998-3009, 3092-3103, 3180-3189, 3330-3337, 3351-3358, 3473-3482); `OnCompile` and `OnRun` are ~90 % identical; the run-colouring loop is duplicated in `Recolour`/`RecolourLine`. Nothing in the form is testable without a window. Suggested split: `Marshal.h` (a `Utf8` RAII helper and a `Toolchain` struct holding the five pinned strings), `Sheet.cpp` (per-tab state incl. language override, encoding, EOL, error, highlight row), `Highlighter.cpp`, `BuildRunner.cpp` (the worker, C1/C2), `MainForm.cpp` (menus and glue).
- **L3. Naming inconsistency.** Untitled tab is "untitled" in `MakeSheet` (1158) and `FillTree` (1981) but "[no name]" in `MarkTab` (2873); the label flips on the first keystroke.
- **L4. Gutter metrics.** Width `22 + 9*digits` (1222) ignores the chosen font size (Tools ▸ Font up to 48 pt); `MeasureString`/`DrawString` per line per paint — use `TextRenderer` and measure once per font.
- **L5. Tab key.** With spaces, inserts `indentWidth_` spaces regardless of column (1427); with a selection, replaces it; Shift+Tab falls through to the control and inserts a tab (no outdent).
- **L6. `OnCloseProject`** leaves `breaks_`, the project's indent/tool/arch values and any running debugger in place; `console_` is cleared but errors are not forgotten.
- **L7. `OnDeleteFile`** creates a spare untitled sheet when the last tab goes (2260), while `CloseSheet` deliberately leaves an empty environment; and `OnSheetChanged(nullptr,nullptr)` at 2261 is a no-op when no sheet remains, leaving `text_` pointing at the deleted box until `MakeSheet` runs.
- **L8. `ClosableTabControl::OnMouseDown`** closes on any button (right/middle) over the ×; `TabCloseRequested(i)` is raised without a null-delegate check.
- **L9. Logs and console.** `RIDE.log`/`RIDE-fault.log` in `%TEMP%` grow without bound; `AllocConsole` then `SW_HIDE` flashes a console window at start; both log files are world-readable and contain paths.
- **L10. `ProcessCmdKey`** swallows Ctrl+Up/Ctrl+Down at all times (normally scroll-one-line in editors) just to print "nothing is stopped".
- **L11. Re-entrancy of `OnCompile`/`OnRun`.** They check `busy_` but never set it; the `Application::DoEvents()` at 3021/3120/3227/4170 can run a second Ctrl+B nested inside the first, clobbering `console_`.
- **L12. `OnFormClosing`** does not stop the debugger; `ride_program_free` in the finalizer tries to delete a program that may still be running.
- **L13. `Program.cpp:37-48`** — `freopen_s` changes CRT streams only; children still inherit the hidden console's Win32 handles (feeds C2).
- **L14. `ride_project_load`/`save_as` error buffers** (512 bytes) are cut with `strncpy` mid-UTF-8-sequence; `FromUtf8` then shows a replacement character at the end of long messages.
- **L15 (Suspected).** `OnScrolled` restarts `recolourTimer_` on `VScroll`; if the RichTextBox raises `VScroll` for the programmatic scrolls `Recolour` itself causes, the 70 ms timer re-arms after every pass. The scroll-position restore probably prevents a visible loop, but a trace of `OnRecolourTick` frequency on a large file would settle it.

---

## What is done well (so it is not "fixed" away)

- `bridge.cpp` bounds-checks every indexed accessor and null-checks every string input; `static_assert`s catch enum drift between the header and the core.
- The `scratch()` leak-on-purpose and the comment explaining why (`bridge.cpp:92-94`) is exactly the kind of note a maintainer needs.
- `ride_undo_suspend/resume` through `ITextDocument` is the right fix for colouring polluting undo.
- Canonical-path tab reuse (`OneName`/`SamePath`), `FixedPanel` arrangement in `OnShown`, `EM_GETSCROLLPOS`/`WM_SETREDRAW` around colouring, and the owner-drawn closable tabs are all correct and well-scoped.
- `Help ▸ Keys` reading the menu bar rather than a second table.

---

## Prioritised fix list

1. **C1** — Gate every command on `busy_` (or disable the menu strip) while `WhileBusy` runs; cancel `OnFormClosing` while busy; stop the debugger in `OnFormClosing`; never touch `debugger_` from the UI thread while the worker owns it. Then move toward a real async session.
2. **C2** — Run compile/run/build/run-built/convert on the worker with a *Stop* command that kills the process tree; give children a real stdin; stream output via `BeginInvoke`.
3. **H1** — Save only when modified; preserve encoding/EOL per sheet; refuse NUL-containing files; temp-file + replace on write.
4. **H5** — `ForgetError` in `CloseSheet`; null-guard `GoTo`/`EndDebugging`/`ShowStop`/`LookAt`; add `Application::ThreadException`.
5. **H2** — Replace `Text =` with `SelectAll()+SelectedText` in `OnLayOut`/`OnReplace`; verify `CanUndo` afterwards.
6. **H3** — Move auto-realign to `KeyPress`, trigger on typed `}`/`#`/`:` only, language-aware.
7. **H4** — Stop invalidating the lexer cache on caret moves; cache line count; remove whole-document fetches from `OnKeyUp`/`OnGutterPaint`.
8. **M11** — `try/finally` around every `BeginColouring`/`WM_SETREDRAW` pair (cheap, prevents a frozen editor).
9. **M1, M2, M4** — Per-sheet language/highlight state; split `FillTree` from settings reload; keep AUTO as AUTO.
10. **M3, M5, M6** — `SamePath` matching and `OpenPath` on stop; use `ride_build_error_file` in single-file paths and add `ride_ran_error_file`; parse the console line under the caret; `AppendText`; normalise CRLF.
11. **M7, M8, M9** — Save-as ordering and duplicate-tab check; `LoadProject` failure hygiene and `argv` file-vs-directory; paste `UnicodeText` only.
12. **M10** — Dispose closed tabs/menus; hoist GDI objects; dispose dialogs and timers.
13. **M12** — Document lifetimes/threading/side effects in `bridge.h`; unify error reporting; add cancel/progress hooks (this unblocks the macOS window too).
14. **M13, M14** — Harden the fault handler; resolve tool paths fully and audit `compile.cpp` quoting.
15. **M15** — Bind Ctrl+PageUp/PageDown and F2, add *Help ▸ Contents* or fix the README; label the macOS-only claims.
16. **L1–L15** — Clean-up pass alongside the file split in L2.

---

## Addendum: `scanf` never receives input (both editors)

Reported by the user after the review and confirmed in the shared core. Neither editor is at fault on its own. The problem sits in the run path both of them use.

- **Root cause.** `src/compile.cpp:178-188` `runCaptured()` always appends `" < NUL"` (Windows) or `" < /dev/null"` (macOS/Linux) to the command. `runProgram()` (`compile.cpp:849-854`) adds the same redirect again. The user's program therefore gets end-of-file on its first read: `scanf` returns `EOF` at once, and the program carries on with uninitialised variables. The same happens with `getchar`, `fgets` and `cin >>`.
- **The bridge has no way to pass input.** `ride_run()` and `ride_run_built()` (`winforms/bridge.h:270-275`) run the program to completion and return all of its output in one piece. They take no stdin argument, stream nothing and have no cancel.
- **The GUIs offer nowhere to type.** The Output tab in WinForms and on macOS is read-only, and neither has an input line.
- **Already in the core but unused.** `src/process.cpp` has a `Process` class that gives the child a real stdin pipe (`start()`, `say()`, `readUntil()`, `stop()`). The run path does not use it.

**Suggested fix (applies to both editors):**
1. Core: add `runInteractive()` built on `Process`. It starts the program with a stdin pipe, streams stdout and stderr to the existing `LineSink` as they arrive, and takes input and stop calls. Keep `runCaptured()` for the compiler and linker, where `< NUL` is correct.
2. Bridge: add `ride_run_start(...) -> RIDERunning*`, `ride_running_send(RIDERunning*, const char* line)`, `ride_running_close_input()`, `ride_running_stop()`, `ride_running_done()`/`status()`, with an output callback. The callback fires on a worker thread, and each GUI marshals it to its UI thread (`BeginInvoke` / `dispatch_async(main)`).
3. GUIs: add an input line under the Output tab. Enter sends the line followed by `\n`, and Ctrl-D / Ctrl-Z sends EOF. Add a **Build ▸ Stop** item. This also closes C2 in the WinForms review and H2 in the macOS review.
4. Optional: add an "Open in Terminal" run mode (macOS `Terminal.app` / Windows new console) for programs that need a real TTY.
