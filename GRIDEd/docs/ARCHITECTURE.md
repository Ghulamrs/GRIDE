# GRIDE – architecture

Everything lives under `GRIDE/src`, organised in layers. Each layer only uses the layers above it in this table.

| Layer | Folder | Namespace | Purpose |
|---|---|---|---|
| Core | `Core/` | `Gride::Core`, `Gride::Native` | JSON, paths, language/target enums, Win32 helpers (jobs, the pseudo-console) |
| Model | `Model/` | `Gride::Model` | Projects, workspaces, the CCS 7.4 / 5.5 reader, the Visual Studio reader |
| Settings | `Settings/` | `Gride::Settings` | `gride.settings.json`, `toolchain.json`, RIDE's Compiler Options |
| Services | `Services/` | `Gride::Services` | RIDE's toolchain rules, the build engine, tool runner, program runner, converter, the debuggers, templates |
| UI | `UI/` | `Gride::UI` | WinForms: theme, editor, docking, panes, dialogs, main window |

Nothing of the toolchain's names stays in the code: `toolchain.json` (shipped beside `GRIDE.exe`, the
user's file over it field by field) names every tool, library, argument template, debugger session and
Compiler Options row; the code asks `ToolchainDefinitions` and `RideToolchain`. The format is in
`TOOLCHAIN-FORMAT.md`.

## Class map

### Core
| Class | File | Role |
|---|---|---|
| `JsonValue`, `JsonParser`, `JsonWriter` | Json.h | Dependency-free JSON that keeps key order |
| `PathUtil` | PathUtil.h | Converts relative ↔ absolute paths (the `.pro` "one step difference") |
| `LanguageInfo`, `TargetInfo`, `ConfigName` | LanguageTarget.h | C90 / C++11 / Shalimar and Windows / Linux / macOS / C6747 |
| `NativeMethods` | NativeMethods.h/.cpp | WM_SETREDRAW, focus handle, rich-edit scroll position; job objects; a child on a pseudo-console (`CreatePseudoConsole` through `GetProcAddress`) for cdb |

### Model
| Class | File | Role |
|---|---|---|
| `GrideProject`, `ProjectFolder`, `CcsInfo`, `VsInfo` | GrideProject.h | A `.pro` file: folders, files, targets, configurations, dependencies, the compiler per folder, RIDE's Compiler Options (`options`); what a CCS or Visual Studio project was read from |
| `BuildConfiguration` | BuildConfiguration.h | One `Debug\|TMS320C6747`: output dirs, defines, include dirs, libs, linker .cmd |
| `Workspace` | Workspace.h | A `.grws` file: projects, startup project, active config/target, breakpoints, build order; a Visual Studio solution's folders and items; what GRIDE keeps for an imported (CCS / Visual Studio) project - its compiler word, a folder's, the program's arguments, emulateOnly - in its entry |
| `CcsReading`, `CcsProjectReader` | CcsReading.h, CcsProjectReader.h | Reads `.project`, `.cproject` and `.ccsproject` as RIDE reads them (TI's option definitions from `lib\ccs\*.tsv`, the mapping, refusals as notes, linked resources, workspaces) into a read-only `GrideProject` |
| `VsSolution`, `VsProject`, `VsProjectReader` | VsSolution.h, VsProjectReader.h | Reads `.sln`, `.vcxproj` (+ `.filters`) and `.vcproj` (the `cppparse` ProjectReader's logic in C++/CLI) into a read-only `GrideProject` per project and a `Workspace` per solution; each configuration's other ClCompile settings are kept (`ClSettings`) for a cl build |

### Settings
| Class | Role |
|---|---|
| `GrideSettings` | Root object; holds all the classes below; `PublishPaths` hands RIDE's root, TI's directories and the vcvars folder to the services |
| `EnvironmentSettings` | Theme, font, tabs, folders, RIDE's `settings.json` keys (include / lib / vcvars / ti / tilib / includes / libraries / compiler / askNative / ccs / the C6747 debugger) |
| `ToolchainSettings` | One per target: tool paths, include/lib dirs, command templates, flags - the defaults from `toolchain.json`, a value at its default following the file |
| `ToolchainDefinitions`, `ToolDefinition`, `SessionDefinition`, `LibraryDefinition`, `CompilerOptionDefinition` ... | `toolchain.json`: the shipped file with the user's over it, validated with messages per field; every tool, its lookup, operations and session, every library, the Compiler Options rows, the CCS and Visual Studio sections |
| `CompilerOptionStore` | RIDE's Compiler Options per configuration (the installation's in the settings, a project's in its `.pro`) |
| `LanguageSettings` | Extensions, keywords, types, comment markers |
| `ConverterSettings`, `LayoutSettings`, `RecentList`, `ExternalTool` | Converter tools, window layout, recent files, external tools |
| `SettingsManager` | Finds, loads and saves the settings file (portable or `%APPDATA%`), loading the toolchain definitions first |

### Services
| Class | Role |
|---|---|
| `RideToolchain` | RIDE's `toolchain.cpp` rules on the definitions: the words and aliases, which compiler takes a file, the refusals, the flags per configuration and target (through the Compiler Options), tool discovery (environment variable, tool folder, beside the IDE, RIDE's installation, TI's bin, PATH), the installation's headers, the shown lines, the C6747 runtime choice (RTS6x or TI's) |
| `BuildEngine` | Runs compile → assemble → link on a worker thread as RIDE builds: a compiler per folder, a Shalimar program as one compilation, CodeView through clang and `link.exe /DEBUG`, the C6747 link against RTS6x (or TI's runtime) with the Shalimar runtime first, the native-tools question, up-to-date checks, clean, batch builds, missing-tool errors; a part given to cl is Microsoft's (cl writes the object with a Visual Studio project's own settings through `projectSettings`, no installation header directory in front of Visual Studio's, Microsoft's `link.exe` links); every progress line names the program run and the file it writes |
| `MacroExpander` | Expands `$(Name)` in command templates |
| `ToolRunner` | Starts a tool in a job object and streams its output (no `cmd.exe`) |
| `ProgramRunner` | A program run inside the Output pane with an input line, the C6747 runners (`sim6747 --run`, Emulate on `vm6747`, Verify) |
| `VisualStudio` | Finds Visual Studio's tools and `INCLUDE` / `LIB` / `PATH` without running `vcvars64.bat` (vswhere, the installation's layout, or the named batch file's folder); the newest compiler version, or the one `msvcVersion` pins |
| `BuildMessage`, `DiagnosticReader` | Recognises RIDE's, MSVC-, TI-, GNU- and shalimar-style error lines, the preprocessor caret |
| `CodeConverter` | C ⇄ Shalimar through the converter tools |
| `DebugEngine` (abstract), `PlaceholderDebugger` | Debugger contract (stops, locals with type and address, frames and the frame looked at, watches, registers, memory, set variable, the program's output), plus a simulation of it where nothing can debug a build |
| `DebugProcess` | A debugger or a self-debugging program as a child: both streams on a pipe, or on a pseudo-console (cdb; RIDE's `plainText`), or the streams kept apart (the Shalimar session) |
| `DebugTranscript`, `ElfImage`, `DebuggerChoice`, `ProcessDebugger` | RIDE's `debugger.cpp`: what gdb, lldb and cdb say read into stops, variables, frames, values; whether a `.out` carries a line table; which engine debugs a build (`vm6747 --debug`, `sim6747 --run --debug`, cdb, the fallback debugger, the Shalimar session) and the command line from the tool's `session`; the engine asking one command at a time with a marker |
| `ShalimarSession` | RIDE's `shalimar::Session`: a Shalimar Debug build stopping itself (`SHM_DEBUG=1`, `#file` / `#ready` / `#stop` / `#exit` on its standard error), the program itself or its `.out` under `sim6747` |
| `ProjectTemplates`, `ItemTemplate` | Starter files and the C6747 linker command file |

### UI
| Class | Role |
|---|---|
| `Theme`, `ThemeColorTable`, `ThemeRenderer`, `ThemeManager` | Dark / Light / Blue themes for every control |
| `IconFactory` | Draws every icon in code (no image files) |
| `Command`, `CommandRegistry`, `KeyText`, `KeySets` | One command per action, shared by menus, toolbars, shortcuts and Ctrl+K chords; the Visual Studio and RIDE key sets |
| `TabStrip`, `StartPage` | Themed tab headers; the page shown when no file is open |
| `CodeEditor`, `CodeTextBox`, `LineGutter`, `SyntaxHighlighter`, `UndoManager` | The editor |
| `EditorDocument`, `DocumentManager` | Open files (encoding, line endings) and the tabbed document area |
| `ToolPane`, `DockArea`, `FloatWindow` | Docking: left/right/bottom areas, tabs, float and dock back |
| `ProjectExplorerPane`, `ClassViewPane`, `FilesPane` | Left panes (the explorer shows CCS and Visual Studio projects read-only, with their folders) |
| `PropertiesPane`, `BreakpointsPane` | Right panes |
| `OutputPane`, `ErrorListPane`, `FindResultsPane`, `TerminalPane` | Bottom panes (Output has the Build / Debug / Tools sources and the running program's input line) |
| `WatchPane`, `LocalsPane`, `CallStackPane`, `RegistersPane`, `MemoryPane`, `DisassemblyPane` | Debug panes, in RIDE's words: the stop line, `Name \| Value \| Type \| Address`, changed values marked, the frame looked at, the keys line |
| `ThemedDialog`, `InputDialog`, `TextDialog` | Dialog base classes |
| `NewProjectDialog`, `NewItemDialog`, `ProjectPropertiesDialog`, `ConfigurationManagerDialog`, `DependenciesDialog`, `BatchBuildDialog`, `CompilerOptionsEditor` | Project dialogs; RIDE's Compiler Options table |
| `OptionsDialog` plus its pages | Everything in `gride.settings.json`, the Definitions page for `toolchain.json`, RIDE Paths, Compiler Options, the Keyboard page's key set |
| `FindReplaceDialog`, `GoToLineDialog`, `WindowsDialog`, `SnippetsDialog` | Edit dialogs |
| `ConvertDialog`, `ToolchainCheckDialog`, `ExternalToolsDialog`, `ShortcutsDialog`, `AboutDialog` | Tools and Help dialogs |
| `MainForm` | Main window, split across 5 `.cpp` files (one per menu group) |

## Main flows

| Flow | Path through the classes |
|---|---|
| **Open a workspace** | `MainForm::OpenPath` → `Workspace::Load` → `GrideProject::Load` / `CcsProjectReader::Read` / `VsProjectReader::Read` (a `.sln` through `Workspace::ForSolution`) → `SetWorkspace` → explorer and class view refresh |
| **Build (F7)** | `MainForm::StartBuild` → `BuildEngine::StartAsync` (worker thread) → `RideToolchain` for the rules and `ToolRunner` per step → output, messages and finish events → `BeginInvoke` → Output pane, Error List, status bar |
| **Run (Ctrl+F5)** | `MainForm::RunChosen` → `ProgramRunner` in the Output pane (or `sim6747` / `vm6747` on the C6747; Verify runs both) → `[program returned N]` |
| **Debug (F5)** | Build the startup project → `LaunchDebugger` → `DebuggerChoice::For` picks the engine and its session from `toolchain.json` → `ProcessDebugger` or `ShalimarSession` (the placeholder with the reason when nothing can) → `Stopped` event → the file opened on the line, then the debug panes refresh; Up / Down the Stack through `SelectFrame` |
| **Breakpoints** | One `SortedSet<int>` per file, shared by the editor gutter, the `Workspace` (saved in `.grws`) and the debugger (resent when toggled while stopped) |
| **Settings** | `OptionsDialog` edits a clone → OK → `SettingsManager::Save` → `MainForm::ApplySettings` re-applies theme, the key set and bindings, editor options, and publishes RIDE's paths to the services |
| **The toolchain file** | `SettingsManager::Load` → `ToolchainDefinitions::Load(shipped, user)` → `ToolchainSettings::ApplyDefaults` per target; the Definitions page edits the user's file; Tools > Toolchain Check lists the problems and what was found where |
