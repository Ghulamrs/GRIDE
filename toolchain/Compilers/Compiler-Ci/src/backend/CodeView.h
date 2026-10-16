#pragma once

// **CodeView, the debug information Microsoft's tools read**, written as assembly
// text for clang's COFF assembler on x86_64-windows (-masm=gnu -g). link.exe /DEBUG
// makes a PDB of it, and cdb, WinDbg and RIDE's Debug tab read that.

// It is Dwarf.cpp's counterpart and is fed the same DwarfFunction, DwarfGlobal,
// Local and Type information. Two sections: `.debug$S` holds the symbols and the
// line tables, `.debug$T` the types, numbered from 0x1000.

// .debug$S: S_OBJNAME and S_COMPILE3 (language C); per function S_GPROC32 or
// S_LPROC32, S_FRAMEPROC, S_REGREL32 off RBP per parameter then per local, S_LDATA32
// per static local, S_BLOCK32 per block that declares something, and S_END.

// After each function its `.cv_linetable`; after them all S_GDATA32 or S_LDATA32 per
// global and S_UDT per tag.

// .debug$T: LF_POINTER, LF_ARRAY, LF_STRUCTURE / LF_UNION over an LF_FIELDLIST of
// LF_MEMBER (LF_BITFIELD for a bit-field), LF_ENUM over LF_ENUMERATEs, LF_PROCEDURE
// over LF_ARGLIST. Basic types and pointers to them are built-in indices, with no record.

// The line entries are the spelling's: CoffSpelling writes `.cv_file`, `.cv_func_id`
// and `.cv_loc`. The design is cpp11's (Compiler-Cppi's CodeView.cpp, M10 W1 and W2);
// the two compilers share no code, so this is c90's own.

// Measured against cl /TC /Zi in cdb: `m10.py oracle tests/m10/w3.c LINE --compiler c90`.

#include "Dwarf.h"

#include <string>
#include <vector>

void writeCodeView(std::string &out, const Target &target, const std::string &objectName,
                   const std::vector<DwarfFunction> &fns,
                   const std::vector<DwarfGlobal> &globals,
                   const std::vector<EnumType> &enums);

// A source path as `.cv_file` wants it: absolute (cdb matches a breakpoint by it), backslashes doubled.
std::string codeViewPath(const std::string &compDir, const std::string &name);
