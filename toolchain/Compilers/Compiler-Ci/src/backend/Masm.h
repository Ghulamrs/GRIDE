#pragma once

#include "Backend.h"
#include "Spelling.h"
#include "X86_64Linux.h"

#include <iosfwd>
#include <set>
#include <string>
#include <vector>

class MasmSpelling final : public Spelling {
public:
    explicit MasmSpelling(std::string &o) : o_(o) {}

    void ins(const std::string &m) override;
    void ins(const std::string &m, const Op &a) override;
    void ins(const std::string &m, const Op &a, const Op &b) override;

    void defLabel(const std::string &l) override;
    void functionBegin(const std::string &name, bool exported) override;
    void prologue(int frameSize) override;
    void functionEnd(const std::string &name) override;

    void globl(const std::string &name) override;
    void textSection() override;
    void rodataSection() override;
    void dataSection() override;
    void bssSection() override;
    void objectType(const std::string &name) override;
    void objectSize(const std::string &name, int size) override;
    void align(int n) override;
    void zero(int n) override;
    void dataInt(int size, long long v) override;
    void dataSym(const std::string &sym, long long off) override;
    void dataBytes(const std::string &bytes) override;

    // -g: the source files and lines, in the directives GRIDE's masm reads (ml64 has none)
    void fileEntry(int n, const std::string &name) override;
    void location(int file, int line, int column) override;
    // -g: the CodeView writeCodeView makes for clang, spelled for GRIDE's masm (see Masm.cpp)
    void codeView(const std::string &gnu);

    void predefine(const std::vector<std::string> &names) override;
    void preamble(std::ostream &sink) override;
    void postamble(std::ostream &sink) override;

private:
    std::string &o_;
    enum Seg { None, Code, Data, Const, Bss } seg_ = None;

    // -g (a `.FILE` was written): each function gets the labels its S_GPROC32 is measured by,
    // after its prologue and after its code - CoffSpelling's prologEnd and codeEnd
    bool debug_ = false;
    std::string function_;

    std::string pending_;

    std::set<std::string> defined_, exported_, referenced_, unreserved_;

    // The line entries of the function being written: the last `.LINE` and where it went (one no
    // instruction follows is replaced by the next), and every (file, line) given a run already.
    bool inFunction_ = false;
    std::string lastLoc_;
    std::size_t lastLocAt_ = 0;
    int lastLocFile_ = 0, lastLocLine_ = 0;
    std::set<std::pair<int, int> > linesRun_;

    struct Rendered {
        std::string text;
        bool isMem = false, isImm = false, isXmm = false;
    };
    Rendered render(const Op &x);
    std::string mangle(const std::string &name);
    std::string mangleNames(const std::string &expr);
    void flushPending();
    void items(const char *dir, const std::vector<std::string> &it);
};

class MasmCodeGen final : public X86_64Linux {
public:
    MasmCodeGen(std::ostream &sink, const Target &target, const Abi &abi)
        : X86_64Linux(sink, target, abi), masmTarget_(target), masm_(out_) { a_ = &masm_; }

private:
    // -g: the variables and types as CodeView, which masm carries into the object (writeDebug)
    bool writesDwarf() const override { return true; }
    void writeDebug(const std::vector<DwarfFunction> &fns,
                    const std::vector<DwarfGlobal> &globals) override;
    // -g: absolute, as cdb matches a breakpoint's file by its path; a MASM string, not escaped
    std::string debugFileName(const std::string &name) const override;
    bool marksClosingBrace() const override { return true; }

    const Target &masmTarget_;
    MasmSpelling masm_;
};
