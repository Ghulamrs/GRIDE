#pragma once
// ============================================================================
//  GRIDE 2.0 - a debugger, or a program that debugs itself, as a child (R09:
//  GRIDEd's Services\DebugProcess.h ported): lines are said to its standard
//  input (no byte-order mark) and what it prints is read until a marker turns
//  up or a time runs out, as RIDE's Process does it. Three ways to start it:
//    Start           both streams joined, on pipes (gdb, lldb, the emulator and simulator)
//    StartOnConsole  on a pseudo-console, where cdb flushes its prompt (RIDE's
//                    startOnPseudoConsole): the console's escape sequences are
//                    taken out as it is read, Enter is "\r"
//    StartApart      the streams kept apart (RIDE's shalimar::Channel): the lines on
//                    standard error are heard one at a time, standard output is the
//                    program's own and Printed answers it
//  The child lives in a job object, so Stop ends it and whatever it started.
// ============================================================================
#include "NativeConsole.h"

namespace Gride
{

    using namespace System::Diagnostics;
    using namespace System::Threading;

    public ref class DebugProcess
    {
    public:
        DebugProcess()
        {
            proc = nullptr; job = IntPtr::Zero; running = false; exitCode = 0; onConsole = false; apart = false;
            process = IntPtr::Zero; console = IntPtr::Zero; input = IntPtr::Zero; output = IntPtr::Zero;
            reader = nullptr; waiter = nullptr;
            pending = gcnew StringBuilder();
            heard = gcnew Queue<String^>();
            gotData = gcnew AutoResetEvent(false);
            encoding = gcnew UTF8Encoding(false);
            decoder = encoding->GetDecoder();
            vtState = 0; vtParams = gcnew StringBuilder(); row = 1; column = 1;
            Transcript = gcnew StringBuilder();
        }

        property bool Running { bool get() { return running; } }
        property int ExitCode { int get() { return exitCode; } }
        property bool OnConsole { bool get() { return onConsole; } }
        // everything said and read, for a report
        property StringBuilder^ Transcript;

        // starts the command line in workDir, both streams on one pipe; false, with the reason in `why`, when it could not start
        bool Start(String^ commandLine, String^ workDir, List<String^>^ environment, String^% why)
        {
            return StartOnPipes(commandLine, workDir, environment, false, why);
        }

        // the same with the streams kept apart: standard error is heard line by line (Hear), standard output is the program's (Printed)
        bool StartApart(String^ commandLine, String^ workDir, List<String^>^ environment, String^% why)
        {
            return StartOnPipes(commandLine, workDir, environment, true, why);
        }

        // on a pseudo-console: the child's whole environment is this process's with the session's laid over it
        bool StartOnConsole(String^ commandLine, String^ workDir, List<String^>^ environment, String^% why)
        {
            why = String::Empty;
            if (running) { why = "a session is already running"; return false; }
            String^ file; String^ args;
            SplitCommand(commandLine, file, args);
            if (!File::Exists(file)) { why = "not found: " + file; return false; }
            SortedDictionary<String^, String^>^ env = gcnew SortedDictionary<String^, String^>(StringComparer::OrdinalIgnoreCase);
            for each (System::Collections::DictionaryEntry e in Environment::GetEnvironmentVariables())
                env[e.Key->ToString()] = e.Value == nullptr ? String::Empty : e.Value->ToString();
            if (environment != nullptr)
                for each (String^ pair in environment)
                {
                    int eq = pair == nullptr ? -1 : pair->IndexOf(L'=');
                    if (eq > 0) env[pair->Substring(0, eq)->Trim()] = Environment::ExpandEnvironmentVariables(pair->Substring(eq + 1)->Trim());
                }
            List<String^>^ block = gcnew List<String^>();
            for each (KeyValuePair<String^, String^> kv in env) block->Add(kv.Key + "=" + kv.Value);
            Monitor::Enter(pending);
            try { pending->Length = 0; } finally { Monitor::Exit(pending); }
            vtState = 0; vtParams->Length = 0; row = 1; column = 1;
            decoder->Reset();
            IntPtr p, c, i, o, j;
            String^ reason;
            String^ dir = !String::IsNullOrEmpty(workDir) && Directory::Exists(workDir) ? workDir : nullptr;
            if (!NativeConsole::StartOnConsole(commandLine, dir, block->ToArray(), 500, 50, p, c, i, o, j, reason))
            {
                why = "could not start " + file + " on a console: " + reason;
                return false;
            }
            process = p; console = c; input = i; output = o; job = j;
            onConsole = true; apart = false; exitCode = 0;
            running = true;
            reader = gcnew Thread(gcnew ThreadStart(this, &DebugProcess::ReadConsole));
            reader->IsBackground = true;
            reader->Name = "console reader";
            reader->Start();
            waiter = gcnew Thread(gcnew ThreadStart(this, &DebugProcess::WaitConsoleChild));
            waiter->IsBackground = true;
            waiter->Name = "console waiter";
            waiter->Start();
            return true;
        }

        // a line to the child, with its line end: a newline on a pipe, Enter ("\r") on a console
        bool Say(String^ line)
        {
            if (!running) return false;
            try
            {
                if (onConsole)
                {
                    if (!NativeConsole::WriteTo(input, encoding->GetBytes(line + "\r"))) return false;
                }
                else
                {
                    Process^ p = proc;
                    if (p == nullptr) return false;
                    array<Byte>^ bytes = encoding->GetBytes(line + "\n");
                    System::IO::Stream^ s = p->StandardInput->BaseStream;
                    s->Write(bytes, 0, bytes->Length);
                    s->Flush();
                }
                Transcript->Append("> " + line + "\n");
                return true;
            }
            catch (Exception^) { return false; }
        }

        // what the child printed up to the marker (the marker and its line end taken out); found
        // says whether the marker came. When the time runs out, or the child ends, what is pending
        // is answered and found is false.
        String^ ReadUntil(String^ marker, int timeoutMs, bool% found)
        {
            found = false;
            if (String::IsNullOrEmpty(marker)) return String::Empty;
            Stopwatch^ sw = Stopwatch::StartNew();
            for (;;)
            {
                Monitor::Enter(pending);
                try
                {
                    String^ text = pending->ToString();
                    int at = text->IndexOf(marker);
                    if (at >= 0)
                    {
                        String^ answer = text->Substring(0, at);
                        int after = at + marker->Length;
                        if (after < text->Length && text[after] == L'\r') after++;
                        if (after < text->Length && text[after] == L'\n') after++;
                        pending->Length = 0;
                        pending->Append(text->Substring(after));
                        found = true;
                        return answer;
                    }
                    if (!running) { pending->Length = 0; return text; }
                }
                finally { Monitor::Exit(pending); }
                int left = timeoutMs - (int)sw->ElapsedMilliseconds;
                if (left <= 0) break;
                gotData->WaitOne(Math::Min(left, 250));
            }
            Monitor::Enter(pending);
            try { String^ rest = pending->ToString(); pending->Length = 0; return rest; }
            finally { Monitor::Exit(pending); }
        }

        // the next line said on standard error (StartApart), null when none came in the time; alive is
        // false once the child has ended with nothing left to hear (RIDE's Channel::hear)
        String^ Hear(int timeoutMs, bool% alive)
        {
            alive = true;
            Stopwatch^ sw = Stopwatch::StartNew();
            for (;;)
            {
                Monitor::Enter(heard);
                try { if (heard->Count > 0) return heard->Dequeue(); }
                finally { Monitor::Exit(heard); }
                if (!running) { alive = false; return nullptr; }
                int left = timeoutMs - (int)sw->ElapsedMilliseconds;
                if (left <= 0) return nullptr;
                gotData->WaitOne(Math::Min(left, 250));
            }
        }

        // what the program printed since the last call (standard output under StartApart; RIDE's Channel::printed)
        String^ Printed()
        {
            Monitor::Enter(pending);
            try { String^ text = pending->ToString(); pending->Length = 0; return text; }
            finally { Monitor::Exit(pending); }
        }

        // the child told to quit, then ended with everything it started
        void Stop(String^ quitCommand)
        {
            if (onConsole) { StopConsole(quitCommand); return; }
            Process^ p = proc;
            if (p == nullptr) return;
            if (running && !String::IsNullOrEmpty(quitCommand)) Say(quitCommand);
            try { p->StandardInput->Close(); } catch (Exception^) { }
            try
            {
                if (!p->WaitForExit(2000))
                {
                    if (job != IntPtr::Zero) NativeConsole::TerminateJob(job);
                    else if (!p->HasExited) p->Kill();
                }
            }
            catch (Exception^) { }
        }

        // "\"C:\\x\\cdb.exe\" -y ..." -> the program and the rest (GRIDEd's ToolRunner::SplitCommand)
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

    private:
        Process^ proc;
        IntPtr job;
        IntPtr process, console, input, output;
        Thread^ reader;
        Thread^ waiter;
        StringBuilder^ pending;
        Queue<String^>^ heard;
        AutoResetEvent^ gotData;
        UTF8Encoding^ encoding;
        System::Text::Decoder^ decoder;
        volatile bool running;
        int exitCode;
        bool onConsole;
        bool apart;
        // where the reading of the console's escape sequences has got to (RIDE's plainText state)
        int vtState;
        StringBuilder^ vtParams;
        int row, column;

        bool StartOnPipes(String^ commandLine, String^ workDir, List<String^>^ environment, bool keepApart, String^% why)
        {
            why = String::Empty;
            if (running) { why = "a session is already running"; return false; }
            String^ file; String^ args;
            SplitCommand(commandLine, file, args);
            if (!File::Exists(file)) { why = "not found: " + file; return false; }
            ToolRunner::PlainInputEncoding();
            ProcessStartInfo^ si = gcnew ProcessStartInfo(file, args);
            si->UseShellExecute = false;
            si->CreateNoWindow = true;
            si->RedirectStandardInput = true;
            si->RedirectStandardOutput = true;
            si->RedirectStandardError = true;
            si->StandardOutputEncoding = encoding;
            si->StandardErrorEncoding = encoding;
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
            p->OutputDataReceived += gcnew DataReceivedEventHandler(this, &DebugProcess::OnOutput);
            p->ErrorDataReceived += gcnew DataReceivedEventHandler(this, &DebugProcess::OnError);
            p->Exited += gcnew EventHandler(this, &DebugProcess::OnExited);
            Monitor::Enter(pending);
            try { pending->Length = 0; } finally { Monitor::Exit(pending); }
            Monitor::Enter(heard);
            try { heard->Clear(); } finally { Monitor::Exit(heard); }
            apart = keepApart; onConsole = false; exitCode = 0;
            running = true;
            try { p->Start(); }
            catch (Exception^ ex) { running = false; why = "could not start " + file + ": " + ex->Message; return false; }
            job = NativeConsole::CreateJob();
            if (!NativeConsole::AssignToJob(job, p->Handle)) { NativeConsole::Close(job); job = IntPtr::Zero; }
            proc = p;
            p->BeginOutputReadLine();
            p->BeginErrorReadLine();
            return true;
        }

        void OnOutput(Object^ sender, DataReceivedEventArgs^ e)
        {
            if (e->Data == nullptr) return;
            Monitor::Enter(pending);
            try { pending->Append(e->Data); pending->Append("\n"); Transcript->Append(e->Data + "\n"); }
            finally { Monitor::Exit(pending); }
            gotData->Set();
        }

        void OnError(Object^ sender, DataReceivedEventArgs^ e)
        {
            if (e->Data == nullptr) return;
            if (!apart) { OnOutput(sender, e); return; }
            Monitor::Enter(heard);
            try { heard->Enqueue(e->Data); Transcript->Append("! " + e->Data + "\n"); }
            finally { Monitor::Exit(heard); }
            gotData->Set();
        }

        void OnExited(Object^ sender, EventArgs^ e)
        {
            Process^ p = proc;
            // the last lines are still on their way when Exited fires; WaitForExit without a time waits for them
            try { p->WaitForExit(); } catch (Exception^) { }
            try { exitCode = p->ExitCode; } catch (Exception^) { }
            running = false;
            if (job != IntPtr::Zero) { NativeConsole::Close(job); job = IntPtr::Zero; }
            gotData->Set();
        }

        // ---- the pseudo-console ----
        void ReadConsole()
        {
            array<Byte>^ buffer = gcnew array<Byte>(4096);
            for (;;)
            {
                int got = NativeConsole::ReadFrom(output, buffer);
                if (got <= 0) break;
                String^ text = PlainText(buffer, got);
                if (text->Length == 0) continue;
                Monitor::Enter(pending);
                try { pending->Append(text); Transcript->Append(text); }
                finally { Monitor::Exit(pending); }
                gotData->Set();
            }
            gotData->Set();
        }

        void WaitConsoleChild()
        {
            int code = 0;
            NativeConsole::WaitForProcess(process, -1, code);
            exitCode = code;
            running = false;
            gotData->Set();
        }

        void StopConsole(String^ quitCommand)
        {
            if (process == IntPtr::Zero) return;
            if (running && !String::IsNullOrEmpty(quitCommand)) Say(quitCommand);
            int code = 0;
            if (!NativeConsole::WaitForProcess(process, 2000, code))
            {
                if (job != IntPtr::Zero) NativeConsole::TerminateJob(job);
                else NativeConsole::KillProcess(process);
                NativeConsole::WaitForProcess(process, 2000, code);
            }
            // the console closed while the reader still reads: closing waits for what it wrote to be read,
            // and the reader ends when the console's side of the pipe goes
            NativeConsole::CloseConsole(console);
            console = IntPtr::Zero;
            if (reader != nullptr) { reader->Join(3000); reader = nullptr; }
            if (waiter != nullptr) { waiter->Join(3000); waiter = nullptr; }
            running = false;
            NativeConsole::Close(input); input = IntPtr::Zero;
            NativeConsole::Close(output); output = IntPtr::Zero;
            NativeConsole::Close(process); process = IntPtr::Zero;
            if (job != IntPtr::Zero) { NativeConsole::Close(job); job = IntPtr::Zero; }
        }

        // what a pseudo-console writes, as text (RIDE's plainText): its escape sequences out - CSI, OSC
        // such as the title, the short ones - "\r" dropped, the cursor sent to a lower row as that many
        // newlines and sent right as that many spaces (the console writes a trailing space as a move).
        // One state across reads; the bytes are UTF-8, decoded with a state of their own.
        String^ PlainText(array<Byte>^ bytes, int size)
        {
            MemoryStream^ out = gcnew MemoryStream();
            for (int i = 0; i < size; i++)
            {
                Byte c = bytes[i];
                switch (vtState)
                {
                case 0:
                    if (c == 0x1b) { vtState = 1; continue; }
                    if (c == '\r' || c == 0x07) continue;
                    if (c == '\n') { row++; column = 1; }
                    else if ((c & 0xc0) != 0x80) column++;
                    out->WriteByte(c);
                    continue;
                case 1:
                    if (c == '[') { vtState = 2; vtParams->Length = 0; }
                    else if (c == ']') vtState = 3;
                    else if (c == '(' || c == ')' || c == '#' || c == '%') vtState = 5;
                    else vtState = 0;
                    continue;
                case 2:
                    if (c >= 0x40 && c <= 0x7e)
                    {
                        String^ params = vtParams->ToString();
                        if (c == 'H' || c == 'f')
                        {
                            int wanted = Math::Max(1, LeadingNumber(params, 0));
                            int semi = params->IndexOf(L';');
                            int col = semi < 0 ? 1 : Math::Max(1, LeadingNumber(params, semi + 1));
                            if (row < wanted) column = 1;
                            for (; row < wanted; ++row) out->WriteByte('\n');
                            for (; row == wanted && column < col; ++column) out->WriteByte(' ');
                        }
                        else if (c == 'C')
                        {
                            int n = Math::Max(1, LeadingNumber(params, 0));
                            for (; n > 0; --n, ++column) out->WriteByte(' ');
                        }
                        else if (c == 'G')
                        {
                            int col = LeadingNumber(params, 0);
                            for (; column < col; ++column) out->WriteByte(' ');
                        }
                        vtState = 0;
                    }
                    else vtParams->Append((wchar_t)c);
                    continue;
                case 3:
                    if (c == 0x07) vtState = 0;
                    else if (c == 0x1b) vtState = 4;
                    continue;
                case 4:
                    vtState = c == '\\' ? 0 : 3;
                    continue;
                default:
                    vtState = 0;
                    continue;
                }
            }
            array<Byte>^ kept = out->ToArray();
            if (kept->Length == 0) return String::Empty;
            int chars = decoder->GetCharCount(kept, 0, kept->Length);
            array<wchar_t>^ text = gcnew array<wchar_t>(chars);
            decoder->GetChars(kept, 0, kept->Length, text, 0);
            return gcnew String(text, 0, chars);
        }

        static int LeadingNumber(String^ s, int from)
        {
            int value = 0;
            for (int i = from; i < s->Length && Char::IsDigit(s[i]); i++) value = value * 10 + (s[i] - L'0');
            return value;
        }
    };
}
