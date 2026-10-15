# GRIDE 2.0 - Code Review Report

| Item | Value |
|---|---|
| Requested reviewer | Fable 5.1 |
| Actual reviewer | **Opus 5.5** (independent agent) - the Fable 5.1 request was refused: account Fable usage limit reached |
| Passes | 2 (full review, then verification of every fix) |
| Result | 32 findings + 8 from the verification pass; all blockers and compile risks fixed |

---

## Pass 1 - findings and what was done

### A. Compile errors / risks

| # | Severity | Finding | Status |
|---|---|---|---|
| 1 | Blocker | `ContextMenuStrip` type clashed with the Form property | Fixed (fully qualified) |
| 2 | Risk | `CloseReason` may resolve to an internal Form property | Fixed (qualified) |
| 3 | Risk | `extern "C"` P/Invoke inside a namespace | Fixed (`NativeMethods` ref class) |
| 4 | Risk | `/RTC1` conflicts with `/clr` in Debug | Fixed (`BasicRuntimeChecks=Default`, PDB format, async EH) |
| 5 | Minor | `EntryPointSymbol=main` | Kept (standard VS C++/CLI WinForms template) |
| 6 | Risk | `AddRange(array<String^>)` conversion | Fixed (loops) |
| 7 | Minor | `full + ".tmp"` style | Fixed |

### B. Runtime bugs

| # | Severity | Finding | Status |
|---|---|---|---|
| 8 | Major | Project Properties OK/Cancel off-screen | Fixed (right-to-left flow panel) |
| 9 | Major | Re-colouring broke mouse/Shift selection | Fixed (defer while selecting) |
| 10 | Major | Possible endless re-highlight loop | Fixed (guard) |
| 11 | Major | RichEdit native undo buffer growing | Fixed (`EM_SETUNDOLIMIT 0`) |
| 12 | Major | View jumped in files > ~4000 lines | Fixed (line-based restore) |
| 13 | Major | Data race in error parser | Fixed (pre-compiled regex + lock) |
| 14 | Major | Linker inputs ignored by incremental build | Fixed (link stamp + timestamps); include-path headers documented |
| 15 | Major | TI asm6x error format not recognised | Fixed (new pattern; tool names not treated as files) |
| 16 | Major | Trailing `\` broke quoted arguments | Fixed |
| 17 | Minor | stdin, output encoding, child processes | Fixed (stdin closed, OEM/code page, `taskkill /T`) |
| 18 | Minor | Closing during a build | Fixed (cancel + wait, token in job, dispose) |
| 19 | Minor | `Uri` mishandled `#`/`%` in paths | Fixed (manual relative path) |
| 20 | Minor | Non-atomic JSON save | Fixed (`File::Replace`) |
| 21 | Minor | Splitter restore/save | Fixed |
| 22 | Minor | First tool fields empty in Options | Fixed |
| 23 | Minor | DPI / font scaling | Fixed (font first, no auto-scale) |
| 24 | Minor | Fonts not disposed, bold nodes clipped | Fixed |
| 25 | Minor | Whole-text undo snapshots | Fixed (delta undo) |

### C. Design gaps

| # | Finding | Status |
|---|---|---|
| 26 | MASM/LINK need the VS environment | Fixed (per-tool setup script + environment) |
| 27 | Fixed tool list; `{asmflags}` | `{asmflags}` added; fixed list kept by design |
| 28 | Editor mode ignored project | Fixed (`.h` per language, GNU asm mode) |
| 29 | Tabs, word delete, context menu, disk changes, Undo state | Fixed |
| 30 | Compile file, Find in Files, Ctrl+Tab, column jump, `.pro` resync | Fixed; Error List filters not yet |
| 31 | CCS `locationURI` links | Fixed (`PROJECT_LOC`, `PARENT-n-...`, variables, linked folders) |
| 32 | Runtime library twice; runner stdin | Fixed / documented |

---

## Pass 2 - verification findings

| # | Severity | Finding | Status |
|---|---|---|---|
| - | Risk | `Action(p, &Process::WaitForExit)` overload binding | Fixed (`ExitWaiter` helper) |
| N1 | Major | Setup script never used (tool "not found" first) | Fixed |
| N2 | Minor | Colouring waited while any text was selected | Fixed |
| N3 | Minor | Project at a drive root stored absolute paths | Fixed |
| N4 | Minor | Work resumed while window closing | Fixed (closing flag, form disabled) |
| N5 | Minor | Undo caret position | Fixed |
| N6 | Minor | cmd metacharacters / timeout race | Documented |
| N7 | Minor | Old settings missed `{asmflags}` | Fixed (settings v3 upgrade) |
| N8 | Risk | Font disposed before child controls | Fixed |

## Reviewer verdict (pass 2)

All compile blockers and risks from the first review are resolved; with N1 fixed the project is ready for its first compile in VS 2022.
