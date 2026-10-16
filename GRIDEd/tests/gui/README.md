# GUI regression tests

`run-suite.ps1` drives GRIDEd (`x64\Release\GRIDE.exe`) through its own window: it rebuilds the three
projects in `projects\` (C90 cdemo, C++11 cppshapes, Shalimar shlmatrix) in Debug and Release on Windows
and TMS320C6747, and debugs each on both targets (breakpoint, F10, F11, Shift+F11, F5 to the return
code). About two and a half minutes; leave the keyboard and mouse alone while it runs.

    powershell -ExecutionPolicy Bypass -File tests\gui\run-suite.ps1            # everything
    powershell -ExecutionPolicy Bypass -File tests\gui\run-suite.ps1 -Only debug  # the debug sessions

It prints PASS / FAIL per check and exits with the number of failures. Windows debugging needs cdb
(Windows SDK Debugging Tools).

`g2-check.ps1` runs the same build and debug checks on GRIDE 2.0 (`..\GRIDE`), one project at a time,
with its own keys (F8, F7, F6).

`lib\` holds the helpers: UI Automation only to find menus, Win32 messages to read the panes, shortcut
keys for every debug step.
