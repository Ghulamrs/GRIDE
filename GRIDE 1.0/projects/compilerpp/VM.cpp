// VM.cpp
//
// C++98 only.

#include "VM.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>

namespace {
const int  HeaderSize   = 16;                  // [size][next] before each block

// -MIN and MIN / -1 have no answer in the range, and signed overflow is
// undefined in C++ -- on x86-64 the divide instruction faults and takes the
// whole process with it.  The VM defines them the way the hardware would if
// it were allowed to: wrap, like every other signed operation here.
vmword negate(vmword v) {
    return static_cast<vmword>(~static_cast<uvmword>(v) + 1);
}
}

VM::VM()
    : stepsExhausted(false), steps(0), stackBase(0), stackTop(0), heapBase(0), heapTop(0),
      freeList(0),
      img(0), inputGood(true) {}

void VM::trap(const std::string &msg) {
    if (error.empty()) error = msg;
}

void VM::push(vmword v)    { Value x; x.i = v; stack.push_back(x); }
void VM::pushD(double v) { Value x; x.d = v; stack.push_back(x); }

VM::Value VM::pop() {
    if (stack.empty()) {
        trap("operand stack underflow");
        Value z; z.i = 0; return z;
    }
    Value v = stack.back();
    stack.pop_back();
    return v;
}

// --- memory ---

vmword VM::readInt(vmword addr, int size, bool isSigned) {
    // A width the word cannot hold would shift by 64 or more, which is
    // undefined -- and `size` comes out of the file.
    if (size > 8) { trap("read of a width this machine has no word for"); return 0; }
    if (addr <= 0 || size < 0 || size > static_cast<vmword>(mem.size()) ||
        addr > static_cast<vmword>(mem.size()) - size) {
        std::ostringstream ss;
        ss << "read of " << size << " bytes at invalid address " << addr;
        trap(ss.str());
        return 0;
    }
    uvmword raw = 0;
    for (int i = size - 1; i >= 0; --i) {
        raw = (raw << 8) | mem[addr + i];
    }
    if (isSigned && size < 8) {
        const uvmword signBit = static_cast<uvmword>(1) << (size * 8 - 1);
        if (raw & signBit) raw |= ~((static_cast<uvmword>(1) << (size * 8)) - 1);
    }
    return static_cast<vmword>(raw);
}

void VM::writeInt(vmword addr, int size, vmword value) {
    if (size > 8) { trap("write of a width this machine has no word for"); return; }
    if (addr <= 0 || size < 0 || size > static_cast<vmword>(mem.size()) ||
        addr > static_cast<vmword>(mem.size()) - size) {
        std::ostringstream ss;
        ss << "write of " << size << " bytes at invalid address " << addr;
        trap(ss.str());
        return;
    }
    uvmword raw = static_cast<uvmword>(value);
    for (int i = 0; i < size; ++i) {
        mem[addr + i] = static_cast<unsigned char>((raw >> (i * 8)) & 0xFF);
    }
}

double VM::readFloat(vmword addr, int size) {
    // Only two widths exist.  Any other passed the bounds check on `size` and
    // then moved eight bytes regardless, reading past the end of memory for
    // every size below eight.
    if (size != 4 && size != 8) { trap("floating read of an unsupported width"); return 0.0; }
    if (addr <= 0 || size < 0 || size > static_cast<vmword>(mem.size()) ||
        addr > static_cast<vmword>(mem.size()) - size) {
        trap("floating read at an invalid address");
        return 0.0;
    }
    if (size == 4) {
        float f;
        std::memcpy(&f, &mem[addr], 4);
        return f;
    }
    double d;
    std::memcpy(&d, &mem[addr], 8);
    return d;
}

void VM::writeFloat(vmword addr, int size, double value) {
    if (size != 4 && size != 8) { trap("floating write of an unsupported width"); return; }
    if (addr <= 0 || size < 0 || size > static_cast<vmword>(mem.size()) ||
        addr > static_cast<vmword>(mem.size()) - size) {
        trap("floating write at an invalid address");
        return;
    }
    if (size == 4) {
        float f = static_cast<float>(value);
        std::memcpy(&mem[addr], &f, 4);
        return;
    }
    std::memcpy(&mem[addr], &value, 8);
}

// Every block on the free list needs a header of its own, so the list cannot
// be longer than the heap holds headers.  A walk that goes further is going
// round a cycle rather than along a list, and must stop rather than spin.
vmword VM::freeListLimit() const {
    return (heapTop - heapBase) / HeaderSize + 1;
}

// A block carries its size, so `delete` knows how much it is releasing, and a
// next pointer, so a released block can be reused.  First fit, because the
// simplest allocator that reuses memory is enough to run a loop.
// The block's `next` field is only a free-list link while the block is FREE.
// While it is allocated it is spare, so what made the block lives there: 0 for
// plain new, and the element count plus one for new[].  One word, no cookie in
// front of the payload, and every address the program sees is still the address
// the allocator returned.
//
// The block's SIZE cannot stand in for the count: a block is rounded up to a
// multiple of eight, so five four-byte elements come back as six.
vmword VM::allocate(vmword bytes, vmword arrayCount) {
    const vmword mark = arrayCount < 0 ? 0 : arrayCount + 1;
    if (bytes <= 0) bytes = 1;
    // Round up AFTER the size is known to fit, or the rounding itself wraps.
    if (bytes > static_cast<vmword>(mem.size())) { trap("out of heap memory"); return 0; }
    if (bytes % 8) bytes += 8 - (bytes % 8);

    vmword prev = 0;
    vmword walked = 0;
    const vmword limit = freeListLimit();
    for (vmword b = freeList; b != 0; ) {
        if (++walked > limit) { trap("heap free list is corrupt"); return 0; }
        const vmword size = readInt(b, 8, true);
        const vmword next = readInt(b + 8, 8, true);
        if (size >= bytes) {
            if (prev) writeInt(prev + 8, 8, next);
            else      freeList = next;
            writeInt(b + 8, 8, mark);
            return b + HeaderSize;
        }
        prev = b;
        b = next;
    }

    // As a difference: the sum overflows for a size near the top of the range
    // and wraps back under the limit.
    if (heapTop > static_cast<vmword>(mem.size()) - HeaderSize - bytes) {
        trap("out of heap memory");
        return 0;
    }
    const vmword block = heapTop;
    heapTop += HeaderSize + bytes;
    writeInt(block, 8, bytes);
    writeInt(block + 8, 8, mark);
    return block + HeaderSize;
}

// Walking from the bottom of the heap: every block says how long it is, so the
// starts are exactly the addresses this walk lands on.
bool VM::isBlockStart(vmword block) {
    vmword walk = heapBase;
    vmword steppedOver = 0;
    const vmword guard = freeListLimit();
    while (walk < heapTop) {
        if (++steppedOver > guard) { trap("heap is corrupt"); return false; }
        if (walk == block) return true;
        const vmword size = readInt(walk, 8, true);
        if (size <= 0) { trap("heap is corrupt"); return false; }
        walk += HeaderSize + size;
    }
    return false;
}

bool VM::isOnFreeList(vmword block) {
    vmword walked = 0;
    const vmword limit = freeListLimit();
    for (vmword b = freeList; b != 0; b = readInt(b + 8, 8, true)) {
        if (++walked > limit) { trap("heap free list is corrupt"); return false; }
        if (b == block) return true;
    }
    return false;
}

// The same question arrayCount asks, without the trap: a pointer that is not a
// heap block is a legitimate answer here, not an error.
// The array a `char buf[32]` names has decayed to a pointer by the time it
// reaches a native, and the type went with it.  The SLOT is still described,
// though, by the frame table of whichever function declared it -- so the
// machine looks the address up in the frames it has pushed and answers how
// much room is left from there to the end of that slot.
bool VM::frameCapacity(vmword addr, vmword &cap) {
    if (!img || addr <= 0) return false;
    for (std::size_t f = frames.size(); f-- > 0; ) {
        const Frame &fr = frames[f];
        if (fr.func < 0 || fr.func >= static_cast<int>(img->functions.size())) continue;
        const FuncImage &fi = img->functions[fr.func];
        for (std::size_t k = 0; k < fi.localSize.size(); ++k) {
            const vmword start = fr.base + fi.localOffset[k];
            const vmword end   = start + fi.localSize[k];
            if (addr >= start && addr < end) { cap = end - addr; return cap > 0; }
        }
    }
    return false;
}

bool VM::heapCapacity(vmword addr, vmword &cap) {
    if (addr <= 0) return false;
    const vmword block = addr - HeaderSize;
    if (block < heapBase || block >= heapTop) return false;
    if (!isBlockStart(block) || isOnFreeList(block)) return false;
    cap = readInt(block, 8, true);
    return cap > 0;
}

void VM::writeCString(vmword addr, const std::string &s, vmword cap) {
    if (cap <= 0) return;
    const vmword limit = static_cast<vmword>(mem.size());
    if (addr <= 0 || cap > limit || addr > limit - cap) {
        trap("input written to an invalid address");
        return;
    }
    vmword n = static_cast<vmword>(s.size());
    if (n > cap - 1) n = cap - 1;               // room for the terminator
    for (vmword i = 0; i < n; ++i) {
        mem[addr + i] = static_cast<unsigned char>(s[static_cast<std::size_t>(i)]);
    }
    mem[addr + n] = 0;
}

vmword VM::arrayCount(vmword addr) {
    if (addr == 0) return 0;                    // delete[] of null: nothing to do
    const vmword block = addr - HeaderSize;
    if (block < heapBase || block >= heapTop || !isBlockStart(block)) {
        trap("delete[] of a pointer that did not come from new[]");
        return 0;
    }
    // Asked BEFORE the mark is read: releasing a block overwrites the mark with
    // a free-list link, so a second delete[] would otherwise be reported as a
    // form mismatch rather than as the double delete it is.
    if (isOnFreeList(block)) {
        trap("delete[] of a pointer that was already deleted");
        return 0;
    }
    const vmword mark = readInt(block + 8, 8, true);
    if (mark <= 0) {
        trap("delete[] applied to a pointer from plain 'new'");
        return 0;
    }
    return mark - 1;
}

void VM::release(vmword addr, bool isArray) {
    if (addr == 0) return;                      // deleting null is harmless
    const vmword block = addr - HeaderSize;
    if (block < heapBase || block >= heapTop) {
        trap("delete of a pointer that did not come from new");
        return;
    }
    // Being INSIDE the heap is not the same as being a block.  `delete` of a
    // pointer to a field of an object landed here, and the bytes of that field
    // then became a header: a size and a next of the program's own choosing,
    // which the next allocation followed.  So the blocks are walked from the
    // bottom -- each one says how long it is -- and only a block START counts.
    // Linear in the number of blocks, which is what a first-fit allocator with
    // a single free list already costs.
    if (!isBlockStart(block)) {
        if (!failed()) {
            trap(isArray ? "delete[] of a pointer that did not come from new[]"
                         : "delete of a pointer that did not come from new");
        }
        return;
    }
    // Deleting a block that is already free would link it to ITSELF, and the
    // next allocation too big to satisfy from it would then follow `next`
    // round that loop forever -- inside allocate(), where the interpreter's
    // step limit does not reach.  A double delete is a fault in the program,
    // so it is reported as one instead of hanging the machine.
    //
    // Asked before the form is read, because releasing a block overwrites the
    // form with a free-list link: a second delete[] would otherwise be
    // reported as a mismatch rather than as the double delete it is.
    if (isOnFreeList(block)) {
        if (!failed()) {
            trap(isArray ? "delete[] of a pointer that was already deleted"
                         : "delete of a pointer that was already deleted");
        }
        return;
    }
    // The two forms are not interchangeable: delete[] runs a destructor for
    // every element and delete runs one.  The language leaves the mismatch
    // undefined because a real allocator has nowhere to record which was used.
    // This one does, so it is an error rather than a mystery.
    const vmword wasArray = readInt(block + 8, 8, true);
    if ((wasArray != 0) != isArray) {
        trap(isArray ? "delete[] applied to a pointer from plain 'new'"
                     : "delete applied to a pointer from 'new[]'; use delete[]");
        return;
    }
    writeInt(block + 8, 8, freeList);
    freeList = block;
}

// --- natives ---

void VM::callNative(NativeId id, int argc) {
    // Arguments come off the stack in reverse, so the last one lands last.
    // Every one is popped, whether the native reads it or not: leaving any
    // behind would desync the stack for everything after.
    Value a[NativeMaxArgs];
    for (int i = 0; i < NativeMaxArgs; ++i) a[i].i = 0;
    for (int k = argc - 1; k >= 0; --k) {
        const Value v = pop();
        if (k < NativeMaxArgs) a[k] = v;
    }

    switch (id) {
    case NAT_PrintInt:    std::cout << a[0].i; break;
    case NAT_PrintChar:   std::cout << static_cast<char>(a[0].i); break;
    case NAT_PrintDouble: std::cout << a[0].d; break;
    case NAT_PrintLine:   std::cout << std::endl; break;
    case NAT_PrintString: {
        vmword p = a[0].i;
        while (p > 0 && p < static_cast<vmword>(mem.size()) && mem[p]) {
            std::cout << static_cast<char>(mem[p]);
            ++p;
        }
        break;
    }

    // The same five, on the error stream.
    case NAT_ErrInt:    std::cerr << a[0].i; break;
    case NAT_ErrChar:   std::cerr << static_cast<char>(a[0].i); break;
    case NAT_ErrDouble: std::cerr << a[0].d; break;
    case NAT_ErrLine:   std::cerr << std::endl; break;
    case NAT_ErrString: {
        vmword p = a[0].i;
        while (p > 0 && p < static_cast<vmword>(mem.size()) && mem[p]) {
            std::cerr << static_cast<char>(mem[p]);
            ++p;
        }
        break;
    }

    // Maths.  The operand stack carries a double, which is exactly what the
    // C library wants, so these are one call each.
    case NAT_Sqrt:  pushD(std::sqrt(a[0].d));  return;
    case NAT_Sin:   pushD(std::sin(a[0].d));   return;
    case NAT_Cos:   pushD(std::cos(a[0].d));   return;
    case NAT_Tan:   pushD(std::tan(a[0].d));   return;
    case NAT_Asin:  pushD(std::asin(a[0].d));  return;
    case NAT_Acos:  pushD(std::acos(a[0].d));  return;
    case NAT_Atan:  pushD(std::atan(a[0].d));  return;
    case NAT_Atan2: pushD(std::atan2(a[0].d, a[1].d)); return;
    case NAT_Sinh:  pushD(std::sinh(a[0].d));  return;
    case NAT_Cosh:  pushD(std::cosh(a[0].d));  return;
    case NAT_Tanh:  pushD(std::tanh(a[0].d));  return;
    case NAT_Pow:   pushD(std::pow(a[0].d, a[1].d));   return;
    case NAT_Fabs:  pushD(std::fabs(a[0].d));  return;
    case NAT_Floor: pushD(std::floor(a[0].d)); return;
    case NAT_Ceil:  pushD(std::ceil(a[0].d));  return;
    case NAT_Fmod:  pushD(std::fmod(a[0].d, a[1].d));  return;
    // trunc and round are C99; this is C++98, so they are written in terms of
    // the two roundings C++98 does have.  Both go away from zero the way the
    // C99 versions do: trunc(-2.7) is -2, round(-2.5) is -3.
    case NAT_Trunc:
        pushD(a[0].d < 0.0 ? std::ceil(a[0].d) : std::floor(a[0].d));
        return;
    case NAT_Round:
        pushD(a[0].d < 0.0 ? std::ceil(a[0].d - 0.5) : std::floor(a[0].d + 0.5));
        return;
    case NAT_Log:   pushD(std::log(a[0].d));   return;
    case NAT_Log10: pushD(std::log10(a[0].d)); return;
    case NAT_Exp:   pushD(std::exp(a[0].d));   return;
    case NAT_Abs:   push(a[0].i < 0 ? negate(a[0].i) : a[0].i); return;

    // An address, printed the way C++ prints one: as a number in hex, which
    // is what makes two pointers comparable by eye.  It is this machine's
    // address, not the host's, and that is the useful one -- it is where the
    // object actually lives in the memory this program can see.
    case NAT_PrintPointer:
    case NAT_ErrPointer: {
        std::ostream &out = (id == NAT_PrintPointer) ? std::cout : std::cerr;
        if (a[0].i == 0) { out << "0"; break; }
        std::ostringstream ss;
        ss << "0x" << std::hex << a[0].i;
        out << ss.str();
        break;
    }

    // --- input ---
    // A failed read leaves the destination alone and turns inputGood false.
    // There are no exceptions here and no stream-state object to carry one, so
    // that flag is the whole of the mechanism and cin.good() reads it.
    case NAT_ReadInt: {
        long v = 0;
        inputGood = (std::cin >> v) ? true : false;
        push(inputGood ? static_cast<vmword>(v) : 0);
        return;
    }
    case NAT_ReadDouble: {
        double v = 0.0;
        inputGood = (std::cin >> v) ? true : false;
        pushD(inputGood ? v : 0.0);
        return;
    }
    case NAT_ReadChar: {
        char c = 0;
        inputGood = (std::cin >> c) ? true : false;   // leading space skipped, as >> does
        push(inputGood ? static_cast<vmword>(c) : 0);
        return;
    }
    case NAT_ReadString: {
        std::string w;
        inputGood = (std::cin >> w) ? true : false;
        vmword cap = a[1].i;
        if (cap <= 0) {
            // `cin >> s` gives no width.  If the buffer came from new[] the
            // machine knows how long it is; otherwise nothing does, and a read
            // into a buffer of unknown length is the overflow this VM refuses
            // everywhere else.
            if (!heapCapacity(a[0].i, cap) && !frameCapacity(a[0].i, cap)) {
                trap("reading a word into a buffer of unknown size; "
                     "use cin.getline(buffer, size)");
                push(0);
                return;
            }
        }
        if (inputGood) writeCString(a[0].i, w, cap);
        push(0);
        return;
    }
    case NAT_ReadLine: {
        std::string l;
        inputGood = std::getline(std::cin, l) ? true : false;
        vmword cap = a[1].i;
        if (cap <= 0 && !heapCapacity(a[0].i, cap) && !frameCapacity(a[0].i, cap)) {
            trap("reading a line into a buffer of unknown size; "
                 "give cin.getline a size");
            push(0);
            return;
        }
        if (inputGood) writeCString(a[0].i, l, cap);
        push(0);
        return;
    }
    case NAT_InputGood: push(inputGood ? 1 : 0); return;

    // Not a native: the caller range-checks the id, and this keeps the switch
    // exhaustive so the compiler goes on checking it too.
    case NAT_Count:
        trap("call to a native this machine does not have");
        break;
    }
    push(0);                                    // a print yields a value too
}

// --- the loop ---

vmword VM::run(const Image &image, bool &ok) {
    ok = false;
    error.clear();
    steps = 0;
    stepsExhausted = false;
    // And the machine itself, which a trap leaves standing.  The driver builds
    // a fresh VM per run and exits, so only an embedder reuses one -- and it
    // inherited the previous run's frames, whose `func` is an index into the
    // image they came from.  The dispatch loop reads image.functions[fr.func]
    // before it checks anything, so a stale index into a SMALLER image is a
    // read past the end of the table: ASan calls it a heap-buffer-overflow
    // 9104 bytes past a 480-byte allocation, and it is reachable from nothing
    // more exotic than running one program after another one trapped.
    frames.clear();
    stack.clear();

    if (image.entry < 0) { trap("no entry point"); return 0; }
    // A .cxb may have come from anywhere, so nothing in it is trusted: an
    // out-of-range entry, or a function with no register base, would index
    // straight past the tables it arrived with.
    if (image.entry >= static_cast<int>(image.functions.size())) {
        trap("entry point is not a function in this image");
        return 0;
    }
    for (std::size_t i = 0; i < image.functions.size(); ++i) {
        const FuncImage &fi = image.functions[i];
        if (fi.localOffset.empty()) {
            trap("function '" + fi.name + "' has no frame layout");
            return 0;
        }
        // The four per-local tables describe the same slots and are written
        // that way -- one entry each, plus a sentinel on localOffset saying
        // where the registers start.  They are READ back as four independent
        // length-prefixed arrays, so a file that disagrees with itself would
        // send the argument loop past the end of the short one and use
        // whatever it found there as a frame offset.  They have to agree
        // before any of them is indexed.
        if (fi.localOffset.size() != fi.localSize.size() + 1 ||
            fi.localFloat.size()  != fi.localSize.size() ||
            fi.localObject.size() != fi.localSize.size()) {
            trap("function '" + fi.name + "' has an inconsistent frame layout");
            return 0;
        }
        if (fi.frameSize < 0 || fi.paramCount < 0 || fi.registerCount < 0) {
            trap("function '" + fi.name + "' has a negative frame size");
            return 0;
        }
    }

    // The length is the file's to claim, and the memory is a fixed size: a
    // claim larger than the machine wrote past the end of the vector's buffer
    // and corrupted the host's heap, which is not a fault the program can be
    // blamed for.
    // A machine has to be big enough to be one.  The limits are the host's to
    // choose and a host can choose badly, so they are checked here rather than
    // trusted -- the heap starts after the call stack, and a call stack that
    // does not fit puts every allocation past the end of memory.
    if (limits.memory <= 0 || limits.callStack <= 0 || limits.maxSteps <= 0) {
        trap("this machine has no memory, no call stack, or no step budget");
        return 0;
    }
    if (limits.callStack >= limits.memory) {
        trap("this machine's call stack does not fit in its memory");
        return 0;
    }
    if (static_cast<vmword>(image.staticData.size()) + limits.callStack >= limits.memory) {
        trap("static data and the call stack do not fit in this machine's memory");
        return 0;
    }
    if (static_cast<vmword>(image.staticData.size()) > limits.memory) {
        trap("static data does not fit in this machine's memory");
        return 0;
    }
    img = &image;                   // the frame tables, for frameCapacity
    mem.assign(static_cast<std::size_t>(limits.memory), 0);
    std::copy(image.staticData.begin(), image.staticData.end(), mem.begin());

    stackBase = static_cast<vmword>(image.staticData.size());
    if (stackBase % 8) stackBase += 8 - (stackBase % 8);
    stackTop = stackBase;
    heapBase = stackBase + limits.callStack;
    heapTop = heapBase;
    freeList = 0;

    // The entry frame.
    Frame top;
    top.func = image.entry;
    top.pc = 0;
    top.base = stackTop;
    top.regBase = image.functions[image.entry].localOffset.back();
    top.wantsResult = true;
    // The same bound every other call gets.  The entry frame was pushed
    // without one, so a frame size the file made up slid the whole stack past
    // the heap before a single instruction ran.
    if (stackTop > heapBase - image.functions[image.entry].frameSize) {
        trap("the entry function's frame does not fit on the stack");
        return 0;
    }
    stackTop += image.functions[image.entry].frameSize;
    frames.push_back(top);

    vmword result = 0;
    bool finiDone = false;

    while (!frames.empty() && !failed()) {
        if (++steps > limits.maxSteps) {
            stepsExhausted = true;
            trap("execution did not terminate");
            break;
        }

        Frame &fr = frames.back();
        const FuncImage &fi = image.functions[fr.func];
        if (fr.pc < 0 || fr.pc >= static_cast<int>(fi.code.size())) {
            trap("program counter left the function");
            break;
        }
        const Instr &in = fi.code[fr.pc++];

        switch (in.op) {
        case OP_PushConst:  push(in.imm); break;
        case OP_PushFConst: pushD(in.fimm); break;
        case OP_Pop:        pop(); break;

        case OP_LoadReg:
            // The frame declares how many it has.  Without this the index was
            // multiplied by eight and added to the frame base unchecked, which
            // both overflows and reaches anywhere in memory.
            if (in.imm < 0 || in.imm >= fi.registerCount) {
                trap("register out of range");
                break;
            }
            push(readInt(fr.base + fr.regBase + in.imm * 8, 8, true));
            break;
        case OP_StoreReg:
            if (in.imm < 0 || in.imm >= fi.registerCount) {
                trap("register out of range");
                break;
            }
            writeInt(fr.base + fr.regBase + in.imm * 8, 8, pop().i);
            break;

        case OP_LocalAddr:
            if (in.imm < 0 || in.imm >= static_cast<vmword>(fi.localOffset.size()) - 1) {
                trap("local slot out of range");
                break;
            }
            push(fr.base + fi.localOffset[in.imm]);
            break;
        case OP_StaticAddr: push(in.imm); break;
        case OP_FuncAddr:   push(in.imm); break;
        case OP_FieldAddr:  push(pop().i + in.imm); break;

        case OP_Load: {
            const vmword addr = pop().i;
            if (in.b & 2) pushD(readFloat(addr, static_cast<int>(in.imm)));
            else          push(readInt(addr, static_cast<int>(in.imm), (in.b & 1) != 0));
            break;
        }
        case OP_Store: {
            const Value v = pop();
            const vmword addr = pop().i;
            if (in.b & 2) writeFloat(addr, static_cast<int>(in.imm), v.d);
            else          writeInt(addr, static_cast<int>(in.imm), v.i);
            break;
        }

        case OP_MemCopy: {
            const vmword src = pop().i;
            const vmword dst = pop().i;
            const vmword n   = in.imm;
            // As differences: `src + n` overflows for a count near the top of
            // the range and wraps back under the limit, which is how a copy of
            // nine million million bytes passed this check.  The by-value
            // argument copy below was written this way already; this one was
            // not.
            const vmword limit = static_cast<vmword>(mem.size());
            if (src <= 0 || dst <= 0 || n < 0 || n > limit ||
                src > limit - n || dst > limit - n) {
                trap("object copy at an invalid address");
                break;
            }
            if (src != dst) std::memmove(&mem[dst], &mem[src], static_cast<std::size_t>(n));
            break;
        }

        // A shift by a silly amount is undefined in C++, so the VM defines it:
        // out of range yields 0 rather than whatever the host would do.
        case OP_Shl: {
            vmword b = pop().i, a = pop().i;
            push((b < 0 || b > 63) ? 0 : static_cast<vmword>(
                     static_cast<uvmword>(a) << b));
            break;
        }
        case OP_Shr: {
            vmword b = pop().i, a = pop().i;
            if (b < 0 || b > 63) { push(a < 0 ? -1 : 0); break; }
            push(a >> b);                       // arithmetic: the sign is kept
            break;
        }
        case OP_UShr: {
            vmword b = pop().i, a = pop().i;
            push((b < 0 || b > 63) ? 0 : static_cast<vmword>(
                     static_cast<uvmword>(a) >> b));
            break;
        }

        case OP_Add: { vmword b = pop().i, a = pop().i; push(a + b); break; }
        case OP_Sub: { vmword b = pop().i, a = pop().i; push(a - b); break; }
        case OP_Mul: { vmword b = pop().i, a = pop().i; push(a * b); break; }
        case OP_Div: {
            vmword b = pop().i, a = pop().i;
            if (b == 0) { trap("division by zero"); break; }
            if (b == -1) { push(negate(a)); break; }
            push(a / b);
            break;
        }
        case OP_Mod: {
            vmword b = pop().i, a = pop().i;
            if (b == 0) { trap("remainder by zero"); break; }
            if (b == -1) { push(0); break; }
            push(a % b);
            break;
        }
        case OP_UDiv: {
            uvmword b = static_cast<uvmword>(pop().i);
            uvmword a = static_cast<uvmword>(pop().i);
            if (b == 0) { trap("division by zero"); break; }
            push(static_cast<vmword>(a / b));
            break;
        }
        case OP_UMod: {
            uvmword b = static_cast<uvmword>(pop().i);
            uvmword a = static_cast<uvmword>(pop().i);
            if (b == 0) { trap("remainder by zero"); break; }
            push(static_cast<vmword>(a % b));
            break;
        }
        case OP_Neg: push(negate(pop().i)); break;
        case OP_Not: push(pop().i == 0 ? 1 : 0); break;

        case OP_FAdd: { double b = pop().d, a = pop().d; pushD(a + b); break; }
        case OP_FSub: { double b = pop().d, a = pop().d; pushD(a - b); break; }
        case OP_FMul: { double b = pop().d, a = pop().d; pushD(a * b); break; }
        case OP_FDiv: { double b = pop().d, a = pop().d; pushD(a / b); break; }
        case OP_FNeg: pushD(-pop().d); break;

        case OP_CmpEQ: { vmword b = pop().i, a = pop().i; push(a == b); break; }
        case OP_CmpNE: { vmword b = pop().i, a = pop().i; push(a != b); break; }
        case OP_CmpLT: { vmword b = pop().i, a = pop().i; push(a <  b); break; }
        case OP_CmpGT: { vmword b = pop().i, a = pop().i; push(a >  b); break; }
        case OP_CmpLE: { vmword b = pop().i, a = pop().i; push(a <= b); break; }
        case OP_CmpGE: { vmword b = pop().i, a = pop().i; push(a >= b); break; }
        case OP_UCmpLT: { uvmword b = pop().i, a = pop().i; push(a <  b); break; }
        case OP_UCmpGT: { uvmword b = pop().i, a = pop().i; push(a >  b); break; }
        case OP_UCmpLE: { uvmword b = pop().i, a = pop().i; push(a <= b); break; }
        case OP_UCmpGE: { uvmword b = pop().i, a = pop().i; push(a >= b); break; }
        case OP_FCmpEQ: { double b = pop().d, a = pop().d; push(a == b); break; }
        case OP_FCmpNE: { double b = pop().d, a = pop().d; push(a != b); break; }
        case OP_FCmpLT: { double b = pop().d, a = pop().d; push(a <  b); break; }
        case OP_FCmpGT: { double b = pop().d, a = pop().d; push(a >  b); break; }
        case OP_FCmpLE: { double b = pop().d, a = pop().d; push(a <= b); break; }
        case OP_FCmpGE: { double b = pop().d, a = pop().d; push(a >= b); break; }

        case OP_IntToFloat: {
            const vmword v = pop().i;
            pushD(in.imm ? static_cast<double>(static_cast<uvmword>(v))
                         : static_cast<double>(v));
            break;
        }
        case OP_FloatToInt: push(static_cast<vmword>(pop().d)); break;
        case OP_FloatResize: {
            const double v = pop().d;
            pushD(in.imm == 4 ? static_cast<double>(static_cast<float>(v)) : v);
            break;
        }
        case OP_IntResize: {
            const vmword v = pop().i;
            const int size = static_cast<int>(in.imm);
            if (size <= 0) { trap("integer resize to an impossible width"); break; }
            if (size >= 8) { push(v); break; }
            uvmword masked = static_cast<uvmword>(v) & ((static_cast<uvmword>(1) << (size * 8)) - 1);
            if (in.b) {
                const uvmword signBit = static_cast<uvmword>(1) << (size * 8 - 1);
                if (masked & signBit) masked |= ~((static_cast<uvmword>(1) << (size * 8)) - 1);
            }
            push(static_cast<vmword>(masked));
            break;
        }

        case OP_Jump:       fr.pc = static_cast<int>(in.imm); break;
        case OP_BranchZero: if (pop().i == 0) fr.pc = static_cast<int>(in.imm); break;
        case OP_BranchNZ:   if (pop().i != 0) fr.pc = static_cast<int>(in.imm); break;

        case OP_VTableLoad: {
            const vmword obj = pop().i;
            const vmword vtable = readInt(obj, 8, true);
            push(readInt(vtable + in.imm * 8, 8, true));
            break;
        }

        case OP_Native:
            if (in.imm < 0 || in.imm >= NAT_Count) {
                trap("call to a native this machine does not have");
                break;
            }
            // The count says how many to pop.  Unvalidated it popped for as
            // long as it liked -- setting the trap on the first empty pop and
            // then carrying on for two thousand million more.
            if (in.b < 0 || static_cast<std::size_t>(in.b) > stack.size()) {
                trap("native call wants more arguments than the stack holds");
                break;
            }
            callNative(static_cast<NativeId>(in.imm), static_cast<int>(in.b));
            break;

        case OP_Call:
        case OP_CallIndirect: {
            const int argc = static_cast<int>(in.b);
            // This sizes a vector.  Negative became an enormous size_t and
            // threw length_error; large positive threw bad_alloc.  Nothing
            // catches either, so a single flipped byte in a .cxb aborted the
            // process.  The stack is the true bound: the arguments are on it.
            if (argc < 0 || static_cast<std::size_t>(argc) > stack.size()) {
                trap("call wants more arguments than the stack holds");
                break;
            }
            vmword target = in.imm;
            std::vector<Value> args(argc);
            for (int k = argc - 1; k >= 0; --k) args[k] = pop();
            if (in.op == OP_CallIndirect) target = pop().i;

            if (target < 0 || target >= static_cast<vmword>(image.functions.size())) {
                trap("call to an undefined function");
                break;
            }
            const FuncImage &callee = image.functions[target];
            if (stackTop + callee.frameSize > heapBase) {
                trap("call stack overflow (runaway recursion?)");
                break;
            }
            Frame nf;
            nf.func = static_cast<int>(target);
            nf.pc = 0;
            nf.base = stackTop;
            nf.regBase = callee.localOffset.back();
            nf.wantsResult = true;
            stackTop += callee.frameSize;
            // Arguments land in the first local slots, which is exactly where
            // the parameters were declared.
            for (int k = 0; k < argc && k < static_cast<int>(callee.localSize.size()); ++k) {
                // A float parameter is narrowed here; the operand stack always
                // carries a double, and the slot may be four bytes wide.
                const bool obj = k < static_cast<int>(callee.localObject.size())
                                 && callee.localObject[k] != 0;
                if (obj) {
                    // By value: the argument is the source address, and the
                    // parameter's own slot is the copy the callee owns.
                    const vmword src = args[k].i;
                    const vmword dst = nf.base + callee.localOffset[k];
                    const vmword n = callee.localSize[k];
                    // dst is bounded below as well as above: the offset comes
                    // out of the image, so it can be negative, and a negative
                    // dst passes an upper bound on its own and then writes in
                    // FRONT of memory.  The sums are written as differences
                    // for the same reason -- src + n overflows for an address
                    // near the top of the range and wraps past the check.
                    const vmword limit = static_cast<vmword>(mem.size());
                    if (src <= 0 || dst <= 0 || n < 0 || n > limit ||
                        src > limit - n || dst > limit - n) {
                        trap("object argument at an invalid address");
                        break;
                    }
                    std::memmove(&mem[dst], &mem[src], static_cast<std::size_t>(n));
                    continue;
                }
                const bool flt = k < static_cast<int>(callee.localFloat.size())
                                 && callee.localFloat[k] != 0;
                if (flt) writeFloat(nf.base + callee.localOffset[k], callee.localSize[k], args[k].d);
                else     writeInt(nf.base + callee.localOffset[k], callee.localSize[k], args[k].i);
            }
            frames.push_back(nf);
            break;
        }

        case OP_Return:
        case OP_ReturnVoid: {
            Value rv;
            rv.i = 0;
            if (in.op == OP_Return) rv = pop();
            stackTop = fr.base;
            frames.pop_back();
            if (frames.empty()) {
                result = rv.i;
                // main has returned; global objects are destroyed now, because
                // no scope in the program owns them.
                if (!finiDone && image.fini >= 0 &&
                    image.fini < static_cast<int>(image.functions.size())) {
                    finiDone = true;
                    const FuncImage &ff = image.functions[image.fini];
                    if (stackTop + ff.frameSize <= heapBase && !ff.localOffset.empty()) {
                        Frame nf;
                        nf.func = image.fini;
                        nf.pc = 0;
                        nf.base = stackTop;
                        nf.regBase = ff.localOffset.back();
                        nf.wantsResult = false;
                        stackTop += ff.frameSize;
                        frames.push_back(nf);
                        break;
                    }
                }
                ok = !failed();
                return result;
            }
            stack.push_back(rv);
            break;
        }

        case OP_Alloc:  push(allocate(in.imm, -1)); break;
        case OP_AllocN: {
            const vmword bytes = pop().i;
            vmword count = -1;
            if (in.b != 0) {
                count = pop().i;
                // new T[-1] is not a mistake the language catches for you.
                // Here the size is about to be a negative number of bytes, so
                // it is caught before it becomes one.
                if (count < 0) { trap("negative element count in 'new[]'"); break; }
            }
            push(allocate(bytes, count));
            break;
        }
        case OP_ArrayCount: push(arrayCount(pop().i)); break;
        case OP_Free:   release(pop().i, in.b != 0); break;
        case OP_Halt:  frames.clear(); break;

        // Bytecode.cpp casts a byte straight to OpCode, and 195 of the 256
        // values are not opcodes.  Without this they fell out of the switch as
        // silent no-ops, so a corrupt image RAN, quietly did nothing, and
        // reported success -- the one outcome an untrusted file should never
        // get.  OP_Count is not an instruction; it is here so the switch is
        // exhaustive and the compiler keeps it that way.
        case OP_Count:
        default: {
            std::ostringstream ss;
            ss << "unknown instruction " << static_cast<int>(in.op);
            trap(ss.str());
            break;
        }
        }
    }

    ok = !failed();
    return result;
}
