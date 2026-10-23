#include "../Name.h"
#include "../Source.h"
#include "Masm.h"
#include "CodeView.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ostream>
#include <string>
#include <vector>

namespace {

[[noreturn]] void give_up(const std::string &what, const std::string &why) {
    std::fprintf(stderr, "%s: masm: %s\n  for: %s\n", program::kName, why.c_str(),
                 what.c_str());
    std::exit(1);
}

bool isReservedInMasm(const std::string &name) {
    static const char *const kReserved[] = {
        "ah","al","ax","eax","rax","bh","bl","bx","ebx","rbx",
        "ch","cl","cx","ecx","rcx","dh","dl","dx","edx","rdx",
        "si","esi","rsi","di","edi","rdi","bp","ebp","rbp","sp","esp","rsp",
        "r8","r9","r10","r11","r12","r13","r14","r15",
        "cs","ds","es","fs","gs","ss","st","flat","eip","rip",
        "byte","word","dword","qword","tbyte","oword","ptr","offset",
        "length","lengthof","size","sizeof","type","typedef","this",
        "near","far","short","proc","endp","segment","ends","assume",
        "public","extern","externdef","end","align","even","org","dup",
        "mask","width","low","high","lowword","highword",
        "and","or","xor","not","mod","shl","shr","eq","ne","lt","le","gt","ge",
        "if","else","endif","macro","endm","rept","irp","exitm","local",
        "label","comment","include","includelib","name","group","record",
        "struc","struct","union","db","dw","dd","dq","dt","page","title",

        "aaa","aad","aam","aas","adc","add","bound","bsf","bsr","bt","btc",
        "btr","bts","call","cbw","cdq","clc","cld","cli","cmc","cmp","cmps",
        "cmpsb","cmpsd","cmpsw","cqo","cwd","cwde","daa","das","dec","div",
        "enter","esc","hlt","idiv","imul","in","inc","ins","int","into","iret",
        "ja","jae","jb","jbe","jc","jcxz","je","jg","jge","jl","jle","jmp",
        "jna","jnb","jnc","jne","jng","jnl","jno","jnp","jns","jnz","jo","jp",
        "jpe","jpo","js","jz","lahf","lds","lea","leave","les","lock","lods",
        "lodsb","lodsd","lodsw","loop","loope","loopne","loopnz","loopz",
        "mov","movs","movsb","movsd","movss","movsw","movsx","movsxd","movzx",
        "mul","neg","nop","out","outs","pop","popa","popf","push","pusha",
        "pushf","rcl","rcr","rep","repe","repne","repnz","repz","ret","retf",
        "retn","rol","ror","sahf","sal","sar","sbb","scas","scasb","scasd",
        "scasw","seta","setae","setb","setbe","sete","setg","setge","setl",
        "setle","setna","setne","setnz","setz","sgdt","shld","shrd","sidt",
        "stc","std","sti","stos","stosb","stosd","stosw","sub","test","wait",
        "xadd","xchg","xlat","xlatb",
        "fabs","fadd","fchs","fcos","fdiv","fild","finit","fist","fld","fmul",
        "fnop","fprem","fptan","frndint","fscale","fsin","fsqrt","fst","fstp",
        "fsub","ftst","fwait","fxam","fxch",
        "addpd","addps","addsd","addss","andpd","andps","cvtsd2ss","cvtsi2sd",
        "cvtss2sd","cvttsd2si","divsd","divss","maxsd","minsd","movapd","movaps",
        "movd","movq","mulsd","mulss","orpd","orps","pxor","sqrtsd","subsd",
        "subss","ucomisd","ucomiss","xorpd","xorps",
        nullptr,
    };
    std::string lower;
    for (char c : name) lower += static_cast<char>(std::tolower(c));
    for (const char *const *p = kReserved; *p != nullptr; ++p)
        if (lower == *p) return true;
    return false;
}

bool canUnreserve(const std::string &name) {
    static const char *const kNeverEmitted[] = {
        "fabs","fadd","fchs","fcos","fdiv","fild","finit","fist","fld","fmul",
        "fnop","fprem","fptan","frndint","fscale","fsin","fsqrt","fst","fstp",
        "fsub","ftst","fwait","fxam","fxch",
        nullptr,
    };
    std::string lower;
    for (char c : name) lower += static_cast<char>(std::tolower(c));
    for (const char *const *p = kNeverEmitted; *p != nullptr; ++p)
        if (lower == *p) return true;
    return false;
}

const char *ptrFor(int bytes) {
    switch (bytes) {
    case 1: return "BYTE PTR ";
    case 2: return "WORD PTR ";
    case 4: return "DWORD PTR ";
    case 8: return "QWORD PTR ";
    default: return "";
    }
}

struct Rule {
    const char *att;
    const char *masm;
    int width;

};

const Rule kRules[] = {
    { "movb", "mov", 1 }, { "movw", "mov", 2 },
    { "movl", "mov", 4 }, { "movq", "mov", 8 },
    { "mov",  "mov", 0 }, { "movabs", "mov", 0 },
    { "movslq", "movsxd", 4 },
    { "movsbq", "movsx", 1 }, { "movswq", "movsx", 2 },

    { "movzbq", "movzx", 1 }, { "movzwq", "movzx", 2 },
    { "movzbl", "movzx", 1 }, { "movzwl", "movzx", 2 },

    { "lea", "lea", 0 },
    { "push", "push", 0 }, { "pop", "pop", 0 },

    { "add", "add", 0 }, { "sub", "sub", 0 }, { "imul", "imul", 0 },
    { "idiv", "idiv", 0 }, { "div", "div", 0 }, { "neg", "neg", 0 },
    { "and", "and", 0 }, { "or", "or", 0 }, { "orb", "or", 1 }, { "xor", "xor", 0 },
    { "shl", "shl", 0 }, { "shr", "shr", 0 }, { "sar", "sar", 0 },
    { "cmp", "cmp", 0 }, { "cdq", "cdq", 0 }, { "cqo", "cqo", 0 },
    { "addl", "add", 4 }, { "cmpl", "cmp", 4 }, { "testb", "test", 1 },

    { "call", "call", 0 }, { "ret", "ret", 0 },
    { "jmp", "jmp", 0 }, { "je", "je", 0 }, { "jne", "jne", 0 },
    { "jae", "jae", 0 }, { "jns", "jns", 0 },
    // What shorten() writes: `leave` for the epilogue pair, and `test` where a
    // comparison against zero is one instruction shorter.
    { "leave", "leave", 0 },
    { "test", "test", 0 }, { "testq", "test", 8 }, { "testl", "test", 4 },
    { "testw", "test", 2 },
    // The branches foldCompareBranch writes, reading a comparison by its flags.
    { "jl", "jl", 0 }, { "jge", "jge", 0 }, { "jle", "jle", 0 },
    { "jg", "jg", 0 }, { "jb", "jb", 0 }, { "jbe", "jbe", 0 },
    { "ja", "ja", 0 }, { "jp", "jp", 0 }, { "jnp", "jnp", 0 },

    { "sete", "sete", 0 }, { "setne", "setne", 0 },
    { "setl", "setl", 0 }, { "setle", "setle", 0 },
    { "setg", "setg", 0 }, { "setge", "setge", 0 },
    { "seta", "seta", 0 }, { "setae", "setae", 0 },
    { "setb", "setb", 0 }, { "setbe", "setbe", 0 },
    { "setp", "setp", 0 }, { "setnp", "setnp", 0 },

    { "movsd", "movsd", 8 }, { "movss", "movss", 4 },
    { "movapd", "movapd", 0 }, { "movaps", "movaps", 0 },
    { "movd", "movd", 0 },
    { "addsd", "addsd", 0 }, { "subsd", "subsd", 0 },
    { "mulsd", "mulsd", 0 }, { "divsd", "divsd", 0 },
    { "addss", "addss", 0 }, { "subss", "subss", 0 },
    { "mulss", "mulss", 0 }, { "divss", "divss", 0 },
    { "ucomisd", "ucomisd", 0 }, { "ucomiss", "ucomiss", 0 },
    { "pxor", "pxor", 0 }, { "xorpd", "xorpd", 0 }, { "xorps", "xorps", 0 },
    { "cvtsi2sdq", "cvtsi2sd", 8 }, { "cvtsi2ssq", "cvtsi2ss", 8 },
    { "cvttsd2si", "cvttsd2si", 0 }, { "cvttss2si", "cvttss2si", 0 },
    { "cvtss2sd", "cvtss2sd", 0 }, { "cvtsd2ss", "cvtsd2ss", 0 },
};

const Rule *ruleFor(const std::string &m) {
    for (const Rule &r : kRules)
        if (m == r.att) return &r;
    return nullptr;
}

}

std::string MasmSpelling::mangle(const std::string &name) {
    if (name.find('.') != std::string::npos) {
        std::string out = "$";
        for (char c : name) out += (c == '.') ? '_' : c;
        return out;
    }
    if (!isReservedInMasm(name)) return name;
    if (defined_.find(name) != defined_.end()) return "$" + name;
    if (!canUnreserve(name))
        give_up(name, "'" + name + "' is a name ml64 reserves and this file "
                      "emits, so an imported symbol cannot be spelled it");
    unreserved_.insert(name);
    return name;
}

MasmSpelling::Rendered MasmSpelling::render(const Op &x) {
    switch (x.kind) {
    case Op::Reg: {

        Str name = x.text.substr(1);
        return { std::string(name), false, false, name.startsWith("xmm") };
    }
    case Op::Imm: {
        if (!x.immNumeric) return { std::string(x.text), false, true, false };
        std::string v = x.immNeg ? "-" + std::to_string(x.uimm)
                                 : std::to_string(x.uimm);
        return { v, false, true, false };
    }
    case Op::Mem: {

        std::string t = "[" + std::string(x.text.substr(1));
        if (x.hasDisp && x.disp != 0) {
            if (x.disp < 0) t += std::to_string(x.disp);
            else            t += "+" + std::to_string(x.disp);
        }
        t += "]";
        return { t, true, false, false };
    }
    case Op::Rip: {

        std::string sym(x.text);
        referenced_.insert(sym);
        return { "[" + mangle(sym) + "]", true, false, false };
    }
    case Op::Ind:
        return { std::string(x.text.substr(1)), false, false, false };
    case Op::Lbl: {
        std::string sym(x.text);
        referenced_.insert(sym);
        return { mangle(sym), false, false, false };
    }
    }
    return {};
}

void MasmSpelling::ins(const std::string &m) {
    const Rule *r = ruleFor(m);
    if (r == nullptr) give_up(m, "an instruction this spelling does not know");
    o_ += "  "; o_ += r->masm; o_ += '\n';
}

void MasmSpelling::ins(const std::string &m, const Op &a) {
    const Rule *r = ruleFor(m);
    if (r == nullptr) give_up(m, "an instruction this spelling does not know");
    Rendered x = render(a);
    std::string t = x.text;
    if (x.isMem && r->width != 0) t = std::string(ptrFor(r->width)) + t;
    o_ += "  "; o_ += r->masm; o_ += ' '; o_ += t; o_ += '\n';
}

void MasmSpelling::ins(const std::string &m, const Op &a, const Op &b) {
    const Rule *r = ruleFor(m);
    if (r == nullptr) give_up(m, "an instruction this spelling does not know");

    Rendered src = render(a), dst = render(b);

    std::string name = r->masm;
    if (m == "movq" && (src.isXmm || dst.isXmm)) name = "movq";

    std::string sTxt = src.text, dTxt = dst.text;
    if (r->width != 0) {
        if (src.isMem) sTxt = std::string(ptrFor(r->width)) + sTxt;
        else if (dst.isMem && m != "movsxd")
            dTxt = std::string(ptrFor(r->width)) + dTxt;
    }

    if (r->width == 0 && dst.isMem && src.isImm)
        give_up(m, "a store of an immediate with no width to infer");

    o_ += "  "; o_ += name; o_ += ' '; o_ += dTxt; o_ += ", "; o_ += sTxt; o_ += '\n';
}

void MasmSpelling::defLabel(const std::string &l) {
    if (seg_ == Code) {

        // internal: the walker's own (.L.), and under -g the function's bounds (.Lfunc.)
        if (l.compare(0, 3, ".L.") != 0 && l.compare(0, 7, ".Lfunc.") != 0)
            give_up(l, "a label in code that is not an internal one");
        defined_.insert(l);
        o_ += mangle(l); o_ += ":\n";
        return;
    }
    defined_.insert(l);
    flushPending();
    pending_ = mangle(l);
}

void MasmSpelling::functionBegin(const std::string &name, bool exported) {
    inFunction_ = true;
    function_ = name;
    lastLoc_.clear();
    lastLocFile_ = lastLocLine_ = 0;
    linesRun_.clear();
    entries_ = 0;
    defined_.insert(name);
    if (exported) exported_.insert(name);
    flushPending();
    if (seg_ != Code) { o_ += "\n.CODE\n"; seg_ = Code; }

    o_ += mangle(name); o_ += " PROC FRAME\n";
}

void MasmSpelling::prologue(int frameSize) {
    o_ += "  push rbp\n";
    o_ += "  .PUSHREG rbp\n";
    o_ += "  mov rbp, rsp\n";

    o_ += "  .SETFRAME rbp, 0\n";
    if (frameSize > 0) {
        o_ += "  sub rsp, "; appendNum(o_, frameSize); o_ += '\n';
        o_ += "  .ALLOCSTACK "; appendNum(o_, frameSize); o_ += '\n';
    }
    o_ += "  .ENDPROLOG\n";
    if (debug_) { o_ += mangle(CoffSpelling::prologEnd(function_)); o_ += ":\n"; }
}

void MasmSpelling::functionEnd(const std::string &name) {
    inFunction_ = false;
    if (debug_) { o_ += mangle(CoffSpelling::codeEnd(name)); o_ += ":\n"; }
    o_ += mangle(name); o_ += " ENDP\n\n";
}

// `.FILE n, "path"` names source file n for GRIDE's masm, which writes the CodeView line table
// from the `.LINE`s; the path arrives absolute (debugFileName).
void MasmSpelling::fileEntry(int n, const std::string &name) {
    debug_ = true;
    o_ += ".FILE "; appendNum(o_, n); o_ += ", \""; o_ += name; o_ += "\"\n";
}

// Every name in an expression as this spelling writes it: `.Lcv.r3.e-.Lcv.r3.b` and the like.
std::string MasmSpelling::mangleNames(const std::string &expr) {
    std::string out;
    for (std::size_t i = 0; i < expr.size();) {
        const char c = expr[i];
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '$' || c == '?' || c == '@') {
            std::size_t j = i;
            while (j < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[j])) || expr[j] == '_' ||
                                       expr[j] == '.' || expr[j] == '$' || expr[j] == '?' || expr[j] == '@'))
                j++;
            out += mangle(expr.substr(i, j - i));
            i = j;
        } else {
            out += c;
            i++;
        }
    }
    return out;
}

// **The CodeView writeCodeView makes for clang, in MASM** - one writer of the records, two
// spellings of them. `.section .debug$S` and `.debug$T` become SEGMENTs GRIDE's masm carries
// into the object; `.long`, `.short`, `.byte` and `.asciz` its DD, DW and DB; `.secrel32` and
// `.secidx` SECTIONREL and SECTION; a label its MASM name. The line table, the file checksums and
// the string table are left to masm, which makes them from the `.LINE`s after these records.
void MasmSpelling::codeView(const std::string &gnu) {
    flushPending();
    std::string open;
    for (std::size_t at = 0; at < gnu.size();) {
        std::size_t nl = gnu.find('\n', at);
        if (nl == std::string::npos) nl = gnu.size();
        const std::string line = gnu.substr(at, nl - at);
        at = nl + 1;
        std::size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        const std::string t = line.substr(b);
        if (b == 0 && t[t.size() - 1] == ':') {
            o_ += mangle(t.substr(0, t.size() - 1)); o_ += ":\n";
            continue;
        }
        const std::size_t sp = t.find_first_of(" \t");
        const std::string d = t.substr(0, sp);
        const std::string rest = sp == std::string::npos ? std::string() : t.substr(t.find_first_not_of(" \t", sp));
        if (d == ".section") {
            if (!open.empty()) o_ += open + " ENDS\n";
            open = rest.compare(0, 8, ".debug$T") == 0 ? ".debug$T" : ".debug$S";
            o_ += "\n" + open + " SEGMENT\n";
        } else if (d == ".p2align") {
            o_ += "  ALIGN "; appendNum(o_, 1LL << std::atoi(rest.c_str())); o_ += '\n';
        } else if (d == ".long") {
            o_ += "  DD " + mangleNames(rest) + "\n";
        } else if (d == ".short") {
            o_ += "  DW " + mangleNames(rest) + "\n";
        } else if (d == ".byte") {
            o_ += "  DB " + rest + "\n";
        } else if (d == ".asciz") {
            // the GNU string's escapes undone, MASM's one escape - a doubled quote - made
            std::string s;
            for (std::size_t k = 1; k + 1 < rest.size(); k++) {
                if (rest[k] == '\\' && k + 2 < rest.size()) k++;
                if (rest[k] == '"') s += '"';
                s += rest[k];
            }
            o_ += s.empty() ? "  DB 0\n" : "  DB \"" + s + "\", 0\n";
        } else if (d == ".secrel32") {
            o_ += "  DD SECTIONREL " + mangleNames(rest) + "\n";
        } else if (d == ".secidx") {
            o_ += "  DW SECTION " + mangleNames(rest) + "\n";
        } else if (d != ".cv_linetable" && d != ".cv_filechecksums" && d != ".cv_stringtable") {
            give_up(line, "a CodeView directive the MASM spelling has no words for");
        }
    }
    if (!open.empty()) o_ += open + " ENDS\n";
}

void MasmCodeGen::writeDebug(const std::vector<DwarfFunction> &fns,
                             const std::vector<DwarfGlobal> &globals) {
    std::string gnu;
    writeCodeView(gnu, masmTarget_, codeViewPath(compDir(), lineSource()->files().front() + ".obj"),
                  fns, globals, *enums_);
    masm_.codeView(gnu);
}

// Whether text holds an instruction: a line indented, and not a directive.
static bool holdsInstruction(const std::string &text) {
    for (std::size_t at = 0; at < text.size();) {
        if (text.compare(at, 2, "  ") == 0 && at + 2 < text.size() && text[at + 2] != '.') return true;
        const std::size_t nl = text.find('\n', at);
        if (nl == std::string::npos) break;
        at = nl + 1;
    }
    return false;
}

// `.LINE line, file` before the instructions of a line, as CoffSpelling writes `.cv_loc`: one entry
// per run of a line, no second run of it in a function (cdb refuses `bp` on a line entered twice),
// and one no instruction follows replaced by the next. **But the body's start**: a first statement
// on the entry's own `{` line (a one-line function) is entered again after the prologue and the
// parameters' stores, so a debugger stops where the parameters are stored (gride-dbg's bodyStart
// takes a function's second entry of its first line as its body).
void MasmSpelling::location(int file, int line, int) {
    if (!inFunction_) return;
    const bool bodyStart = entries_ == 1 && file == lastLocFile_ && line == lastLocLine_ &&
                           lastLocAt_ + lastLoc_.size() <= o_.size() && holdsInstruction(o_.substr(lastLocAt_ + lastLoc_.size()));
    if (!bodyStart) {
        if (file == lastLocFile_ && line == lastLocLine_) return;
        if (!linesRun_.insert(std::make_pair(file, line)).second) return;
    }
    entries_++;
    lastLocFile_ = file;
    lastLocLine_ = line;
    if (!lastLoc_.empty() && lastLocAt_ + lastLoc_.size() <= o_.size() &&
        o_.compare(lastLocAt_, lastLoc_.size(), lastLoc_) == 0 &&
        !holdsInstruction(o_.substr(lastLocAt_ + lastLoc_.size())))
        o_.erase(lastLocAt_, lastLoc_.size());
    lastLocAt_ = o_.size();
    o_ += "  .LINE "; appendNum(o_, line); o_ += ", "; appendNum(o_, file); o_ += '\n';
    lastLoc_ = o_.substr(lastLocAt_);
}

std::string MasmCodeGen::debugFileName(const std::string &name) const {
    const bool absolute = (name.size() > 1 && name[1] == ':') ||
                          (!name.empty() && (name[0] == '/' || name[0] == '\\'));
    const std::string full = absolute || compDir().empty() ? name : compDir() + "\\" + name;
    std::string out;
    for (char c : full) {
        if (c == '/') c = '\\';
        if (c == '"') out += '"';
        out += c;
    }
    return out;
}

void MasmSpelling::globl(const std::string &name) { exported_.insert(name); }

void MasmSpelling::textSection() {
    flushPending();
    if (seg_ != Code) { o_ += "\n.CODE\n"; seg_ = Code; }
}

void MasmSpelling::rodataSection() {
    flushPending();
    if (seg_ != Const) { o_ += "\n.CONST\n"; seg_ = Const; }
}

void MasmSpelling::dataSection() {
    flushPending();
    if (seg_ != Data) { o_ += "\n.DATA\n"; seg_ = Data; }
}

void MasmSpelling::bssSection() {
    flushPending();
    if (seg_ != Bss) { o_ += "\n.DATA?\n"; seg_ = Bss; }
}

void MasmSpelling::objectType(const std::string &) {}
void MasmSpelling::objectSize(const std::string &, int) {}

void MasmSpelling::align(int n) {
    flushPending();
    o_ += "  ALIGN "; appendNum(o_, n); o_ += '\n';
}

void MasmSpelling::zero(int n) {
    if (n == 0) return;
    items("DB", { std::to_string(n) +
                  (seg_ == Bss ? " DUP (?)" : " DUP (0)") });
}

void MasmSpelling::dataInt(int size, long long v) {
    const char *dir = size == 1 ? "DB" : size == 2 ? "DW"
                    : size == 4 ? "DD" : "DQ";
    items(dir, { std::to_string(v) });
}

void MasmSpelling::dataSym(const std::string &sym, long long off) {
    std::string t = mangle(sym);
    if (off > 0) t += "+" + std::to_string(off);
    else if (off < 0) t += "-" + std::to_string(-off);
    items("DQ", { t });
}

void MasmSpelling::dataBytes(const std::string &bytes) {
    std::vector<std::string> it;
    it.reserve(bytes.size());
    for (char c : bytes)
        it.push_back(std::to_string(
            static_cast<int>(static_cast<unsigned char>(c))));
    items("DB", it);
}

void MasmSpelling::flushPending() {
    if (!pending_.empty()) {
        o_ += pending_; o_ += " LABEL BYTE\n";
        pending_.clear();
    }
}

void MasmSpelling::items(const char *dir, const std::vector<std::string> &it) {
    const std::size_t kPerLine = 16;
    for (std::size_t i = 0; i < it.size(); i += kPerLine) {
        std::string chunk;
        for (std::size_t j = i; j < it.size() && j < i + kPerLine; j++) {
            if (!chunk.empty()) chunk += ", ";
            chunk += it[j];
        }
        if (i == 0 && !pending_.empty()) {
            o_ += pending_; o_ += ' '; o_ += dir; o_ += ' '; o_ += chunk; o_ += '\n';
            pending_.clear();
        } else {
            o_ += "  "; o_ += dir; o_ += ' '; o_ += chunk; o_ += '\n';
        }
    }
}

void MasmSpelling::predefine(const std::vector<std::string> &names) {
    for (const std::string &n : names) defined_.insert(n);
}

void MasmSpelling::preamble(std::ostream &sink) {
    sink << "; Generated by cc1 for x86_64-windows, in MASM syntax for ml64.\n";
    sink << "; The instruction selection is the same one the Linux backend makes;\n";
    sink << "; only the spelling is Microsoft's. See src/backend/Masm.cpp.\n\n";

    for (const std::string &g : unreserved_)
        sink << "OPTION NOKEYWORD:<" << g << ">\n";
    if (!unreserved_.empty()) sink << "\n";

    for (const std::string &e : exported_)
        sink << "PUBLIC " << mangle(e) << "\n";
    for (const std::string &r : referenced_)
        if (defined_.find(r) == defined_.end())
            sink << "EXTERN " << mangle(r) << ":PROC\n";
    sink << "\n";
}

void MasmSpelling::postamble(std::ostream &sink) {

    if (!pending_.empty())
        give_up(pending_, "a data label left dangling at the end of the file");
    sink << "\nEND\n";
}
