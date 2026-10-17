// =============================================================================
//  ShalimarSession.h - RIDE's shalimar::Session for GRIDEd. A Shalimar program stops itself: the
//  compiler calls shm_line before every statement, and a Debug build's runtime, started with the
//  session's environment (SHM_DEBUG=1), talks on its standard error while the program prints on its
//  standard output - a line for every source (#file <unit> <leaf>), one when it is ready, one at
//  each stop (#stop <unit> <line> <depth>) and one at the end (#exit <status>). What it is told is
//  the session's commands in toolchain.json (b / d unit line, c, n, s, o, w, q). On tms6747 the
//  .out runs under the simulator. No variables, and one frame that says how deep it is: RIDE's
//  sentences say so.
// =============================================================================
#pragma once
#include "GdbDebugger.h"

namespace Gride { namespace Services {

    public ref class ShalimarSession : public DebugEngine
    {
    public:
        literal int ReadyTimeoutMs = 5000;       // RIDE's wait for each line before the ready line
        literal int SettleMs = 150;              // the shipped runtime can say ready before a file line: a moment for the rest
        literal int MoveTimeoutMs = 600000;      // a move waits for the program; Stop ends the wait

        ShalimarSession(DebugLaunch^ launch)
        {
            this->launch = launch;
            session = launch->Session;
            proc = nullptr; last = gcnew DebugStop(); moving = false;
            gate = gcnew Object();
            units = gcnew Dictionary<int, String^>();
            sent = gcnew List<String^>();
            unit = 0; line = 0; depth = 0; temporaryUnit = -1; temporaryLine = 0;
            Said = gcnew StringBuilder();
        }

        property String^ Name { virtual String^ get() override { return "the program itself (the Shalimar session)"; } }
        property String^ Backend { virtual String^ get() override { return launch->Backend; } }
        property String^ BackendWhy { virtual String^ get() override { return launch->BackendWhy; } }
        property bool Running { bool get() { return proc != nullptr && proc->Running; } }
        property DebugStop^ LastStop { DebugStop^ get() { return last; } }
        // unit -> the source's leaf name, as the program named them
        property Dictionary<int, String^>^ Units { Dictionary<int, String^>^ get() { return units; } }
        property int Depth { int get() { return depth; } }
        // every protocol line heard, for a report
        property StringBuilder^ Said;

        virtual bool Start(String^ program, Target target, String^ entryFile, int entryLine) override
        {
            if (!StartSession()) return false;
            BeginMove("run");
            return true;
        }

        // RIDE's Session::start: the program with the session's environment, every file line until the
        // ready line; then the first command (the shipped runtime reads and discards one line) and the breakpoints
        bool StartSession()
        {
            if (session == nullptr) { Print(launch->WhyNot->Length > 0 ? launch->WhyNot : "no session"); return false; }
            Stop();
            exited = false; exitStatus = 0; currentFrame = 0; last = gcnew DebugStop();
            units->Clear(); sent->Clear(); depth = 0; unit = 0; line = 0; temporaryUnit = -1;
            proc = gcnew DebugProcess();
            Print("[" + launch->Backend + "] " + launch->Command);
            if (launch->BackendWhy->Length > 0) Print("(" + launch->BackendWhy + ")");
            String^ why;
            if (!proc->StartApart(launch->Command, launch->WorkDir, launch->Environment, why))
            {
                Print("the program could not be started: " + why);
                proc = nullptr;
                return false;
            }
            String^ ready = session->ProtocolWord("ready");
            for (;;)
            {
                bool alive;
                String^ said = proc->Hear(ReadyTimeoutMs, alive);
                if (said == nullptr)
                {
                    PrintProgramLines();
                    Print(ShalimarWords::DidNotArm + (alive ? " (nothing came in " + (ReadyTimeoutMs / 1000) + " s)" : String::Empty));
                    proc->Stop(nullptr);
                    proc = nullptr;
                    return false;
                }
                Said->Append(said + "\n");
                if (said == ready) break;
                if (!Register(said)) PrintProgram(said);
            }
            for (;;)
            {
                bool alive;
                String^ said = proc->Hear(SettleMs, alive);
                if (said == nullptr) break;
                Said->Append(said + "\n");
                if (!Register(said)) PrintProgram(said);
            }
            if (session->AfterReady->Length > 0 && session->Has(session->AfterReady)) proc->Say(session->Command(session->AfterReady));
            SetState(DebugState::Running);
            int set = SendWanted();
            Print("started the program itself with " + set + " breakpoint" + (set == 1 ? "" : "s"));
            return true;
        }

        virtual void Stop() override
        {
            DebugProcess^ p = proc;
            if (p == nullptr) { if (state != DebugState::Idle) { location = nullptr; SetState(DebugState::Idle); } return; }
            proc = nullptr;
            p->Stop(session == nullptr ? nullptr : session->Command("quit"));
            if (state != DebugState::Idle)
            {
                Print("Debugging stopped by user.");
                location = nullptr;
                SetState(DebugState::Terminated);
                SetState(DebugState::Idle);
            }
        }

        virtual void Continue() override { if (state == DebugState::Break) BeginMove("continue"); }
        virtual void StepOver() override { if (state == DebugState::Break) BeginMove("stepOver"); }
        virtual void StepInto() override { if (state == DebugState::Break) BeginMove("stepInto"); }
        virtual void StepOut() override { if (state == DebugState::Break) BeginMove("stepOut"); }
        virtual void BreakAll() override { Print("a Shalimar program cannot be interrupted here - it stops at a breakpoint or at its end"); }

        // a breakpoint on the line for this run alone, taken away at the next stop
        virtual void RunToCursor(String^ file, int atLine) override
        {
            if (state != DebugState::Break) return;
            int u = UnitFor(file);
            DebugProcess^ p = proc;
            if (u < 0 || p == nullptr || !session->Has("break")) { Continue(); return; }
            p->Say(Fill(session->Command("break"), u, atLine));
            temporaryUnit = u; temporaryLine = atLine;
            BeginMove("continue");
        }

        // ---- the moves, synchronous (the tests' way in; the overrides above run these on a worker) ----
        DebugStop^ RunNow() { return MoveNow("run"); }
        DebugStop^ ContinueNow() { return MoveNow("continue"); }
        DebugStop^ StepOverNow() { return MoveNow("stepOver"); }
        DebugStop^ StepIntoNow() { return MoveNow("stepInto"); }
        DebugStop^ StepOutNow() { return MoveNow("stepOut"); }

        // ---- what the program answers while stopped: where it is, how deep, and nothing else ----
        virtual List<DebugVariable^>^ Locals() override { return gcnew List<DebugVariable^>(); }
        virtual String^ WhyNoLocals() override { return ShalimarWords::SaysWhereOnly; }

        virtual List<DebugFrame^>^ CallStack() override
        {
            List<DebugFrame^>^ frames = gcnew List<DebugFrame^>();
            if (state != DebugState::Break) return frames;
            frames->Add(gcnew DebugFrame(String::Format("{0} call{1} deep", depth, depth == 1 ? "" : "s"), ResolveFile(FileFor(unit)), line));
            return frames;
        }

        virtual bool SelectFrame(int index) override { return false; }
        virtual String^ WhyNoStackWalk() override { return ShalimarWords::SaysHowDeepOnly; }
        virtual String^ Evaluate(String^ expression) override { bool ok; return Evaluate(expression, ok); }
        virtual String^ Evaluate(String^ expression, bool% ok) override { ok = false; return ShalimarWords::SaysWhereOnly + " - nothing to watch with"; }
        virtual bool SetVariable(String^ name, String^ value, String^% said) override { said = ShalimarWords::SaysWhereOnly; return false; }
        virtual List<RegisterValue^>^ Registers() override { return gcnew List<RegisterValue^>(); }
        virtual array<Byte>^ ReadMemory(UInt64 address, int length) override { return gcnew array<Byte>(0); }

        // the breakpoints changed while stopped: the ones set taken away, the ones wanted set
        virtual void BreakpointsChanged() override
        {
            if (state != DebugState::Break) return;
            DebugProcess^ p = proc;
            if (p == nullptr) return;
            Monitor::Enter(gate);
            try
            {
                if (session->Has("clearBreakpoint"))
                    for each (String^ key in sent)
                    {
                        array<String^>^ parts = key->Split(L':');
                        p->Say(Fill(session->Command("clearBreakpoint"), Int32::Parse(parts[0]), Int32::Parse(parts[1])));
                    }
                sent->Clear();
                SendWanted();
            }
            finally { Monitor::Exit(gate); }
        }

    private:
        DebugLaunch^ launch;
        SessionDefinition^ session;
        DebugProcess^ proc;
        DebugStop^ last;
        Object^ gate;
        volatile bool moving;
        Dictionary<int, String^>^ units;
        List<String^>^ sent;                     // "unit:line" of every breakpoint told to the program
        int unit, line, depth;
        int temporaryUnit, temporaryLine;

        static String^ Fill(String^ templ, int u, int l)
        {
            return templ->Replace("$(Unit)", u.ToString())->Replace("$(Line)", l.ToString());
        }

        // "#file 0 steps.shl" remembered; false for any other line
        bool Register(String^ said)
        {
            String^ head = session->ProtocolWord("file");
            if (head->Length == 0 || !said->StartsWith(head + " ")) return false;
            String^ rest = said->Substring(head->Length + 1)->Trim();
            int space = rest->IndexOf(L' ');
            int u;
            if (space <= 0 || !Int32::TryParse(rest->Substring(0, space), u)) return false;
            units[u] = rest->Substring(space + 1)->Trim();
            return true;
        }

        int UnitFor(String^ file)
        {
            String^ leaf = Path::GetFileName(file);
            for each (KeyValuePair<int, String^> kv in units) if (String::Compare(kv.Value, leaf, true) == 0) return kv.Key;
            return -1;
        }

        String^ FileFor(int u)
        {
            String^ v;
            return units->TryGetValue(u, v) ? v : String::Empty;
        }

        // every stored breakpoint whose file the program named (RIDE's sendWanted); the others said
        int SendWanted()
        {
            int set = 0;
            DebugProcess^ p = proc;
            if (p == nullptr || !session->Has("break")) return 0;
            for each (KeyValuePair<String^, SortedSet<int>^> kv in breakpoints)
            {
                if (kv.Value->Count == 0) continue;
                int u = UnitFor(kv.Key);
                if (u < 0) { Print("no breakpoint in " + Path::GetFileName(kv.Key) + " - the program names no such source"); continue; }
                for each (int l in kv.Value)
                {
                    p->Say(Fill(session->Command("break"), u, l));
                    sent->Add(u + ":" + l);
                    set++;
                }
            }
            return set;
        }

        // three numbers after the line's first word
        static bool Three(String^ rest, int% a, int% b, int% c)
        {
            a = 0; b = 0; c = 0;
            array<String^>^ parts = rest->Split(gcnew array<wchar_t>{ L' ', L'\t' }, StringSplitOptions::RemoveEmptyEntries);
            if (parts->Length < 3) return false;
            return Int32::TryParse(parts[0], a) && Int32::TryParse(parts[1], b) && Int32::TryParse(parts[2], c);
        }

        ref class Move
        {
        public:
            Move(ShalimarSession^ e, String^ c) { engine = e; command = c; }
            void Run(Object^) { engine->MoveNow(command); }
        private:
            ShalimarSession^ engine; String^ command;
        };

        void BeginMove(String^ command)
        {
            if (moving) return;
            moving = true;
            if (state != DebugState::Running) SetState(DebugState::Running);
            ThreadPool::QueueUserWorkItem(gcnew WaitCallback(gcnew Move(this, command), &Move::Run));
        }

        DebugStop^ MoveNow(String^ commandId)
        {
            moving = true;
            try
            {
                if (state != DebugState::Running) state = DebugState::Running;
                DebugStop^ stop;
                Monitor::Enter(gate);
                try
                {
                    DebugProcess^ p = proc;
                    if (p == nullptr || !p->Running) { stop = gcnew DebugStop(); stop->Exited = true; stop->Status = exitStatus; }
                    else
                    {
                        p->Say(session->Command(commandId));
                        stop = Listen(MoveTimeoutMs);
                        if (temporaryUnit >= 0 && p->Running && session->Has("clearBreakpoint"))
                        {
                            p->Say(Fill(session->Command("clearBreakpoint"), temporaryUnit, temporaryLine));
                            temporaryUnit = -1;
                        }
                    }
                }
                finally { Monitor::Exit(gate); }
                Announce(stop, commandId == "run" || commandId == "continue");
                return stop;
            }
            finally { moving = false; }
        }

        // RIDE's Session::listen: the next stop or exit line, the program's own lines shown as they
        // come; the end of the program with neither is an exit with its exit code
        DebugStop^ Listen(int timeoutMs)
        {
            DebugStop^ where = gcnew DebugStop();
            String^ stopWord = session->ProtocolWord("stop");
            String^ exitWord = session->ProtocolWord("exit");
            String^ atWord = session->ProtocolWord("at");
            for (;;)
            {
                bool alive;
                String^ said = proc->Hear(timeoutMs, alive);
                if (said == nullptr)
                {
                    if (!alive)
                    {
                        where->Exited = true;
                        where->Status = proc->ExitCode;
                    }
                    where->Said = PrintProgramLines();
                    return where;
                }
                Said->Append(said + "\n");
                if (stopWord->Length > 0 && said->StartsWith(stopWord + " "))
                {
                    int u, l, d;
                    if (!Three(said->Substring(stopWord->Length), u, l, d)) continue;
                    unit = u; line = l; depth = d;
                    where->Stopped = true; where->File = FileFor(u); where->Line = l;
                    where->Said = PrintProgramLines();
                    return where;
                }
                if (exitWord->Length > 0 && said->StartsWith(exitWord + " "))
                {
                    int status, ignored, alsoIgnored;
                    if (!Three(said->Substring(exitWord->Length) + " 0 0", status, ignored, alsoIgnored)) status = 0;
                    where->Exited = true; where->Status = status;
                    WaitForEnd(2000);
                    where->Said = PrintProgramLines();
                    return where;
                }
                if (Register(said) || (atWord->Length > 0 && said->StartsWith(atWord + " "))) continue;
                PrintProgram(said);
            }
        }

        // the program's standard output is flushed as it ends: a moment for it after the exit line
        void WaitForEnd(int ms)
        {
            Stopwatch^ sw = Stopwatch::StartNew();
            while (proc != nullptr && proc->Running && sw->ElapsedMilliseconds < ms) Thread::Sleep(10);
        }

        String^ PrintProgramLines()
        {
            if (proc == nullptr) return String::Empty;
            String^ text = proc->Printed();
            for each (String^ l in DebugTranscript::Lines(text)) PrintProgram(l);
            return text;
        }

        // the stop or the end, as RIDE's window says them
        void Announce(DebugStop^ stop, bool ran)
        {
            if (stop->Stopped)
            {
                String^ file = ResolveFile(stop->File);
                last = stop;
                ReadWatches();
                Print(DebugTranscript::StopLine(file, stop->Line, String::Empty));
                StopAt(file, stop->Line, ran ? "breakpoint" : "step");
                return;
            }
            exited = stop->Exited;
            exitStatus = stop->Status;
            if (stop->Exited) Print("the program ran to the end and returned " + stop->Status);
            else Print("the program stopped answering");
            ReadWatches();
            DebugProcess^ p = proc;
            proc = nullptr;
            if (p != nullptr) p->Stop(session->Command("quit"));
            location = nullptr;
            SetState(DebugState::Terminated);
            SetState(DebugState::Idle);
        }
    };
}}
