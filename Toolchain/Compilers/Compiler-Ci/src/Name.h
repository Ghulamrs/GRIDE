#pragma once

// **The program's name, once**: what it calls itself in messages and temporary
// files, and the prefix of every variable it reads (C90_AS, C90_LD, ...). The
// Makefile's PROGRAM and RIDE's tools/make-projects.py spell it the same.

#include <string>

namespace program {

constexpr const char *kName = "c90";

// The environment variable named for this program: env("AS") is C90_AS.
inline std::string env(const char *what) { return std::string("C90_") + what; }

}
