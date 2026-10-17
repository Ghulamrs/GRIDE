// =============================================================================
//  MainForm.Commands.cpp - the command table, menus, toolbars, context menus
//
//  Every action exists once, as a Command with an id ("Build.BuildProject").
//  Menus, toolbar buttons, context menus and keyboard shortcuts all point to
//  the same Command, so enabling/disabling and shortcuts stay consistent.
// =============================================================================
#include "../pch.h"
#include "MainForm.h"

// shorthand for member-function delegates
#define DO(m)   gcnew Action(this, &MainForm::m)
#define WHEN(m) gcnew Func<bool>(this, &MainForm::m)

namespace Gride { namespace UI {

    Command^ MainForm::Cmd(String^ id, String^ text, String^ keys, String^ icon, Action^ handler)
    {
        return commands->Add(id, text, keys, icon, handler);
    }

    // =========================================================================
    void MainForm::RegisterCommands()
    {
        Command^ c;

        // ------------------------------------------------------------- File
        commands->BeginCategory("File");
        Cmd("File.NewProject", "&Project...", "Ctrl+Shift+N", "new", DO(FileNewProject));
        Cmd("File.NewWorkspace", "&Workspace...", nullptr, "workspace", DO(FileNewWorkspace));
        Cmd("File.NewFile", "&File...", "Ctrl+N", "file", DO(FileNewFile));
        Cmd("File.OpenWorkspace", "&Project / Workspace...", "Ctrl+Shift+O", "open", DO(FileOpenWorkspace));
        Cmd("File.OpenFile", "&File...", "Ctrl+O", "open", DO(FileOpenFile));
        c = Cmd("File.OpenCcs", "&CCS 7.4 / 5.5 Project...", nullptr, "projectccs", DO(FileOpenCcs)); c->CanExecute = WHEN(CcsEnabled);
        c = Cmd("File.ImportCcs", "&CCS 7.4 / 5.5 Project...", nullptr, "projectccs", DO(FileOpenCcs)); c->CanExecute = WHEN(CcsEnabled);
        Cmd("File.ImportFolder", "&Existing Folder...", nullptr, "folder", DO(FileImportFolder));
        c = Cmd("File.Close", "&Close", "Ctrl+F4", "close", DO(FileClose)); c->CanExecute = WHEN(HasDocument);
        c = Cmd("File.CloseWorkspace", "Close &Workspace", nullptr, nullptr, DO(FileCloseWorkspace)); c->CanExecute = WHEN(HasWorkspace);
        c = Cmd("File.Save", "&Save", "Ctrl+S", "save", DO(FileSave)); c->CanExecute = WHEN(HasDocument);
        c = Cmd("File.SaveAs", "Save &As...", nullptr, nullptr, DO(FileSaveAs)); c->CanExecute = WHEN(HasDocument);
        Cmd("File.SaveAll", "Save A&ll", "Ctrl+Shift+S", "saveall", DO(FileSaveAll));
        c = Cmd("File.ExportZip", "Export Project as &ZIP...", nullptr, nullptr, DO(FileExportZip)); c->CanExecute = WHEN(HasProject);
        Cmd("File.PageSetup", "Page Set&up...", nullptr, nullptr, DO(FilePageSetup));
        c = Cmd("File.Print", "&Print...", "Ctrl+P", nullptr, DO(FilePrint)); c->CanExecute = WHEN(HasDocument);
        Cmd("File.Exit", "E&xit", "Alt+F4", nullptr, DO(FileExit))->SetDisplayOnly();
        c = Cmd("File.OpenSelected", "&Open", nullptr, "open", DO(FileOpenSelected)); c->CanExecute = WHEN(HasSelectedFile);

        // ------------------------------------------------------------- Edit
        commands->BeginCategory("Edit");
        Cmd("Edit.Undo", "&Undo", "Ctrl+Z", "undo", DO(EditUndo))->SetDisplayOnly();
        Cmd("Edit.Redo", "&Redo", "Ctrl+Y", "redo", DO(EditRedo))->SetDisplayOnly();
        Cmd("Edit.Cut", "Cu&t", "Ctrl+X", "cut", DO(EditCut))->SetDisplayOnly();
        Cmd("Edit.Copy", "&Copy", "Ctrl+C", "copy", DO(EditCopy))->SetDisplayOnly();
        Cmd("Edit.Paste", "&Paste", "Ctrl+V", "paste", DO(EditPaste))->SetDisplayOnly();
        Cmd("Edit.Delete", "&Delete", "Del", nullptr, DO(EditDelete))->SetDisplayOnly();
        Cmd("Edit.SelectAll", "Select &All", "Ctrl+A", nullptr, DO(EditSelectAll))->SetDisplayOnly();
        Cmd("Edit.Find", "&Find...", "Ctrl+F", "find", DO(EditFind));
        Cmd("Edit.Replace", "&Replace...", "Ctrl+H", nullptr, DO(EditReplace));
        Cmd("Edit.FindInFiles", "Find in F&iles...", "Ctrl+Shift+F", "find", DO(EditFindInFiles));
        c = Cmd("Edit.GoToLine", "&Go To Line...", "Ctrl+G", nullptr, DO(EditGoToLine)); c->CanExecute = WHEN(HasDocument);
        c = Cmd("Edit.Comment", "Comment Selection", "Ctrl+K,C", "comment", DO(EditComment)); c->CanExecute = WHEN(HasDocument);
        c = Cmd("Edit.Uncomment", "Uncomment Selection", "Ctrl+K,U", "uncomment", DO(EditUncomment)); c->CanExecute = WHEN(HasDocument);
        c = Cmd("Edit.Format", "Format Document", "Ctrl+K,D", nullptr, DO(EditFormat)); c->CanExecute = WHEN(HasDocument);
        c = Cmd("Edit.Upper", "Make Uppercase", "Ctrl+Shift+U", nullptr, DO(EditUpper)); c->CanExecute = WHEN(HasDocument);
        c = Cmd("Edit.Lower", "Make Lowercase", "Ctrl+U", nullptr, DO(EditLower)); c->CanExecute = WHEN(HasDocument);
        c = Cmd("Edit.Tabify", "Tabify / Untabify", nullptr, nullptr, DO(EditTabify)); c->CanExecute = WHEN(HasDocument);
        c = Cmd("Edit.Indent", "Increase Indent", nullptr, "indent", DO(EditIndent)); c->CanExecute = WHEN(HasDocument);
        c = Cmd("Edit.Outdent", "Decrease Indent", nullptr, "outdent", DO(EditOutdent)); c->CanExecute = WHEN(HasDocument);
        c = Cmd("Edit.ToggleBookmark", "Toggle Bookmark", "Ctrl+F2", "bookmark", DO(EditToggleBookmark)); c->CanExecute = WHEN(HasDocument);
        c = Cmd("Edit.NextBookmark", "Next Bookmark", "F2", "nextbm", DO(EditNextBookmark)); c->CanExecute = WHEN(HasDocument);
        c = Cmd("Edit.PrevBookmark", "Previous Bookmark", "Shift+F2", "prevbm", DO(EditPrevBookmark)); c->CanExecute = WHEN(HasDocument);
        c = Cmd("Edit.ClearBookmarks", "Clear All Bookmarks", nullptr, nullptr, DO(EditClearBookmarks)); c->CanExecute = WHEN(HasDocument);

        // ------------------------------------------------------------- View
        commands->BeginCategory("View");
        array<ToolPane^>^ viewPanes = gcnew array<ToolPane^>{ explorer, classView, filesPane, properties, output, errorList, findResults, terminal };
        array<String^>^ viewIds = gcnew array<String^>{ "View.ProjectExplorer", "View.ClassView", "View.Files", "View.Properties",
                                                         "View.Output", "View.ErrorList", "View.FindResults", "View.Terminal" };
        array<String^>^ viewKeys = gcnew array<String^>{ "Ctrl+Alt+L", "Ctrl+Shift+C", nullptr, "F4", "Ctrl+Alt+O", "Ctrl+\\,E", nullptr, "Ctrl+`" };
        for (int i = 0; i < viewPanes->Length; i++)
        {
            PaneToggle^ pt = gcnew PaneToggle(viewPanes[i], false);
            c = Cmd(viewIds[i], viewPanes[i]->Title, viewKeys[i], viewPanes[i]->IconName, gcnew Action(pt, &PaneToggle::Run));
            c->IsChecked = gcnew Func<bool>(pt, &PaneToggle::IsShown);
        }
        c = Cmd("View.StatusBar", "Status &Bar", nullptr, nullptr, DO(ViewStatusBar)); c->IsChecked = WHEN(IsStatusBar);
        c = Cmd("View.FullScreen", "F&ull Screen", "Shift+Alt+Enter", nullptr, DO(ViewFullScreen)); c->IsChecked = WHEN(IsFullScreen);
        for each (String^ name in Theme::Names())
        {
            ValueCommand^ vc = gcnew ValueCommand(name, gcnew Action<String^>(this, &MainForm::SetTheme), gcnew Func<String^>(this, &MainForm::CurrentTheme));
            c = Cmd("View.Theme" + name, name, nullptr, nullptr, gcnew Action(vc, &ValueCommand::Run));
            c->IsChecked = gcnew Func<bool>(vc, &ValueCommand::IsCurrent);
        }
        c = Cmd("View.ZoomIn", "Zoom &In", "Ctrl++", nullptr, DO(ViewZoomIn)); c->CanExecute = WHEN(HasDocument);
        c = Cmd("View.ZoomOut", "Zoom &Out", "Ctrl+-", nullptr, DO(ViewZoomOut)); c->CanExecute = WHEN(HasDocument);
        c = Cmd("View.ZoomReset", "&Reset Zoom", "Ctrl+0", nullptr, DO(ViewZoomReset)); c->CanExecute = WHEN(HasDocument);
        c = Cmd("View.LineNumbers", "&Line Numbers", nullptr, nullptr, DO(ViewLineNumbers)); c->IsChecked = WHEN(IsLineNumbers);
        c = Cmd("View.Whitespace", "Show &White Space", nullptr, nullptr, DO(ViewWhitespace)); c->IsChecked = WHEN(IsWhitespace);

        // ------------------------------------------------------------- Project
        commands->BeginCategory("Project");
        c = Cmd("Project.AddNewItem", "Add &New Item...", "Ctrl+Shift+A", "new", DO(ProjectAddNewItem)); c->CanExecute = WHEN(HasWritableProject);
        c = Cmd("Project.AddExisting", "Add E&xisting Item...", "Shift+Alt+A", "open", DO(ProjectAddExisting)); c->CanExecute = WHEN(HasWritableProject);
        c = Cmd("Project.NewFolder", "New &Folder...", nullptr, "folder", DO(ProjectNewFolder)); c->CanExecute = WHEN(HasWritableProject);
        // F2 / Del act on the explorer only while it has the focus (MainForm::ProcessCmdKey)
        c = Cmd("Project.Remove", "&Remove from Project", "Del", "close", DO(ProjectRemoveItem)); c->CanExecute = WHEN(HasSelectedFile); c->SetDisplayOnly();
        c = Cmd("Project.Rename", "Re&name", "F2", nullptr, DO(ProjectRename)); c->CanExecute = WHEN(HasSelectedFile); c->SetDisplayOnly();
        c = Cmd("Project.Exclude", "E&xclude From Build", nullptr, nullptr, DO(ProjectExclude));
        c->CanExecute = WHEN(HasSelectedFile); c->IsChecked = WHEN(IsSelectedExcluded);
        Cmd("Project.AddProject", "Add Project to &Workspace...", nullptr, "project", DO(ProjectAddProject));
        c = Cmd("Project.RemoveProject", "Remove Project from Workspace", nullptr, nullptr, DO(ProjectRemoveProject)); c->CanExecute = WHEN(HasProject);
        c = Cmd("Project.SetStartup", "Set as &Startup Project", nullptr, nullptr, DO(ProjectSetStartup)); c->CanExecute = WHEN(HasProject);
        c = Cmd("Project.Dependencies", "Project &Dependencies...", nullptr, nullptr, DO(ProjectDependencies)); c->CanExecute = WHEN(HasWorkspace);
        c = Cmd("Project.BuildOrder", "Build &Order...", nullptr, nullptr, DO(ProjectBuildOrder)); c->CanExecute = WHEN(HasWorkspace);
        c = Cmd("Project.RefreshCcs", "Refresh Imported Project", nullptr, "refresh", DO(ProjectRefreshCcs)); c->CanExecute = WHEN(HasProject);
        c = Cmd("Project.CcsMapping", "CCS / VS &Mapping...", nullptr, "info", DO(ProjectCcsMapping)); c->CanExecute = WHEN(HasImportedProject);
        c = Cmd("Project.CompilerOptions", "Compiler &Options...", nullptr, "settings", DO(ProjectCompilerOptions)); c->CanExecute = WHEN(HasProject);
        c = Cmd("Project.OpenFolder", "Open Containing Folder", nullptr, "folderopen", DO(ProjectOpenFolder)); c->CanExecute = WHEN(HasWorkspace);
        c = Cmd("Project.Properties", "&Properties...", "Alt+F7", "properties", DO(ProjectShowProperties)); c->CanExecute = WHEN(HasProject);

        // ------------------------------------------------------------- Build
        commands->BeginCategory("Build");
        c = Cmd("Build.BuildWorkspace", "&Build Workspace", "F7", "build", DO(BuildWorkspace)); c->CanExecute = WHEN(CanBuild);
        c = Cmd("Build.RebuildWorkspace", "&Rebuild Workspace", nullptr, "rebuild", DO(RebuildWorkspace)); c->CanExecute = WHEN(CanBuild);
        c = Cmd("Build.CleanWorkspace", "&Clean Workspace", nullptr, "clean", DO(CleanWorkspace)); c->CanExecute = WHEN(CanBuild);
        c = Cmd("Build.BuildProject", "Build Project", "Ctrl+B", "build", DO(BuildProject)); c->CanExecute = WHEN(CanBuild);
        c = Cmd("Build.RebuildProject", "Rebuild Project", nullptr, "rebuild", DO(RebuildProject)); c->CanExecute = WHEN(CanBuild);
        c = Cmd("Build.CleanProject", "Clean Project", nullptr, "clean", DO(CleanProject)); c->CanExecute = WHEN(CanBuild);
        c = Cmd("Build.Compile", "Compile", "Ctrl+F7", nullptr, DO(CompileFile)); c->CanExecute = WHEN(CanBuild);
        c = Cmd("Build.AssembleOnly", "Assemble Only (ASM6x / MASM)", nullptr, nullptr, DO(AssembleOnly)); c->CanExecute = WHEN(CanBuild);
        c = Cmd("Build.LinkOnly", "Link Only (LNK6x / LINK)", nullptr, nullptr, DO(LinkOnly)); c->CanExecute = WHEN(CanBuild);
        c = Cmd("Build.Batch", "Batch B&uild...", nullptr, nullptr, DO(BatchBuild)); c->CanExecute = WHEN(CanBuild);
        c = Cmd("Build.ConfigManager", "Configuration &Manager...", nullptr, "settings", DO(ConfigurationManager)); c->CanExecute = WHEN(HasWorkspace);
        c = Cmd("Build.Cancel", "Cance&l Build", "Ctrl+Break", "stop", DO(CancelBuild)); c->CanExecute = WHEN(IsBuilding);

        // ------------------------------------------------------------- Debug
        commands->BeginCategory("Debug");
        c = Cmd("Debug.Start", "&Start Debugging", "F5", "debug", DO(DebugStart)); c->CanExecute = WHEN(CanStartDebug);
        c = Cmd("Debug.StartNoDebug", "Start &Without Debugging", "Ctrl+F5", "run", DO(DebugStartNoDebug)); c->CanExecute = WHEN(CanBuild);
        // RIDE's Build > Emulate on vm6747 and Build > Verify (tms6747), and the stop of a program running in the Output pane
        c = Cmd("Debug.Emulate", "&Emulate on VM6747", nullptr, "run", DO(DebugEmulate)); c->CanExecute = WHEN(CanBuild);
        c = Cmd("Debug.Verify", "&Verify (vm6747 against sim6747)", nullptr, nullptr, DO(DebugVerify)); c->CanExecute = WHEN(CanBuild);
        c = Cmd("Debug.StopProgram", "Stop &Program", nullptr, "stop", DO(StopProgram)); c->CanExecute = WHEN(IsRunning);
        c = Cmd("Debug.Stop", "S&top Debugging", "Shift+F5", "stop", DO(DebugStop)); c->CanExecute = WHEN(IsDebugging);
        c = Cmd("Debug.Restart", "&Restart", "Ctrl+Shift+F5", "restart", DO(DebugRestart)); c->CanExecute = WHEN(IsDebugging);
        c = Cmd("Debug.BreakAll", "Break &All", "Ctrl+Alt+Break", "pause", DO(DebugBreakAll)); c->CanExecute = WHEN(IsDebugging);
        c = Cmd("Debug.StepInto", "Step &Into", "F11", "stepinto", DO(DebugStepInto)); c->CanExecute = WHEN(IsInBreak);
        c = Cmd("Debug.StepOver", "Step &Over", "F10", "stepover", DO(DebugStepOver)); c->CanExecute = WHEN(IsInBreak);
        c = Cmd("Debug.StepOut", "Step O&ut", "Shift+F11", "stepout", DO(DebugStepOut)); c->CanExecute = WHEN(IsInBreak);
        c = Cmd("Debug.RunToCursor", "Run to &Cursor", "Ctrl+F10", nullptr, DO(DebugRunToCursor)); c->CanExecute = WHEN(IsInBreak);
        // RIDE's Debug menu: up and down the stack (its Ctrl-Up / Ctrl-Down), a watch, a variable set
        c = Cmd("Debug.UpStack", "&Up the Stack", nullptr, nullptr, DO(DebugUpStack)); c->CanExecute = WHEN(IsInBreak);
        c = Cmd("Debug.DownStack", "Do&wn the Stack", nullptr, nullptr, DO(DebugDownStack)); c->CanExecute = WHEN(IsInBreak);
        c = Cmd("Debug.AddWatch", "&Watch Expression...", nullptr, "find", DO(DebugAddWatch)); c->CanExecute = WHEN(IsDebugging);
        c = Cmd("Debug.SetVariable", "Set &Variable...", nullptr, "variable", DO(DebugSetVariable)); c->CanExecute = WHEN(IsInBreak);
        c = Cmd("Debug.ToggleBreakpoint", "Toggle &Breakpoint", "F9", "breakpoint", DO(DebugToggleBreakpoint)); c->CanExecute = WHEN(HasDocument);
        Cmd("Debug.DeleteAllBreakpoints", "&Delete All Breakpoints", "Ctrl+Shift+F9", nullptr, DO(DebugDeleteAllBreakpoints));
        array<ToolPane^>^ dbgPanes = gcnew array<ToolPane^>{ watch, locals, callStack, registers, memory, disasm, bpPane };
        array<String^>^ dbgIds = gcnew array<String^>{ "Debug.WinWatch", "Debug.WinLocals", "Debug.WinCallStack", "Debug.WinRegisters",
                                                        "Debug.WinMemory", "Debug.WinDisassembly", "Debug.WinBreakpoints" };
        array<String^>^ dbgKeys = gcnew array<String^>{ "Ctrl+Alt+W", "Ctrl+Alt+V", "Ctrl+Alt+C", "Ctrl+Alt+G", "Ctrl+Alt+M", "Ctrl+Alt+D", "Ctrl+Alt+B" };
        for (int i = 0; i < dbgPanes->Length; i++)
        {
            PaneToggle^ pt = gcnew PaneToggle(dbgPanes[i], true);
            c = Cmd(dbgIds[i], dbgPanes[i]->Title, dbgKeys[i], dbgPanes[i]->IconName, gcnew Action(pt, &PaneToggle::Run));
            c->IsChecked = gcnew Func<bool>(pt, &PaneToggle::IsShown);
        }
        Cmd("Debug.Connect", "Connect to VM6747...", nullptr, nullptr, DO(DebugConnect));
        c = Cmd("Debug.LoadProgram", "Load Program (.out)...", nullptr, "open", DO(DebugLoadProgram)); c->CanExecute = WHEN(CanStartDebug);

        // ------------------------------------------------------------- Tools
        commands->BeginCategory("Tools");
        Cmd("Tools.CToShalimar", "C  ->  Shalimar...", "Ctrl+Alt+S", "convert", DO(ToolsCToShalimar));
        Cmd("Tools.ShalimarToC", "Shalimar  ->  C...", "Ctrl+Alt+H", "convert", DO(ToolsShalimarToC));
        Cmd("Tools.ToolchainCheck", "&Toolchain Check...", nullptr, "info", DO(ToolsToolchainCheck));
        Cmd("Tools.CommandPrompt", "Toolchain &Command Prompt", nullptr, "terminal", DO(ToolsCommandPrompt));
        Cmd("Tools.ExternalTools", "&External Tools...", nullptr, nullptr, DO(ToolsExternalTools));
        Cmd("Tools.Snippets", "Code &Snippets Manager...", nullptr, nullptr, DO(ToolsSnippets));
        Cmd("Tools.Keyboard", "Customize &Keyboard...", nullptr, nullptr, DO(ToolsKeyboard));
        Cmd("Tools.Options", "&Options...", nullptr, "settings", DO(ToolsOptions));

        // ------------------------------------------------------------- Window
        commands->BeginCategory("Window");
        c = Cmd("Window.Split", "&Split Editor", nullptr, nullptr, DO(WindowSplit)); c->CanExecute = WHEN(HasDocument); c->IsChecked = WHEN(IsSplit);
        Cmd("Window.Float", "&Float", nullptr, "float", DO(WindowFloat));
        Cmd("Window.Dock", "&Dock", nullptr, nullptr, DO(WindowDock));
        Cmd("Window.AutoHide", "Auto &Hide All", nullptr, nullptr, DO(WindowAutoHide));
        c = Cmd("Window.PinTab", "&Pin Tab", nullptr, nullptr, DO(WindowPinTab)); c->CanExecute = WHEN(HasDocument);
        c = Cmd("Window.CloseAll", "Close &All Documents", "Ctrl+Shift+F4", nullptr, DO(WindowCloseAll)); c->CanExecute = WHEN(HasDocument);
        c = Cmd("Window.CloseAllButThis", "Close All &But This", nullptr, nullptr, DO(WindowCloseAllButThis)); c->CanExecute = WHEN(HasDocument);
        Cmd("Window.ResetLayout", "&Reset Window Layout", nullptr, nullptr, DO(WindowResetLayout));
        Cmd("Window.SaveLayout", "Sa&ve Window Layout", nullptr, nullptr, DO(WindowSaveLayout));
        c = Cmd("Window.Windows", "&Windows...", nullptr, nullptr, DO(WindowList)); c->CanExecute = WHEN(HasDocument);

        // ------------------------------------------------------------- Help
        commands->BeginCategory("Help");
        Cmd("Help.ViewHelp", "View &Help", "F1", "help", DO(HelpView));
        Cmd("Help.UserGuide", "GRIDE &User Guide", nullptr, nullptr, DO(HelpUserGuide));
        array<String^>^ refIds = gcnew array<String^>{ "Help.RefC90", "Help.RefCpp11", "Help.RefShalimar",
                                                        "Help.RefMasm", "Help.RefAsm6x", "Help.RefRts", "Help.RefVm" };
        array<String^>^ refText = gcnew array<String^>{ "C90", "C++11", "Shalimar",
                                                         "MASM / LINK (Windows)", "ASM6x / LNK6x", "RTS6747 Runtime", "VM6747 / SIM6747" };
        array<String^>^ refFile = gcnew array<String^>{ "C90_Reference", "Cpp11_Reference", "Shalimar_Reference",
                                                         "MASM_LINK_Reference", "ASM6x_LNK6x_Reference", "RTS6747_Reference", "VM6747_Reference" };
        for (int i = 0; i < refIds->Length; i++)
        {
            ValueCommand^ vc = gcnew ValueCommand(refFile[i], gcnew Action<String^>(this, &MainForm::OpenHelpFile), nullptr);
            Cmd(refIds[i], refText[i], nullptr, nullptr, gcnew Action(vc, &ValueCommand::Run));
        }
        Cmd("Help.Shortcuts", "&Keyboard Shortcuts", nullptr, nullptr, DO(HelpShortcuts));
        Cmd("Help.ReleaseNotes", "&Release Notes", nullptr, nullptr, DO(HelpReleaseNotes));
        Cmd("Help.About", "&About GRIDE...", nullptr, nullptr, DO(HelpAbout));
    }

    // =========================================================================
    //  menus
    // =========================================================================
    ToolStripMenuItem^ MainForm::TopMenu(String^ text)
    {
        ToolStripMenuItem^ m = gcnew ToolStripMenuItem(text);
        m->DropDownOpening += gcnew EventHandler(this, &MainForm::OnMenuOpening);
        menu->Items->Add(m);
        return m;
    }

    ToolStripMenuItem^ MainForm::SubMenu(ToolStripMenuItem^ parent, String^ text)
    {
        ToolStripMenuItem^ m = gcnew ToolStripMenuItem(text);
        m->DropDownOpening += gcnew EventHandler(this, &MainForm::OnMenuOpening);
        parent->DropDownItems->Add(m);
        return m;
    }

    // "-" adds a separator
    void MainForm::AddItems(ToolStripMenuItem^ parent, array<String^>^ ids)
    {
        for each (String^ id in ids)
        {
            if (id == "-") parent->DropDownItems->Add(gcnew ToolStripSeparator());
            else parent->DropDownItems->Add(commands->MenuItem(id));
        }
    }

    void MainForm::BuildMenus()
    {
        // ---- File
        ToolStripMenuItem^ file = TopMenu("&File");
        file->DropDownOpening += gcnew EventHandler(this, &MainForm::OnFileMenuOpening);
        AddItems(SubMenu(file, "&New"), gcnew array<String^>{ "File.NewProject", "File.NewWorkspace", "File.NewFile" });
        AddItems(SubMenu(file, "&Open"), gcnew array<String^>{ "File.OpenWorkspace", "File.OpenFile", "File.OpenCcs" });
        AddItems(file, gcnew array<String^>{ "File.Close", "File.CloseWorkspace", "-", "File.Save", "File.SaveAs", "File.SaveAll", "-" });
        AddItems(SubMenu(file, "&Import"), gcnew array<String^>{ "File.ImportCcs", "File.ImportFolder" });
        AddItems(file, gcnew array<String^>{ "File.ExportZip", "-", "File.PageSetup", "File.Print", "-" });
        miRecentFiles = SubMenu(file, "Recent &Files");
        miRecentWorkspaces = SubMenu(file, "Recent &Workspaces");
        AddItems(file, gcnew array<String^>{ "-", "File.Exit" });

        // ---- Edit
        ToolStripMenuItem^ edit = TopMenu("&Edit");
        AddItems(edit, gcnew array<String^>{ "Edit.Undo", "Edit.Redo", "-", "Edit.Cut", "Edit.Copy", "Edit.Paste", "Edit.Delete",
                                             "Edit.SelectAll", "-", "Edit.Find", "Edit.Replace", "Edit.FindInFiles", "Edit.GoToLine", "-" });
        AddItems(SubMenu(edit, "Ad&vanced"), gcnew array<String^>{ "Edit.Comment", "Edit.Uncomment", "Edit.Format", "-",
                                                                   "Edit.Upper", "Edit.Lower", "Edit.Tabify", "-", "Edit.Indent", "Edit.Outdent" });
        AddItems(SubMenu(edit, "&Bookmarks"), gcnew array<String^>{ "Edit.ToggleBookmark", "Edit.NextBookmark", "Edit.PrevBookmark", "Edit.ClearBookmarks" });

        // ---- View
        ToolStripMenuItem^ view = TopMenu("&View");
        AddItems(view, gcnew array<String^>{ "View.ProjectExplorer", "View.ClassView", "View.Files", "View.Properties", "View.Output",
                                             "View.ErrorList", "View.FindResults", "View.Terminal", "-" });
        ToolStripMenuItem^ bars = SubMenu(view, "&Toolbars");
        AddItems(view, gcnew array<String^>{ "View.StatusBar", "View.FullScreen", "-" });
        ToolStripMenuItem^ themes = SubMenu(view, "T&heme");
        for each (String^ n in Theme::Names()) themes->DropDownItems->Add(commands->MenuItem("View.Theme" + n));
        AddItems(view, gcnew array<String^>{ "-", "View.ZoomIn", "View.ZoomOut", "View.ZoomReset", "-", "View.LineNumbers", "View.Whitespace" });
        // toolbar toggles need the toolbars, created later: remember the submenu
        bars->Tag = "toolbars";

        // ---- Project
        ToolStripMenuItem^ project = TopMenu("&Project");
        AddItems(project, gcnew array<String^>{ "Project.AddNewItem", "Project.AddExisting", "Project.NewFolder", "Project.Remove",
                                                "Project.Rename", "Project.Exclude", "-", "Project.AddProject", "Project.RemoveProject",
                                                "Project.SetStartup", "-", "Project.Dependencies", "Project.BuildOrder", "-",
                                                "Project.RefreshCcs", "Project.CcsMapping", "Project.OpenFolder", "-", "Project.CompilerOptions", "Project.Properties" });

        // ---- Build
        miBuild = TopMenu("&Build");
        miBuild->DropDownOpening += gcnew EventHandler(this, &MainForm::OnBuildMenuOpening);
        AddItems(miBuild, gcnew array<String^>{ "Build.BuildWorkspace", "Build.RebuildWorkspace", "Build.CleanWorkspace", "-",
                                                "Build.BuildProject", "Build.RebuildProject", "Build.CleanProject", "-",
                                                "Build.Compile", "Build.AssembleOnly", "Build.LinkOnly", "-",
                                                "Build.Batch", "Build.ConfigManager", "-", "Build.Cancel" });

        // ---- Debug
        ToolStripMenuItem^ debug = TopMenu("&Debug");
        AddItems(debug, gcnew array<String^>{ "Debug.Start", "Debug.StartNoDebug", "Debug.Emulate", "Debug.Verify", "Debug.StopProgram", "-",
                                              "Debug.Stop", "Debug.Restart", "Debug.BreakAll", "-",
                                              "Debug.StepInto", "Debug.StepOver", "Debug.StepOut", "Debug.RunToCursor", "-",
                                              "Debug.UpStack", "Debug.DownStack", "Debug.AddWatch", "Debug.SetVariable", "-",
                                              "Debug.ToggleBreakpoint", "Debug.DeleteAllBreakpoints", "-" });
        AddItems(SubMenu(debug, "&Windows"), gcnew array<String^>{ "Debug.WinWatch", "Debug.WinLocals", "Debug.WinCallStack",
                                                                   "Debug.WinRegisters", "Debug.WinMemory", "Debug.WinDisassembly",
                                                                   "Debug.WinBreakpoints" });
        AddItems(debug, gcnew array<String^>{ "-", "Debug.Connect", "Debug.LoadProgram" });

        // ---- Tools
        miTools = TopMenu("&Tools");
        miTools->DropDownOpening += gcnew EventHandler(this, &MainForm::OnToolsMenuOpening);
        AddItems(SubMenu(miTools, "&Convert"), gcnew array<String^>{ "Tools.CToShalimar", "Tools.ShalimarToC" });
        AddItems(miTools, gcnew array<String^>{ "-", "Tools.ToolchainCheck", "Tools.CommandPrompt", "Tools.ExternalTools", "-",
                                                "Tools.Snippets", "Tools.Keyboard", "-", "Tools.Options" });

        // ---- Window
        miWindow = TopMenu("&Window");
        miWindow->DropDownOpening += gcnew EventHandler(this, &MainForm::OnWindowMenuOpening);
        AddItems(miWindow, gcnew array<String^>{ "Window.Split", "Window.Float", "Window.Dock", "Window.AutoHide", "Window.PinTab", "-",
                                                 "Window.CloseAll", "Window.CloseAllButThis", "-", "Window.ResetLayout", "Window.SaveLayout",
                                                 "-", "Window.Windows" });

        // ---- Help
        ToolStripMenuItem^ help = TopMenu("&Help");
        AddItems(help, gcnew array<String^>{ "Help.ViewHelp", "Help.UserGuide" });
        AddItems(SubMenu(help, "&Language Reference"), gcnew array<String^>{ "Help.RefC90", "Help.RefCpp11", "Help.RefShalimar" });
        AddItems(SubMenu(help, "&Toolchain Reference"), gcnew array<String^>{ "Help.RefMasm", "Help.RefAsm6x", "Help.RefRts", "Help.RefVm" });
        AddItems(help, gcnew array<String^>{ "Help.Shortcuts", "-", "Help.ReleaseNotes", "Help.About" });
    }

    // =========================================================================
    //  toolbars
    // =========================================================================
    void MainForm::BuildToolbars()
    {
        tbStandard = PaneUtil::MakeBar();
        tbStandard->Name = "Standard";
        tbStandard->Dock = DockStyle::None;
        tbStandard->Items->Add(commands->Button("File.NewProject", false));
        tbStandard->Items->Add(commands->Button("File.OpenWorkspace", false));
        tbStandard->Items->Add(commands->Button("File.Save", false));
        tbStandard->Items->Add(commands->Button("File.SaveAll", false));
        tbStandard->Items->Add(gcnew ToolStripSeparator());
        tbStandard->Items->Add(commands->Button("Edit.Undo", false));
        tbStandard->Items->Add(commands->Button("Edit.Redo", false));
        tbStandard->Items->Add(gcnew ToolStripSeparator());
        tbStandard->Items->Add(gcnew ToolStripLabel("Config"));
        cbConfig = gcnew ToolStripComboBox();
        cbConfig->DropDownStyle = ComboBoxStyle::DropDownList;
        cbConfig->Items->AddRange(gcnew array<Object^>{ "Debug", "Release" });
        cbConfig->SelectedIndex = 0;
        cbConfig->Width = 80;
        cbConfig->SelectedIndexChanged += gcnew EventHandler(this, &MainForm::OnConfigChanged);
        tbStandard->Items->Add(cbConfig);
        tbStandard->Items->Add(gcnew ToolStripLabel("Target"));
        cbTarget = gcnew ToolStripComboBox();
        cbTarget->DropDownStyle = ComboBoxStyle::DropDownList;
        for each (Target t in TargetInfo::All()) cbTarget->Items->Add(TargetInfo::DisplayName(t));
        cbTarget->SelectedIndex = 0;
        cbTarget->Width = 170;
        cbTarget->SelectedIndexChanged += gcnew EventHandler(this, &MainForm::OnTargetChanged);
        tbStandard->Items->Add(cbTarget);
        tbStandard->Items->Add(gcnew ToolStripLabel("Language"));
        cbLanguage = gcnew ToolStripComboBox();
        cbLanguage->DropDownStyle = ComboBoxStyle::DropDownList;
        cbLanguage->Items->AddRange(gcnew array<Object^>{ "C90", "C++11", "Shalimar" });
        cbLanguage->SelectedIndex = 0;
        cbLanguage->Width = 90;
        cbLanguage->SelectedIndexChanged += gcnew EventHandler(this, &MainForm::OnLanguageChanged);
        tbStandard->Items->Add(cbLanguage);

        tbBuild = PaneUtil::MakeBar();
        tbBuild->Name = "Build";
        tbBuild->Dock = DockStyle::None;
        tbBuild->Items->Add(commands->Button("Build.BuildWorkspace", true));
        btnStart = commands->Button("Debug.Start", true);
        tbBuild->Items->Add(btnStart);
        tbBuild->Items->Add(commands->Button("Debug.Stop", false));
        tbBuild->Items->Add(gcnew ToolStripSeparator());
        ToolStripButton^ conv = commands->Button("Tools.CToShalimar", true);
        conv->Text = "C " + gcnew String(L'\x21C4', 1) + " Shalimar";
        tbBuild->Items->Add(conv);

        tbDebug = PaneUtil::MakeBar();
        tbDebug->Name = "Debug";
        tbDebug->Dock = DockStyle::None;
        for each (String^ id in gcnew array<String^>{ "Debug.BreakAll", "Debug.Restart", "Debug.StepInto", "Debug.StepOver",
                                                      "Debug.StepOut", "Debug.ToggleBreakpoint" })
            tbDebug->Items->Add(commands->Button(id, false));

        tbEditor = PaneUtil::MakeBar();
        tbEditor->Name = "Text Editor";
        tbEditor->Dock = DockStyle::None;
        for each (String^ id in gcnew array<String^>{ "Edit.Comment", "Edit.Uncomment", "Edit.Indent", "Edit.Outdent",
                                                      "Edit.ToggleBookmark", "Edit.PrevBookmark", "Edit.NextBookmark" })
            tbEditor->Items->Add(commands->Button(id, false));

        for each (ToolStrip^ ts in gcnew array<ToolStrip^>{ tbStandard, tbBuild, tbDebug, tbEditor })
        {
            ts->AutoSize = true;
            ts->Margin = WF::Padding(0, 0, 8, 0);
            toolbarHost->Controls->Add(ts);
        }

        // View > Toolbars submenu
        for each (ToolStripItem^ top in menu->Items)
        {
            ToolStripMenuItem^ tm = safe_cast<ToolStripMenuItem^>(top);
            for each (ToolStripItem^ sub in tm->DropDownItems)
            {
                ToolStripMenuItem^ sm = dynamic_cast<ToolStripMenuItem^>(sub);
                if (sm == nullptr || dynamic_cast<String^>(sm->Tag) != "toolbars") continue;
                sm->Tag = nullptr;
                for each (ToolStrip^ ts in gcnew array<ToolStrip^>{ tbStandard, tbBuild, tbDebug, tbEditor })
                {
                    ToolbarToggle^ tt = gcnew ToolbarToggle(ts);
                    Command^ c = commands->Add("View.Toolbar" + ts->Name->Replace(" ", ""), ts->Name, nullptr, nullptr,
                                               gcnew Action(tt, &ToolbarToggle::Run));
                    c->Category = "View";
                    c->IsChecked = gcnew Func<bool>(tt, &ToolbarToggle::IsShown);
                    sm->DropDownItems->Add(commands->MenuItem(c->Id));
                }
            }
        }
    }

    // =========================================================================
    //  Project Explorer context menus
    // =========================================================================
    System::Windows::Forms::ContextMenuStrip^ MainForm::MakeContextMenu(array<String^>^ ids)
    {
        System::Windows::Forms::ContextMenuStrip^ m = gcnew System::Windows::Forms::ContextMenuStrip();
        for each (String^ id in ids)
        {
            if (id == "-") m->Items->Add(gcnew ToolStripSeparator());
            else m->Items->Add(commands->MenuItem(id));
        }
        m->Opening += gcnew System::ComponentModel::CancelEventHandler(this, &MainForm::OnContextOpening);
        return m;
    }

    void MainForm::BuildContextMenus()
    {
        explorer->WorkspaceMenu = MakeContextMenu(gcnew array<String^>{ "Build.BuildWorkspace", "Build.RebuildWorkspace",
            "Build.CleanWorkspace", "-", "File.NewProject", "Project.AddProject", "File.ImportCcs", "-", "Build.ConfigManager",
            "Project.Dependencies", "Project.BuildOrder", "-", "Project.OpenFolder" });
        explorer->ProjectMenu = MakeContextMenu(gcnew array<String^>{ "Build.BuildProject", "Build.RebuildProject", "Build.CleanProject",
            "-", "Project.SetStartup", "-", "Project.AddNewItem", "Project.AddExisting", "Project.NewFolder", "-",
            "Project.RefreshCcs", "Project.CcsMapping", "Project.Dependencies", "Project.RemoveProject", "-", "Project.OpenFolder", "Project.CompilerOptions", "Project.Properties" });
        explorer->FolderMenu = MakeContextMenu(gcnew array<String^>{ "Project.AddNewItem", "Project.AddExisting", "-", "Project.OpenFolder" });
        explorer->FileMenu = MakeContextMenu(gcnew array<String^>{ "File.OpenSelected", "Build.Compile", "-", "Project.Exclude",
            "Project.Rename", "Project.Remove", "-", "Tools.CToShalimar", "Tools.ShalimarToC", "-", "Project.OpenFolder", "View.Properties" });
    }

    // one per editor (DocumentManager asks for it); the editor disposes it
    System::Windows::Forms::ContextMenuStrip^ MainForm::MakeEditorMenu()
    {
        return MakeContextMenu(gcnew array<String^>{ "Edit.Undo", "Edit.Redo", "-", "Edit.Cut", "Edit.Copy", "Edit.Paste", "-",
            "Edit.SelectAll", "-", "Debug.ToggleBreakpoint", "Edit.ToggleBookmark", "-", "Debug.RunToCursor" });
    }

    // =========================================================================
    //  dynamic menu content
    // =========================================================================
    void MainForm::OnMenuOpening(Object^ sender, EventArgs^ e)
    {
        ToolStripMenuItem^ mi = dynamic_cast<ToolStripMenuItem^>(sender);
        if (mi != nullptr) RefreshMenuTree(mi->DropDownItems);
    }

    void MainForm::OnContextOpening(Object^ sender, System::ComponentModel::CancelEventArgs^ e)
    {
        System::Windows::Forms::ContextMenuStrip^ cm = dynamic_cast<System::Windows::Forms::ContextMenuStrip^>(sender);
        if (cm == nullptr) return;
        OnBuildMenuOpening(nullptr, EventArgs::Empty);
        RefreshMenuTree(cm->Items);
    }

    void MainForm::OnFileMenuOpening(Object^ sender, EventArgs^ e)
    {
        miRecentFiles->DropDownItems->Clear();
        int n = 1;
        for each (String^ f in settings->Recent->Files)
        {
            ToolStripMenuItem^ mi = gcnew ToolStripMenuItem(String::Format("&{0} {1}", n++, f));
            mi->Tag = f;
            mi->Click += gcnew EventHandler(this, &MainForm::OnRecentFileClick);
            miRecentFiles->DropDownItems->Add(mi);
        }
        miRecentFiles->Enabled = miRecentFiles->DropDownItems->Count > 0;
        miRecentWorkspaces->DropDownItems->Clear();
        n = 1;
        for each (String^ f in settings->Recent->Workspaces)
        {
            ToolStripMenuItem^ mi = gcnew ToolStripMenuItem(String::Format("&{0} {1}", n++, f));
            mi->Tag = f;
            mi->Click += gcnew EventHandler(this, &MainForm::OnRecentWorkspaceClick);
            miRecentWorkspaces->DropDownItems->Add(mi);
        }
        miRecentWorkspaces->Enabled = miRecentWorkspaces->DropDownItems->Count > 0;
    }

    void MainForm::OnWindowMenuOpening(Object^ sender, EventArgs^ e)
    {
        // remove the previous document list
        for (int i = miWindow->DropDownItems->Count - 1; i >= 0; i--)
            if (dynamic_cast<String^>(miWindow->DropDownItems[i]->Tag) == "doclist") miWindow->DropDownItems->RemoveAt(i);
        int insertAt = miWindow->DropDownItems->Count - 1;      // before "Windows..."
        int n = 0;
        for each (EditorDocument^ d in docs->Documents)
        {
            if (++n > 9) break;
            ToolStripMenuItem^ mi = gcnew ToolStripMenuItem(String::Format("&{0} {1}", n, d->Title));
            mi->Tag = "doclist";
            mi->Name = String::Format("{0}", n - 1);
            mi->Checked = d == docs->Active;
            mi->Click += gcnew EventHandler(this, &MainForm::OnWindowDocClick);
            miWindow->DropDownItems->Insert(insertAt++, mi);
        }
        if (n > 0)
        {
            ToolStripSeparator^ sep = gcnew ToolStripSeparator();
            sep->Tag = "doclist";
            miWindow->DropDownItems->Insert(insertAt, sep);
        }
    }

    void MainForm::OnToolsMenuOpening(Object^ sender, EventArgs^ e)
    {
        for (int i = miTools->DropDownItems->Count - 1; i >= 0; i--)
            if (dynamic_cast<ExternalTool^>(miTools->DropDownItems[i]->Tag) != nullptr) miTools->DropDownItems->RemoveAt(i);
        int at = miTools->DropDownItems->IndexOfKey("mi_Tools.ExternalTools") + 1;
        for each (ExternalTool^ t in settings->ExternalTools)
        {
            ToolStripMenuItem^ mi = gcnew ToolStripMenuItem(t->Title);
            mi->Tag = t;
            mi->Click += gcnew EventHandler(this, &MainForm::OnExternalToolClick);
            miTools->DropDownItems->Insert(at++, mi);
        }
    }

    void MainForm::OnBuildMenuOpening(Object^ sender, EventArgs^ e)
    {
        GrideProject^ p = ContextProject();
        String^ name = "Project";
        if (p != nullptr) name = p->Name;
        commands->Get("Build.BuildProject")->Text = "Build " + name;
        commands->Get("Build.RebuildProject")->Text = "Rebuild " + name;
        commands->Get("Build.CleanProject")->Text = "Clean " + name;
        EditorDocument^ d = docs->Active;
        String^ file = explorer->SelectedFile;
        if (file == nullptr && d != nullptr && !d->IsUntitled) file = d->FilePath;
        String^ compileText = "Compile";
        if (file != nullptr) compileText = "Compile " + Path::GetFileName(file);
        commands->Get("Build.Compile")->Text = compileText;
        // push the new texts into the menu items
        for each (ToolStripItem^ it in miBuild->DropDownItems)
        {
            Command^ c = dynamic_cast<Command^>(it->Tag);
            if (c != nullptr && it->Text != c->Text) it->Text = c->Text;
        }
        for each (System::Windows::Forms::ContextMenuStrip^ cm in gcnew array<System::Windows::Forms::ContextMenuStrip^>{
                     explorer->ProjectMenu, explorer->FileMenu })
            for each (ToolStripItem^ it in cm->Items)
            {
                Command^ c = dynamic_cast<Command^>(it->Tag);
                if (c != nullptr && it->Text != c->Text) it->Text = c->Text;
            }
    }

    void MainForm::OnRecentFileClick(Object^ sender, EventArgs^ e)
    {
        String^ f = safe_cast<String^>(safe_cast<ToolStripItem^>(sender)->Tag);
        if (File::Exists(f)) OpenDocument(f, 0);
        else
        {
            settings->Recent->Files->Remove(f);
            MessageBox::Show(this, "File not found:\n" + f, "GRIDE");
        }
    }

    void MainForm::OnRecentWorkspaceClick(Object^ sender, EventArgs^ e)
    {
        String^ f = safe_cast<String^>(safe_cast<ToolStripItem^>(sender)->Tag);
        if (File::Exists(f) || Directory::Exists(f)) OpenPath(f);
        else
        {
            settings->Recent->Workspaces->Remove(f);
            MessageBox::Show(this, "Not found:\n" + f, "GRIDE");
        }
    }

    void MainForm::OnWindowDocClick(Object^ sender, EventArgs^ e)
    {
        int i = Int32::Parse(safe_cast<ToolStripItem^>(sender)->Name);
        if (i < docs->Documents->Count) docs->Activate(docs->Documents[i]);
    }

    void MainForm::OnExternalToolClick(Object^ sender, EventArgs^ e)
    {
        RunExternalTool(safe_cast<ExternalTool^>(safe_cast<ToolStripItem^>(sender)->Tag));
    }

    // =========================================================================
    //  toolbar combos
    // =========================================================================
    void MainForm::OnConfigChanged(Object^ sender, EventArgs^ e)
    {
        if (updatingCombos) return;
        if (workspace != nullptr) { workspace->ActiveConfig = CurrentConfig(); workspace->IsDirty = true; }
        UpdateStatus();
    }

    void MainForm::OnTargetChanged(Object^ sender, EventArgs^ e)
    {
        if (updatingCombos) return;
        if (workspace != nullptr) { workspace->ActiveTarget = (Target)cbTarget->SelectedIndex; workspace->IsDirty = true; }
        UpdateStatus();
    }

    void MainForm::OnLanguageChanged(Object^ sender, EventArgs^ e)
    {
        if (updatingCombos) return;
        EditorDocument^ d = docs->Active;
        if (d == nullptr) { UpdateStatus(); return; }
        Language l = (Language)cbLanguage->SelectedIndex;
        if (d->Lang == l) return;
        d->Lang = l;
        d->Editor->SetLanguage(l, settings->LanguageOf(l));
        UpdateStatus();
    }

    // =========================================================================
    //  CanExecute / IsChecked tests
    // =========================================================================
    bool MainForm::HasWorkspace() { return workspace != nullptr; }
    bool MainForm::HasDocument() { return docs->Active != nullptr; }
    bool MainForm::HasProject() { return ContextProject() != nullptr; }
    bool MainForm::HasWritableProject() { GrideProject^ p = ContextProject(); return p != nullptr && !p->IsReadOnly; }
    bool MainForm::HasSelectedFile() { return explorer->SelectedFile != nullptr; }
    // something to build: a workspace with a project in it, or - none open - the file in the editor with main() in it
    bool MainForm::CanBuild()
    {
        if (engine->IsBuilding || debugger->IsActive || runner->IsRunning) return false;
        if (workspace != nullptr) return workspace->Projects->Count > 0;
        return ActiveFileHasMain();
    }
    bool MainForm::IsBuilding() { return engine->IsBuilding || runner->IsRunning; }
    bool MainForm::IsRunning() { return runner->IsRunning; }
    bool MainForm::IsDebugging() { return debugger->IsActive; }
    bool MainForm::IsInBreak() { return debugger->State == DebugState::Break; }
    bool MainForm::CanStartDebug() { return IsInBreak() || CanBuild(); }
    bool MainForm::IsLineNumbers() { return settings->Env->LineNumbers; }
    bool MainForm::IsWhitespace() { return settings->Env->ShowWhitespace; }
    bool MainForm::IsStatusBar() { return status->Visible; }
    bool MainForm::IsSplit() { CodeEditor^ e = ActiveEditor(); return e != nullptr && e->IsSplit; }
    bool MainForm::IsFullScreen() { return fullScreen; }
    bool MainForm::IsSelectedExcluded()
    {
        String^ f = explorer->SelectedFile;
        GrideProject^ p = explorer->SelectedProject;
        return f != nullptr && p != nullptr && p->IsExcluded(f);
    }
    String^ MainForm::CurrentTheme() { return settings->Env->Theme; }
}}
