// IR.cpp
//
// C++98 only.  See IR.h for why this layer has no cxx:: counterpart.

#include "IR.h"

#include <cstddef>
#include <iostream>
#include <sstream>

#include "AST1.h"

const char *irOpName(IROp op) {
    switch (op) {
    case IR_Const:        return "const";
    case IR_FConst:       return "fconst";
    case IR_StringAddr:   return "straddr";
    case IR_Move:         return "move";
    case IR_Add:          return "add";
    case IR_Sub:          return "sub";
    case IR_Mul:          return "mul";
    case IR_Div:          return "div";
    case IR_Mod:          return "mod";
    case IR_UDiv:         return "udiv";
    case IR_UMod:         return "umod";
    case IR_FAdd:         return "fadd";
    case IR_FSub:         return "fsub";
    case IR_FMul:         return "fmul";
    case IR_FDiv:         return "fdiv";
    case IR_FNeg:         return "fneg";
    case IR_Shl:          return "shl";
    case IR_Shr:          return "shr";
    case IR_UShr:         return "ushr";
    case IR_Neg:          return "neg";
    case IR_LogicalNot:   return "not";
    case IR_CmpEQ:        return "cmp.eq";
    case IR_CmpNE:        return "cmp.ne";
    case IR_CmpLT:        return "cmp.lt";
    case IR_CmpGT:        return "cmp.gt";
    case IR_CmpLE:        return "cmp.le";
    case IR_CmpGE:        return "cmp.ge";
    case IR_UCmpLT:       return "ucmp.lt";
    case IR_UCmpGT:       return "ucmp.gt";
    case IR_UCmpLE:       return "ucmp.le";
    case IR_UCmpGE:       return "ucmp.ge";
    case IR_FCmpEQ:       return "fcmp.eq";
    case IR_FCmpNE:       return "fcmp.ne";
    case IR_FCmpLT:       return "fcmp.lt";
    case IR_FCmpGT:       return "fcmp.gt";
    case IR_FCmpLE:       return "fcmp.le";
    case IR_FCmpGE:       return "fcmp.ge";
    case IR_IntToFloat:   return "itof";
    case IR_FloatToInt:   return "ftoi";
    case IR_FloatResize:  return "fresize";
    case IR_IntResize:    return "iresize";
    case IR_LocalAddr:    return "local";
    case IR_GlobalAddr:   return "global";
    case IR_FieldAddr:    return "field";
    case IR_FuncAddr:     return "funcaddr";
    case IR_Load:         return "load";
    case IR_Store:        return "store";
    case IR_MemCopy:      return "memcpy";
    case IR_Call:         return "call";
    case IR_CallIndirect: return "call.ind";
    case IR_VCallTarget:  return "vtable";
    case IR_Alloc:        return "alloc";
    case IR_ArrayCount:   return "arraycount";
    case IR_Free:         return "free";
    case IR_Label:        return "label";
    case IR_Jump:         return "jump";
    case IR_BranchZero:   return "brz";
    case IR_BranchNZ:     return "brnz";
    case IR_Return:       return "ret";
    }
    return "?";
}

// --- name mangling ----------------------------------------------------

// A short code per type: i d c s l f v, P for a pointer, R for a reference,
// and a class by name.  Readable in a dump, and unique.
static std::string typeCode(cc::Type *t) {
    if (!t) return "v";
    // The mangler and the semantic pass must mean the same thing by
    // "signature", or two distinct overloads end up sharing one symbol.
    // 'K' is const, as in the Itanium ABI this borrows its spirit from.
    const std::string k = t->isConst ? "K" : "";
    if (cc::PointerType *pt = dynamic_cast<cc::PointerType*>(t)) return k + "P" + typeCode(pt->base);
    if (cc::ArrayType *at = dynamic_cast<cc::ArrayType*>(t)) return k + "P" + typeCode(at->element);
    if (cc::BuiltinType *bt = dynamic_cast<cc::BuiltinType*>(t)) {
        switch (bt->kind) {
        case cc::BK_Void:   return k + "v";
        case cc::BK_Char:   return k + "c";
        case cc::BK_SChar:  return k + "a";
        case cc::BK_UChar:  return k + "h";
        case cc::BK_Short:  return k + "s";
        case cc::BK_UShort: return k + "t";
        case cc::BK_Int:    return k + "i";
        case cc::BK_UInt:   return k + "j";
        case cc::BK_Long:   return k + "l";
        case cc::BK_ULong:  return k + "m";
        case cc::BK_Float:  return k + "f";
        case cc::BK_Double: return k + "d";
        }
    }
    if (dynamic_cast<cxx::BoolType*>(t)) return k + "b";
    if (cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(t)) {
        return "R" + typeCode(rt->base);
    }
    if (cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(t)) {
        std::ostringstream ss;
        ss << k << ct->className.size() << ct->className;
        return ss.str();
    }
    return "X";
}

std::string mangleSignature(const std::vector<cc::VarDecl*> &params) {
    if (params.empty()) return "v";
    std::string out;
    for (std::size_t i = 0; i < params.size(); ++i) {
        out += typeCode(params[i]->type);
    }
    return out;
}

std::string mangleOverload(const std::string &className, const std::string &name,
                           const std::vector<cc::VarDecl*> &params, bool isConstMethod) {
    // A const member function is a different function from its non-const twin,
    // so it needs a different symbol -- otherwise the pair a container declares
    // would collide.
    return mangleFunction(className, name) + "$" + mangleSignature(params)
         + (isConstMethod ? "K" : "");
}

std::string mangleFunction(const std::string &className, const std::string &name) {
    if (className.empty()) return name;
    return className + "__" + name;
}

// Constructors overload by argument count, so the count distinguishes them.
std::string mangleConstructor(const std::string &className,
                              const std::vector<cc::VarDecl*> &params) {
    // By SIGNATURE, not by argument count: P(int,int) and P(double,double) are
    // two constructors, and encoding only the arity gave them one symbol.
    return className + "__ctor$" + mangleSignature(params);
}

std::string mangleDestructor(const std::string &className) {
    return className + "__dtor";
}

std::string mangleVTable(const std::string &className) {
    return className + "__vtable";
}

// --- the builder ------------------------------------------------------

int IRFunction::addLocal(const std::string &n, int size, bool isParam, bool isFloat,
                         bool isObject) {
    const int slot = static_cast<int>(locals.size());
    locals.push_back(IRLocal(n, slot, size, isParam, isFloat, isObject));
    return slot;
}

bool IRFunction::endsWithTerminator() const {
    if (code.empty()) return false;
    const IROp last = code.back().op;
    return last == IR_Return || last == IR_Jump;
}

IRReg IRFunction::emitConst(long value, int line) {
    IRInstr i(IR_Const);
    i.dest = newReg();
    i.imm = value;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitFConst(double value, int line) {
    IRInstr i(IR_FConst);
    i.dest = newReg();
    i.fimm = value;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitStringAddr(const std::string &sym, int line) {
    IRInstr i(IR_StringAddr);
    i.dest = newReg();
    i.sym = sym;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitConvert(IROp op, IRReg a, long imm, IRReg signFlag, int line) {
    IRInstr i(op);
    i.dest = newReg();
    i.a = a;
    i.b = signFlag;
    i.imm = imm;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitUnary(IROp op, IRReg a, int line) {
    IRInstr i(op);
    i.dest = newReg();
    i.a = a;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitBinary(IROp op, IRReg a, IRReg b, int line) {
    IRInstr i(op);
    i.dest = newReg();
    i.a = a;
    i.b = b;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitLocalAddr(int slot, int line) {
    IRInstr i(IR_LocalAddr);
    i.dest = newReg();
    i.imm = slot;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitGlobalAddr(const std::string &sym, int line) {
    IRInstr i(IR_GlobalAddr);
    i.dest = newReg();
    i.sym = sym;
    i.line = line;
    push(i);
    return i.dest;
}

// Offset zero still emits, so a dump shows every member access as one step.
IRReg IRFunction::emitFieldAddr(IRReg base, long offset, int line) {
    IRInstr i(IR_FieldAddr);
    i.dest = newReg();
    i.a = base;
    i.imm = offset;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitFuncAddr(const std::string &sym, int line) {
    IRInstr i(IR_FuncAddr);
    i.dest = newReg();
    i.sym = sym;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitLoad(IRReg addr, int size, bool isFloat, int line) {
    IRInstr i(IR_Load);
    i.dest = newReg();
    i.a = addr;
    i.imm = size;
    i.isFloat = isFloat;
    i.line = line;
    push(i);
    return i.dest;
}

void IRFunction::emitStore(IRReg addr, IRReg value, int size, bool isFloat, int line) {
    IRInstr i(IR_Store);
    i.a = addr;
    i.b = value;
    i.imm = size;
    i.isFloat = isFloat;
    i.line = line;
    push(i);
}

void IRFunction::emitMemCopy(IRReg dst, IRReg src, int size, int line) {
    IRInstr i(IR_MemCopy);
    i.a = dst;
    i.b = src;
    i.imm = size;
    i.line = line;
    push(i);
}

IRReg IRFunction::emitCall(const std::string &sym, const std::vector<IRReg> &args,
                           bool wantsResult, int line) {
    IRInstr i(IR_Call);
    i.dest = wantsResult ? newReg() : IR_NoReg;
    i.sym = sym;
    i.args = args;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitCallIndirect(IRReg target, const std::vector<IRReg> &args,
                                   bool wantsResult, int line) {
    IRInstr i(IR_CallIndirect);
    i.dest = wantsResult ? newReg() : IR_NoReg;
    i.a = target;
    i.args = args;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitVCallTarget(IRReg object, long slot, int line) {
    IRInstr i(IR_VCallTarget);
    i.dest = newReg();
    i.a = object;
    i.imm = slot;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitAlloc(long bytes, int line) {
    IRInstr i(IR_Alloc);
    i.dest = newReg();
    i.imm = bytes;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitAllocN(IRReg bytes, IRReg count, int line) {
    IRInstr i(IR_Alloc);
    i.dest = newReg();
    i.a = bytes;                // a register here means "the size is this"
    i.b = count;                // and a register here means "and it is new[]"
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitArrayCount(IRReg ptr, int line) {
    IRInstr i(IR_ArrayCount);
    i.dest = newReg();
    i.a = ptr;
    i.line = line;
    push(i);
    return i.dest;
}

void IRFunction::emitFree(IRReg ptr, int line, bool isArray) {
    IRInstr i(IR_Free);
    i.a = ptr;
    i.isArray = isArray;
    i.line = line;
    push(i);
}

void IRFunction::emitLabel(int label) {
    IRInstr i(IR_Label);
    i.imm = label;
    push(i);
}

void IRFunction::emitJump(int label, int line) {
    IRInstr i(IR_Jump);
    i.imm = label;
    i.line = line;
    push(i);
}

void IRFunction::emitBranchZero(IRReg cond, int label, int line) {
    IRInstr i(IR_BranchZero);
    i.a = cond;
    i.imm = label;
    i.line = line;
    push(i);
}

void IRFunction::emitBranchNZ(IRReg cond, int label, int line) {
    IRInstr i(IR_BranchNZ);
    i.a = cond;
    i.imm = label;
    i.line = line;
    push(i);
}

void IRFunction::emitReturn(IRReg value, int line) {
    IRInstr i(IR_Return);
    i.a = value;
    i.line = line;
    push(i);
}

// --- the module and its dump ------------------------------------------

std::string IRModule::internString(const std::string &value) {
    for (std::size_t i = 0; i < strings.size(); ++i) {
        if (strings[i].value == value) return strings[i].name;
    }
    std::ostringstream ss;
    ss << "str" << strings.size();
    strings.push_back(IRString(ss.str(), value));
    return strings.back().name;
}

IRModule::~IRModule() {
    for (std::size_t i = 0; i < functions.size(); ++i) delete functions[i];
}

static std::string regName(IRReg r) {
    if (r == IR_NoReg) return "_";
    std::ostringstream ss;
    ss << "%" << r;
    return ss.str();
}

void IRModule::printInstr(const IRInstr &i) {
    std::cout << "    ";
    if (i.op == IR_Label) {
        std::cout << "L" << i.imm << ":" << std::endl;
        return;
    }
    if (i.dest != IR_NoReg) std::cout << regName(i.dest) << " = ";
    else                    std::cout << "        ";
    std::cout << irOpName(i.op);

    switch (i.op) {
    case IR_Const:
        std::cout << " " << i.imm;
        break;
    case IR_Alloc:
        // The size is a constant or a register, and a count beside it says the
        // instruction is a new[]; both show in the dump.
        if (i.a != IR_NoReg) std::cout << " " << regName(i.a);
        else                 std::cout << " " << i.imm;
        if (i.b != IR_NoReg) std::cout << " [" << regName(i.b) << "]";
        break;
    case IR_FConst:
        std::cout << " " << i.fimm;
        break;
    case IR_StringAddr:
        std::cout << " " << i.sym;
        break;
    case IR_IntResize:
        std::cout << " " << regName(i.a) << " :" << i.imm
                  << (i.b == 1 ? " signed" : " unsigned");
        break;
    case IR_IntToFloat:
        std::cout << " " << regName(i.a) << (i.imm ? " (unsigned)" : "");
        break;
    case IR_FloatToInt:
    case IR_FloatResize:
        std::cout << " " << regName(i.a) << " :" << i.imm;
        break;
    case IR_LocalAddr:
        std::cout << " #" << i.imm;
        break;
    case IR_GlobalAddr:
    case IR_FuncAddr:
        std::cout << " " << i.sym;
        break;
    case IR_FieldAddr:
        std::cout << " " << regName(i.a) << " +" << i.imm;
        break;
    case IR_Load:
        std::cout << " [" << regName(i.a) << "] :" << i.imm << (i.isFloat ? "f" : "");
        break;
    case IR_Store:
        std::cout << " [" << regName(i.a) << "] <- " << regName(i.b)
                  << " :" << i.imm << (i.isFloat ? "f" : "");
        break;
    case IR_MemCopy:
        std::cout << " [" << regName(i.a) << "] <- [" << regName(i.b)
                  << "] :" << i.imm;
        break;
    case IR_VCallTarget:
        std::cout << " [" << regName(i.a) << "] slot " << i.imm;
        break;
    case IR_Call:
    case IR_CallIndirect:
        if (i.op == IR_Call) std::cout << " " << i.sym;
        else                 std::cout << " " << regName(i.a);
        std::cout << "(";
        for (std::size_t k = 0; k < i.args.size(); ++k) {
            if (k) std::cout << ", ";
            std::cout << regName(i.args[k]);
        }
        std::cout << ")";
        break;
    case IR_Jump:
        std::cout << " L" << i.imm;
        break;
    case IR_BranchZero:
    case IR_BranchNZ:
        std::cout << " " << regName(i.a) << " L" << i.imm;
        break;
    case IR_Return:
        if (i.a != IR_NoReg) std::cout << " " << regName(i.a);
        break;
    case IR_Free:
        std::cout << " " << regName(i.a);
        if (i.isArray) std::cout << " []";
        break;
    default:
        if (i.a != IR_NoReg) std::cout << " " << regName(i.a);
        if (i.b != IR_NoReg) std::cout << ", " << regName(i.b);
        break;
    }
    std::cout << std::endl;
}

void IRModule::print() const {
    for (std::size_t s = 0; s < strings.size(); ++s) {
        std::cout << "string " << strings[s].name << "  \"" << strings[s].value
                  << "\"" << std::endl;
    }
    if (!strings.empty()) std::cout << std::endl;

    for (std::size_t g = 0; g < globals.size(); ++g) {
        std::cout << "global " << globals[g].name
                  << "  " << globals[g].size << " bytes" << std::endl;
    }
    if (!globals.empty()) std::cout << std::endl;

    for (std::size_t v = 0; v < vtables.size(); ++v) {
        const IRVTable &vt = vtables[v];
        std::cout << "vtable " << mangleVTable(vt.className) << std::endl;
        for (std::size_t s = 0; s < vt.slots.size(); ++s) {
            std::cout << "    [" << s << "] " << vt.slots[s] << std::endl;
        }
        std::cout << std::endl;
    }

    for (std::size_t f = 0; f < functions.size(); ++f) {
        const IRFunction &fn = *functions[f];
        std::cout << "function " << fn.name;
        if (fn.sourceName != fn.name) std::cout << "   ; " << fn.sourceName;
        std::cout << std::endl;
        for (std::size_t l = 0; l < fn.locals.size(); ++l) {
            const IRLocal &loc = fn.locals[l];
            std::cout << "  #" << loc.slot << " " << loc.name
                      << "  " << loc.size << " bytes"
                      << (loc.isParam ? "  (param)" : "") << std::endl;
        }
        for (std::size_t c = 0; c < fn.code.size(); ++c) printInstr(fn.code[c]);
        std::cout << std::endl;
    }
}
