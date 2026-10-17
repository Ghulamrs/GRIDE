// Bytecode.h -- PASS 6a, the instruction set the compiler targets.
//
// A stack machine, chosen so the compiler can RUN what it produces on the
// machine that built it.  Emitting native code for two ABIs would be more
// authentic and less useful: nothing about a calling convention teaches
// anything about C++, and cross-compiled output cannot be watched.
//
// One flat byte memory holds everything, so an address is an address whatever
// it points at -- a global, a string, a local, or the heap:
//
//     [0 .. 8)          reserved, so the null pointer is address 0
//     [static data]     globals, vtables, string literals
//     [frame stack]     one frame per active call
//     [heap]            new and delete
//
// Values on the operand stack are 8 bytes and hold either a long or a double;
// memory keeps each type at its declared width, so LOAD and STORE carry a size.
//
// C++98 only.

#ifndef BYTECODE_H
#define BYTECODE_H

#include <string>
#include <vector>

// The VM's machine word.
//
// This compiler's type model fixes `long` at 8 bytes -- see the note on the
// builtin table in AST.h -- and the VM's memory, addresses and operand stack
// are all defined in those terms.  The HOST's `long` is not the same thing:
// it is 8 bytes on macOS and Linux (LP64) but 4 on Windows (LLP64), and using
// it here silently gave the VM a 32-bit word on MSVC.  Every value wider than
// 32 bits was then truncated -- doubles through a register lost half their
// bit pattern, and `(1UL << 32) - 1` became 0, so every 4-byte integer resize
// masked its value away to nothing.
//
// C++98 has no <cstdint>, so the width is pinned per compiler.  Both spellings
// are pre-C++11 extensions their compilers have always accepted.
#if defined(_MSC_VER)
typedef __int64          vmword;
typedef unsigned __int64 uvmword;
#else
typedef long long          vmword;
typedef unsigned long long uvmword;
#endif

// How much memory the machine has.  It lives here, with the word width, rather
// than in VM.cpp, because it is not only the VM's business: an object too big
// to fit is a thing the FRONT END should refuse, at the declaration, with a
// line number -- and it cannot refuse what it cannot see.  Two constants that
// had to agree would be one more thing to drift.
const vmword MachineMemory   = 4L * 1024 * 1024;
const vmword MachineStack    = 1L * 1024 * 1024;
const vmword MachineMaxSteps = 50L * 1000 * 1000;

// The three numbers above, as a thing that can be handed to a machine rather
// than compiled into it.  A command line wants the defaults and never thinks
// about them; an application embedding this compiler has different problems.
//
// `memory` is claimed whole on every run and is most of what the process is
// holding while a program runs, so a host that runs small programs wants it
// small.  `maxSteps` is what stops `while(1){}` -- and the default is tighter
// than it looks: a plain million-iteration loop spends 49 million steps, so a
// host that wants real loops to finish must raise it and offer a Stop instead.
// `callStack` is the room for frames, taken out of `memory` before the heap
// starts.
//
// They are one struct because they are one decision: memory has to exceed the
// call stack plus whatever the program's statics need, and a host setting one
// without looking at the others gets a machine that cannot start.
struct MachineLimits {
    vmword memory;
    vmword callStack;
    vmword maxSteps;
    MachineLimits()
        : memory(MachineMemory), callStack(MachineStack), maxSteps(MachineMaxSteps) {}
};

enum OpCode {
    // --- operand stack ---
    OP_PushConst,       // push imm
    OP_PushFConst,      // push fimm
    OP_LoadReg,         // push the value of frame register imm
    OP_StoreReg,        // pop into frame register imm
    OP_Pop,

    // --- addresses ---
    OP_LocalAddr,       // push the address of local slot imm
    OP_StaticAddr,      // push imm, an absolute address in static data
    OP_FieldAddr,       // pop a, push a + imm
    OP_FuncAddr,        // push imm, a function index

    // --- memory (imm = width in bytes; `b` = 1 when sign-extending) ---
    OP_Load,
    OP_Store,
    OP_MemCopy,     // imm = byte count; pops src then dst

    // --- integer arithmetic ---
    OP_Add, OP_Sub, OP_Mul, OP_Div, OP_Mod, OP_UDiv, OP_UMod,
    OP_Shl, OP_Shr, OP_UShr,
    OP_Neg, OP_Not,

    // --- floating arithmetic ---
    OP_FAdd, OP_FSub, OP_FMul, OP_FDiv, OP_FNeg,

    // --- comparison ---
    OP_CmpEQ, OP_CmpNE, OP_CmpLT, OP_CmpGT, OP_CmpLE, OP_CmpGE,
    OP_UCmpLT, OP_UCmpGT, OP_UCmpLE, OP_UCmpGE,
    OP_FCmpEQ, OP_FCmpNE, OP_FCmpLT, OP_FCmpGT, OP_FCmpLE, OP_FCmpGE,

    // --- conversions ---
    OP_IntToFloat,      // imm = 1 when the source is unsigned
    OP_FloatToInt,
    OP_FloatResize,     // imm = target width
    OP_IntResize,       // imm = target width, b = 1 when sign-extending

    // --- control ---
    OP_Jump,            // goto imm
    OP_BranchZero,      // pop; goto imm if zero
    OP_BranchNZ,
    OP_Call,            // imm = function index, b = argument count
    OP_CallIndirect,    // pop a function index, b = argument count
    OP_VTableLoad,      // pop an object address, push its vtable's slot imm
    OP_Native,          // imm = native index, b = argument count
    OP_Return,          // pop a value and return it
    OP_ReturnVoid,

    // --- free store ---
    OP_Alloc,           // push the address of imm fresh bytes; b = 1 for new[]
    OP_Free,            // pop an address and release it; b = 1 for delete[]

    OP_Halt,

    // Appended here rather than beside OP_Alloc so that no existing opcode's
    // value moves: a .cxb written by an earlier build still means what it
    // meant, and the malformed images in tests/images stay the exact bytes
    // they were recorded as.
    OP_AllocN,          // pop a size (and, when b = 1, an element count first),
                        // push the address
    OP_ArrayCount,      // pop an address from new[], push its element count

    // Not an instruction: the count, so a byte read out of a file can be
    // checked against the set of opcodes that actually exist.
    OP_Count
};

const char *opCodeName(OpCode op);

struct Instr {
    OpCode op;
    vmword imm;
    vmword b;
    double fimm;
    int line;
    Instr(OpCode o = OP_Halt) : op(o), imm(0), b(0), fimm(0.0), line(0) {}
};

// One frame's shape.  Locals keep their declared widths; every virtual
// register is 8 bytes, because a register holds a long or a double and nothing
// smaller is worth the arithmetic.
struct FuncImage {
    std::string name;
    int paramCount;
    int frameSize;                  // bytes of locals
    int registerCount;
    std::vector<int> localOffset;   // slot -> byte offset within the frame
    std::vector<int> localSize;
    // Parallel to localSize: 1 when the slot holds a float or double, so the
    // VM writes an incoming argument with the right representation.
    std::vector<unsigned char> localFloat;
    // 1 when the slot is a by-value object: the argument is an address and the
    // VM copies localSize bytes from it.
    std::vector<unsigned char> localObject;
    std::vector<Instr> code;
    FuncImage() : paramCount(0), frameSize(0), registerCount(0) {}
};

// Everything the VM needs to run: the code, and the bytes that exist before it
// starts.
struct Image {
    std::vector<FuncImage> functions;
    std::vector<unsigned char> staticData;
    int entry;                      // index of main, or -1
    // Run after the entry function returns: global objects are destroyed
    // there, because there is no scope in the program that owns them.
    int fini;
    Image() : entry(-1), fini(-1) {}

    void disassemble() const;

    // --- the object file -------------------------------------------------
    // A compiled program has to outlive the process that made it, so the
    // image is written whole to a .cxb file: a magic word, a version, the
    // static data, then every function.  Little-endian and fixed-width
    // throughout, so a file written on one machine loads on another.
    bool write(const std::string &path, std::string &error) const;
    bool read(const std::string &path, std::string &error);

    static const unsigned long Magic   = 0x31425843UL;  // "CXB1"
    // v2 localFloat, v3 localObject, v4 fini, v5 the maths natives grew.
    // A native is called by its INDEX, so inserting one renumbers every native
    // after it: a v4 image would still load and would then call the wrong
    // function.  The bump makes it say so instead.
    static const unsigned long Version = 5;
};

// Functions the VM supplies.  A program gets them by DECLARING one without a
// body -- there is no header file to include, so the declaration is the
// binding.  Without these a compiled program could compute but never show
// anything, which would make running it pointless.
enum NativeId {
    NAT_PrintInt,
    NAT_PrintChar,
    NAT_PrintDouble,
    NAT_PrintString,
    NAT_PrintLine,
    // The error stream, so `cerr` is actually diagnosable output and survives
    // redirecting stdout.
    NAT_ErrInt, NAT_ErrChar, NAT_ErrDouble, NAT_ErrString, NAT_ErrLine,
    // Maths.  There is no <cmath> to include, so these are declared the same
    // way everything else is: `double sqrt(double);` with no body.
    NAT_Sqrt, NAT_Sin, NAT_Cos, NAT_Tan,
    NAT_Asin, NAT_Acos, NAT_Atan, NAT_Atan2,
    NAT_Sinh, NAT_Cosh, NAT_Tanh,
    NAT_Pow, NAT_Fabs, NAT_Floor, NAT_Ceil,
    // `%` needs integer operands and says so, which left no way at all to take
    // a remainder of two doubles.  fmod is that operation.
    NAT_Fmod,
    // Rounding to a whole number in the two directions floor and ceil cannot
    // express: toward zero, and to the nearest.
    NAT_Trunc, NAT_Round,
    NAT_Log, NAT_Log10, NAT_Exp,
    NAT_Abs,                    // the one that is integer in and integer out
    // Input.  A read that fails leaves the destination alone and turns
    // NAT_InputGood false, which is what `cin.good()` reports -- there are no
    // exceptions to throw and no stream state object to carry one.
    NAT_PrintPointer, NAT_ErrPointer,   // an address, which no other native takes
    NAT_ReadInt, NAT_ReadDouble, NAT_ReadChar,
    NAT_ReadString,             // one whitespace-delimited word
    NAT_ReadLine,               // the rest of the line
    NAT_InputGood,
    NAT_Count
};

// Returns NAT_Count when the name is not a native.
NativeId nativeByName(const std::string &name);
const char *nativeName(NativeId id);
int nativeArgCount(NativeId id);
// True when the native hands back a double rather than an integer.
bool nativeReturnsFloat(NativeId id);
// The most any native takes; the VM keeps that many argument slots.
const int NativeMaxArgs = 2;

#endif
