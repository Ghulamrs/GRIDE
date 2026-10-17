// IR.h -- PASS 5a, the intermediate representation.
//
// There is no IR1.h, and that is the point.  Lowering's whole job is to erase
// the C++ layer -- a method becomes a function taking `this`, a reference
// becomes a pointer, a field becomes an address plus an offset, a virtual call
// becomes load-vptr / index / call.  By the time anything reaches this file
// there are no classes, references, inheritance or dispatch left.  So the two
// lowering PASSES are layered as usual (Lower.h, Lower1.h) but both target
// this one C-level instruction set.
//
// Shape: a flat list of three-address instructions per function over unlimited
// virtual registers, with labels rather than basic blocks.  Linear is easier
// to read and lowers directly to either a stack VM or emitted C.
//
// C++98 only.

#ifndef IR_H
#define IR_H

#include <cstddef>
#include <string>
#include <vector>

#include "AST.h"

// No register allocation here -- that is the code generator's problem.
typedef int IRReg;
const IRReg IR_NoReg = -1;

enum IROp {
    // --- values -------------------------------------------------------
    IR_Const,        // dest = imm
    IR_FConst,       // dest = fimm
    IR_StringAddr,   // dest = address of string constant `sym`
    IR_Move,         // dest = a

    // --- integer arithmetic -------------------------------------------
    IR_Add, IR_Sub, IR_Mul, IR_Div, IR_Mod,
    IR_UDiv, IR_UMod,       // unsigned division differs from signed
    IR_Shl, IR_Shr,  // dest = a << b, a >> b  (>> is arithmetic when signed)
    IR_UShr,         // logical shift right, for an unsigned left operand
    IR_Neg,          // dest = -a
    IR_LogicalNot,   // dest = (a == 0)

    // --- floating arithmetic ------------------------------------------
    // Separate opcodes rather than a flag: float add and integer add are
    // different machine operations, and a dump that hides that is lying.
    IR_FAdd, IR_FSub, IR_FMul, IR_FDiv, IR_FNeg,

    // --- comparison (all yield 0 or 1) --------------------------------
    IR_CmpEQ, IR_CmpNE, IR_CmpLT, IR_CmpGT, IR_CmpLE, IR_CmpGE,
    IR_UCmpLT, IR_UCmpGT, IR_UCmpLE, IR_UCmpGE,     // unsigned orderings
    IR_FCmpEQ, IR_FCmpNE, IR_FCmpLT, IR_FCmpGT, IR_FCmpLE, IR_FCmpGE,

    // --- conversions --------------------------------------------------
    // Every one of these is a real machine operation, so lowering emits them
    // explicitly rather than letting a size mismatch pass silently.
    IR_IntToFloat,   // imm = 1 when the source is unsigned
    IR_FloatToInt,
    IR_FloatResize,  // float <-> double
    IR_IntResize,    // imm = target size in bytes; b = 1 when sign-extending

    // --- addresses ----------------------------------------------------
    IR_LocalAddr,    // dest = address of local slot imm
    IR_GlobalAddr,   // dest = address of global `sym`
    IR_FieldAddr,    // dest = a + imm        -- a field at a constant offset
    IR_FuncAddr,     // dest = address of function `sym`

    // --- memory -------------------------------------------------------
    IR_Load,         // dest = *a             (imm = size in bytes)
    IR_Store,        // *a = b                (imm = size in bytes)
    // An object is copied whole.  A class does not fit in a register, so
    // load-then-store would shift its bytes off the end of one.
    IR_MemCopy,      // *a = *b               (imm = size in bytes)

    // --- calls --------------------------------------------------------
    IR_Call,         // dest = sym(args...)
    IR_CallIndirect, // dest = (*a)(args...)  -- this is a virtual call

    // --- dispatch -----------------------------------------------------
    // One opcode rather than three loads, so a dump stays readable; the code
    // generator expands it.
    IR_VCallTarget,  // dest = (*(vptr of a))[imm]

    // --- free store ---------------------------------------------------
    IR_Alloc,        // dest = allocate imm bytes, or a bytes when a is a register;
                     // b, when set, is the element count of a new[]
    IR_ArrayCount,   // dest = how many elements the new[] block at a holds
    IR_Free,         // release a

    // --- control ------------------------------------------------------
    IR_Label,        // imm = label id
    IR_Jump,         // goto imm
    IR_BranchZero,   // if a == 0 goto imm
    IR_BranchNZ,     // if a != 0 goto imm
    IR_Return        // return a, or return nothing when a is IR_NoReg
};

const char *irOpName(IROp op);

struct IRInstr {
    IROp op;
    IRReg dest;
    IRReg a;
    IRReg b;
    long imm;
    double fimm;            // IR_FConst
    // A load or store of a floating value moves different bits than an integer
    // one of the same width -- a 4-byte float is not the low half of a double.
    bool isFloat;
    // IR_Free: which FORM of delete this is.  The allocator records the form in
    // the block, so `delete` on a `new[]` block is caught rather than left
    // undefined the way the language leaves it.  IR_Alloc says the same thing
    // by carrying an element count in `b`.
    bool isArray;
    std::string sym;            // callee or global name
    std::vector<IRReg> args;    // for IR_Call / IR_CallIndirect
    int line;

    IRInstr(IROp o)
        : op(o), dest(IR_NoReg), a(IR_NoReg), b(IR_NoReg), imm(0), fimm(0.0),
          isFloat(false), isArray(false), line(0) {}
};

// A class-typed local occupies its whole object size here, which is what makes
// `Point p;` a real object rather than a pointer to one.
struct IRLocal {
    std::string name;
    int slot;
    int size;
    bool isParam;
    // A 4-byte float slot has to be written as a float, not as four bytes of
    // an integer -- otherwise a float parameter arrives as noise.
    bool isFloat;
    // A by-value object parameter: the caller passes an ADDRESS and the VM
    // copies the bytes in, because that is what "by value" means and an
    // object does not fit in the register the argument travelled in.
    bool isObject;
    IRLocal(const std::string &n, int s, int sz, bool p, bool f = false, bool o = false)
        : name(n), slot(s), size(sz), isParam(p), isFloat(f), isObject(o) {}
};

struct IRFunction {
    std::string name;           // mangled:  Point__getX
    std::string sourceName;     // as written:  Point::getX
    int paramCount;             // includes `this` where there is one
    bool returnsValue;
    std::vector<IRLocal> locals;
    std::vector<IRInstr> code;

    IRFunction(const std::string &mangled, const std::string &source)
        : name(mangled), sourceName(source), paramCount(0),
          returnsValue(false), nextReg(0), nextLabel(0) {}

    IRReg newReg() { return nextReg++; }
    int newLabel() { return nextLabel++; }
    int addLocal(const std::string &n, int size, bool isParam, bool isFloat = false,
                 bool isObject = false);

    // Every emit returns its destination register, so expressions compose.
    IRReg emitConst(long value, int line);
    IRReg emitFConst(double value, int line);
    IRReg emitStringAddr(const std::string &sym, int line);
    IRReg emitConvert(IROp op, IRReg a, long imm, IRReg signFlag, int line);
    IRReg emitUnary(IROp op, IRReg a, int line);
    IRReg emitBinary(IROp op, IRReg a, IRReg b, int line);
    IRReg emitLocalAddr(int slot, int line);
    IRReg emitGlobalAddr(const std::string &sym, int line);
    IRReg emitFieldAddr(IRReg base, long offset, int line);
    IRReg emitFuncAddr(const std::string &sym, int line);
    IRReg emitLoad(IRReg addr, int size, bool isFloat, int line);
    void  emitStore(IRReg addr, IRReg value, int size, bool isFloat, int line);
    void  emitMemCopy(IRReg dst, IRReg src, int size, int line);
    IRReg emitCall(const std::string &sym, const std::vector<IRReg> &args,
                   bool wantsResult, int line);
    IRReg emitCallIndirect(IRReg target, const std::vector<IRReg> &args,
                           bool wantsResult, int line);
    IRReg emitVCallTarget(IRReg object, long slot, int line);
    IRReg emitAlloc(long bytes, int line);
    // The size in a register, and for new[] the element count beside it.  The
    // count is stored in the block: the SIZE cannot stand in for it, because
    // the allocator rounds a block up and five four-byte elements would come
    // back as six.
    IRReg emitAllocN(IRReg bytes, IRReg count, int line);
    IRReg emitArrayCount(IRReg ptr, int line);
    void  emitFree(IRReg ptr, int line, bool isArray = false);
    void  emitLabel(int label);
    void  emitJump(int label, int line);
    void  emitBranchZero(IRReg cond, int label, int line);
    void  emitBranchNZ(IRReg cond, int label, int line);
    void  emitReturn(IRReg value, int line);

    int registerCount() const { return nextReg; }
    // Anything after a return or jump cannot run, so lowering asks before
    // emitting a function's trailing code.
    bool endsWithTerminator() const;

private:
    int nextReg;
    int nextLabel;
    void push(const IRInstr &i) { code.push_back(i); }
};

struct IRGlobal {
    std::string name;
    int size;
    IRGlobal(const std::string &n, int s) : name(n), size(s) {}
};

// Read-only text, so a char* has something to point at.
struct IRString {
    std::string name;       // str0, str1, ...
    std::string value;
    IRString(const std::string &n, const std::string &v) : name(n), value(v) {}
};

// Data, not code.  Slot indices come from Layout, which is why they mean the
// same thing in a base and everything derived from it.
struct IRVTable {
    std::string className;
    std::vector<std::string> slots;     // mangled function names
};

struct IRModule {
    std::vector<IRFunction*> functions;
    std::vector<IRGlobal> globals;
    std::vector<IRString> strings;
    std::vector<IRVTable> vtables;

    // Interned: the same text used twice is one constant.
    std::string internString(const std::string &value);

    ~IRModule();
    void print() const;

private:
    static void printInstr(const IRInstr &i);
};

// One flat namespace of symbols, so the owning class folds into the name.
// Overloading means a name is no longer a symbol.  The signature is folded in
// so that add(int,int) and add(double,double) become different symbols, which
// is the whole reason real C++ mangles names at all.
std::string mangleSignature(const std::vector<cc::VarDecl*> &params);
std::string mangleFunction(const std::string &className, const std::string &name);
std::string mangleOverload(const std::string &className, const std::string &name,
                           const std::vector<cc::VarDecl*> &params,
                           bool isConstMethod = false);
std::string mangleConstructor(const std::string &className,
                              const std::vector<cc::VarDecl*> &params);
std::string mangleDestructor(const std::string &className);
std::string mangleVTable(const std::string &className);

#endif
