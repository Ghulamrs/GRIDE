# GRIDE 2.0

A Visual Studio-style IDE for your own toolchain.

| Item | Value |
|---|---|
| Languages | C90, C++11, Shalimar |
| Targets | Windows (MASM/LINK), Linux, macOS, TMS6747 DSP (ASM6x, LNK6x, RTS6747, VM6747) |
| Built with | VS 2022, C++/CLI, .NET Framework 4.8, pure WinForms (no third-party libraries) |
| Project file | JSON `.pro` |
| IDE settings | JSON, `%APPDATA%\GRIDE\settings.json` |
| CCS 7.4 | Projects opened read-only (view only) |

---

## 1. Build GRIDE

1. Install **Visual Studio 2022** with:
   - Desktop development with C++
   - **C++/CLI support for v143 build tools**
   - .NET Framework 4.8 targeting pack
2. Open `GRIDE.sln`.
3. Choose **Release | x64** (or Debug).
4. Build > Build Solution. Output: `x64\Release\GRIDE.exe`.

> The code was reviewed but not compiled before delivery (no MSVC in the authoring environment). If the first compile shows any error, send me the Error List text and I will fix it.

---

## 2. Folder layout

| File | Phase | Contents |
|---|---|---|
| `src/Main.cpp` | - | Entry point (single translation unit, all classes header-only) |
| `src/Common/Common.h` | - | JSON load/save, paths, text-file encoding, Win32 interop |
| `src/Settings/IdeSettings.h` | 3 | IDE settings + default tool definitions |
| `src/Project/GrideProject.h` | 4 | `.pro` project model, configurations |
| `src/Editor/Syntax.h` | 5 | Keyword sets, syntax tokenizer |
| `src/Editor/CodeEditor.h` | 5 | Editor control (gutter, colouring, undo, find/replace) |
| `src/Toolchain/Toolchain.h` | 6 | Target info, async process runner |
| `src/Build/BuildEngine.h` | 7-10 | Build pipeline, error parser, run, convert |
| `src/Import/CcsImporter.h` | 11 | CCS 7.4 `.project` / `.cproject` reader |
| `src/UI/Dialogs.h` | 2, 12 | New project, find/replace, properties, input box |
| `src/UI/OptionsDialog.h` | 3, 6 | Tools > Options |
| `src/UI/MainForm.h` | 2, 4, 7-12 | Main window |

---

## 3. First-time setup (Tools > Options > Toolchains)

Every external program is a **tool** with: path, arguments, include/define/library switches, error regex, environment, setup script and output code page.

| Tool | Set this |
|---|---|
| C90 / C++11 / Shalimar compiler | Path to your compiler; adjust arguments to its command line |
| MASM (ml) / LINK | **Setup script** = `vcvars32.bat` (or `vcvars64.bat` + `ml64.exe`) |
| ASM6x / LNK6x | Paths to `asm6x.exe` / `lnk6x.exe` |
| RTS6747 | The runtime **library file** (added to the link automatically) |
| VM6747 | Path to the virtual machine; args `"{exe}"` |
| Linux / macOS tools | Cross assembler/linker, or WSL / remote scripts |
| Converters | C -> Shalimar and Shalimar -> C programs |

**Placeholders** (Help > Placeholder Reference): `{in} {out} {objs} {libs} {includes} {defines} {flags} {asmflags} {linkflags} {target} {config} {name} {projectdir} {outdir} {objdir} {exe} {map} {cmdfiles} {runtime}`

**Shalimar**: set its file extensions and keywords in Options > Languages (defaults: `.shl`, placeholder keywords).

---

## 4. How a build works

| Step | Tool | Output |
|---|---|---|
| 1. Compile (`.c`, `.cpp`, Shalimar) | Compiler for the file's extension | Target assembly in `obj\` |
| 2. Assemble (`.asm`, `.s` or step 1 output) | Target assembler | Object file |
| 3. Link | Target linker + libraries (+ RTS6747, `.cmd` files for TMS) | `.exe` / `.out` / binary |

- **Incremental:** only changed files rebuild. A change to a project header, compile settings or tools rebuilds all; linker changes force a relink.
- Headers reached only through include paths are not tracked: use **Rebuild**.
- Output folder (default): `build\$(Target)\$(Config)`.
- Errors from MSVC/MASM, TI (asm6x/lnk6x/compiler) and GCC formats appear in the **Error List**; double-click to jump.

---

## 5. `.pro` file format

```json
{
  "formatVersion": 2,
  "name": "Demo",
  "language": "C90",
  "target": "TMS6747",
  "outputName": "Demo",
  "sourceFiles": [ "main.c", "src\\uart.c", "lnk.cmd" ],
  "includePaths": [ "include" ],
  "libraries": [ ],
  "activeConfiguration": "Debug",
  "configurations": [
    { "name": "Debug", "defines": [ "_DEBUG" ], "flags": "", "linkFlags": "",
      "outputDir": "build\\$(Target)\\$(Config)", "asmFlags": "" }
  ]
}
```

- `language`: `C90` | `C++11` | `Shalimar` (default for new files).
- `target`: `Windows` | `Linux` | `macOS` | `TMS6747`.

---

## 6. Keyboard shortcuts

| Keys | Action |
|---|---|
| Ctrl+Shift+N / Ctrl+N | New project / new file |
| Ctrl+Shift+O / Ctrl+O | Open project / open file |
| Ctrl+S / Ctrl+Shift+S | Save / save all |
| Ctrl+F / Ctrl+H / F3 / Ctrl+G | Find / replace / find next / go to line |
| Ctrl+Shift+F | Find in files |
| Tab / Shift+Tab | Indent / outdent selection |
| Ctrl+Shift+B / Ctrl+F7 | Build project / compile current file |
| F5 / Ctrl+F5 / Shift+F5 | Build and run / run / stop |
| Ctrl+Tab | Next document |
| Alt+F7 / F4 | Project properties / properties window |

---

## 7. Known limits

- The tool list is fixed (one entry per known tool); paths and arguments are fully editable.
- Programs run on VM6747 / Linux / macOS runners: output is captured, but there is no keyboard input.
- With a setup script, arguments pass through `cmd`: avoid `& | < > ^` in paths and flags.
- CCS 7.4 projects cannot be built or edited in GRIDE (by design).
- Error List has no filter buttons yet.
