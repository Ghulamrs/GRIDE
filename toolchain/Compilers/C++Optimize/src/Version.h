#ifndef CXX1_VERSION_H
#define CXX1_VERSION_H

// **The release, in one place.**
//
// 1.24 (2026-10-21) is 1.23 - mended against clang on macOS - with what 1.10 found against cl on
// Windows: masm and link started without cmd, CPP11_DECLINES empty or 0 as unset, `volatile` said as a
// warning at its line, deduction inside a template-id matching the argument as it is, the UCRT's
// headers (time_t of eight bytes, _time64, _findfirst, <sys/stat.h>, <windows.h>, <direct.h>), the
// string members 1.23 lacked, <algorithm>'s last functions inside std, and the headers copied
// beside cpp11.exe.
//
// 1.25 (2026-10-21): a braced list inside the braces that call a constructor initialises the
// parameter it reaches - `const CMatrix<3, 3> X = { {1.0, 2.0, 3.0}, ... }` makes each row the
// CVector<3> of CMatrix(CVector<3>, CVector<3>, CVector<3>), as cl and clang++ -std=c++11 do - and a
// braced call argument goes to the overloads whose parameter such a list can initialise.
//
// 1.26 (2026-10-21): the first five fixes of the 1.25 review - a temporary bound to `T &&`, `auto &&`
// or `const T &` is the call's own object and lives to the reference's scope's end, a temporary a
// reference holds a member of is destroyed there too, a static local whose initialiser throws is
// built again on the next call, the implicit move assignment exists and a written copy assignment
// suppresses the implicit move constructor, and a 256 MB stack with the parser's nesting bounded by
// name where 80 parentheses or a 40-deep template recursion used to overflow it.
//
// 1.27 (2026-10-21): the 1.25 review's two remaining items. Breadth - overload ranking by cv and by
// the nearer base, a user-defined conversion's second standard conversion, deduction keeping a
// reference's const and seeing through `T (&)[N]`, `const T &&`, ADL on an enumeration's operators,
// a nested class reading the outer class's later and private members, a local class's members by
// the written name, `decltype` of a comparison, of a string literal and of `c ? x : cx`, `PB::v`
// and `Base<T>::v` through `this`, `a[arr]`, `return {4, 5}`, `S{"s", 1}` for an aggregate holding a
// std::string, `int a[]{...}`, `const int &r{9}`, `throw` as an expression, `__func__`, a UTF-8 BOM,
// `(1, 3)` as a constant, `-1 > 0u` in `#if`, `_Pragma`, a function-like macro's name at the end of
// an expansion rescanned with the tokens after it, pointers to member functions in arrays and against
// null, `&Outer::K`, `const int Outer::K;` holding its value, a lambda reading the enclosing
// function's constants, a member of an rvalue being an xvalue, and a function template's signature
// read in its own namespace. Library - vector::at and array::at throw out_of_range on every target,
// vector moves where the standard moves and takes a move-only element type, std::swap moves, a map's
// element is pair<const K, V> - its insert takes the pair<K, V> make_pair gives, and a pair<K, V> is
// made from it - std::declval.
//
// 1.28 (2026-10-22): the 1.25 review's wrong-code set. A static local of an inline, in-class or
// template function is one object for the program - in a COMDAT under cl's name, `?n@?1??f@@YAHXZ@4HA`
// with its `?$TSS0@...` guard, and under Itanium's `_ZZ1fvE1n` with `_ZGV`, so a cpp11 unit and a cl
// unit share it; `int *` and `const int *` thrown in one unit write their `_CTA2PEAH` once; a member
// template called with explicit arguments, `s.m<int>(1)`, keeps its definition in the unit; and three
// ill-formed programs are refused: `static_cast` between pointers to unrelated objects, an `explicit
// operator bool` in copy-initialisation, and `--` on a bool.
#define CXX1_VERSION      "1.28"
#define CXX1_SEAL_DATE    "22-10-2026"
#define CXX1_SEAL_FILE    "cxx1-1.28.dat"

// The line every run prints first; what it claims is docs/EXCLUSIONS.md's to measure (review of 2026-10-08).
#define CXX1_BANNER "©2026 G. R. Akhtar - C++03 with C++11 extensions"

#endif
