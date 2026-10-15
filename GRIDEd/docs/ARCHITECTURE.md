# GRIDE – architecture

Everything lives under `GRIDE/src`, organised in layers. Each layer only uses the layers above it in this table.

| Layer | Folder | Namespace | Purpose |
|---|---|---|---|
| Core | `Core/` | `Gride::Core`, `Gride::Native` | JSON, paths, language/target enums, Win32 helpers |
| Model | `Model/` | `Gride::Model` | Projects, workspaces, CCS 7.4 reader |
| Settings | `Settings/` | `Gride::Settings` | `gride.settings.json` |
| Services | `Services/` | `Gride::Services` | Build engine, tool runner, converter, debugger, templates |
| UI | `UI/` | `Gride::UI` | WinForms: theme, editor, docking, panes, dialogs, main window |

## Class map

### Core
| Class | File | Role |
|---|---|---|
| `JsonValue`, `JsonParser`, `JsonWriter` | Json.h | Dependency-free JSON that keeps key order |
| `PathUtil` | PathUtil.h | Converts relative ↔ absolute paths (the `.pro` "one step difference") |
| `LanguageInfo`, `TargetInfo`, `ConfigName` | LanguageTarget.h | C90 / C++11 / Shalimar and Windows / Linux / macOS / C6747 |
| `NativeMethods` | NativeMethods.h/.cpp | WM_SETREDRAW, focus handle, rich-edit scroll position |

### Model
| Class | File | Role |
|---|---|---|
| `GrideProject`, `ProjectFolder`, `CcsInfo` | GrideProject.h | A `.pro` file: folders, files, targets, configurations, dependencies |
| `BuildConfiguration` | BuildConfiguration.h | One `Debug\|TMS320C6747`: output dirs, defines, include dirs, libs, linker .cmd |
| `Workspace` | Workspace.h | A `.grws` file: projects, startup project, active config/target, breakpoints, build order |
| `CcsProjectReader` | CcsProjectReader.h | Reads `.project`, `.cproject` and `.ccsproject` into a read-only `GrideProject` |

### Settings
| Class | Role |
|---|---|
| `GrideSettings` | Root object; holds all the classes below |
| `EnvironmentSettings` | Theme, font, tabs, folders, CCS `${CG_TOOL_ROOT}` |
| `ToolchainSettings` | One per target: tool paths, include/lib dirs, command templates, flags |
| `LanguageSettings` | Extensions, keywords, types, comment markers |
| `ConverterSettings`, `LayoutSettings`, `RecentList`, `ExternalTool` | Converter tools, window layout, recent files, external tools |
| `SettingsManager` | Finds, loads and saves the settings file (portable or `%APPDATA%`) |

### Services
| Class | Role |
|---|---|
| `BuildEngine` | Runs compile → assemble → link on a worker thread. Handles up-to-date checks, clean, batch builds and missing-tool errors |
| `MacroExpander` | Expands `$(Name)` in command templates |
| `ToolRunner` | Starts a tool and streams its output |
| `BuildMessage`, `BuildMessageParser` | Recognises MSVC-, TI- and GNU-style error lines |
| `CodeConverter` | C ⇄ Shalimar through the converter tools |
| `DebugEngine` (abstract), `PlaceholderDebugger` | Debugger contract, plus a simulation of it |
| `ProjectTemplates`, `ItemTemplate` | Starter files and the C6747 linker command file |

### UI
| Class | Role |
|---|---|
| `Theme`, `ThemeColorTable`, `ThemeRenderer`, `ThemeManager` | Dark / Light / Blue themes for every control |
| `IconFactory` | Draws every icon in code (no image files) |
| `Command`, `CommandRegistry`, `KeyText` | One command per action, shared by menus, toolbars, shortcuts and Ctrl+K chords |
| `TabStrip`, `StartPage` | Themed tab headers; the page shown when no file is open |
| `CodeEditor`, `CodeTextBox`, `LineGutter`, `SyntaxHighlighter`, `UndoManager` | The editor |
| `EditorDocument`, `DocumentManager` | Open files (encoding, line endings) and the tabbed document area |
| `ToolPane`, `DockArea`, `FloatWindow` | Docking: left/right/bottom areas, tabs, float and dock back |
| `ProjectExplorerPane`, `ClassViewPane`, `FilesPane` | Left panes |
| `PropertiesPane`, `BreakpointsPane` | Right panes |
| `OutputPane`, `ErrorListPane`, `FindResultsPane`, `TerminalPane` | Bottom panes |
| `WatchPane`, `LocalsPane`, `CallStackPane`, `RegistersPane`, `MemoryPane`, `DisassemblyPane` | Debug panes |
| `ThemedDialog`, `InputDialog`, `TextDialog` | Dialog base classes |
| `NewProjectDialog`, `NewItemDialog`, `ProjectPropertiesDialog`, `ConfigurationManagerDialog`, `DependenciesDialog`, `BatchBuildDialog` | Project dialogs |
| `OptionsDialog` plus its pages | Everything in `gride.settings.json` |
| `FindReplaceDialog`, `GoToLineDialog`, `WindowsDialog`, `SnippetsDialog` | Edit dialogs |
| `ConvertDialog`, `ToolchainCheckDialog`, `ExternalToolsDialog`, `ShortcutsDialog`, `AboutDialog` | Tools and Help dialogs |
| `MainForm` | Main window, split across 5 `.cpp` files (one per menu group) |

## Main flows

| Flow | Path through the classes |
|---|---|
| **Open a workspace** | `MainForm::OpenPath` → `Workspace::Load` → `GrideProject::Load` / `CcsProjectReader::Read` → `SetWorkspace` → explorer and class view refresh |
| **Build (F7)** | `MainForm::StartBuild` → `BuildEngine::StartAsync` (worker thread) → `ToolRunner` per step → output, messages and finish events → `BeginInvoke` → Output pane, Error List, status bar |
| **Debug (F5)** | Build the startup project → `LaunchDebugger` → `DebugEngine::Start` → `Stopped` event → execution marker in the editor, then the debug panes refresh |
| **Breakpoints** | One `SortedSet<int>` per file, shared by the editor gutter, the `Workspace` (saved in `.grws`) and the debugger |
| **Settings** | `OptionsDialog` edits a clone → OK → `SettingsManager::Save` → `MainForm::ApplySettings` re-applies theme, key bindings and editor options |
