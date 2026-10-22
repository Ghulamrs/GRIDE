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
#define CXX1_VERSION      "1.25"
#define CXX1_SEAL_DATE    "21-10-2026"
#define CXX1_SEAL_FILE    "cxx1-1.25.dat"

// The line every run prints first; what it claims is docs/EXCLUSIONS.md's to measure (review of 2026-10-08).
#define CXX1_BANNER "©2026 G. R. Akhtar - C++03 with C++11 extensions"

#endif
