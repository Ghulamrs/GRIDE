#pragma once
// ============================================================================
//  GRIDE 2.0 - a built program run inside the IDE, as RIDE's window runs one
//  ProgramRunner  : the program on a job object, both streams line by line, a
//                   line of input to it, Stop ends it and everything it started
//                   (the end is "[program returned N]" or "[stopped]"); Verify
//                   runs the emulator and the simulator captured and compares
//  ProgramLauncher: what a Run, an Emulate or a Verify runs (RIDE's runnerFor,
//                   launchCommand, simulateCommand, runnerLine,
//                   simulationMissing) - every tool and name from the settings'
//                   entries, which come from toolchain.json
// ============================================================================
#include "BuildEngine.h"

namespace Gride
{
    // Run: the program (the simulator on the .out on the C6747, the emulator when the project says
    // emulateOnly); Emulate: the emulator on the build's assembly; Verify: both, compared
    public enum class RunHow { Run, Emulate, Verify };

    // what a run is: the program or its runner with the arguments, the line shown, or why nothing runs
    public ref class RunPlan
    {
    public:
        RunPlan()
        {
            Notes = gcnew List<String^>(); Error = nullptr;
            Exe = String::Empty; Args = String::Empty; Shown = String::Empty; WorkDir = String::Empty; CodePage = 0;
            EmulateExe = String::Empty; EmulateArgs = String::Empty; SimulateExe = String::Empty; SimulateArgs = String::Empty;
            OutName = String::Empty; Missing = String::Empty; EmulatorName = String::Empty; SimulatorName = String::Empty;
        }
        property RunHow How;
        property String^ Error;                 // RIDE's sentence when nothing runs
        property List<String^>^ Notes;          // said before the run starts
        property String^ Exe;
        property String^ Args;
        property String^ Shown;                 // "$ prog args", "$ sim6747 --run prog.out", "$ vm6747 build\...\obj"
        property String^ WorkDir;
        property int CodePage;
        property ToolDefinition^ Tool;          // the runner's entry (its environment), nullptr for the program itself
        property bool HasArguments;
        // the emulator's and the simulator's commands (Verify runs both)
        property String^ EmulateExe;
        property String^ EmulateArgs;
        property ToolDefinition^ EmulatorTool;
        property String^ SimulateExe;
        property String^ SimulateArgs;
        property ToolDefinition^ SimulatorTool;
        property String^ OutName;               // prog.out
        property String^ Missing;               // why the simulator cannot run it (simulationMissing), or ""
        property String^ EmulatorName;
        property String^ SimulatorName;
    };

    // collects a captured program's lines (its two streams come on two threads)
    ref class CapturedLines
    {
    public:
        CapturedLines(List<String^>^ into) { lines = into; }
        void Add(String^ s)
        {
            System::Threading::Monitor::Enter(lines);
            try { lines->Add(s); }
            finally { System::Threading::Monitor::Exit(lines); }
        }
    private:
        List<String^>^ lines;
    };

    public ref class ProgramRunner
    {
    public:
        // output: each line of the program, both streams (worker threads); exited: once, after the last line
        ProgramRunner(Action<String^>^ output, Action<int, bool>^ exited)
        {
            this->output = output; this->exited = exited;
            proc = nullptr; job = IntPtr::Zero; running = false; stopped = false; LastStatus = 0;
            inputEncoding = Encoding::Default;
        }

        property bool IsRunning { bool get() { return running; } }
        property int LastStatus;

        // starts the program (or its runner) in workDir with the tool's environment; false, with the
        // reason said, when it could not start
        bool Start(String^ exe, String^ args, String^ workDir, ToolDefinition^ tool, int codePage)
        {
            if (running) return false;
            if (String::IsNullOrEmpty(exe) || !File::Exists(exe)) { Say(String::Concat("could not run ", exe)); return false; }
            ToolRunner::PlainInputEncoding();
            ProcessStartInfo^ si = gcnew ProcessStartInfo(exe, args == nullptr ? String::Empty : args);
            si->UseShellExecute = false;
            si->CreateNoWindow = true;
            si->RedirectStandardInput = true;
            si->RedirectStandardOutput = true;
            si->RedirectStandardError = true;
            Encoding^ enc = ToolRunner::EncodingFor(codePage);
            si->StandardOutputEncoding = enc;
            si->StandardErrorEncoding = enc;
            inputEncoding = enc;
            if (!String::IsNullOrEmpty(workDir) && Directory::Exists(workDir)) si->WorkingDirectory = workDir;
            if (tool != nullptr) ToolRunner::ApplyEnvironment(si, tool->Environment);
            Process^ p = gcnew Process();
            p->StartInfo = si;
            p->EnableRaisingEvents = true;
            p->OutputDataReceived += gcnew DataReceivedEventHandler(this, &ProgramRunner::OnData);
            p->ErrorDataReceived += gcnew DataReceivedEventHandler(this, &ProgramRunner::OnData);
            p->Exited += gcnew EventHandler(this, &ProgramRunner::OnExited);
            stopped = false;
            running = true;
            try { p->Start(); }
            catch (Exception^ ex)
            {
                running = false;
                Say(String::Format("could not run {0}: {1}", exe, ex->Message));
                return false;
            }
            // the job holds it and everything it starts; where a job cannot be had, Stop kills the program alone
            job = NativeMethods::CreateJobObject(IntPtr::Zero, nullptr);
            if (job != IntPtr::Zero && !NativeMethods::AssignProcessToJobObject(job, p->Handle)) { NativeMethods::CloseHandle(job); job = IntPtr::Zero; }
            proc = p;
            p->BeginOutputReadLine();
            p->BeginErrorReadLine();
            return true;
        }

        // a line of input for the program, with its newline - bytes of the program's code page, never a
        // byte-order mark (a StreamWriter on the pipe would put one in front of the first line)
        bool Write(String^ line)
        {
            Process^ p = proc;
            if (p == nullptr || !running) return false;
            try
            {
                array<Byte>^ bytes = inputEncoding->GetBytes(String::Concat(line, "\r\n"));
                Stream^ s = p->StandardInput->BaseStream;
                s->Write(bytes, 0, bytes->Length);
                s->Flush();
                return true;
            }
            catch (Exception^) { return false; }
        }

        // the end of the program's input (a program reading to the end goes on)
        void CloseInput()
        {
            Process^ p = proc;
            if (p == nullptr) return;
            try { p->StandardInput->Close(); } catch (Exception^) { }
        }

        // ends the program and everything it started
        void Stop()
        {
            Process^ p = proc;
            if (p == nullptr || !running) return;
            stopped = true;
            try
            {
                if (job != IntPtr::Zero) NativeMethods::TerminateJobObject(job, 1);
                else if (!p->HasExited) p->Kill();
            }
            catch (Exception^) { }
        }

        bool WaitForExit(int milliseconds)
        {
            Stopwatch^ sw = Stopwatch::StartNew();
            while (running && sw->ElapsedMilliseconds < milliseconds) System::Threading::Thread::Sleep(20);
            return !running;
        }

        // RIDE's verifyBuilt: the emulator on the assembly and the simulator on the .out, both captured,
        // compared line for line with the simulator's "CYCLES " line aside; 0 when they agree, 1 when they
        // differ, 2 when the simulation is missing
        static int Verify(RunPlan^ plan, Action<String^>^ say, CancellationToken token)
        {
            if (plan->Missing->Length > 0) { say->Invoke(String::Concat("[verify] ", plan->Missing)); return 2; }
            if (plan->HasArguments) say->Invoke("[verify] the simulator takes no command line - both run without one");
            List<String^>^ emulated = gcnew List<String^>();
            List<String^>^ simulated = gcnew List<String^>();
            int emuStatus = Capture(plan->EmulateExe, plan->EmulateArgs, plan->WorkDir, plan->EmulatorTool, emulated, token);
            if (token.IsCancellationRequested) return 2;
            Capture(plan->SimulateExe, plan->SimulateArgs, plan->WorkDir, plan->SimulatorTool, simulated, token);
            if (token.IsCancellationRequested) return 2;
            String^ cycles = String::Empty;
            List<String^>^ b = gcnew List<String^>();
            for each (String^ line in simulated)
            {
                if (line->StartsWith("CYCLES ")) { cycles = line->Substring(7); continue; }
                b->Add(line);
            }
            for each (String^ line in emulated) say->Invoke(line);
            say->Invoke(String::Format("[verify] emulator  ({0}, the assembly):    {1} lines, exit {2}", plan->EmulatorName, emulated->Count, emuStatus));
            say->Invoke(String::Format("[verify] simulator ({0}, {1}): {2} lines{3}", plan->SimulatorName, plan->OutName, b->Count, cycles->Length == 0 ? String::Empty : String::Concat(", ", cycles)));
            int k = 0;
            while (k < emulated->Count && k < b->Count && String::Equals(emulated[k], b[k])) k++;
            if (k == emulated->Count && k == b->Count)
            {
                say->Invoke("[verify] the two agree, line for line");
                return 0;
            }
            say->Invoke(String::Format("[verify] they differ at line {0}:", k + 1));
            say->Invoke(String::Concat("  emulator : ", k < emulated->Count ? emulated[k] : Str("(no more output)")));
            say->Invoke(String::Concat("  simulator: ", k < b->Count ? b[k] : Str("(no more output)")));
            return 1;
        }

        // a program run to the end with its output kept, its input closed at once (RIDE's runCaptured)
        static int Capture(String^ exe, String^ args, String^ workDir, ToolDefinition^ tool, List<String^>^ lines, CancellationToken token)
        {
            CapturedLines^ c = gcnew CapturedLines(lines);
            return ToolRunner::Run(exe, args, workDir, gcnew Action<String^>(c, &CapturedLines::Add), token, tool);
        }

    private:
        Action<String^>^ output;
        Action<int, bool>^ exited;
        Process^ proc;
        IntPtr job;
        Encoding^ inputEncoding;
        volatile bool running;
        volatile bool stopped;

        void Say(String^ s) { if (output != nullptr) output->Invoke(s); }

        void OnData(Object^ sender, DataReceivedEventArgs^ e)
        {
            if (e->Data != nullptr) Say(e->Data);
        }

        // the program ended: its last lines are flushed (a child holding the pipe open is given ten seconds), then exited
        void OnExited(Object^ sender, EventArgs^ e)
        {
            Process^ p = proc;
            if (p == nullptr) p = safe_cast<Process^>(sender);
            int code = 0;
            try { code = p->ExitCode; } catch (Exception^) { }
            try
            {
                System::Threading::Tasks::Task^ t = System::Threading::Tasks::Task::Factory->StartNew(gcnew Action(gcnew ExitWaiter(p), &ExitWaiter::Wait));
                t->Wait(10000);
            }
            catch (Exception^) { }
            LastStatus = code;
            if (job != IntPtr::Zero) { NativeMethods::CloseHandle(job); job = IntPtr::Zero; }
            proc = nullptr;
            running = false;
            if (exited != nullptr) exited->Invoke(code, stopped);
            try { p->Close(); } catch (Exception^) { }
        }
    };

    // what runs a built program (RIDE's runnerFor and the commands behind it)
    public ref class ProgramLauncher abstract sealed
    {
    public:
        // why a run is turned away before anything is built, or nullptr: another machine's target
        // (whyNotRun), Emulate or Verify off the emulated target
        static String^ Refusal(GrideProject^ p, RunHow how)
        {
            TargetInfo^ t = TargetInfo::Get(p->TargetValue);
            if (how != RunHow::Run && !t->Emulated)
            {
                TargetEntry^ emu = EmulatedEntry();
                String^ what = how == RunHow::Emulate ? String::Concat("Emulate on ", EmulatorNameOf(emu)) : Str("Verify");
                return String::Format("{0} is for {1} - choose it under Target", what, emu == nullptr ? Str("the emulated target") : emu->Arch);
            }
            if (t->CrossOnly || !t->RunsHere)
            {
                String^ why = TargetInfo::WhyNotRun(p->TargetValue);
                return why->Length > 0 ? why : String::Format("{0} programs do not run on this machine", p->Target);
            }
            return nullptr;
        }

        static RunPlan^ Plan(GrideProject^ p, IdeSettings^ s, RunHow how)
        {
            RunPlan^ plan = gcnew RunPlan();
            plan->How = how;
            plan->WorkDir = p->ProjectDir;
            List<String^>^ args = p->Arguments();
            plan->HasArguments = args->Count > 0;
            String^ refused = Refusal(p, how);
            if (refused != nullptr) { plan->Error = refused; return plan; }
            TargetInfo^ t = TargetInfo::Get(p->TargetValue);
            String^ program = BuildEngine::OutputFileOf(p);
            if (!t->Emulated)
            {
                // the program itself (RIDE's launchCommand): "<prog>" "<arg>"...
                if (!File::Exists(program)) { plan->Error = String::Format("no {0} - build it first", program); return plan; }
                plan->Exe = program;
                plan->Args = PathUtil::QuoteArguments(args);
                plan->Shown = String::Concat("$ ", p->RelativePath(program), args->Count > 0 ? String::Concat(" ", PathUtil::JoinArguments(args)) : String::Empty);
                return plan;
            }

            // the C6747 (RIDE's runnerFor): Run is the simulator on the .out unless the project says emulateOnly
            if (how == RunHow::Run && p->EmulateOnly) plan->How = how = RunHow::Emulate;
            bool debug = BuildEngine::IsDebugConfig(p->ActiveConfig()->Name);
            ToolDefinition^ sim = t->RunnerId == nullptr ? nullptr : s->GetTool(t->RunnerId);
            ToolDefinition^ vm = t->EmulatorId == nullptr ? nullptr : s->GetTool(t->EmulatorId);
            String^ simPath = sim == nullptr ? nullptr : sim->ResolvedPath();
            String^ vmPath = vm == nullptr ? nullptr : vm->ResolvedPath();
            String^ asmDir = Path::Combine(p->OutputDirectory(), "obj");      // the assembly the build wrote, which the emulator runs
            plan->EmulatorName = EmulatorNameOf(t->Entry);
            plan->SimulatorName = ToolNameOf(t->Entry, "simulator");
            plan->OutName = Path::GetFileName(program);
            plan->Missing = SimulationMissing(t, simPath, program);
            plan->EmulatorTool = vm;
            plan->SimulatorTool = sim;
            if (vm != nullptr)
            {
                // the program, the Shalimar runtime's assembly for a Shalimar program, then the arguments (the order vm6747 reads them in)
                String^ shmrt = HasShalimar(p, s) ? IdeSettingsPaths::FindShalimarRuntimeAsm(ToolchainFile::Current->LibraryByKind(t->Arch, "shalimarRuntime"), debug) : nullptr;
                Dictionary<String^, String^>^ v = Vars(p, asmDir);
                v["ShalimarRuntimeDir"] = shmrt == nullptr ? String::Empty : PathUtil::Quote(shmrt);
                v["Arguments"] = how == RunHow::Verify ? String::Empty : PathUtil::QuoteArguments(args);
                v["args"] = v["Arguments"];
                plan->EmulateExe = vmPath == nullptr ? String::Empty : vmPath;
                plan->EmulateArgs = ToolRunner::Expand(vm->Arguments, v);
            }
            if (sim != nullptr)
            {
                plan->SimulateExe = simPath == nullptr ? String::Empty : simPath;
                plan->SimulateArgs = ToolRunner::Expand(sim->Arguments, Vars(p, program));
            }
            if (how == RunHow::Verify)
            {
                if (plan->Missing->Length == 0 && vmPath == nullptr) plan->Error = EmulatorMissing(vm, plan->EmulatorName);
                return plan;
            }
            if (how == RunHow::Emulate)
            {
                if (vmPath == nullptr) { plan->Error = EmulatorMissing(vm, plan->EmulatorName); return plan; }
                plan->Exe = vmPath; plan->Args = plan->EmulateArgs; plan->Tool = vm;
                plan->Shown = String::Concat("$ ", plan->EmulatorName, " ", p->RelativePath(asmDir));
            }
            else
            {
                if (plan->Missing->Length > 0) { plan->Error = plan->Missing; return plan; }
                if (args->Count > 0) plan->Notes->Add(String::Format("[run] the simulator takes no command line - Emulate on {0} hands them over", plan->EmulatorName));
                plan->Exe = simPath; plan->Args = plan->SimulateArgs; plan->Tool = sim;
                plan->Shown = RunnerLine(sim, plan->SimulatorName, Path::GetFileName(program));
            }
            // RIDE's runners write UTF-8
            plan->CodePage = plan->Tool->OutputCodePage == 0 ? 65001 : plan->Tool->OutputCodePage;
            return plan;
        }

        // RIDE's simulationMissing: why the simulator cannot run the program, or "" - the names from the file
        static String^ SimulationMissing(TargetInfo^ t, String^ simulator, String^ out)
        {
            String^ sim = ToolNameOf(t->Entry, "simulator");
            if (String::IsNullOrEmpty(simulator) || !File::Exists(simulator))
                return String::Format("no {0} beside GRIDE - the C6747 simulator ships with it from 5.0", sim);
            if (!File::Exists(out))
            {
                LibraryEntry^ rts = t->RuntimeLibrary;
                return String::Format("no {0}: a {1} build links one only with {2} and {3} beside GRIDE and a runtime - {4} in {5} beside it, or TI's named under Tools",
                                      Path::GetFileName(out), t->Arch, ToolNameOf(t->Entry, "assembler"), ToolNameOf(t->Entry, "linker"),
                                      rts == nullptr ? Str("the runtime") : rts->Name, rts == nullptr ? String::Empty : rts->Directory->Replace(L'\\', L'/'));
            }
            return String::Empty;
        }

        // the name of the tool a target's role gives (the file's name), else the role
        static String^ ToolNameOf(TargetEntry^ e, String^ role)
        {
            ToolEntry^ tool = e == nullptr ? nullptr : ToolchainFile::Current->Tool(e->ToolIdFor(role));
            return tool == nullptr ? role : tool->Name;
        }

        // the emulated target of the file (the C6747), or nullptr
        static TargetEntry^ EmulatedEntry()
        {
            for each (TargetEntry^ t in ToolchainFile::Current->Targets) if (t->Emulated) return t;
            return nullptr;
        }

        static String^ EmulatorNameOf(TargetEntry^ e) { return ToolNameOf(e == nullptr ? EmulatedEntry() : e, "emulator"); }

    private:
        static Dictionary<String^, String^>^ Vars(GrideProject^ p, String^ program)
        {
            Dictionary<String^, String^>^ v = gcnew Dictionary<String^, String^>();
            v["Program"] = program; v["exe"] = program;
            v["asmdir"] = program;
            v["ProjectDir"] = p->ProjectDir; v["projectdir"] = p->ProjectDir;
            v["Arguments"] = String::Empty; v["args"] = String::Empty; v["ShalimarRuntimeDir"] = String::Empty;
            return v;
        }

    public:
        // a Shalimar program: the project holds a Shalimar source (the debugger's choice asks too)
        static bool HasShalimar(GrideProject^ p, IdeSettings^ s)
        {
            for each (String^ f in p->SourceFiles) if (s->IsShalimarExtension(Path::GetExtension(f))) return true;
            return false;
        }

    private:

        // "$ sim6747 --run prog.out" (RIDE's runnerLine): the operation's shown form with the program's name
        static String^ RunnerLine(ToolDefinition^ tool, String^ name, String^ programName)
        {
            ToolEntry^ e = ToolchainFile::Current->Tool(tool->Definition);
            ToolOperation^ op = e == nullptr ? nullptr : e->Operation(tool->Operation);
            String^ shown = !tool->Custom && op != nullptr && op->Shown->Length > 0 ? op->Shown : tool->Arguments;
            Dictionary<String^, String^>^ v = gcnew Dictionary<String^, String^>();
            v["Program"] = programName; v["exe"] = programName;
            return String::Concat("$ ", name, " ", ToolRunner::Expand(shown, v)->Replace("\"", String::Empty));
        }

        static String^ EmulatorMissing(ToolDefinition^ vm, String^ name)
        {
            if (vm == nullptr || !vm->IsConfigured()) return String::Format("no {0} - the emulator runs the assembly; name it under Tools > Options > Toolchains", name);
            return String::Format("no {0} - the emulator runs the assembly (looked in {1})", vm->ExecutablePath, vm->LookedIn());
        }
    };
}
