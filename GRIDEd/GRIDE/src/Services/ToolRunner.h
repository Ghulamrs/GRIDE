// =============================================================================
//  ToolRunner.h - starts one external tool (compiler, assembler, linker,
//  converter ...) with redirected output, line by line.
//  Run() blocks; call it from a worker thread (BuildEngine does).
//
//  Tools are started directly. Only when a setup script (e.g. vcvars64.bat) is
//  configured does the tool run through  cmd.exe /s /c "call setup && tool",
//  because the environment the script creates exists only inside that cmd.
// =============================================================================
#pragma once
#include "BuildMessage.h"
#include "../Core/NativeMethods.h"

namespace Gride { namespace Services {

    using namespace System::Diagnostics;
    using namespace Gride::Native;

    // how one tool is started (filled from the toolchain of the build target)
    public ref class ToolRunOptions
    {
    public:
        ToolRunOptions() { SetupScript = String::Empty; Environment = gcnew List<String^>(); OutputCodePage = 0; }
        property String^ SetupScript;
        property List<String^>^ Environment;   // "NAME=value", %VAR% expanded against the IDE environment
        property int OutputCodePage;           // 0 = OEM code page (what console tools write)
    };

    // binds Process::WaitForExit() (no-argument overload) for a Task
    ref class ExitWaiter
    {
    public:
        ExitWaiter(Process^ proc) { p = proc; }
        void Wait() { p->WaitForExit(); }
    private:
        Process^ p;
    };

    public ref class ToolRunner
    {
    public:
        literal int ExitStartFailed = -1;
        literal int ExitCancelled = -2;

        ToolRunner() { cancelled = false; lineLock = gcnew Object(); }

        event TextLineHandler^ Line;          // raised on worker / pool threads

        property bool Cancelled { bool get() { return cancelled; } }

        // splits  "C:\x\tool.exe" -a -b  into file name + arguments
        static void SplitCommand(String^ cmd, String^% file, String^% args)
        {
            cmd = cmd->Trim();
            if (cmd->StartsWith("\""))
            {
                int end = cmd->IndexOf(L'"', 1);
                if (end < 0) { file = cmd->Trim(L'"'); args = String::Empty; return; }
                file = cmd->Substring(1, end - 1);
                args = cmd->Substring(end + 1)->Trim();
            }
            else
            {
                int sp = cmd->IndexOf(L' ');
                if (sp < 0) { file = cmd; args = String::Empty; return; }
                file = cmd->Substring(0, sp);
                args = cmd->Substring(sp + 1)->Trim();
            }
        }

        int Run(String^ commandLine, String^ workDir) { return Run(commandLine, workDir, nullptr); }

        // .NET's StandardInput is a StreamWriter in Console.InputEncoding with AutoFlush on, and the
        // AutoFlush setter writes the encoding's byte-order mark into the child's input at Start - a
        // UTF-8 console (code page 65001) hands every tool "<BOM>" before its first line (RIDE's
        // debuggers answered 'Undefined command: "﻿set"'). An input encoding without a preamble
        // prevents it; a window without a console has one without a preamble to begin with.
        static void PlainInputEncoding()
        {
            try
            {
                if (Console::InputEncoding->GetPreamble()->Length > 0) Console::InputEncoding = gcnew UTF8Encoding(false);
            }
            catch (Exception^) { }
        }

        // returns the exit code, ExitStartFailed when the tool could not be started,
        // ExitCancelled after Cancel()
        int Run(String^ commandLine, String^ workDir, ToolRunOptions^ options)
        {
            String^ file; String^ args;
            SplitCommand(commandLine, file, args);
            bool useSetup = options != nullptr && !String::IsNullOrWhiteSpace(options->SetupScript);
            // with a setup script the tool may only be found on the PATH the script sets up
            if (!useSetup && !File::Exists(file))
            {
                Line("error: tool not found: " + file);
                return ExitStartFailed;
            }
            if (cancelled) return ExitCancelled;

            String^ exe = file;
            String^ arguments = args;
            if (useSetup)
            {
                // inside cmd, & | < > ^ in paths or flags are shell characters
                String^ setup = Environment::ExpandEnvironmentVariables(options->SetupScript->Trim()->Trim(L'"'));
                exe = "cmd.exe";
                arguments = String::Format("/s /c \"call \"{0}\" >nul && \"{1}\" {2}\"", setup, file, args);
            }

            PlainInputEncoding();
            ProcessStartInfo^ si = gcnew ProcessStartInfo(exe, arguments);
            si->UseShellExecute = false;
            si->CreateNoWindow = true;
            si->RedirectStandardOutput = true;
            si->RedirectStandardError = true;
            // closed at once: a tool that prompts gets end-of-file instead of waiting on the
            // (hidden) console input of its own console for ever
            si->RedirectStandardInput = true;
            Encoding^ enc = OutputEncoding(options);
            si->StandardOutputEncoding = enc;
            si->StandardErrorEncoding = enc;
            if (!String::IsNullOrEmpty(workDir) && Directory::Exists(workDir)) si->WorkingDirectory = workDir;
            if (options != nullptr) ApplyEnvironment(si, options->Environment);

            Process^ p = gcnew Process();
            p->StartInfo = si;
            p->OutputDataReceived += gcnew DataReceivedEventHandler(this, &ToolRunner::OnData);
            p->ErrorDataReceived += gcnew DataReceivedEventHandler(this, &ToolRunner::OnData);
            IntPtr job = IntPtr::Zero;
            try
            {
                try { p->Start(); }
                catch (Exception^ ex)
                {
                    Line("error: cannot start " + exe + ": " + ex->Message);
                    return ExitStartFailed;
                }
                job = NativeMethods::CreateJob();
                if (!NativeMethods::AssignToJob(job, p->Handle)) { NativeMethods::CloseHandle(job); job = IntPtr::Zero; }
                try { p->StandardInput->Close(); } catch (Exception^) { }
                p->BeginOutputReadLine();
                p->BeginErrorReadLine();
                while (!p->WaitForExit(100))
                {
                    if (cancelled)
                    {
                        KillTree(p, job);
                        p->WaitForExit(3000);
                        Line("*** Cancelled by user.");
                        return ExitCancelled;
                    }
                }
                // flush the asynchronous output; a child that keeps the pipe open must not hang the build
                if (!WaitForOutput(p)) { KillTree(p, job); return ExitCancelled; }
                return p->ExitCode;
            }
            finally
            {
                NativeMethods::CloseHandle(job);
                p->Close();
            }
        }

        void Cancel() { cancelled = true; }

        void Reset() { cancelled = false; }

    private:
        volatile bool cancelled;
        Object^ lineLock;

        // stdout and stderr are read on two pool threads: one line at a time to the handlers, so a handler
        // collecting into a List (ProgramRunner::Capture for Verify) is never entered twice at once
        void OnData(Object^ sender, DataReceivedEventArgs^ e)
        {
            if (e->Data == nullptr) return;
            System::Threading::Monitor::Enter(lineLock);
            try { Line(e->Data); }
            finally { System::Threading::Monitor::Exit(lineLock); }
        }

        static Encoding^ OutputEncoding(ToolRunOptions^ options)
        {
            int cp = options != nullptr ? options->OutputCodePage : 0;
            try
            {
                if (cp > 0) return Encoding::GetEncoding(cp);
                return Encoding::GetEncoding(CultureInfo::CurrentCulture->TextInfo->OEMCodePage);
            }
            catch (Exception^) { return Encoding::Default; }
        }

        static void ApplyEnvironment(ProcessStartInfo^ si, List<String^>^ vars)
        {
            if (vars == nullptr) return;
            for each (String^ pair in vars)
            {
                if (pair == nullptr) continue;
                int eq = pair->IndexOf(L'=');
                if (eq <= 0) continue;
                String^ name = pair->Substring(0, eq)->Trim();
                String^ value = Environment::ExpandEnvironmentVariables(pair->Substring(eq + 1)->Trim());
                if (name->Length > 0) si->EnvironmentVariables[name] = value;
            }
        }

        // WaitForExit() without a timeout also waits for the redirected streams to close;
        // it runs on a task so a grandchild holding the pipe cannot block the build for ever
        bool WaitForOutput(Process^ p)
        {
            System::Threading::Tasks::Task^ t;
            try
            {
                t = System::Threading::Tasks::Task::Factory->StartNew(gcnew Action(gcnew ExitWaiter(p), &ExitWaiter::Wait));
            }
            catch (Exception^) { return true; }
            for (int i = 0; i < 300; i++)               // up to 30 s
            {
                if (t->Wait(100)) return true;
                if (cancelled) return false;
            }
            Line("warning: tool output still open after 30 s - continuing without it");
            return true;
        }

        // the tool and everything it started (a .bat that runs a compiler, cmd + setup script)
        static void KillTree(Process^ p, IntPtr job)
        {
            if (job != IntPtr::Zero) NativeMethods::TerminateJob(job);
            else
            {
                try
                {
                    ProcessStartInfo^ k = gcnew ProcessStartInfo("taskkill.exe", String::Format("/T /F /PID {0}", p->Id));
                    k->UseShellExecute = false;
                    k->CreateNoWindow = true;
                    Process^ kp = Process::Start(k);
                    if (kp != nullptr) { kp->WaitForExit(5000); kp->Close(); }
                }
                catch (Exception^) { }
            }
            try { if (!p->HasExited) p->Kill(); } catch (Exception^) { }
        }
    };
}}
