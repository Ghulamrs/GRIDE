#pragma once

#include "Backend.h"
#include "Dwarf.h"
#include "Optimizer.h"
#include "Spelling.h"
#include "Walker.h"

#include <iosfwd>
#include <sstream>
#include <string>
#include <vector>

class LinuxX86_64Target final : public Target {
public:
    int sizeOf(Kind) const override;
    int alignOf(Kind) const override;
    bool plainCharIsSigned() const override { return true; }
    Kind sizeType() const override { return Kind::ULong; }
    Kind wcharType() const override { return Kind::Int; }
    const char *name() const override { return "x86_64-linux"; }
};

class X86_64LinuxBackend final : public Backend {
public:
    const char *name() const override { return "x86_64-linux"; }
    const Target &target() const override { return target_; }
    const Abi &abi() const override;
    bool emits() const override { return true; }
    const char *const *identityMacros() const override;
    std::unique_ptr<CodeGen> codegen(std::ostream &sink) const override;
    bool emitsLineTable() const override { return true; }
private:
    LinuxX86_64Target target_;
};

class X86_64Linux : public Walker {
public:
    X86_64Linux(std::ostream &sink, const Target &target, const Abi &abi)
        : sink_(sink), target_(target), abi_(abi) {}

    using Walker::visit;
    void run(const Program &program) override;

    void visit(const Num &) override;
    void visit(const Var &) override;
    void visit(const Assign &) override;
    void visit(const Unary &) override;
    void visit(const Binary &) override;
    void visit(const Postfix &) override;
    void visit(const Call &) override;
    void visit(const Cast &) override;
    void visit(const StrLit &) override;
    void visit(const VaStart &) override;
    void visit(const VaArg &) override;
    void visit(const MemberAccess &) override;
    void visit(const Return &) override;

protected:

    virtual bool writesDwarf() const { return true; }
    // The debug information -g writes, and the name a source file is given in it.
    virtual void writeDebug(const std::vector<DwarfFunction> &fns,
                            const std::vector<DwarfGlobal> &globals);
    virtual std::string debugFileName(const std::string &name) const { return name; }

    std::string out_;
    // Measured after the IR has written out, so the count is of real text.
    std::size_t emittedSize() override { opt_.flush(); return out_.size(); }
    // **The IR in front of whichever spelling the target chose**, and only
    // when asked: at -O0 the walker speaks to the spelling directly.
    void setOptimize(int level) override {
        if (level < 1) return;
        opt_.wrap(a_, level);
        a_ = &opt_;
    }
    Spelling *a_ = &gnu_;
    // The program's enums, for a debug writer that names them (CodeView).
    const std::vector<EnumType> *enums_ = nullptr;
    Optimizer opt_;

private:
    std::vector<std::string> chunks_;
    std::vector<DwarfFunction> dwarfFns_;
    std::vector<DwarfGlobal> dwarfGlobals_;
    std::ostream &sink_;
    GnuSpelling gnu_{out_};

    const Target &target_;
    const Abi &abi_;
    int depth_ = 0;
    std::string returnLabel_;
    void emitLoc(int file, int line, int column) override { a_->location(file, line, column); }
    void defineLabel(const std::string &l) override;
    void jump(const std::string &l) override;
    void branchIfZero(const std::string &l) override;
    void branchIfNotZero(const std::string &l) override;
    void caseBranch(long long v, const std::string &l) override;
    std::string labelPrefix_;
    int sretSlot_ = 0;
    int regSave_ = 0;
    int varGp_ = 0, varFp_ = 48, varOverflow_ = 16;

    void emit(const Function &fn);
    // The function's closing brace, the return that is its last statement if one is, and
    // the call that return returns if it returns one: cl gives what follows each the brace's line.
    std::size_t closingBrace_ = 0;
    const Return *finalReturn_ = nullptr;
    const Call *finalCall_ = nullptr;
    void jumpToReturn(const Return &n);
    void finishChunk();
    std::string label(const char *kind, int id) const override;
    std::string userLabel(const std::string &name) const override;
    void emitData(const Program &program);
    void emitGlobal(const Global &g, Segment seg);
    void push();
    void pop(const char *into);
    void pushF();
    void popF(const char *into);

    void pushX87();
    void popX87();

    bool isX87(const Type *t) const { return t->isX87(target_); }

    Kind genKind(const Type *t) const;

    void loadX87Const(long double v);
    void x87ToInt(const Type *to);
    void intToX87(const Type *from);
    void genX87Binary(const Binary &n);

    void genAddr(const Expr &e);

    void load(const Type *t);
    void store(const Type *t);
    void storeAt(const Type *t, int offset);
    void bitFieldUnitAddr(const MemberAccess &m);
    void bitFieldExtract(const MemberAccess &m);
    void bitFieldInsert(const MemberAccess &m);

    void copyBlock(int size);
    // The last lane of an aggregate is composed, never approximated: exactly
    // `left` bytes, never one past the object, where a single widened move
    // took the largest power of two and lost a 3-byte struct's third byte.
    void storeTailFromReg(const char *reg64, long long off, const char *base, int left);   // clobbers reg64
    void loadTailToReg(const char *reg64, long long off, const char *base, int left);

    void canonicalise(const Type *t);
    void genFloatBinary(const Binary &n);
    void genConversion(const Type *from, const Type *to);
    void genTruth(const Expr &e) override;

    const char *acc(const Type *t) const;
    const char *rhs(const Type *t) const;

    void unsupported(const char *what);

    void msAggregateToRax(const Type *t, int slot);
    void msCopyToSlot(const Type *t, int slot, const char *from);
    int takeSlot(bool sse, int &ints, int &sses) const;
};

std::vector<bool> classifyEightbytes(const Type *t, const Target &target);
