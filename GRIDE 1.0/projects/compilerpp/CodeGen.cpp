// CodeGen.cpp
//
// C++98 only.

#include "CodeGen.h"

#include <cstddef>

namespace {

// Every value on the operand stack is 8 bytes, so a register slot is too.
const int RegSize = 8;

void put64(std::vector<unsigned char> &mem, std::size_t at, vmword value) {
    for (int i = 0; i < 8; ++i) {
        mem[at + i] = static_cast<unsigned char>((value >> (i * 8)) & 0xFF);
    }
}

Instr make(OpCode op, vmword imm = 0, vmword b = 0, int line = 0) {
    Instr n(op);
    n.imm = imm;
    n.b = b;
    n.line = line;
    return n;
}

} // namespace

CodeGen::CodeGen(Diagnostics &d) : diag(d) {}

// --- symbols ---

void CodeGen::collectSymbols(const IRModule &module, Image &out) {
    for (std::size_t i = 0; i < module.functions.size(); ++i) {
        const std::string &name = module.functions[i]->name;
        functionIndex[name] = static_cast<int>(i);
        if (name == "main") out.entry = static_cast<int>(i);
        // Named rather than included: pulling in Lower.h here would tie the
        // back end to the pass in front of it for one string.
        if (name == "__global_fini") out.fini = static_cast<int>(i);
    }
}

// Globals, strings and vtables all become bytes in one image, so that a
// pointer to any of them is an ordinary address.
void CodeGen::layoutStaticData(const IRModule &module, Image &out) {
    // Address 0 is reserved, so a null pointer is distinguishable.
    out.staticData.assign(8, 0);

    for (std::size_t g = 0; g < module.globals.size(); ++g) {
        const IRGlobal &gl = module.globals[g];
        staticAddress[gl.name] = static_cast<long>(out.staticData.size());
        out.staticData.resize(out.staticData.size() + (gl.size > 0 ? gl.size : 1), 0);
    }

    for (std::size_t s = 0; s < module.strings.size(); ++s) {
        const IRString &st = module.strings[s];
        staticAddress[st.name] = static_cast<long>(out.staticData.size());
        for (std::size_t c = 0; c < st.value.size(); ++c) {
            out.staticData.push_back(static_cast<unsigned char>(st.value[c]));
        }
        out.staticData.push_back(0);            // NUL, so print_string can stop
    }

    // A vtable is an array of function indices.  Reading slot n is then a load
    // at vtableAddress + n * 8, which is what OP_VTableLoad does.
    for (std::size_t v = 0; v < module.vtables.size(); ++v) {
        const IRVTable &vt = module.vtables[v];
        // Align, so the 8-byte reads below are on natural boundaries.
        while (out.staticData.size() % 8) out.staticData.push_back(0);
        const std::size_t at = out.staticData.size();
        staticAddress[mangleVTable(vt.className)] = static_cast<long>(at);
        out.staticData.resize(at + vt.slots.size() * 8, 0);
        for (std::size_t s = 0; s < vt.slots.size(); ++s) {
            std::map<std::string, int>::const_iterator it = functionIndex.find(vt.slots[s]);
            const long target = (it == functionIndex.end()) ? -1 : it->second;
            put64(out.staticData, at + s * 8, target);
        }
    }
}

void CodeGen::generate(IRModule &module, Image &out) {
    collectSymbols(module, out);
    layoutStaticData(module, out);
    out.functions.resize(module.functions.size());
    for (std::size_t i = 0; i < module.functions.size(); ++i) {
        IRFunction &fn = *module.functions[i];
        generateFunction(fn, out.functions[i]);
        // Released here rather than when the module dies.  Held to the end,
        // the IR and the finished image are both whole at the same moment and
        // that moment is the compile's high-water mark -- while this half of
        // it has been dead since the line above.  The C++98 way to make a
        // vector give its memory back is to swap it with an empty one;
        // `clear()` keeps the capacity, which is the whole of what is wanted
        // back.
        std::vector<IRInstr>().swap(fn.code);
    }
    if (out.entry < 0) diag.error(0, 0, "no 'main' function to run");
}

// --- one function ---

void CodeGen::generateFunction(const IRFunction &fn, FuncImage &out) {
    out.name = fn.name;
    out.paramCount = fn.paramCount;
    out.registerCount = fn.registerCount();

    // Locals first, at their declared widths; the registers follow, uniformly
    // 8 bytes each.  Both live in one frame so an address into either is an
    // ordinary address.
    int offset = 0;
    for (std::size_t i = 0; i < fn.locals.size(); ++i) {
        const int align = fn.locals[i].size >= 8 ? 8 : (fn.locals[i].size >= 4 ? 4 : 1);
        if (align > 1 && offset % align) offset += align - (offset % align);
        out.localOffset.push_back(offset);
        out.localSize.push_back(fn.locals[i].size);
        out.localFloat.push_back(fn.locals[i].isFloat ? 1 : 0);
        out.localObject.push_back(fn.locals[i].isObject ? 1 : 0);
        offset += fn.locals[i].size > 0 ? fn.locals[i].size : 1;
    }
    if (offset % 8) offset += 8 - (offset % 8);
    const int regBase = offset;
    out.frameSize = regBase + out.registerCount * RegSize;

    // Registers are addressed by index; the VM adds regBase itself, so it is
    // recorded here rather than folded into every instruction.
    out.localOffset.push_back(regBase);         // sentinel: where registers start

    std::map<vmword, int> labelAt;                // IR label id -> instruction index

    for (std::size_t i = 0; i < fn.code.size(); ++i) {
        const IRInstr &in = fn.code[i];
        const int line = in.line;

        switch (in.op) {
        case IR_Label:
            labelAt[in.imm] = static_cast<int>(out.code.size());
            continue;

        case IR_Const:
            out.code.push_back(make(OP_PushConst, in.imm, 0, line));
            break;
        case IR_FConst: {
            Instr n(OP_PushFConst);
            n.fimm = in.fimm;
            n.line = line;
            out.code.push_back(n);
            break;
        }
        case IR_StringAddr:
        case IR_GlobalAddr: {
            std::map<std::string, long>::const_iterator it = staticAddress.find(in.sym);
            const long addr = (it == staticAddress.end()) ? 0 : it->second;
            if (it == staticAddress.end()) {
                diag.error(line, 0, "internal: unknown static symbol '" + in.sym + "'");
            }
            out.code.push_back(make(OP_StaticAddr, addr, 0, line));
            break;
        }
        case IR_FuncAddr: {
            std::map<std::string, int>::const_iterator it = functionIndex.find(in.sym);
            out.code.push_back(make(OP_FuncAddr,
                                    it == functionIndex.end() ? -1 : it->second, 0, line));
            break;
        }
        case IR_LocalAddr:
            out.code.push_back(make(OP_LocalAddr, in.imm, 0, line));
            break;
        case IR_FieldAddr:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_FieldAddr, in.imm, 0, line));
            break;

        // The flag word says how the bits travel: bit 0 sign-extends an
        // integer, bit 1 marks a floating value, whose 4-byte form is a real
        // conversion and not the low half of a double.
        case IR_Load:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_Load, in.imm, in.isFloat ? 2 : 1, line));
            break;
        case IR_Store:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_LoadReg, in.b, 0, line));
            out.code.push_back(make(OP_Store, in.imm, in.isFloat ? 2 : 0, line));
            continue;                            // no destination register

        case IR_MemCopy:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));   // dst
            out.code.push_back(make(OP_LoadReg, in.b, 0, line));   // src
            out.code.push_back(make(OP_MemCopy, in.imm, 0, line));
            continue;

        case IR_Call:
        case IR_CallIndirect: {
            if (in.op == IR_CallIndirect) out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            for (std::size_t k = 0; k < in.args.size(); ++k) {
                out.code.push_back(make(OP_LoadReg, in.args[k], 0, line));
            }
            if (in.op == IR_CallIndirect) {
                out.code.push_back(make(OP_CallIndirect, 0,
                                        static_cast<long>(in.args.size()), line));
            } else {
                const NativeId nat = nativeByName(in.sym);
                std::map<std::string, int>::const_iterator it = functionIndex.find(in.sym);
                if (it != functionIndex.end()) {
                    out.code.push_back(make(OP_Call, it->second,
                                            static_cast<long>(in.args.size()), line));
                } else if (nat != NAT_Count) {
                    // Declared without a body and named like a native: the
                    // declaration IS the binding.
                    out.code.push_back(make(OP_Native, nat,
                                            static_cast<long>(in.args.size()), line));
                } else {
                    diag.error(line, 0, "'" + in.sym + "' is declared but never defined");
                    out.code.push_back(make(OP_PushConst, 0, 0, line));
                }
            }
            if (in.dest == IR_NoReg) out.code.push_back(make(OP_Pop, 0, 0, line));
            break;
        }

        case IR_VCallTarget:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_VTableLoad, in.imm, 0, line));
            break;

        case IR_Alloc:
            if (in.a != IR_NoReg) {
                // The count goes on first, so the size is on top: the machine
                // pops the size it always pops, and the count only when told.
                if (in.b != IR_NoReg) out.code.push_back(make(OP_LoadReg, in.b, 0, line));
                out.code.push_back(make(OP_LoadReg, in.a, 0, line));
                out.code.push_back(make(OP_AllocN, 0, in.b != IR_NoReg ? 1 : 0, line));
            } else {
                out.code.push_back(make(OP_Alloc, in.imm, 0, line));
            }
            break;
        case IR_ArrayCount:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_ArrayCount, 0, 0, line));
            break;
        case IR_Free:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_Free, 0, in.isArray ? 1 : 0, line));
            continue;

        case IR_Jump:
            out.code.push_back(make(OP_Jump, in.imm, 0, line));
            continue;
        case IR_BranchZero:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_BranchZero, in.imm, 0, line));
            continue;
        case IR_BranchNZ:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_BranchNZ, in.imm, 0, line));
            continue;
        case IR_Return:
            if (in.a != IR_NoReg) {
                out.code.push_back(make(OP_LoadReg, in.a, 0, line));
                out.code.push_back(make(OP_Return, 0, 0, line));
            } else {
                out.code.push_back(make(OP_ReturnVoid, 0, 0, line));
            }
            continue;

        case IR_IntToFloat:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_IntToFloat, in.imm, 0, line));
            break;
        case IR_FloatToInt:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_FloatToInt, in.imm, 0, line));
            break;
        case IR_FloatResize:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_FloatResize, in.imm, 0, line));
            break;
        case IR_IntResize:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_IntResize, in.imm, in.b == 1 ? 1 : 0, line));
            break;

        case IR_Move:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            break;

        default: {
            // The remaining opcodes are all unary or binary arithmetic, and
            // map one to one.
            OpCode op = OP_Add;
            bool binary = true;
            switch (in.op) {
            case IR_Add: op = OP_Add; break;
            case IR_Sub: op = OP_Sub; break;
            case IR_Mul: op = OP_Mul; break;
            case IR_Div: op = OP_Div; break;
            case IR_Mod: op = OP_Mod; break;
            case IR_UDiv: op = OP_UDiv; break;
            case IR_UMod: op = OP_UMod; break;
            case IR_Shl:  op = OP_Shl;  break;
            case IR_Shr:  op = OP_Shr;  break;
            case IR_UShr: op = OP_UShr; break;
            case IR_FAdd: op = OP_FAdd; break;
            case IR_FSub: op = OP_FSub; break;
            case IR_FMul: op = OP_FMul; break;
            case IR_FDiv: op = OP_FDiv; break;
            case IR_CmpEQ: op = OP_CmpEQ; break;
            case IR_CmpNE: op = OP_CmpNE; break;
            case IR_CmpLT: op = OP_CmpLT; break;
            case IR_CmpGT: op = OP_CmpGT; break;
            case IR_CmpLE: op = OP_CmpLE; break;
            case IR_CmpGE: op = OP_CmpGE; break;
            case IR_UCmpLT: op = OP_UCmpLT; break;
            case IR_UCmpGT: op = OP_UCmpGT; break;
            case IR_UCmpLE: op = OP_UCmpLE; break;
            case IR_UCmpGE: op = OP_UCmpGE; break;
            case IR_FCmpEQ: op = OP_FCmpEQ; break;
            case IR_FCmpNE: op = OP_FCmpNE; break;
            case IR_FCmpLT: op = OP_FCmpLT; break;
            case IR_FCmpGT: op = OP_FCmpGT; break;
            case IR_FCmpLE: op = OP_FCmpLE; break;
            case IR_FCmpGE: op = OP_FCmpGE; break;
            case IR_Neg: op = OP_Neg; binary = false; break;
            case IR_FNeg: op = OP_FNeg; binary = false; break;
            case IR_LogicalNot: op = OP_Not; binary = false; break;
            default:
                diag.error(line, 0, "internal: no bytecode for this IR opcode");
                break;
            }
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            if (binary) out.code.push_back(make(OP_LoadReg, in.b, 0, line));
            out.code.push_back(make(op, 0, 0, line));
            break;
        }
        }

        if (in.dest != IR_NoReg) {
            out.code.push_back(make(OP_StoreReg, in.dest, 0, line));
        }
    }

    out.code.push_back(make(OP_ReturnVoid));
    resolveLabels(fn, out, labelAt);
}

// IR labels are ids that mean nothing to the VM; branches need offsets.
void CodeGen::resolveLabels(const IRFunction &, FuncImage &out,
                            const std::map<vmword, int> &labelAt) {
    for (std::size_t i = 0; i < out.code.size(); ++i) {
        Instr &n = out.code[i];
        if (n.op != OP_Jump && n.op != OP_BranchZero && n.op != OP_BranchNZ) continue;
        std::map<vmword, int>::const_iterator it = labelAt.find(n.imm);
        if (it == labelAt.end()) {
            diag.error(n.line, 0, "internal: branch to an unplaced label");
            n.imm = static_cast<long>(out.code.size() - 1);
        } else {
            n.imm = it->second;
        }
    }
}
