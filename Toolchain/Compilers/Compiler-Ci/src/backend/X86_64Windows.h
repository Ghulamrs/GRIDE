#pragma once

#include "Backend.h"

class WindowsX86_64Target final : public Target {
public:
    int sizeOf(Kind) const override;
    int alignOf(Kind) const override;
    bool plainCharIsSigned() const override { return true; }
    Kind sizeType() const override { return Kind::ULongLong; }

    Kind wcharType() const override { return Kind::UShort; }
    bool microsoftLayout() const override { return true; }
    const char *name() const override { return "x86_64-windows"; }
};

// -masm=: MASM for ml64; the GNU spelling of a COFF object, for clang; or the same
// instructions in an ELF object, which is how the Linux suites run the Microsoft convention.
enum class WindowsAsm { Masm, Gnu, GnuElf };
void setWindowsAsmSyntax(WindowsAsm syntax);
// Whether a GNU spelling was asked for: clang assembles it, ml64 only MASM.
bool windowsAsmIsGnu();

class X86_64WindowsBackend final : public Backend {
public:
    const char *name() const override { return "x86_64-windows"; }
    const Target &target() const override { return target_; }
    const Abi &abi() const override;
    bool emits() const override { return true; }
    const char *const *identityMacros() const override;
    bool emitsLineTable() const override;
    std::unique_ptr<CodeGen> codegen(std::ostream &sink) const override;
private:
    WindowsX86_64Target target_;
};
