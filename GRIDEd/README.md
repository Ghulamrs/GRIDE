# GRIDE

A C++/CLI IDE in the style of Visual Studio for three languages and four targets:

| Languages | Targets | Tool chain |
|---|---|---|
| C90, C++11, Shalimar | Windows, Linux, macOS, TMS320C6747 | MASM / LINK (Windows) and ASM6x / LNK6x / RTS6747 / VM6747 (DSP) |

It also has C ⇄ Shalimar conversion and read-only import of Code Composer Studio 7.4 projects.

---

## 1. Build it

| Need | Where |
|---|---|
| Visual Studio 2022 (also opens in VS 2026) | Workload **Desktop development with C++** |
| C++/CLI support | Installer > Individual components > **C++/CLI support for v143 build tools** |
| .NET Framework 4.8 targeting pack | Installer > Individual components |

1. Open `GRIDE.sln`.
2. Choose `Debug | x64` (or x86).
3. Build (F7) and run (F5).

The output is `bin\<platform>\<config>\GRIDE.exe`. The `docs` folder is copied next to it, and **Help > View Help** opens it.

> **Note:** this code was written without a compiler at hand. If VS 2022 reports a small compile error on the first build, send me the error list and I'll fix it.

## 2. Try it

1. Run GRIDE.
2. Go to **File > Open > Project / Workspace** and pick `samples\INS_Suite\INS_Suite.grws`.
3. You get three projects:
   - **NavFilter**: C90, Windows + C6747
   - **GuidanceLaw**: Shalimar, all four targets
   - **ImuDriver**: a CCS 7.4 project, read-only
4. Press **F7** to build. Each missing tool shows as an error in the Error List and points you to **Tools > Options > Toolchains**.
5. Press **F5** to start the placeholder debugger. It stops at the breakpoint in `kalman.c`. Then use **F10 / F11** and the Watch, Registers and Memory panes.

## 3. Files GRIDE uses

| File | What it holds | Where |
|---|---|---|
| `*.pro` | One project: language, targets, folders and files (paths relative to the `.pro`), configurations | Project folder |
| `*.grws` | Workspace: list of `.pro` files and CCS projects, startup project, active Config/Target, breakpoints | Anywhere |
| `gride.settings.json` | IDE settings: tool paths, include/lib folders, command templates, languages, converters, theme, keys, layout | `%APPDATA%\GRIDE\`, or next to `GRIDE.exe` for a portable install |

## 4. Plugging in your real tools

Open **Tools > Options > Toolchains > *target***. Each target has:

- **Tool paths:** C90 / C++11 / Shalimar compilers, assembler, linker, runtime and simulator. A status column shows whether each one is found.
- **Command templates** for each build step. For example:

  ```
  "$(Assembler)" "$(AsmFile)" --output_file="$(ObjFile)"
  "$(Linker)" $(Objects) "$(LinkerCmd)" -o "$(Output)" $(LibDirFlags) $(LibFlags)
  ```

- **Flag spellings** (`-I`, `-D`, `/LIBPATH:` …) and file extensions.

The build pipeline is `source → (compile) → .asm → (assemble) → .obj → (link) → program`. No code change is needed when the real tools arrive.

## 5. Where to change code

See `docs/ARCHITECTURE.md` for the class map. Common entry points:

| Task | File |
|---|---|
| Real VM6747 / native debugger | Derive from `DebugEngine` in `src/Services/Debugger.h`, then create it in the `MainForm` constructor |
| New menu command | Add it in `RegisterCommands()` and `BuildMenus()` in `src/UI/MainForm.Commands.cpp` |
| Shalimar keywords | Tools > Options > Languages > Shalimar (no code needed) |
| New pane | Derive from `ToolPane`, then `AddPane` it in `MainForm::CreatePanes()` |

## 6. Known limits (v1.0)

- **Editor:** based on RichTextBox. It is fine for normal source files; files over 1.5 MB open without colouring.
- **Debugger:** a simulation, until VM6747 / native debug interfaces exist.
- **CCS projects:** read-only. GRIDE builds them with its own tool chain into `.gride\` and never changes CCS files.
- **DPI:** the app is not DPI-aware, so Windows scales it on high-DPI screens.
