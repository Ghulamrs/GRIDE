#pragma once
// ============================================================================
//  GRIDE 2.0 - Toolchain layer (Phase 6)
//  TargetInfo  : what each of the 4 targets needs - the extensions, the arch
//                word the compilers take, how a program runs here - from
//                toolchain.json's target entry, with the tool ids that stand
//                for the target's assembler, linker, runner, emulator, runtime
//  ToolRunner  : starts an external tool, streams its output asynchronously,
//                supports cancellation (Stop)
// ============================================================================
#include "../Project/GrideProject.h"

using namespace System::Diagnostics;
using System::Threading::CancellationToken;

namespace Gride
{
    public ref class TargetInfo
    {
    public:
        property TargetKind Kind;
        property String^ IdeName;           // the IDE's target name
        property TargetEntry^ Entry;        // toolchain.json's target, or nullptr when the file lacks it
        property LibraryEntry^ RuntimeLibrary;   // the file's runtime library of the target (RTS6x; the Windows CRT), or nullptr
        property String^ AsmExt;            // compiler output
        property String^ ObjExt;            // assembler output
        property String^ ExeExt;            // linker output
        property String^ AssemblerId;
        property String^ LinkerId;
        property String^ RunnerId;          // nullptr = run natively on Windows
        property String^ EmulatorId;        // runs the assembly (vm6747), or nullptr
        property String^ RuntimeId;         // runtime library tool id (or nullptr)
        property String^ Arch;              // RIDE's arch word, the value of {arch}
        property String^ Run;               // the file's run: program / simulator / none
        property String^ DebugAssemblerId;  // a Debug build's CodeView path: clang, then Microsoft's link.exe /DEBUG (nullptr where the target has none)
        property String^ DebugLinkerId;
        property String^ NativeAssemblerId; // the vendor's tools the native question offers (Visual Studio's ml64 and link.exe, TI's lnk6x)
        property String^ NativeLinkerId;

        property bool RunsNatively { bool get() { return String::Equals(Run, "program"); } }
        // the target's program can be run on this machine (natively, or on the simulator)
        property bool RunsHere { bool get() { return RunsNatively || (String::Equals(Run, "simulator") && RunnerId != nullptr); } }
        property bool Host { bool get() { return Entry != nullptr && Entry->Host; } }
        property bool Emulated { bool get() { return Entry != nullptr && Entry->Emulated; } }
        // RIDE's whyNotRun case: another machine's target, which only reaches assembly here
        property bool CrossOnly { bool get() { return !Host && !Emulated; } }
        // the target's debug information is CodeView (debugsWithCodeView: a Debug build goes through clang and link.exe /DEBUG)
        property bool CodeView { bool get() { return Entry != nullptr && String::Equals(Entry->DebugInfo, "codeview", StringComparison::OrdinalIgnoreCase); } }
        // the target writes debug information at all (emitsDebugInfo: -g is dropped where it does not)
        property bool EmitsDebugInfo { bool get() { return Entry != nullptr && !String::IsNullOrEmpty(Entry->DebugInfo); } }

        static TargetInfo^ Get(TargetKind k)
        {
            TargetInfo^ t = gcnew TargetInfo();
            t->Kind = k;
            t->IdeName = Names::TargetName(k);
            String^ runner = nullptr;
            String^ emulator = nullptr;
            String^ runtime = nullptr;
            switch (k)
            {
            case TargetKind::Linux:
                t->AssemblerId = ToolIds::LinuxAssembler; t->LinkerId = ToolIds::LinuxLinker; runner = ToolIds::LinuxRunner;
                break;
            case TargetKind::macOS:
                t->AssemblerId = ToolIds::MacAssembler; t->LinkerId = ToolIds::MacLinker; runner = ToolIds::MacRunner;
                break;
            case TargetKind::TMS6747:
                t->AssemblerId = ToolIds::TmsAssembler; t->LinkerId = ToolIds::TmsLinker;
                runner = ToolIds::TmsSim; emulator = ToolIds::TmsSimulator; runtime = ToolIds::TmsRuntime;
                break;
            default:
                t->AssemblerId = ToolIds::WinAssembler; t->LinkerId = ToolIds::WinLinker;
                break;
            }
            TargetEntry^ e = ToolIds::TargetEntryOf(k);
            t->Entry = e;
            t->DebugAssemblerId = ToolIds::IdOfRole(k, "debugAssembler");
            t->DebugLinkerId = ToolIds::IdOfRole(k, "debugLinker");
            t->NativeAssemblerId = ToolIds::IdOfRole(k, "nativeAssembler");
            t->NativeLinkerId = ToolIds::IdOfRole(k, "nativeLinker");
            if (e == nullptr)
            {
                // no entry in toolchain.json: nothing is known about the target, and no program runs
                t->Arch = String::Empty; t->AsmExt = ".s"; t->ObjExt = ".o"; t->ExeExt = String::Empty; t->Run = "none";
                t->RunnerId = runner; t->EmulatorId = nullptr; t->RuntimeId = nullptr; t->RuntimeLibrary = nullptr;
                return t;
            }
            t->Arch = e->Arch;
            t->AsmExt = e->AssemblyExtension; t->ObjExt = e->ObjectExtension; t->ExeExt = e->ProgramExtension;
            t->Run = e->Run;
            t->RunnerId = String::Equals(e->Run, "program") ? nullptr : runner;
            t->EmulatorId = e->ToolIdFor("emulator")->Length > 0 ? emulator : nullptr;
            t->RuntimeLibrary = ToolchainFile::Current->Library(e->Runtime);
            t->RuntimeId = t->RuntimeLibrary != nullptr && String::Equals(t->RuntimeLibrary->Kind, "runtime") ? runtime : nullptr;
            return t;
        }

        // RIDE's whyNotRun: "" where the program runs here, else the sentence (the host is the file's host target)
        static String^ WhyNotRun(TargetKind k)
        {
            TargetInfo^ t = Get(k);
            if (t->RunsHere) return String::Empty;
            TargetEntry^ host = ToolchainFile::Current->HostTarget();
            String^ hostArch = host != nullptr ? host->Arch : Get(TargetKind::Windows)->Arch;
            return String::Format("{0} only reaches -S here - switch to {1} to run it", t->Arch->Length > 0 ? t->Arch : t->IdeName, hostArch);
        }

        // Compiler tool id for a source extension; nullptr if the file is not compiled.
        static String^ CompilerFor(String^ ext, IdeSettings^ s)
        {
            if (String::IsNullOrEmpty(ext)) return nullptr;
            String^ e = ext->ToLowerInvariant();
            if (s != nullptr && s->IsShalimarExtension(e)) return ToolIds::CompilerShalimar;
            if (String::Equals(e, ".c")) return ToolIds::CompilerC90;
            if (String::Equals(e, ".cpp") || String::Equals(e, ".cc") || String::Equals(e, ".cxx")) return ToolIds::CompilerCpp11;
            return nullptr;
        }

        // RIDE's resolve: the project's toolchain word over the file's language. auto takes the
        // file's own compiler; a word naming a compiler that does not take the file is refused
        // (refusal, RIDE's sentence) - nullptr then, with the sentence in `why`
        static String^ CompilerFor(String^ ext, IdeSettings^ s, String^ toolchainWord, String^% why)
        {
            why = String::Empty;
            String^ own = CompilerFor(ext, s);
            if (own == nullptr) return nullptr;
            String^ named = ToolWords::ToolOfWord(toolchainWord);
            if (named == nullptr) return own;
            if (String::Equals(named, own)) return own;
            // a compiler taking more than one language (cl: C and C++) takes the file when it takes its language
            ToolEntry^ ne = ToolIds::EntryOf(named);
            String^ lang = ToolWords::LanguageOfTool(own);
            if (ne != nullptr && lang->Length > 0 && ne->Languages->Count > 1 && ne->CompilesLanguage(lang)) return named;
            why = ToolWords::Refusal(named, own);
            return nullptr;
        }

        static bool IsAssemblySource(String^ ext)
        {
            if (String::IsNullOrEmpty(ext)) return false;
            String^ e = ext->ToLowerInvariant();
            return String::Equals(e, ".asm") || String::Equals(e, ".s");
        }

        static bool IsHeader(String^ ext)
        {
            if (String::IsNullOrEmpty(ext)) return false;
            String^ e = ext->ToLowerInvariant();
            return String::Equals(e, ".h") || String::Equals(e, ".hpp") || String::Equals(e, ".hh") ||
                   String::Equals(e, ".hxx") || String::Equals(e, ".inc") || String::Equals(e, ".inl");
        }

        static bool IsLinkerCommandFile(String^ ext, TargetKind k)
        {
            return k == TargetKind::TMS6747 && !String::IsNullOrEmpty(ext) &&
                   String::Equals(ext->ToLowerInvariant(), ".cmd");
        }

        static bool IsSource(String^ ext, IdeSettings^ s)
        {
            return CompilerFor(ext, s) != nullptr || IsAssemblySource(ext);
        }
    };

    // Forwards redirected stdout/stderr lines to a callback. The two streams are read on two pool threads; the
    // callback is given one line at a time (as GRIDEd's ToolRunner now does), so one that keeps a plain list is safe
    ref class LineForwarder
    {
    public:
        LineForwarder(Action<String^>^ target) : target(target) {}
        void OnData(Object^ sender, DataReceivedEventArgs^ e)
        {
            if (e->Data != nullptr) Say(e->Data);
        }
        void Say(String^ line)
        {
            if (target == nullptr) return;
            System::Threading::Monitor::Enter(this);
            try { target->Invoke(line); }
            finally { System::Threading::Monitor::Exit(this); }
        }
    private:
        Action<String^>^ target;
    };

    // Binds Process::WaitForExit() (no-argument overload) for a Task.
    ref class ExitWaiter
    {
    public:
        ExitWaiter(Process^ proc) : p(proc) {}
        void Wait() { p->WaitForExit(); }
    private:
        Process^ p;
    };

    public ref class ToolRunner abstract sealed
    {
    public:
        literal int ExitStartFailed = -1;
        literal int ExitCancelled = -2;

        // a template with {name} placeholders (GRIDE's spelling) or $(Name) ones (toolchain.json's)
        static String^ Expand(String^ tmpl, Dictionary<String^, String^>^ vars)
        {
            if (tmpl == nullptr) return String::Empty;
            String^ r = tmpl;
            for each (KeyValuePair<String^, String^> kv in vars)
            {
                String^ value = kv.Value == nullptr ? String::Empty : kv.Value;
                r = r->Replace(String::Concat("{", kv.Key, "}"), value)->Replace(String::Concat("$(", kv.Key, ")"), value);
            }
            // tidy double spaces left by empty placeholders (outside quotes only)
            StringBuilder^ sb = gcnew StringBuilder();
            bool inQuote = false;
            for (int i = 0; i < r->Length; i++)
            {
                wchar_t c = r[i];
                if (c == L'"') inQuote = !inQuote;
                if (!inQuote && c == L' ' && sb->Length > 0 && sb[sb->Length - 1] == L' ') continue;
                sb->Append(c);
            }
            return sb->ToString()->Trim();
        }

        // Runs exe with args, streaming every output line to onLine. Blocks until exit.
        // tool (may be nullptr) supplies extra environment, a setup script and output code page.
        static int Run(String^ exe, String^ args, String^ workDir, Action<String^>^ onLine,
                       CancellationToken token, ToolDefinition^ tool)
        {
            return Run(exe, args, workDir, onLine, token, tool, nullptr);
        }

        // ... with the variables the build sets before the tool's own ("NAME=value" each, the value
        // whole - Visual Studio's INCLUDE / LIB / PATH hold ';' - and %VAR% expanded against the IDE's)
        static int Run(String^ exe, String^ args, String^ workDir, Action<String^>^ onLine,
                       CancellationToken token, ToolDefinition^ tool, List<String^>^ buildEnvironment)
        {
            String^ fileName = exe;
            String^ arguments = args;
            if (tool != nullptr && !String::IsNullOrWhiteSpace(tool->SetupScript))
            {
                // cmd /s /c "call "setup.bat" >nul && "tool.exe" args"
                // Setup output is hidden (vcvars is chatty) but its errors still reach the build
                // output through stderr. NOTE: inside cmd, & | < > ^ in paths or flags are shell
                // characters - avoid them in tool arguments when a setup script is used.
                String^ setup = System::Environment::ExpandEnvironmentVariables(tool->SetupScript->Trim()->Trim(L'"'));
                fileName = "cmd.exe";
                arguments = String::Format("/s /c \"call {0} >nul && {1} {2}\"", PathUtil::Quote(setup), PathUtil::Quote(exe), args);
            }

            PlainInputEncoding();
            ProcessStartInfo^ psi = gcnew ProcessStartInfo(fileName, arguments);
            psi->UseShellExecute = false;
            psi->RedirectStandardOutput = true;
            psi->RedirectStandardError = true;
            psi->RedirectStandardInput = true;          // closed at once: a prompting tool cannot hang
            psi->CreateNoWindow = true;
            Encoding^ outEnc = OutputEncoding(tool);
            psi->StandardOutputEncoding = outEnc;
            psi->StandardErrorEncoding = outEnc;
            if (!String::IsNullOrEmpty(workDir) && Directory::Exists(workDir)) psi->WorkingDirectory = workDir;
            if (buildEnvironment != nullptr)
                for each (String^ line in buildEnvironment)
                {
                    int eq = line == nullptr ? -1 : line->IndexOf(L'=');
                    if (eq <= 0) continue;
                    psi->EnvironmentVariables[line->Substring(0, eq)->Trim()] = System::Environment::ExpandEnvironmentVariables(line->Substring(eq + 1));
                }
            if (tool != nullptr) ApplyEnvironment(psi, tool->Environment);

            Process^ p = gcnew Process();
            p->StartInfo = psi;
            LineForwarder^ fwd = gcnew LineForwarder(onLine);
            p->OutputDataReceived += gcnew DataReceivedEventHandler(fwd, &LineForwarder::OnData);
            p->ErrorDataReceived += gcnew DataReceivedEventHandler(fwd, &LineForwarder::OnData);
            try
            {
                try
                {
                    p->Start();
                }
                catch (Exception^ ex)
                {
                    if (onLine != nullptr) onLine->Invoke(String::Format("error: could not start '{0}': {1}", fileName, ex->Message));
                    return ExitStartFailed;
                }
                try { p->StandardInput->Close(); } catch (Exception^) {}
                p->BeginOutputReadLine();
                p->BeginErrorReadLine();
                while (!p->WaitForExit(100))
                {
                    if (token.IsCancellationRequested)
                    {
                        KillTree(p);
                        p->WaitForExit(3000);
                        fwd->Say("*** Cancelled by user.");
                        return ExitCancelled;
                    }
                }
                // Flush async output. A grandchild that keeps the pipe open must not hang us.
                if (!WaitForOutput(p, token)) { KillTree(p); return ExitCancelled; }
                return p->ExitCode;
            }
            finally
            {
                p->Close();
            }
        }

        static int Run(String^ exe, String^ args, String^ workDir, Action<String^>^ onLine, CancellationToken token)
        {
            return Run(exe, args, workDir, onLine, token, nullptr);
        }

        // .NET's StandardInput is a StreamWriter in Console.InputEncoding whose AutoFlush, set by Start, writes
        // the encoding's byte-order mark into the child's input at once: under a UTF-8 console every program
        // got EF BB BF before its first line. An input encoding without a preamble prevents it (a window
        // without a console has one to begin with)
        static void PlainInputEncoding()
        {
            try
            {
                if (Console::InputEncoding->GetPreamble()->Length > 0) Console::InputEncoding = gcnew UTF8Encoding(false);
            }
            catch (Exception^) { }
        }

        static Encoding^ OutputEncoding(ToolDefinition^ tool)
        {
            return EncodingFor(tool != nullptr ? tool->OutputCodePage : 0);
        }

        // a code page's encoding: 0 is the console's (OEM), which console programs write by default
        static Encoding^ EncodingFor(int cp)
        {
            try
            {
                if (cp > 0) return Encoding::GetEncoding(cp);
                return Encoding::GetEncoding(System::Globalization::CultureInfo::CurrentCulture->TextInfo->OEMCodePage);
            }
            catch (Exception^) { return Encoding::Default; }
        }

        // "NAME=value;NAME2=value" ; %VAR% is expanded against the IDE's environment
        static void ApplyEnvironment(ProcessStartInfo^ psi, String^ env)
        {
            if (String::IsNullOrWhiteSpace(env)) return;
            for each (String^ pair in env->Split(gcnew array<wchar_t>{ L';', L'\n', L'\r' }, StringSplitOptions::RemoveEmptyEntries))
            {
                int eq = pair->IndexOf(L'=');
                if (eq <= 0) continue;
                String^ name = pair->Substring(0, eq)->Trim();
                String^ value = System::Environment::ExpandEnvironmentVariables(pair->Substring(eq + 1)->Trim());
                if (name->Length > 0) psi->EnvironmentVariables[name] = value;
            }
        }

        static bool WaitForOutput(Process^ p, CancellationToken token)
        {
            // WaitForExit() without a timeout also waits until the redirected streams close.
            // Run it on a task so a grandchild holding the pipe cannot block us for ever.
            System::Threading::Tasks::Task^ t;
            try
            {
                t = System::Threading::Tasks::Task::Factory->StartNew(gcnew Action(gcnew ExitWaiter(p), &ExitWaiter::Wait));
            }
            catch (Exception^) { return true; }
            for (int i = 0; i < 300; i++)               // up to 30 s
            {
                if (t->Wait(100)) return true;
                if (token.IsCancellationRequested) return false;
            }
            // Give up waiting for stray output. The task may still be inside WaitForExit when
            // the caller closes the process; its exception is unobserved, harmless on .NET 4.5+.
            return true;
        }

        // Kills the process and everything it started (e.g. a .bat that runs a compiler).
        static void KillTree(Process^ p)
        {
            try
            {
                ProcessStartInfo^ k = gcnew ProcessStartInfo("taskkill.exe", String::Format("/T /F /PID {0}", p->Id));
                k->UseShellExecute = false;
                k->CreateNoWindow = true;
                Process^ kp = Process::Start(k);
                if (kp != nullptr) { kp->WaitForExit(5000); kp->Close(); }
            }
            catch (Exception^) {}
            try { if (!p->HasExited) p->Kill(); } catch (Exception^) {}
        }
    };
}
