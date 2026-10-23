// =============================================================================
//  Debugger.h - debugger abstraction + placeholder implementation
//
//  DebugEngine        abstract base the IDE talks to (Debug menu, panes)
//  PlaceholderDebugger simulates a session (break, step, registers, memory)
//                      so the whole Debug UI works today. Replace it with a
//                      real VM6747 / native debugger class later - only
//                      MainForm::CreateDebugger() needs to change.
// =============================================================================
#pragma once
#include "BuildMessage.h"
#include "../Core/LanguageTarget.h"

namespace Gride { namespace Services {

    using namespace Gride::Core;

    public enum class DebugState { Idle, Running, Break, Terminated };

    public ref class StopInfo
    {
    public:
        StopInfo(String^ file, int line, String^ reason) { File = file; Line = line; Reason = reason; Function = String::Empty; }
        StopInfo(String^ file, int line, String^ reason, String^ function) { File = file; Line = line; Reason = reason; Function = function == nullptr ? String::Empty : function; }
        property String^ File;
        property int Line;          // 1-based
        property String^ Reason;    // "breakpoint", "step", ...
        property String^ Function;  // the function it stopped in, when the engine says ("main"); "" for a Shalimar stop
    };

    public ref class RegisterValue
    {
    public:
        RegisterValue(String^ n, String^ v) { Name = n; Value = v; }
        property String^ Name;
        property String^ Value;
    };

    public ref class DebugFrame
    {
    public:
        DebugFrame(String^ fn, String^ file, int line) { Function = fn; File = file; Line = line; }
        property String^ Function;
        property String^ File;
        property int Line;
    };

    public ref class DebugVariable
    {
    public:
        DebugVariable(String^ n, String^ v, String^ t) { Name = n; Value = v; Type = t; Address = String::Empty; }
        DebugVariable(String^ n, String^ v, String^ t, String^ a) { Name = n; Value = v; Type = t; Address = a == nullptr ? String::Empty : a; }
        property String^ Name;
        property String^ Value;
        property String^ Type;
        property String^ Address;     // "0x..." where the variable lives, "" in a register or unknown (RIDE's Variable)
    };

    // RIDE's Watch: an expression read again at every stop and frame change
    public ref class DebugWatch
    {
    public:
        DebugWatch(String^ expression) { Expression = expression; Value = String::Empty; Type = String::Empty; Address = String::Empty; Ok = false; }
        property String^ Expression;
        property String^ Value;       // the value, or what the debugger said when it could not answer
        property String^ Type;
        property String^ Address;
        property bool Ok;
    };

    public delegate void DebugStateHandler(DebugState s);
    public delegate void DebugStopHandler(StopInfo^ info);

    // -------------------------------------------------------------------------
    //  DebugEngine - the contract between the IDE and any debugger
    // -------------------------------------------------------------------------
    public ref class DebugEngine abstract
    {
    public:
        event DebugStateHandler^ StateChanged;
        event DebugStopHandler^ Stopped;
        event TextLineHandler^ Output;           // the engine's own lines (the Output pane's Debug source)
        event TextLineHandler^ ProgramOutput;    // the program's own lines, as heard through the debugger (RIDE's console)

        property DebugState State { DebugState get() { return state; } }
        property bool IsActive { bool get() { return state == DebugState::Running || state == DebugState::Break; } }
        property StopInfo^ Location { StopInfo^ get() { return location; } }

        property String^ Name { virtual String^ get() abstract; }
        // the program behind the engine (vm6747 / sim6747 for the C6x), and why that one when another was not chosen
        property String^ Backend { virtual String^ get() { return Name; } }
        property String^ BackendWhy { virtual String^ get() { return String::Empty; } }
        // the frame the variables belong to (0 = where it stopped), and the status when the program ended
        property int CurrentFrame { int get() { return currentFrame; } }
        property int ExitStatus { int get() { return exitStatus; } }
        property bool Exited { bool get() { return exited; } }

        // program = linked output, entryFile/entryLine = where main() starts
        virtual bool Start(String^ program, Target target, String^ entryFile, int entryLine) abstract;
        // F10 / F11 started the session: Start stops at main's first line, as Visual Studio does (F5 runs on
        // to a breakpoint or the end)
        property bool StopAtEntry;
        virtual void Stop() abstract;
        virtual void Continue() abstract;
        virtual void BreakAll() abstract;
        virtual void StepInto() abstract;
        virtual void StepOver() abstract;
        virtual void StepOut() abstract;
        virtual void RunToCursor(String^ file, int line) abstract;

        virtual List<RegisterValue^>^ Registers() abstract;
        virtual List<DebugFrame^>^ CallStack() abstract;
        virtual List<DebugVariable^>^ Locals() abstract;
        virtual String^ Evaluate(String^ expression) abstract;
        virtual array<Byte>^ ReadMemory(UInt32 address, int length) abstract;

        // ---- what a real engine adds (RIDE's Debugger); an engine without them answers as below ----
        // the frame looked at: its variables are then what Locals answers (RIDE's selectFrame)
        virtual bool SelectFrame(int index) { return false; }
        // a value, with whether the engine could answer; said is the complaint when it could not
        virtual String^ Evaluate(String^ expression, bool% ok) { ok = true; return Evaluate(expression); }
        virtual String^ AddressOf(String^ expression) { return String::Empty; }
        virtual String^ TypeOf(String^ expression) { return String::Empty; }
        virtual bool SetVariable(String^ name, String^ value, String^% said) { said = "this debugger cannot set a value"; return false; }
        // RIDE's watches: read again at every stop and frame change
        virtual List<DebugWatch^>^ Watches() { return watches; }
        virtual void AddWatch(String^ expression) { if (!String::IsNullOrWhiteSpace(expression)) { watches->Add(gcnew DebugWatch(expression->Trim())); ReadWatches(); } }
        virtual void SetWatch(int index, String^ expression)
        {
            if (index < 0 || index >= watches->Count) return;
            if (String::IsNullOrWhiteSpace(expression)) { watches->RemoveAt(index); return; }
            watches[index] = gcnew DebugWatch(expression->Trim());
            ReadWatches();
        }
        virtual void RemoveWatch(int index) { if (index >= 0 && index < watches->Count) watches->RemoveAt(index); }
        virtual void ReadWatches()
        {
            for each (DebugWatch^ w in watches)
            {
                if (state != DebugState::Break) { w->Value = "not running"; w->Ok = false; w->Type = String::Empty; w->Address = String::Empty; continue; }
                bool ok;
                w->Value = Evaluate(w->Expression, ok);
                w->Ok = ok;
                w->Address = ok ? AddressOf(w->Expression) : String::Empty;
                w->Type = ok ? TypeOf(w->Expression) : String::Empty;
            }
        }
        // the breakpoints changed while the session runs: an engine that must be told resends them
        virtual void BreakpointsChanged() { }
        // why there are no locals to show, when the engine has none to give (RIDE's reasons)
        virtual String^ WhyNoLocals() { return String::Empty; }
        // why another frame cannot be looked at, when the engine walks no stack (RIDE's saysHowDeepOnly)
        virtual String^ WhyNoStackWalk() { return String::Empty; }
        // the program's own input: whether a line typed for it reaches it, and the line handed over (the
        // program's console echoes it, so it comes back among the program's lines)
        property bool TakesInput { virtual bool get() { return false; } }
        virtual bool SendInput(String^ line) { return false; }

        // a program line that goes on with the line before it, told unfinished ("Enter n: " at a stop, then
        // "5" once typed): the rest, after the engine's mark; false for a line of its own
        literal String^ ContinuedMark = "<<more>>";
        static bool Continues(String^ line, String^% rest)
        {
            rest = line;
            if (line == nullptr || !line->StartsWith(ContinuedMark)) return false;
            rest = line->Substring(ContinuedMark->Length);
            return true;
        }

        // breakpoints: full path -> 1-based lines (shared with the editors, read by the engine)
        void SetBreakpoints(Dictionary<String^, SortedSet<int>^>^ bps) { if (bps != nullptr) breakpoints = bps; }
        // the files of the program, so a leaf name the debugger says can be opened
        void SetSourceFiles(IEnumerable<String^>^ files) { sources->Clear(); if (files != nullptr) for each (String^ f in files) sources->Add(f); }

        // a file as the debugger named it (a leaf, or a path) to the project's own
        String^ ResolveFile(String^ named)
        {
            if (String::IsNullOrEmpty(named)) return named;
            try
            {
                if (Path::IsPathRooted(named) && File::Exists(named)) return Path::GetFullPath(named);
                String^ leaf = Path::GetFileName(named->Replace(L'/', L'\\'));
                for each (String^ f in sources) if (String::Compare(Path::GetFileName(f), leaf, true) == 0) return f;
                for each (String^ f in breakpoints->Keys) if (String::Compare(Path::GetFileName(f), leaf, true) == 0) return f;
            }
            catch (Exception^) { }
            return named;
        }

    protected:
        DebugEngine()
        {
            state = DebugState::Idle;
            location = nullptr;
            breakpoints = gcnew Dictionary<String^, SortedSet<int>^>(StringComparer::OrdinalIgnoreCase);
            sources = gcnew List<String^>();
            watches = gcnew List<DebugWatch^>();
            currentFrame = 0; exitStatus = 0; exited = false;
            StopAtEntry = false;
        }

        DebugState state;
        StopInfo^ location;
        Dictionary<String^, SortedSet<int>^>^ breakpoints;
        List<String^>^ sources;
        List<DebugWatch^>^ watches;
        int currentFrame;
        int exitStatus;
        bool exited;

        void SetState(DebugState s) { state = s; StateChanged(s); }
        void Print(String^ s) { Output(s); }
        void PrintProgram(String^ s) { ProgramOutput(s); }
        void StopAt(String^ file, int line, String^ reason) { StopAt(file, line, reason, String::Empty); }
        void StopAt(String^ file, int line, String^ reason, String^ function)
        {
            location = gcnew StopInfo(file, line, reason, function);
            currentFrame = 0;
            state = DebugState::Break;
            ReadWatches();      // read stopped, so the Watch pane shows values, not "not running"
            StateChanged(state);
            Stopped(location);
        }
    };

    // -------------------------------------------------------------------------
    //  PlaceholderDebugger - simulation until VM6747 / native debugging exists
    // -------------------------------------------------------------------------
    public ref class PlaceholderDebugger : public DebugEngine
    {
    public:
        PlaceholderDebugger() { lines = gcnew array<String^>(0); file = String::Empty; target = Target::Windows; steps = 0; }

        property String^ Name { virtual String^ get() override { return "Placeholder debugger (simulation)"; } }

        virtual bool Start(String^ program, Target t, String^ entryFile, int entryLine) override
        {
            target = t;
            file = entryFile;
            steps = 0;
            lines = (file != nullptr && File::Exists(file)) ? File::ReadAllLines(file) : gcnew array<String^>(0);
            Print("Placeholder debugger started - no real target is attached.");
            Print("Program: " + (program == nullptr ? String::Empty : program));
            if (program != nullptr && !File::Exists(program))
                Print("(program file does not exist yet - build the project to produce it)");
            if (t == Target::C6747) Print("VM6747 integration pending: execution is simulated line by line.");
            SetState(DebugState::Running);

            int first = NextBreakpoint(entryLine - 1);
            if (first > 0) StopAt(file, first, "breakpoint");
            else
            {
                int l = NextCodeLine(entryLine);
                if (l > 0) StopAt(file, l, "entry");
                else Finish();
            }
            return true;
        }

        virtual void Stop() override
        {
            if (state == DebugState::Idle) return;
            Print("Debugging stopped by user.");
            location = nullptr;
            SetState(DebugState::Terminated);
            SetState(DebugState::Idle);
        }

        virtual void Continue() override
        {
            if (state != DebugState::Break) return;
            SetState(DebugState::Running);
            int next = NextBreakpoint(location->Line);
            if (next > 0) StopAt(file, next, "breakpoint");
            else Finish();
        }

        virtual void BreakAll() override
        {
            if (state == DebugState::Running) StopAt(file, location != nullptr ? location->Line : 1, "break");
        }

        virtual void StepInto() override { Step(); }
        virtual void StepOver() override { Step(); }
        virtual void StepOut() override { Continue(); }

        virtual void RunToCursor(String^ f, int line) override
        {
            if (state != DebugState::Break) return;
            if (String::Compare(f, file, true) != 0 || line <= location->Line) { Continue(); return; }
            int bp = NextBreakpoint(location->Line);
            if (bp > 0 && bp < line) StopAt(file, bp, "breakpoint");
            else StopAt(file, line, "cursor");
        }

        virtual List<RegisterValue^>^ Registers() override
        {
            List<RegisterValue^>^ r = gcnew List<RegisterValue^>();
            int seed = (location != nullptr ? location->Line : 0) * 2654435 + steps * 977;
            if (target == Target::C6747)
            {
                for (int i = 0; i < 16; i++) r->Add(gcnew RegisterValue(String::Format("A{0}", i), Hex32(seed + i * 4099)));
                for (int i = 0; i < 16; i++) r->Add(gcnew RegisterValue(String::Format("B{0}", i), Hex32(seed * 3 + i * 7919)));
                r->Add(gcnew RegisterValue("PC", Hex32(0x11800000 + (location != nullptr ? location->Line : 0) * 32)));
                r->Add(gcnew RegisterValue("CSR", "0x14000100"));
                r->Add(gcnew RegisterValue("AMR", "0x00000000"));
                r->Add(gcnew RegisterValue("IER", "0x00000003"));
            }
            else
            {
                array<String^>^ names = { "RAX", "RBX", "RCX", "RDX", "RSI", "RDI", "RBP", "RSP", "R8", "R9", "R10", "R11" };
                for (int i = 0; i < names->Length; i++) r->Add(gcnew RegisterValue(names[i], Hex32(seed + i * 104729)));
                r->Add(gcnew RegisterValue("RIP", Hex32(0x00401000 + (location != nullptr ? location->Line : 0) * 16)));
                r->Add(gcnew RegisterValue("EFLAGS", "0x00000246"));
            }
            return r;
        }

        virtual List<DebugFrame^>^ CallStack() override
        {
            List<DebugFrame^>^ r = gcnew List<DebugFrame^>();
            if (location == nullptr) return r;
            r->Add(gcnew DebugFrame(FunctionAt(location->Line) + "()", file, location->Line));
            if (target == Target::C6747) r->Add(gcnew DebugFrame("_c_int00()  [RTS6747]", String::Empty, 0));
            else r->Add(gcnew DebugFrame("[startup code]", String::Empty, 0));
            return r;
        }

        virtual List<DebugVariable^>^ Locals() override
        {
            // simple scan of declarations "type name" between the function start and the current line
            List<DebugVariable^>^ r = gcnew List<DebugVariable^>();
            if (location == nullptr) return r;
            int start = FunctionStartLine(location->Line);
            Regex^ decl = gcnew Regex("^\\s*(?:const\\s+|static\\s+|unsigned\\s+|signed\\s+)*(?<type>int|char|short|long|float|double|[A-Z_]\\w*)\\s*\\*?\\s*(?<name>[A-Za-z_]\\w*)\\s*(=|;|\\[)");
            for (int i = start; i < location->Line - 1 && i < lines->Length; i++)
            {
                Match^ m = decl->Match(lines[i]);
                if (m->Success) r->Add(gcnew DebugVariable(m->Groups["name"]->Value, "<simulated>", m->Groups["type"]->Value));
            }
            return r;
        }

        virtual String^ Evaluate(String^ expression) override
        {
            return "<no target attached - value unavailable>";
        }

        virtual array<Byte>^ ReadMemory(UInt32 address, int length) override
        {
            array<Byte>^ b = gcnew array<Byte>(length);
            for (int i = 0; i < length; i++) b[i] = (Byte)(((address + (UInt32)i) * 31u + 7u) & 0xFF);
            return b;
        }

    private:
        array<String^>^ lines;
        String^ file;
        Target target;
        int steps;

        static String^ Hex32(int v) { return String::Format("0x{0:X8}", (UInt32)v); }

        void Finish()
        {
            Print("Program exited with code 0 (simulated).");
            location = nullptr;
            SetState(DebugState::Terminated);
            SetState(DebugState::Idle);
        }

        void Step()
        {
            if (state != DebugState::Break) return;
            steps++;
            int l = NextCodeLine(location->Line + 1);
            if (l > 0) StopAt(file, l, "step");
            else Finish();
        }

        // first breakpoint in the current file after 'afterLine' (1-based), or -1
        int NextBreakpoint(int afterLine)
        {
            SortedSet<int>^ list;
            if (file == nullptr || !breakpoints->TryGetValue(file, list)) return -1;
            int best = -1;
            for each (int l in list)
                if (l > afterLine && (best < 0 || l < best)) best = l;
            return best;
        }

        // first line >= from (1-based) that holds code (not blank, not a comment or brace)
        int NextCodeLine(int from)
        {
            if (from < 1) from = 1;
            for (int i = from; i <= lines->Length; i++)
            {
                String^ t = lines[i - 1]->Trim();
                if (t->Length == 0 || t == "{" || t == "}" || t->StartsWith("//") || t->StartsWith("/*") ||
                    t->StartsWith("*") || t->StartsWith("#")) continue;
                return i;
            }
            return -1;
        }

        int FunctionStartLine(int line)
        {
            Regex^ fn = gcnew Regex("^[A-Za-z_][\\w\\s\\*]*\\b(\\w+)\\s*\\([^;]*\\)\\s*\\{?\\s*$");
            for (int i = Math::Min(line - 1, lines->Length - 1); i >= 0; i--)
                if (fn->IsMatch(lines[i])) return i;
            return 0;
        }

        String^ FunctionAt(int line)
        {
            Regex^ fn = gcnew Regex("^[A-Za-z_][\\w\\s\\*]*\\b(\\w+)\\s*\\([^;]*\\)\\s*\\{?\\s*$");
            for (int i = Math::Min(line - 1, lines->Length - 1); i >= 0; i--)
            {
                Match^ m = fn->Match(lines[i]);
                if (m->Success) return m->Groups[1]->Value;
            }
            return "main";
        }
    };
}}
