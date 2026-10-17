# RIDE macOS window - audit of 02-10-2026

Two halves, done together:

- **Live.** The installed `/Applications/RIDE 4.51.app` (4.51 and 4.7 share this window's code but for
  the CCS-workspace chooser) driven by hand on the Mac: a project opened from Finder, edits, Compile
  File with an error, Build/Run Project, Run File with input and an endless loop and Stop, Find, Go to
  Line, Undo, the font keys, Compiler Options, Close Project, Recent Projects, Help, Environment, a
  small window. Projects were copies in a scratch folder; nothing of the user's was edited.
- **Code.** A read-only review of `macos/` and the parts of `winforms/bridge.cpp` and `src/` it calls,
  at RIDE-4.7 `553ded3`, which also re-checked every finding of `docs/RIDE-4.5-macOS-Review.md`
  (27-09). The 27-09 review stays as written.

`WC` is `macos/WindowController.mm`. **Seen** marks what was watched happening in the running app;
**Code** what was traced in the source; **Suspected** what the code suggests and nobody has seen yet.

## What works

Opening a `.pro` from Finder; Build and Run Project; Compile File with an error - the Errors tab
counts it, the gutter marks the line red, the status bar goes to `Ln 16, Col 15`; Run File with a
program reading `scanf` from the input line; Stop (Cmd-.) on an endless loop, `[stopped]`; Find with
10 matches and Replace; Go to Line; Undo; Bigger and Smaller Font, the gutter re-measured; Cancel in
Compiler Options leaving the options alone; Help opening in the browser; a narrow window keeping its
layout. Of the 27-09 review, H2, H4, M2-M11, M13, the scanf addendum and most LOW items are fixed.

## Findings, most severe first

### HIGH

**1. A CCS project is reloaded on the build thread while the window reads it - Code.**
`ride_build_target` → `ride_project_target_ready` → `reloadIfCcs` → `loadCcs` (bridge.cpp:1502,
1613-1615; project.cpp:930-970) reassigns the project's name, root, groups, arch and toolchain on the
build thread, while the main thread reads the same strings and vectors through `refreshTitle`
(WC:978-981, on any keystroke), `validateMenuItem` (WC:3456-3460, on every menu open) and
`fillNavigator`. Typing or opening a menu during Build Project on a CCS project can crash RIDE or garble
its title. *Fix:* reload only on the main thread (where `buildProject:` already calls
`ride_project_target_ready`, WC:2825) and give the build thread a snapshot.

**2. The project tree shortens even short file names - Seen.** `account.c` is drawn `acc…t.c`,
`main.c` with its modified dot `mai…`, and in the CCS sample every name becomes `Sa…cpp`, `Sa…rix.h`,
`Sa…or.h` - which cannot be told apart. The name column is far narrower than the navigator. *Fix:*
let the outline's single column take the navigator's width (`columnAutoresizingStyle`, or size the
column on resize), and truncate at the tail.

**3. Close Project leaves the closed project's tree - Seen; Code.** After Close Project the status line
says `cpp-shapes closed, and 1 file(s) with it`, Build Project is disabled, and the navigator still
shows CPP-SHAPES with its groups and files. `closeProject:` (WC:2072-2102) never calls
`fillNavigator`; it also keeps the closed project's `projectDirectory_`, compiler, target,
configuration and indentation, so a CCS project leaves `tms6747` in the status bar and Show in Finder
opens the closed project's folder. *Fix:* rebuild the navigator and reset those to the installation's.

**4. Output a program prints without end grows memory and slows the window - Code; Suspected impact.**
One main-queue block and one append-and-scroll per chunk of output (WC:2723-2731, 2749-2757), no cap
on `output_`. `for(;;) printf("hi\n");` fills memory and keeps draining after Stop. *Fix:* collect on
the worker under a lock, drain with a 20-30 Hz timer, cap Output (say the last 1-2 MB), drop the queue
on Stop.

### MEDIUM

**5. A project under a linked folder does not know its own files - Seen.** Opened from `/tmp/...` (a
link to `/private/tmp` on macOS), the path bar shows the whole path rather than `c-bank › main.c`, and
Close Project says `c-bank closed, and 0 file(s) with it` while `main.c` - a file of that project, with
unsaved changes - stays open and nothing asks about it. The same project in an ordinary folder closes
its file. Path comparison does not resolve links. *Fix:* compare resolved paths (`realpath` /
`URLByResolvingSymlinksInPath`) in `ride_project_holds` and the relative-path code.

**6. A project that fails to load leaves the old one on screen, no longer loaded - Code.** The load
clears `loaded_` first (project.cpp:210) and the failure path (WC:1961-1970) refreshes nothing: the
title and tree still say project A while Build and Close Project say there is no project. *Fix:*
refresh the navigator, title and build line on failure - or keep the old project until the new one has
loaded.

**7. Save As during a build changes the project the build is reading - Code.** `ride_adopt_saved`
(WC:1450) adds the file to the project's groups while the build thread walks them. Option-menu items
that rewrite settings.json are not held back during a build either. *Fix:* while busy, write the file
but adopt it after the build; gate the settings writers.

**8. Right-clicking a group acts on the file in the editor - Code.** `targetFile` (WC:1759-1767) falls
back to the current file when the clicked row has no path, so Remove from Project on "Sources" removes
the file in front, unasked. *Fix:* a clicked row without a path is no target.

**9. A CCS project's edits are reported done and then lost - Code.** Include paths, libraries, Target,
Compiler and file operations are enabled for a CCS project (no `ride_project_is_ccs` in
`validateMenuItem`, WC:3393-3473), say they were written, and vanish at the next build's reload.
*Fix:* disable them for a CCS project and say "change it in CCS", as Compiler Options already does.

**10. Open and Open Recent are disabled for as long as a program runs - Code.** `busy_` lasts until the
program ends (WC:3462-3471), including while it waits on `scanf`. *Fix:* allow opening files; gate only
what changes the project.

**11. The running test suite writes the user's real `~/.ride/settings.json` on macOS - Seen.** On a Mac
settings live in `~/.ride/settings.json`, and parts of `tests/test.cpp` run with no install directory
pretended, so they write there. On 02-10 at 16:03 a run of the 4.7 suite set the user's
`"ccs": {"enabled": false, "root": ""}` (the CCS-workspace test's `rememberCcs(false, "")`). The same
file names `includes: /Users/g.r.akhtar/Documents/Claude/RIDE-4.5/examples` and `options.debug.c90.opt
-O1`, which look like test values from earlier runs; Help > Environment shows the include path as the
user's. *Fix:* the suite sets `HOME` to a scratch directory before anything reads settings, as the
session suite already does with its own home.

### LOW

12. **Compiler noise in Output - Seen.** Build and run puts the compiler's echoed path and its banner
    `©2026 G. R. Akhtar - ISO C 90` ahead of the program's own lines.
13. **A pasted line does not reach the program - Seen.** Pasting `Ghulam` plus a newline into the input
    line sends nothing and leaves the field empty; typed and Return, it works.
14. **The status bar says `Building and running loop.c with c90` for as long as the program runs -
    Seen.** It should say the program is running.
15. **The input line takes the keyboard as soon as the compile starts - Seen; Code (WC:2744-2746).**
    Keys meant for the editor during a compile go to the program's input.
16. **The Find overlay outlives Escape and Go to Line - Seen.** The editor stays greyed, and the caret
    out of sight, until Done is clicked (the 27-09 M12 residue, WC:1240-1247).
17. **Undo back to the saved text leaves the modified dot - Seen; Code (WC:1195).** Quitting then asks
    to save an unchanged file. *Fix:* track the undo manager's change count, or compare with the
    saved text.
18. **Compiler Options has no frame or title bar of its own - Seen.** It looks pasted onto the editor;
    opened while the window is on another Space it appears nowhere, and RIDE waits on it unseen. A
    sheet on the window (`beginSheet`) fixes both.
19. **Compiler Options previews the wrong compiler for a CCS project - Code (WC:293, 350-351).** The
    "CCS project" tab shifts the index by one.
20. **A stored option a popup does not list is silently replaced - Suspected (WC:331, 342).**
21. **Help > Environment and Option > Show Tools in Use clear Output, even under a running program -
    Code (WC:3334, 3372).**
22. **The last Output line hides behind the input line in a short window - Seen.**
23. **Coming back to the window can switch the file in front - Code (WC:1407, 1535).** A changed
    background file is reloaded by showing it.
24. **Converting again shows the old converted file - Code (WC:2996, 1327-1330).**
25. **Close Project loses the surviving file's caret and scroll - Code (WC:2091-2093).**
26. **Recent menus show only file names - Seen.** Two `main.c` files look the same.
27. **The workspace chooser drops duplicate project names - Code (WC:1925; 4.7 only).**
28. **Quitting during a build may crash - Suspected (main.mm:56-60).**
29. **Saving replaces a symlink with a file and resets its permissions - Suspected (WC:1377).**
30. **The window repeats the menu bar inside itself - Seen (27-09 L1, still present).**
31. **The window offers nothing to accessibility - Seen.** macOS accessibility sees no elements in it:
    VoiceOver and automation cannot reach the tree, tabs, panel or buttons.
32. **Remove File and Move File are disabled for the file in front - Seen.** They act on the tree's
    selection only; with nothing selected, the menu offers neither for the open project file.
33. **Info.plist claims `com.ghulamrs.ride.project` for `.pro` in every RIDE version, and Qt claims
    `.pro` too - Code.** Open With and icons may go to another app or version.

Residues of the 27-09 review (Code): H1 partly fixed (items 1 and 7 above); H3 still sends the text
before the caret to the core on Enter, `}`, `#`, `:` and Tab; M1's status-bar column counts UTF-16
units; M12 as item 16; L10 still rebuilds and re-expands the tree on Save As, Add, Rename, Move and on
closing a background file.

## Fix order

1. Items 1 and 4 - the crash and the runaway output.
2. Items 2, 3, 5, 6 - what the navigator shows after Close, a failed load, a linked folder, and its
   names.
3. Item 11 - the test suite out of the user's settings - with the user's `ccs` setting put back.
4. Items 7-10 - holding project changes during a build, the context menu, CCS-aware menus, Open while
   running.
5. The LOW items, 12-18 first: they are what a user meets in the first ten minutes.

## State after the fixes, 02-10-2026 evening

The findings above stay as written; this is what became of them. **Checked** means watched in the
4.7 window built on the Mac, on scratch copies; **Built** means compiled and reasoned through, with
no way to watch it here.

| # | State | How |
| --- | --- | --- |
| 1 | fixed | the build thread builds what the main thread prepared and never reloads (bridge `prepared`); Built |
| 2 | fixed | the name column takes the pane's width and follows it, names cut at the tail; Checked |
| 3 | fixed | Close Project rebuilds the navigator and resets folder, target, compiler, configuration, indentation; Checked on a CCS project and a .pro |
| 4 | fixed | output collected under a lock, drained ~25 times a second, Output capped near 2 MB; Checked: 5.8 M lines in 10 s, memory flat at ~228 MB, Stop at once |
| 5 | fixed | path comparisons try again with links resolved (`path::same`, `relativeTo`); Checked under /tmp: 1 file closed with its project |
| 6 | fixed | a load is tried on a project of its own, as in the Windows window; Checked with a broken .pro |
| 7 | fixed | Save As during a build adds the file afterwards; Option-menu settings items disabled while building; Built |
| 8 | fixed | a clicked row that is not a file is no target; Built |
| 9 | fixed | for a CCS project, Target, Compiler, include paths, libraries and file add/move/remove are disabled; Built |
| 10 | fixed | Open and Open Recent allowed while a program runs, not while a build reads; Built |
| 11 | fixed | the suite sets HOME to a scratch directory; the user's settings put back by hand |
| 12 | by design | the editor shows what the compiler said, banner included - decided 11-09-2026 and pinned by tests/test.cpp ("the banner is in the console, as cxx1 wrote it"); a filter written today failed that check on both boxes and was taken out |
| 13 | fixed | whole pasted lines go to the program; Checked: "hello Ghulam" |
| 14, 15 | fixed | the input line takes the keyboard, and the status line says "running", when the program first prints; Checked |
| 16 | fixed, Escape unconfirmed | Go to Line closes the find bar (Checked); Escape is caught by a key monitor, but the test tool's Escape never reaches the app, so a press by hand confirms it |
| 17 | fixed | modified means "differs from the text last on disk"; Checked: undo clears the dot |
| 18 | fixed | Compiler Options is a sheet on the window; Checked |
| 19, 20 | fixed | the CCS tab no longer shifts the preview; an unlisted stored value is added as a choice; Built |
| 21 | fixed | Environment and Show Tools append under a running program; Built |
| 22 | withdrawn | a short window's last Output line scrolls into view normally; it was the resize, not a fault |
| 23 | fixed | a background file changed on disk is reloaded in place; Built |
| 24 | fixed | an open, unmodified file changed on disk is read again when opened; Built |
| 25 | fixed | the surviving file keeps its caret; Checked |
| 26 | fixed | Recent menus show the name and its two folders; Checked |
| 27 | not a fault | Eclipse keeps project names unique within a workspace |
| 28 | fixed | quitting during a build waits for it (up to 10 s, NSTerminateLater); Built |
| 29 | fixed | saving writes where a link points and keeps the permissions; Built |
| 30 | fixed | the in-window menu row removed at the user's request; the menus are the Mac menu bar's |
| 31 | withdrawn | accessibility lists the window's buttons, tabs and text once the window is on the current Space |
| 32 | fixed | it was finding 5: Remove and Move are enabled for the open project file; Checked |
| 33 | left | one UTI for every RIDE version's .pro is right; Qt's claim is Launch Services' to rank |
| new | fixed | a .ccsproject, .cproject or .project opened from Finder opens its project, not the XML; Built |
| M1 | fixed | the status bar's column is the compilers' (UTF-8 bytes) |
| L10 | fixed | folded groups stay folded when the tree is rebuilt |
