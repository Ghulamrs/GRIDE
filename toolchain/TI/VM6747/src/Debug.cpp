// The debugger session of Debug.h: gdb's commands in, gdb's answers out. Every stop is reported the
// way gdb reports it - "Breakpoint 1, main () at t.c:6" then "6\t<the line>", a step that stays in
// its function as the echo alone, a step that lands in another as "f () at t.c:3" first, the end as
// "[Inferior 1 (process 1) exited normally]" or "... with code 03" (octal, as gdb) - because that is
// what RIDE's debugger.cpp parses for gdb, and one parser serving three engines is the whole point.
//
// A call stack the machine never keeps is kept here: a frame is pushed when the program counter
// reaches a function's first instruction, with B3 - the return address, landed by then - beside
// it, and popped when the program counter reaches that return address. That is what `next`
// (the same depth or less), `step` (any depth), `finish` (one less) and `backtrace` read.

#include "Debug.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>

namespace dbg {

namespace {

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string leafOf(const std::string &path) {
    size_t k = path.find_last_of("/\\");
    return k == std::string::npos ? path : path.substr(k + 1);
}

bool allDigits(const std::string &s) {
    if (s.empty()) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    return true;
}

// A register's number from its name - A0..A31, B0..B31, pc, sp (B15), fp (A15) - or -1.
int registerNamed(const std::string &n) {
    if (n == "sp") return 32 + 15;
    if (n == "fp") return 15;
    if (n == "pc") return 64;
    if (n.size() < 2 || n.size() > 3 || (n[0] != 'A' && n[0] != 'B')) return -1;
    if (!allDigits(n.substr(1))) return -1;
    int k = std::atoi(n.c_str() + 1);
    if (k > 31) return -1;
    return (n[0] == 'A' ? 0 : 32) + k;
}

std::string registerName(int r) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "%c%d", r < 32 ? 'A' : 'B', r & 31);
    return buf;
}

struct Breakpoint { int id = 0; uint32_t addr = 0; std::string file; int line = 0; std::string function; };
struct Frame { uint32_t entry = 0; uint32_t returnPc = 0; };

class Session {
public:
    explicit Session(Machine &m) : m_(m) {}
    int run();

private:
    Machine &m_;
    std::vector<Breakpoint> breaks_;
    int nextBreak_ = 1;
    std::vector<Frame> frames_;
    bool started_ = false, alive_ = true;
    int values_ = 0;
    size_t frame_ = 0;            // the frame `frame N` selected, 0 the innermost
    uint32_t lastEntry_ = 0;      // the function the last stop was in: a stop elsewhere names its function
    Position entryPos_;           // the position of the last function entered: its own line, which a step passes

    enum Mode { Continue, Next, Step, Finish };
    void go(Mode mode);
    void track(uint32_t pc);
    const Breakpoint *breakAt(uint32_t pc) const;
    void stopped(const char *head, bool announceFunction);
    void exited();
    std::string place(uint32_t pc, bool withAddress);
    uint32_t framePc(size_t k) const;
    void say(const std::string &s) { std::fputs(s.c_str(), stdout); }
    void line(const std::string &s) { std::fputs(s.c_str(), stdout); std::fputc('\n', stdout); }

    void cmdBreak(const std::string &arg);
    void cmdDelete(const std::string &arg);
    void cmdPrint(const std::string &arg, bool typeOnly);
    void cmdSet(const std::string &arg);
    void cmdBacktrace();
    void cmdFrame(const std::string &arg);
    void cmdRegisters();
    void cmdInfoBreak();
    void cmdInfoVariables(bool params);
    bool evaluate(const std::string &expr, uint32_t &value, bool &isRegister, int &reg);
    // -g: the selected frame's variables, where one lives, and its value as gdb prints it
    const std::vector<Variable> *frameVariables() const;
    const Variable *variableNamed(const std::string &name) const;
    bool addressOf(const Variable &v, uint32_t &addr) const;
    std::string valueOf(const Variable &v) const;
    uint32_t read32(uint32_t addr) const;
};

const Breakpoint *Session::breakAt(uint32_t pc) const {
    for (const Breakpoint &b : breaks_) if (b.addr == pc) return &b;
    return nullptr;
}

// A function's first packet pushes a frame; the program counter reaching a frame's return address
// pops it, and every frame above it - a frame a tail call or a goto left behind goes with the return.
void Session::track(uint32_t pc) {
    for (size_t k = frames_.size(); k-- > 0;)
        if (frames_[k].returnPc == pc) { frames_.resize(k); break; }
    if (m_.isFunctionEntry(pc)) {
        Frame f; f.entry = pc; f.returnPc = m_.reg(32 + 3); frames_.push_back(f);
        if (!m_.positionAt(pc, entryPos_)) entryPos_ = Position();
    }
}

uint32_t Session::framePc(size_t k) const {
    if (k == 0) return m_.pc();
    if (k >= frames_.size()) return 0;
    return frames_[frames_.size() - k].returnPc;
}

// "main () at t.c:6", or with the address in front as gdb writes a stop that is not at a line's start.
std::string Session::place(uint32_t pc, bool withAddress) {
    std::string fn, out;
    uint32_t entry = 0;
    if (!m_.functionAt(pc, fn, entry)) fn = "??";
    char buf[32];
    if (withAddress) { std::snprintf(buf, sizeof buf, "0x%08x in ", pc); out += buf; }
    out += fn + " ()";
    Position p;
    if (m_.positionAt(pc, p) && p.known()) out += " at " + p.file + ":" + std::to_string(p.line);
    return out;
}

void Session::stopped(const char *head, bool announceFunction) {
    uint32_t pc = m_.pc();
    std::string fn;
    uint32_t entry = 0;
    m_.functionAt(pc, fn, entry);
    Position p;
    bool here = m_.positionAt(pc, p) && p.known();
    if (head != nullptr && std::strcmp(head, "Run till exit done: ") == 0) line(place(pc, true));
    else if (head != nullptr) line(std::string(head) + place(pc, false));
    else if (announceFunction || entry != lastEntry_ || !here) line(place(pc, !here));
    if (here) line(std::to_string(p.line) + "\t" + m_.sourceLine(p.file, p.line));
    lastEntry_ = entry;
    frame_ = 0;
}

void Session::exited() {
    m_.finish();
    alive_ = false;
    int st = m_.status() & 0xff;
    char buf[64];
    if (st == 0) std::snprintf(buf, sizeof buf, "[Inferior 1 (process 1) exited normally]");
    else std::snprintf(buf, sizeof buf, "[Inferior 1 (process 1) exited with code %02o]", st);
    line(buf);
}

void Session::go(Mode mode) {
    const size_t depth0 = frames_.size();
    Position pos0;
    m_.positionAt(m_.pc(), pos0);
    bool first = true;
    for (;;) {
        uint32_t pc = m_.pc();
        if (!first) {
            track(pc);
            if (const Breakpoint *b = breakAt(pc)) {
                stopped(("\nBreakpoint " + std::to_string(b->id) + ", ").c_str(), true);
                return;
            }
            Position p;
            bool here = m_.positionAt(pc, p) && p.known();
            size_t depth = frames_.size();
            if (mode == Next && ((here && depth <= depth0 && p != pos0) || depth < depth0)) { stopped(nullptr, false); return; }
            // A step into a callee passes its entry position - the function's own line, over the prologue - and
            // stops on its first statement, where gdb stops; one that returns stops in the caller wherever it lands.
            if (mode == Step && ((here && p != pos0 && !(depth > depth0 && p == entryPos_)) || depth < depth0)) { stopped(nullptr, false); return; }
            if (mode == Finish && depth < depth0) { stopped("Run till exit done: ", false); return; }
        }
        first = false;
        m_.step();
        std::fflush(stdout);
        if (m_.ended()) { exited(); return; }
    }
}

// break FILE:LINE, break LINE (the file the program counter is in), break FUNCTION.
void Session::cmdBreak(const std::string &arg) {
    std::string a = trim(arg);
    if (a.empty()) { line("No default breakpoint address now."); return; }
    Breakpoint b;
    b.id = nextBreak_;
    size_t colon = a.find_last_of(':');
    std::string file = colon == std::string::npos ? std::string() : a.substr(0, colon);
    std::string tail = colon == std::string::npos ? a : a.substr(colon + 1);
    if (allDigits(tail)) {
        if (file.empty()) {
            Position p;
            if (m_.positionAt(m_.pc(), p) && p.known()) file = p.file;
        }
        if (!m_.hasLines()) { line("No line " + tail + " in file \"" + file + "\": " + m_.noLinesWhy()); return; }
        int used = 0;
        if (file.empty() || !m_.addressOfLine(leafOf(file), std::atoi(tail.c_str()), b.addr, used)) {
            line("No line " + tail + " in file \"" + file + "\".");
            return;
        }
        b.file = file; b.line = used;
    } else {
        if (!m_.addressOfFunction(a, b.addr)) { line("Function \"" + a + "\" not defined."); return; }
        b.function = a;
        Position p;
        if (m_.positionAt(b.addr, p) && p.known()) { b.file = p.file; b.line = p.line; }
    }
    nextBreak_++;
    breaks_.push_back(b);
    char buf[48];
    std::snprintf(buf, sizeof buf, "Breakpoint %d at 0x%x", b.id, b.addr);
    std::string said = buf;
    if (b.line > 0) said += ": file " + b.file + ", line " + std::to_string(b.line) + ".";
    else said += ".";
    line(said);
}

void Session::cmdDelete(const std::string &arg) {
    std::string a = trim(arg);
    if (a.empty()) { breaks_.clear(); return; }
    int id = std::atoi(a.c_str());
    for (size_t k = 0; k < breaks_.size(); k++)
        if (breaks_[k].id == id) { breaks_.erase(breaks_.begin() + static_cast<long>(k)); return; }
    line("No breakpoint number " + a + ".");
}

void Session::cmdInfoBreak() {
    if (breaks_.empty()) { line("No breakpoints or watchpoints."); return; }
    line("Num     Type           Disp Enb Address    What");
    for (const Breakpoint &b : breaks_) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "%-7d breakpoint     keep y   0x%08x in ", b.id, b.addr);
        std::string fn; uint32_t entry = 0;
        if (!m_.functionAt(b.addr, fn, entry)) fn = "??";
        line(buf + fn + " at " + b.file + ":" + std::to_string(b.line));
    }
}

// A register ($A4, $pc, $sp), a number (decimal or 0x), or nothing this machine knows a name for.
bool Session::evaluate(const std::string &expr, uint32_t &value, bool &isRegister, int &reg) {
    std::string e = trim(expr);
    isRegister = false; reg = -1;
    if (e.empty()) return false;
    if (e[0] == '$') {
        reg = registerNamed(e.substr(1));
        if (reg < 0) return false;
        isRegister = true;
        value = reg == 64 ? m_.pc() : m_.reg(reg);
        return true;
    }
    char *end = nullptr;
    long long v = std::strtoll(e.c_str(), &end, 0);
    if (end != nullptr && *end == '\0') { value = static_cast<uint32_t>(v); return true; }
    return false;
}

void Session::cmdPrint(const std::string &arg, bool typeOnly) {
    uint32_t v = 0; bool isReg = false; int reg = -1;
    std::string e = trim(arg);
    if (!evaluate(e, v, isReg, reg)) {
        std::string name = e;
        bool address = !name.empty() && name[0] == '&';
        if (address) name = trim(name.substr(1));
        if (name.size() > 1 && name[0] == '(' && name[name.size() - 1] == ')') name = trim(name.substr(1, name.size() - 2));
        // -g: a variable of the selected frame - print n, print &(n), whatis n
        if (const Variable *var = variableNamed(name)) {
            if (typeOnly) { line("type = " + var->type + (address ? " *" : "")); return; }
            values_++;
            if (address) {
                uint32_t at = 0;
                if (!addressOf(*var, at)) { line("Can't take address of \"" + name + "\" which isn't an lvalue."); return; }
                char buf[96];
                std::snprintf(buf, sizeof buf, "$%d = (%s *) 0x%x", values_, var->type.c_str(), at);
                line(buf);
            } else line("$" + std::to_string(values_) + " = " + valueOf(*var));
            return;
        }
        line("No symbol \"" + name + "\" in current context.");
        return;
    }
    if (typeOnly) { line(reg == 64 ? "type = void (*)()" : "type = int"); return; }
    values_++;
    char buf[48];
    if (reg == 64) std::snprintf(buf, sizeof buf, "$%d = (void (*)()) 0x%x", values_, v);
    else std::snprintf(buf, sizeof buf, "$%d = %d", values_, static_cast<int>(v));
    line(buf);
}

// set variable $A4 = 5 / set var ...; any other `set` (confirm, pagination, width ...) is taken silently.
void Session::cmdSet(const std::string &arg) {
    std::string a = trim(arg);
    bool var = a.compare(0, 9, "variable ") == 0 || a.compare(0, 4, "var ") == 0;
    if (!var) return;
    a = trim(a.substr(a.find(' ') + 1));
    size_t eq = a.find('=');
    if (eq == std::string::npos) { line("A syntax error in expression, near `'."); return; }
    std::string lhs = trim(a.substr(0, eq)), rhs = trim(a.substr(eq + 1));
    uint32_t v = 0; bool isReg = false; int reg = -1;
    if (!evaluate(lhs, v, isReg, reg) || !isReg || reg == 64) {
        line("No symbol \"" + (lhs.empty() || lhs[0] != '$' ? lhs : lhs.substr(1)) + "\" in current context.");
        return;
    }
    uint32_t value = 0; bool r2 = false; int reg2 = -1;
    if (!evaluate(rhs, value, r2, reg2)) { line("No symbol \"" + rhs + "\" in current context."); return; }
    m_.setReg(reg, value);
}

void Session::cmdBacktrace() {
    if (!alive_ || !started_) { line("No stack."); return; }
    size_t n = frames_.empty() ? 1 : frames_.size();
    for (size_t k = 0; k < n; k++) {
        uint32_t pc = framePc(k);
        if (k > 0 && pc == 0) break;          // the outermost frame's return address, never set: the start-up's
        line("#" + std::to_string(k) + "  " + place(pc, k > 0));
    }
}

void Session::cmdFrame(const std::string &arg) {
    std::string a = trim(arg);
    if (!alive_ || !started_) { line("No stack."); return; }
    size_t k = a.empty() ? frame_ : static_cast<size_t>(std::atoi(a.c_str()));
    size_t n = frames_.empty() ? 1 : frames_.size();
    if (k >= n) { line("No frame at level " + a + "."); return; }
    frame_ = k;
    uint32_t pc = framePc(k);
    line("#" + std::to_string(k) + "  " + place(pc, k > 0));
    Position p;
    if (m_.positionAt(pc, p) && p.known()) line(std::to_string(p.line) + "\t" + m_.sourceLine(p.file, p.line));
}

uint32_t Session::read32(uint32_t addr) const {
    uint8_t b[4] = { 0, 0, 0, 0 };
    if (!m_.read(addr, b, 4)) return 0;
    return b[0] | (b[1] << 8) | (b[2] << 16) | (static_cast<uint32_t>(b[3]) << 24);
}

// The selected frame's function's variables, from the tags the compilers wrote (-g).
const std::vector<Variable> *Session::frameVariables() const {
    if (!alive_ || !started_) return nullptr;
    std::string fn;
    uint32_t entry = 0;
    if (!m_.functionAt(framePc(frame_), fn, entry)) return nullptr;
    return m_.variablesAt(entry);
}

const Variable *Session::variableNamed(const std::string &name) const {
    const std::vector<Variable> *vs = frameVariables();
    if (vs == nullptr) return nullptr;
    for (const Variable &v : *vs) if (v.name == name) return &v;
    return nullptr;
}

// A frame variable is at the frame's A15 plus its offset. Every function the compilers write keeps the
// caller's A15 at the word its own A15 points to (STW A15, *B15 then MV B15, A15), so an outer frame's
// A15 is read down that chain, one word a frame.
bool Session::addressOf(const Variable &v, uint32_t &addr) const {
    if (v.global) {
        if (!m_.addressOfFunction(v.symbol, addr)) return false;
    } else {
        uint32_t a15 = m_.reg(15);
        for (size_t k = 0; k < frame_; k++) a15 = read32(a15);
        addr = a15 + static_cast<uint32_t>(v.offset);
    }
    if (v.deref) addr = read32(addr);
    return true;
}

// The value as gdb prints it: an integer in decimal, a real as %g, a pointer in hex, an aggregate as {...}.
std::string Session::valueOf(const Variable &v) const {
    uint32_t addr = 0;
    if (!addressOf(v, addr)) return "<optimized out>";
    std::string t = v.type;
    for (const char *q : { "const ", "volatile " })
        for (size_t at; (at = t.find(q)) != std::string::npos;) t.erase(at, std::strlen(q));
    t = trim(t);
    char buf[64];
    if (!t.empty() && (t.back() == '*' || t.find("(*") != std::string::npos)) {
        std::snprintf(buf, sizeof buf, "(%s) 0x%x", v.type.c_str(), read32(addr));
        return buf;
    }
    if (t.empty() || t.find('[') != std::string::npos || t.compare(0, 7, "struct ") == 0 || t.compare(0, 6, "class ") == 0 ||
        t.compare(0, 6, "union ") == 0)
        return "{...}";
    uint8_t b[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    if (t == "double" || t == "long double" || t == "real") {
        if (!m_.read(addr, b, 8)) return "<unreadable>";
        double d; std::memcpy(&d, b, 8);
        std::snprintf(buf, sizeof buf, "%g", d);
        return buf;
    }
    if (t == "float") {
        if (!m_.read(addr, b, 4)) return "<unreadable>";
        float f; std::memcpy(&f, b, 4);
        std::snprintf(buf, sizeof buf, "%g", f);
        return buf;
    }
    bool isUnsigned = t.compare(0, 9, "unsigned ") == 0 || t == "unsigned" || t == "bool" || t == "_Bool";
    size_t size = 4;
    if (t.find("char") != std::string::npos || t == "bool" || t == "_Bool") size = 1;
    else if (t.find("short") != std::string::npos) size = 2;
    else if (t.find("long long") != std::string::npos) size = 8;
    if (!m_.read(addr, b, size)) return "<unreadable>";
    uint64_t u = 0;
    for (size_t k = size; k-- > 0;) u = (u << 8) | b[k];
    if (t == "bool" || t == "_Bool") return u ? "true" : "false";
    if (isUnsigned) { std::snprintf(buf, sizeof buf, "%llu", static_cast<unsigned long long>(u)); return buf; }
    long long s = size == 8 ? static_cast<long long>(u) : static_cast<long long>(u << (64 - 8 * size)) >> (64 - 8 * size);
    std::snprintf(buf, sizeof buf, "%lld", s);
    return buf;
}

// info args / info locals: "name = value" a line, as gdb answers; a name two blocks share is listed once.
void Session::cmdInfoVariables(bool params) {
    if (!alive_ || !started_) { line("No frame selected."); return; }
    const std::vector<Variable> *vs = frameVariables();
    if (vs == nullptr) { line("No symbol table info available."); return; }
    std::vector<std::string> seen;
    for (const Variable &v : *vs) {
        if (v.param != params) continue;
        bool again = false;
        for (const std::string &s : seen) if (s == v.name) again = true;
        if (again) continue;
        seen.push_back(v.name);
        line(v.name + " = " + valueOf(v));
    }
    if (seen.empty()) line(params ? "No arguments." : "No locals.");
}

void Session::cmdRegisters() {
    for (int r = 0; r < 64; r++) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%-15s0x%-18x%d", registerName(r).c_str(), m_.reg(r), static_cast<int>(m_.reg(r)));
        line(buf);
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "%-15s0x%-18x0x%x", "pc", m_.pc(), m_.pc());
    line(buf);
}

int Session::run() {
    std::string input;
    while (std::getline(std::cin, input)) {
        std::string c = trim(input);
        if (c.empty()) { std::fflush(stdout); continue; }
        size_t sp = c.find_first_of(" \t");
        std::string verb = sp == std::string::npos ? c : c.substr(0, sp);
        std::string arg = sp == std::string::npos ? std::string() : trim(c.substr(sp + 1));
        if (verb == "quit" || verb == "q") break;
        else if (verb == "echo") {
            std::string out;
            for (size_t i = 0; i < arg.size(); i++) {
                if (arg[i] == '\\' && i + 1 < arg.size()) { i++; out += arg[i] == 'n' ? '\n' : arg[i] == 't' ? '\t' : arg[i]; }
                else out += arg[i];
            }
            say(out);
        }
        else if (verb == "set") cmdSet(arg);
        else if (verb == "break" || verb == "b" || verb == "tbreak") cmdBreak(arg);
        else if (verb == "delete" || verb == "d") cmdDelete(arg);
        else if (verb == "run" || verb == "r") {
            if (!alive_) line("The program is not being run.");
            else {
                line("Starting program: " + m_.programName());
                started_ = true;
                // The entry is a function's first packet: its frame is pushed by the first track().
                track(m_.pc());
                if (const Breakpoint *b = breakAt(m_.pc())) stopped(("\nBreakpoint " + std::to_string(b->id) + ", ").c_str(), true);
                else go(Continue);
            }
        }
        else if (verb == "continue" || verb == "c") {
            if (!alive_ || !started_) line("The program is not being run.");
            else { line("Continuing."); go(Continue); }
        }
        else if (verb == "next" || verb == "n") { if (!alive_ || !started_) line("The program is not being run."); else go(Next); }
        else if (verb == "step" || verb == "s") { if (!alive_ || !started_) line("The program is not being run."); else go(Step); }
        else if (verb == "finish" || verb == "fin") {
            if (!alive_ || !started_) line("The program is not being run.");
            else if (frames_.size() <= 1) line("\"finish\" not meaningful in the outermost frame.");
            else { line("Run till exit from #0  " + place(m_.pc(), false)); go(Finish); }
        }
        else if (verb == "backtrace" || verb == "bt" || verb == "where") cmdBacktrace();
        else if (verb == "frame" || verb == "f") cmdFrame(arg);
        else if (verb == "print" || verb == "p") cmdPrint(arg, false);
        else if (verb == "whatis") cmdPrint(arg, true);
        else if (verb == "info") {
            if (arg == "args" || arg == "locals") cmdInfoVariables(arg == "args");
            else if (arg == "registers" || arg == "reg") cmdRegisters();
            else if (arg == "breakpoints" || arg == "break" || arg == "b") cmdInfoBreak();
            else if (arg == "line" || arg == "source") line(place(m_.pc(), false));
            else line("Undefined info command: \"" + arg + "\".  Try \"help info\".");
        }
        else line("Undefined command: \"" + verb + "\".  Try \"help\".");
        std::fflush(stdout);
    }
    std::fflush(stdout);
    return alive_ ? 0 : (m_.status() & 0xff);
}

}  // namespace

int shell(Machine &m) { return Session(m).run(); }

namespace {

// The text of an attribute - DW_AT_name("n") gives n, DW_AT_low_pc(greet) greet - or empty.
std::string attribute(const std::string &line, const char *key) {
    size_t at = line.find(key);
    if (at == std::string::npos) return std::string();
    at += std::strlen(key);
    if (at < line.size() && line[at] == '"') {
        size_t end = line.find('"', at + 1);
        return end == std::string::npos ? std::string() : line.substr(at + 1, end - at - 1);
    }
    size_t end = line.find(')', at);
    return end == std::string::npos ? std::string() : trim(line.substr(at, end - at));
}

}  // namespace

// The lines the compilers write, as the assembler kept them - spaced as written, or rebuilt from asm6x's
// tokens with no space after a comma:
//   .dwtag DW_TAG_subprogram, DW_AT_name("greet"), DW_AT_low_pc(greet)
//   .dwtag DW_TAG_formal_parameter, DW_AT_name("n"), DW_AT_type("int"), DW_AT_location[DW_OP_breg15 -44]
//   .dwtag DW_TAG_variable, ..., DW_AT_location[DW_OP_addr L.s.1]
//   .dwendtag
// DW_OP_breg15 is A15 (TI numbers A0-A15 0-15); a location in another register is passed over.
void readTags(const std::string &text, std::map<std::string, std::vector<Variable> > &byLabel) {
    std::string label;
    size_t from = 0;
    while (from < text.size()) {
        size_t nl = text.find('\n', from);
        std::string line = trim(text.substr(from, nl == std::string::npos ? std::string::npos : nl - from));
        from = nl == std::string::npos ? text.size() : nl + 1;
        if (line.compare(0, 9, ".dwendtag") == 0) { label.clear(); continue; }
        if (line.compare(0, 6, ".dwtag") != 0) continue;
        if (line.find("DW_TAG_subprogram") != std::string::npos) {
            label = attribute(line, "DW_AT_low_pc(");
            if (!label.empty()) byLabel[label];
            continue;
        }
        bool param = line.find("DW_TAG_formal_parameter") != std::string::npos;
        if (label.empty() || (!param && line.find("DW_TAG_variable") == std::string::npos)) continue;
        Variable v;
        v.name = attribute(line, "DW_AT_name(");
        v.type = attribute(line, "DW_AT_type(");
        v.param = param;
        size_t loc = line.find("DW_AT_location[");
        if (v.name.empty() || loc == std::string::npos) continue;
        size_t end = line.find(']', loc);
        std::string where = line.substr(loc + 15, end == std::string::npos ? std::string::npos : end - loc - 15);
        v.deref = where.find("DW_OP_deref") != std::string::npos;
        size_t breg = where.find("DW_OP_breg"), addr = where.find("DW_OP_addr");
        if (breg != std::string::npos) {
            const char *p = where.c_str() + breg + 10;
            char *after = nullptr;
            long reg = std::strtol(p, &after, 10);
            if (reg != 15 || after == p) continue;
            v.offset = static_cast<int>(std::strtol(after, nullptr, 10));
        } else if (addr != std::string::npos) {
            std::string s = trim(where.substr(addr + 10));
            size_t stop = s.find_first_of(", \t");
            v.symbol = stop == std::string::npos ? s : s.substr(0, stop);
            v.global = !v.symbol.empty();
            if (!v.global) continue;
        } else continue;
        byLabel[label].push_back(v);
    }
}

}  // namespace dbg
