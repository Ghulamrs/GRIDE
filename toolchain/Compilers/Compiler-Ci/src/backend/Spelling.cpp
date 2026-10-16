#include "Spelling.h"

#include <ostream>

void GnuSpelling::op(const Op &x) {
    switch (x.kind) {
    case Op::Reg: o_ += x.text; return;
    case Op::Imm:
        o_ += '$';
        if (!x.immNumeric) { o_ += x.text; return; }
        if (x.immNeg) o_ += '-';
        appendNum(o_, x.uimm);
        return;
    case Op::Mem:
        if (x.hasDisp) appendNum(o_, x.disp);
        o_ += '(';
        o_ += x.text;
        o_ += ')';
        return;
    case Op::Rip: o_ += x.text; o_ += "(%rip)"; return;
    case Op::Ind: o_ += '*'; o_ += x.text; return;
    case Op::Lbl: o_ += x.text; return;
    }
}

void GnuSpelling::ins(const std::string &m) { o_ += "  "; o_ += m; o_ += '\n'; }

void GnuSpelling::ins(const std::string &m, const Op &a) {
    o_ += "  "; o_ += m; o_ += ' ';
    op(a);
    o_ += '\n';
}

void GnuSpelling::ins(const std::string &m, const Op &a, const Op &b) {
    o_ += "  "; o_ += m; o_ += ' ';
    op(a);
    o_ += ", ";
    op(b);
    o_ += '\n';
}

void GnuSpelling::defLabel(const std::string &l) { o_ += l; o_ += ":\n"; }

void GnuSpelling::functionBegin(const std::string &name, bool exported) {
    if (exported) globl(name);
    textSection();
    defLabel(name);
}

void GnuSpelling::prologue(int frameSize) {
    ins("push", reg("%rbp"));
    ins("mov", reg("%rsp"), reg("%rbp"));
    if (frameSize > 0) ins("sub", imm(frameSize), reg("%rsp"));
}

void GnuSpelling::functionEnd(const std::string &) {}

void GnuSpelling::globl(const std::string &name) {
    o_ += "  .globl "; o_ += name; o_ += '\n';
}

void GnuSpelling::fileEntry(int n, const std::string &name) {
    o_ += "  .file ";
    appendNum(o_, n);
    o_ += " \"";
    o_ += name;
    o_ += "\"\n";
}

void GnuSpelling::location(int file, int line, int column) {
    o_ += "  .loc ";
    appendNum(o_, file);
    o_ += ' ';
    appendNum(o_, line);
    o_ += ' ';
    appendNum(o_, column);
    o_ += '\n';
}

void GnuSpelling::textSection()   { o_ += "  .text\n"; }
void GnuSpelling::rodataSection() { o_ += "  .section .rodata\n"; }
void GnuSpelling::dataSection()   { o_ += "  .data\n"; }
void GnuSpelling::bssSection()    { o_ += "  .bss\n"; }

void GnuSpelling::objectType(const std::string &name) {
    o_ += "  .type "; o_ += name; o_ += ", @object\n";
}

void GnuSpelling::objectSize(const std::string &name, int size) {
    o_ += "  .size "; o_ += name; o_ += ", "; appendNum(o_, size); o_ += '\n';
}

void GnuSpelling::align(int n) { o_ += "  .align "; appendNum(o_, n); o_ += '\n'; }
void GnuSpelling::zero(int n)  { o_ += "  .zero ";  appendNum(o_, n); o_ += '\n'; }

void GnuSpelling::dataInt(int size, long long v) {
    switch (size) {
    case 1: o_ += "  .byte "; break;
    case 2: o_ += "  .word "; break;
    case 4: o_ += "  .long "; break;
    default: o_ += "  .quad "; break;
    }
    appendNum(o_, v);
    o_ += '\n';
}

void GnuSpelling::dataSym(const std::string &sym, long long off) {
    o_ += "  .quad ";
    o_ += sym;
    if (off > 0) { o_ += '+'; appendNum(o_, off); }
    else if (off < 0) { o_ += '-'; appendNum(o_, -off); }
    o_ += '\n';
}

void GnuSpelling::dataBytes(const std::string &bytes) {
    o_ += "  .byte ";
    for (std::size_t k = 0; k < bytes.size(); k++) {
        if (k) o_ += ", ";
        appendNum(o_, static_cast<long long>(
                          static_cast<unsigned char>(bytes[k])));
    }
    o_ += '\n';
}

// --- CoffSpelling -----------------------------------------------------------

// A function symbol is declared one (`.type 32`) so the linker and the PDB know it for code.
void CoffSpelling::functionBegin(const std::string &name, bool exported) {
    function_ = name;
    lastLoc_.clear();
    textSection();
    o_ += "  .def " + name + "; .scl " + (exported ? "2" : "3") + "; .type 32; .endef\n";
    if (exported) globl(name);
    defLabel(name);
    o_ += "  .seh_proc " + name + '\n';
    if (codeView_) { o_ += "  .cv_func_id "; appendNum(o_, cvFunctions_++); o_ += '\n'; }
}

// **The frame is RBP's, and the unwind data says so**: the unwinder takes RSP back
// from RBP, so the pushes an expression makes in the body need no description.
void CoffSpelling::prologue(int frameSize) {
    ins("push", reg("%rbp"));
    o_ += "  .seh_pushreg %rbp\n";
    ins("mov", reg("%rsp"), reg("%rbp"));
    o_ += "  .seh_setframe %rbp, 0\n";
    if (frameSize > 0) {
        ins("sub", imm(frameSize), reg("%rsp"));
        o_ += "  .seh_stackalloc ";
        appendNum(o_, frameSize);
        o_ += '\n';
    }
    o_ += "  .seh_endprologue\n";
    defLabel(prologEnd(function_));
}

// The end label is the spelling's own: one the walker defines can wait in the optimizer past the last function.
void CoffSpelling::functionEnd(const std::string &name) {
    defLabel(codeEnd(name));
    o_ += "  .seh_endproc\n";
}

// The name arrives absolute and escaped (codeViewPath); the checksum is optional and left out.
void CoffSpelling::fileEntry(int n, const std::string &name) {
    codeView_ = true;
    o_ += "  .cv_file ";
    appendNum(o_, n);
    o_ += " \"" + name + "\"\n";
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

// **A line entry no instruction follows is replaced by the next**: at one address cdb
// takes the first, so a breakpoint on a declaration stopped on the line before it.
// **One entry per run of a line, as cl writes it**: a `for` gave four, and cdb refused `bp` on it as ambiguous.
void CoffSpelling::location(int file, int line, int column) {
    if (!codeView_ || cvFunctions_ == 0) return;
    if (cvFunctions_ == lastLocFn_ && file == lastLocFile_ && line == lastLocLine_) return;
    // **No second run of a line**: a loop's step written after its body was a second run of the `for`
    // line, and cdb refused `bp` on it as ambiguous; that code counts as the line before it.
    if (cvFunctions_ != lastLocFn_) linesRun_.clear();
    if (!linesRun_.insert(std::make_pair(file, line)).second) return;
    lastLocFn_ = cvFunctions_;
    lastLocFile_ = file;
    lastLocLine_ = line;
    if (!lastLoc_.empty() && lastLocAt_ + lastLoc_.size() <= o_.size() &&
        o_.compare(lastLocAt_, lastLoc_.size(), lastLoc_) == 0 &&
        !holdsInstruction(o_.substr(lastLocAt_ + lastLoc_.size())))
        o_.erase(lastLocAt_, lastLoc_.size());
    lastLocAt_ = o_.size();
    o_ += "  .cv_loc ";
    appendNum(o_, cvFunctions_ - 1);
    o_ += ' ';
    appendNum(o_, file);
    o_ += ' ';
    appendNum(o_, line);
    o_ += ' ';
    appendNum(o_, column);
    o_ += '\n';
    lastLoc_ = o_.substr(lastLocAt_);
}

void CoffSpelling::rodataSection() { o_ += "  .section .rdata,\"dr\"\n"; }

// `.balign`: what `.align` counts in differs between object formats, and bytes are meant.
void CoffSpelling::align(int n) { o_ += "  .balign "; appendNum(o_, n); o_ += '\n'; }
