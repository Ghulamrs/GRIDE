// Diagnostics.h -- one place for every complaint the compiler makes.
//
// Reporting is separate from deciding what to do next: the parser reports and
// then recovers, the semantic pass reports and carries on, and main() asks at
// the end whether anything went wrong.  The file:line:col format is the one
// Xcode parses into clickable issues.
//
// C++98 only.

#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <string>

class Diagnostics {
public:
    explicit Diagnostics(const std::string &sourceName);

    void error(int line, int col, const std::string &msg);
    void warning(int line, int col, const std::string &msg);

    int errorCount() const { return errors; }
    int warningCount() const { return warnings; }
    bool hadError() const { return errors > 0; }

    // Printed once at the end of a run.
    void printSummary() const;

    // Past this many errors the rest are almost always cascade, and a wall of
    // them is worse than silence.  Counting continues; printing stops.
    static const int MaxReported = 20;

    // Lines of prelude prepended before the user's own first line.  Subtracted
    // from every report, so a program that includes <iostream> still sees its
    // own line numbers.
    void setLineOffset(int n) { lineOffset = n; }

private:
    std::string name;
    int errors;
    int warnings;
    bool capped;            // errors have stopped being reported
    bool warningsCapped;    // and warnings, separately -- one cannot end the other
    int lineOffset;

    void report(const char *level, int line, int col, const std::string &msg);

    // not copyable (C++98 way: declared private, never defined)
    Diagnostics(const Diagnostics &);
    Diagnostics &operator=(const Diagnostics &);
};

#endif
