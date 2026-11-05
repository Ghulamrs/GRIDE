// X86.cpp - x86-64 MASM code generation.
#include "X86.hpp"
#include "Literal.hpp"
#include "Mangle.hpp"
#include "Optimizer.hpp"
#include "PathUtil.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <sstream>

namespace cppparse {

namespace {

// the integer registers the allocator hands out: first the callee-saved ones (a function saves those it uses),
// then the volatile r8..r11, which no instruction sequence uses as a scratch register except around a call or
// a throw: a value that lives across none of those can sit there without being saved
// (rcx, rdx and rax are never handed out by the allocator: the coalescing step after it gives a parameter its
// incoming rcx / rdx and a returned value rax when nothing writes them meanwhile)
const char* const kIntRegs[] = { "rbx", "rsi", "rdi", "r12", "r13", "r14", "r15", "r8", "r9", "r10", "r11", "rcx", "rdx", "rax" };
const int kIntRegCount = 7;          // callee-saved: indexes 0..6
const int kVolatileRegBase = 7;      // volatile: indexes 7..10
const int kVolatileRegCount = 4;
const int kRcx = 11, kRdx = 12, kRax = 13;
const int kArgReg[4] = { kRcx, kRdx, kVolatileRegBase, kVolatileRegBase + 1 };   // the first four integer arguments: rcx, rdx, r8, r9
const int kFloatRegBase = 6;     // xmm6 .. xmm13, callee-saved: indexes 0..7
const int kFloatRegCount = 8;
const int kFloatVolatile = 8;    // xmm3 and xmm2 (indexes 8, 9), volatile: a value that no call, throw or try operation interrupts
const int kFloatVolatile2 = 9;   // (xmm0, xmm1 are the emitter's operand scratch, xmm4 the stack argument's and the float argument
                                 // cycle's, xmm5 the spill's and the second float operand's)
std::string fregName(int r) { return r == kFloatVolatile ? std::string("xmm3") : r == kFloatVolatile2 ? std::string("xmm2") : "xmm" + intToString(kFloatRegBase + r); }
const int kTryFrameSize = 272;   // sizeof(jmp_buf) on x64 (256) plus the frame link


std::string reg32(const std::string& r)
{
    if (r == "rax") return "eax"; if (r == "rbx") return "ebx"; if (r == "rcx") return "ecx"; if (r == "rdx") return "edx";
    if (r == "rsi") return "esi"; if (r == "rdi") return "edi"; if (r == "r8") return "r8d"; if (r == "r9") return "r9d";
    if (r == "r10") return "r10d"; if (r == "r11") return "r11d";
    return r + "d";   // r12..r15
}

std::string reg16(const std::string& r)
{
    if (r == "rax") return "ax"; if (r == "rbx") return "bx"; if (r == "rcx") return "cx"; if (r == "rdx") return "dx";
    if (r == "rsi") return "si"; if (r == "rdi") return "di";
    return r + "w";   // r8w ... r15w
}

std::string reg8(const std::string& r)
{
    if (r == "rax") return "al"; if (r == "rbx") return "bl"; if (r == "rcx") return "cl"; if (r == "rdx") return "dl";
    if (r == "rsi") return "sil"; if (r == "rdi") return "dil";
    return r + "b";   // r8b ... r15b
}

std::string regSized(const std::string& r, int size)
{
    if (size == 1) return reg8(r);
    if (size == 2) return reg16(r);
    if (size == 4) return reg32(r);
    return r;
}

// the opposite condition: an exact complement on the flags (unordered floating-point compares included)
std::string invertCc(const std::string& cc)
{
    if (cc == "e") return "ne";   if (cc == "ne") return "e";
    if (cc == "fe") return "fne"; if (cc == "fne") return "fe";   // floating ==, != : the parity flag decides unordered
    if (cc == "l") return "ge";   if (cc == "ge") return "l";
    if (cc == "le") return "g";   if (cc == "g") return "le";
    if (cc == "b") return "ae";   if (cc == "ae") return "b";
    if (cc == "be") return "a";   if (cc == "a") return "be";
    return cc == "s" ? "ns" : "s";
}

const char* ptrWord(int size)
{
    if (size == 1) return "byte ptr";
    if (size == 2) return "word ptr";
    if (size == 4) return "dword ptr";
    return "qword ptr";
}

// new- and delete-expressions call the global allocation functions by cl's names, which the C++ library defines and
// a program may replace (in any unit): operator new, delete (also sized, as cl calls it), new[], delete[] (also sized)
std::string runtimeName(const std::string& sub)
{
    if (sub == "__new") return "??2@YAPEAX_K@Z";
    if (sub == "__new_array") return "??_U@YAPEAX_K@Z";
    if (sub == "__delete") return "??3@YAXPEAX@Z";
    if (sub == "__delete_sized") return "??3@YAXPEAX_K@Z";
    if (sub == "__delete_array") return "??_V@YAXPEAX@Z";
    if (sub == "__delete_array_sized") return "??_V@YAXPEAX_K@Z";
    if (sub == "__malloc") return "malloc";
    if (sub == "__copy") return "memcpy";
    if (sub == "__zero") return "memset";
    if (sub == "__throw") return "cppparse_throw";
    return sub;
}

// a definition of a global allocation function replaces the library's: it gets cl's name in either mangling
std::string replacedAllocationName(const std::string& irName)
{
    if (irName == "operator new(unsigned long long)") return runtimeName("__new");
    if (irName == "operator new[](unsigned long long)") return runtimeName("__new_array");
    if (irName == "operator delete(void*)") return runtimeName("__delete");
    if (irName == "operator delete(void*, unsigned long long)") return runtimeName("__delete_sized");
    if (irName == "operator delete[](void*)") return runtimeName("__delete_array");
    if (irName == "operator delete[](void*, unsigned long long)") return runtimeName("__delete_array_sized");
    return std::string();
}

// the name a debugger shows for a function (its CodeView symbol): the IR name without the parameter list and the
// qualifiers after it ("geom::Circle::area() const" -> "geom::Circle::area", "S::operator()(int)" -> "S::operator()")
std::string displayName(const std::string& irName)
{
    size_t p = irName.find('(');
    if (p != std::string::npos && p >= 8 && irName.compare(p - 8, 8, "operator") == 0 && p + 1 < irName.size() && irName[p + 1] == ')') p = irName.find('(', p + 2);
    return p == std::string::npos ? irName : irName.substr(0, p);
}

} // namespace

std::string X86Module::symbol(const std::string& irName)
{
    if (startsWith(irName, "__")) return runtimeName(irName);                                  // __new, __throw, ...
    if (endsWith(irName, "?")) return lastComponent(irName.substr(0, irName.size() - 1));       // unknown library function: its C name
    if (irName.find('(') == std::string::npos) return irName;                                  // extern "C": plain name
    if (startsWith(irName, "operator ")) {
        const std::string replaced = replacedAllocationName(irName);
        if (!replaced.empty()) return replaced;
    }
    if (m_msvc) {
        std::map<std::string, std::string>::const_iterator it = m_functionNames.find(irName);
        if (it != m_functionNames.end()) return it->second;
    }
    return mangle(irName);
}

std::string X86Module::globalSymbol(const std::string& name)
{
    // first: a static local's IR name ("f(int)::n#0") contains its function's parentheses
    if (m_msvc) {
        std::map<std::string, std::string>::const_iterator it = m_globalNames.find(name);
        if (it != m_globalNames.end()) return it->second;
    }
    if (name.find('(') != std::string::npos || m_cFunctions.count(name)) return symbol(name);   // address of a function (an extern "C" one has no parameter list)
    if (startsWith(name, "vtable@")) return m_msvc ? msvcMangleVtable(name.substr(7)) : "vtable_" + mangle(name.substr(7));
    return "g_" + mangle(name);
}

void X86Module::registerSymbolName(const std::string& irName, const std::string& decorated)
{
    if (!m_functionNames.count(irName)) m_functionNames[irName] = decorated;
}

void X86Module::registerGlobalName(const std::string& irGlobalName, const std::string& decorated)
{
    if (!m_globalNames.count(irGlobalName)) m_globalNames[irGlobalName] = decorated;
}

void X86Module::addComdatInitializer(const std::string& irName, const std::string& entrySymbol, const std::string& globalName)
{
    ComdatInit c;
    c.irName = irName; c.entry = entrySymbol; c.global = globalName;
    m_comdatInitializers.push_back(c);
}

namespace {

} // namespace

// ---- per function --------------------------------------------------------------------

class X86Function {
public:
    X86Function(X86Module& m, const IrFunction& f, LowerStats& stats) : m_m(m), m_f(f), m_stats(stats), m_frameSize(0), m_throwSlot(0), m_outgoingArgs(0), m_prefixEnd(0), m_frameless(false), m_firstSpills(0), m_tableEh(false), m_tableRegs(false), m_openRegion(-1), m_parityLabels(0), m_lastLine(-1), m_hotCount(0), m_openEnd(false)
    { for (int p = 0; p < 4; ++p) m_lastParamLoad[p] = -1; }
    std::string run();

private:
    // crosses: a call, throw or try operation destroys the volatile registers while the value is live; callResult: the
    // call at start defines the value, after it read its arguments; liveOutEnd: live out of the block that ends at end
    // (all three from computeIntervals). fixed: the register was settled by coalescing; a second allocation keeps it
    // and works around it. weight: the definitions and uses, each counted 1, 10, 100 or 1000 by the loop depth of its
    // block; with the length it decides which value a spill takes when no register is free
    struct Interval { int temp; int start, end; bool isFloat; int reg; int slot; bool crosses, callResult, liveOutEnd; bool fixed; int weight; Interval() : temp(-1), start(1 << 30), end(-1), isFloat(false), reg(-1), slot(0), crosses(false), callResult(false), liveOutEnd(false), fixed(false), weight(0) {} };
    struct ByStart { bool operator()(const Interval* a, const Interval* b) const { return a->start != b->start ? a->start < b->start : a->temp < b->temp; } };
    // a local or a spilled value in the frame, with its uses per 8 bytes: the frame is laid out in that order
    struct FrameSlot { int local, temp, size, align; long long density; };
    struct ByDensity { bool operator()(const FrameSlot& a, const FrameSlot& b) const { return a.density > b.density; } };
    static long long density(const Interval& v) { return static_cast<long long>(v.weight) * 1024 / (v.end >= v.start ? v.end - v.start + 1 : 1); }   // weight per instruction of the life

    void layoutBlocks();
    void computeIntervals();
    const IrInst& instAt(size_t pos) const { const std::pair<int, int>& l = m_linear[pos]; return m_f.blocks[static_cast<size_t>(l.first)].insts[static_cast<size_t>(l.second)]; }
    void allocate();
    void layoutFrame();
    void emitBody();
    void emitInst(const IrInst& in, int blockIndex);
    void emitCall(const IrInst& in);
    void extend(const std::string& rd, const Type* t);

    bool isFloatType(const Type* t) const { return t && (t->kind == Type::Float || t->kind == Type::Double); }
    bool tempIsFloat(int t) const { return isFloatType(m_f.typeOf(t)); }
    bool opIsFloat(const IrOperand& o) const { return o.kind == IrOperand::FImm || (o.kind == IrOperand::Temp && tempIsFloat(o.id)) || (o.type && isFloatType(o.type) && o.kind != IrOperand::Imm); }
    int  typeSize(const Type* t) const { if (!t) return 8; if (t->kind == Type::Enum || t->kind == Type::Bool) return t->kind == Type::Bool ? 1 : 4; int n = t->builtinSize(); return n > 0 ? n : 8; }
    bool isSingle(const Type* t) const { return t && t->kind == Type::Float; }

    std::string use(const IrOperand& o, const std::string& scratch);     // materialise into a register name
    std::string memOperand(const IrOperand& addr);                        // "[reg]", or "[base+offset]" for a folded address
    // can this operand be an instruction's immediate? (a 32-bit constant; an unsigned 32-bit one with the top bit
    // set would be sign-extended in a 64-bit operation, so it stays in a register there)
    bool immediateOperand(const IrOperand& o, int opSize) const
    {
        if (o.kind != IrOperand::Imm) return false;
        const bool u32 = o.type && o.type->isUnsigned && typeSize(o.type) == 4;
        return !(u32 && static_cast<int>(o.ival) < 0 && opSize == 8);
    }
    // an addrindex whose constant index is the instruction's offset (a huge or an unsigned negative index, which is
    // zero-extended, goes through a register)
    bool indexImmediate(const IrInst& in) const
    {
        const IrOperand& i = in.args[1];
        const long size = in.extra > 0 ? in.extra : 1;
        return i.kind == IrOperand::Imm && size <= 1024 && i.ival >= -(1L << 20) && i.ival <= (1L << 20) && !(i.ival < 0 && i.type && i.type->isUnsigned);
    }
    std::string def(int temp, bool isFloat, const std::string& scratch, int& spill);
    void        finish(int temp, int spill, bool isFloat, const std::string& reg);
    // a negative offset is a parameter in its home slot above the return address (a variadic function's)
    std::string localRef(int local) const
    {
        const int off = m_localOffset[static_cast<size_t>(local)];
        return off < 0 ? "[rbp+" + intToString(-off) + "]" : "[rbp-" + intToString(off) + "]";
    }
    std::string label(int b) const { return m_label + "_B" + intToString(b); }
    std::string catchType(int hb) const;
    void        line(const std::string& s) { m_out << "        " << s << "\n"; ++m_stats.instructions; m_openEnd = s.compare(0, 5, "call ") == 0; }
    void        directive(const std::string& s) { m_out << "        " << s << "\n"; }   // for the assembler, not an instruction
    void        comment(const std::string& s) { m_out << "        ; " << s << "\n"; }
    std::string floatConst(double v);

    X86Module&          m_m;
    const IrFunction&   m_f;
    LowerStats&         m_stats;
    std::ostringstream  m_out;
    std::string         m_label;
    std::vector<std::pair<int, int> > m_linear;
    std::vector<int>    m_blockStart, m_blockEnd;
    // the blocks in the order the code lays them out (the intervals keep the blocks' own order): the entry's group, the
    // blocks its branches reach, in chains, then the landing pads and handlers, each with what it alone reaches, so that
    // one target of every branch follows it where the order allows and a branch over a pad falls through
    std::vector<int>    m_order;
    std::vector<bool>   m_emitted;        // the blocks the layout placed: the others nothing reaches
    std::vector<int>    m_next;           // block -> the block laid out after it, -1 for the last
    size_t              m_hotCount;       // the entry's group: the exit code follows its last block, the cold groups come after
    std::map<int, Interval> m_intervals;
    std::vector<int>    m_calls;
    std::vector<int>    m_localOffset;
    std::vector<int>    m_localWeight;    // a local's loads, stores and address-takings, 1, 10, 100 or 1000 each by loop depth
    int                 m_frameSize;
    std::map<long, int> m_tryOffset;      // version 5: try region id -> [rbp-offset] of its setjmp frame
    int                 m_throwSlot;      // [rbp-offset] where a thrown scalar is stored before the call
    std::set<int>       m_usedIntRegs, m_usedFloatRegs;
    std::map<int, int>  m_savedFloatOffset;   // xmm6..13 are stored in the frame (the integer ones are pushed)
    int                 m_outgoingArgs;
    std::vector<int>    m_clobbers;       // positions that destroy the volatile registers (calls, throws, try regions)
    // an address computed only for the next load or store (addrfield t = base + off; loadmem/storemem [t]): the
    // instruction uses [base+off] directly and t gets no register
    struct Folded { IrOperand base; long offset; };
    std::map<int, Folded> m_folded;
    // a comparison whose result only steers the next conditional branch: cmp + jcc, no setcc / test
    std::set<int>       m_fusedCmp;
    std::string         m_pendingCc;      // the condition code of the comparison just emitted for the branch
    // a 1-byte value (a bool, a char) that only steers the next conditional branch: the branch tests the byte where it
    // is (a call's result register, the local, the memory or the global it was loaded from, a parameter's register)
    // instead of a zero-extension into a 32-bit register and a test of that; such a load emits nothing
    struct ByteTest { int kind; int local; IrOperand mem; std::string name; ByteTest() : kind(0), local(-1) {} };   // kind 1: a call's result, 2: the local, 3: memory at mem, 4: the global name
    std::map<int, ByteTest> m_byteTest;
    bool byteInMemory(int t) const { std::map<int, ByteTest>::const_iterator b = m_byteTest.find(t); return b != m_byteTest.end() && b->second.kind != 1; }   // tested in memory: no register of its own
    int                 m_parityLabels;   // the local labels of the setcc sequences of floating ==, != (_P1, _P2...)
    int                 m_lastLine;       // the source line of the last ;@line marker written (-1: none yet)
    // a register parameter that is only read, by loads at the very start of the function (before anything can
    // overwrite its argument register), is used straight from that register and never stored to the frame
    void analyzeParams();
    std::vector<bool>   m_paramInReg;
    std::vector<bool>   m_paramUsed;
    int                 m_prefixEnd;      // instructions [0, m_prefixEnd) of block 0 are those loads
    int                 m_lastParamLoad[4];   // position of the last of them reading parameter p's register, -1 if none
    bool                m_frameless;      // no calls, nothing in memory, no saved registers: no frame at all
    void decideFrame();
    // after allocation: a parameter stays in its incoming register and a returned value is computed in rax when
    // nothing writes those registers meanwhile, which saves the copies
    void coalesce();
    void reallocateAroundCoalesced();
    bool canStayInArgReg(int t, int p) const;
    // after the allocation: a value whose last use is a call argument is computed in the argument's register when
    // nothing before the call writes it, which saves the copy before the call
    void hintArguments();
    bool holdsArgReg(const Interval& iv, int k) const;
    void resultsInRax();   // after it: a call's result read once, soon after the call, stays in rax
    void countSavedRegisters();   // the callee-saved registers in use, from the intervals
    bool inRegister(const IrOperand& o) const;
    std::string regName(const IrOperand& o) const;      // the integer register a temp lives in, "" when it is in memory or an xmm register
    std::string fregOf(const IrOperand& o) const;       // the xmm register a temp lives in, "" when it is in memory or an integer register
    bool writesArgReg(const IrInst& in, int p) const;   // may the code for 'in' overwrite parameter p's register?
    std::set<int>       m_returnDirect;   // temps computed straight into rax for the return that follows
    int                 m_firstSpills;    // spills counted by the first allocation (a second one replaces them)
    // table-based exceptions (--eh=table): a try region costs no code; the code it covers is listed as address
    // ranges in the function's unwind info, each with the landing pad (the dispatch stub of its innermost region)
    // where the runtime resumes this frame when an exception passes through a call in that range
    bool computeRegions();                       // false when the regions are not properly nested along every path
    // the exception edges of the table regions: a call or throw inside a region continues at the handlers of that
    // region and of every region around it, with each register as it was at the call; what a handler reads (and
    // what the code after it reads, a loop that goes round again) is live across every such call
    void computeEhEdges();
    std::vector<std::vector<int> > m_ehSuccs;    // block -> handler blocks its calls and throws may continue at
    void setRegions(const std::vector<long>& now);   // closes / opens a range where the innermost region changes
    struct EhRange { std::string begin, end; long region; };
    bool                m_tableEh;
    bool                m_tableRegs;             // table regions whose handlers take nothing in registers: all registers
    std::vector<std::vector<long> > m_regionIn;  // regions active on entry to each block, innermost last
    std::map<long, std::vector<long> > m_regionOuter;   // region -> the regions active at its trybegin
    std::map<long, std::string> m_padLabel;   // region -> the block its ranges name in place of a dispatch stub (an object's pad, catch (...) first)
    bool                m_openEnd;        // the code so far ends with a call (one that does not return, else a jump or ret would follow): int 3 closes the function
    std::vector<long>   m_regionNow;
    std::vector<EhRange> m_ranges;
    long                m_openRegion;            // innermost region of the open range, -1 when none is open
};

bool X86Function::computeRegions()
{
    const size_t n = m_f.blocks.size();
    m_regionIn.assign(n, std::vector<long>());
    std::vector<bool> seen(n, false);
    std::vector<int> work;
    bool ok = true;
    seen[0] = true;
    work.push_back(0);
    while (!work.empty() && ok) {
        const int b = work.back();
        work.pop_back();
        std::vector<long> cur = m_regionIn[static_cast<size_t>(b)];
        const IrBlock& blk = m_f.blocks[static_cast<size_t>(b)];
        for (size_t i = 0; i < blk.insts.size() && ok; ++i) {
            const IrInst& in = blk.insts[i];
            std::vector<int> targets;
            if (in.op == IrInst::TryBegin) {
                m_regionOuter[in.extra] = cur;
                for (size_t a = 0; a < in.args.size(); ++a) if (in.args[a].kind == IrOperand::Block) targets.push_back(in.args[a].id);
            } else if (in.op == IrInst::Br || in.op == IrInst::CondBr) {
                for (size_t a = 0; a < in.args.size(); ++a) if (in.args[a].kind == IrOperand::Block) targets.push_back(in.args[a].id);
            }
            // the handlers of a region run in the regions around it; a branch keeps the current ones
            for (size_t t = 0; t < targets.size(); ++t) {
                const size_t h = static_cast<size_t>(targets[t]);
                if (h >= n) { ok = false; break; }
                if (!seen[h]) { seen[h] = true; m_regionIn[h] = cur; work.push_back(targets[t]); }
                else if (m_regionIn[h] != cur) ok = false;
            }
            if (in.op == IrInst::TryBegin) cur.push_back(in.extra);
            else if (in.op == IrInst::TryEnd) {
                if (cur.empty() || cur.back() != in.extra) ok = false;
                else cur.pop_back();
            }
        }
    }
    return ok;
}

void X86Function::computeEhEdges()
{
    const size_t n = m_f.blocks.size();
    m_ehSuccs.assign(n, std::vector<int>());
    std::map<long, std::vector<int> > handlersOf;   // region -> its handler entries (the TryBegin's block operands)
    for (size_t b = 0; b < n; ++b)
        for (size_t i = 0; i < m_f.blocks[b].insts.size(); ++i) {
            const IrInst& in = m_f.blocks[b].insts[i];
            if (in.op != IrInst::TryBegin) continue;
            for (size_t a = 0; a < in.args.size(); ++a) if (in.args[a].kind == IrOperand::Block) handlersOf[in.extra].push_back(in.args[a].id);
        }
    for (size_t b = 0; b < n; ++b) {
        std::vector<long> cur = m_regionIn[b];
        std::vector<int>& out = m_ehSuccs[b];
        for (size_t i = 0; i < m_f.blocks[b].insts.size(); ++i) {
            const IrInst& in = m_f.blocks[b].insts[i];
            if (in.op == IrInst::TryBegin) { cur.push_back(in.extra); continue; }
            if (in.op == IrInst::TryEnd) { if (!cur.empty() && cur.back() == in.extra) cur.pop_back(); continue; }
            if (!in.mayThrow()) continue;   // a call to a function with C linkage reaches no handler (cl /EHsc)
            // a clause that does not match rethrows from the landing pad to the regions around: all of them count
            for (size_t r = 0; r < cur.size(); ++r) {
                const std::vector<int>& hs = handlersOf[cur[r]];
                for (size_t h = 0; h < hs.size(); ++h)
                    if (hs[h] >= 0 && static_cast<size_t>(hs[h]) < n && std::find(out.begin(), out.end(), hs[h]) == out.end()) out.push_back(hs[h]);
            }
        }
    }
}

void X86Function::setRegions(const std::vector<long>& now)
{
    m_regionNow = now;
    const long inner = now.empty() ? -1 : now.back();
    if (inner == m_openRegion) return;
    if (m_openRegion >= 0) {
        m_ranges.back().end = m_label + "_R" + intToString(static_cast<int>(m_ranges.size())) + "e";
        m_out << m_ranges.back().end << ":\n";
    }
    m_openRegion = inner;
    if (inner >= 0) {
        EhRange r;
        r.begin = m_label + "_R" + intToString(static_cast<int>(m_ranges.size()) + 1) + "b";
        r.region = inner;
        m_ranges.push_back(r);
        m_out << r.begin << ":\n";
    }
}

void X86Function::layoutBlocks()
{
    // group by group, each a root and the blocks its branches reach that no earlier group took, the roots in order of
    // their numbers: the entry's group is the ordinary code; a landing pad (a block only an exception enters) roots a
    // group of its own, placed after the code of the region it serves, as are the handlers and whatever they alone
    // reach. A block no branch and no region of a placed block reaches (the code after a return, a switch's end that
    // no case breaks to: IrGen leaves them at -O0) is not placed, so not emitted
    const size_t n = m_f.blocks.size();
    std::vector<bool> placed(n, false), reachable(n, false);
    m_order.clear();
    if (n > 0) reachable[0] = true;
    for (size_t root = 0; root < n; ++root) {
        if (placed[root] || !reachable[root]) continue;
        std::vector<bool> inGroup(n, false);
        std::vector<int> work;
        inGroup[root] = true;
        work.push_back(static_cast<int>(root));
        while (!work.empty()) {
            const std::vector<IrInst>& insts = m_f.blocks[static_cast<size_t>(work.back())].insts;
            work.pop_back();
            for (size_t i = 0; i < insts.size(); ++i) {
                const IrInst& in = insts[i];
                if (in.op != IrInst::Br && in.op != IrInst::CondBr) continue;   // (a try region's handlers are reached by exceptions only)
                for (size_t a = 0; a < in.args.size(); ++a) {
                    const int t = in.args[a].id;
                    if (in.args[a].kind == IrOperand::Block && t >= 0 && static_cast<size_t>(t) < n && !placed[static_cast<size_t>(t)] && !inGroup[static_cast<size_t>(t)]) { inGroup[static_cast<size_t>(t)] = true; work.push_back(t); }
                }
            }
        }
        // the group in chains: after a block, one of its branch targets still to place, by preference the one that only
        // this block enters (the next operand of && or ||, a then-block: a join point would stand between a branch and
        // its other target), then the one IrGen made right after this block, then the true target before the false;
        // a chain that ends continues with the lowest-numbered block of the group still to place
        int cur = static_cast<int>(root);
        while (cur >= 0) {
            placed[static_cast<size_t>(cur)] = true;
            m_order.push_back(cur);
            int next = -1, bestRank = 9;
            const std::vector<IrInst>& insts = m_f.blocks[static_cast<size_t>(cur)].insts;
            for (size_t i = 0; i < insts.size(); ++i) {
                const IrInst& in = insts[i];
                if (in.op != IrInst::Br && in.op != IrInst::CondBr) continue;
                for (size_t a = 0; a < in.args.size(); ++a) {
                    const int t = in.args[a].id;
                    if (in.args[a].kind != IrOperand::Block || t < 0 || static_cast<size_t>(t) >= n || !inGroup[static_cast<size_t>(t)] || placed[static_cast<size_t>(t)]) continue;
                    const int rank = (m_f.blocks[static_cast<size_t>(t)].preds.size() == 1 ? 0 : 4) + (t == cur + 1 ? 0 : 2) + (next < 0 ? 0 : 1);
                    if (rank < bestRank) { bestRank = rank; next = t; }
                }
            }
            if (next < 0) for (size_t b = 0; b < n && next < 0; ++b) if (inGroup[b] && !placed[b]) next = static_cast<int>(b);
            cur = next;
        }
        if (root == 0) m_hotCount = m_order.size();
        // the handlers of the regions the group opens root groups of their own (a handler may bear a lower number than
        // this root: the scan goes back to it)
        size_t lowest = n;
        for (size_t k = 0; k < m_order.size(); ++k) {
            const std::vector<IrInst>& insts = m_f.blocks[static_cast<size_t>(m_order[k])].insts;
            for (size_t i = 0; i < insts.size(); ++i) {
                if (insts[i].op != IrInst::TryBegin) continue;
                for (size_t a = 0; a < insts[i].args.size(); ++a) {
                    const int t = insts[i].args[a].id;
                    if (insts[i].args[a].kind != IrOperand::Block || t < 0 || static_cast<size_t>(t) >= n || reachable[static_cast<size_t>(t)]) continue;
                    reachable[static_cast<size_t>(t)] = true;
                    if (static_cast<size_t>(t) < lowest) lowest = static_cast<size_t>(t);
                }
            }
        }
        if (lowest < root) root = lowest - 1;
    }
    m_emitted = placed;
    m_next.assign(n, -1);
    for (size_t k = 0; k + 1 < m_order.size(); ++k) m_next[static_cast<size_t>(m_order[k])] = m_order[k + 1];
}

void X86Function::computeIntervals()
{
    m_blockStart.assign(m_f.blocks.size(), 0);
    m_blockEnd.assign(m_f.blocks.size(), 0);
    int k = 0;
    // the linear order of the intervals is the blocks' own: a loop is a backward edge there (the layout below is for the
    // emission alone; a register assignment made for one linear order holds for any order of the same blocks, since an
    // interval covers every point its value is live at, whichever block comes next in the code)
    for (size_t b = 0; b < m_f.blocks.size(); ++b) {
        m_blockStart[b] = k;
        for (size_t i = 0; i < m_f.blocks[b].insts.size(); ++i) { m_linear.push_back(std::make_pair(static_cast<int>(b), static_cast<int>(i))); ++k; }
        m_blockEnd[b] = k - 1;
    }
    m_outgoingArgs = 0;
    // a definition or use counts 1, 10, 100 or 1000 towards its value's weight by the loop depth of its block
    std::vector<int> blockWeight(m_f.blocks.size(), 1);
    {
        std::vector<LoopInfo> loops;
        Optimizer::naturalLoops(m_f, loops);
        std::vector<int> depth(m_f.blocks.size(), 0);
        for (size_t L = 0; L < loops.size(); ++L)
            for (size_t lb = 0; lb < loops[L].blocks.size(); ++lb) if (loops[L].blocks[lb] >= 0 && static_cast<size_t>(loops[L].blocks[lb]) < depth.size()) ++depth[static_cast<size_t>(loops[L].blocks[lb])];
        for (size_t b = 0; b < depth.size(); ++b) blockWeight[b] = depth[b] >= 3 ? 1000 : depth[b] == 2 ? 100 : depth[b] == 1 ? 10 : 1;
    }
    // addresses used only by the next instruction's load or store are folded into its memory operand
    std::map<int, int> useCount;
    for (size_t b = 0; b < m_f.blocks.size(); ++b)
        for (size_t i = 0; i < m_f.blocks[b].insts.size(); ++i)
            for (size_t a = 0; a < m_f.blocks[b].insts[i].args.size(); ++a)
                if (m_f.blocks[b].insts[i].args[a].kind == IrOperand::Temp) ++useCount[m_f.blocks[b].insts[i].args[a].id];
    for (size_t b = 0; b < m_f.blocks.size(); ++b)
        for (size_t i = 0; i + 1 < m_f.blocks[b].insts.size(); ++i) {
            const IrInst& in = m_f.blocks[b].insts[i];
            const IrInst& next = m_f.blocks[b].insts[i + 1];
            if (in.op != IrInst::AddrField || in.dst < 0 || in.args.empty() || useCount[in.dst] != 1) continue;
            if (next.args.empty() || next.args[0].kind != IrOperand::Temp || next.args[0].id != in.dst) continue;
            if (next.op != IrInst::LoadMem && !(next.op == IrInst::StoreMem && next.args.size() == 2)) continue;
            Folded f; f.base = in.args[0]; f.offset = in.extra;
            m_folded[in.dst] = f;
        }
    for (size_t b = 0; b < m_f.blocks.size(); ++b)
        for (size_t i = 0; i + 1 < m_f.blocks[b].insts.size(); ++i) {
            const IrInst& in = m_f.blocks[b].insts[i];
            const IrInst& next = m_f.blocks[b].insts[i + 1];
            if (in.op == IrInst::Cmp && in.dst >= 0 && useCount[in.dst] == 1 && next.op == IrInst::CondBr &&
                !next.args.empty() && next.args[0].kind == IrOperand::Temp && next.args[0].id == in.dst) m_fusedCmp.insert(in.dst);
        }
    for (size_t b = 0; b < m_f.blocks.size(); ++b)
        for (size_t i = 0; i + 1 < m_f.blocks[b].insts.size(); ++i) {
            const IrInst& in = m_f.blocks[b].insts[i];
            const IrInst& next = m_f.blocks[b].insts[i + 1];
            if (in.dst < 0 || !in.type || typeSize(in.type) != 1 || useCount[in.dst] != 1 || next.op != IrInst::CondBr ||
                next.args.empty() || next.args[0].kind != IrOperand::Temp || next.args[0].id != in.dst) continue;
            ByteTest bt;
            if (in.op == IrInst::Call || in.op == IrInst::VCall) bt.kind = 1;
            else if (in.args.size() != 1) continue;
            else if (in.op == IrInst::LoadLocal && in.args[0].kind == IrOperand::Local) { bt.kind = 2; bt.local = in.args[0].id; }
            else if (in.op == IrInst::LoadMem && in.args[0].kind == IrOperand::Temp) { bt.kind = 3; bt.mem = in.args[0]; }
            else if (in.op == IrInst::LoadGlobal && in.args[0].kind == IrOperand::Global) { bt.kind = 4; bt.name = in.args[0].name; }
            if (bt.kind) m_byteTest[in.dst] = bt;
        }
    m_localWeight.assign(m_f.locals.size(), 1);
    std::map<int, int> firstDef;                       // temp -> linear position of its first definition
    std::vector<std::pair<int, int> > uses;             // (temp, position)
    for (size_t p = 0; p < m_linear.size(); ++p) {
        const IrInst& in = m_f.blocks[static_cast<size_t>(m_linear[p].first)].insts[static_cast<size_t>(m_linear[p].second)];
        int pos = static_cast<int>(p);
        if (in.dst >= 0 && !m_folded.count(in.dst) && !m_fusedCmp.count(in.dst) && !byteInMemory(in.dst)) { Interval& iv = m_intervals[in.dst]; iv.temp = in.dst; iv.isFloat = isFloatType(in.type); if (pos < iv.start) iv.start = pos; if (pos > iv.end) iv.end = pos; if (!firstDef.count(in.dst)) firstDef[in.dst] = pos; iv.weight += blockWeight[static_cast<size_t>(m_linear[p].first)]; }
        for (size_t a = 0; a < in.args.size(); ++a) {
            if (in.args[a].kind == IrOperand::Local && in.args[a].id >= 0 && static_cast<size_t>(in.args[a].id) < m_localWeight.size()) m_localWeight[static_cast<size_t>(in.args[a].id)] += blockWeight[static_cast<size_t>(m_linear[p].first)];
            if (in.args[a].kind != IrOperand::Temp) continue;
            int t = in.args[a].id;
            const IrOperand* op = &in.args[a];
            if (m_fusedCmp.count(t)) continue;           // the branch reads the flags, not a register
            if (byteInMemory(t)) {                       // the branch reads the byte in memory: for a load from an address, that address
                const ByteTest& bt = m_byteTest[t];
                if (bt.kind != 3) continue;
                op = &bt.mem;
                t = op->id;
            }
            if (m_folded.count(t)) {                    // a folded address: its base is what this instruction reads
                op = &m_folded[t].base;
                if (op->kind != IrOperand::Temp) continue;
                t = op->id;
            }
            Interval& iv = m_intervals[t];
            iv.temp = t;
            if (iv.start > pos) iv.start = pos;
            if (pos > iv.end) iv.end = pos;
            if (op->type && isFloatType(op->type)) iv.isFloat = true;
            iv.weight += blockWeight[static_cast<size_t>(m_linear[p].first)];
            uses.push_back(std::make_pair(t, pos));
        }
        if (in.op == IrInst::Call || in.op == IrInst::VCall) {
            m_calls.push_back(pos);
            int n = static_cast<int>(in.args.size());
            if (n - 4 > m_outgoingArgs) m_outgoingArgs = n - 4;
        }
        // (a table region's trybegin / tryend are no code at all)
        if (in.op == IrInst::Call || in.op == IrInst::VCall || in.op == IrInst::Throw || in.op == IrInst::CatchParam || in.op == IrInst::CatchEnd ||
            ((in.op == IrInst::TryBegin || in.op == IrInst::TryEnd) && !m_tableEh)) m_clobbers.push_back(pos);
    }
    // a use that precedes every definition in linear order is reached through a backward edge: the value is
    // live from the start of the using block (after SSA, copies of a phi's operands create such uses)
    for (size_t u = 0; u < uses.size(); ++u) {
        std::map<int, int>::const_iterator d = firstDef.find(uses[u].first);
        if (d == firstDef.end() || uses[u].second >= d->second) continue;
        int blockStart = m_blockStart[static_cast<size_t>(m_linear[static_cast<size_t>(uses[u].second)].first)];
        Interval& iv = m_intervals[uses[u].first];
        if (blockStart < iv.start) iv.start = blockStart;
    }
    // an interval spans every block the value is live through: a loop header that only passes a value on (its
    // copies sit in edge blocks laid out after the loop) must not give the value's register to another one
    // (with table regions, the exception edges count: a value a landing pad's handler needs stays in its place
    // across every call that may throw to it)
    std::vector<std::set<int> > liveIn, liveOut;
    m_f.computeLiveness(liveIn, liveOut, m_tableEh ? &m_ehSuccs : 0);
    for (size_t b = 0; b < m_f.blocks.size(); ++b) {
        for (std::set<int>::const_iterator t = liveIn[b].begin(); t != liveIn[b].end(); ++t) {
            std::map<int, Interval>::iterator it = m_intervals.find(*t);
            if (it != m_intervals.end() && m_blockStart[b] < it->second.start) it->second.start = m_blockStart[b];
        }
        for (std::set<int>::const_iterator t = liveOut[b].begin(); t != liveOut[b].end(); ++t) {
            std::map<int, Interval>::iterator it = m_intervals.find(*t);
            if (it != m_intervals.end() && m_blockEnd[b] > it->second.end) it->second.end = m_blockEnd[b];
        }
    }
    for (std::map<int, Interval>::iterator it = m_intervals.begin(); it != m_intervals.end(); ++it)
        if (!it->second.isFloat) it->second.isFloat = tempIsFloat(it->first);
    for (size_t b = 0; b < m_f.blocks.size(); ++b) {
        std::vector<int> succs = m_f.blocks[b].succs;
        if (m_tableEh && b < m_ehSuccs.size()) succs.insert(succs.end(), m_ehSuccs[b].begin(), m_ehSuccs[b].end());
        for (size_t s = 0; s < succs.size(); ++s) {
            int target = succs[s];
            if (target < 0 || static_cast<size_t>(target) >= m_f.blocks.size()) continue;
            if (m_blockStart[static_cast<size_t>(target)] > m_blockStart[b]) continue;
            int loopStart = m_blockStart[static_cast<size_t>(target)], loopEnd = m_blockEnd[b];
            for (std::map<int, Interval>::iterator it = m_intervals.begin(); it != m_intervals.end(); ++it) {
                Interval& iv = it->second;
                if (iv.start < loopStart && iv.end >= loopStart && iv.end < loopEnd) iv.end = loopEnd;
                if (iv.start >= loopStart && iv.start <= loopEnd && iv.end < loopEnd && iv.start > iv.end) iv.end = loopEnd;
            }
        }
    }
    // a clobber inside an interval (a call, a throw, a try operation: the code destroys the volatile registers)
    // keeps the value out of them. A call that only reads the value (an argument at its last use) or only defines
    // it (its result) does not: the value is not live while the call runs, and emitCall stages the arguments so
    // that none is overwritten before it is read
    std::vector<bool> isCall(m_linear.size(), false);
    for (size_t c = 0; c < m_calls.size(); ++c) isCall[static_cast<size_t>(m_calls[c])] = true;
    for (std::map<int, Interval>::iterator it = m_intervals.begin(); it != m_intervals.end(); ++it) {
        Interval& iv = it->second;
        iv.callResult = isCall[static_cast<size_t>(iv.start)] && instAt(static_cast<size_t>(iv.start)).dst == iv.temp;
        const size_t endBlock = static_cast<size_t>(m_linear[static_cast<size_t>(iv.end)].first);
        iv.liveOutEnd = iv.end == m_blockEnd[endBlock] && liveOut[endBlock].count(iv.temp) > 0;
        for (size_t c = 0; c < m_clobbers.size() && !iv.crosses; ++c) {
            const int pos = m_clobbers[c];
            if (pos < iv.start || pos > iv.end) continue;
            if (!isCall[static_cast<size_t>(pos)]) { iv.crosses = true; continue; }
            const bool liveBefore = pos > iv.start || !iv.callResult;
            const bool liveAfter = pos < iv.end || iv.liveOutEnd;
            iv.crosses = liveBefore && liveAfter;
        }
    }
}

void X86Function::analyzeParams()
{
    const size_t n = static_cast<size_t>(m_f.paramCount);
    m_paramInReg.assign(n, false);
    m_paramUsed.assign(n, false);
    m_prefixEnd = 0;
    if (m_f.blocks.empty()) return;
    const std::vector<IrInst>& entry = m_f.blocks[0].insts;
    // the leading loads of parameters: nothing before them can overwrite an argument register
    while (static_cast<size_t>(m_prefixEnd) < entry.size()) {
        const IrInst& in = entry[static_cast<size_t>(m_prefixEnd)];
        if (in.op != IrInst::LoadLocal || in.args.empty() || in.args[0].kind != IrOperand::Local || in.args[0].id >= m_f.paramCount) break;
        ++m_prefixEnd;
    }
    for (size_t p = 0; p < n && p < 4; ++p) {
        const IrLocal& l = m_f.locals[p];
        const Type* t = l.type;
        const bool scalar = t && t->kind != Type::Class && t->kind != Type::Array && t->kind != Type::Unknown && t->kind != Type::Void;
        m_paramInReg[p] = scalar && !l.addressTaken;
    }
    // a jump back to the entry block would read the registers again after the body has used them
    for (size_t b = 0; b < m_f.blocks.size(); ++b)
        for (size_t s = 0; s < m_f.blocks[b].succs.size(); ++s)
            if (m_f.blocks[b].succs[s] == 0) { m_paramInReg.assign(n, false); m_prefixEnd = 0; }
    for (int p = 0; p < 4; ++p) m_lastParamLoad[p] = -1;
    for (size_t b = 0; b < m_f.blocks.size(); ++b)
        for (size_t i = 0; i < m_f.blocks[b].insts.size(); ++i) {
            const IrInst& in = m_f.blocks[b].insts[i];
            for (size_t a = 0; a < in.args.size(); ++a) {
                if (in.args[a].kind != IrOperand::Local || in.args[a].id < 0 || in.args[a].id >= m_f.paramCount) continue;
                const size_t p = static_cast<size_t>(in.args[a].id);
                m_paramUsed[p] = true;
                const bool prefixLoad = b == 0 && static_cast<int>(i) < m_prefixEnd && in.op == IrInst::LoadLocal && a == 0;
                if (!prefixLoad) m_paramInReg[p] = false;
            }
        }
    // block 0 starts at linear position 0, so a prefix index is also its position
    for (int i = 0; i < m_prefixEnd; ++i) {
        const int p = m_f.blocks[0].insts[static_cast<size_t>(i)].args[0].id;
        if (p < 4 && m_paramInReg[static_cast<size_t>(p)]) m_lastParamLoad[p] = i;
    }
}

void X86Function::decideFrame()
{
    m_frameless = false;
    if (!m_calls.empty() || !m_clobbers.empty() || !m_usedIntRegs.empty() || !m_usedFloatRegs.empty()) return;
    for (std::map<int, Interval>::const_iterator it = m_intervals.begin(); it != m_intervals.end(); ++it)
        if (it->second.reg < 0) return;                                   // a spilled value lives in the frame
    for (size_t b = 0; b < m_f.blocks.size(); ++b)
        for (size_t i = 0; i < m_f.blocks[b].insts.size(); ++i) {
            const IrInst& in = m_f.blocks[b].insts[i];
            for (size_t a = 0; a < in.args.size(); ++a) {
                if (in.args[a].kind != IrOperand::Local) continue;
                const int l = in.args[a].id;
                if (l >= 0 && l < m_f.paramCount && m_paramInReg[static_cast<size_t>(l)]) continue;   // read from its register
                return;                                                   // a local in memory
            }
        }
    for (size_t p = 0; p < m_paramInReg.size(); ++p) if (m_paramUsed[p] && !m_paramInReg[p]) return;
    m_frameless = true;
}

bool X86Function::inRegister(const IrOperand& o) const
{
    if (o.kind != IrOperand::Temp) return false;
    std::map<int, Interval>::const_iterator it = m_intervals.find(o.id);
    return it != m_intervals.end() && it->second.reg >= 0;
}

std::string X86Function::regName(const IrOperand& o) const
{
    if (o.kind != IrOperand::Temp) return std::string();
    std::map<int, Interval>::const_iterator it = m_intervals.find(o.id);
    return it != m_intervals.end() && it->second.reg >= 0 && !it->second.isFloat ? std::string(kIntRegs[it->second.reg]) : std::string();
}

std::string X86Function::fregOf(const IrOperand& o) const
{
    if (o.kind != IrOperand::Temp) return std::string();
    std::map<int, Interval>::const_iterator it = m_intervals.find(o.id);
    return it != m_intervals.end() && it->second.reg >= 0 && it->second.isFloat ? fregName(it->second.reg) : std::string();
}

// does the code emitted for 'in' write rcx (p = 0) or rdx (p = 1), the scratch registers of its sequences? The
// rules follow emitInst: an operand not in a register is materialised in rdx (rcx for an index), division uses
// both, a shift by a variable count uses cl, and calls, throws and try operations destroy every volatile register
bool X86Function::writesArgReg(const IrInst& in, int p) const
{
    switch (in.op) {
    case IrInst::Call: case IrInst::VCall: case IrInst::Throw: case IrInst::TryBegin: case IrInst::TryEnd:
    case IrInst::CatchParam: case IrInst::CatchEnd:
        return true;
    case IrInst::Bin: {
        const std::string& s = in.sub;
        if (s == "sdiv" || s == "udiv" || s == "srem" || s == "urem") return true;
        if (s == "shl" || s == "ashr" || s == "lshr") return p == 0 && in.args[1].kind != IrOperand::Imm;
        if (s[0] == 'f') return false;                                   // floating point: xmm registers only
        if (p != 1) return false;
        if (immediateOperand(in.args[1], typeSize(in.type))) return false;   // encoded in the instruction
        if (!inRegister(in.args[1])) return true;                        // b materialised in rdx
        std::map<int, Interval>::const_iterator d = m_intervals.find(in.dst), b = m_intervals.find(in.args[1].id);
        return d != m_intervals.end() && b != m_intervals.end() && d->second.reg == b->second.reg;   // "mov rdx, b"
    }
    case IrInst::Cmp: {
        if (p != 1 || in.sub[0] == 'f' || inRegister(in.args[1])) return false;
        const int size = typeSize(in.args[0].type ? in.args[0].type : m_f.typeOf(in.args[0].kind == IrOperand::Temp ? in.args[0].id : -1));
        return !immediateOperand(in.args[1], size);
    }
    case IrInst::Copy: return p == 1 && !tempIsFloat(in.dst) && !inRegister(in.args[0]);
    case IrInst::AddrField: return p == 1 && !m_folded.count(in.dst) && !inRegister(in.args[0]);
    case IrInst::AddrIndex: return p == 1 ? !inRegister(in.args[0]) : !indexImmediate(in) && !inRegister(in.args[1]);
    case IrInst::CondBr: {
        // a byte tested in memory at an address: as a load from it
        if (p != 1 || in.args.empty() || in.args[0].kind != IrOperand::Temp) return false;
        std::map<int, ByteTest>::const_iterator bt = m_byteTest.find(in.args[0].id);
        if (bt == m_byteTest.end() || bt->second.kind != 3) return false;
        const IrOperand& a = bt->second.mem;
        if (a.kind == IrOperand::Temp && m_folded.count(a.id)) return !inRegister(m_folded.find(a.id)->second.base);
        return !inRegister(a);
    }
    case IrInst::LoadMem: case IrInst::StoreMem: {
        if (p != 1) return false;
        const IrOperand& a = in.args[0];
        if (a.kind == IrOperand::Temp && m_folded.count(a.id)) return !inRegister(m_folded.find(a.id)->second.base);
        return !inRegister(a);
    }
    default: return false;
    }
}

// may parameter p's value (temp t) live in its argument register? Nothing during its lifetime may write that
// register, and no other value may hold it
bool X86Function::canStayInArgReg(int t, int p) const
{
    const int* const argIdx = kArgReg;
    std::map<int, Interval>::const_iterator it = m_intervals.find(t);
    if (it == m_intervals.end()) return false;
    const Interval& iv = it->second;
    for (int k = iv.start; k <= iv.end; ++k) {
        const IrInst& at = m_f.blocks[static_cast<size_t>(m_linear[static_cast<size_t>(k)].first)].insts[static_cast<size_t>(m_linear[static_cast<size_t>(k)].second)];
        if (p < 2) { if (writesArgReg(at, p)) return false; }
        else for (size_t c = 0; c < m_clobbers.size(); ++c) if (m_clobbers[c] == k) return false;
    }
    for (std::map<int, Interval>::const_iterator o = m_intervals.begin(); o != m_intervals.end(); ++o)
        if (o->first != t && o->second.reg == argIdx[p] && o->second.start <= iv.end && o->second.end >= iv.start) return false;
    return true;
}

void X86Function::coalesce()
{
    m_returnDirect.clear();
    if (m_f.hasTry && !m_tableRegs) return;   // longjmp restores only the callee-saved registers
    // a parameter's value stays in its argument register (rcx, rdx, r8, r9) when nothing writes it meanwhile
    const int* const argIdx = kArgReg;
    for (int i = 0; i < m_prefixEnd; ++i) {
        const IrInst& in = m_f.blocks[0].insts[static_cast<size_t>(i)];
        const int p = in.args[0].id;
        if (p >= 4 || in.dst < 0 || !m_paramInReg[static_cast<size_t>(p)]) continue;
        std::map<int, Interval>::iterator it = m_intervals.find(in.dst);
        if (it == m_intervals.end() || it->second.isFloat || it->second.reg < 0) continue;
        // (an r8 / r9 parameter that the allocator already put there stays: nothing to settle)
        if (it->second.reg != argIdx[p] && canStayInArgReg(in.dst, p)) { it->second.reg = argIdx[p]; it->second.fixed = true; }
    }
    // a value computed by the instruction just before its return, and used only there, is computed in rax
    std::map<int, int> uses;
    for (size_t p = 0; p < m_linear.size(); ++p) {
        const IrInst& in = m_f.blocks[static_cast<size_t>(m_linear[p].first)].insts[static_cast<size_t>(m_linear[p].second)];
        for (size_t a = 0; a < in.args.size(); ++a) if (in.args[a].kind == IrOperand::Temp) ++uses[in.args[a].id];
    }
    for (size_t p = 1; p < m_linear.size(); ++p) {
        const IrInst& in = m_f.blocks[static_cast<size_t>(m_linear[p].first)].insts[static_cast<size_t>(m_linear[p].second)];
        if (in.op != IrInst::Ret || in.args.empty() || in.args[0].kind != IrOperand::Temp) continue;
        // defined in the same block, just before (the nop a function's unwind region leaves before the return is no code)
        size_t q = p - 1;
        while (q > 0 && m_linear[q].first == m_linear[p].first && instAt(q).op == IrInst::Nop) --q;
        if (m_linear[q].first != m_linear[p].first) continue;
        const int v = in.args[0].id;
        std::map<int, Interval>::iterator it = m_intervals.find(v);
        if (it == m_intervals.end() || it->second.isFloat || it->second.reg < 0 || uses[v] != 1) continue;
        if (it->second.start != static_cast<int>(q) || it->second.end != static_cast<int>(p)) continue;
        const IrInst& def = instAt(q);
        if (def.dst != v || def.op == IrInst::Phi) continue;
        it->second.reg = kRax;
        it->second.fixed = true;
        m_returnDirect.insert(v);
    }
    // only the callee-saved registers still in use are saved
    m_usedIntRegs.clear();
    for (std::map<int, Interval>::const_iterator it = m_intervals.begin(); it != m_intervals.end(); ++it)
        if (!it->second.isFloat && it->second.reg >= 0 && it->second.reg < kIntRegCount) m_usedIntRegs.insert(it->second.reg);
}

// the registers freed by coalescing (a parameter's first register, before it stayed in rcx) go to the other
// values: allocate again around the settled ones. Then the settled parameters are checked again (a value spilled
// now would be materialised in rdx); if one no longer fits, the first allocation is kept.
void X86Function::reallocateAroundCoalesced()
{
    bool any = false;
    for (std::map<int, Interval>::const_iterator it = m_intervals.begin(); it != m_intervals.end(); ++it) if (it->second.fixed) any = true;
    if (any) {
        const std::map<int, Interval> settled = m_intervals;
        const int spillsSettled = m_stats.spills;
        for (std::map<int, Interval>::iterator it = m_intervals.begin(); it != m_intervals.end(); ++it) if (!it->second.fixed) it->second.reg = -1;
        m_usedIntRegs.clear();
        m_usedFloatRegs.clear();
        m_stats.spills -= m_firstSpills;
        allocate();
        bool ok = true;
        for (int i = 0; i < m_prefixEnd && ok; ++i) {
            const IrInst& in = m_f.blocks[0].insts[static_cast<size_t>(i)];
            std::map<int, Interval>::const_iterator it = m_intervals.find(in.dst);
            if (in.dst >= 0 && it != m_intervals.end() && it->second.fixed && it->second.reg != kRax) ok = canStayInArgReg(in.dst, in.args[0].id);
        }
        if (!ok) { m_intervals = settled; m_stats.spills = spillsSettled; }
    }
    countSavedRegisters();
}

void X86Function::countSavedRegisters()
{
    m_usedIntRegs.clear();
    m_usedFloatRegs.clear();
    for (std::map<int, Interval>::const_iterator it = m_intervals.begin(); it != m_intervals.end(); ++it) {
        if (it->second.reg < 0) continue;
        if (it->second.isFloat) { if (it->second.reg < kFloatRegCount) m_usedFloatRegs.insert(it->second.reg); }   // (xmm3 is not saved)
        else if (it->second.reg < kIntRegCount) m_usedIntRegs.insert(it->second.reg);
    }
}

// may iv, the k-th register argument of the call at its end, live in that argument register all its life? Nothing
// before the call may write the register: for rcx and rdx the sequences writesArgReg knows, the instruction that
// defines the value included (its own sequence may use them as scratch; a call defining it writes them before the
// result exists); the parameters are read from the four registers at the start; and no other value may hold it
bool X86Function::holdsArgReg(const Interval& iv, int k) const
{
    if (iv.start < m_lastParamLoad[k]) return false;
    if (k < 2)
        for (int q = iv.callResult ? iv.start + 1 : iv.start; q < iv.end; ++q)
            if (writesArgReg(instAt(static_cast<size_t>(q)), k)) return false;
    for (std::map<int, Interval>::const_iterator o = m_intervals.begin(); o != m_intervals.end(); ++o) {
        const Interval& v = o->second;
        if (&v == &iv || v.reg != kArgReg[k] || v.start > iv.end || v.end < iv.start) continue;
        // a value whose last use is the call that defines the other is dead by then (allocate's rule)
        if (v.end == iv.start && iv.callResult && !v.liveOutEnd) continue;
        if (iv.end == v.start && v.callResult && !iv.liveOutEnd) continue;
        return false;
    }
    return true;
}

// a call's result read once, by an instruction that follows the call before anything writes rax, stays in rax: the
// instructions between may only be constants, addresses and loads of locals and globals into registers (their
// sequences write their own register alone; spilled, they would go through rax) and nops, and the reader takes the
// value before its own sequence writes rax: a store of the value, a conversion, a negation, a return, a branch, a
// copy, a load through it, the first operand of an arithmetic or a comparison (the second is read after the first
// was materialised in rax); not a call's argument (emitCall breaks a cycle through rax). After the last allocation:
// what is spilled is settled by then
void X86Function::resultsInRax()
{
    if (m_f.hasTry && !m_tableRegs) return;
    std::map<int, int> uses;
    for (size_t p = 0; p < m_linear.size(); ++p) {
        const IrInst& in = instAt(p);
        for (size_t a = 0; a < in.args.size(); ++a) if (in.args[a].kind == IrOperand::Temp) ++uses[in.args[a].id];
    }
    for (size_t c = 0; c < m_calls.size(); ++c) {
        const size_t p = static_cast<size_t>(m_calls[c]);
        const IrInst& call = instAt(p);
        if (call.dst < 0) continue;
        std::map<int, Interval>::iterator it = m_intervals.find(call.dst);
        if (it == m_intervals.end() || it->second.isFloat || it->second.fixed || uses[call.dst] != 1) continue;
        size_t q = p + 1;
        bool clear = true;
        for (; q < m_linear.size() && m_linear[q].first == m_linear[p].first && clear; ++q) {
            const IrInst& in = instAt(q);
            if (in.op == IrInst::Nop) continue;
            if (in.op != IrInst::Const && in.op != IrInst::FConst && in.op != IrInst::StrConst && in.op != IrInst::AddrLocal && in.op != IrInst::AddrGlobal
                && in.op != IrInst::LoadLocal && in.op != IrInst::LoadGlobal) break;
            std::map<int, Interval>::const_iterator d = m_intervals.find(in.dst);
            clear = d != m_intervals.end() && d->second.reg >= 0 && d->second.reg != kRax;
        }
        if (!clear || q >= m_linear.size() || m_linear[q].first != m_linear[p].first) continue;
        if (it->second.start != static_cast<int>(p) || it->second.end != static_cast<int>(q)) continue;
        const IrInst& user = instAt(q);
        const bool first = !user.args.empty() && user.args[0].kind == IrOperand::Temp && user.args[0].id == call.dst;
        const bool value = user.args.size() > 1 && user.args[1].kind == IrOperand::Temp && user.args[1].id == call.dst;
        bool ok = false;
        switch (user.op) {
        case IrInst::StoreLocal: case IrInst::StoreGlobal: case IrInst::StoreMem: ok = value && !first; break;
        case IrInst::Conv: case IrInst::Un: case IrInst::Ret: case IrInst::CondBr: case IrInst::Copy: case IrInst::LoadMem: ok = first; break;
        case IrInst::Bin: case IrInst::Cmp: ok = first && !value; break;
        default: break;
        }
        if (!ok) continue;
        it->second.reg = kRax;
        it->second.fixed = true;
    }
    countSavedRegisters();
}

// (a function under the setjmp rules, hasTry without m_tableRegs, takes the hints too: a hinted value dies at its call
// and crosses no call, throw or try operation, so no longjmp and no landing pad ever restores it; the landing pads'
// own operands, the object's address and its destructor, went through callee-saved registers and two moves there)
void X86Function::hintArguments()
{
    for (size_t c = 0; c < m_calls.size(); ++c) {
        const int pos = m_calls[c];
        const IrInst& call = instAt(static_cast<size_t>(pos));
        const size_t firstArg = call.sub == "(indirect)" && !call.args.empty() ? 1 : 0;
        for (size_t a = firstArg; a < call.args.size() && a < firstArg + 4; ++a) {
            const IrOperand& o = call.args[a];
            if (o.kind != IrOperand::Temp || opIsFloat(o)) continue;
            std::map<int, Interval>::iterator it = m_intervals.find(o.id);
            if (it == m_intervals.end()) continue;
            Interval& iv = it->second;
            const int k = static_cast<int>(a - firstArg);
            // (a value passed twice keeps the first position's register; the second is copied from it)
            if (iv.fixed || iv.isFloat || iv.crosses || iv.end != pos || iv.liveOutEnd || iv.reg == kArgReg[k]) continue;
            if (!holdsArgReg(iv, k)) continue;
            iv.reg = kArgReg[k];
            iv.fixed = true;
        }
    }
    countSavedRegisters();
}

void X86Function::allocate()
{
    // callee-saved registers survive calls, so intervals need no call-crossing treatment
    std::vector<Interval*> order;
    for (std::map<int, Interval>::iterator it = m_intervals.begin(); it != m_intervals.end(); ++it) order.push_back(&it->second);
    std::sort(order.begin(), order.end(), ByStart());
    std::vector<Interval*> active;
    bool intFree[kIntRegCount + kVolatileRegCount], fltFree[kFloatRegCount + 2];
    for (int i = 0; i < kIntRegCount + kVolatileRegCount; ++i) intFree[i] = true;
    for (int i = 0; i < kFloatRegCount + 2; ++i) fltFree[i] = true;
    // a volatile register may hold a value that no call, throw or try operation interrupts; setjmp/longjmp
    // restore only the callee-saved registers, so a function with try regions keeps to those (unless its regions
    // are tables: see run())
    const bool volatileOk = !m_f.hasTry || m_tableRegs;
    for (size_t k = 0; k < order.size(); ++k) {
        Interval* iv = order[k];
        for (size_t a = 0; a < active.size();) {
            // a value whose last use is the call that defines this one is dead by the time the result is written
            const bool dead = active[a]->end < iv->start || (iv->callResult && active[a]->end == iv->start && !active[a]->liveOutEnd);
            if (dead) {
                if (active[a]->reg >= 0 && (active[a]->isFloat || active[a]->reg < kIntRegCount + kVolatileRegCount)) {
                    if (active[a]->isFloat) fltFree[active[a]->reg] = true; else intFree[active[a]->reg] = true;
                }
                active.erase(active.begin() + static_cast<long>(a));
            } else ++a;
        }
        if (iv->fixed) {
            // settled by coalescing: rcx / rdx / rax are outside the pool; r8 / r9 are taken for its lifetime
            if (iv->reg >= 0 && iv->reg < kIntRegCount + kVolatileRegCount) { intFree[iv->reg] = false; active.push_back(iv); }
            continue;
        }
        const bool mayUseVolatile = volatileOk && !iv->isFloat && !iv->crosses;
        // r8 and r9 carry the third and fourth parameters until their last load at the start: a value defined
        // before that load must not overwrite them (the load itself may: "movsxd r8, r8d")
        const bool r8Busy = iv->start < m_lastParamLoad[2], r9Busy = iv->start < m_lastParamLoad[3];
        // xmm3 and xmm2 (the fourth and third parameter's registers until their last read) for a floating-point value
        // that crosses nothing
        const bool mayUseX3 = volatileOk && iv->isFloat && !iv->crosses && !r9Busy, mayUseX2 = volatileOk && iv->isFloat && !iv->crosses && !r8Busy;
        // a value takes over the register of an operand whose last use is the instruction defining it: the two-address
        // forms (mov rd, a; op rd, b), a copy (the phi copies after SSA), a conversion or an address computation then
        // need no move (the emitter leaves out "mov rd, rd", and an operand that is the second one is read first)
        if (iv->start >= 0 && static_cast<size_t>(iv->start) < m_linear.size()) {
            const IrInst& def = instAt(static_cast<size_t>(iv->start));
            Interval* dying = 0;
            if (def.dst == iv->temp && (def.op == IrInst::Copy || def.op == IrInst::Bin || def.op == IrInst::Un || def.op == IrInst::Conv ||
                                        def.op == IrInst::AddrField || def.op == IrInst::AddrIndex || def.op == IrInst::LoadMem))
                for (size_t a = 0; a < def.args.size() && !dying; ++a) {
                    if (def.args[a].kind != IrOperand::Temp) continue;
                    std::map<int, Interval>::iterator d = m_intervals.find(def.args[a].id);
                    if (d == m_intervals.end() || d->second.isFloat != iv->isFloat || d->second.reg < 0 || d->second.fixed) continue;
                    if (d->second.end != iv->start || d->second.liveOutEnd) continue;
                    const int r = d->second.reg;
                    if (!iv->isFloat && r >= kVolatileRegBase && (!mayUseVolatile || (r == kVolatileRegBase && r8Busy) || (r == kVolatileRegBase + 1 && r9Busy))) continue;
                    if (!iv->isFloat && r >= kIntRegCount + kVolatileRegCount) continue;
                    if (iv->isFloat && ((r == kFloatVolatile && !mayUseX3) || (r == kFloatVolatile2 && !mayUseX2))) continue;
                    for (size_t x = 0; x < active.size() && !dying; ++x) if (active[x] == &d->second) dying = &d->second;
                }
            if (dying) {
                // the operand leaves the active list without freeing its register: this value holds it from here on
                for (size_t x = 0; x < active.size(); ++x) if (active[x] == dying) { active.erase(active.begin() + static_cast<long>(x)); break; }
                iv->reg = dying->reg;
                active.push_back(iv);
                if (iv->isFloat) { if (iv->reg < kFloatRegCount) m_usedFloatRegs.insert(iv->reg); } else if (iv->reg < kIntRegCount) m_usedIntRegs.insert(iv->reg);
                continue;
            }
        }
        int got = -1;
        if (iv->isFloat) {
            if (mayUseX3 && fltFree[kFloatVolatile]) got = kFloatVolatile;
            else if (mayUseX2 && fltFree[kFloatVolatile2]) got = kFloatVolatile2;
            else for (int r = 0; r < kFloatRegCount; ++r) if (fltFree[r]) { got = r; break; }
        }
        else {
            if (mayUseVolatile)
                for (int r = kVolatileRegBase; r < kVolatileRegBase + kVolatileRegCount; ++r)
                    if (intFree[r] && !(r == kVolatileRegBase && r8Busy) && !(r == kVolatileRegBase + 1 && r9Busy)) { got = r; break; }
            if (got < 0) for (int r = 0; r < kIntRegCount; ++r) if (intFree[r]) { got = r; break; }
        }
        if (got >= 0) {
            iv->reg = got;
            (iv->isFloat ? fltFree : intFree)[got] = false;
            active.push_back(iv);
            if (iv->isFloat) { if (got < kFloatRegCount) m_usedFloatRegs.insert(got); } else if (got < kIntRegCount) m_usedIntRegs.insert(got);   // only callee-saved ones are saved
            continue;
        }
        // no register free: the value with the fewest definitions and uses per instruction of its life (weighted by
        // loop depth) goes to memory, this one or the active one with the lowest such density; among equals the one
        // that lives longest. A spilled value costs a store and a load at each use, so the rarely used long ones go
        Interval* victim = 0;
        for (size_t a = 0; a < active.size(); ++a) {
            Interval* v = active[a];
            if (v->isFloat != iv->isFloat || v->reg < 0 || v->fixed) continue;
            if (iv->isFloat && ((v->reg == kFloatVolatile && !mayUseX3) || (v->reg == kFloatVolatile2 && !mayUseX2))) continue;
            if (!iv->isFloat && v->reg >= kVolatileRegBase &&
                (!mayUseVolatile || (v->reg == kVolatileRegBase && r8Busy) || (v->reg == kVolatileRegBase + 1 && r9Busy))) continue;
            if (!victim || density(*v) < density(*victim) || (density(*v) == density(*victim) && v->end > victim->end)) victim = v;
        }
        if (victim && (density(*victim) < density(*iv) || (density(*victim) == density(*iv) && victim->end > iv->end))) { iv->reg = victim->reg; victim->reg = -1; ++m_stats.spills; active.push_back(iv); }
        else { iv->reg = -1; ++m_stats.spills; }
    }
}

void X86Function::layoutFrame()
{
    // the callee-saved integer registers are pushed right after rbp, as cl, clang and gcc save them (1 or 2
    // bytes each instead of a 4 to 8 byte store): they occupy [rbp-8] .. [rbp-8n], the frame continues below
    int off = 8 * static_cast<int>(m_usedIntRegs.size());
    m_localOffset.assign(m_f.locals.size(), 0);
    // the locals and the spilled values in order of use per byte, the most used nearest rbp: a slot within 128 bytes
    // of it is addressed with a byte of displacement, one further away with four, in every instruction that reads or
    // writes it (a variadic function's parameters stay in their home slots, see below)
    std::vector<FrameSlot> slots;
    for (size_t l = 0; l < m_f.locals.size(); ++l) {
        if (m_f.variadic && static_cast<int>(l) < m_f.paramCount) continue;
        FrameSlot fs;
        fs.local = static_cast<int>(l); fs.temp = -1;
        fs.size = m_f.locals[l].size > 0 ? m_f.locals[l].size : 8;
        fs.align = m_f.locals[l].align > 0 ? m_f.locals[l].align : 8;
        if (fs.align > 16) fs.align = 16;
        fs.density = static_cast<long long>(l < m_localWeight.size() ? m_localWeight[l] : 1) * 8 / fs.size;
        slots.push_back(fs);
    }
    for (std::map<int, Interval>::iterator it = m_intervals.begin(); it != m_intervals.end(); ++it)
        if (it->second.reg < 0) { FrameSlot fs; fs.local = -1; fs.temp = it->first; fs.size = 8; fs.align = 8; fs.density = it->second.weight; slots.push_back(fs); }
    std::stable_sort(slots.begin(), slots.end(), ByDensity());
    for (size_t k = 0; k < slots.size(); ++k) {
        const FrameSlot& fs = slots[k];
        off = (off + fs.size + fs.align - 1) / fs.align * fs.align;
        if (fs.local >= 0) m_localOffset[static_cast<size_t>(fs.local)] = off; else m_intervals[fs.temp].slot = off;
    }
    for (size_t l = 0; l < m_f.locals.size(); ++l) {
        // a variadic function keeps its parameters where MSVC's va_start expects them: in the 8-byte home slots of
        // the caller's shadow space and stack arguments, so the unnamed arguments follow the last named one
        if (m_f.variadic && static_cast<int>(l) < m_f.paramCount) m_localOffset[l] = -(16 + 8 * static_cast<int>(l));
    }
    for (std::set<int>::iterator r = m_usedFloatRegs.begin(); r != m_usedFloatRegs.end(); ++r) { off = (off + 16 + 15) / 16 * 16; m_savedFloatOffset[*r] = off; }
    // version 5: one setjmp frame (jmp_buf plus link, 272 bytes, 16-byte aligned) per try region,
    // and a 16-byte slot for thrown scalars
    for (size_t b = 0; b < m_f.blocks.size(); ++b)
        for (size_t i = 0; i < m_f.blocks[b].insts.size(); ++i) {
            const IrInst& in = m_f.blocks[b].insts[i];
            if (in.op == IrInst::TryBegin && !m_tableEh) { off = (off + kTryFrameSize + 15) / 16 * 16; m_tryOffset[in.extra] = off; }
            else if (in.op == IrInst::Throw && !in.args.empty() && m_throwSlot == 0) { off = (off + 16 + 15) / 16 * 16; m_throwSlot = off; }
        }
    int outgoing = 32 + 8 * m_outgoingArgs;       // shadow space plus stack arguments
    m_frameSize = (off + outgoing + 15) / 16 * 16;
    m_stats.frameSize = m_frameSize;
}

std::string X86Function::floatConst(double v) { return m_m.floatLabel(v); }

namespace {

std::string hex32(unsigned long v)
{
    std::string r;
    for (int i = 7; i >= 0; --i) r += "0123456789abcdef"[(v >> (i * 4)) & 15];
    return r;
}

// +0.0 (every bit clear, so of either precision): the register is cleared by xorps instead of loaded from a constant
bool isPositiveZero(double v)
{
    unsigned long half[2];
    std::memcpy(half, &v, sizeof half);
    return half[0] == 0 && half[1] == 0;
}

// label for a double from its IEEE bits (two 32-bit halves: C++98 has no 64-bit integer)
std::string floatName(double v)
{
    unsigned long half[2];
    std::memcpy(half, &v, sizeof half);
    return "fconst_" + hex32(half[1]) + hex32(half[0]);
}

// label for a string literal from two independent 32-bit hashes of its contents
std::string stringName(const std::string& text)
{
    unsigned long fnv = 2166136261UL, djb = 5381UL;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        fnv = (fnv ^ c) * 16777619UL;
        djb = djb * 33UL + c;
    }
    return "str_" + hex32(fnv) + "_" + hex32(djb);
}

} // namespace

std::string X86Module::floatLabel(double v)
{
    std::string name = floatName(v);
    Lock l(m_mutex);
    m_floats[name] = v;
    return name;
}

std::string X86Module::stringLabel(const std::string& text)
{
    std::string name = stringName(text);
    Lock l(m_mutex);
    m_strings[name] = text;
    return name;
}

std::string X86Function::use(const IrOperand& o, const std::string& scratch)
{
    bool fl = opIsFloat(o);
    switch (o.kind) {
    case IrOperand::Temp: {
        std::map<int, Interval>::iterator it = m_intervals.find(o.id);
        if (it == m_intervals.end()) { line("xor " + reg32(scratch) + ", " + reg32(scratch) + "        ; undefined temporary"); return scratch; }
        if (it->second.reg >= 0) return it->second.isFloat ? fregName(it->second.reg) : kIntRegs[it->second.reg];
        if (it->second.isFloat) {
            std::string xr = fl ? scratch : "xmm5";
            line("movsd " + xr + ", real8 ptr [rbp-" + intToString(it->second.slot) + "]");
            return xr;
        }
        line("mov " + scratch + ", qword ptr [rbp-" + intToString(it->second.slot) + "]");
        return scratch;
    }
    case IrOperand::Imm: {
        // a non-negative value, or one of a type up to 4 bytes (it lives in the low 32 bits), through the 32-bit
        // register (shorter, clears the upper half); no xor here: an operand can be materialised between a
        // comparison and the instruction that reads its flags
        const bool w32 = o.type && typeSize(o.type) <= 4;
        line("mov " + (w32 || static_cast<int>(o.ival) >= 0 ? reg32(scratch) : scratch) + ", " + intToString(static_cast<int>(o.ival)));
        return scratch;
    }
    case IrOperand::FImm: {
        if (isPositiveZero(o.fval)) { line("xorps " + scratch + ", " + scratch); return scratch; }   // (xorps leaves the flags alone)
        std::string c = floatConst(o.fval);
        line("movsd " + scratch + ", real8 ptr [" + c + "]");
        if (isSingle(o.type)) line("cvtsd2ss " + scratch + ", " + scratch);   // constants are kept as doubles
        return scratch;
    }
    case IrOperand::Local: line("lea " + scratch + ", " + localRef(o.id)); return scratch;
    case IrOperand::Global: { std::string g = m_m.globalSymbol(o.name); m_m.useGlobal(o.name); line("lea " + scratch + ", " + g); return scratch; }
    case IrOperand::Str: { line("lea " + scratch + ", " + m_m.stringLabel(o.name)); return scratch; }
    default: return scratch;
    }
}

std::string X86Function::memOperand(const IrOperand& addr)
{
    if (addr.kind == IrOperand::Temp) {
        std::map<int, Folded>::const_iterator f = m_folded.find(addr.id);
        if (f != m_folded.end()) return "[" + use(f->second.base, "rdx") + "+" + intToString(static_cast<int>(f->second.offset)) + "]";
    }
    return "[" + use(addr, "rdx") + "]";
}

std::string X86Function::def(int temp, bool isFloat, const std::string& scratch, int& spill)
{
    spill = 0;
    std::map<int, Interval>::iterator it = m_intervals.find(temp);
    if (it == m_intervals.end()) return scratch;
    if (it->second.reg >= 0) return it->second.isFloat ? fregName(it->second.reg) : kIntRegs[it->second.reg];
    spill = it->second.slot;
    (void)isFloat;
    return scratch;
}

void X86Function::finish(int temp, int spill, bool isFloat, const std::string& reg)
{
    (void)temp;
    if (spill > 0) line(isFloat ? "movsd real8 ptr [rbp-" + intToString(spill) + "], " + reg : "mov qword ptr [rbp-" + intToString(spill) + "], " + reg);
}

// integer values narrower than 64 bits live in the low 32 bits of their register, the upper 32 unspecified, as cl's
// code has them: an operation on a 4-byte value is a 32-bit instruction, a 1- or 2-byte value is kept promoted to
// 32 bits by its signedness (this), so that the 32-bit forms of compare, test, divide and shift read it right; a
// conversion to 8 bytes and an index extend a value explicitly
void X86Function::extend(const std::string& rd, const Type* t)
{
    const int size = typeSize(t);
    if ((size != 1 && size != 2) || !t || t->kind == Type::Pointer || t->kind == Type::Reference) return;
    const bool zero = t->isUnsigned || t->kind == Type::Bool;
    line(std::string(zero ? "movzx " : "movsx ") + reg32(rd) + ", " + regSized(rd, size));
}

void X86Function::emitCall(const IrInst& in)
{
    static const char* const argRegs[] = { "rcx", "rdx", "r8", "r9" };
    const bool indirect = in.sub == "(indirect)" && !in.args.empty();   // args[0] is the function pointer: called through r10
    const size_t firstArg = indirect ? 1 : 0;
    // the fifth argument on go to the outgoing area [rsp+8a] (the first four slots are the shadow space)
    for (size_t a = firstArg + 4; a < in.args.size(); ++a) {
        const IrOperand& o = in.args[a];
        int slot = 8 * static_cast<int>(a - firstArg);
        if (opIsFloat(o)) { std::string r = use(o, "xmm4"); line("movsd real8 ptr [rsp+" + intToString(slot) + "], " + r); }
        else { std::string r = use(o, "rax"); line("mov qword ptr [rsp+" + intToString(slot) + "], " + r); }
    }
    // the first four go into their registers, the function pointer into r10. A source may be a volatile register (a
    // value whose last use is this call), one of those destinations among them: the moves between registers go in
    // an order that reads every source before it is overwritten, a cycle through rax; the constants, addresses and
    // spilled values are materialised straight into their registers after them (they read no register another
    // argument writes)
    std::vector<std::pair<std::string, std::string> > moves, fmoves;   // destination, source (integer / xmm registers)
    std::vector<size_t> later;                                 // the arguments materialised after the moves
    if (indirect) { if (!regName(in.args[0]).empty()) moves.push_back(std::make_pair(std::string("r10"), regName(in.args[0]))); else later.push_back(0); }
    for (size_t a = firstArg; a < in.args.size() && a < firstArg + 4; ++a) {
        const IrOperand& o = in.args[a];
        if (!opIsFloat(o) && !regName(o).empty()) moves.push_back(std::make_pair(std::string(argRegs[a - firstArg]), regName(o)));
        else if (opIsFloat(o) && !fregOf(o).empty()) fmoves.push_back(std::make_pair("xmm" + intToString(static_cast<int>(a - firstArg)), fregOf(o)));
        else later.push_back(a);
    }
    for (int pass = 0; pass < 2; ++pass) {
        // pass 0 the integer registers (a cycle through rax), pass 1 the xmm registers (xmm2 and xmm3 may hold values
        // whose last use is this call; a cycle through xmm4); a floating-point argument also goes to its integer
        // register, see below, after the integer moves have read theirs
        std::vector<std::pair<std::string, std::string> >& mv = pass == 0 ? moves : fmoves;
        const std::string scratch = pass == 0 ? "rax" : "xmm4", mnemonic = pass == 0 ? "mov " : "movsd ";
        while (!mv.empty()) {
            size_t m = 0;
            for (; m < mv.size(); ++m) {
                bool read = false;   // is this destination still the source of another move?
                for (size_t j = 0; j < mv.size() && !read; ++j) read = j != m && mv[j].second == mv[m].first;
                if (!read) break;
            }
            if (m == mv.size()) {
                // every destination is still to be read: a cycle; the first source goes through the scratch register
                line(mnemonic + scratch + ", " + mv[0].second);
                for (size_t j = 1; j < mv.size(); ++j) if (mv[j].second == mv[0].second) mv[j].second = scratch;
                mv[0].second = scratch;
                continue;
            }
            if (mv[m].first != mv[m].second) line(mnemonic + mv[m].first + ", " + mv[m].second);
            if (pass == 1 && in.variadic) line("movq " + std::string(argRegs[mv[m].first[3] - '0']) + ", " + mv[m].first);   // a variadic callee reads it there
            mv.erase(mv.begin() + static_cast<long>(m));
        }
    }
    for (size_t i = 0; i < later.size(); ++i) {
        const size_t a = later[i];
        const IrOperand& o = in.args[a];
        if (indirect && a == 0) { std::string fp = use(o, "r10"); if (fp != "r10") line("mov r10, " + fp); continue; }
        const int k = static_cast<int>(a - firstArg);
        if (opIsFloat(o)) {
            const std::string x = "xmm" + intToString(k);
            std::string r = use(o, x);
            if (r != x) line("movsd " + x + ", " + r);
            // also in the integer register for a variadic callee (or one of unknown type), which reads its arguments
            // from the integer registers it spills to the home area, as the Microsoft x64 convention requires; any
            // other callee ignores that register
            if (in.variadic) line("movq " + std::string(argRegs[k]) + ", " + x);
        } else {
            std::string r = use(o, argRegs[k]);
            if (r != argRegs[k]) line("mov " + std::string(argRegs[k]) + ", " + r);
        }
    }
    if (in.op == IrInst::VCall) {
        line("mov rax, qword ptr [rcx]              ; vptr");
        line("call qword ptr [rax+" + intToString(static_cast<int>(in.extra * 8)) + "]        ; slot " + intToString(static_cast<int>(in.extra)) + " " + in.sub);
    } else if (in.sub == "(indirect)") {
        line("call r10");
    } else {
        std::string callee = m_m.symbol(in.sub);
        m_m.useExtern(callee);
        line("call " + callee);
    }
    if (in.dst >= 0) {
        bool fl = tempIsFloat(in.dst);
        // the callee (an MSVC-compiled one too) defines only the low bytes of a narrow result: a 1- or 2-byte one is
        // promoted to 32 bits, as every narrow value is held, unless the branch right after tests the byte itself
        const int size = fl ? 8 : typeSize(in.type);
        const bool zero = in.type && (in.type->isUnsigned || in.type->kind == Type::Bool);
        if ((size == 2 || size == 1) && !m_byteTest.count(in.dst)) line(std::string(zero ? "movzx " : "movsx ") + "eax, " + regSized("rax", size));
        int spill;
        std::string rd = def(in.dst, fl, fl ? "xmm0" : "rax", spill);
        if (fl) { if (rd != "xmm0") line("movsd " + rd + ", xmm0"); }
        else if (rd != "rax") line("mov " + rd + ", rax");
        finish(in.dst, spill, fl, rd);
    }
}

void X86Function::emitInst(const IrInst& in, int blockIndex)
{
    // the line table (a comment to ml64, read by the COFF writer): the source line of the code from here on
    if (!m_f.sourceFile.empty() && in.line > 0 && in.line != m_lastLine) { m_out << ";@line " << in.line << "\n"; m_lastLine = in.line; }
    comment(in.str());
    bool dstFloat = in.dst >= 0 && tempIsFloat(in.dst);
    int spill = 0;
    switch (in.op) {
    case IrInst::Const: {
        std::string rd = def(in.dst, false, "rax", spill);
        // shortest form, as cl, clang and gcc load constants: zero by xor (a constant is its own IR instruction,
        // so no flags are live across it), a non-negative value or one of a type up to 4 bytes (which lives in the
        // low 32 bits) through the 32-bit register, a negative 8-byte one sign-extended into the full register
        const bool w32 = in.type && typeSize(in.type) <= 4;
        const long v = static_cast<long>(static_cast<int>(in.args[0].ival));
        if (v == 0) line("xor " + reg32(rd) + ", " + reg32(rd));
        else line("mov " + (w32 || v > 0 ? reg32(rd) : rd) + ", " + intToString(static_cast<int>(v)));
        finish(in.dst, spill, false, rd);
        break;
    }
    case IrInst::FConst: {
        std::string rd = def(in.dst, true, "xmm0", spill);
        if (isPositiveZero(in.args[0].fval)) line("xorps " + rd + ", " + rd);
        else {
            line("movsd " + rd + ", real8 ptr [" + floatConst(in.args[0].fval) + "]");
            if (isSingle(in.type)) line("cvtsd2ss " + rd + ", " + rd);
        }
        finish(in.dst, spill, true, rd);
        break;
    }
    case IrInst::StrConst: { std::string rd = def(in.dst, false, "rax", spill); line("lea " + rd + ", " + m_m.stringLabel(in.args[0].name)); finish(in.dst, spill, false, rd); break; }
    case IrInst::Copy: {
        std::string rs = use(in.args[0], dstFloat ? "xmm1" : "rdx");
        std::string rd = def(in.dst, dstFloat, dstFloat ? "xmm0" : "rax", spill);
        const bool w32 = !dstFloat && typeSize(in.type) > 0 && typeSize(in.type) <= 4;   // a narrow value: its low 32 bits (no REX prefix)
        if (rd != rs) line(dstFloat ? "movsd " + rd + ", " + rs : w32 ? "mov " + reg32(rd) + ", " + reg32(rs) : "mov " + rd + ", " + rs);
        finish(in.dst, spill, dstFloat, rd);
        break;
    }
    case IrInst::LoadLocal: case IrInst::LoadGlobal: case IrInst::LoadMem: {
        if (in.dst >= 0 && byteInMemory(in.dst)) break;   // the branch right after tests the byte where it is
        int size = typeSize(in.type);
        if (in.op == IrInst::LoadLocal && in.args[0].id >= 0 && in.args[0].id < m_f.paramCount && m_paramInReg[static_cast<size_t>(in.args[0].id)]) {
            // the parameter is still in its argument register; narrow integers are extended as a load would
            // (the caller defines only the low bytes). A spilled destination goes through rax / xmm5, which
            // hold no parameter.
            static const char* const argRegs[] = { "rcx", "rdx", "r8", "r9" };
            const int p = in.args[0].id;
            std::string rd = def(in.dst, dstFloat, dstFloat ? "xmm5" : "rax", spill);
            if (dstFloat) {
                const std::string src = "xmm" + intToString(p);
                if (rd != src) line(std::string(isSingle(in.type) ? "movss " : "movsd ") + rd + ", " + src + "      ; param " + m_f.locals[static_cast<size_t>(p)].name);
            } else {
                const std::string src = argRegs[p];
                const bool zero = in.type && (in.type->isUnsigned || in.type->kind == Type::Bool);
                if (size == 8) { if (rd != src) line("mov " + rd + ", " + src + "      ; param " + m_f.locals[static_cast<size_t>(p)].name); }
                else if (size == 4) { if (rd != src) line("mov " + reg32(rd) + ", " + reg32(src) + "      ; param " + m_f.locals[static_cast<size_t>(p)].name); }
                else line(std::string(zero ? "movzx " : "movsx ") + reg32(rd) + ", " + regSized(src, size) + "      ; param " + m_f.locals[static_cast<size_t>(p)].name);
            }
            finish(in.dst, spill, dstFloat, rd);
            break;
        }
        std::string mem;
        if (in.op == IrInst::LoadLocal) mem = localRef(in.args[0].id);
        else if (in.op == IrInst::LoadGlobal) { mem = "[" + m_m.globalSymbol(in.args[0].name) + "]"; m_m.useGlobal(in.args[0].name); }
        else mem = memOperand(in.args[0]);
        std::string rd = def(in.dst, dstFloat, dstFloat ? "xmm0" : "rax", spill);
        if (dstFloat) line(std::string(isSingle(in.type) ? "movss " : "movsd ") + rd + ", " + (isSingle(in.type) ? "real4 ptr " : "real8 ptr ") + mem);
        else if (size == 4 && m_returnDirect.count(in.dst)) line("mov eax, dword ptr " + mem);   // returned as is: only eax counts
        else if (size == 8) line("mov " + rd + ", qword ptr " + mem);
        else if (size == 4) line("mov " + reg32(rd) + ", dword ptr " + mem);
        else line(std::string(in.type && (in.type->isUnsigned || in.type->kind == Type::Bool) ? "movzx " : "movsx ") + reg32(rd) + ", " + ptrWord(size) + " " + mem);
        finish(in.dst, spill, dstFloat, rd);
        break;
    }
    case IrInst::StoreLocal: case IrInst::StoreGlobal: case IrInst::StoreMem: {
        int size = typeSize(in.type);
        bool fl = opIsFloat(in.args[1]);
        std::string mem;
        if (in.op == IrInst::StoreLocal) mem = localRef(in.args[0].id);
        else if (in.op == IrInst::StoreGlobal) { mem = "[" + m_m.globalSymbol(in.args[0].name) + "]"; m_m.useGlobal(in.args[0].name); }
        else mem = memOperand(in.args[0]);
        const bool zeroExtended = in.args[1].type && in.args[1].type->isUnsigned && typeSize(in.args[1].type) == 4 && in.args[1].ival < 0;
        if (!fl && in.args[1].kind == IrOperand::Imm && !(size == 8 && zeroExtended)) {
            // a constant is stored directly (a 64-bit slot takes the sign-extended 32-bit immediate)
            line("mov " + std::string(ptrWord(size)) + " " + mem + ", " + intToString(static_cast<int>(in.args[1].ival)));
            break;
        }
        std::string rv = use(in.args[1], fl ? "xmm0" : "rax");
        if (fl) line(std::string(isSingle(in.type) ? "movss " : "movsd ") + (isSingle(in.type) ? "real4 ptr " : "real8 ptr ") + mem + ", " + rv);
        else line("mov " + std::string(ptrWord(size)) + " " + mem + ", " + regSized(rv, size));
        break;
    }
    case IrInst::AddrLocal: { std::string rd = def(in.dst, false, "rax", spill); line("lea " + rd + ", " + localRef(in.args[0].id)); finish(in.dst, spill, false, rd); break; }
    case IrInst::AddrGlobal: { std::string rd = def(in.dst, false, "rax", spill); m_m.useGlobal(in.args[0].name); line("lea " + rd + ", " + m_m.globalSymbol(in.args[0].name)); finish(in.dst, spill, false, rd); break; }
    case IrInst::AddrField: {
        if (in.dst >= 0 && m_folded.count(in.dst)) break;   // the next load or store uses [base+offset] itself
        std::string rb = use(in.args[0], "rdx"); std::string rd = def(in.dst, false, "rax", spill); line("lea " + rd + ", [" + rb + "+" + intToString(static_cast<int>(in.extra)) + "]      ; " + in.sub); finish(in.dst, spill, false, rd); break; }
    case IrInst::AddrIndex: {
        std::string rb = use(in.args[0], "rdx");
        long size = in.extra > 0 ? in.extra : 1;
        if (indexImmediate(in)) {
            // a constant index: the offset is in the instruction (an add when the result takes the base's register), as
            // cl steps a pointer
            const long off = in.args[1].ival * size;
            std::string rd = def(in.dst, false, "rax", spill);
            if (off == 0) { if (rd != rb) line("mov " + rd + ", " + rb); }
            else if (rd == rb) line(std::string(off > 0 ? "add " : "sub ") + rd + ", " + intToString(static_cast<int>(off > 0 ? off : -off)));
            else line("lea " + rd + ", [" + rb + (off > 0 ? "+" : "-") + intToString(static_cast<int>(off > 0 ? off : -off)) + "]");
            finish(in.dst, spill, false, rd);
            break;
        }
        std::string ri = use(in.args[1], "rcx");
        const int isz = typeSize(in.args[1].type);
        if (isz > 0 && isz < 8) {
            // the index as 64 bits: a narrow value holds its low 32 bits only, extended here by its type (a 1- or 2-byte
            // one is promoted to 32 already)
            const std::string x = rb == "rax" ? "rcx" : "rax";   // (a scratch the base does not hold)
            line((in.args[1].type && in.args[1].type->isUnsigned ? "mov " + reg32(x) : "movsxd " + x) + ", " + reg32(ri));
            ri = x;
        }
        std::string rd = def(in.dst, false, "rax", spill);
        if (size == 1 || size == 2 || size == 4 || size == 8) line("lea " + rd + ", [" + rb + "+" + ri + "*" + intToString(static_cast<int>(size)) + "]");
        else { line("imul rax, " + ri + ", " + intToString(static_cast<int>(size))); line("add rax, " + rb); if (rd != "rax") line("mov " + rd + ", rax"); }   // (rd may be the base's register: read first)
        finish(in.dst, spill, false, rd);
        break;
    }
    case IrInst::Bin: {
        const std::string& s = in.sub;
        if (s[0] == 'f') {
            std::string a = use(in.args[0], "xmm0"), b = use(in.args[1], "xmm1");
            std::string rd = def(in.dst, true, "xmm0", spill);
            bool single = isSingle(in.type);
            std::string mn = s == "fadd" ? "adds" : s == "fsub" ? "subs" : s == "fmul" ? "muls" : "divs";
            mn += single ? "s" : "d";
            if (rd == b && rd != a) { line("movsd xmm5, " + b); b = "xmm5"; }   // (xmm2 may hold a value: a volatile register of the pool)
            if (rd != a) line(std::string(single ? "movss " : "movsd ") + rd + ", " + a);
            line(mn + " " + rd + ", " + b);
            finish(in.dst, spill, true, rd);
            break;
        }
        // a 4-byte value (or a narrower one, promoted to 32 bits) is computed with the 32-bit forms, as cl does
        const bool w32 = typeSize(in.type) > 0 && typeSize(in.type) <= 4;
        if (s == "sdiv" || s == "udiv" || s == "srem" || s == "urem") {
            std::string a = use(in.args[0], "rax"), b = use(in.args[1], "rcx");
            if (a != "rax") line(w32 ? "mov eax, " + reg32(a) : "mov rax, " + a);
            if (b == "rax" || b == "rdx") { line(w32 ? "mov ecx, " + reg32(b) : "mov rcx, " + b); b = "rcx"; }
            if (s[0] == 's') { line(w32 ? "cdq" : "cqo"); line("idiv " + (w32 ? reg32(b) : b)); } else { line("xor edx, edx"); line("div " + (w32 ? reg32(b) : b)); }
            std::string rd = def(in.dst, false, "rax", spill);
            std::string src = (s == "srem" || s == "urem") ? "rdx" : "rax";
            if (rd != src) line(w32 ? "mov " + reg32(rd) + ", " + reg32(src) : "mov " + rd + ", " + src);
            extend(rd, in.type);
            finish(in.dst, spill, false, rd);
            break;
        }
        if (s == "shl" || s == "ashr" || s == "lshr") {
            std::string a = use(in.args[0], "rax");
            std::string rd = def(in.dst, false, "rax", spill);
            const std::string rdw = w32 ? reg32(rd) : rd, aw = w32 ? reg32(a) : a;
            if (in.args[1].kind == IrOperand::Imm) {
                if (rd != a) line("mov " + rdw + ", " + aw);
                line(std::string(s == "shl" ? "shl " : s == "ashr" ? "sar " : "shr ") + rdw + ", " + intToString(static_cast<int>(in.args[1].ival)));
            } else {
                std::string b = use(in.args[1], "rcx");
                if (b != "rcx") line("mov ecx, " + reg32(b));   // the count: cl reads its low byte
                if (rd != a) line("mov " + rdw + ", " + aw);
                line(std::string(s == "shl" ? "shl " : s == "ashr" ? "sar " : "shr ") + rdw + ", cl");
            }
            extend(rd, in.type);
            finish(in.dst, spill, false, rd);
            break;
        }
        std::string mn = s == "add" ? "add" : s == "sub" ? "sub" : s == "mul" ? "imul" : s == "and" ? "and" : s == "or" ? "or" : "xor";
        if (immediateOperand(in.args[1], typeSize(in.type))) {
            // a constant second operand is encoded in the instruction, as cl, clang and gcc do: add rd, 1
            std::string a = use(in.args[0], "rax");
            std::string rd = def(in.dst, false, "rax", spill);
            const std::string rdw = w32 ? reg32(rd) : rd, aw = w32 ? reg32(a) : a;
            const std::string imm = intToString(static_cast<int>(in.args[1].ival));
            const long iv = in.args[1].ival;
            if (mn == "imul") line("imul " + rdw + ", " + aw + ", " + imm);
            else if ((mn == "add" || mn == "sub") && rd != a && iv > -2147483647L && iv < 2147483647L) {
                // into another register: one lea (the low 32 bits of the sum are the 32-bit result)
                const bool plus = (mn == "add") == (iv >= 0);
                line("lea " + rdw + ", [" + a + (plus ? "+" : "-") + intToString(static_cast<int>(iv >= 0 ? iv : -iv)) + "]");
            }
            else { if (rd != a) line("mov " + rdw + ", " + aw); line(mn + " " + rdw + ", " + imm); }
            extend(rd, in.type);
            finish(in.dst, spill, false, rd);
            break;
        }
        std::string a = use(in.args[0], "rax"), b = use(in.args[1], "rdx");
        std::string rd = def(in.dst, false, "rax", spill);
        if (rd == b && rd != a) { if (mn == "add" || mn == "imul" || mn == "and" || mn == "or" || mn == "xor") std::swap(a, b); else { line(w32 ? "mov edx, " + reg32(b) : "mov rdx, " + b); b = "rdx"; } }
        const std::string rdw = w32 ? reg32(rd) : rd;
        if (mn == "add" && rd != a) line("lea " + rdw + ", [" + a + "+" + b + "]");   // (rd is not b either: exchanged above)
        else {
            if (rd != a) line("mov " + rdw + ", " + (w32 ? reg32(a) : a));
            line(mn + " " + rdw + ", " + (w32 ? reg32(b) : b));
        }
        extend(rd, in.type);
        finish(in.dst, spill, false, rd);
        break;
    }
    case IrInst::Un: {
        if (in.sub == "fneg") {
            std::string a = use(in.args[0], "xmm0");
            std::string rd = def(in.dst, true, "xmm0", spill);
            if (rd != a) line("movsd " + rd + ", " + a);
            line("xorpd " + rd + ", xmmword ptr [sign_mask]");
            finish(in.dst, spill, true, rd);
            break;
        }
        std::string a = use(in.args[0], "rax");
        std::string rd = def(in.dst, false, "rax", spill);
        const bool w32 = typeSize(in.type) > 0 && typeSize(in.type) <= 4;
        if (rd != a) line(w32 ? "mov " + reg32(rd) + ", " + reg32(a) : "mov " + rd + ", " + a);
        line(std::string(in.sub == "neg" ? "neg " : "not ") + (w32 ? reg32(rd) : rd));
        extend(rd, in.type);
        finish(in.dst, spill, false, rd);
        break;
    }
    case IrInst::Cmp: {
        bool fl = in.sub[0] == 'f';
        std::string cc;
        std::string s = fl ? in.sub.substr(1) : in.sub;
        if (fl) {
            std::string a = use(in.args[0], "xmm0"), b = use(in.args[1], "xmm1");
            // ucomiss / ucomisd (by the operands' type) set CF/ZF like unsigned compares: above/below conditions,
            // which are false when unordered (ZF = PF = CF = 1); == and != also test PF: NaN compares unequal
            const Type* at = in.args[0].type ? in.args[0].type : m_f.typeOf(in.args[0].kind == IrOperand::Temp ? in.args[0].id : -1);
            if (s == "lt" || s == "le") { std::swap(a, b); s = (s == "lt") ? "gt" : "ge"; }
            line(std::string(isSingle(at) ? "ucomiss " : "ucomisd ") + a + ", " + b);
            cc = s == "eq" ? "fe" : s == "ne" ? "fne" : s == "gt" ? "a" : "ae";
            if (!m_fusedCmp.count(in.dst) && (cc == "fe" || cc == "fne")) {
                // rd = unordered (PF) ? the != result : ZF's; mov leaves the flags alone
                std::string rd = def(in.dst, false, "rax", spill);
                const std::string skip = m_label + "_P" + intToString(++m_parityLabels);
                line("mov " + reg32(rd) + ", " + (cc == "fne" ? "1" : "0"));
                line("jp " + skip);
                line(std::string(cc == "fne" ? "setne " : "sete ") + reg8(rd));
                m_out << skip << ":\n";
                finish(in.dst, spill, false, rd);
                break;
            }
        } else {
            int size = typeSize(in.args[0].type ? in.args[0].type : m_f.typeOf(in.args[0].kind == IrOperand::Temp ? in.args[0].id : -1));
            std::string a = use(in.args[0], "rax");
            const bool imm = immediateOperand(in.args[1], size);
            std::string b = imm ? intToString(static_cast<int>(in.args[1].ival)) : use(in.args[1], "rdx");
            line("cmp " + regSized(a, size) + ", " + (imm ? b : regSized(b, size)));
            cc = s == "eq" ? "e" : s == "ne" ? "ne" : s == "slt" ? "l" : s == "sle" ? "le" : s == "sgt" ? "g" : s == "sge" ? "ge" : s == "ult" ? "b" : s == "ule" ? "be" : s == "ugt" ? "a" : "ae";
        }
        if (m_fusedCmp.count(in.dst)) { m_pendingCc = cc; break; }   // the next instruction branches on the flags
        std::string rd = def(in.dst, false, "rax", spill);
        line("set" + cc + " " + reg8(rd));
        line("movzx " + reg32(rd) + ", " + reg8(rd));
        finish(in.dst, spill, false, rd);
        break;
    }
    case IrInst::Conv: {
        const std::string& s = in.sub;
        if (s == "sitofp" || s == "uitofp") {
            std::string a = use(in.args[0], "rax");
            std::string rd = def(in.dst, true, "xmm0", spill);
            const std::string cvt = isSingle(in.type) ? "cvtsi2ss " : "cvtsi2sd ";
            if (s == "uitofp" && typeSize(in.args[0].type) == 8) {
                // unsigned long long as cl: with the top bit set, half of it (the lost bit kept as a sticky bit)
                // converted and doubled
                const std::string big = m_label + "_U" + intToString(in.dst) + "b", even = m_label + "_U" + intToString(in.dst) + "e",
                                  done = m_label + "_U" + intToString(in.dst) + "d";
                line("test " + a + ", " + a);
                line("js " + big);
                line(cvt + rd + ", " + a);
                line("jmp " + done);
                m_out << big << ":\n";
                if (a != "rax") line("mov rax, " + a);
                line("shr rax, 1");
                line("jnc " + even);
                line("or rax, 1");
                m_out << even << ":\n";
                line(cvt + rd + ", rax");
                line(std::string(isSingle(in.type) ? "addss " : "addsd ") + rd + ", " + rd);
                m_out << done << ":\n";
            } else if (typeSize(in.args[0].type) == 4 && in.args[0].type && in.args[0].type->isUnsigned) {
                // unsigned int: zero-extended to 64 bits (the upper 32 of its register are unspecified), converted as such
                line("mov eax, " + reg32(a));
                line(cvt + rd + ", rax");
            } else line(cvt + rd + ", " + (typeSize(in.args[0].type) < 8 ? reg32(a) : a));   // (a narrower value is promoted to 32 bits)
            finish(in.dst, spill, true, rd);
            break;
        }
        if (s == "fptosi") {
            // as cl: a signed or narrow target converts through 32 bits (out of range: 80000000h, then truncated),
            // unsigned int through 64 bits, unsigned long long through cl's two-range sequence
            std::string a = use(in.args[0], "xmm0");
            std::string rd = def(in.dst, false, "rax", spill);
            const int to = typeSize(in.type);
            const bool uns = in.type && in.type->isUnsigned;
            const std::string cvt = isSingle(in.args[0].type) ? "cvttss2si " : "cvttsd2si ";
            if (to == 8 && uns) {
                // x < 2^63 (or NaN): cvttsd2si x; else y = x - 2^63 and cvttsd2si y, plus 2^63 when y < 2^63
                if (isSingle(in.args[0].type)) { line("cvtss2sd xmm0, " + a); a = "xmm0"; }
                const std::string small = m_label + "_U" + intToString(in.dst) + "s", done = m_label + "_U" + intToString(in.dst) + "d";
                line("movsd xmm1, real8 ptr [" + floatConst(9223372036854775808.0) + "]");
                line("comisd " + a + ", xmm1");
                line("jb " + small);
                if (a != "xmm0") line("movsd xmm0, " + a);
                line("subsd xmm0, xmm1");
                line("comisd xmm0, xmm1");
                line("cvttsd2si " + rd + ", xmm0");
                line("jae " + done);
                line("rol " + rd + ", 1");    // rd < 2^63 here: setting bit 63 adds 2^63
                line("or " + rd + ", 1");
                line("ror " + rd + ", 1");
                line("jmp " + done);
                m_out << small << ":\n";
                line("cvttsd2si " + rd + ", " + a);
                m_out << done << ":\n";
            } else if (to == 8 || (to == 4 && uns)) {
                line(cvt + rd + ", " + a);   // (unsigned int through 64 bits: its low 32 are the value)
            } else {
                line(cvt + reg32(rd) + ", " + a);
                if (to == 2 || to == 1) line(std::string(uns ? "movzx " : "movsx ") + reg32(rd) + ", " + regSized(rd, to));
            }
            finish(in.dst, spill, false, rd);
            break;
        }
        if (s == "fpext" || s == "fptrunc") {
            std::string a = use(in.args[0], "xmm0");
            std::string rd = def(in.dst, true, "xmm0", spill);
            line(std::string(s == "fpext" ? "cvtss2sd " : "cvtsd2ss ") + rd + ", " + a);
            finish(in.dst, spill, true, rd);
            break;
        }
        std::string a = use(in.args[0], "rax");
        std::string rd = def(in.dst, false, "rax", spill);
        int from = typeSize(in.args[0].type), to = typeSize(in.type);
        if (s == "sext" && from < 8) {
            // to 8 bytes: the extension proper; to 4 from a narrower value (promoted to 32 already) or to a narrower one: the
            // 32-bit promotion by the target's signedness (nothing between 4-byte types)
            if (to == 8) line(std::string(from == 4 ? "movsxd " : "movsx ") + rd + ", " + regSized(a, from));
            else if (to < 4) line("movsx " + reg32(rd) + ", " + regSized(a, to));
            else if (from < 4) line("movsx " + reg32(rd) + ", " + regSized(a, from));
            else if (rd != a) line("mov " + reg32(rd) + ", " + reg32(a));
        }
        else if (s == "zext" && from < 8) {
            if (to == 8) { if (from == 4) line("mov " + reg32(rd) + ", " + reg32(a)); else line("movzx " + rd + ", " + regSized(a, from)); }
            else if (to < 4) line("movzx " + reg32(rd) + ", " + regSized(a, to));
            else if (from < 4) line("movzx " + reg32(rd) + ", " + regSized(a, from));
            else if (rd != a) line("mov " + reg32(rd) + ", " + reg32(a));
        }
        else if (s == "trunc") {
            // the low bytes are the value: a 4-byte result needs nothing, a narrower one its 32-bit promotion
            const bool zero = in.type && (in.type->isUnsigned || in.type->kind == Type::Bool);
            if (to == 2 || to == 1) line(std::string(zero ? "movzx " : "movsx ") + reg32(rd) + ", " + regSized(a, to));
            else if (to == 4) { if (rd != a) line("mov " + reg32(rd) + ", " + reg32(a)); }
            else if (rd != a) line("mov " + rd + ", " + a);
        }
        else if (rd != a) line("mov " + rd + ", " + a);
        finish(in.dst, spill, false, rd);
        break;
    }
    case IrInst::Call: case IrInst::VCall: emitCall(in); break;
    case IrInst::Br: if (in.args[0].id != m_next[static_cast<size_t>(blockIndex)]) line("jmp " + label(in.args[0].id)); break;
    case IrInst::CondBr: {
        // the condition: the flags of the comparison just emitted (fused), a byte tested where it is, or a 0/1 value
        // tested here
        std::string cc = "ne";
        std::map<int, ByteTest>::const_iterator bt = in.args[0].kind == IrOperand::Temp ? m_byteTest.find(in.args[0].id) : m_byteTest.end();
        if (in.args[0].kind == IrOperand::Temp && m_fusedCmp.count(in.args[0].id)) cc = m_pendingCc;
        else if (bt != m_byteTest.end()) {
            static const char* const argRegs[] = { "rcx", "rdx", "r8", "r9" };
            const ByteTest& b = bt->second;
            if (b.kind == 1) { std::string rc = use(in.args[0], "rax"); line("test " + reg8(rc) + ", " + reg8(rc)); }
            else if (b.kind == 2 && b.local < m_f.paramCount && m_paramInReg[static_cast<size_t>(b.local)]) line("test " + reg8(argRegs[b.local]) + ", " + reg8(argRegs[b.local]) + "      ; param " + m_f.locals[static_cast<size_t>(b.local)].name);
            else if (b.kind == 2) line("cmp byte ptr " + localRef(b.local) + ", 0");
            else if (b.kind == 3) line("cmp byte ptr " + memOperand(b.mem) + ", 0");
            else { line("cmp byte ptr [" + m_m.globalSymbol(b.name) + "], 0"); m_m.useGlobal(b.name); }
        }
        else { std::string rc = use(in.args[0], "rax"); line("test " + reg32(rc) + ", " + reg32(rc)); }
        const int t = in.args[1].id, f = in.args[2].id;
        if (cc == "fe" || cc == "fne") {
            // floating == / !=: unordered (PF) is false for ==, true for !=, whatever ZF says
            const bool eq = cc == "fe";
            line("jp " + label(eq ? f : t));
            if (t == m_next[static_cast<size_t>(blockIndex)]) line(std::string(eq ? "jne " : "je ") + label(f));
            else {
                line(std::string(eq ? "je " : "jne ") + label(t));
                if (f != m_next[static_cast<size_t>(blockIndex)]) line("jmp " + label(f));
            }
            break;
        }
        if (t == m_next[static_cast<size_t>(blockIndex)]) line("j" + invertCc(cc) + " " + label(f));   // fall through into the true block
        else {
            line("j" + cc + " " + label(t));
            if (f != m_next[static_cast<size_t>(blockIndex)]) line("jmp " + label(f));
        }
        break;
    }
    case IrInst::Ret: {
        if (!in.args.empty()) {
            bool fl = opIsFloat(in.args[0]);
            std::string r = use(in.args[0], fl ? "xmm0" : "rax");
            if (fl && r != "xmm0") line("movsd xmm0, " + r);
            else if (!fl && r != "rax") line("mov rax, " + r);
        }
        // the exit code follows the last instruction of the last block: no jump needed there
        const IrBlock& last = m_f.blocks[static_cast<size_t>(m_order[m_hotCount > 0 ? m_hotCount - 1 : 0])];
        if (last.insts.empty() || &in != &last.insts.back()) line("jmp " + m_label + "_exit");
        break;
    }
    case IrInst::Throw: {
        // cppparse_throw(object address, type descriptor, size): copies the object and longjmps to the innermost frame
        if (in.args.empty()) { m_m.useExtern("cppparse_rethrow"); line("call cppparse_rethrow            ; throw;"); break; }
        const Type* t = in.args[0].type;
        bool cls = t && t->kind == Type::Class;
        std::string slot = "[rbp-" + intToString(m_throwSlot) + "]";
        if (cls) { std::string r = use(in.args[0], "rcx"); if (r != "rcx") line("mov rcx, " + r); }
        else if (opIsFloat(in.args[0])) { std::string r = use(in.args[0], "xmm0"); line("movsd real8 ptr " + slot + ", " + r); line("lea rcx, " + slot); }
        else { std::string r = use(in.args[0], "rax"); line("mov qword ptr " + slot + ", " + r); line("lea rcx, " + slot); }
        line("lea rdx, " + m_m.stringLabel("\"" + in.sub + "\""));
        line("mov r8, " + intToString(static_cast<int>(in.extra > 0 ? in.extra : 8)));
        if (in.args.size() > 1) {
            // version 6: the object was copy-constructed into malloc storage, args[1] is its destructor or 0
            std::string d = use(in.args[1], "r9");
            if (d != "r9") line("mov r9, " + d);
            m_m.useExtern("cppparse_throw_owned");
            line("call cppparse_throw_owned");
            break;
        }
        m_m.useExtern("cppparse_throw");
        line("call cppparse_throw");
        break;
    }
    case IrInst::TryBegin: {
        if (m_tableEh) { std::vector<long> now = m_regionNow; now.push_back(in.extra); setRegions(now); break; }
        // register the frame, then _setjmp(frame, NULL): 0 now, 1 when a throw lands here
        std::string fr = "[rbp-" + intToString(m_tryOffset[in.extra]) + "]";
        m_m.useExtern("cppparse_push_frame");
        m_m.useExtern("_setjmp");
        line("lea rcx, " + fr);
        line("call cppparse_push_frame");
        line("lea rcx, " + fr);
        line("xor edx, edx");
        line("call _setjmp");
        line("test eax, eax");
        line("jne " + m_label + "_T" + intToString(static_cast<int>(in.extra)) + "        ; exception: dispatch to the handlers");
        break;
    }
    case IrInst::TryEnd: {
        if (m_tableEh) { std::vector<long> now = m_regionNow; if (!now.empty()) now.pop_back(); setRegions(now); break; }
        m_m.useExtern("cppparse_pop_frame");
        line("lea rcx, [rbp-" + intToString(m_tryOffset[in.extra]) + "]");
        line("call cppparse_pop_frame");
        break;
    }
    case IrInst::CatchParam: {
        std::string rd = def(in.dst, false, "rax", spill);
        m_m.useExtern("cppparse_catch_begin");
        line("call cppparse_catch_begin       ; catch (" + in.sub + "): rax = object");
        if (rd != "rax") line("mov " + rd + ", rax");
        finish(in.dst, spill, false, rd);
        break;
    }
    case IrInst::CatchEnd: { m_m.useExtern("cppparse_catch_end"); line("call cppparse_catch_end"); break; }
    case IrInst::Unreachable:
        // after a call that does not return (a landing pad's cppparse_unwind_destroy): no code of its own; should the
        // function's code end with that call, or with a throw, emitBody adds int 3, which keeps the call's return
        // address inside the function, where the stack walk of an exception looks it up (as cl and clang end such a
        // function; before, the dispatch stubs followed every landing pad)
        break;
    default: break;
    }
}

// the type a handler block catches: its catchparam's, "..." without one (catch (...), an object's landing pad)
std::string X86Function::catchType(int hb) const
{
    const IrBlock& blk = m_f.blocks[static_cast<size_t>(hb)];
    for (size_t k = 0; k < blk.insts.size(); ++k) if (blk.insts[k].op == IrInst::CatchParam) return blk.insts[k].sub;
    return "...";
}

void X86Function::emitBody()
{
    if (m_f.linkOnce) m_out << ";@comdat any\n";   // read by the COFF writer: the function gets its own COMDAT section
    // a function with a frame describes its prologue with the MASM unwind directives (.pushreg, .allocstack,
    // .savexmm128, .endprolog): the assembler turns them into the Windows unwind tables (.xdata, .pdata) through
    // which a debugger or an exception walks from this function to its caller. A frameless leaf needs none.
    // With table-based try regions the unwind info also names the runtime's handler, followed by the ranges.
    if (m_tableEh) m_m.useExtern("cppparse_eh_handler");
    m_out << m_label << (m_frameless ? " PROC\n" : m_tableEh ? " PROC FRAME:cppparse_eh_handler\n" : " PROC FRAME\n");
    m_out << "        ; " << m_f.name << "\n";
    if (!m_f.sourceFile.empty()) {
        // CodeView, read by the COFF writer (comments to ml64): the function's source file and the name its symbol
        // shows, then its own line at offset 0 (the prologue); emitInst marks every change of line after it
        m_out << ";@debug \"" << m_f.sourceFile << "\" " << displayName(m_f.name) << "\n";
        if (m_f.sourceLine > 0) { m_out << ";@line " << m_f.sourceLine << "\n"; m_lastLine = m_f.sourceLine; }
    }
    m_out << "        ; frame " << m_frameSize << " bytes";
    for (size_t l = 0; l < m_f.locals.size(); ++l) {
        if (l && l % 6 == 0) m_out << "\n        ;   ";
        m_out << (l ? ", " : ": ") << m_f.locals[l].name << "@" << localRef(static_cast<int>(l));
    }
    m_out << "\n";
    if (m_frameless) {
        // nothing lives in memory and nothing is called: no frame, the parameters stay in their registers
        for (size_t o = 0; o < m_order.size(); ++o) {
            if (o == m_hotCount) { m_out << m_label << "_exit:\n"; line("ret"); }
            const size_t b = static_cast<size_t>(m_order[o]);
            m_out << label(static_cast<int>(b)) << ":" << (m_f.blocks[b].label.empty() ? "" : "        ; " + m_f.blocks[b].label) << "\n";
            for (size_t i = 0; i < m_f.blocks[b].insts.size(); ++i) emitInst(m_f.blocks[b].insts[i], static_cast<int>(b));
        }
        if (m_hotCount >= m_order.size()) { m_out << m_label << "_exit:\n"; line("ret"); }
        if (m_openEnd) line("int 3");
        m_out << m_label << " ENDP\n\n";
        return;
    }
    // rsp does not move in the body, so the unwind codes describe the frame from rsp alone: rbp is declared as a
    // pushed register, not as a frame register (whose saves could only lie above it, where ours are not)
    line("push rbp");
    directive(".pushreg rbp");
    line("mov rbp, rsp");
    for (std::set<int>::iterator r = m_usedIntRegs.begin(); r != m_usedIntRegs.end(); ++r) {
        line("push " + std::string(kIntRegs[*r]));
        directive(".pushreg " + std::string(kIntRegs[*r]));
    }
    // the rest of the frame below the pushed registers (the frame size counts them, so rsp stays 16-aligned)
    const int below = m_frameSize - 8 * static_cast<int>(m_usedIntRegs.size());
    if (below >= 4096) {
        // a frame of a page or more is probed page by page, so the stack guard page grows the stack in order
        m_m.useExtern("__chkstk");
        line("mov eax, " + intToString(below));
        line("call __chkstk");
        line("sub rsp, rax");
    } else if (below > 0) line("sub rsp, " + intToString(below));
    if (below > 0) directive(".allocstack " + intToString(below));
    // [rbp-x] is [rsp+frameSize-x] once the frame is allocated; x is a multiple of 16 and rbp is 16-aligned
    for (std::map<int, int>::iterator r = m_savedFloatOffset.begin(); r != m_savedFloatOffset.end(); ++r) {
        line("movdqu xmmword ptr [rbp-" + intToString(r->second) + "], xmm" + intToString(kFloatRegBase + r->first));
        directive(".savexmm128 xmm" + intToString(kFloatRegBase + r->first) + ", " + intToString(m_frameSize - r->second));
    }
    directive(".endprolog");
    // incoming parameters by position
    static const char* const argRegs[] = { "rcx", "rdx", "r8", "r9" };
    if (m_f.variadic) {
        // the four register arguments go to their home slots, the named ones in their own type, the unnamed ones
        // from the integer registers (a caller passes a double there as well); the rest are on the stack already
        for (int p = 0; p < 4; ++p) {
            const std::string home = "[rbp+" + intToString(16 + 8 * p) + "]";
            const IrLocal* l = p < m_f.paramCount ? &m_f.locals[static_cast<size_t>(p)] : 0;
            if (l && isFloatType(l->type)) line(std::string(isSingle(l->type) ? "movss real4 ptr " : "movsd real8 ptr ") + home + ", xmm" + intToString(p) + "      ; param " + l->name);
            else line("mov qword ptr " + home + ", " + argRegs[p] + (l ? "      ; param " + l->name : std::string("      ; variadic")));
        }
    }
    for (int p = 0; p < m_f.paramCount && !m_f.variadic; ++p) {
        // a parameter read straight from its register, or never read at all, needs no copy in the frame
        if (m_paramInReg[static_cast<size_t>(p)] || !m_paramUsed[static_cast<size_t>(p)]) continue;
        const IrLocal& l = m_f.locals[static_cast<size_t>(p)];
        bool fl = isFloatType(l.type);
        int size = l.type && l.type->kind != Type::Reference && l.type->kind != Type::Class ? typeSize(l.type) : 8;
        if (p < 4) {
            if (fl) line(std::string(isSingle(l.type) ? "movss " : "movsd ") + (isSingle(l.type) ? "real4 ptr " : "real8 ptr ") + localRef(p) + ", xmm" + intToString(p) + "      ; param " + l.name);
            else line("mov " + std::string(ptrWord(size)) + " " + localRef(p) + ", " + regSized(argRegs[p], size) + "      ; param " + l.name);
        } else {
            std::string home = "[rbp+" + intToString(16 + 8 * p) + "]";
            // a float passed on the stack fills the low 4 bytes of its 8-byte slot, and its frame slot holds 4 bytes.
            // The copy goes through xmm5 (rax for integers), which holds no parameter: xmm0-xmm3 may still be read
            if (fl && isSingle(l.type)) { line("movss xmm5, real4 ptr " + home); line("movss real4 ptr " + localRef(p) + ", xmm5      ; param " + l.name); }
            else if (fl) { line("movsd xmm5, real8 ptr " + home); line("movsd real8 ptr " + localRef(p) + ", xmm5      ; param " + l.name); }
            else { line("mov rax, qword ptr " + home); line("mov " + std::string(ptrWord(size)) + " " + localRef(p) + ", " + regSized("rax", size) + "      ; param " + l.name); }
        }
    }
    for (size_t o = 0; o < m_hotCount && o < m_order.size(); ++o) {
        const size_t b = static_cast<size_t>(m_order[o]);
        m_out << label(static_cast<int>(b)) << ":" << (m_f.blocks[b].label.empty() ? "" : "        ; " + m_f.blocks[b].label) << "\n";
        if (m_tableEh) setRegions(m_regionIn[b]);
        for (size_t i = 0; i < m_f.blocks[b].insts.size(); ++i) emitInst(m_f.blocks[b].insts[i], static_cast<int>(b));
    }
    if (m_tableEh) setRegions(std::vector<long>());
    m_out << m_label << "_exit:\n";
    for (std::map<int, int>::iterator r = m_savedFloatOffset.begin(); r != m_savedFloatOffset.end(); ++r) line("movdqu xmm" + intToString(kFloatRegBase + r->first) + ", xmmword ptr [rbp-" + intToString(r->second) + "]");
    if (m_usedIntRegs.empty()) line("leave");
    else {
        // rsp back to the pushed registers (a throw may have landed here through longjmp, which restores rbp),
        // then pop them in reverse order
        line("lea rsp, [rbp-" + intToString(8 * static_cast<int>(m_usedIntRegs.size())) + "]");
        std::vector<int> pushed(m_usedIntRegs.begin(), m_usedIntRegs.end());
        for (size_t k = pushed.size(); k-- > 0;) line("pop " + std::string(kIntRegs[pushed[k]]));
        line("pop rbp");
    }
    line("ret");
    // the cold groups: landing pads, handlers and what they alone reach, each entered with the frame intact
    for (size_t o = m_hotCount; o < m_order.size(); ++o) {
        const size_t b = static_cast<size_t>(m_order[o]);
        m_out << label(static_cast<int>(b)) << ":" << (m_f.blocks[b].label.empty() ? "" : "        ; " + m_f.blocks[b].label) << "\n";
        if (m_tableEh) setRegions(m_regionIn[b]);
        for (size_t i = 0; i < m_f.blocks[b].insts.size(); ++i) emitInst(m_f.blocks[b].insts[i], static_cast<int>(b));
    }
    if (m_tableEh) setRegions(std::vector<long>());
    // version 5: one dispatch stub per try region: pop the frame, try the catch clauses in order, else rethrow.
    // With tables the stub is the region's landing pad, and runs in the regions around the region (a rethrow
    // from it reaches the next outer landing pad)
    for (size_t b = 0; b < m_f.blocks.size(); ++b)
        for (size_t i = 0; i < m_f.blocks[b].insts.size(); ++i) {
            const IrInst& in = m_f.blocks[b].insts[i];
            if (!m_emitted[b] || in.op != IrInst::TryBegin) continue;
            const int first = in.args.empty() || in.args[0].kind != IrOperand::Block ? -1 : in.args[0].id;
            if (m_tableEh && first >= 0 && catchType(first) == "...") {
                // an object's landing pad, or catch (...) first: the stub would be one jump to the block, so the ranges
                // name the block itself
                m_padLabel[in.extra] = label(first);
                continue;
            }
            m_out << m_label << "_T" << in.extra << ":        ; handlers of try region " << in.extra << "\n";
            m_m.useExtern("cppparse_match");
            m_m.useExtern("cppparse_rethrow");
            if (m_tableEh) setRegions(m_regionOuter[in.extra]);
            else {
                m_m.useExtern("cppparse_pop_frame");
                line("lea rcx, [rbp-" + intToString(m_tryOffset[in.extra]) + "]");
                line("call cppparse_pop_frame");
            }
            bool catchAll = false;
            for (size_t h = 0; h < in.args.size() && !catchAll; ++h) {
                int hb = in.args[h].id;
                const std::string ctype = catchType(hb);
                if (ctype == "...") { line("jmp " + label(hb) + "        ; catch (...)"); catchAll = true; break; }
                line("lea rcx, " + m_m.stringLabel("\"" + ctype + "\""));
                line("call cppparse_match");
                line("test eax, eax");
                line("jne " + label(hb) + "        ; catch (" + ctype + ")");
            }
            if (!catchAll) {
                line("call cppparse_rethrow            ; no clause matched: outer frame");
                // the call does not return; int 3 keeps its return address inside this function, where the stack walk
                // of the exception looks it up (as cl and clang end a function with a call that does not return)
                line("int 3");
            }
        }
    if (m_tableEh) {
        setRegions(std::vector<long>());
        // read by the COFF writer (a comment to ml64): code range, landing pad
        for (size_t r = 0; r < m_ranges.size(); ++r) {
            const std::map<long, std::string>::const_iterator pad = m_padLabel.find(m_ranges[r].region);
            m_out << ";@ehrange " << m_ranges[r].begin << ", " << m_ranges[r].end << ", " << (pad != m_padLabel.end() ? pad->second : m_label + "_T" + intToString(static_cast<int>(m_ranges[r].region))) << "\n";
        }
    }
    if (m_openEnd) line("int 3");
    m_out << m_label << " ENDP\n\n";
}

std::string X86Function::run()
{
    m_label = m_m.symbol(m_f.name);
    layoutBlocks();
    if (m_m.tableExceptions() && m_f.hasTry) {
        std::vector<int> handlers;
        for (size_t b = 0; b < m_f.blocks.size(); ++b)
            for (size_t i = 0; i < m_f.blocks[b].insts.size(); ++i) {
                const IrInst& in = m_f.blocks[b].insts[i];
                if (in.op != IrInst::TryBegin) continue;
                for (size_t a = 0; a < in.args.size(); ++a) if (in.args[a].kind == IrOperand::Block) handlers.push_back(in.args[a].id);
            }
        // regions that are not nested the same way on every path keep the setjmp frames (the runtime handles both)
        m_tableEh = !handlers.empty() && computeRegions();
        if (m_tableEh) computeEhEdges();
        // a landing pad finds every register as it was at the call that threw, so the setjmp rules (callee-saved
        // registers only, no coalescing) are not needed, as long as a handler takes no value in a register from
        // the code before it (the optimiser keeps what handlers read in memory; checked here all the same)
        if (m_tableEh) {
            std::vector<std::set<int> > liveIn, liveOut;
            m_f.computeLiveness(liveIn, liveOut);
            m_tableRegs = true;
            for (size_t h = 0; h < handlers.size(); ++h)
                if (handlers[h] >= 0 && static_cast<size_t>(handlers[h]) < liveIn.size() && !liveIn[static_cast<size_t>(handlers[h])].empty()) m_tableRegs = false;
        }
    }
    analyzeParams();
    computeIntervals();
    const int spillsBefore = m_stats.spills;
    allocate();
    m_firstSpills = m_stats.spills - spillsBefore;
    coalesce();
    reallocateAroundCoalesced();
    hintArguments();
    resultsInRax();
    layoutFrame();
    decideFrame();
    if (m_frameless) { m_frameSize = 0; m_stats.frameSize = 0; }
    emitBody();
    return m_out.str();
}

// ---- module ---------------------------------------------------------------------------------

X86Module::X86Module(bool msvcNames) : m_msvc(msvcNames), m_tableEh(false) {}

namespace {

// the condition of an integer comparison with its operands exchanged
std::string exchangedCondition(const std::string& s)
{
    return s == "slt" ? "sgt" : s == "sgt" ? "slt" : s == "sle" ? "sge" : s == "sge" ? "sle" :
           s == "ult" ? "ugt" : s == "ugt" ? "ult" : s == "ule" ? "uge" : s == "uge" ? "ule" : s;
}

// where instruction 'in' can take the constant at operand 'a' as an immediate: the operand's position after any
// exchange, -1 when it cannot. These are the instructions whose code encodes the constant or materialises it in the
// register the operand goes to anyway: a store's value, the second operand of an arithmetic instruction or a
// comparison (a commutative operation or a comparison with the constant first gets its operands exchanged), a call
// argument, a returned value, the index of an address computation
int immediatePosition(IrInst& in, size_t a)
{
    switch (in.op) {
    case IrInst::StoreLocal: case IrInst::StoreGlobal: case IrInst::StoreMem: return a == 1 ? 1 : -1;
    case IrInst::Ret: return a == 0 ? 0 : -1;
    case IrInst::AddrIndex: return a == 1 ? 1 : -1;
    case IrInst::Call: return in.sub == "(indirect)" && a == 0 ? -1 : static_cast<int>(a);
    case IrInst::VCall: return a == 0 ? -1 : static_cast<int>(a);
    case IrInst::Bin: case IrInst::Cmp: {
        if (in.sub.empty() || in.sub[0] == 'f' || in.args.size() != 2) return -1;
        if (a == 1) return 1;
        if (in.args[1].kind != IrOperand::Temp) return 0;   // both constants: the first is materialised in a scratch register
        const std::string& s = in.sub;
        if (in.op == IrInst::Bin && s != "add" && s != "mul" && s != "and" && s != "or" && s != "xor") return -1;
        std::swap(in.args[0], in.args[1]);
        if (in.op == IrInst::Cmp) in.sub = exchangedCondition(s);
        return 1;
    }
    default: return -1;
    }
}

// a conversion of an immediate to an integer or pointer type is a constant of that type, by the optimiser's rules
// (constantFold): a value truncated to fewer than 32 bits wraps by the type's width and signedness, a bool is 0 or
// 1, and a negative 32-bit pattern zero-extended to more than 32 bits has no immediate (the IR's are 32-bit)
bool foldIntegerConv(IrInst& in)
{
    if (in.args.size() != 1 || in.args[0].kind != IrOperand::Imm || !in.type || !(in.type->isIntegral() || in.type->isPointer())) return false;
    long v = in.args[0].ival;
    if (in.sub == "zext" && v < 0 && in.type->builtinSize() > 4) return false;
    if (in.sub == "trunc") { const int bits = in.type->builtinSize() * 8; if (bits < 32) v = in.type->isUnsigned ? (v & ((1L << bits) - 1)) : ((v << (32 - bits)) >> (32 - bits)); }
    if (in.type->kind == Type::Bool) v = v != 0;
    in.op = IrInst::Const; in.sub.clear(); in.args[0] = IrOperand::imm(v, in.type);
    return true;
}

// the same value: the type pointers of a local's loads and stores may differ in qualification
bool sameValueType(const Type* a, const Type* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    return a->kind == b->kind && a->isUnsigned == b->isUnsigned && a->builtinSize() == b->builtinSize();
}

// every use of temporary t becomes operand o (with the use's own type when it has one)
void replaceTempUses(IrFunction& f, int t, const IrOperand& o)
{
    for (size_t b = 0; b < f.blocks.size(); ++b)
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) {
            IrInst& in = f.blocks[b].insts[i];
            for (size_t a = 0; a < in.args.size(); ++a)
                if (in.args[a].kind == IrOperand::Temp && in.args[a].id == t) { const Type* ty = in.args[a].type; in.args[a] = o; if (ty) in.args[a].type = ty; }
        }
}

// a value no instruction reads, computed without a side effect: an address, a constant, a copy, an arithmetic result
// (not a division, which may trap), a comparison, a conversion, or a load of a local whose address nothing holds (a
// volatile local is address-taken). IrGen leaves such loads of this, addresses of arrays and constants behind,
// which the optimiser removes from -O1 on; -O0 emitted them as mov and lea
bool unreadValue(const IrInst& in, const IrFunction& g, const std::map<int, int>& uses)
{
    if (in.dst < 0) return false;
    const std::map<int, int>::const_iterator u = uses.find(in.dst);
    if (u != uses.end() && u->second > 0) return false;
    switch (in.op) {
    case IrInst::Const: case IrInst::FConst: case IrInst::StrConst: case IrInst::Copy: case IrInst::AddrLocal: case IrInst::AddrField:
    case IrInst::AddrIndex: case IrInst::AddrGlobal: case IrInst::Un: case IrInst::Cmp: case IrInst::Conv:
        return true;
    case IrInst::Bin:
        return in.sub != "sdiv" && in.sub != "udiv" && in.sub != "srem" && in.sub != "urem";
    case IrInst::LoadLocal:
        return in.args.size() == 1 && in.args[0].kind == IrOperand::Local && in.args[0].id >= 0 && static_cast<size_t>(in.args[0].id) < g.locals.size() &&
               !g.locals[static_cast<size_t>(in.args[0].id)].addressTaken;
    default:
        return false;
    }
}

// drops the unread values, and what fed them alone, until none is left
void dropUnreadValues(IrFunction& g)
{
    for (bool changed = true; changed;) {
        changed = false;
        std::map<int, int> uses;
        for (size_t b = 0; b < g.blocks.size(); ++b)
            for (size_t i = 0; i < g.blocks[b].insts.size(); ++i)
                for (size_t a = 0; a < g.blocks[b].insts[i].args.size(); ++a)
                    if (g.blocks[b].insts[i].args[a].kind == IrOperand::Temp) ++uses[g.blocks[b].insts[i].args[a].id];
        for (size_t b = 0; b < g.blocks.size(); ++b) {
            std::vector<IrInst> kept;
            for (size_t i = 0; i < g.blocks[b].insts.size(); ++i) {
                if (unreadValue(g.blocks[b].insts[i], g, uses)) changed = true;
                else kept.push_back(g.blocks[b].insts[i]);
            }
            if (kept.size() != g.blocks[b].insts.size()) g.blocks[b].insts.swap(kept);
        }
    }
}

// -O0 lowers the IR as generated: a local assigned and read in the next statement is stored and loaded again, and
// every constant is a `const` instruction whose value goes through a register to the one instruction that reads it.
// Here, on a copy of the function (the IR itself is what the listings show), a load of a local that follows its
// store in the same block takes the stored value, as the optimiser's store-to-load forwarding does from -O1 on: the
// stored register, or the constant (then the load is a constant of its own), when no call, throw or try operation
// lies between (a value kept alive across one would need a callee-saved register, dearer than the load; nothing else
// can change a local whose address is not taken). Then a `const` defined once and read once becomes its reader's
// immediate operand where the instruction encodes or materialises it in place; a conversion of a constant to an
// integer or pointer type folds to a constant of the converted type. The loads and `const` instructions that went
// into their readers are dropped; the others keep their register. Last, the values nothing reads go (dropUnreadValues)
bool constantsIntoOperands(const IrFunction& f, IrFunction& out)
{
    std::map<int, int> defs, uses;
    std::map<int, const IrInst*> defOf;
    for (size_t b = 0; b < f.blocks.size(); ++b)
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) {
            const IrInst& in = f.blocks[b].insts[i];
            if (in.dst >= 0) { ++defs[in.dst]; defOf[in.dst] = &in; }
            for (size_t a = 0; a < in.args.size(); ++a) if (in.args[a].kind == IrOperand::Temp) ++uses[in.args[a].id];
        }
    bool any = false;
    for (size_t b = 0; b < f.blocks.size() && !any; ++b) {
        int stored = -1;   // the local of the latest store a load may take from
        for (size_t i = 0; i < f.blocks[b].insts.size() && !any; ++i) {
            const IrInst& in = f.blocks[b].insts[i];
            if (in.op == IrInst::Const && in.dst >= 0 && in.args.size() == 1 && in.args[0].kind == IrOperand::Imm && defs[in.dst] == 1 && uses[in.dst] == 1) any = true;
            else if (in.op == IrInst::StoreLocal && in.args.size() == 2 && in.args[0].kind == IrOperand::Local) stored = in.args[0].id;
            else if (in.op == IrInst::LoadLocal && in.args.size() == 1 && in.args[0].kind == IrOperand::Local && in.args[0].id == stored) any = true;
            else if (in.op == IrInst::Call || in.op == IrInst::VCall || in.op == IrInst::Throw || in.op == IrInst::TryBegin || in.op == IrInst::TryEnd || in.op == IrInst::CatchParam || in.op == IrInst::CatchEnd) stored = -1;
        }
    }
    if (!any)
        for (size_t b = 0; b < f.blocks.size() && !any; ++b)
            for (size_t i = 0; i < f.blocks[b].insts.size() && !any; ++i) if (unreadValue(f.blocks[b].insts[i], f, uses)) any = true;
    if (!any) return false;
    out = f;
    // store-to-load forwarding within a block
    std::set<int> dropped;   // the loads that took the stored value, by their result
    for (size_t b = 0; b < out.blocks.size(); ++b) {
        std::map<int, IrOperand> known;   // local -> what its latest store in this block stored
        for (size_t i = 0; i < out.blocks[b].insts.size(); ++i) {
            IrInst& in = out.blocks[b].insts[i];
            if (in.op == IrInst::Call || in.op == IrInst::VCall || in.op == IrInst::Throw || in.op == IrInst::TryBegin || in.op == IrInst::TryEnd || in.op == IrInst::CatchParam || in.op == IrInst::CatchEnd) { known.clear(); continue; }
            if (in.op == IrInst::StoreLocal && in.args.size() == 2 && in.args[0].kind == IrOperand::Local) {
                const int local = in.args[0].id;
                const bool home = out.variadic && local < out.paramCount;   // a variadic function's parameters: va_start reads them
                if (local < 0 || static_cast<size_t>(local) >= out.locals.size() || out.locals[static_cast<size_t>(local)].addressTaken || home) { known.erase(local); continue; }
                IrOperand v = in.args[1];
                if (v.kind == IrOperand::Temp) {
                    // a constant goes as the constant itself: its `const` keeps this store as its one reader
                    std::map<int, const IrInst*>::const_iterator d = defOf.find(v.id);
                    if (d != defOf.end() && defs[v.id] == 1 && (d->second->op == IrInst::Const || d->second->op == IrInst::FConst) && d->second->args.size() == 1) v = d->second->args[0];
                }
                if (v.kind == IrOperand::Temp || v.kind == IrOperand::Imm || v.kind == IrOperand::FImm) { v.type = in.type; known[local] = v; } else known.erase(local);
                continue;
            }
            if (in.op == IrInst::LoadLocal && in.args.size() == 1 && in.args[0].kind == IrOperand::Local && in.dst >= 0) {
                std::map<int, IrOperand>::iterator k = known.find(in.args[0].id);
                if (k == known.end() || !sameValueType(k->second.type, in.type) || defs[in.dst] != 1) continue;
                if (k->second.kind == IrOperand::Temp) { if (k->second.id == in.dst) continue; replaceTempUses(out, in.dst, IrOperand::temp(k->second.id, in.type)); dropped.insert(in.dst); }
                else {
                    // the stored value is a constant: the load becomes a constant definition
                    in.op = k->second.kind == IrOperand::FImm ? IrInst::FConst : IrInst::Const;
                    in.args.clear();
                    in.args.push_back(k->second);
                    in.args[0].type = in.type;
                }
            }
        }
    }
    // the counts again: the forwarding moved uses between temporaries
    defs.clear(); uses.clear();
    for (size_t b = 0; b < out.blocks.size(); ++b)
        for (size_t i = 0; i < out.blocks[b].insts.size(); ++i) {
            const IrInst& in = out.blocks[b].insts[i];
            if (in.dst >= 0 && !dropped.count(in.dst)) ++defs[in.dst];
            for (size_t a = 0; a < in.args.size(); ++a) if (in.args[a].kind == IrOperand::Temp) ++uses[in.args[a].id];
        }
    std::map<int, IrOperand> value;   // the single-use constants not yet placed: temp -> its immediate
    std::set<int> folded;             // the constants that went into their reader
    for (int round = 0; round < 4; ++round) {
        bool changed = false;
        for (size_t b = 0; b < out.blocks.size(); ++b)
            for (size_t i = 0; i < out.blocks[b].insts.size(); ++i) {
                IrInst& in = out.blocks[b].insts[i];
                if (in.op == IrInst::Const) {
                    if (in.dst >= 0 && in.args.size() == 1 && in.args[0].kind == IrOperand::Imm && defs[in.dst] == 1 && uses[in.dst] == 1 && !folded.count(in.dst)) value[in.dst] = in.args[0];
                    continue;
                }
                if (in.op == IrInst::Conv && in.args.size() == 1 && in.args[0].kind == IrOperand::Imm) {
                    // a conversion of a forwarded constant
                    if (!foldIntegerConv(in)) continue;
                    if (in.dst >= 0 && defs[in.dst] == 1 && uses[in.dst] == 1) value[in.dst] = in.args[0];
                    changed = true;
                    continue;
                }
                for (size_t a = in.args.size(); a-- > 0;) {
                    if (in.args[a].kind != IrOperand::Temp) continue;
                    std::map<int, IrOperand>::iterator v = value.find(in.args[a].id);
                    if (v == value.end()) continue;
                    const int t = in.args[a].id;
                    IrOperand imm = v->second;
                    if (in.args[a].type) imm.type = in.args[a].type;
                    if (in.op == IrInst::Conv) {
                        if (a != 0) continue;
                        IrInst trial = in;
                        trial.args[0] = imm;
                        if (!foldIntegerConv(trial)) continue;
                        in = trial;
                        if (in.dst >= 0 && defs[in.dst] == 1 && uses[in.dst] == 1) value[in.dst] = in.args[0];
                    } else {
                        const int k = immediatePosition(in, a);
                        if (k < 0) continue;
                        in.args[static_cast<size_t>(k)] = imm;
                    }
                    folded.insert(t);
                    value.erase(v);
                    changed = true;
                }
            }
        if (!changed) break;
    }
    for (size_t b = 0; b < out.blocks.size(); ++b) {
        std::vector<IrInst> kept;
        for (size_t i = 0; i < out.blocks[b].insts.size(); ++i) {
            const IrInst& in = out.blocks[b].insts[i];
            if (in.dst >= 0 && dropped.count(in.dst)) continue;
            if (!(in.op == IrInst::Const && in.dst >= 0 && folded.count(in.dst))) kept.push_back(in);
        }
        out.blocks[b].insts.swap(kept);
    }
    dropUnreadValues(out);
    return true;
}

} // namespace

void X86Module::addFunction(const IrFunction& f, LowerStats& stats) { appendCode(codeFor(f, stats)); }

std::string X86Module::codeFor(const IrFunction& f, LowerStats& stats)
{
    { Lock l(m_mutex); m_defined.insert(symbol(f.name)); if (f.internal) m_internal.insert(symbol(f.name)); }
    IrFunction folded;
    const bool any = constantsIntoOperands(f, folded);
    X86Function fn(*this, any ? folded : f, stats);
    return fn.run();
}

void X86Module::appendCode(const std::string& code) { Lock l(m_mutex); m_code += code; }

void X86Module::addThunk(const std::string& irName, const std::string& target, int adjust)
{
    if (!m_thunkNames.insert(irName).second) return;
    Thunk t; t.name = irName; t.target = target; t.adjust = adjust; t.vcall = false; t.returning = false; t.returnAdjust = 0; t.reference = false; t.stackArgs = 0; t.vtordisp = false;
    m_thunks.push_back(t);
}

void X86Module::addVcallThunk(const std::string& irName, int slotOffset)
{
    if (!m_thunkNames.insert(irName).second) return;
    Thunk t; t.name = irName; t.adjust = slotOffset; t.vcall = true; t.returning = false; t.returnAdjust = 0; t.reference = false; t.stackArgs = 0; t.vtordisp = false;
    m_thunks.push_back(t);
}

void X86Module::addReturnThunk(const std::string& irName, const std::string& target, int thisAdjust, int returnAdjust, bool reference, int stackArgs, int returnVbptr, int returnEntry)
{
    if (!m_thunkNames.insert(irName).second) return;
    Thunk t; t.name = irName; t.target = target; t.adjust = thisAdjust; t.vcall = false; t.returning = true; t.returnAdjust = returnAdjust; t.reference = reference; t.stackArgs = stackArgs; t.vtordisp = false;
    t.vbptrOffset = returnVbptr; t.entryOffset = returnEntry;
    m_thunks.push_back(t);
}

void X86Module::addVtordispThunk(const std::string& irName, const std::string& target, int adjust)
{
    if (!m_thunkNames.insert(irName).second) return;
    Thunk t; t.name = irName; t.target = target; t.adjust = adjust; t.vcall = false; t.returning = false; t.returnAdjust = 0; t.reference = false; t.stackArgs = 0; t.vtordisp = true;
    m_thunks.push_back(t);
}

void X86Module::addVtordispexThunk(const std::string& irName, const std::string& target, int vbptrOffset, int entryOffset, int nonVirtual)
{
    if (!m_thunkNames.insert(irName).second) return;
    Thunk t; t.name = irName; t.target = target; t.adjust = nonVirtual; t.vtordisp = true; t.vtordispex = true; t.vbptrOffset = vbptrOffset; t.entryOffset = entryOffset;
    m_thunks.push_back(t);
}

void X86Module::emitVtables()
{
    // the adjustor thunks the tables name (decorated names are known by now): this moves from the vfptr the
    // slot was reached through to where the overrider expects it, then a tail jump; a leaf without a frame
    for (size_t i = 0; i < m_thunks.size(); ++i) {
        const std::string sym = symbol(m_thunks[i].name);
        if (m_defined.count(sym)) continue;
        m_defined.insert(sym);
        if (m_thunks[i].vcall) {
            // a pointer to a virtual member function: the call goes through the vfptr this points at
            m_code += ";@comdat any\n" + sym + " PROC\n        ; " + m_thunks[i].name + "\n        mov rax, qword ptr [rcx]\n        jmp qword ptr [rax+" +
                      intToString(m_thunks[i].adjust) + "]\n" + sym + " ENDP\n\n";
            continue;
        }
        const std::string target = symbol(m_thunks[i].target);
        if (!m_defined.count(target)) m_externs.insert(target);
        if (m_thunks[i].vtordispex) {
            // this - vtordisp is the base where this class's layout has it; the vbptr lies vbptrOffset before it, and
            // the overrider's base at its vbtable entry from the vbptr; then the overrider's own adjustor
            const int vb = m_thunks[i].vbptrOffset;
            const int rest = m_thunks[i].adjust - vb;
            m_code += ";@comdat any\n" + sym + " PROC\n        ; " + m_thunks[i].name + "\n        movsxd rax, dword ptr [rcx-4]\n        sub rcx, rax\n" +
                      "        mov rax, qword ptr [rcx" + (vb > 0 ? "-" + intToString(vb) : vb < 0 ? "+" + intToString(-vb) : std::string()) + "]\n" +
                      "        movsxd rax, dword ptr [rax+" + intToString(m_thunks[i].entryOffset) + "]\n        add rcx, rax\n" +
                      (rest > 0 ? "        add rcx, " + intToString(rest) + "\n" : rest < 0 ? "        sub rcx, " + intToString(-rest) + "\n" : std::string()) +
                      "        jmp " + target + "\n" + sym + " ENDP\n\n";
            continue;
        }
        if (m_thunks[i].vtordisp) {
            m_code += ";@comdat any\n" + sym + " PROC\n        ; " + m_thunks[i].name + "\n        movsxd rax, dword ptr [rcx-4]\n        sub rcx, rax\n" +
                      (m_thunks[i].adjust ? "        sub rcx, " + intToString(m_thunks[i].adjust) + "\n" : std::string()) + "        jmp " + target + "\n" + sym + " ENDP\n\n";
            continue;
        }
        if (m_thunks[i].returning) {
            // the frame holds the shadow space and the stack arguments copied from the caller's (the register
            // arguments stay where they are), 16-aligned at the call; the result comes back in rax
            const Thunk& t = m_thunks[i];
            int frame = 32 + 8 * t.stackArgs;
            if (frame % 16 == 0) frame += 8;
            std::string code = ";@comdat any\n" + sym + " PROC FRAME\n        ; " + t.name + "\n        sub rsp, " + intToString(frame) + "\n        .allocstack " + intToString(frame) + "\n        .endprolog\n";
            for (int a = 0; a < t.stackArgs; ++a)
                code += "        mov rax, qword ptr [rsp+" + intToString(frame + 40 + 8 * a) + "]\n        mov qword ptr [rsp+" + intToString(32 + 8 * a) + "], rax\n";
            if (t.adjust) code += "        sub rcx, " + intToString(t.adjust) + "\n";
            code += "        call " + target + "\n";
            if (t.vbptrOffset >= 0) {
                // through a virtual base, as cl: the vbptr in the result, the base's entry of its vbtable, then the rest
                const int vb = t.vbptrOffset;
                if (!t.reference) code += "        test rax, rax\n        je " + sym + "_null\n";
                code += "        mov rcx, qword ptr [rax" + (vb ? "+" + intToString(vb) : std::string()) + "]\n        movsxd rcx, dword ptr [rcx+" + intToString(t.entryOffset) + "]\n        add rax, rcx\n";
                if (vb + t.returnAdjust) code += "        add rax, " + intToString(vb + t.returnAdjust) + "\n";
                if (!t.reference) code += sym + "_null:\n";
            }
            else if (t.returnAdjust) {
                if (!t.reference) code += "        test rax, rax\n        je " + sym + "_null\n";
                code += "        add rax, " + intToString(t.returnAdjust) + "\n";
                if (!t.reference) code += sym + "_null:\n";
            }
            m_code += code + "        add rsp, " + intToString(frame) + "\n        ret\n" + sym + " ENDP\n\n";
            continue;
        }
        m_code += ";@comdat any\n" + sym + " PROC\n        ; " + m_thunks[i].name + "\n        sub rcx, " + intToString(m_thunks[i].adjust) +
                  "\n        jmp " + target + "\n" + sym + " ENDP\n\n";
    }
    for (size_t i = 0; i < m_vtables.size(); ++i) {
        const IrVtable& v = m_vtables[i];
        std::string tableSym = globalSymbol("vtable@" + v.className);
        m_dataSymbols.insert(tableSym);
        // every translation unit that needs the table emits it, read-only as cl's; the symbol stays static: the library's
        // classes share their names with the MSVC runtime's (std::bad_cast), whose tables have other layouts
        std::string table = ";@comdat any align 8\n";
        // cl's layout: the RTTI complete object locator in the 8 bytes before the table (the vptr points at slot 0;
        // vcruntime's __RTtypeid and __RTDynamicCast read it at vptr - 8)
        if (!v.locator.empty()) { table += "        DQ OFFSET " + globalSymbol(v.locator) + "\n"; useGlobal(v.locator); }
        table += tableSym + " LABEL QWORD\n";
        for (size_t k = 0; k < v.entries.size(); ++k) {
            // a pure virtual function's slot holds vcruntime's _purecall, which reports R6025 and aborts, as cl's tables do
            std::string sym = v.entries[k].empty() ? std::string("") : v.entries[k] == "__purecall" ? std::string("_purecall") : symbol(v.entries[k]);
            if (!sym.empty() && !m_defined.count(sym)) m_externs.insert(sym);
            table += "        DQ " + (sym.empty() ? std::string("0") : "OFFSET " + sym) + "        ; [" + intToString(static_cast<int>(k)) + "] " + v.entries[k] + "\n";
        }
        m_rdata += table;
    }
}

void X86Module::emitGlobals()
{
    std::set<std::string> done;
    for (size_t i = 0; i < m_globals.size(); ++i) {
        const IrGlobal& g = m_globals[i];
        if (!done.insert(g.name).second) continue;
        std::string sym = globalSymbol(g.name);
        bool numeric = !g.init.empty();
        std::string def;
        if (!g.data.empty()) {
            // a constant table: a label, then its items; an address is a DQ OFFSET the writer turns into a relocation
            def = sym + " LABEL BYTE";
            for (size_t k = 0; k < g.data.size(); ++k) {
                const IrDataItem& it = g.data[k];
                const char* dir = it.size == 1 ? "DB" : it.size == 2 ? "DW" : it.size == 4 ? "DD" : "DQ";
                def += "\n        ";
                if (it.kind == IrDataItem::Zero) def += "DB " + intToString(it.size) + " DUP (0)";
                else if (it.kind == IrDataItem::Int) def += std::string(dir) + " " + (it.size == 8 ? int64ToString(it.value) : intToString(static_cast<int>(it.value)));
                else if (it.kind == IrDataItem::Float) def += std::string(it.size == 4 ? "REAL4 " : "REAL8 ") + it.text;
                else if (it.kind == IrDataItem::String) def += "DQ OFFSET " + stringLabel(it.text);
                else if (it.kind == IrDataItem::Bytes) {
                    // the bytes as numbers (a type descriptor's name with its terminator)
                    def += "DB ";
                    for (size_t b = 0; b < it.text.size(); ++b) def += (b ? "," : "") + intToString(static_cast<unsigned char>(it.text[b]));
                }
                else if (it.kind == IrDataItem::Rva) { def += "DD IMAGEREL " + globalSymbol(it.text); useGlobal(it.text); }   // an image-relative address (RTTI descriptors)
                else { def += "DQ OFFSET " + globalSymbol(it.text); useGlobal(it.text); }
            }
        }
        else if (g.isFloat) def = sym + (g.size == 4 ? " REAL4 " : " REAL8 ") + (numeric ? g.init : std::string("0.0"));   // float is loaded with movss
        else if (g.size == 1) def = sym + " DB " + (numeric ? g.init : std::string("0"));
        else if (g.size == 2) def = sym + " DW " + (numeric ? g.init : std::string("0"));
        else if (g.size == 4) def = sym + " DD " + (numeric ? g.init : std::string("0"));
        else if (g.size == 8) def = sym + " DQ " + (numeric ? g.init : std::string("0"));
        else def = sym + " DB " + intToString(g.size) + " DUP (0)";
        // as cl: the type's (or the declared) alignment, and at least the natural boundary of the size up to 8 bytes
        int align = g.size >= 8 ? 8 : g.size >= 4 ? 4 : g.size >= 2 ? 2 : 1;
        if (g.align > align) align = g.align;
        // a COMDAT item is a section of its own: its alignment goes to the section (on the marker), not into padding
        std::string& out = g.readOnly ? m_rdata : m_data;
        if (g.linkOnce) out += ";@comdat any" + (align > 1 ? " align " + intToString(align) : std::string("")) + "\n";
        out += (g.internal ? std::string("") : "        PUBLIC " + sym + "\n");
        if (!g.linkOnce && align > 1) out += "        ALIGN " + intToString(align) + "\n";
        out += def + "\n";
        m_defined.insert(sym);
        m_dataSymbols.insert(sym);
    }
}

std::string X86Module::finish()
{
    emitGlobals();
    emitVtables();
    std::ostringstream s;
    s << "; cppparse version 4 - x86-64 code for the Windows x64 ABI (MASM syntax, ml64)\n";
    s << "; assemble: ml64 /c /Fo module.obj module.asm\n\n";
    s << "OPTION CASEMAP:NONE\n\n";
    for (std::set<std::string>::iterator e = m_externs.begin(); e != m_externs.end(); ++e)
        if (!m_defined.count(*e)) s << "EXTERN " << *e << ":PROC\n";
    for (std::set<std::string>::iterator g = m_globalsUsed.begin(); g != m_globalsUsed.end(); ++g) {
        std::string sym = globalSymbol(*g);
        if (!m_defined.count(sym) && !startsWith(*g, "vtable@")) { s << "EXTERN " << sym << ":QWORD\n"; m_defined.insert(sym); }
    }
    s << "\n.data\n" << m_data;
    s << "\n.const\n";
    s << "sign_mask DQ 8000000000000000h, 8000000000000000h\n";
    // the label spells the constant's IEEE bits (fconst_<16 hex digits>): emitted as they are, so that no
    // decimal formatting can round the value (17 fixed digits lose small constants such as 1e-20)
    for (std::map<std::string, double>::const_iterator fi = m_floats.begin(); fi != m_floats.end(); ++fi)
        s << fi->first << " DQ 0" << fi->first.substr(7) << "h\n";
    for (std::map<std::string, std::string>::const_iterator si = m_strings.begin(); si != m_strings.end(); ++si) {
        // the literal is stored as written (quotes, escapes, adjacent pieces, L prefix): its code units, wide ones
        // as 16-bit words at an even address; MASM limits a line to 512 characters: at most 24 units per line
        std::vector<unsigned long> units;
        const bool wide = stringLiteralUnits(si->second, units);
        units.push_back(0);
        if (wide) s << "ALIGN 2\n";
        s << si->first << (wide ? " LABEL WORD\n" : " LABEL BYTE\n");
        for (size_t c = 0; c < units.size(); c += 24) {
            s << (wide ? "        DW " : "        DB ");
            for (size_t k = c; k < units.size() && k < c + 24; ++k) s << (k == c ? "" : ", ") << units[k];
            s << "\n";
        }
    }
    s << m_rdata;   // after the literals: sign_mask keeps the section's 16-byte alignment
    if (!m_initializers.empty() || !m_comdatInitializers.empty()) {
        // pointers to the dynamic initialisers of this module's globals: the CRT calls them before main; a COMDAT
        // variable's initialiser has an entry of its own in a COMDAT (after the module's: the COFF writer appends an
        // unlabelled item to the piece before it)
        s << "\nCRT$XCU SEGMENT ALIGN(8) ALIAS('.CRT$XCU') READ\n";
        for (size_t i = 0; i < m_initializers.size(); ++i) s << "        DQ OFFSET " << symbol(m_initializers[i]) << "\n";
        for (size_t i = 0; i < m_comdatInitializers.size(); ++i) {
            // the entry goes with the variable's own COMDAT (associative): the linker keeps it when it keeps the variable
            const ComdatInit& c = m_comdatInitializers[i];
            const std::string entry = c.entry.empty() ? symbol(c.irName) + "$initializer" : c.entry;
            s << ";@comdat assoc " << globalSymbol(c.global) << " align 8\n" << entry << " DQ OFFSET " << symbol(c.irName) << "\n";
        }
        s << "CRT$XCU ENDS\n";
    }
    s << "\n.code\n";
    for (std::set<std::string>::iterator d = m_defined.begin(); d != m_defined.end(); ++d)
        if (!m_dataSymbols.count(*d) && !m_internal.count(*d)) s << "PUBLIC " << *d << "\n";
    s << "\n" << m_code << "END\n";
    return s.str();
}

void X86Module::describeSymbols(const std::vector<IrFunction*>& functions, std::map<std::string, std::string>& out)
{
    for (size_t i = 0; i < functions.size(); ++i) out[symbol(functions[i]->name)] = functions[i]->name;
    for (size_t i = 0; i < m_globals.size(); ++i) out[globalSymbol(m_globals[i].name)] = m_globals[i].name;
    for (size_t i = 0; i < m_vtables.size(); ++i) out[globalSymbol("vtable@" + m_vtables[i].className)] = m_vtables[i].className + "::`vftable'";
    for (size_t i = 0; i < m_thunks.size(); ++i) out[symbol(m_thunks[i].name)] = "[thunk] " + m_thunks[i].name;
    for (std::map<std::string, double>::const_iterator fi = m_floats.begin(); fi != m_floats.end(); ++fi) {
        std::ostringstream v;
        v.precision(17);
        v << "double constant " << fi->second;
        out[fi->first] = v.str();
    }
    for (std::map<std::string, std::string>::const_iterator si = m_strings.begin(); si != m_strings.end(); ++si)
        out[si->first] = "string literal " + (si->second.size() > 48 ? si->second.substr(0, 45) + "...\"" : si->second);
    out["sign_mask"] = "sign mask for floating-point negation";
}

} // namespace cppparse
