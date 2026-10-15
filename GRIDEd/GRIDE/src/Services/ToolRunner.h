// =============================================================================
//  ToolRunner.h - starts one external tool (compiler, assembler, linker,
//  converter ...) with redirected output, line by line.
//  Run() blocks; call it from a worker thread (BuildEngine does).
// =============================================================================
#pragma once
#include "BuildMessage.h"

namespace Gride { namespace Services {

    using namespace System::Diagnostics;

    public ref class ToolRunner
    {
    public:
        ToolRunner() { current = nullptr; cancelled = false; }

        event TextLineHandler^ Line;          // raised on the worker thread

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

        // returns the exit code, or -1 when the tool could not be started
        int Run(String^ commandLine, String^ workDir)
        {
            String^ file; String^ args;
            SplitCommand(commandLine, file, args);
            if (!File::Exists(file))
            {
                Line("error: tool not found: " + file);
                return -1;
            }

            ProcessStartInfo^ si = gcnew ProcessStartInfo(file, args);
            si->UseShellExecute = false;
            si->CreateNoWindow = true;
            si->RedirectStandardOutput = true;
            si->RedirectStandardError = true;
            if (!String::IsNullOrEmpty(workDir) && Directory::Exists(workDir)) si->WorkingDirectory = workDir;

            Process^ p = gcnew Process();
            p->StartInfo = si;
            p->OutputDataReceived += gcnew DataReceivedEventHandler(this, &ToolRunner::OnData);
            p->ErrorDataReceived += gcnew DataReceivedEventHandler(this, &ToolRunner::OnData);
            try
            {
                p->Start();
            }
            catch (Exception^ ex)
            {
                Line("error: cannot start " + file + ": " + ex->Message);
                return -1;
            }
            current = p;
            p->BeginOutputReadLine();
            p->BeginErrorReadLine();
            p->WaitForExit();
            int code = p->ExitCode;
            current = nullptr;
            p->Close();
            return code;
        }

        void Cancel()
        {
            cancelled = true;
            Process^ p = current;
            if (p != nullptr)
            {
                try { if (!p->HasExited) p->Kill(); } catch (Exception^) { }
            }
        }

        void Reset() { cancelled = false; }

    private:
        Process^ current;
        bool cancelled;

        void OnData(Object^ sender, DataReceivedEventArgs^ e)
        {
            if (e->Data != nullptr) Line(e->Data);
        }
    };
}}
