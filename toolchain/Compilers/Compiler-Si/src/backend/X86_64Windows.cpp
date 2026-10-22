#include "X86_64Windows.h"

#include "CodeView.h"

#include <cctype>
#include <cstdlib>

namespace shalimar {

// A source path for `.FILE`: absolute, since cdb matches a breakpoint's file by its path; a MASM
// string, whose only escape is a doubled quote.
static std::string masmPath(const std::string &compDir, const std::string &name) {
    const bool absolute = (name.size() > 1 && name[1] == ':') ||
                          (!name.empty() && (name[0] == '/' || name[0] == '\\'));
    const std::string full = absolute || compDir.empty() ? name : compDir + "\\" + name;
    std::string out;
    for (char c : full) {
        if (c == '/') c = '\\';
        if (c == '"') out += '"';
        out += c;
    }
    return out;
}

void X86_64WindowsEmitter::beginModule(const std::string &sourceName) {
    sourceName_ = sourceName;
    // the files by unit, numbered from 1
    if (debugInfo())
        for (size_t i = 0; i < files_.size(); ++i)
            raw(".FILE\t" + std::to_string(i + 1) + ", \"" + masmPath(compDir_, files_[i]) + "\"");
}

void X86_64WindowsEmitter::beginFunction(const std::string &name) {
    const std::string s = symbol(name);
    noteDefined(s);
    openProcedure_ = s;
    blank();
    raw("PUBLIC\t" + s);
    raw(s + "\tPROC");
    prologueMark_ = text_.size();
}

void X86_64WindowsEmitter::endFunction(int slots) {
    emitEpilogue(slots);
    if (describing()) {
        // Slot k is at rsp + shadow + 8k, and rsp is rbp less the frame: rbp was taken before the allocation.
        DebugFunction &d = debugFunctions_.back();
        d.slotBias = abi_.shadowBytes - frameBytesFor(slots);
        d.frameBias = frameBytesFor(slots);
        raw(d.end + ":");
    }
    raw(openProcedure_ + "\tENDP");
    openProcedure_.clear();
    text_.insert(prologueMark_, prologue(slots));
    describing_ = false;
}

// **A line entry is entered once per run of the line**, as cl writes them: cdb takes the first
// entry at an address and refuses `bp` on a line entered twice.
void X86_64WindowsEmitter::sourceLine(int unit, int line) {
    if (!describing()) return;
    if (unit == lastUnit_ && line == lastLine_) return;
    lastUnit_ = unit;
    lastLine_ = line;
    raw("\t.LINE\t" + std::to_string(line) + ", " + std::to_string(unit + 1));
}

// Right after beginFunction: the PROC becomes a PROC FRAME, whose unwind directives the prologue
// writes (afterPush ...), and the function's own line comes before the prologue the mark will take.
void X86_64WindowsEmitter::describeFunction(const DebugFunction &function) {
    if (!debugInfo()) return;
    if (prologueMark_ > 0 && prologueMark_ <= text_.size()) {
        text_.insert(prologueMark_ - 1, " FRAME");
        prologueMark_ += 6;
    }
    // the labels its S_GPROC32 is measured by, as the COFF emitter names them
    DebugFunction d = function;
    d.begin = d.symbol;
    d.end = "Lfunc_end_" + d.symbol;
    d.prologEnd = "Lfunc_prolog_" + d.symbol;
    d.codeEnd = d.end;
    debugFunctions_.push_back(d);
    lastUnit_ = -1;
    lastLine_ = -1;
    describing_ = true;
    if (function.line > 0) sourceLine(function.unit, function.line);
    prologueMark_ = text_.size();
}

std::string X86_64WindowsEmitter::afterPush() const { return describing() ? "\t.PUSHREG\trbp\n" : ""; }
std::string X86_64WindowsEmitter::afterSetFrame() const { return describing() ? "\t.SETFRAME\trbp, 0\n" : ""; }
std::string X86_64WindowsEmitter::afterAlloc(int bytes) const {
    return describing() ? "\t.ALLOCSTACK\t" + std::to_string(bytes) + "\n" : "";
}
std::string X86_64WindowsEmitter::endPrologue() const {
    if (!describing() || debugFunctions_.empty()) return "";
    return "\t.ENDPROLOG\n" + debugFunctions_.back().prologEnd + ":\n";
}

// A name of the CodeView writer's in MASM: a dot is not a MASM name's.
static std::string masmName(const std::string &name) {
    if (name.find('.') == std::string::npos) return name;
    std::string out = "$";
    for (char c : name) out += c == '.' ? '_' : c;
    return out;
}

static std::string masmNames(const std::string &expr) {
    std::string out;
    for (size_t i = 0; i < expr.size();) {
        const char c = expr[i];
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '$' || c == '?' || c == '@') {
            size_t j = i;
            while (j < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[j])) || expr[j] == '_' ||
                                       expr[j] == '.' || expr[j] == '$' || expr[j] == '?' || expr[j] == '@'))
                j++;
            out += masmName(expr.substr(i, j - i));
            i = j;
        } else {
            out += c;
            i++;
        }
    }
    return out;
}

// **The CodeView writeCodeView makes for clang, in MASM**: `.section .debug$S` and `.debug$T`
// become SEGMENTs GRIDE's masm carries into the object; `.long`, `.short`, `.byte` and `.asciz` its
// DD, DW and DB; `.secrel32` and `.secidx` SECTIONREL and SECTION. The line table, file checksums and
// string table are left to masm, which makes them from the `.LINE`s.
static std::string masmCodeView(const std::string &gnu) {
    std::string o, open;
    for (size_t at = 0; at < gnu.size();) {
        size_t nl = gnu.find('\n', at);
        if (nl == std::string::npos) nl = gnu.size();
        const std::string line = gnu.substr(at, nl - at);
        at = nl + 1;
        const size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        const std::string t = line.substr(b);
        if (b == 0 && t[t.size() - 1] == ':') { o += masmName(t.substr(0, t.size() - 1)) + ":\n"; continue; }
        const size_t sp = t.find_first_of(" \t");
        const std::string d = t.substr(0, sp);
        const std::string rest = sp == std::string::npos ? std::string() : t.substr(t.find_first_not_of(" \t", sp));
        if (d == ".section") {
            if (!open.empty()) o += open + "\tENDS\n";
            open = rest.compare(0, 8, ".debug$T") == 0 ? ".debug$T" : ".debug$S";
            o += "\n" + open + "\tSEGMENT\n";
        } else if (d == ".p2align") {
            o += "\tALIGN\t" + std::to_string(1 << std::atoi(rest.c_str())) + "\n";
        } else if (d == ".long") {
            o += "\tDD\t" + masmNames(rest) + "\n";
        } else if (d == ".short") {
            o += "\tDW\t" + masmNames(rest) + "\n";
        } else if (d == ".byte") {
            o += "\tDB\t" + rest + "\n";
        } else if (d == ".asciz") {
            std::string s;
            for (size_t k = 1; k + 1 < rest.size(); k++) {
                if (rest[k] == '\\' && k + 2 < rest.size()) k++;
                if (rest[k] == '"') s += '"';
                s += rest[k];
            }
            o += s.empty() ? "\tDB\t0\n" : "\tDB\t\"" + s + "\", 0\n";
        } else if (d == ".secrel32") {
            o += "\tDD\tSECTIONREL " + masmNames(rest) + "\n";
        } else if (d == ".secidx") {
            o += "\tDW\tSECTION " + masmNames(rest) + "\n";
        }
        // .cv_linetable, .cv_filechecksums and .cv_stringtable: masm's
    }
    if (!open.empty()) o += open + "\tENDS\n";
    return o;
}

// A label of the file's own: a dot is not a MASM name's.
std::string X86_64WindowsEmitter::localLabel(const std::string &name) const {
    std::string out = "L";
    for (size_t i = 0; i < name.size(); ++i) out += name[i] == '.' ? '_' : name[i];
    return out;
}

void X86_64WindowsEmitter::label(int id) {
    raw(labelName(id) + ":");
}

void X86_64WindowsEmitter::emitGlobalBlock(int slots) {
    globalSlots_ = slots;
}

void X86_64WindowsEmitter::openConstSection() {
    raw("CONST\tSEGMENT");
}

void X86_64WindowsEmitter::closeConstSection() {
    raw("CONST\tENDS");
}

void X86_64WindowsEmitter::endModule() {
    std::string header;
    header += "; " + sourceName_ + "\n";
    header += "OPTION\tCASEMAP:NONE\n";

    // **A C library function can share its name with an x87 instruction.** FABS is a mnemonic
    // ml64 has known since the 8087, so `EXTRN fabs:PROC` answers `A2008: syntax error : fabs`;
    // OPTION NOKEYWORD is MASM's own answer, safe module-wide because this compiler emits no x87 at all, and listed from the externals actually used.
    std::string suppressed;
    for (const std::string &name : externals()) {
        if (name == "fabs") suppressed += (suppressed.empty() ? "" : " ") + name;
    }
    if (!suppressed.empty()) {
        header += "OPTION\tNOKEYWORD:<" + suppressed + ">\n";
    }

    for (const std::string &name : externals()) {
        header += "EXTRN\t" + name + ":PROC\n";
    }
    header += "\n_TEXT\tSEGMENT\n";

    text_ = header + text_;
    raw("_TEXT\tENDS");
    if (globalSlots_ > 0) {
        blank();
        raw("_BSS\tSEGMENT");
        raw(globalsLabel() + "\tQWORD\t" + std::to_string(globalSlots_) + " DUP (0)");
        raw("_BSS\tENDS");
    }
    if (!data_.empty()) {
        blank();
        openConstSection();
        text_ += data_;
        closeConstSection();
    }
    // -g: the functions' and the globals' CodeView, carried by GRIDE's masm
    if (debugInfo() && !debugFunctions_.empty()) {
        std::string gnu;
        writeCodeView(gnu, codeViewPath(compDir_, sourceName_ + ".obj"), debugFunctions_, debugGlobals_, globalsLabel());
        text_ += masmCodeView(gnu);
    }
    raw("END");
}

}
