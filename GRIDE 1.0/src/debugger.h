#ifndef EDITOR_DEBUGGER_H
#define EDITOR_DEBUGGER_H

#include <cstddef>
#include <string>
#include <vector>

#include "process.h"
#include "project.h"
#include "toolchain.h"

namespace editor {

enum DebuggerKind {
    DebuggerNone = 0,
    DebuggerLldb,
    DebuggerGdb,
    DebuggerCdb,
    // vm6747 --debug and sim6747 --run --debug: the C6747 emulator and simulator speaking gdb's commands and
    // answering in gdb's shapes (VM6747 src/Debug.h), so everything below that reads gdb reads them.
    DebuggerC6x
};

// gdb, or an engine that speaks as gdb does: the C6747 emulator and simulator.
bool dbg_gdbLike(DebuggerKind kind);

// Whether a linked C6747 image (.out) carries a .debug_line whose rows reach the program's own code - a
// unit with a set_address inside an executable section - which is what sim6747 --run --debug stops on.
bool dbg_imageHasLines(const std::string& out, std::string* why = 0);

// Which program debugs a tms6747 build: "sim6747" on the .out beside the assembly when it exists, is no
// older than the build and has lines, else "vm6747" on the assembly; GRIDE_DEBUG_SIM6747=1 and
// GRIDE_DEBUG_VM6747=1 force one. why says what decided it when the fallback was taken.
std::string dbg_c6xBackend(const std::string& executable, std::string* why = 0);

// gride-dbg, GRIDE's own debugger for x86_64-windows (the PDB of GRIDE's link or link.exe's, read through dbghelp, and
// answers in cdb's words): $GRIDE_DBG, else the one beside the editor; "" when there is none.
std::string dbg_grideDbg();
// Which program reads a Windows program's PDB: "gride-dbg" or "cdb", as settings.json's windowsDebugger asks and what
// is here allows - the one named stands in for the other where that one is not there; "" when neither is. why says
// what put it there when the setting asked otherwise.
std::string dbg_windowsBackend(std::string* why = 0);

// The line main() starts on in a source: C's and C++'s "int main(", Shalimar's "fun <> = main("; 0 where there is
// none, and in a header.
size_t dbg_mainLineIn(const std::string& file);

DebuggerKind dbg_here();
const char* dbg_name(DebuggerKind kind);
const char* dbg_program(DebuggerKind kind);

DebuggerKind dbg_for(ToolchainKind kind, const std::string& arch);

bool dbg_stopsItself(ToolchainKind kind);
// The Shalimar runtime directory to put beside a Shalimar .s on vm6747 --debug (" dir", quoted), else "".
std::string dbg_shalimarRuntime(const std::string& assembly);

struct DebugPlan {
    ToolchainKind kind;
    DebuggerKind engine;
    bool stopsItself;
    std::vector<std::string> blind;

    DebugPlan() : kind(ToolAuto), engine(DebuggerNone), stopsItself(false) {}

    bool possible() const { return stopsItself || engine != DebuggerNone; }
};

DebugPlan dbg_planFor(const Toolchain& tool, const std::vector<Part>& parts,
                      const std::string& arch);

std::string dbg_whyNot(ToolchainKind kind, const std::string& arch);

struct Stop {
    bool stopped;
    bool exited;
    int status;
    std::string file;
    size_t line;
    std::string function;
    std::string said;
    // The program's lines in said were told as they came (Debugger::tellProgram): none is left to show.
    bool told;

    Stop() : stopped(false), exited(false), status(0), line(0), told(false) {}
};

// A line the program printed, told while a move goes on.
typedef void (*ProgramSink)(void* context, const std::string& line);

// What starts the rest of a program line told unfinished before ("Enter n: ", then "5" once typed): gride-dbg's mark.
const char* dbg_continuedMark();

// address is where the variable lives, "0x..." as the engine said it, and empty in a register.
struct Variable {
    std::string name;
    std::string type;
    std::string value;
    std::string address;
};

struct StackFrame {
    std::string function;
    std::string file;
    size_t line;

    StackFrame() : line(0) {}
};

// address is filled where the expression is an lvalue and the engine says where it is.
struct Watch {
    std::string expression;
    std::string value;
    std::string type;
    std::string address;
    bool ok;

    Watch() : ok(false) {}
};

class Debugger {
public:
    Debugger();
    ~Debugger();

    bool start(DebuggerKind kind, const std::string& executable,
               const std::string& program = std::string());
    bool running() const { return kind_ != DebuggerNone && child_.running(); }
    DebuggerKind kind() const { return kind_; }
    // The program behind the kind - "vm6747" or "sim6747" for C6x, "gride-dbg" or "cdb" for cdb's words - and why
    // that one: vm6747 when the simulator was not chosen; cdb named by the setting over gride-dbg, or either one
    // standing in for the other the setting named.
    const std::string& backend() const { return backend_; }
    const std::string& backendWhy() const { return backendWhy_; }
    void stop();

    bool breakAt(const std::string& file, size_t line);
    bool clearBreakpoints();

    Stop run();
    Stop resume();
    Stop stepOver();
    Stop stepInto();
    Stop stepOut();
    // **F7 and F6 with nothing being debugged**: the run to main's first line, a breakpoint there for once, and
    // past main's prologue when the stop is its first instruction - as a step into a function goes on to its first
    // line. A line refused says so in note() and the run goes on to a breakpoint or the end.
    Stop runToEntry(const std::string& file, size_t line);
    // What the last move had to say beside its stop, or "".
    const std::string& note() const { return note_; }

    // **The program's lines as a move goes**, each told as it comes - a prompt among them before the program waits
    // for its input - not when the move ends; the Stop then says so (Stop::told). Read off the debugger's pipe, a
    // little at a time; a debugger on a console is read whole, and its lines told at the end of the move.
    void tellProgram(ProgramSink sink, void* context) { sink_ = sink; sinkContext_ = context; }
    // **The program's own input** while it is debugged, where the engine hands it over (gride-dbg's .input): at
    // once, even while a move waits for the program, which may be waiting for this very line. Not echoed: the
    // program's console echoes it among its own lines.
    bool takesInput() const;
    bool sendInput(const std::string& line);

    std::vector<Variable> locals();

    std::vector<StackFrame> frames();

    bool selectFrame(size_t which);

    bool setVariable(const std::string& name, const std::string& value,
                     std::string* said = 0);

    std::string evaluate(const std::string& expression, bool* ok = 0);

    // Where an expression lives - "&(e)" asked of the engine - or empty when it is no lvalue.
    std::string addressOf(const std::string& expression);

    // The type an expression has, as the engine names it: cdb's ??, lldb's expression, gdb's whatis.
    std::string typeOf(const std::string& expression);

    void addWatch(const std::string& expression);
    void setWatch(size_t which, const std::string& expression);
    void removeWatch(size_t which);
    const std::vector<Watch>& watches() const { return watches_; }
    void readWatches();

    std::string ask(const std::string& command, int timeoutMs = 30000);

private:
    Debugger(const Debugger&);
    Debugger& operator=(const Debugger&);

    Stop afterMoving(const std::string& command);

    Stop afterStepping(const std::string& command);

    // A move's answer, waited for as long as the program may take; told: how much of it was told already.
    std::string askMoving(const std::string& command, size_t* told);
    void tell(const std::string& said);
    bool ownDebugger() const { return kind_ == DebuggerCdb && backend_ == "gride-dbg"; }

    Process child_;
    DebuggerKind kind_;
    std::string backend_, backendWhy_;
    bool onConsole_;
    std::string executable_;
    std::string note_;
    ProgramSink sink_;
    void* sinkContext_;

    Stop last_;

    std::vector<Watch> watches_;
};

Stop dbg_readStop(DebuggerKind kind, const std::string& said);

bool dbg_stoppedWithNoSource(const std::string& said);

bool dbg_wentNowhere(DebuggerKind kind, const Stop& before, const Stop& after);

std::string dbg_withoutEscapes(const std::string& text);
std::string dbg_withoutEcho(const std::string& said, const std::string& asked,
                            const std::string& marker);

std::string dbg_programOutput(DebuggerKind kind, const std::string& said);

std::vector<Variable> dbg_readVariables(DebuggerKind kind, const std::string& said);

std::vector<StackFrame> dbg_readFrames(DebuggerKind kind, const std::string& said);

std::string dbg_frameLine(const StackFrame& frame, bool looking = false);
size_t dbg_frameOnLine(const std::vector<StackFrame>& stack, const std::string& line);

std::string dbg_variableLine(const Variable& variable);
size_t dbg_variableOnLine(const std::vector<Variable>& locals, const std::string& line);

std::string dbg_watchLine(const Watch& watch);
size_t dbg_watchOnLine(const std::vector<Watch>& watches, const std::string& line);

std::string dbg_readValue(DebuggerKind kind, const std::string& said);

// The first "0x..." in an answer, cdb's backtick taken out, or empty: how every engine spells an address.
std::string dbg_addressIn(const std::string& said);

// The type in an answer to typeOf's question, or empty: cdb "int 0n12", lldb "(int) $0 = 12", gdb "type = int".
std::string dbg_readType(DebuggerKind kind, const std::string& said);

std::string dbg_lookingAt(const StackFrame& frame);

std::string dbg_stopLine(const std::string& file, size_t line, const std::string& function);

}

#endif
