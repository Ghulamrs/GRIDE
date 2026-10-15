// =============================================================================
//  ProgramRunner.h - runs a built program inside the IDE, as RIDE's window runs
//  one: its output comes line by line into the Output pane, an input line feeds
//  its standard input, Stop kills it and everything it started (a job object),
//  and the end is "[program returned N]" or "[stopped]". Verify runs the
//  emulator and the simulator captured and compares them line for line.
// =============================================================================
#pragma once
#include "ToolRunner.h"
#include "RideToolchain.h"

namespace Gride { namespace Services {

    using namespace System::Diagnostics;
    using namespace Gride::Core;
    using namespace Gride::Settings;

    public delegate void ProgramExitedHandler(int status, bool stopped);

    public ref class ProgramRunner
    {
    public:
        ProgramRunner() { proc = nullptr; job = IntPtr::Zero; running = false; stopped = false; LastStatus = 0; inputEncoding = Encoding::Default; }

        event TextLineHandler^ Output;          // the program's lines, both streams, as they come (worker threads)
        event ProgramExitedHandler^ Exited;     // once, on a worker thread

        property bool IsRunning { bool get() { return running; } }
        property int LastStatus;

        // starts the command line (the program, then its arguments, each quoted) in workDir; false when
        // it could not start, with the reason in the output
        bool Start(String^ commandLine, String^ workDir, List<String^>^ environment, int codePage)
        {
            if (running) return false;
            String^ file; String^ args;
            ToolRunner::SplitCommand(commandLine, file, args);
            if (!File::Exists(file)) { Output("could not run " + file); return false; }
            ToolRunner::PlainInputEncoding();
            ProcessStartInfo^ si = gcnew ProcessStartInfo(file, args);
            si->UseShellExecute = false;
            si->CreateNoWindow = true;
            si->RedirectStandardInput = true;
            si->RedirectStandardOutput = true;
            si->RedirectStandardError = true;
            Encoding^ enc = EncodingFor(codePage);
            si->StandardOutputEncoding = enc;
            si->StandardErrorEncoding = enc;
            inputEncoding = enc;
            if (!String::IsNullOrEmpty(workDir) && Directory::Exists(workDir)) si->WorkingDirectory = workDir;
            if (environment != nullptr)
                for each (String^ pair in environment)
                {
                    int eq = pair == nullptr ? -1 : pair->IndexOf(L'=');
                    if (eq > 0) si->EnvironmentVariables[pair->Substring(0, eq)->Trim()] = Environment::ExpandEnvironmentVariables(pair->Substring(eq + 1)->Trim());
                }
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
                Output("could not run " + file + ": " + ex->Message);
                return false;
            }
            job = NativeMethods::CreateJob();
            if (!NativeMethods::AssignToJob(job, p->Handle)) { NativeMethods::CloseHandle(job); job = IntPtr::Zero; }
            proc = p;
            p->BeginOutputReadLine();
            p->BeginErrorReadLine();
            return true;
        }

        // a line of input for the program, with its newline - as bytes of the program's code page, never a
        // byte-order mark (a StreamWriter on the pipe would put one in front of the first line)
        bool Write(String^ line)
        {
            Process^ p = proc;
            if (p == nullptr || !running) return false;
            try
            {
                array<Byte>^ bytes = inputEncoding->GetBytes(line + "\r\n");
                System::IO::Stream^ s = p->StandardInput->BaseStream;
                s->Write(bytes, 0, bytes->Length);
                s->Flush();
                return true;
            }
            catch (Exception^) { return false; }
        }

        // end of input (a program reading to the end goes on)
        void CloseInput()
        {
            Process^ p = proc;
            if (p == nullptr) return;
            try { p->StandardInput->Close(); } catch (Exception^) { }
        }

        // kills the program and everything it started
        void Stop()
        {
            Process^ p = proc;
            if (p == nullptr || !running) return;
            stopped = true;
            try
            {
                if (job != IntPtr::Zero) NativeMethods::TerminateJob(job);
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
        // compared line for line with the simulator's "CYCLES " line aside; the lines go to `say`;
        // 0 when they agree, 1 when they differ, 2 when the simulation is missing
        static int Verify(String^ emulateCommand, String^ simulateCommand, String^ workDir, int codePage, String^ outName, String^ missing,
                          bool hasArguments, Action<String^>^ say)
        {
            if (!String::IsNullOrEmpty(missing)) { say("[verify] " + missing); return 2; }
            if (hasArguments) say("[verify] the simulator takes no command line - both run without one");
            List<String^>^ emulated = gcnew List<String^>();
            List<String^>^ simulated = gcnew List<String^>();
            int emuStatus = Capture(emulateCommand, workDir, codePage, emulated);
            Capture(simulateCommand, workDir, codePage, simulated);
            String^ cycles = String::Empty;
            List<String^>^ b = gcnew List<String^>();
            for each (String^ line in simulated)
            {
                if (line->StartsWith("CYCLES ")) { cycles = line->Substring(7); continue; }
                b->Add(line);
            }
            for each (String^ line in emulated) say(line);
            say("[verify] emulator  (vm6747, the assembly):    " + emulated->Count + " lines, exit " + emuStatus);
            say("[verify] simulator (sim6747, " + outName + "): " + b->Count + " lines" + (cycles->Length == 0 ? String::Empty : ", " + cycles));
            int k = 0;
            while (k < emulated->Count && k < b->Count && emulated[k] == b[k]) k++;
            if (k == emulated->Count && k == b->Count)
            {
                say("[verify] the two agree, line for line");
                return 0;
            }
            say("[verify] they differ at line " + (k + 1) + ":");
            say("  emulator : " + (k < emulated->Count ? emulated[k] : "(no more output)"));
            say("  simulator: " + (k < b->Count ? b[k] : "(no more output)"));
            return 1;
        }

        // a program run to the end with its output kept (the null device as its input, as RIDE's runCaptured)
        static int Capture(String^ command, String^ workDir, int codePage, List<String^>^ lines)
        {
            ToolRunner^ r = gcnew ToolRunner();
            LineCollector^ c = gcnew LineCollector(lines);
            r->Line += gcnew TextLineHandler(c, &LineCollector::Add);
            ToolRunOptions^ o = gcnew ToolRunOptions();
            o->OutputCodePage = codePage;
            return r->Run(command, workDir, o);
        }

    private:
        ref class LineCollector
        {
        public:
            LineCollector(List<String^>^ into) { lines = into; }
            void Add(String^ s) { if (s != nullptr) lines->Add(s); }
        private:
            List<String^>^ lines;
        };

        Process^ proc;
        IntPtr job;
        Encoding^ inputEncoding;
        volatile bool running;
        volatile bool stopped;

        static Encoding^ EncodingFor(int codePage)
        {
            try
            {
                if (codePage > 0) return Encoding::GetEncoding(codePage);
                return Encoding::GetEncoding(System::Globalization::CultureInfo::CurrentCulture->TextInfo->OEMCodePage);
            }
            catch (Exception^) { return Encoding::Default; }
        }

        void OnData(Object^ sender, DataReceivedEventArgs^ e)
        {
            if (e->Data != nullptr) Output(e->Data);
        }

        // the program ended: its last lines are flushed (a child holding the pipe open is given ten seconds), then Exited
        void OnExited(Object^ sender, EventArgs^ e)
        {
            Process^ p = proc;
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
            Exited(code, stopped);
            try { p->Close(); } catch (Exception^) { }
        }
    };
}}
