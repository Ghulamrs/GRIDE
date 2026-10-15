#pragma once
// ============================================================================
//  GRIDE 2.0 - Toolchain layer (Phase 6)
//  TargetInfo  : what each of the 4 targets needs (extensions, tool ids)
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
        property String^ AsmExt;        // compiler output
        property String^ ObjExt;        // assembler output
        property String^ ExeExt;        // linker output
        property String^ AssemblerId;
        property String^ LinkerId;
        property String^ RunnerId;      // nullptr = run natively on Windows
        property String^ RuntimeId;     // runtime library tool id (or nullptr)
        property String^ TargetSwitch;  // value of {target}

        property bool RunsNatively { bool get() { return RunnerId == nullptr; } }

        static TargetInfo^ Get(TargetKind k)
        {
            TargetInfo^ t = gcnew TargetInfo();
            t->Kind = k;
            switch (k)
            {
            case TargetKind::Linux:
                t->AsmExt = ".s"; t->ObjExt = ".o"; t->ExeExt = "";
                t->AssemblerId = ToolIds::LinuxAssembler; t->LinkerId = ToolIds::LinuxLinker;
                t->RunnerId = ToolIds::LinuxRunner; t->RuntimeId = nullptr; t->TargetSwitch = "linux";
                break;
            case TargetKind::macOS:
                t->AsmExt = ".s"; t->ObjExt = ".o"; t->ExeExt = "";
                t->AssemblerId = ToolIds::MacAssembler; t->LinkerId = ToolIds::MacLinker;
                t->RunnerId = ToolIds::MacRunner; t->RuntimeId = nullptr; t->TargetSwitch = "macos";
                break;
            case TargetKind::TMS6747:
                t->AsmExt = ".asm"; t->ObjExt = ".obj"; t->ExeExt = ".out";
                t->AssemblerId = ToolIds::TmsAssembler; t->LinkerId = ToolIds::TmsLinker;
                t->RunnerId = ToolIds::TmsSimulator; t->RuntimeId = ToolIds::TmsRuntime; t->TargetSwitch = "tms6747";
                break;
            default:
                t->AsmExt = ".asm"; t->ObjExt = ".obj"; t->ExeExt = ".exe";
                t->AssemblerId = ToolIds::WinAssembler; t->LinkerId = ToolIds::WinLinker;
                t->RunnerId = nullptr; t->RuntimeId = nullptr; t->TargetSwitch = "windows";
                break;
            }
            return t;
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

    // Forwards redirected stdout/stderr lines to a callback (runs on pool threads).
    ref class LineForwarder
    {
    public:
        LineForwarder(Action<String^>^ target) : target(target) {}
        void OnData(Object^ sender, DataReceivedEventArgs^ e)
        {
            if (e->Data != nullptr && target != nullptr) target->Invoke(e->Data);
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

        static String^ Expand(String^ tmpl, Dictionary<String^, String^>^ vars)
        {
            if (tmpl == nullptr) return String::Empty;
            String^ r = tmpl;
            for each (KeyValuePair<String^, String^> kv in vars)
                r = r->Replace(String::Concat("{", kv.Key, "}"), kv.Value == nullptr ? String::Empty : kv.Value);
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
                        if (onLine != nullptr) onLine->Invoke("*** Cancelled by user.");
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

    private:
        static Encoding^ OutputEncoding(ToolDefinition^ tool)
        {
            int cp = tool != nullptr ? tool->OutputCodePage : 0;
            try
            {
                if (cp > 0) return Encoding::GetEncoding(cp);
                // console tools write in the OEM code page by default
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
