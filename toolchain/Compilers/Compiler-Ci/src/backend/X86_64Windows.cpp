#include "X86_64Windows.h"
#include "X86_64Linux.h"
#include "Masm.h"
#include "CodeView.h"
#include "../Source.h"

#include <cstdio>
#include <cstdlib>

int WindowsX86_64Target::sizeOf(Kind k) const {
    switch (k) {
    case Kind::Void:                                       return 1;
    case Kind::Char: case Kind::SChar: case Kind::UChar:   return 1;
    case Kind::Short: case Kind::UShort:                   return 2;
    case Kind::Int: case Kind::UInt:                       return 4;
    case Kind::Long: case Kind::ULong:                     return 4;
    case Kind::LongLong: case Kind::ULongLong:             return 8;
    case Kind::Float:                                      return 4;
    case Kind::Double:                                     return 8;

    case Kind::LongDouble:                                 return 8;
    case Kind::Pointer:                                    return 8;
    default:
        std::fprintf(stderr, "target: no size for this type yet\n");
        std::exit(1);
    }
}

int WindowsX86_64Target::alignOf(Kind k) const { return sizeOf(k); }

static const char *const kArgRegs[] = { "%rcx", "%rdx", "%r8", "%r9" };
static const char *const kSseRegs[] = { "%xmm0", "%xmm1", "%xmm2", "%xmm3" };

static const Abi kMsAbi = {
    kArgRegs, 4,
    kSseRegs, 4,
    true,
    32,
    8,
    true,
    false,
    "%r10", "%r10d",
    false,
    false,

};

const Abi &X86_64WindowsBackend::abi() const { return kMsAbi; }

static WindowsAsm syntax_ = WindowsAsm::Masm;
void setWindowsAsmSyntax(WindowsAsm syntax) { syntax_ = syntax; }

static const char *const kWindowsMacros[] = {
    "__x86_64__=1", "__x86_64=1", "__amd64__=1", "__amd64=1",
    "_WIN32=1", "_WIN64=1", "__llp64__=1", nullptr,
};
const char *const *X86_64WindowsBackend::identityMacros() const { return kWindowsMacros; }

bool X86_64WindowsBackend::emitsLineTable() const { return syntax_ != WindowsAsm::Masm; }

bool windowsAsmIsGnu() { return syntax_ != WindowsAsm::Masm; }

namespace {

// **The GNU spelling as a COFF object**, for clang: the instruction stream is the
// Linux backend's, the sections and unwind data CoffSpelling's, and -g writes
// CodeView, since link.exe drops DWARF from a COFF object and no Microsoft tool reads it.
class CoffCodeGen final : public X86_64Linux {
public:
    CoffCodeGen(std::ostream &sink, const Target &target, const Abi &abi)
        : X86_64Linux(sink, target, abi), coffTarget_(target), coff_(out_) { a_ = &coff_; }

private:
    void writeDebug(const std::vector<DwarfFunction> &fns,
                    const std::vector<DwarfGlobal> &globals) override {
        writeCodeView(out_, coffTarget_, codeViewPath(compDir(), lineSource()->files().front() + ".obj"),
                      fns, globals, *enums_);
    }
    std::string debugFileName(const std::string &name) const override {
        return codeViewPath(compDir(), name);
    }
    bool marksClosingBrace() const override { return true; }

    const Target &coffTarget_;
    CoffSpelling coff_;
};

}

std::unique_ptr<CodeGen> X86_64WindowsBackend::codegen(std::ostream &sink) const {
    if (syntax_ == WindowsAsm::Gnu)
        return std::unique_ptr<CodeGen>(new CoffCodeGen(sink, target_, kMsAbi));
    if (syntax_ == WindowsAsm::GnuElf)
        return std::unique_ptr<CodeGen>(new X86_64Linux(sink, target_, kMsAbi));
    return std::unique_ptr<CodeGen>(new MasmCodeGen(sink, target_, kMsAbi));
}
