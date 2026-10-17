# RIDE 4.5 — macOS (AppKit / Objective-C++) front end: code review

Scope: `macos/WindowController.mm` (3027 lines), `WindowController.h`, `CodeView.mm/.h`, `LineNumbers.mm/.h`, `Text.h`, `main.mm`, `Info.plist`, `Makefile`, `macos/README.md`, plus `winforms/bridge.h` and the parts of `winforms/bridge.cpp` the findings depend on. Read-only review; nothing was modified. Line numbers refer to the files as uploaded.

Findings are marked **Verified** (confirmed by reading the code and, where relevant, `bridge.cpp`) or **Suspected** (depends on AppKit or core behaviour I could not confirm from the sources here).

---

## Executive summary

The front end is small, readable and mostly idiomatic: one `NSTextView` with per-file `NSTextStorage`/`NSUndoManager` swapped in, colouring via layout-manager temporary attributes (so it never touches undo — the right call), semantic colours throughout (dark mode works), builds off the main thread with results copied into a plain C++ `Outcome` before crossing back. There are no NSTask/pipe hazards because process launching lives in the core, and I found no retain cycles that matter (the controller is a process-lifetime singleton).

The problems are concentrated in four places:

1. **A real data race on the shared `RIDEProject*` during a build.** `openDocument:` (choosing a `.pro`), `Recent Projects`, Finder drops, `Add Current File`, `Remove from Project` and `Move to Group` are *not* gated on `busy_`, so the main thread can reload or mutate the project while the build thread is reading it through `toolFrom()` / `ride_build_target()`.
2. **Everything is O(n) in file length per keystroke, per caret move and per scroll tick** — `rowOfIndex`, `lineCount`, the ruler's newline count and a full-file re-highlight — all implemented with `characterAtIndex:` loops. Fine for hello.c, unusable for a 1–5 MB file.
3. **Text-model mismatches with the core:** compiler columns are UTF-8 bytes but are applied as UTF-16 offsets; `lineRangeForRange` (which treats U+0085/U+2028/U+2029 as line breaks) is mixed with `'\n'`-only counting, which breaks on the Latin-1 fallback path; invalid UTF-8 in compiler/program output is silently replaced by an empty string; CRLF files are silently rewritten as LF; Revert does not apply the same decoding as Open.
4. **No way to stop a build or a running program.** A program that loops or waits on stdin leaves the window `busy_` forever; the only recovery is quitting.

Plus a set of smaller correctness bugs (project builds with file-less diagnostics try to open the project *directory* as a file; the gutter never re-measures after a font change; multiple Untitled buffers alias to the first; `loadProject` failure sets the root to the `.pro` file path; builds proceed after a failed save), some HIG friction (in-window menu row, Control-key shortcuts that shadow the text view's Emacs bindings, app-modal alerts, an unreadable About panel in dark mode), and a maintainability concern: the controller includes `compile.h`/`product.h` directly, quietly breaking the "everything goes through `bridge.h`" contract the README promises.

**Overall verdict: not ready to ship as-is, but close.** No Critical findings; 4 High, 13 Medium, ~15 Low. The High items are each a contained fix (a `busy_` check in `validateMenuItem:` and the delegate hooks, a line-index cache, a cancel path, and a decoding helper). With those, the editor is solid for the small-program use case the README describes; large-file work needs the performance items.

Counts: **Critical 0 · High 4 · Medium 13 · Low 15** (Suspected items counted at the severity they would have if confirmed).

---

## Findings, most severe first

### HIGH

#### H1. Project state is mutated on the main thread while the build thread reads it — Verified
**File:** `WindowController.mm:2018–2051`, `2173–2199`, `2689–2762` (validation), `1334–1351`, `1494–1497`, `1589–1598`, `1616–1621`, `1661–1669`; `main.mm:37–53`; `bridge.cpp:236–254`, `1431–1442`.

The build block captures `RIDEProject* project = project_` and calls `ride_build(project, …)` / `ride_build_target(project, …)` on a global queue. In `bridge.cpp`, `toolFrom()` reads `project->project.loaded()`, `absoluteIncludes()`, `absoluteLibraries()`, and `ride_build_target` calls `ride_project_target_ready(project)` again on the *build thread* and then runs `editor::buildParts(tool, project->parts, …, project->program)` with references into the `RIDEProject` for the whole build.

`validateMenuItem:` gates `newProject:`, `openProject:`, `closeProject:`, `saveProjectAs:`, `addFiles:`, `chooseArch:`, `chooseTool:` on `!busy_` — but **not**:
- `openDocument:` (falls to `return YES`, line 2762), which calls `loadProject:` when the picked file has the project suffix (1345–1347) → `ride_project_load` replaces `project->project` in place;
- `openRecentProject:` (menu items created in `menuNeedsUpdate:`, validated by the default `YES`);
- `application:openFiles:` in `main.mm:43–45` (Finder/Dock drop of a `.pro` or a directory) → `loadProject:`;
- `addCurrentFile:` (2750), `removeFromProject:` / `moveToGroup:` (2752–2755) → `ride_add_existing` / `ride_remove_from_project` / `ride_move_to_group` mutate the project and save it.

**Failure scenario:** start `Build Project` on a multi-part project, then pick a recent project from `Project ▸ Recent Projects` (or drop a project folder on the Dock icon). `editor::Project` is destroyed/reassigned under the build thread while `buildParts` is iterating `project->parts` and `toolFrom` has just copied `absoluteIncludes()` → use-after-free / torn `std::vector` → crash or a build against a half-loaded project.

**Fix:** treat *every* project-mutating or project-replacing entry point as gated on `busy_`: add `openDocument:`, `openRecentProject:`, `openRecentFile:` (harmless but consistent), `addCurrentFile:`, `removeFromProject:`, `moveToGroup:`, `renameFile:`/`deleteFile:` (already gated) to the `!busy_` set in `validateMenuItem:`; in `loadProject:` and `application:openFiles:` early-return with a status message when `busy_`. Longer term, have the build thread work on a *snapshot* (the bridge could expose `ride_build_target` taking copied `parts`/`program`, or the controller could serialise all `ride_project_*` calls on one serial queue).

#### H2. No way to cancel a build or a running program; a non-terminating program wedges the IDE — Verified
**File:** `WindowController.mm:1924–1964` (`beginWork`/`endWork`/`mayStartWork`), `2018–2051`, `2173–2199`; `bridge.h:270–279`.

`busy_` is set in `beginWork:` and cleared only in `endWork:`, which is reached only when the build thread's `dispatch_async` back to main runs — i.e. after `ride_run` / `ride_run_built` returns. `ride_run` returns the program's *whole* output after it exits (`ride_ran_output`). There is no Stop/Cancel action anywhere, no timeout, and no streaming.

**Failure scenario:** `Run File` on `while (1) {}` or on a program that calls `scanf`. The spinner turns forever, every Build/Target/Project item is disabled (`!busy_`), `Compile File` says "still working - give it a moment", and the status line never changes. Quitting works (`applicationShouldTerminate` ignores `busy_`), but orphans the child.

**Fix:** add `Build ▸ Stop` (Cmd-.) wired to a bridge call that kills the child process (the core's `process.cpp` owns the pid; expose `ride_build_cancel()` or give `ride_run` a cancel token). At minimum, make `applicationShouldTerminate:` warn when `busy_` and kill the child on exit. Streaming output (a callback per line marshalled to main) would also let the Output tab show progress instead of nothing until exit.

#### H3. Whole-file scans on every keystroke, caret move and scroll — Verified
**Files:** `CodeView.mm:11–36` (`rowOfIndex`, `indexOfRow`, `caretRow`), `LineNumbers.mm:57–64` (`lineCount`), `LineNumbers.mm:105–108` (newline count to first visible char), `WindowController.mm:766–826` (`recolour`), `849–864` (`textDidChange:`, `textViewDidChangeSelection:`), `88–110` / `112–131` (`realignRow:`, `insertNewline:` pass `Utf8(self.string)`).

Per keystroke in a file of N characters:
- `textViewDidChangeSelection:` → `sayWhere` → `caretRow` → `rowOfIndex` scans from 0 with `characterAtIndex:` (a message send per character): O(N).
- `textDidChange:` → `gutter_ textDidChange` → `remeasure` → `lineCount`: O(N).
- `recolourSoon` → 120 ms later `recolour`: removes temporary attributes over the whole range, then re-highlights every line (`substringWithRange`, `UTF8String`, `ride_highlight`, `addTemporaryAttribute` per run): O(N) plus allocation per line, and it invalidates the display of the whole document.
- Every Enter (`insertNewline:`) and every typed `}`, `#`, `:` or Tab (`realignRow:`) does `Utf8(self.string)` — a full UTF-8 re-encode of the buffer — and hands it to the core, which presumably re-parses from the top.
- Every scroll tick (`NSViewBoundsDidChangeNotification` → `needsDisplay`) → `drawHashMarksAndLabelsInRect:` counts newlines from 0 to the first visible character: O(N) per frame while scrolling; plus `lineCount` when `numberAttributes_` is nil.
- `insertText:replacementRange:` calls `caretRow` (O(N)) for every single typed character even before the `}`/`#`/`:` check (line 169 is after the check — good — but `insertTab:` line 139 and `realignRow` line 91 are not).

**Failure scenario:** open a 2 MB generated `.c` (≈60k lines). Each keystroke costs three full `characterAtIndex:` scans (~30–60 ms) plus a full re-highlight and full-document redraw; dragging a selection or scrolling with the trackpad fires the O(N) scan per event. The editor becomes visibly laggy at ~0.5 MB and unusable past a few MB.

**Fix:** maintain a line-start index (`std::vector<NSUInteger>` of newline offsets) on the `Sheet`, updated incrementally from `textStorage:didProcessEditing:` (or `NSTextStorageDelegate`) using the edited range and delta; `rowOfIndex` becomes a binary search, `indexOfRow` an array lookup, `lineCount` its size. Recolour only the lines intersecting the edited range (carry the per-line `state` in the index so you can resume, and stop when the state matches the stored one) and only `removeTemporaryAttribute` on that range. Have the gutter derive the first visible line from the index. Give the core an incremental entry point or at least cache the UTF-8 encoding between keystrokes.

#### H4. Invalid UTF-8 in compiler/program output silently becomes an empty string — Verified
**File:** `Text.h:14–18` (`Str`), `WindowController.mm:2056`, `2208`, `2238`, `2285`, `1789`, `1792`.

`Str()` returns `@""` when `stringWithUTF8String:` returns nil, which it does for any byte sequence that is not valid UTF-8. `finishFile:`/`finishProject:` pass the *entire* build output and program output through `Str` in one call.

**Failure scenario:** `Run File` on a C program that does `putchar(0xE9)` (a Latin-1 "é"), or a compiler that echoes a source line containing a Windows-1252 byte, or any program that prints binary. The Output tab shows nothing but `[program returned 0]`; the Errors tab is empty because `collectIssues:` still parsed the raw `std::string` but every `Issue.message` / `Issue.file` that contains the byte comes back `@""`. The user has no hint why.

**Fix:** decode lossily: try UTF-8, then fall back to `[[NSString alloc] initWithData:… encoding:NSISOLatin1StringEncoding]` (never fails), or replace invalid sequences with U+FFFD. Do this in one helper in `Text.h` (`StrLossy`) and use it for all output/diagnostic text; keep strict `Str` for paths.

### MEDIUM

#### M1. Compiler columns (UTF-8 bytes) are applied as UTF-16 character offsets — Verified
**File:** `CodeView.mm:59–70` (`goToLine:column:`), `WindowController.mm:1831`, `2072`; contrast `CodeView.mm:124–125` ("The core counts columns in bytes of UTF-8").

`goToLine:column:` does `start + (column - 1)` in `NSString` units. Diagnostics from the core are byte columns (the same convention `insertNewline:` explicitly honours). Any non-ASCII character before the error on that line shifts the caret right by (bytes − units).

**Failure scenario:** `printf("naïve — %d\n" x);` — the compiler reports the missing comma at byte column 22; the caret lands 3 characters too far right, and the find-indicator flashes the wrong token. With CJK identifiers or comments before the error it lands off the end of the line (clamped).

**Fix:** convert byte column → UTF-16 offset by walking the line's UTF-8 (`Utf8(lineString)`), or ask the core for character columns. Do the reverse in `sayWhere` so the status bar's `Col` agrees with what the terminal/Windows editor show.

#### M2. `lineRangeForRange` (Unicode line breaks) mixed with `'\n'`-only counting; the Latin-1 fallback makes it reachable — Verified
**Files:** `WindowController.mm:927–939` (Latin-1 fallback), `783` (`recolour` uses `lineRangeForRange`), `CodeView.mm:11–36` (`rowOfIndex` counts `'\n'`, `caretColumn` uses `lineRangeForRange`), `LineNumbers.mm:158` (ruler walks `lineRangeForRange`).

`NSString lineRangeForRange:` treats U+000A, U+000D, U+0085 (NEL), U+2028 and U+2029 as line terminators. `rowOfIndex`/`indexOfRow`/`lineCount` count only `'\n'`, as the core does. When a file is not valid UTF-8 it is decoded as ISO-Latin-1, so byte 0x85 (the "…" ellipsis in Windows-1252, common in comments) becomes U+0085 — a line break to `lineRangeForRange` but not to the row functions.

**Failure scenario:** open a Windows-1252 `.c` whose line 10 comment contains "…". From that line on: the gutter shows one extra line number (it increments per `lineRangeForRange` fragment, `LineNumbers.mm:162`), so the red error badge for "line 20" is drawn beside line 19; `recolour` feeds the two halves as separate lines to the highlighter so a `//` comment ends at the "…"; `caretColumn` (per `lineRangeForRange`) and `caretRow` (per `'\n'`) disagree, so `Ln/Col` and `insertTab:`'s "in the leading space" test are wrong for that line; `contentsOfRow:` returns a truncated line so `realignRow:` re-lays only part of it.

**Fix:** use one definition of "line" everywhere — the core's (`'\n'`). Replace `lineRangeForRange` with a helper built on the line index from H3, or at least strip/replace U+0085/U+2028/U+2029 on load (they cannot survive a UTF-8 round-trip to the compilers meaningfully anyway) and tell the user the file was not UTF-8.

#### M3. A project-build diagnostic with no file name opens the project *directory* and jumps in the wrong file — Verified
**File:** `WindowController.mm:2209` (`source:[root stringByAppendingPathComponent:@"."]`), `1756–1768` (`absoluteFor:` returns `source` when `file` is empty), `1826–1837` (`goToIssue:`), `2220–2223`, `1888` (File column).

For project builds `source` is `"<root>/."`. Any diagnostic whose `d.file` is empty (linker errors such as "undefined reference to `foo`", `lnk6x` errors, the build's own `errorFile` when empty) gets `issue.file = "<root>/."`. `goToIssue:` checks `fileExistsAtPath:` — true for a directory — and calls `openPath:`, which fails in `stringWithContentsOfFile:` and *overwrites the build verdict in the status bar* with the read error; then `goToLine:` is applied to whatever file is currently in front. The Errors table's File column shows `"."`.

**Fix:** pass `nil`/`@""` as the project-build source and let `absoluteFor:` return `nil` for an unknown file; in `goToIssue:` check `isDirectory` and skip both the open and the `goToLine:` when the file is unknown. Same for `finishFile:` line 2071 (`openPath:first.file ?: path` where `first.file` may be an unresolved relative name — `openPath:` then reads relative to the process cwd, which is `/` when launched from Finder).

#### M4. Builds proceed after a failed save, and the failure message is immediately overwritten — Verified
**File:** `WindowController.mm:999–1004` (`saveEveryModified`), `951–961` (`writeSheet` returns NO after `say:`), `1975`, `2122`, then `beginWork:` line 1935 `say:title`.

`saveEveryModified` ignores `writeSheet`'s return; `buildFile:`/`buildProject:` then compile whatever is on disk and `beginWork:` replaces the status line, so the only trace of "not written" is gone before the user can read it.

**Failure scenario:** the file is on a read-only volume or was `chmod 444`. The user edits, presses Cmd-B, sees "Compiling x.c…" and then errors that refer to lines they already fixed — or a clean compile of stale code.

**Fix:** make `saveEveryModified` return BOOL (or the list of failures), abort the build with an alert naming the file, and keep `modified` YES (it already does).

#### M5. CRLF files are silently rewritten as LF and non-UTF-8 files silently transcoded; Revert applies neither rule — Verified
**File:** `WindowController.mm:927–939` (Open: UTF-8 → Latin-1 fallback, CRLF/CR → LF), `951–961` (Save: always UTF-8 + LF), `1384–1395` (Revert: UTF-8 only, no normalisation).

- A project shared with the Windows editor (`.pro` and sources with CRLF) gets every saved file converted to LF — whole-file diffs in version control, and the Windows editor presumably writes CRLF back, so the files flip-flop.
- A Windows-1252 file becomes UTF-8 on save with no notice (and per M2 may already be mis-displayed).
- `revertDocumentToSaved:` reads UTF-8 only (a Latin-1 file cannot be reverted: "cannot read it back") and does not normalise CRLF, so after Revert the buffer contains `\r\n`, `caretColumn` is off by one on every line, and the next Save writes CRLF for that file only.

**Fix:** remember the detected encoding and line ending on the `Sheet` (`NSStringEncoding encoding; BOOL crlf;`), write them back on save, show them in the status bar (Windows probably does; the README is silent), and route Revert through the same reader as Open. If the core insists on LF for its own parsing, convert only for the calls into the core, not on disk.

#### M6. Gutter width never re-measured after a font change — Verified
**File:** `LineNumbers.mm:66–78` (`remeasure`), `WindowController.mm:2415–2428` (`useFont:` → `gutter_ textDidChange`).

`remeasure` rebuilds `numberAttributes_` with the new font, then returns early if the digit count is unchanged — *before* recomputing `ruleThickness`. So `View ▸ Bigger Font` (or the font panel) changes the number font but not the ruler width.

**Failure scenario:** press Cmd-+ ten times (13 → 23 pt). The 3-digit width computed at 13 pt (~46 px) is kept; numbers are drawn at `width − size.width − 10`, which goes negative, so line numbers are clipped on the left and the error badge is narrower than the digits. Shrinking the font leaves a too-wide gutter.

**Fix:** include the font size (or the measured width of "8") in the early-return test, or just always recompute `ruleThickness` and let AppKit no-op when it is unchanged. Also call `[self.enclosingScrollView tile]` after changing thickness.

#### M7. Several Untitled buffers alias to the first one — Verified
**File:** `WindowController.mm:1099–1107` (navigator lists every sheet), `1235–1253` (`navigatorClicked:` opens the *first* `path == nil` sheet), `1118–1135` (`selectCurrentInNavigator` selects the first "Untitled" row), `1206–1212` (the dot is shown on every Untitled row if *any* untitled sheet is modified).

**Failure scenario:** File ▸ New twice, type in the second. The navigator shows two "Untitled" rows, both with a dot; clicking the second one shows the first; the selection highlight always sits on the first. `sheetFor:` cannot find untitled sheets either (returns nil for `path == nil`), so `closeSheet`/`mayDiscard` on the "wrong" one is possible.

**Fix:** give `NavItem` a `Sheet*` reference (or a `sheetIndex`) for open-file rows and use identity instead of the path/title heuristics; name them "Untitled", "Untitled 2", … as NSDocument does.

#### M8. `loadProject:` failure sets the project root to the `.pro` *file* path — Verified
**File:** `WindowController.mm:1414–1431`.

`directory` is computed correctly on line 1417, but the failure path calls `ride_project_set_root(project_, Utf8(where))` with `where`, which is the `.pro` file when the user picked a file. `rootNow` then returns the file path; `newProjectFile:` says "It will be made in …/x.pro", `addFiles:` opens the panel "in" a file, and `ride_create_file` relative to a file fails or creates `x.pro/foo.c`.

**Fix:** pass `Utf8(directory)`.

#### M9. Menu Control-key shortcuts shadow the text view's standard Emacs bindings — Verified
**File:** `WindowController.mm:2946` (`Next Target` ^T), `2957` (`Next Compiler` ^K), `2878` (`Re-indent` ^I), `2890–2892` (^1/^2/^3).

`NSApp sendEvent:` offers every key event to the main menu (`performKeyEquivalent:`) *before* the first responder sees it. `NSTextView`'s `StandardKeyBinding.dict` binds ^K to `deleteToEndOfParagraph:` and ^T to `transpose:` — bindings many Mac users rely on in every text field. With these items present, ^K and ^T never reach the editor. (^I is fine: nothing binds it by default; ^1–^3 are unbound.)

The Windows/terminal editors use Ctrl+K/Ctrl+T on purpose (root README "Where the keys differ"), but on macOS the equivalent convention is Cmd-based; the HIG also discourages Control-only equivalents for menu items.

**Fix:** move these to Cmd-Opt-K / Cmd-Opt-T (or Cmd-Shift-K/T), keep the items, and document the difference in the keys table like the other intentional divergences. Alternatively, intercept them in `CodeView keyDown:` only when the text view is not first responder.

#### M10. No external-change detection; Save clobbers what changed on disk — Verified (design)
**File:** `WindowController.mm:951–961`; no `NSFilePresenter`, `NSDocument`, `FSEvents` or mtime check anywhere.

Because the editor does not adopt `NSDocument` (or at least record the file's mtime), a file changed outside — `git checkout`, the converter, a `ride_rename_file` of an open file's sibling, another RIDE window — is overwritten on Cmd-S with no warning, and there is no "file changed on disk, reload?" path. NSDocument would also give autosave/versions, proper sheet-based "Save changes?" dialogs, system Open Recent, and state restoration of open files for free.

**Fix:** minimal: store `mtime` on `Sheet` at load/save; before `writeSheet` compare and ask ("The file has changed on disk. Overwrite / Reload / Cancel"); on window activation (`windowDidBecomeKey:`) check open sheets and offer reload when unmodified. Full: move `Sheet` to an `NSDocument` subclass (the one-view/many-storages design can stay; NSDocument does not require one window per document).

#### M11. The controller bypasses `bridge.h` and includes core C++ headers directly — Verified
**File:** `WindowController.mm:10–11` (`#include "compile.h"`, `#include "product.h"`), `235–237` (`editor::product::kCompilerC` …), `1784–1786` (`editor::parseDiagnostic`, `editor::Diagnostic`); `WindowController.h:3` and root `README.md:777–802` promise that both windows "consume the core through winforms/bridge.h" so they "cannot drift apart on anything but looks".

The mac window now has a private C++ seam: it parses diagnostics line by line with `editor::parseDiagnostic`, which the Windows window (C++/CLI, which *could* call it but per the README uses `ride_build_error_*`) does not. The Errors tab's content therefore differs between the two front ends by construction, and `tests/test.cpp`, which "drives the window's own seam" through `bridge.cpp`, cannot cover it. It also drags the core's C++ headers into an ARC ObjC++ translation unit, which is where the "function-local static with destructor corrupts the heap" class of bug noted in `bridge.cpp:92–94` came from.

**Fix:** add `ride_parse_diagnostic(const char* line, const char* source, RIDEDiag* out)` (and `ride_compiler_name(int)` for the product constants) to `bridge.h`, use it from both windows, and drop the two includes.

#### M12. `NSTextFinder` is not told when the text storage is swapped — Suspected
**File:** `WindowController.mm:531–532` (`usesFindBar`, `incrementalSearchingEnabled`), `896` (`replaceTextStorage:`).

`NSTextFinder` requires `noteClientStringWillChange` before the client's string changes; `NSTextView` does this for its own edits but `NSLayoutManager replaceTextStorage:` is not an edit. With incremental search enabled the finder keeps match ranges and a highlight overlay for the previous string.

**Failure scenario (to confirm on a Mac):** open a long file, Cmd-F "foo" with several matches, switch to a short file with Cmd-Ctrl-→, press Cmd-G. Expected symptoms range from stale highlights to an `NSRangeException` from `showFindIndicatorForRange:` beyond the new length.

**Fix:** before swapping, hide the find bar (`performTextFinderAction:` with `NSTextFinderActionHideFindInterface`) or toggle `usesFindBar` off/on; or make the swap an edit the finder sees by routing it through `shouldChangeTextInRange:`. Alternatively, use the bridge's own `ride_find_next`/`ride_replace_all` (currently unused on macOS) so Find semantics match the other two front ends.

#### M13. Undo coalescing is not broken across sheet swaps — Suspected
**File:** `WindowController.mm:888–916` (`showSheet:`), `844–847` (`undoManagerForTextView:`).

`NSTextView` coalesces consecutive typing into one undo action and keeps that group open until `breakUndoCoalescing` is called (AppKit calls it on selection changes it makes itself, but not when the storage under it is replaced). After `replaceTextStorage:` the view's "current typing" bookkeeping still refers to the previous sheet's ranges while `undoManager` now answers with the new sheet's manager.

**Failure scenario (to confirm):** type "abc" in A at offset 0, switch to B (empty), type "d". If the coalescer accepts "d" as a continuation, the undo action for "abc" in A's manager is extended with a range in B; Cmd-Z in B does nothing, Cmd-Z in A raises or deletes the wrong characters.

**Fix:** call `[code_ breakUndoCoalescing]` at the top of `showSheet:` (and after `useFont:`, `revertDocumentToSaved:`). Cheap and safe even if AppKit already handles it.

### LOW

#### L1. In-window menu row duplicates the menu bar — Verified (HIG)
`WindowController.mm:396–404`, `3002–3025`. "As on Windows" is not a reason on macOS; it costs 26 px of every window, confuses VoiceOver (buttons whose action is to open a menu that already exists), and `popUpMenuPositioningItem:` on a menu that is also in the menu bar is unusual. Drop it, or make it a toolbar (`NSToolbar`) with the build/run/target items instead.

#### L2. About panel credits are black-on-black in dark mode — Verified
`WindowController.mm:2680–2682`: the credits `NSAttributedString` sets only a font; the default foreground is opaque black. Add `NSForegroundColorAttributeName: NSColor.textColor` (or `labelColor`).

#### L3. Destructive alert has Cancel as the default/rightmost button — Verified
`WindowController.mm:1645–1651`: first button "Cancel" (Return), second "Delete". HIG: the action button goes rightmost/default only if non-destructive; here Delete should be rightmost with `hasDestructiveAction = YES` (macOS 11+) and Cancel to its left, and Return should not trigger it — set `keyEquivalent = @""` on Delete. Also all alerts use `runModal` (app-modal) rather than `beginSheetModalForWindow:` — fine for a single window, but blocks the Font panel and the About panel too.

#### L4. `AskNativeInWindow` alert puts the product name as the message and the question as the detail — Verified
`WindowController.mm:96–100`. `messageText` should be the question (bold), `informativeText` the consequence; the app name is already in the icon/title.

#### L5. Cmd-+ requires Shift on US keyboards — Verified
`WindowController.mm:2895`: key `"+"`. Register `"="` (Cmd-=) as the key and optionally a hidden alternate for `"+"`, as Safari/Xcode do.

#### L6. Compilers are searched for beside the bundle and in `../bin` relative to wherever the app sits — Verified (security, low risk)
`WindowController.mm:191–214`. If the app is run from `~/Downloads`, a planted `~/Downloads/bin/c90.exe` runs with the user's privileges on Cmd-B. Prefer: inside the bundle, then `~/.ride/settings.json`-configured paths, then `PATH`; never directories relative to the app's parent unless the app is inside a checkout (detect `../macos/Makefile`).

#### L7. `~/Documents/RIDE` is hard-coded via `NSHomeDirectory()` — Verified
`WindowController.mm:1305–1317`. Use `NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, …)` (respects redirected/iCloud Documents) and be prepared for the TCC "RIDE would like to access your Documents folder" prompt on macOS 12+ — there is no `NSDocumentsFolderUsageDescription` in `Info.plist`, so the prompt has no explanation. Also the first `Save As` silently copies the bundled sample programs into the user's Documents (line 1312–1315), which surprises people.

#### L8. `saveAll:` reports "0 file(s) written" after a successful Save As; `saveSheetAs:` renames the sheet before the write succeeds — Verified
`WindowController.mm:1371–1382`, `983–984`. Count the Save As results; set `sheet.path` only after `writeSheet` succeeds (keep the old path on failure).

#### L9. `revertDocumentToSaved:` / `replaceRange:with:` insert plain text that inherits attributes from the first replaced character — Verified
`CodeView.mm:74–78`. When the storage is empty (a new buffer, or a file emptied), `replaceCharactersInRange:withString:` gives the text no font → default Helvetica 12 until the next `useFont:`. Use `replaceCharactersInRange:withAttributedString:` with the host's `codeAttributes`, or `insertText:replacementRange:` which applies `typingAttributes`.

#### L10. Navigator is rebuilt and fully re-expanded on every file switch — Verified
`WindowController.mm:1073–1116` from `showSheet:` (914). User-collapsed groups pop open again each time; selection flickers. Rebuild only on project changes; update the OPEN FILES section in place.

#### L11. `RIDE.icns` is a Makefile prerequisite but is not in the reviewed tree — Suspected
`Makefile:75`, `Info.plist:9–10`. If the file is genuinely absent from the repository, `make` stops with "No rule to make target 'RIDE.icns'". The README also mentions `Window.xcodeproj` and `make-xcodeproj.py`, neither present here. Verify they are committed.

#### L12. Bundle/signing: ad-hoc `--deep` signature, no hardened runtime, no entitlements; non-Mach-O helpers — Verified / Suspected
`Makefile:88`. `codesign --deep --sign -` is fine for local use; for distribution you need a Developer ID, `--options runtime`, and notarization, and `--deep` is deprecated (sign the helpers explicitly). If `../bin/*.exe` ever includes real Windows PE binaries (MASM/LINK are Windows tools per the README), `codesign --deep` treats non-Mach-O files in `Contents/MacOS` as resources — verify it does not fail. `Contents/MacOS/lib` as a symlink into `Resources` is legal but unusual; put the runtime in `Contents/Frameworks` or `Contents/Resources` and point the core at it.

#### L13. `Info.plist` nits — Verified
Uses deprecated `CFBundleTypeExtensions` (no `LSItemContentTypes`/`UTExportedTypeDeclarations` for `.shl`/`.pro`, so Finder shows generic icons and no "Open With" association for those); `CFBundleVersion` should be a monotonically increasing build number, not "4.5"; no `NSHumanReadableCopyright`. `LSMinimumSystemVersion` 12.0 matches `-mmacosx-version-min` and all APIs used (`[NSApp activate]` is correctly `@available`-guarded).

#### L14. Makefile nits — Verified
`-fobjc-arc` on the link line (line 70) is meaningless; single-architecture build (no `-arch arm64 -arch x86_64` or `ARCHS` variable) although the status bar advertises the target; `-Werror -Wextra` on ObjC++ works today but will break on the first SDK deprecation once `MACOS_MIN` is raised (consider `-Wno-error=deprecated-declarations`); no `-g`/dSYM even for debug. The core is compiled a second time into `mac-obj/core` rather than reusing `../Makefile`'s objects — intentional per the comment, but the two flag sets can drift.

#### L15. Dead code and duplication — Verified
- `Outcome.programRan` is written (2044, 2190) and never read.
- Empty class extension `@interface WindowController () @end` (125–126).
- `nextCompiler:`/`nextTarget:` fabricate a throwaway `NSMenuItem` to reuse `chooseTool:`/`chooseArch:` (2498–2500, 2510–2512) — factor the tag-free core out.
- The `Outcome` extraction is copy-pasted three times (2019–2047, 2174–2194, 2277–2283); `finishFile:` and `finishProject:` duplicate the hasError / !ok / success ladder; `shiftBy:` and `toggleComment:` duplicate the "selected lines as an array" prologue/epilogue; `rowOfIndex`/`lineCount`/newline-counting exist in three places (`CodeView`, `LineNumbers`, `recolour`).
- Bridge functions unused on macOS that represent parity gaps: `ride_find_next/previous/replace_all` (Find is AppKit's, so case/wrap semantics differ from Windows/terminal), `ride_describe_build` (Windows "describe the assembly"), `ride_debug_note`, the entire debugger surface (acknowledged in `macos/README.md`), `ride_undo_suspend/resume` (correctly unnecessary here).
- Split views have no `autosaveName`, so divider positions are not remembered (Windows remembers "the frame style").
- The 3027-line controller mixes model classes (`Sheet`, `Issue`, `NavItem`), two data sources, build orchestration, menu construction and every action. Natural seams: `Sheet`/document model, `BuildRunner` (the three async blocks + `Outcome`), `Navigator` data source, `IssueList`, `MenuBuilder`.

---

## Things that are right (so they are not "fixed" away)

- Colouring through `NSLayoutManager` temporary attributes: never enters undo, never dirties the file. Keep it; just make it incremental (H3).
- Per-sheet `NSUndoManager` via `undoManagerForTextView:` and one shared layout manager with `replaceTextStorage:` — a sound design for a single-window editor (add `breakUndoCoalescing`, M13).
- `Outcome` copied out of the core's objects on the build thread before `ride_*_free`, then handed to main — correct ownership; no core pointer crosses threads except the shared project (H1).
- `AskNativeInWindow` uses `dispatch_sync` to main only when not already on main — no self-deadlock.
- Semantic colours everywhere (`textColor`, `textBackgroundColor`, `separatorColor`, `secondaryLabelColor`, `system*Color`), so dark mode works without special casing (except L2).
- `LineNumbers` removes its notification observer in `dealloc`; the ruler is created after the text view is in the scroll view, so `enclosingScrollView` is non-nil.
- ARC on all `.mm`; `Text.h` helpers copy before `ride_free`; `Utf8()` results are consumed within the autorelease scope; `StdString()` is used for anything crossing threads. No retain cycles found (`delegate`/`dataSource`/`target`/`host` are weak or assign; the `NSTimer` is non-repeating and invalidated).
- `main.mm` skips `-psn_…`/`-NS…` arguments correctly, answers `--version` without touching AppKit, and handles `application:openFiles:` both before and after the window exists.
- Modifier/HIG choices that are right: Cmd-0 navigator, Cmd-Shift-Y panel, Cmd-[/] shift, Cmd-/ comment, Cmd-E use selection, Cmd-T font panel, Cmd-Ctrl-F full screen, Save/Cancel/Don't Save order in `mayDiscard:`.

---

## Prioritised fix list

1. **H1** — Gate `openDocument:` (for `.pro`), `openRecentProject:`, `application:openFiles:`, `addCurrentFile:`, `removeFromProject:`, `moveToGroup:` on `!busy_`; early-return in `loadProject:` while busy. (Small, prevents crashes.)
2. **H4** — Lossy decoding helper in `Text.h` for all output/diagnostic text. (Small, prevents empty Output.)
3. **M4** — Abort the build when `saveEveryModified` fails; alert. (Small.)
4. **M8** — `ride_project_set_root(project_, Utf8(directory))`. (One line.)
5. **M6** — Recompute `ruleThickness` on every `remeasure`. (One line.)
6. **M13** — `[code_ breakUndoCoalescing]` in `showSheet:`. (One line; then verify M12 on a Mac.)
7. **M3** — `nil` source for project builds; skip open/goto for unknown files. (Small.)
8. **M1** — Byte→UTF-16 column conversion in `goToLine:column:` and the reverse in `sayWhere`. (Small.)
9. **H2** — `Build ▸ Stop` with a cancel path through the bridge; warn on quit while busy. (Medium; needs core support.)
10. **H3 + M2** — Line-start index on `Sheet` with incremental update; incremental recolour; ruler and row functions derived from it; single `'\n'` line definition. (Medium; the one change that makes large files usable.)
11. **M5** — Remember encoding + line ending per sheet; write them back; route Revert through the Open reader. (Medium.)
12. **M7** — Identity-based navigator rows for open files; "Untitled N". (Small.)
13. **M10** — mtime check before save and on activation; consider NSDocument. (Medium.)
14. **M11** — `ride_parse_diagnostic` / `ride_compiler_name` in `bridge.h`; drop `compile.h`/`product.h`. (Small; restores the architectural contract.)
15. **M9, L1–L5** — Shortcut and HIG cleanups. (Small each.)
16. **L6–L15** — Hygiene: compiler search path, Documents path, Save As/Save All bookkeeping, navigator rebuild, icon/Xcode files, signing, plist, Makefile, dead code, split-view autosave, controller decomposition.

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
