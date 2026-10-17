// Diagnostics.cpp
//
// C++98 only.

#include "Diagnostics.h"

#include <iostream>

Diagnostics::Diagnostics(const std::string &sourceName)
    : name(sourceName), errors(0), warnings(0), capped(false), warningsCapped(false),
      lineOffset(0) {}

void Diagnostics::report(const char *level, int line, int col, const std::string &msg) {
    std::cout.flush();          // keep diagnostics in step with any AST dump
    std::cerr << name;
    const int shown = line - lineOffset;
    if (shown > 0) std::cerr << ":" << shown << ":" << col;
    std::cerr << ": " << level << ": " << msg << std::endl;
}

void Diagnostics::error(int line, int col, const std::string &msg) {
    ++errors;
    if (errors > MaxReported) {
        if (!capped) {
            capped = true;
            std::cerr << name << ": error: too many errors; stopping here" << std::endl;
        }
        return;
    }
    report("error", line, col, msg);
}

// A warning has its own budget, and the error cap does not spend it.
//
// This used to read `if (capped || warnings > MaxReported)`, so the twenty-first
// ERROR silenced every warning for the rest of the compilation -- including the
// ones already found, and including the ones about the very code the errors were
// in. Two channels, one of which could switch the other off: a program with a
// syntax error early on had its narrowing warnings disappear, and nothing said
// they had.
//
// They are counted apart and capped apart, and the notice says which ran out.
void Diagnostics::warning(int line, int col, const std::string &msg) {
    ++warnings;
    if (warnings > MaxReported) {
        if (!warningsCapped) {
            warningsCapped = true;
            std::cerr << name << ": warning: too many warnings; stopping here" << std::endl;
        }
        return;
    }
    report("warning", line, col, msg);
}

void Diagnostics::printSummary() const {
    std::cout.flush();
    if (errors == 0 && warnings == 0) {
        std::cout << "No errors." << std::endl;
        return;
    }
    std::cerr << errors << " error(s), " << warnings << " warning(s)." << std::endl;
}
