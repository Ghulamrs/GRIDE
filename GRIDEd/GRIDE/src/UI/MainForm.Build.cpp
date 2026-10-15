// =============================================================================
//  MainForm.Build.cpp - Build, Debug and Tools menus
// =============================================================================
#include "../pch.h"
#include "MainForm.h"

namespace Gride { namespace UI {

    using namespace System::Diagnostics;

    String^ MainForm::CurrentConfig()
    {
        String^ c = safe_cast<String^>(cbConfig->SelectedItem);
        if (c == nullptr) return "Debug";
        return c;
    }

    Target MainForm::CurrentTarget()
    {
        return (Target)Math::Max(0, cbTarget->SelectedIndex);
    }

    // =========================================================================
    //  Build
    // =========================================================================
    bool MainForm::StartBuild(BuildAction action, List<GrideProject^>^ projects, String^ singleFile)
    {
        if (workspace == nullptr || engine->IsBuilding) return false;
        if (settings->Env->SaveBeforeBuild) { docs->SaveAll(); SaveWorkspace(); }

        BuildRequest^ r = gcnew BuildRequest();
        r->Projects = projects;
        r->Config = CurrentConfig();
        r->TargetKind = CurrentTarget();
        r->Action = action;
        r->SingleFile = singleFile;
        r->WorkspaceDir = workspace->Directory;

        output->Clear("Build");
        output->ShowSource("Build");
        errorList->Clear();
        bottomArea->ShowPane(output);
        SetStatus("Build started...");
        if (engine->StartAsync(r)) return true;
        SetStatus("A build is already running");
        return false;
    }

    void MainForm::BuildWorkspace()   { StartBuild(BuildAction::Build, workspace->BuildOrder(), nullptr); }
    void MainForm::RebuildWorkspace() { StartBuild(BuildAction::Rebuild, workspace->BuildOrder(), nullptr); }
    void MainForm::CleanWorkspace()   { StartBuild(BuildAction::Clean, workspace->BuildOrder(), nullptr); }

    void MainForm::BuildProject()
    {
        GrideProject^ p = ContextProject();
        if (p != nullptr) StartBuild(BuildAction::Build, workspace->BuildOrderFor(p), nullptr);
    }
    void MainForm::RebuildProject()
    {
        GrideProject^ p = ContextProject();
        List<GrideProject^>^ one = gcnew List<GrideProject^>();
        if (p == nullptr) return;
        one->Add(p);
        StartBuild(BuildAction::Rebuild, one, nullptr);
    }
    void MainForm::CleanProject()
    {
        GrideProject^ p = ContextProject();
        List<GrideProject^>^ one = gcnew List<GrideProject^>();
        if (p == nullptr) return;
        one->Add(p);
        StartBuild(BuildAction::Clean, one, nullptr);
    }

    void MainForm::CompileFile()
    {
        String^ file = explorer->SelectedFile;
        EditorDocument^ d = docs->Active;
        if (file == nullptr && d != nullptr && !d->IsUntitled) file = d->FilePath;
        if (file == nullptr) return;
        GrideProject^ p = workspace->FindProjectForFile(file);
        if (p == nullptr) { MessageBox::Show(this, "This file is not part of any project in the workspace.", "Compile"); return; }
        List<GrideProject^>^ one = gcnew List<GrideProject^>();
        one->Add(p);
        StartBuild(BuildAction::CompileFile, one, file);
    }

    void MainForm::AssembleOnly()
    {
        GrideProject^ p = ContextProject();
        if (p == nullptr) return;
        List<GrideProject^>^ one = gcnew List<GrideProject^>();
        one->Add(p);
        StartBuild(BuildAction::AssembleOnly, one, nullptr);
    }

    void MainForm::LinkOnly()
    {
        GrideProject^ p = ContextProject();
        if (p == nullptr) return;
        List<GrideProject^>^ one = gcnew List<GrideProject^>();
        one->Add(p);
        StartBuild(BuildAction::LinkOnly, one, nullptr);
    }

    // runs the checked project|config|target combinations one after another
    void MainForm::BatchBuild()
    {
        BatchBuildDialog^ dlg = gcnew BatchBuildDialog(workspace);
        if (dlg->ShowDialog(this) != WF::DialogResult::OK) return;
        if (settings->Env->SaveBeforeBuild) { docs->SaveAll(); SaveWorkspace(); }
        List<BuildRequest^>^ requests = gcnew List<BuildRequest^>();
        for each (BatchItem^ item in dlg->Selected)
        {
            BuildRequest^ r = gcnew BuildRequest();
            r->Projects->Add(item->Project);
            r->Config = item->Config;
            r->TargetKind = item->TargetKind;
            r->Action = dlg->Action;
            r->WorkspaceDir = workspace->Directory;
            requests->Add(r);
        }
        output->Clear("Build");
        output->ShowSource("Build");
        errorList->Clear();
        bottomArea->ShowPane(output);
        SetStatus("Batch build started...");
        engine->StartBatchAsync(requests);
    }

    void MainForm::ConfigurationManager()
    {
        ConfigurationManagerDialog^ dlg = gcnew ConfigurationManagerDialog(workspace);
        if (dlg->ShowDialog(this) != WF::DialogResult::OK) return;
        UpdateCombos();
        SaveWorkspace();
        RefreshProjectViews();
    }

    void MainForm::CancelBuild()
    {
        engine->Cancel();
        afterBuild = AfterBuild::Nothing;
        SetStatus("Cancelling build...");
    }

    // ---- engine events arrive on the worker thread: marshal to the UI ------------
    // (BeginInvoke throws once the window handle is gone, e.g. while closing)
    void MainForm::OnEngineOutput(String^ line)
    {
        if (IsDisposed || !IsHandleCreated) return;
        try { BeginInvoke(gcnew Action<String^>(this, &MainForm::AppendBuildOutput), line); }
        catch (InvalidOperationException^) { }
    }
    void MainForm::OnEngineMessage(BuildMessage^ m)
    {
        if (IsDisposed || !IsHandleCreated) return;
        try { BeginInvoke(gcnew Action<BuildMessage^>(this, &MainForm::AddBuildMessage), m); }
        catch (InvalidOperationException^) { }
    }
    void MainForm::OnEngineFinished(BuildSummary^ s)
    {
        if (IsDisposed || !IsHandleCreated) return;
        try { BeginInvoke(gcnew Action<BuildSummary^>(this, &MainForm::BuildFinishedUI), s); }
        catch (InvalidOperationException^) { }
    }

    void MainForm::AppendBuildOutput(String^ line) { output->Append("Build", line); }
    void MainForm::AddBuildMessage(BuildMessage^ m) { errorList->Add(m); }

    void MainForm::BuildFinishedUI(BuildSummary^ s)
    {
        if (s->Cancelled) SetStatus("Build cancelled");
        else if (s->Ok) SetStatus(String::Format("Build succeeded  ({0} warning(s))", s->Warnings));
        else SetStatus(String::Format("Build failed  ({0} error(s), {1} warning(s))", s->Errors, s->Warnings));
        if (s->Errors > 0) bottomArea->ShowPane(errorList);
        for each (EditorDocument^ d in docs->Documents) d->ChangedOnDisk();   // swallow timestamp changes made by tools

        AfterBuild next = afterBuild;
        afterBuild = AfterBuild::Nothing;
        if (!s->Ok) return;
        if (workspace == nullptr || workspace->StartupProject == nullptr) return;   // closed during the build
        if (next == AfterBuild::Debug)
        {
            GrideProject^ p = workspace->StartupProject;
            LaunchDebugger(BuildEngine::ProgramPath(p, p->GetConfiguration(CurrentConfig(), CurrentTarget())));
        }
        else if (next == AfterBuild::Run) RunProgram();
    }

    // =========================================================================
    //  Debug
    // =========================================================================
    void MainForm::DebugStart()
    {
        if (IsInBreak()) { debugger->Continue(); return; }
        if (workspace == nullptr || workspace->StartupProject == nullptr) return;
        afterBuild = AfterBuild::Debug;
        if (!StartBuild(BuildAction::Build, workspace->BuildOrderFor(workspace->StartupProject), nullptr))
            afterBuild = AfterBuild::Nothing;
    }

    void MainForm::DebugStartNoDebug()
    {
        if (workspace == nullptr || workspace->StartupProject == nullptr) return;
        afterBuild = AfterBuild::Run;
        if (!StartBuild(BuildAction::Build, workspace->BuildOrderFor(workspace->StartupProject), nullptr))
            afterBuild = AfterBuild::Nothing;
    }

    void MainForm::DebugStop()        { debugger->Stop(); }
    void MainForm::DebugBreakAll()    { debugger->BreakAll(); }
    void MainForm::DebugStepInto()    { debugger->StepInto(); }
    void MainForm::DebugStepOver()    { debugger->StepOver(); }
    void MainForm::DebugStepOut()     { debugger->StepOut(); }

    void MainForm::DebugRestart()
    {
        debugger->Stop();
        GrideProject^ p = nullptr;
        if (workspace != nullptr) p = workspace->StartupProject;
        if (p != nullptr) LaunchDebugger(BuildEngine::ProgramPath(p, p->GetConfiguration(CurrentConfig(), CurrentTarget())));
    }

    void MainForm::DebugRunToCursor()
    {
        EditorDocument^ d = docs->Active;
        if (d != nullptr && !d->IsUntitled) debugger->RunToCursor(d->FilePath, d->Editor->CurrentLine);
    }

    void MainForm::DebugToggleBreakpoint()
    {
        CodeEditor^ ed = ActiveEditor();
        if (ed != nullptr) ed->ToggleBreakpoint(0);
    }

    void MainForm::DebugDeleteAllBreakpoints()
    {
        for each (SortedSet<int>^ s in BreakpointMap()->Values) s->Clear();
        for each (EditorDocument^ d in docs->Documents) d->Editor->RefreshGutter();
        RefreshBreakpointsPane();
        if (workspace != nullptr) workspace->IsDirty = true;
    }

    void MainForm::DebugConnect()
    {
        ToolchainSettings^ tc = settings->Toolchain(Target::C6747);
        String^ vm = tc->Resolve(tc->Simulator);
        StringBuilder^ sb = gcnew StringBuilder();
        sb->AppendLine("VM6747 connection");
        sb->AppendLine("=================");
        sb->AppendLine();
        sb->AppendLine("Simulator : " + vm);
        if (File::Exists(vm)) sb->AppendLine("Status    : found");
        else sb->AppendLine("Status    : not installed yet");
        sb->AppendLine("Run cmd   : " + tc->RunCmd);
        sb->AppendLine("Debug cmd : " + tc->DebugCmd);
        sb->AppendLine();
        sb->AppendLine("Active debugger: " + debugger->Name);
        sb->AppendLine();
        sb->AppendLine("GRIDE talks to debuggers through the DebugEngine class (Services/Debugger.h).");
        sb->AppendLine("When VM6747 offers a debug interface, implement a Vm6747Debugger class derived");
        sb->AppendLine("from DebugEngine and create it in MainForm's constructor instead of PlaceholderDebugger.");
        TextDialog^ dlg = gcnew TextDialog("Connect to VM6747", sb->ToString());
        dlg->ShowDialog(this);
    }

    void MainForm::DebugLoadProgram()
    {
        OpenFileDialog^ dlg = gcnew OpenFileDialog();
        dlg->Title = "Load Program";
        dlg->Filter = "Programs (*.out;*.exe;*.elf)|*.out;*.exe;*.elf|All files (*.*)|*.*";
        if (dlg->ShowDialog(this) == WF::DialogResult::OK) LaunchDebugger(dlg->FileName);
    }

    // searches the project sources for main()
    bool MainForm::FindEntryPoint(GrideProject^ p, String^% file, int% line)
    {
        Regex^ re = gcnew Regex("^\\s*(int|void)?\\s*main\\s*\\(");
        if (p != nullptr)
            for each (String^ f in p->AllFiles())
            {
                if (!File::Exists(f) || PathUtil::HasExtension(f, ".h")) continue;
                array<String^>^ lines = File::ReadAllLines(f);
                for (int i = 0; i < lines->Length; i++)
                    if (re->IsMatch(lines[i])) { file = f; line = i + 1; return true; }
            }
        EditorDocument^ d = docs->Active;
        if (d != nullptr && !d->IsUntitled) { file = d->FilePath; line = 1; return true; }
        return false;
    }

    void MainForm::LaunchDebugger(String^ program)
    {
        String^ file = nullptr;
        int line = 1;
        GrideProject^ p = nullptr;
        if (workspace != nullptr) p = workspace->StartupProject;
        if (!FindEntryPoint(p, file, line))
        {
            MessageBox::Show(this, "No main() found and no file is open.", "Debug", MessageBoxButtons::OK, MessageBoxIcon::Warning);
            return;
        }
        output->Clear("Debug");
        output->ShowSource("Debug");
        bottomArea->ShowPane(output);
        debugger->SetBreakpoints(BreakpointMap());
        bottomArea->ShowPane(watch);
        bottomArea->ShowPane(callStack);
        bottomArea->ShowPane(locals);
        debugger->Start(program, CurrentTarget(), file, line);
    }

    // Ctrl+F5: run the startup program outside the IDE
    void MainForm::RunProgram()
    {
        GrideProject^ p = workspace->StartupProject;
        Target t = CurrentTarget();
        BuildConfiguration^ cfg = p->GetConfiguration(CurrentConfig(), t);
        String^ program = BuildEngine::ProgramPath(p, cfg);
        ToolchainSettings^ tc = settings->Toolchain(t);
        if (String::IsNullOrEmpty(tc->RunCmd))
        {
            MessageBox::Show(this, TargetInfo::Name(t) + " programs cannot run on this Windows PC.\n" +
                             "Copy " + program + " to the target, or set a Run command in\nTools > Options > Toolchains > " + tc->Name + ".",
                             "Start Without Debugging", MessageBoxButtons::OK, MessageBoxIcon::Information);
            return;
        }
        MacroExpander^ mx = gcnew MacroExpander();
        mx->Set("Program", program);
        mx->Set("Simulator", tc->Resolve(tc->Simulator));
        mx->Set("ToolDir", tc->ToolDir);
        mx->Set("ProjectDir", p->ProjectDir);
        String^ cmd = mx->Expand(tc->RunCmd);
        output->Append("Debug", "> " + cmd);
        try
        {
            ProcessStartInfo^ si = gcnew ProcessStartInfo("cmd.exe", "/c \"" + cmd + " & echo. & pause\"");
            si->WorkingDirectory = p->ProjectDir;
            si->UseShellExecute = true;
            Process::Start(si);
        }
        catch (Exception^ ex) { MessageBox::Show(this, ex->Message, "Run", MessageBoxButtons::OK, MessageBoxIcon::Error); }
    }

    // ---- debugger events (may come from another thread for real debuggers) ----------
    void MainForm::OnDebugState(DebugState s)
    {
        Object^ boxed = s;     // DebugState travels boxed through BeginInvoke
        if (InvokeRequired) BeginInvoke(gcnew Action<Object^>(this, &MainForm::DebugStateUI), boxed);
        else DebugStateUI(boxed);
    }

    void MainForm::OnDebugStopped(StopInfo^ info)
    {
        if (InvokeRequired) BeginInvoke(gcnew Action<StopInfo^>(this, &MainForm::DebugStoppedUI), info);
        else DebugStoppedUI(info);
    }

    void MainForm::OnDebugOutput(String^ line)
    {
        if (InvokeRequired) { BeginInvoke(gcnew Action<String^>(this, &MainForm::OnDebugOutput), line); return; }
        output->Append("Debug", line);
    }

    void MainForm::DebugStateUI(Object^ state)
    {
        DebugState s = safe_cast<DebugState>(state);
        if (s == DebugState::Running) { ClearExecutionMarkers(); SetStatus("Running..."); }
        if (s == DebugState::Idle || s == DebugState::Terminated)
        {
            ClearExecutionMarkers();
            for each (DebugPaneBase^ p in debugPanes) p->ClearView();
            SetStatus("Debugging ended");
        }
        UpdateTitle();
    }

    void MainForm::DebugStoppedUI(StopInfo^ info)
    {
        ClearExecutionMarkers();
        EditorDocument^ d = OpenDocument(info->File, 0);
        if (d != nullptr) d->Editor->SetExecutionLine(info->Line);
        RefreshDebugPanes();
        SetStatus(String::Format("Paused at {0}, line {1} ({2})", Path::GetFileName(info->File), info->Line, info->Reason));
        Activate();
    }

    void MainForm::RefreshDebugPanes()
    {
        for each (DebugPaneBase^ p in debugPanes) p->UpdateFrom(debugger);
        // Disassembly shows the compiler's .asm for the current source
        StopInfo^ loc = debugger->Location;
        if (loc != nullptr && workspace != nullptr)
        {
            GrideProject^ p = workspace->FindProjectForFile(loc->File);
            if (p != nullptr)
            {
                BuildConfiguration^ cfg = p->GetConfiguration(CurrentConfig(), CurrentTarget());
                String^ asmPath = Path::Combine(p->ToAbsolute(cfg->ObjDir),
                                                Path::GetFileNameWithoutExtension(loc->File) + settings->Toolchain(CurrentTarget())->AsmExt);
                disasm->ShowFile(loc->File, asmPath);
            }
        }
    }

    void MainForm::ClearExecutionMarkers()
    {
        for each (EditorDocument^ d in docs->Documents) d->Editor->SetExecutionLine(0);
    }

    // ---- breakpoints -----------------------------------------------------------------
    void MainForm::OnBreakpointToggled(EditorDocument^ doc, int line)
    {
        if (doc != nullptr && doc->IsUntitled)
        {
            SetStatus("Save the file to keep its breakpoints");
            return;
        }
        if (workspace != nullptr) workspace->IsDirty = true;
        RefreshBreakpointsPane();
    }

    void MainForm::OnBreakpointRemove(String^ file, int line)
    {
        SortedSet<int>^ s;
        if (BreakpointMap()->TryGetValue(file, s)) s->Remove(line);
        EditorDocument^ d = docs->Find(file);
        if (d != nullptr) d->Editor->RefreshGutter();
        RefreshBreakpointsPane();
    }

    void MainForm::RefreshBreakpointsPane() { bpPane->Fill(BreakpointMap()); }

    // =========================================================================
    //  Tools
    // =========================================================================
    void MainForm::ToolsCToShalimar() { ToolsConvert(ConvertDirection::CToShalimar); }
    void MainForm::ToolsShalimarToC() { ToolsConvert(ConvertDirection::ShalimarToC); }

    void MainForm::ToolsConvert(ConvertDirection dir)
    {
        String^ input = explorer->SelectedFile;
        EditorDocument^ d = docs->Active;
        if (input == nullptr && d != nullptr && !d->IsUntitled) input = d->FilePath;
        if (d != nullptr && d->IsDirty) docs->Save(d);
        ConvertDialog^ dlg = gcnew ConvertDialog(settings->Converter, input, dir);
        if (dlg->ShowDialog(this) != WF::DialogResult::OK) return;
        String^ outFile = dlg->OutputPath;
        output->Append("Tools", "Converted to " + outFile);
        GrideProject^ p = nullptr;
        if (workspace != nullptr && input != nullptr) p = workspace->FindProjectForFile(input);
        if (p != nullptr && !p->IsReadOnly && !p->ContainsFile(outFile) &&
            MessageBox::Show(this, "Add " + Path::GetFileName(outFile) + " to project " + p->Name + "?", "Convert",
                             MessageBoxButtons::YesNo, MessageBoxIcon::Question) == WF::DialogResult::Yes)
        {
            p->AddFile(outFile, p->FolderOfFile(input));
            p->Save();
            RefreshProjectViews();
        }
        if (settings->Converter->OpenResult) OpenDocument(outFile, 0);
    }

    void MainForm::ToolsToolchainCheck()
    {
        ToolchainCheckDialog^ dlg = gcnew ToolchainCheckDialog(settings);
        if (dlg->ShowDialog(this) == WF::DialogResult::Retry) ShowOptions("TC:" + TargetInfo::Name(CurrentTarget()));
    }

    void MainForm::ToolsCommandPrompt()
    {
        try
        {
            ProcessStartInfo^ si = gcnew ProcessStartInfo("cmd.exe", "/k echo GRIDE toolchain prompt - tool folders are on PATH");
            si->UseShellExecute = false;
            String^ extra = ToolchainPath();
            if (extra->Length > 0) si->EnvironmentVariables["PATH"] = extra + ";" + si->EnvironmentVariables["PATH"];
            if (workspace != nullptr) si->WorkingDirectory = workspace->Directory;
            Process::Start(si);
        }
        catch (Exception^ ex) { MessageBox::Show(this, ex->Message, "Command Prompt"); }
    }

    void MainForm::ToolsExternalTools()
    {
        ExternalToolsDialog^ dlg = gcnew ExternalToolsDialog(settings->ExternalTools);
        if (dlg->ShowDialog(this) != WF::DialogResult::OK) return;
        settings->ExternalTools = dlg->Tools;
        SettingsManager::Save(settings);
    }

    void MainForm::RunExternalTool(ExternalTool^ t)
    {
        MacroExpander^ mx = gcnew MacroExpander();
        EditorDocument^ d = docs->Active;
        if (d != nullptr && !d->IsUntitled)
        {
            mx->Set("FilePath", d->FilePath);
            mx->Set("FileDir", Path::GetDirectoryName(d->FilePath));
            mx->Set("FileName", Path::GetFileName(d->FilePath));
        }
        GrideProject^ p = ContextProject();
        if (p != nullptr) { mx->Set("ProjectDir", p->ProjectDir); mx->Set("ProjectName", p->Name); }
        if (workspace != nullptr) mx->Set("WorkspaceDir", workspace->Directory);
        try
        {
            ProcessStartInfo^ si = gcnew ProcessStartInfo(mx->Expand(t->Command), mx->Expand(t->Arguments));
            String^ dir = mx->Expand(t->InitialDir);
            if (Directory::Exists(dir)) si->WorkingDirectory = dir;
            si->UseShellExecute = true;
            Process::Start(si);
            output->Append("Tools", "Started " + t->Title);
        }
        catch (Exception^ ex) { MessageBox::Show(this, ex->Message, t->Title, MessageBoxButtons::OK, MessageBoxIcon::Error); }
    }

    void MainForm::ToolsSnippets()
    {
        SnippetsDialog^ dlg = gcnew SnippetsDialog(settings->Snippets, docs->Active != nullptr);
        if (dlg->ShowDialog(this) != WF::DialogResult::OK) return;
        settings->Snippets = dlg->Snippets;
        SettingsManager::Save(settings);
        CodeEditor^ ed = ActiveEditor();
        if (dlg->InsertText != nullptr && ed != nullptr) ed->InsertText(dlg->InsertText);
    }

    void MainForm::ToolsKeyboard() { ShowOptions("Keyboard"); }
    void MainForm::ToolsOptions() { ShowOptions(nullptr); }

    void MainForm::ShowOptions(String^ page)
    {
        OptionsDialog^ dlg = gcnew OptionsDialog(settings, commands, page);
        if (dlg->ShowDialog(this) != WF::DialogResult::OK) return;
        settings = dlg->Result;
        SettingsManager::Save(settings);
        ApplySettings();
        SetStatus("Options saved to " + settings->FilePath);
    }
}}
