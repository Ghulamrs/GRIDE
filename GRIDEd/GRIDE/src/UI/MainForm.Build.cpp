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
        Workspace^ space = BuildSpace();
        if (space == nullptr || engine->IsBuilding) return false;
        if (settings->Env->SaveBeforeBuild) { docs->SaveAll(); SaveWorkspace(); }

        BuildRequest^ r = gcnew BuildRequest();
        r->Projects = projects;
        r->Config = CurrentConfig();
        r->TargetKind = CurrentTarget();
        r->Action = action;
        r->SingleFile = singleFile;
        r->WorkspaceDir = space->Directory;

        output->Clear("Build");
        output->ShowSource("Build");
        errorList->Clear();
        bottomArea->ShowPane(output);
        SetStatus("Build started...");
        if (engine->StartAsync(r)) return true;
        SetStatus("A build is already running");
        return false;
    }

    void MainForm::BuildWorkspace()   { Workspace^ w = BuildSpace(); if (w != nullptr) StartBuild(BuildAction::Build, w->BuildOrder(), nullptr); }
    void MainForm::RebuildWorkspace() { Workspace^ w = BuildSpace(); if (w != nullptr) StartBuild(BuildAction::Rebuild, w->BuildOrder(), nullptr); }
    void MainForm::CleanWorkspace()   { Workspace^ w = BuildSpace(); if (w != nullptr) StartBuild(BuildAction::Clean, w->BuildOrder(), nullptr); }

    void MainForm::BuildProject()
    {
        GrideProject^ p = BuildContextProject();
        if (p != nullptr) StartBuild(BuildAction::Build, BuildSpace()->BuildOrderFor(p), nullptr);
    }
    void MainForm::RebuildProject()
    {
        GrideProject^ p = BuildContextProject();
        List<GrideProject^>^ one = gcnew List<GrideProject^>();
        if (p == nullptr) return;
        one->Add(p);
        StartBuild(BuildAction::Rebuild, one, nullptr);
    }
    void MainForm::CleanProject()
    {
        GrideProject^ p = BuildContextProject();
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
        Workspace^ w = BuildSpace();
        GrideProject^ p = w == nullptr ? nullptr : w->FindProjectForFile(file);
        if (p == nullptr) { MessageBox::Show(this, "This file is not part of any project in the workspace.", "Compile"); return; }
        List<GrideProject^>^ one = gcnew List<GrideProject^>();
        one->Add(p);
        StartBuild(BuildAction::CompileFile, one, file);
    }

    void MainForm::AssembleOnly()
    {
        GrideProject^ p = BuildContextProject();
        if (p == nullptr) return;
        List<GrideProject^>^ one = gcnew List<GrideProject^>();
        one->Add(p);
        StartBuild(BuildAction::AssembleOnly, one, nullptr);
    }

    void MainForm::LinkOnly()
    {
        GrideProject^ p = BuildContextProject();
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
        // RIDE's Build > Stop ends a running program the same way, and everything it started
        if (runner->IsRunning) { StopProgram(); return; }
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

    // the native-tools question, asked from the build thread and answered on the window's
    bool MainForm::OnEngineQuestion(String^ question)
    {
        if (IsDisposed || !IsHandleCreated) return false;
        try { return safe_cast<bool>(Invoke(gcnew Func<String^, bool>(this, &MainForm::AskYesNo), question)); }
        catch (Exception^) { return false; }
    }
    bool MainForm::AskYesNo(String^ question)
    {
        return MessageBox::Show(this, question, "GRIDE", MessageBoxButtons::YesNo, MessageBoxIcon::Question) == WF::DialogResult::Yes;
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
        // RIDE puts the caret on the first error, opening the file when nothing has - never the assembly a
        // compiler generated (a .s or .asm no project lists): the Error List still names it
        if (!s->Ok && !s->Cancelled && settings->Env->JumpToFirstError)
        {
            BuildMessage^ first = s->FirstError != nullptr ? s->FirstError : errorList->FirstError();
            Workspace^ owner = workspace != nullptr ? workspace : lone;
            String^ ext = first == nullptr ? String::Empty : Path::GetExtension(first->File)->ToLowerInvariant();
            bool generated = (ext == ".s" || ext == ".asm") && (owner == nullptr || owner->FindProjectForFile(first->File) == nullptr);
            if (first != nullptr && File::Exists(first->File) && !generated) OnNavigateTo(first->File, first->Line, first->Column);
        }

        AfterBuild next = afterBuild;
        afterBuild = AfterBuild::Nothing;
        if (!s->Ok) return;
        Workspace^ w = workspace != nullptr ? workspace : lone;
        if (w == nullptr || w->StartupProject == nullptr) return;   // closed during the build
        if (next == AfterBuild::Debug || next == AfterBuild::DebugAtEntry)
        {
            GrideProject^ p = w->StartupProject;
            LaunchDebugger(BuildEngine::ProgramPath(p, p->GetConfiguration(CurrentConfig(), CurrentTarget())), next == AfterBuild::DebugAtEntry);
        }
        else if (next == AfterBuild::Run || next == AfterBuild::Emulate || next == AfterBuild::Verify) RunChosen(next);
    }

    // =========================================================================
    //  Debug
    // =========================================================================
    void MainForm::DebugStart()
    {
        if (IsInBreak()) { debugger->Continue(); return; }
        Workspace^ w = BuildSpace();
        if (w == nullptr || w->StartupProject == nullptr) return;
        afterBuild = AfterBuild::Debug;
        if (!StartBuild(BuildAction::Build, w->BuildOrderFor(w->StartupProject), nullptr))
            afterBuild = AfterBuild::Nothing;
    }

    void MainForm::DebugStartNoDebug()
    {
        Workspace^ w = BuildSpace();
        if (w == nullptr || w->StartupProject == nullptr) return;
        // a Run of a cross target is turned away before anything is built (RIDE's buildAndRun)
        if (!RideToolchain::RunsHere(RideTool::C90, CurrentTarget()))
        {
            output->Clear("Build"); output->ShowSource("Build"); bottomArea->ShowPane(output);
            output->Append("Build", RideToolchain::WhyNotRun(RideTool::C90, CurrentTarget()));
            SetStatus(RideToolchain::WhyNotRun(RideTool::C90, CurrentTarget()));
            return;
        }
        afterBuild = AfterBuild::Run;
        if (!StartBuild(BuildAction::Build, w->BuildOrderFor(w->StartupProject), nullptr))
            afterBuild = AfterBuild::Nothing;
    }

    // RIDE's Build > Emulate on vm6747 and Build > Verify: the same build a Run makes, with what runs the program changed
    void MainForm::DebugEmulate()
    {
        Workspace^ w = BuildSpace();
        if (w == nullptr || w->StartupProject == nullptr) return;
        if (!RideToolchain::IsEmulated(CurrentTarget())) { SetStatus("Emulate on vm6747 is for tms6747 - choose it under Target"); return; }
        afterBuild = AfterBuild::Emulate;
        if (!StartBuild(BuildAction::Build, w->BuildOrderFor(w->StartupProject), nullptr))
            afterBuild = AfterBuild::Nothing;
    }

    void MainForm::DebugVerify()
    {
        Workspace^ w = BuildSpace();
        if (w == nullptr || w->StartupProject == nullptr) return;
        if (!RideToolchain::IsEmulated(CurrentTarget())) { SetStatus("Verify is for tms6747 - choose it under Target"); return; }
        afterBuild = AfterBuild::Verify;
        if (!StartBuild(BuildAction::Build, w->BuildOrderFor(w->StartupProject), nullptr))
            afterBuild = AfterBuild::Nothing;
    }

    void MainForm::StopProgram()
    {
        if (!runner->IsRunning) return;
        SetStatus("Stopping the program...");
        runner->Stop();
    }

    // the running program's lines, and its end, land in the Output pane on the window's thread
    void MainForm::OnRunOutput(String^ line)
    {
        if (IsDisposed || !IsHandleCreated) return;
        try { BeginInvoke(gcnew Action<String^>(this, &MainForm::AppendBuildOutput), line); }
        catch (InvalidOperationException^) { }
    }

    void MainForm::OnRunExited(int status, bool stopped)
    {
        if (IsDisposed || !IsHandleCreated) return;
        try { BeginInvoke(gcnew Action<Object^, Object^>(this, &MainForm::RunExitedUI), status, stopped); }
        catch (InvalidOperationException^) { }
    }

    void MainForm::RunExitedUI(Object^ status, Object^ stopped)
    {
        int code = safe_cast<int>(status);
        bool wasStopped = safe_cast<bool>(stopped);
        output->InputEnabled = false;
        output->Append("Build", String::Empty);
        output->Append("Build", wasStopped ? "[stopped]" : String::Format("[program returned {0}]", code));
        SetStatus(wasStopped ? "The program was stopped" : String::Format("The program returned {0}", code));
    }

    // a line typed below the output: for the program being debugged when its debugger hands input over,
    // else for the program running in the pane
    void MainForm::OnOutputInput(String^ line)
    {
        if (debugger->IsActive && debugger->TakesInput)
        {
            programLineLast = false;
            if (!debugger->SendInput(line)) SetStatus("The program being debugged could not be given that line");
            return;
        }
        if (!runner->Write(line)) SetStatus("No program is running to read that");
    }

    void MainForm::DebugStop()        { debugger->Stop(); }
    void MainForm::DebugBreakAll()    { debugger->BreakAll(); }
    void MainForm::DebugStepInto()    { if (IsInBreak()) debugger->StepInto(); else DebugStartAtEntry(); }
    void MainForm::DebugStepOver()    { if (IsInBreak()) debugger->StepOver(); else DebugStartAtEntry(); }
    void MainForm::DebugStepOut()     { debugger->StepOut(); }

    // F11 or F10 with nothing debugged: the build, then the program started and stopped at main's first line
    void MainForm::DebugStartAtEntry()
    {
        if (IsDebugging()) return;      // running: a step waits for the next stop
        Workspace^ w = BuildSpace();
        if (w == nullptr || w->StartupProject == nullptr) return;
        afterBuild = AfterBuild::DebugAtEntry;
        if (!StartBuild(BuildAction::Build, w->BuildOrderFor(w->StartupProject), nullptr))
            afterBuild = AfterBuild::Nothing;
    }

    void MainForm::DebugRestart()
    {
        debugger->Stop();
        GrideProject^ p = nullptr;
        if (workspace != nullptr) p = workspace->StartupProject;
        else if (lone != nullptr) p = lone->StartupProject;
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

    // RIDE's Ctrl-Up / Ctrl-Down: the frame looked at - its variables in Locals, the editor on its line,
    // "the variables are main's, at f.c:11" in the status bar; the top frame is the stop again
    void MainForm::DebugUpStack() { LookAtFrame(debugger->CurrentFrame + 1); }
    void MainForm::DebugDownStack() { LookAtFrame(debugger->CurrentFrame - 1); }
    void MainForm::OnFrameChosen(int index) { LookAtFrame(index); }

    void MainForm::LookAtFrame(int index)
    {
        if (!IsInBreak()) return;
        if (index < 0) { SetStatus("at the stop"); return; }
        List<DebugFrame^>^ frames = debugger->CallStack();
        if (index >= frames->Count) { SetStatus(frames->Count <= 1 ? "nothing called this" : "the stack ends here"); return; }
        if (index == debugger->CurrentFrame && index == 0) { SetStatus("at the stop"); return; }
        if (!debugger->SelectFrame(index))
        {
            String^ why = debugger->WhyNoStackWalk();
            SetStatus(why->Length > 0 ? why : debugger->Name + " cannot look at another frame");
            return;
        }
        DebugFrame^ f = frames[index];
        if (!String::IsNullOrEmpty(f->File) && f->Line > 0)
        {
            EditorDocument^ d = OpenDocument(f->File, 0);
            if (d != nullptr) d->Editor->GoToLine(f->Line);
        }
        RefreshDebugPanes();
        SetStatus(index == 0 ? "back at the stop" : DebugTranscript::LookingAt(f));
    }

    // RIDE's Watch expression... and the setting of a variable: the panes' own input lines
    void MainForm::DebugAddWatch()
    {
        bottomArea->ShowPane(watch);
        watch->FocusAdd();
    }

    void MainForm::DebugSetVariable()
    {
        bottomArea->ShowPane(locals);
        locals->FocusSet();
    }

    void MainForm::OnVariableSet(String^ name, String^ value)
    {
        if (!IsInBreak()) return;
        String^ said;
        if (!debugger->SetVariable(name, value, said))
        {
            output->Append("Debug", "cannot set " + name + ": " + said);
            programLineLast = false;
            SetStatus("cannot set " + name + " - " + said);
        }
        else SetStatus(name + " = " + value);
        RefreshDebugPanes();
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
        String^ vm = tc->Resolve(tc->Emulator);
        String^ sim = tc->Resolve(tc->Simulator);
        StringBuilder^ sb = gcnew StringBuilder();
        sb->AppendLine("VM6747 connection");
        sb->AppendLine("=================");
        sb->AppendLine();
        sb->AppendLine("Emulator  : " + vm + (File::Exists(vm) ? "  (found)" : "  (not installed yet)"));
        sb->AppendLine("Simulator : " + sim + (File::Exists(sim) ? "  (found)" : "  (not installed yet)"));
        sb->AppendLine("Run cmd   : " + tc->RunCmd);
        sb->AppendLine("Emulate   : " + tc->EmulateCmd);
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

    // searches the project sources for main() - C's and C++'s, and Shalimar's "fun <> = main("
    bool MainForm::FindEntryPoint(GrideProject^ p, String^% file, int% line)
    {
        String^ f; int l;
        if (DebuggerChoice::EntryOf(p, f, l)) { file = f; line = l; return true; }
        EditorDocument^ d = docs->Active;
        if (d != nullptr && !d->IsUntitled) { file = d->FilePath; line = 1; return true; }
        return false;
    }

    // the engine the window talks to: the events of the one before are let go
    void MainForm::AttachDebugger(DebugEngine^ engine)
    {
        if (debugger != nullptr)
        {
            debugger->StateChanged -= gcnew DebugStateHandler(this, &MainForm::OnDebugState);
            debugger->Stopped -= gcnew DebugStopHandler(this, &MainForm::OnDebugStopped);
            debugger->Output -= gcnew TextLineHandler(this, &MainForm::OnDebugOutput);
            debugger->ProgramOutput -= gcnew TextLineHandler(this, &MainForm::OnDebugProgramOutput);
        }
        debugger = engine;
        debugger->StateChanged += gcnew DebugStateHandler(this, &MainForm::OnDebugState);
        debugger->Stopped += gcnew DebugStopHandler(this, &MainForm::OnDebugStopped);
        debugger->Output += gcnew TextLineHandler(this, &MainForm::OnDebugOutput);
        debugger->ProgramOutput += gcnew TextLineHandler(this, &MainForm::OnDebugProgramOutput);
    }

    // what the emulator debugs: the one assembly file of a one-file program, else the object folder of them
    String^ MainForm::AssemblyProgramFor(GrideProject^ p, BuildConfiguration^ cfg, ToolchainSettings^ tc)
    {
        String^ objDir = p->ToAbsolute(cfg->ObjDir);
        try
        {
            if (Directory::Exists(objDir))
            {
                array<String^>^ asms = Directory::GetFiles(objDir, "*" + tc->AsmExt);
                if (asms->Length == 1) return asms[0];
            }
        }
        catch (Exception^) { }
        return objDir;
    }

    void MainForm::LaunchDebugger(String^ program) { LaunchDebugger(program, false); }

    // RIDE's F8: the engine for the build (dbg_planFor), the placeholder with the reason when there is none;
    // atEntry: F10 / F11 started it, so it stops at main's first line
    void MainForm::LaunchDebugger(String^ program, bool atEntry)
    {
        String^ file = nullptr;
        int line = 1;
        GrideProject^ p = nullptr;
        if (workspace != nullptr) p = workspace->StartupProject;
        else if (lone != nullptr) p = lone->StartupProject;
        if (!FindEntryPoint(p, file, line))
        {
            MessageBox::Show(this, "No main() found and no file is open.", "Debug", MessageBoxButtons::OK, MessageBoxIcon::Warning);
            return;
        }
        output->Clear("Debug");
        output->ShowSource("Debug");
        bottomArea->ShowPane(output);
        programLineLast = false;
        DebugEngine^ engine = gcnew PlaceholderDebugger();
        if (p != nullptr)
        {
            Target t = CurrentTarget();
            BuildConfiguration^ cfg = p->GetConfiguration(CurrentConfig(), t);
            ToolchainSettings^ tc = settings->Toolchain(t);
            bool shalimar = false;
            for each (String^ f in p->AllFiles()) if (settings->DetectLanguage(f, p->Lang) == Language::Shalimar) shalimar = true;
            String^ shmrt = shalimar ? RideToolchain::FindShalimarRuntimeDir(tc->ToolDir, ToolchainSettings::RideRoot, cfg->IsDebug) : nullptr;
            DebugLaunch^ launch = DebuggerChoice::For(p, cfg, tc, settings, program, AssemblyProgramFor(p, cfg, tc), shalimar, shmrt);
            if (!launch->Possible) output->Append("Debug", launch->WhyNot + " - the placeholder stands in");
            else if (launch->Kind == DebuggerKind::Shalimar) engine = gcnew ShalimarSession(launch);
            else engine = gcnew ProcessDebugger(launch);
        }
        AttachDebugger(engine);
        if (p != nullptr) engine->SetSourceFiles(p->AllFiles());
        debugger->SetBreakpoints(BreakpointMap());
        bottomArea->ShowPane(watch);
        bottomArea->ShowPane(callStack);
        bottomArea->ShowPane(locals);
        // the program's input line below the output, when its debugger hands input over; what is typed comes
        // back as the console echoes it, among the program's lines
        if (debugger->TakesInput && !runner->IsRunning) { output->EchoInput = false; output->ShowInput(true, false); }
        debugger->StopAtEntry = atEntry;
        debugger->Start(program, CurrentTarget(), file, line);
    }

    // the program's own lines, as heard through the debugger (RIDE's console): the rest of a line it had not
    // finished goes on that line while nothing else was written after it
    void MainForm::OnDebugProgramOutput(String^ line)
    {
        if (InvokeRequired) { BeginInvoke(gcnew Action<String^>(this, &MainForm::OnDebugProgramOutput), line); return; }
        String^ rest;
        if (DebugEngine::Continues(line, rest) && programLineLast) output->Continue("Debug", rest);
        else output->Append("Debug", rest);
        programLineLast = true;
    }

    // Ctrl+F5: run the startup program
    void MainForm::RunProgram() { RunChosen(AfterBuild::Run); }

    // the run's worker for Verify: both programs captured on a thread, the lines posted to the Output pane
    ref class VerifyJob
    {
    public:
        VerifyJob(MainForm^ form, String^ emulate, String^ simulate, String^ workDir, String^ outName, String^ missing, bool hasArgs)
        {
            this->form = form; this->emulate = emulate; this->simulate = simulate; this->workDir = workDir;
            this->outName = outName; this->missing = missing; this->hasArgs = hasArgs;
        }
        void Run()
        {
            int status = ProgramRunner::Verify(emulate, simulate, workDir, 65001, outName, missing, hasArgs, gcnew Action<String^>(this, &VerifyJob::Say));
            form->OnRunExited(status, false);
        }
    private:
        MainForm^ form;
        String^ emulate; String^ simulate; String^ workDir; String^ outName; String^ missing; bool hasArgs;
        void Say(String^ line) { form->OnRunOutput(line); }
    };

    // the program of the startup project, run as RIDE runs one: inside the Output pane with an input
    // line, "[program returned N]" at the end; on tms6747 the simulator on the .out (the emulator on
    // the assembly when the project says emulateOnly), Emulate always the emulator, Verify both
    void MainForm::RunChosen(AfterBuild how)
    {
        Workspace^ w = workspace != nullptr ? workspace : lone;
        if (w == nullptr || w->StartupProject == nullptr || runner->IsRunning) return;
        GrideProject^ p = w->StartupProject;
        Target t = CurrentTarget();
        BuildConfiguration^ cfg = p->GetConfiguration(CurrentConfig(), t);
        String^ program = BuildEngine::ProgramPath(p, cfg);
        ToolchainSettings^ tc = settings->Toolchain(t);
        bool emulated = RideToolchain::IsEmulated(t);
        if (!settings->Env->RunInIde && how == AfterBuild::Run && !emulated) { RunExternal(p, tc, program); return; }

        output->ShowSource("Build");
        bottomArea->ShowPane(output);
        List<String^>^ args = p->ProgramArgs;
        bool shalimar = false;
        for each (String^ f in p->AllFiles()) if (settings->DetectLanguage(f, p->Lang) == Language::Shalimar) shalimar = true;
        String^ cmd; String^ shown;
        int codePage = tc->OutputCodePage;
        if (emulated)
        {
            if (how == AfterBuild::Run && p->EmulateOnly) how = AfterBuild::Emulate;
            String^ sim = tc->ResolveFor("simulator", tc->Simulator);
            String^ vm = tc->ResolveFor("emulator", tc->Emulator);
            String^ out = RideToolchain::TiProgramOf(program);
            String^ asmDir = p->ToAbsolute(cfg->ObjDir);     // the assembly the build wrote, which vm6747 runs
            String^ shmrt = shalimar ? RideToolchain::FindShalimarRuntimeDir(tc->ToolDir, ToolchainSettings::RideRoot, cfg->IsDebug) : nullptr;
            String^ missing = RideToolchain::SimulationMissing(sim, out);
            if (codePage == 0) codePage = 65001;             // RIDE's runners write UTF-8
            if (how == AfterBuild::Verify)
            {
                VerifyJob^ job = gcnew VerifyJob(this, RideToolchain::EmulateCommand(vm, asmDir, shalimar, nullptr, shmrt),
                                                 RideToolchain::SimulateCommand(sim, out), p->ProjectDir, Path::GetFileName(out), missing, args->Count > 0);
                SetStatus("Verifying " + Path::GetFileName(out) + " on vm6747 and sim6747...");
                System::Threading::Thread^ th = gcnew System::Threading::Thread(gcnew System::Threading::ThreadStart(job, &VerifyJob::Run));
                th->IsBackground = true;
                th->Start();
                return;
            }
            if (how == AfterBuild::Emulate)
            {
                if (!File::Exists(vm))
                {
                    output->Append("Build", "no " + tc->Emulator + " - the emulator runs the assembly (looked in " + tc->LookedIn(tc->Emulator) + ")");
                    SetStatus("no emulator"); return;
                }
                cmd = RideToolchain::EmulateCommand(vm, asmDir, shalimar, args, shmrt);
                shown = "$ " + Path::GetFileNameWithoutExtension(vm) + " " + PathUtil::MakeRelative(p->ProjectDir, asmDir);
            }
            else
            {
                if (missing->Length > 0) { output->Append("Build", missing); SetStatus(missing); return; }
                if (args->Count > 0) output->Append("Build", "[run] the simulator takes no command line - Emulate on vm6747 hands them over");
                cmd = RideToolchain::SimulateCommand(sim, out);
                shown = RideToolchain::RunnerLine(true, program);
            }
        }
        else
        {
            if (String::IsNullOrEmpty(tc->RunCmd))
            {
                String^ why = RideToolchain::WhyNotRun(RideTool::C90, t);
                if (why->Length == 0) why = TargetInfo::Name(t) + " programs cannot run on this Windows PC - copy " + program + " to the target";
                output->Append("Build", why); SetStatus(why); return;
            }
            if (!File::Exists(program)) { output->Append("Build", "no " + program + " - build it first"); SetStatus("nothing to run"); return; }
            cmd = RideToolchain::LaunchCommand(program, false, args, String::Empty, nullptr);
            shown = "$ " + PathUtil::MakeRelative(p->ProjectDir, program) + (args->Count > 0 ? " " + String::Join(" ", args) : String::Empty);
        }
        output->Append("Build", shown);
        List<String^>^ env = tc->EnvironmentVars == nullptr ? gcnew List<String^>() : gcnew List<String^>(tc->EnvironmentVars);
        if (!runner->Start(cmd, p->ProjectDir, env, codePage)) { SetStatus("The program could not be started"); return; }
        output->EchoInput = true;
        output->InputEnabled = true;
        SetStatus("Running " + Path::GetFileName(program) + " - type its input below the output, Stop Program ends it");
    }

    // the way before the port, kept as an option: the program in a console window of its own
    void MainForm::RunExternal(GrideProject^ p, ToolchainSettings^ tc, String^ program)
    {
        if (String::IsNullOrEmpty(tc->RunCmd))
        {
            MessageBox::Show(this, TargetInfo::Name(tc->TargetKind) + " programs cannot run on this Windows PC.\n" +
                             "Copy " + program + " to the target, or set a Run command in\nTools > Options > Toolchains > " + tc->Name + ".",
                             "Start Without Debugging", MessageBoxButtons::OK, MessageBoxIcon::Information);
            return;
        }
        MacroExpander^ mx = gcnew MacroExpander();
        mx->Set("Program", program);
        mx->Set("Simulator", tc->ResolveFor("simulator", tc->Simulator));
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
        programLineLast = false;
    }

    void MainForm::DebugStateUI(Object^ state)
    {
        DebugState s = safe_cast<DebugState>(state);
        if (s == DebugState::Running) { ClearExecutionMarkers(); SetStatus("Running..."); }
        if (s == DebugState::Idle || s == DebugState::Terminated)
        {
            if (!output->EchoInput && !runner->IsRunning) { output->ShowInput(false, false); output->EchoInput = true; }
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
        // a new stop: what the panes saw at the last one is what a value is compared with (RIDE's NewStop)
        watch->NewStop();
        locals->NewStop();
        RefreshDebugPanes();
        SetStatus(Path::GetFileName(info->File) + ":" + info->Line + (info->Function->Length > 0 ? " in " + info->Function : String::Empty));
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
                disasm->ShowFile(loc->File, engine->AsmFileFor(p, cfg, loc->File));
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
        if (debugger->IsActive) debugger->BreakpointsChanged();     // toggling while stopped resends them (RIDE)
    }

    void MainForm::OnBreakpointRemove(String^ file, int line)
    {
        SortedSet<int>^ s;
        if (BreakpointMap()->TryGetValue(file, s)) s->Remove(line);
        EditorDocument^ d = docs->Find(file);
        if (d != nullptr) d->Editor->RefreshGutter();
        RefreshBreakpointsPane();
        if (debugger->IsActive) debugger->BreakpointsChanged();
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
