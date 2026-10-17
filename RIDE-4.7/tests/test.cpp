// The two pieces of this editor with a contract worth pinning down: the layout
// rules, and the reading of cc1's one diagnostic. Neither needs a terminal, so
// neither is checked by typing into one and looking.

#include <cstdarg>
#include <cstdio>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>
#include <sstream>
#include <string>
#include <vector>

#include "buffer.h"
#include "path.h"
#include "process.h"
#include "settings.h"
#include "options.h"
#include "product.h"
#include "workspace.h"
#include "debugger.h"
#include "help.h"
#include "about.h"

// The seam the window is built on, tested from here because the window itself only runs on
// Windows, where cc1 emits MASM and there is no debugging to be had - so the one machine that can
// run the GUI is the one machine that cannot exercise what it calls.
#include "bridge.h"
#include "ccs/ccsoptions.h"
#include "ccs/ccsworkspace.h"
#include "ccs/ccsproject.h"
#include "compile.h"
#include "convert.h"
#include "indent.h"
#include "symbols.h"
#include "syntax.h"
#include "find.h"
#include "json.h"
#include "project.h"
#include "shalimar/session.h"
#include "toolchain.h"
#include "workspace.h"
#include "utf8.h"

#ifndef _WIN32
#include <sys/stat.h>
#endif

// What these tests used of <filesystem>, which is C++17 and so not here, over src/path.cpp - the same code the editor uses.
namespace file {

struct path {
    std::string text;

    path() {}
    path(const char* from) : text(editor::path::withSlashes(from)) {}
    path(const std::string& from) : text(editor::path::withSlashes(from)) {}

    std::string string() const { return text; }
    path operator/(const std::string& leaf) const {
        return path(editor::path::join(text, leaf));
    }
};

inline bool exists(const path& where) { return editor::path::exists(where.text); }
inline bool remove_all(const path& where) { return editor::path::removeTree(where.text); }
inline bool create_directories(const path& where) {
    return editor::path::makeDirectories(where.text);
}
inline path temp_directory_path() { return path(editor::path::tempDir()); }

}  // namespace file

namespace {

int failures = 0;
int checks = 0;
// --require-tools (D14): a case skipped for a tool or input of RIDE's own is a failure, said by name.
bool requireTools = false;
int skips = 0;

// A case not run, and why: "  (why)", and under --require-tools a FAIL - unless what is missing is a third party's.
void skippedWith(bool external, const char* format, va_list args) {
    char why[1024];
    std::vsnprintf(why, sizeof why, format, args);
    ++skips;
    if (requireTools && !external) {
        ++checks; ++failures;
        std::printf("  FAIL  skipped: %s\n", why);
    } else {
        std::printf("  (%s%s)\n", external ? "external: " : "", why);
    }
}
void skipped(const char* format, ...) { va_list a; va_start(a, format); skippedWith(false, format, a); va_end(a); }
void skippedExternal(const char* format, ...) { va_list a; va_start(a, format); skippedWith(true, format, a); va_end(a); }
// A case for another platform: nothing is missing here, so it is no failure under --require-tools either.
void skippedNotHere(const char* format, ...) {
    char why[1024]; va_list a; va_start(a, format); std::vsnprintf(why, sizeof why, format, a); va_end(a);
    std::printf("  (not on this platform: %s)\n", why);
}

const std::string kWindows = "x86_64-windows";
const std::string kLinux = "x86_64-linux";
const std::string kDarwin = "arm64-darwin";

std::string joined(const std::vector<std::string>& lines) {
    std::string all;
    for (size_t i = 0; i < lines.size(); ++i) all += lines[i] + "\n";
    return all;
}

// The build's question, as a test front end hears and answers it.
std::string askedQuestion;
bool askedAnswer = false;
bool rememberTheQuestion(void*, const std::string& question) { askedQuestion = question; return askedAnswer; }

void check(bool ok, const std::string& what) {
    ++checks;
    if (ok) return;
    ++failures;
    std::printf("  FAIL  %s\n", what.c_str());
}

void checkEqual(const std::string& got, const std::string& want, const std::string& what) {
    ++checks;
    if (got == want) return;
    ++failures;
    std::printf("  FAIL  %s\n        got  [%s]\n        want [%s]\n", what.c_str(),
                got.c_str(), want.c_str());
}

std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> out;
    std::string line;
    std::istringstream in(text);
    while (std::getline(in, line)) out.push_back(line);
    return out;
}

std::string join(const std::vector<std::string>& lines) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out += "\n";
        out += lines[i];
    }
    return out;
}

std::string laidOut(const std::string& text) {
    editor::IndentStyle style;
    return join(editor::reindent(split(text), style));
}

void diagnostics() {
    std::printf("cc1's diagnostic\n");

    editor::Diagnostic d = editor::parseDiagnostic(
        "hello.c:12:5: error: expected ';'\n    x = 1\n        ^\n");
    check(d.present, "a plain diagnostic is recognised");
    checkEqual(d.file, "hello.c", "file");
    check(d.line == 12 && d.col == 5, "line and column");
    checkEqual(d.message, "expected ';'", "message");

    // The one that decides how the parser is written: a Windows path begins
    // with a drive letter and a colon, which is not a separator.
    editor::Diagnostic w = editor::parseDiagnostic(
        "C:\\src\\hello.c:7:19: error: unknown type name 'foo'\n");
    check(w.present, "a Windows path is recognised");
    checkEqual(w.file, "C:\\src\\hello.c", "drive letter survives");
    check(w.line == 7 && w.col == 19, "line and column after a drive letter");

    editor::Diagnostic none = editor::parseDiagnostic("cc1: no input files\n");
    check(!none.present, "a message that is not a diagnostic is not one");

    editor::Diagnostic later = editor::parseDiagnostic(
        "cc1: reading foo.c\nfoo.c:3:1: error: stray '@'\n");
    check(later.present && later.line == 3, "a diagnostic after other output");

    // The other spelling. These are cl's own words, copied from a real run on
    // the Windows box rather than written from memory.
    editor::Diagnostic cl = editor::parseDiagnostic(
        "bad.c\nbad.c(3,13): error C2059: syntax error: ';'\n");
    check(cl.present, "cl's diagnostic is recognised");
    checkEqual(cl.file, "bad.c", "cl file");
    check(cl.line == 3 && cl.col == 13, "cl line and column");
    checkEqual(cl.message, "C2059: syntax error: ';'",
               "cl's message drops the word the caller puts back");

    // Without /diagnostics:column cl gives no column at all, and ml64 never
    // does. Column one is a better answer than refusing to read the line.
    editor::Diagnostic noCol = editor::parseDiagnostic("bad.c(3): error C2059: oops\n");
    check(noCol.present && noCol.line == 3 && noCol.col == 1,
          "a diagnostic with no column lands at column one");

    editor::Diagnostic clWin = editor::parseDiagnostic(
        "C:\\src\\bad.c(7,19): error C2065: 'foo': undeclared identifier\n");
    check(clWin.present, "cl with a full path");
    checkEqual(clWin.file, "C:\\src\\bad.c", "cl drive letter survives");
    check(clWin.line == 7 && clWin.col == 19, "cl line and column after a drive letter");

    editor::Diagnostic fatal = editor::parseDiagnostic(
        "bad.c(1): fatal error C1083: Cannot open include file: 'x.h'\n");
    check(fatal.present && fatal.line == 1, "a fatal error is an error");

    editor::Diagnostic warn = editor::parseDiagnostic(
        "bad.c(3,5): warning C4101: unreferenced local variable\n");
    check(!warn.present, "a warning is not what the caret is sent to");

    editor::Diagnostic link = editor::parseDiagnostic(
        "main.obj : error LNK2019: unresolved external symbol foo\n");
    check(!link.present, "a linker error names no line, and is left in the console");
}

void layout() {
    std::printf("layout\n");

    checkEqual(laidOut("int main(void)\n{\nreturn 0;\n}\n"),
               "int main(void)\n{\n    return 0;\n}",
               "a function body goes in one step");

    checkEqual(laidOut("void f(void) {\nif (x) {\ng();\n}\n}\n"),
               "void f(void) {\n    if (x) {\n        g();\n    }\n}",
               "a closing brace settles under the line that opened its group");

    checkEqual(laidOut("void f(void) {\nif (x)\ng();\nh();\n}\n"),
               "void f(void) {\n    if (x)\n        g();\n    h();\n}",
               "an if without braces indents one statement and no more");

    checkEqual(laidOut("void f(void) {\nif (a)\nif (b)\nx();\ny();\n}\n"),
               "void f(void) {\n    if (a)\n        if (b)\n            x();\n    y();\n}",
               "one semicolon closes every head that was waiting");

    checkEqual(laidOut("void f(void) {\nswitch (x) {\ncase 1:\na();\nbreak;\ndefault:\nb();\n}\n}\n"),
               "void f(void) {\n    switch (x) {\n    case 1:\n        a();\n        break;\n"
               "    default:\n        b();\n    }\n}",
               "K&R: a case label sits in its switch's own column");

    checkEqual(laidOut("void f(void) {\nx();\nagain:\ny();\n}\n"),
               "void f(void) {\n    x();\nagain:\n    y();\n}",
               "a goto label steps back out");

    checkEqual(laidOut("void f(void) {\nx = a ? b : c;\n}\n"),
               "void f(void) {\n    x = a ? b : c;\n}",
               "a conditional is not a label");

    checkEqual(laidOut("void f(void) {\n#define N 1\ng();\n}\n"),
               "void f(void) {\n#define N 1\n    g();\n}",
               "the preprocessor lives at the left margin");

    checkEqual(laidOut("void f(void) {\nputs(\"} not a brace {\");\ng();\n}\n"),
               "void f(void) {\n    puts(\"} not a brace {\");\n    g();\n}",
               "braces inside a string are text");

    checkEqual(laidOut("void f(void) {\nc = '{';\ng();\n}\n"),
               "void f(void) {\n    c = '{';\n    g();\n}",
               "a brace in a character constant is text");

    checkEqual(laidOut("void f(void) {\nputs(\"a \\\" and a } \");\ng();\n}\n"),
               "void f(void) {\n    puts(\"a \\\" and a } \");\n    g();\n}",
               "an escaped quote does not end the string");

    checkEqual(laidOut("void f(void) {\n/* a brace } here\n   and here { */\ng();\n}\n"),
               "void f(void) {\n    /* a brace } here\n   and here { */\n    g();\n}",
               "a block comment keeps its own layout and hides its braces");

    checkEqual(laidOut("void f(void) {\ng();  // }\nh();\n}\n"),
               "void f(void) {\n    g();  // }\n    h();\n}",
               "a brace after // is text");

    checkEqual(laidOut("void f(void) {\n\ng();\n}\n"),
               "void f(void) {\n\n    g();\n}",
               "a blank line stays blank");

    checkEqual(laidOut("void f(void) {\n} else {\ng();\n}\n"),
               "void f(void) {\n} else {\n    g();\n}",
               "a line that closes and opens holds its place");
}

void typing() {
    std::printf("what a typed newline becomes\n");

    editor::IndentStyle style;

    std::vector<std::string> lines = split("void f(void) {\n    g();");
    checkEqual(editor::indentAfterNewline(lines, 1, 8, style), "    ",
               "enter inside a body keeps the body's level");

    // Shalimar's hard-won rule, carried over: the brace waiting on the other
    // side of the caret belongs to the line that opened the group.
    std::vector<std::string> pair = split("void f(void) { return 1; }");
    checkEqual(editor::indentAfterNewline(pair, 0, 24, style), "",
               "a } after the caret pulls the new line out a step");

    std::vector<std::string> open = split("void f(void) {");
    checkEqual(editor::indentAfterNewline(open, 0, 14, style), "    ",
               "enter after an opening brace goes in a step");

    std::vector<std::string> head = split("void f(void) {\n    if (x)");
    checkEqual(editor::indentAfterNewline(head, 1, 10, style), "        ",
               "enter after an if with no brace goes in a step");

    std::vector<std::string> deep = split("void f(void) {\n    if (x) {\n        g();");
    checkEqual(editor::indentAfterNewline(deep, 2, 12, style), "        ",
               "enter two levels down stays two levels down");
}

// The colours as letters, so a test can say what it expects and be read.
std::string marks(const std::string& line, editor::Language lang,
                  editor::SyntaxState& state) {
    std::vector<unsigned char> kinds = editor::highlight(line, lang, state);
    std::string out;
    for (size_t i = 0; i < kinds.size(); ++i) {
        switch (kinds[i]) {
            case editor::KindKeyword: out += 'k'; break;
            case editor::KindType:    out += 't'; break;
            case editor::KindString:  out += 's'; break;
            case editor::KindChar:    out += 'q'; break;
            case editor::KindComment: out += 'c'; break;
            case editor::KindPreproc: out += 'p'; break;
            case editor::KindNumber:  out += 'n'; break;
            case editor::KindLabel:   out += 'l'; break;
            default:                  out += '.'; break;
        }
    }
    return out;
}

std::string marks(const std::string& line, editor::Language lang) {
    editor::SyntaxState state;
    return marks(line, lang, state);
}

void colours() {
    // The C++ lists, extended - and the two rules they follow: a word cl at
    // /std:c++14 would refuse is not coloured, and none of this leaks into C.
    {
        editor::SyntaxState state;
        std::string line = "thread_local int n = not_eq_count;";
        std::vector<unsigned char> cpp = editor::highlight(line, editor::LangCpp, state);
        check(cpp[0] == editor::KindKeyword, "thread_local is a C++ keyword");

        editor::SyntaxState plain;
        std::vector<unsigned char> asC = editor::highlight(line, editor::LangC, plain);
        check(asC[0] != editor::KindKeyword, "and is not one in C");

        editor::SyntaxState s2;
        std::vector<unsigned char> alt =
            editor::highlight("if (a and b) return not c;", editor::LangCpp, s2);
        check(alt[7] == editor::KindKeyword, "the alternative tokens are keywords too");

        editor::SyntaxState s3;
        std::vector<unsigned char> lib =
            editor::highlight("std::shared_ptr<thread> p;", editor::LangCpp, s3);
        check(lib[0] == editor::KindType, "a library name is coloured as the type it is");
        check(lib[5] == editor::KindType, "shared_ptr among them");
    }

    std::printf("colouring\n");

    check(editor::languageFor("main.c") == editor::LangC, ".c is C");
    check(editor::languageFor("main.h") == editor::LangC, ".h is C here, not C++");
    check(editor::languageFor("main.cpp") == editor::LangCpp, ".cpp is C++");
    check(editor::languageFor("out.S") == editor::LangAsm, ".S is assembly, whatever its case");
    check(editor::languageFor("README") == editor::LangPlain, "a file with no suffix is text");

    checkEqual(marks("return 0;", editor::LangC),
               "kkkkkk.n.",
               "a keyword and a number");

    checkEqual(marks("int x;", editor::LangC),
               "ttt...",
               "a type");

    checkEqual(marks("x = \"return\";", editor::LangC),
               "....ssssssss.",
               "a keyword inside a string is not a keyword");

    checkEqual(marks("a; // b", editor::LangC),
               "...cccc",
               "a line comment runs to the end");

    checkEqual(marks("#include <stdio.h>", editor::LangC),
               "pppppppp.sssssssss",
               "what an include brings in is a string, not two comparisons");

    checkEqual(marks("c = '\\'';", editor::LangC),
               "....qqqq.",
               "an escaped quote does not end a character constant");

    // The state has to survive the line, exactly as the indenter's does.
    {
        editor::SyntaxState state;
        checkEqual(marks("a; /* open", editor::LangC, state), "...ccccccc",
                   "a block comment starts");
        check(state.comment, "and is still open at the end of the line");
        checkEqual(marks("still inside", editor::LangC, state), "cccccccccccc",
                   "the next line is all comment");
        checkEqual(marks("*/ x;", editor::LangC, state), "cc...",
                   "and it closes");
        check(!state.comment, "the comment is closed after that");
    }

    check(marks("class Foo;", editor::LangCpp).compare(0, 5, "kkkkk") == 0,
          "class is a keyword in C++");
    check(marks("class Foo;", editor::LangC).compare(0, 5, ".....") == 0,
          "and is nothing in particular in C");

    checkEqual(marks("  .globl _factorial", editor::LangAsm),
               "..pppppp...........",
               "an assembler directive");
    checkEqual(marks("_factorial:", editor::LangAsm),
               "lllllllllll",
               "an assembler label");
    checkEqual(marks("  mov x29, sp", editor::LangAsm),
               "..kkk........",
               "the mnemonic is the first word");
}

void routing() {
    std::printf("which compiler gets the file\n");

    editor::Toolchain automatic;   // ToolAuto by default

    // C and C++ each have a decision in them, and the same one: the compiler
    // this editor is for, or the machine's own. Shalimar goes to the only
    // thing that reads it.
    check(editor::resolve(automatic, editor::LangC) == editor::ToolCc1,
          "C goes to cc1, which is what this editor is for");
    // Until 3.0 C++ went straight to the host's compiler - cl on Windows,
    // c++ elsewhere - because there was nothing else that read it. cxx1 is
    // that now, on every machine alike, and the host's is one Ctrl-K away.
    check(editor::resolve(automatic, editor::LangCpp) == editor::ToolCxx1,
          "C++ goes to cxx1, the same way C goes to cc1");
    check(editor::canCompile(editor::ToolCxx1, editor::LangCpp), "which can take it");
    check(!editor::canCompile(editor::ToolCxx1, editor::LangC),
          "and not C, which it would read as C++ and answer wrongly for");
    check(editor::usesArch(editor::ToolCxx1),
          "and takes a target, since it has the same three as cc1");
    check(!editor::runsHere(editor::ToolCxx1, "nowhere-else"),
          "so what it builds for another target stops at the assembly here too");
    checkEqual(editor::refusal(editor::ToolCxx1, editor::LangC),
               "cpp11 compiles C++, not C - Ctrl-K for automatic, and it picks c90",
               "and a C file handed to it is told where to go");
    checkEqual(editor::refusal(editor::ToolCc1, editor::LangCpp),
               "c90 compiles C, not C++ - Ctrl-K for automatic, and it picks cpp11",
               "as a C++ file handed to cc1 is");
    checkEqual(editor::toolchainShown(automatic, editor::ToolCxx1), "cpp11",
               "and it is cxx1 in the console whatever path it was found at");
#ifdef _WIN32
    check(editor::hostCppToolchain() == editor::ToolMsvc,
          "the host's C++ compiler is cl, on Windows");
#else
    // This used to say ToolMsvc on every machine, which meant a C++ file on a Mac was routed to a
    // compiler that is not installed there and never could be - so a project of C and C++ could
    // only ever have been built on Windows, however well the rest of it worked.
    check(editor::hostCppToolchain() == editor::ToolCxx,
          "the host's C++ compiler is c++ here, there being no cl to go to");
    check(editor::canCompile(editor::ToolCxx, editor::LangCpp), "which can take it");
    check(editor::canCompile(editor::ToolCxx, editor::LangC),
          "and can take C too, which is what makes naming it on a C group worth doing");
    check(editor::runsHere(editor::ToolCxx, editor::hostArch()),
          "and what it builds runs here, since it takes no target from this editor");

    // By name, not as "c++". Each of these machines has one C++ compiler and
    // which one is not a mystery on any of them, so the console says which
    // rather than saying the generic alias and leaving the reader to guess.
    editor::Toolchain thisMachine;
#ifdef __APPLE__
    checkEqual(thisMachine.cxx, "clang++",
               "a Mac's C++ compiler is clang++, and is called that");
#else
    checkEqual(thisMachine.cxx, "g++", "the Linux box's is g++, and is called that");
#endif
    checkEqual(editor::toolchainShown(thisMachine, editor::ToolCxx), thisMachine.cxx,
               "and that is the name a build writes in the console");
    checkEqual(editor::toolchainShown(thisMachine, editor::ToolCc1), "c90",
               "while cc1 is cc1 whatever path it was found at");
#endif
    check(editor::resolve(automatic, editor::LangPlain) == editor::ToolCc1,
          "anything else falls to cc1, and is refused there rather than here");

    // A choice made by hand is kept, even when it is the wrong one - the editor
    // says why rather than quietly doing something else.
    editor::Toolchain byHand;
    byHand.kind = editor::ToolCc1;
    check(editor::resolve(byHand, editor::LangCpp) == editor::ToolCc1,
          "a hand-picked compiler is not overridden");

    check(!editor::canCompile(editor::ToolCc1, editor::LangCpp),
          "cc1 cannot take C++");
    check(editor::canCompile(editor::ToolMsvc, editor::LangCpp), "cl can");
    check(editor::canCompile(editor::ToolMsvc, editor::LangC), "cl takes C as well");
    check(editor::canCompile(editor::ToolCc1, editor::LangC), "and so does cc1");
    check(!editor::canCompile(editor::ToolCc1, editor::LangAsm),
          "assembly is shown, not compiled");

    check(editor::usesArch(editor::ToolCc1), "cc1 generates for three architectures");
    check(!editor::usesArch(editor::ToolMsvc),
          "cl generates for its own host, so no target is offered");

    // The flags each compiler is given for each language.
    editor::Toolchain tool;
    std::string cpp = editor::shownCommand(tool, editor::ToolMsvc, "a.cpp",
                                           editor::LangCpp, "x86_64-windows",
                                           editor::ConfigDebug);
    check(cpp.find("/TP") != std::string::npos, "C++ is compiled as C++, and said so");
    check(cpp.find("/EHsc") != std::string::npos, "with exceptions turned on");

    std::string c = editor::shownCommand(tool, editor::ToolMsvc, "a.c",
                                         editor::LangC, "x86_64-windows",
                                         editor::ConfigDebug);
    check(c.find("/TC") != std::string::npos, "C is compiled as C");
    check(c.find("/TP") == std::string::npos, "and not as C++");

    // Debug and release, and an honest account of what each compiler can do
    // about them.
    check(editor::configFlags(editor::ToolMsvc, editor::ConfigDebug, kWindows).find("/Od") !=
              std::string::npos,
          "cl's debug turns the optimiser off");
    check(editor::configFlags(editor::ToolMsvc, editor::ConfigRelease, kWindows).find("/O2") !=
              std::string::npos,
          "and its release turns it up");
    check(editor::configFlags(editor::ToolMsvc, editor::ConfigRelease, kWindows).find("NDEBUG") !=
              std::string::npos,
          "with NDEBUG defined alongside");

    // cc1 has no -O, so release is the define and nothing else. Debug is the
    // define plus -g on the two targets cc1 writes DWARF for, and the define
    // alone on the one it does not - where passing -g would be refused.
    std::string linuxDebug = editor::configFlags(editor::ToolCc1, editor::ConfigDebug, kLinux);
    std::string darwinDebug = editor::configFlags(editor::ToolCc1, editor::ConfigDebug, kDarwin);
    std::string winDebug = editor::configFlags(editor::ToolCc1, editor::ConfigDebug, kWindows);
    std::string cc1Release = editor::configFlags(editor::ToolCc1, editor::ConfigRelease, kLinux);

    check(linuxDebug.find("_DEBUG") != std::string::npos, "cc1's debug defines _DEBUG");
    check(cc1Release.find("NDEBUG") != std::string::npos, "and its release defines NDEBUG");
    check(linuxDebug.find("-g") != std::string::npos, "debug asks x86_64-linux for -g");
    check(darwinDebug.find("-g") != std::string::npos, "and arm64-darwin for -g as well");
    check(winDebug.find("-g") != std::string::npos,
          "and x86_64-windows too, whose Debug build is CodeView through clang (M10)");
    check(winDebug.find("_DEBUG") != std::string::npos,
          "which still gets the define, since that is what assert reads");

    // A release build is never given -g on any target: debug information is
    // what debug means here, and release means its absence.
    check(editor::configFlags(editor::ToolCc1, editor::ConfigRelease, kLinux).find("-g") ==
              std::string::npos &&
          editor::configFlags(editor::ToolCc1, editor::ConfigRelease, kDarwin).find("-g") ==
              std::string::npos,
          "and release asks for -g nowhere");
    check(linuxDebug.find("-O") == std::string::npos &&
              darwinDebug.find("-O") == std::string::npos,
          "debug passes no -O");
    check(cc1Release.find("-O2") != std::string::npos,
          "and release passes c90 its own -O2");

    check(editor::emitsDebugInfo(editor::ToolCc1, kLinux) &&
              editor::emitsDebugInfo(editor::ToolCc1, kDarwin),
          "cc1 writes DWARF for two of its three targets");
    check(editor::emitsDebugInfo(editor::ToolCc1, kWindows), "and CodeView for the third (M10)");
    // cl is the other half of this machine's story, and not in the same
    // position: the C file goes to cc1 and carries no line table, while the
    // C++ file goes to cl, which writes CodeView into a .pdb and always could.
    check(editor::emitsDebugInfo(editor::ToolMsvc, kWindows),
          "cl writes debug information for the target it builds for");
    check(editor::configFlags(editor::ToolMsvc, editor::ConfigDebug, kWindows)
              .find("/Zi") != std::string::npos,
          "and a debug build asks it for that, not only for /Od");
    check(editor::configFlags(editor::ToolMsvc, editor::ConfigRelease, kWindows)
              .find("/Zi") == std::string::npos,
          "while a release build does not");

    // The words the panel says are the core's, and they follow the target
    // rather than being written out once and left.
    std::vector<std::string> carries = editor::debugNote(editor::ToolCc1, kDarwin);
    // Since 2026-10-13 every compiler RIDE drives carries a line table on every target - c90 and cpp11 on the
    // C6000 through cl6x's .dwpsn, shalimar through its rungs D1 to D5 - so no target is said to carry none.
    std::vector<std::string> shalimar = editor::debugNote(editor::ToolShc, kDarwin);
    check(!carries.empty() && !shalimar.empty(), "the panel is told something either way");
    check(joined(carries).find("DWARF") != std::string::npos &&
              joined(carries).find(kDarwin) != std::string::npos,
          "the target that carries DWARF is said to, by name");
    check(joined(shalimar).find("no debug information") == std::string::npos,
          "and shalimar is no longer said to carry none");

    // Compiling is one question and running is another. Every target compiles
    // to assembly anywhere; only the host's own goes on to a program, because
    // the assembler and linker cc1 hands off to are this machine's.
    std::string host = editor::hostArch();
    check(host == kWindows || host == kLinux || host == kDarwin,
          "the host is one of the three targets");
    check(editor::runsHere(editor::ToolCc1, host), "and what it builds for itself runs here");

    std::string elsewhere = (host == kLinux) ? kDarwin : kLinux;
    check(!editor::runsHere(editor::ToolCc1, elsewhere), "what it builds for elsewhere does not");
    check(editor::whyNotRun(editor::ToolCc1, host).empty(), "so there is nothing to explain");

    std::string why = editor::whyNotRun(editor::ToolCc1, elsewhere);
    check(why.find(elsewhere) != std::string::npos && why.find(host) != std::string::npos,
          "and when there is, it names both the target and the one to switch to");

    const std::string every[3] = {kWindows, kLinux, kDarwin};
    for (size_t i = 0; i < 3; ++i)
        check(editor::whyNotRun(editor::ToolCc1, every[i]).size() < 80,
              "in a line the status bar can show whole - " + every[i]);

    // cl builds for the machine it was installed on and is handed no target, so the target menu cannot make it unrunnable.
    check(editor::runsHere(editor::ToolMsvc, elsewhere), "cl builds for its own host either way");

    // The recipe that makes a program rather than assembly: cc1 with neither -S
    // nor -c links one, and cl does when it is not given /c.
    editor::Recipe program = editor::programRecipe(tool, editor::ToolCc1, "a.c",
                                                   editor::LangC, host, editor::ConfigDebug);
    check(program.command.find(" -S") == std::string::npos, "the program recipe passes no -S");
    check(program.command.find(" -c") == std::string::npos, "and no -c");
    check(program.command.find("-o") != std::string::npos, "and names what to make");
    check(program.command.find("a.c") != std::string::npos, "out of the file being edited");
    check(!program.assemblyPath.empty(), "and says where it put it");

    // Named for the editor that built it. It used to be one fixed name, so two
    // editors - or an editor and this suite - wrote to the same file.
    const std::string stem = "ride-run-";
    size_t named = program.assemblyPath.find(stem);
    check(named != std::string::npos, "and gives it a name of this editor's own");

    // The digit sits straight after the stem, and the offset is taken from the stem's own length
    // rather than written as a number. It was written as 8, the length of the name before this one,
    // and went on looking like a fact about process ids right up until the name changed.
    size_t digit = named + stem.size();
    check(digit < program.assemblyPath.size() &&
              program.assemblyPath[digit] >= '0' && program.assemblyPath[digit] <= '9',
          "with the number that tells one editor's from another's");
    check(editor::emitsDebugInfo(editor::ToolCc1, host) ==
              (program.command.find("-g") != std::string::npos),
          "and asks for -g exactly when the target can carry it");

    editor::Recipe clProgram = editor::programRecipe(tool, editor::ToolMsvc, "a.cpp",
                                                     editor::LangCpp, kWindows,
                                                     editor::ConfigDebug);
    check(clProgram.command.find(" /c ") == std::string::npos,
          "cl is not told to stop at an object");
    check(clProgram.command.find("/Fe") != std::string::npos, "and is told what to call the program");
    check(clProgram.command.find("/TP") != std::string::npos, "and that this one is C++");
    check(clProgram.command.find("/link /DEBUG") != std::string::npos,
          "and the linker is told as well, since /Zi only describes the object");
    check(clProgram.leftovers.size() == 3,
          "and the object, the .pdb and the .ilk are all cleared up after");

    // C++14, which is what this arena holds itself to - the editor tells cl so
    // rather than leaving it on whatever that compiler defaults to.
    check(clProgram.command.find("/std:c++14") != std::string::npos,
          "and that C++ here means C++14");

    check(editor::optimises(editor::ToolMsvc), "cl optimises");
    check(editor::optimises(editor::ToolCc1) && editor::optimises(editor::ToolCxx1),
          "and so do c90 and cpp11, with their own -O2");
    check(!editor::optimises(editor::ToolShc), "shalimar has no -O and does not pretend to");

    std::string release = editor::shownCommand(tool, editor::ToolCc1, "a.c",
                                               editor::LangC, kDarwin,
                                               editor::ConfigRelease);
    check(release.find("-DNDEBUG=1") != std::string::npos,
          "and the define reaches the command line");
    check(release.find("-O2") != std::string::npos, "and Release asks c90 for -O2");

    // The flag has to survive the whole way to what is actually run, not just
    // to what is shown.
    std::string shownDebug = editor::shownCommand(tool, editor::ToolCc1, "a.c",
                                                  editor::LangC, kDarwin,
                                                  editor::ConfigDebug);
    check(shownDebug.find("-g") != std::string::npos, "and -g reaches it too");
    check(editor::assemblyRecipe(tool, editor::ToolCc1, "a.c", editor::LangC,
                                 kDarwin, editor::ConfigDebug)
              .command.find("-g") != std::string::npos,
          "and reaches the command that is run, not only the one that is shown");

    // Header directories: the project's first, then the shipped ones the
    // compiler reads - lib/ for cc1, include/ for cxx1 - and none for shc.
    {
        editor::Toolchain with = tool;
        with.include = "/opt/ride/include";
        with.lib = "/opt/ride/lib";
        with.includes.push_back("/work/proj/include");
        with.libraries.push_back("/work/proj/lib/mathlib.a");

        std::string cFlags = editor::includeFlags(with, editor::ToolCc1);
        checkEqual(cFlags, " -I\"/work/proj/include\" -I\"/opt/ride/lib\"", "cc1 gets the project's, then lib/");
        std::string cppFlags = editor::includeFlags(with, editor::ToolCxx1);
        checkEqual(cppFlags, " -I\"/work/proj/include\" -I\"/opt/ride/include\"",
                   "cxx1 the project's, then include/");
        checkEqual(editor::includeFlags(with, editor::ToolMsvc), " /I\"/work/proj/include\"",
                   "cl the project's alone, spelled its way");
        checkEqual(editor::includeFlags(with, editor::ToolShc), "", "shc none - Shalimar has no include");

        check(editor::assemblyRecipe(with, editor::ToolCc1, "a.c", editor::LangC, kDarwin,
                                     editor::ConfigDebug).command.find(cFlags) != std::string::npos,
              "and they reach the compile that is run");
        check(editor::shownCommand(with, editor::ToolCc1, "a.c", editor::LangC, kDarwin,
                                   editor::ConfigDebug).find(cFlags) != std::string::npos,
              "and the one that is shown");
        std::vector<std::string> sources(1, "a.c");
        check(editor::targetRecipe(with, editor::ToolCc1, sources, editor::LangC, kDarwin,
                                   editor::ConfigDebug, "prog").command.find(cFlags) != std::string::npos,
              "and a project's build");
        std::vector<std::string> objects;
        check(editor::objectRecipe(with, editor::ToolCxx1, sources, editor::LangCpp, kDarwin,
                                   editor::ConfigDebug, "/tmp/o", objects).command.find(cppFlags) != std::string::npos,
              "and a part's objects");
        std::string link = editor::linkRecipe(with, objects, false, kDarwin, editor::ConfigDebug,
                                              "prog").command;
        check(link.find("\"/work/proj/lib/mathlib.a\"") != std::string::npos,
              "a library is linked after the objects");
        check(link.find("mathlib.a") > link.find(objects[0]), "after them, where a linker reads it");
        check(editor::assemblyRecipe(with, editor::ToolShc, "a.shl", editor::LangShalimar, kDarwin,
                                     editor::ConfigDebug).command.find("-I") == std::string::npos,
              "and shc is given no -I at all");
    }
}

void multiByte() {
    std::printf("characters that take more than one byte\n");

    // "café" - the e-acute is two bytes. "سلام" - four Arabic letters, two
    // bytes each. "中文" - two Chinese characters, three bytes each and two
    // columns each.
    const std::string cafe = "caf\xc3\xa9";
    const std::string salam = "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85";
    const std::string chinese = "\xe4\xb8\xad\xe6\x96\x87";

    check(editor::utf8::lengthFrom('a') == 1, "ASCII is one byte");
    check(editor::utf8::lengthFrom(0xC3) == 2, "a two-byte lead says two");
    check(editor::utf8::lengthFrom(0xE4) == 3, "a three-byte lead says three");
    check(editor::utf8::lengthFrom(0xF0) == 4, "a four-byte lead says four");

    check(cafe.size() == 5 && editor::utf8::count(cafe) == 4,
          "five bytes, four characters");
    check(salam.size() == 8 && editor::utf8::count(salam) == 4,
          "and eight bytes, four letters");

    // Moving over it lands on boundaries and nowhere else.
    check(editor::utf8::next(cafe, 3) == 5, "stepping over the accented letter");
    check(editor::utf8::previous(cafe, 5) == 3, "and back over it");
    check(editor::utf8::startOf(cafe, 4) == 3, "a caret inside one is pulled to its start");

    size_t at = 0, steps = 0;
    while (at < salam.size()) { at = editor::utf8::next(salam, at); ++steps; }
    check(steps == 4, "four steps cross four letters");

    // Columns are not bytes and not characters either.
    check(editor::utf8::columns(cafe, cafe.size()) == 4, "café takes four columns");
    check(editor::utf8::columns(salam, salam.size()) == 4, "and so does سلام");
    check(editor::utf8::columns(chinese, chinese.size()) == 4,
          "two Chinese characters take four");
    check(editor::utf8::widthOf(0x4E2D) == 2, "a Chinese character is two columns wide");
    check(editor::utf8::widthOf(0x0633) == 1, "an Arabic letter is one");
    check(editor::utf8::widthOf(0x064E) == 0, "and a mark drawn on top is none");

    // Rubbish must not wedge anything: every step has to move forward.
    std::string broken = "a\x80\x80z";
    size_t walked = 0, guard = 0;
    while (walked < broken.size() && guard < 100) {
        size_t step = editor::utf8::next(broken, walked);
        check(step > walked, "stepping through malformed bytes always moves on");
        walked = step;
        ++guard;
    }
}

void ranges() {
    std::printf("stretches of text\n");

    editor::Buffer buf;
    size_t endRow = 0, endCol = 0;
    buf.insertText(0, 0, "one\ntwo\nthree", endRow, endCol);
    check(buf.lineCount() == 3, "text with newlines in it becomes lines");
    check(endRow == 2 && endCol == 5, "and says where it ended");

    editor::Range within = editor::ordered(0, 1, 0, 3);
    checkEqual(buf.textIn(within), "ne", "a stretch inside one line");

    editor::Range across = editor::ordered(0, 1, 2, 3);
    checkEqual(buf.textIn(across), "ne\ntwo\nthr", "and one across three");

    // Given backwards, it is the same stretch.
    editor::Range backwards = editor::ordered(2, 3, 0, 1);
    checkEqual(buf.textIn(backwards), "ne\ntwo\nthr", "a selection made backwards");

    editor::Buffer cut = buf;
    cut.eraseRange(across);
    check(cut.lineCount() == 1, "erasing across lines joins what is left");
    checkEqual(cut.line(0), "oee", "of the first and the last");

    editor::Buffer inside = buf;
    inside.eraseRange(within);
    checkEqual(inside.line(0), "o", "and erasing within a line leaves the rest");
}

void undoing() {
    std::printf("going back, and forward again\n");

    editor::Buffer buf;
    size_t cx = 0, cy = 0;

    // A run of typing is one step, not one per letter.
    const char* word = "hello";
    for (size_t i = 0; word[i]; ++i) {
        buf.beginEdit(editor::EditTyping, cx, cy);
        buf.insertChar(0, cx, word[i]);
        ++cx;
    }
    checkEqual(buf.line(0), "hello", "five letters typed");
    check(buf.undoDepth() == 1, "are one step, not five");

    check(buf.undo(cx, cy), "and one undo");
    checkEqual(buf.line(0), "", "takes the word back");
    check(cx == 0 && cy == 0, "and the caret with it");

    check(buf.redo(cx, cy), "redo puts it back");
    checkEqual(buf.line(0), "hello", "text and all");
    check(cx == 5, "with the caret where it was");

    // Moving the caret ends the run, so what comes next is its own step.
    buf.breakRun();
    buf.beginEdit(editor::EditTyping, cx, cy);
    buf.insertChar(0, 5, '!');
    check(buf.undoDepth() == 2, "after moving, the next typing is a new step");
    check(buf.undo(cx, cy), "which undoes on its own");
    checkEqual(buf.line(0), "hello", "leaving what came before it");

    // A different kind of change is always its own step.
    buf.beginEdit(editor::EditOther, 5, 0);
    buf.splitLine(0, 5);
    check(buf.lineCount() == 2, "a line is split");
    check(buf.undo(cx, cy) && buf.lineCount() == 1, "and undoing joins it back");

    // Doing something new throws away what was undone.
    check(buf.canRedo(), "there is something to redo");
    buf.beginEdit(editor::EditOther, 0, 0);
    buf.insertChar(0, 0, 'x');
    check(!buf.canRedo(), "until something else is done");

    // Nothing to undo is not a failure, it is an answer.
    editor::Buffer fresh;
    size_t fx = 0, fy = 0;
    check(!fresh.undo(fx, fy), "an untouched buffer has nothing to undo");
    check(!fresh.redo(fx, fy), "and nothing to redo");

    // The history is capped, and going past the cap does not break it.
    editor::Buffer many;
    size_t mx = 0, my = 0;
    for (int i = 0; i < 150; ++i) {
        many.beginEdit(editor::EditOther, mx, my);
        many.insertChar(0, 0, 'a');
    }
    check(many.undoDepth() == 100, "the history stops at a hundred steps");
    check(many.undo(mx, my), "and still undoes");
    check(many.line(0).size() == 149, "one step at a time");
}

void savedState() {
    std::printf("knowing when the file matches the disk\n");

    file::path dir = file::temp_directory_path() / "ride-saved-test";
    file::remove_all(dir);
    file::create_directories(dir);

    editor::Buffer buf;
    buf.setPath((dir / "saved.c").string());
    size_t cx = 0, cy = 0;
    std::string error;

    buf.beginEdit(editor::EditTyping, cx, cy);
    buf.insertChar(0, 0, 'a');
    check(buf.dirty(), "typing makes it modified");

    check(buf.save(error), "it saves");
    check(!buf.dirty(), "and is not modified once written");

    check(buf.undo(cx, cy), "undoing past the save");
    check(buf.dirty(), "makes it modified again - the disk says otherwise");

    check(buf.redo(cx, cy), "and coming back");
    check(!buf.dirty(), "makes it match the disk once more");

    // A change after a save is its own step, so undoing it lands exactly on
    // what was written.
    buf.beginEdit(editor::EditTyping, cx, cy);
    buf.insertChar(0, 0, 'b');
    check(buf.dirty(), "a change after saving shows as modified");
    check(buf.undo(cx, cy) && !buf.dirty(),
          "and undoing it shows as saved, without writing anything");

    // Saving further along moves the mark with it.
    buf.beginEdit(editor::EditTyping, cx, cy);
    buf.insertChar(0, 0, 'c');
    check(buf.save(error), "saving again");
    check(!buf.dirty(), "clears it");
    check(buf.undo(cx, cy) && buf.dirty(), "and undo past the newer save is modified");

    // When the saved point falls off the end of the capped history it cannot
    // be recognised again, and the safe answer is 'modified'.
    editor::Buffer long_;
    long_.setPath((dir / "long.c").string());
    size_t lx = 0, ly = 0;
    check(long_.save(error), "an empty file is written");
    check(!long_.dirty(), "and is unmodified");
    for (int i = 0; i < 150; ++i) {
        long_.beginEdit(editor::EditOther, lx, ly);
        long_.insertChar(0, 0, 'z');
    }
    while (long_.canUndo()) long_.undo(lx, ly);
    check(long_.dirty(),
          "undoing to the bottom of a capped history still says modified");

    file::remove_all(dir);
}

void searching() {
    std::printf("finding and replacing\n");

    std::vector<std::string> lines = split(
        "int one(void) { return 1; }\n"
        "int two(void) { return 2; }\n"
        "int one_more(void) { return 3; }");

    editor::Match first = editor::findNext(lines, "one", 0, 0);
    check(first.found && first.row == 0 && first.col == 4, "the first one is found");

    editor::Match second = editor::findNext(lines, "one", first.row, first.col + 1);
    check(second.found && second.row == 2 && second.col == 4,
          "and the next is the one after it");

    // Round the end and back to where it started, once.
    editor::Match wrapped = editor::findNext(lines, "one", second.row, second.col + 1);
    check(wrapped.found && wrapped.row == 0, "searching wraps to the top");

    editor::Match missing = editor::findNext(lines, "nowhere", 0, 0);
    check(!missing.found, "what is not there is not found");
    check(!editor::findNext(lines, "", 0, 0).found, "and nothing is not searched for");

    editor::Match back = editor::findPrevious(lines, "one", 2, 4);
    check(back.found && back.row == 0, "and it goes backwards too");

    editor::Match onlyOne = editor::findNext(lines, "two", 1, 4);
    check(onlyOne.found && onlyOne.row == 1 && onlyOne.col == 4,
          "a match under the caret is found where it is");

    std::vector<std::string> changed = lines;
    check(editor::replaceAll(changed, "return", "give back") == 3, "every one is replaced");
    check(changed[0].find("give back 1") != std::string::npos, "and the text is right");

    // The one that could go round for ever if the replacement were searched
    // again.
    std::vector<std::string> growing = split("aaa");
    check(editor::replaceAll(growing, "a", "aa") == 3, "a replacement holding the needle");
    checkEqual(growing[0], "aaaaaa", "grows once and stops");

    std::vector<std::string> untouched = split("nothing here");
    check(editor::replaceAll(untouched, "absent", "x") == 0, "nothing found, nothing changed");
    checkEqual(untouched[0], "nothing here", "and the line is as it was");
}

void jsonReading() {
    std::printf("the project file's format\n");

    std::string why;
    editor::Json one = editor::Json::parse(
        "{\"name\": \"Editor\", \"indent\": {\"width\": 4, \"tabs\": false},"
        " \"groups\": [\"a\", \"b\"]}", why);
    check(why.empty(), "a plain object reads");
    checkEqual(one.get("name").text(), "Editor", "a string member");
    check(one.get("indent").get("width").integer() == 4, "a number inside an object");
    check(one.get("indent").get("tabs").boolean() == false, "a false");
    check(one.get("groups").size() == 2, "an array's length");
    checkEqual(one.get("groups").at(1).text(), "b", "an array's contents");

    check(one.get("missing").text("fallback") == "fallback",
          "what is not there gives the default back");

    // Comments are not JSON, and are allowed on purpose: this is a file people
    // open and edit, and people leave notes in files they edit.
    editor::Json noted = editor::Json::parse(
        "{\n  // which compiler\n  \"toolchain\": \"auto\"\n}", why);
    check(why.empty() && noted.get("toolchain").text() == "auto",
          "a comment is skipped rather than refused");

    std::string broken;
    editor::Json::parse("{\"a\": }", broken);
    check(!broken.empty(), "a malformed file says what went wrong");

    editor::Json::parse("{} and then some", broken);
    check(!broken.empty(), "text after the end is an error too");

    // What goes out must read back as what went in.
    editor::Json out = editor::Json::object();
    out.set("name", editor::Json::fromText("has \"quotes\" in it"));
    out.set("width", editor::Json::fromNumber(4));
    editor::Json back = editor::Json::parse(out.write(), why);
    check(why.empty(), "what it writes, it can read");
    checkEqual(back.get("name").text(), "has \"quotes\" in it", "quotes survive the trip");
    check(out.write().find("4") != std::string::npos &&
              out.write().find("4.0") == std::string::npos,
          "a whole number is written whole");
}

void writeSource(const std::string& where, const char* text) {
    std::ofstream out(where.c_str());
    out << text;
}

// What the editor remembers between sessions, and what it opens when there is
// nothing to remember. Both are about the machine you are on, so both are
// checked with home pointed somewhere disposable.
void sayWhereHomeIs(const std::string& where) {
#ifdef _WIN32
    _putenv_s("USERPROFILE", where.c_str());
#else
    setenv("HOME", where.c_str(), 1);
#endif
}

void projects() {
    std::printf("the project\n");

    file::path dir =
        file::temp_directory_path() / "ride-project-test";
    file::remove_all(dir);
    file::create_directories(dir);

    editor::Project made;
    made.begin(dir.string(), "Trial");
    check(made.loaded(), "a project begun is a project loaded");
    check(made.groups().size() == 1, "and starts with one group");

    check(made.addFile("src/main.c", "Sources"), "a file is added");
    check(!made.addFile("src/main.c", "Sources"), "and not added twice");
    check(made.addFile("src/util.c", "Sources"), "another one");
    check(made.addFile("docs/notes.txt", "Notes"), "a file into a group not yet there");
    check(made.groups().size() == 2, "which makes the group");

    check(made.groupOf("docs/notes.txt") < made.groups().size(), "and it is findable");
    check(made.moveToGroup("docs/notes.txt", "Sources"), "regrouping moves it");
    checkEqual(made.groups()[made.groupOf("docs/notes.txt")].name, "Sources",
               "to the group asked for");

    check(made.renameFile("src/util.c", "src/helper.c"), "renaming follows it");
    check(made.groupOf("src/util.c") == made.groups().size(), "the old name is gone");
    check(made.groupOf("src/helper.c") < made.groups().size(), "the new one is there");

    check(made.removeFile("src/helper.c"), "removing takes it out of the list");
    check(made.groupOf("src/helper.c") == made.groups().size(), "and it is not found after");

    // Two levels and no more. A structure nobody has to explore is one anyone
    // can read, so this is a rule the project keeps rather than a convention.
    std::string why;
    check(editor::Project::allows("main.c", why), "a file at the root");
    check(editor::Project::allows("src/main.c", why), "and one directory down");
    check(!editor::Project::allows("src/deep/main.c", why), "but no deeper");
    check(!why.empty(), "and it says why");
    check(!editor::Project::allows("/etc/passwd", why), "nothing absolute");
    check(!editor::Project::allows("../outside.c", why), "and no going up and out");
    check(!editor::Project::allows("src/", why), "a directory is not a file");

    check(!made.addFile("a/b/c.c", "Sources"), "a file too deep is not added");

    // Depth is limited; width is not. As many directories as a project wants
    // may sit side by side on the ground floor.
    {
        editor::Project wide;
        wide.begin(dir.string(), "Wide");
        const char* dirs[7] = {"src", "tests", "examples", "docs", "tools",
                               "extra", "more"};
        for (int i = 0; i < 7; ++i)
            check(wide.addFile(std::string(dirs[i]) + "/a.c", "Sources"),
                  std::string("directory ") + dirs[i] + " sits alongside the rest");
        check(wide.directories().size() == 7, "seven of them, and none refused");
        check(wide.addFile("root.c", "Sources"), "a file on the ground floor too");
        check(!wide.addFile("src/deeper/a.c", "Sources"), "but still nothing two deep");
    }

    check(made.addFile("src/ok.c", "Sources"), "one at the right depth is");
    check(!made.renameFile("src/ok.c", "a/b/c.c"), "nor renamed into somewhere too deep");
    check(made.removeFile("src/ok.c"), "tidy that away again");

    editor::IndentStyle style;
    style.width = 2;
    style.tabs = true;
    made.setIndent(style);
    made.setToolchain(editor::ToolMsvc);

    std::string error;
    check(made.save(error), "it writes itself out");
    check(error.empty(), "with nothing to report");

    // And the file that comes back is the project that went in.
    editor::Project read;
    check(read.load(dir.string(), error), "and reads back");
    checkEqual(read.name(), "Trial", "the name survives");
    check(read.indent().width == 2 && read.indent().tabs, "the layout settings survive");
    check(read.groups()[0].name == "Sources", "and the groups keep their order");
    check(read.toolchain() == editor::ToolMsvc, "the compiler choice survives");

    // Debug or release is deliberately *not* in here since 2026-08-23. It is what you are building
    // today rather than what the program is, and a project file travels - it should not arrive
    // telling whoever opens it which configuration to be in. It lives in the machine's own settings.
    std::string onDisk;
    {
        std::ifstream f(read.file().c_str());
        std::ostringstream all;
        all << f.rdbuf();
        onDisk = all.str();
    }
    check(onDisk.find("\"config\"") == std::string::npos,
          "and the configuration is not written into the project at all");
    check(read.groups().size() == 2, "the groups survive");
    check(read.groupOf("src/main.c") < read.groups().size(), "and what is in them");

    // A project made here builds what it holds, without anybody writing a
    // "build" entry by hand: the one group, into a program of its own name.
    check(read.builds(), "a new project says what it builds");
    checkEqual(read.target().name, "Trial", "the program is named after it");
    check(read.target().groups.size() == 1 && read.target().groups[0] == "Sources",
          "from its one group");

    // Header directories and libraries: written as the project has them,
    // relative to its root, and read back the same.
    {
        editor::Project paths;
        paths.begin(dir.string(), "Paths");
        std::vector<std::string> dirs;
        dirs.push_back("include");
        dirs.push_back("../common");
        std::vector<std::string> libs;
        libs.push_back("lib/mathlib.a");
        paths.setIncludes(dirs);
        paths.setLibraries(libs);
        check(paths.save(error), "a project with paths writes itself out");

        editor::Project again;
        check(again.load(paths.file(), error), "and reads back");
        check(again.includes().size() == 2 && again.includes()[1] == "../common",
              "with its include paths as written");
        check(again.libraries().size() == 1 && again.libraries()[0] == "lib/mathlib.a",
              "and its libraries");
        std::vector<std::string> full = again.absoluteIncludes();
        check(full.size() == 2 && full[0] == editor::path::absolute((dir / "include").string()),
              "each resolved against the root");
        check(full[1].find("common") != std::string::npos && full[1].find("..") == std::string::npos,
              "and a parent reference folded away");
        std::remove(paths.file().c_str());
    }
    check(read.includes().empty() && read.libraries().empty(),
          "a project that names none has none");

    // A new file named without an extension gets the one its project
    // mostly uses, else .c: the extension is what picks the compiler.
    {
        editor::Project ext;
        ext.begin((dir / "ext").string(), "Ext");
        checkEqual(editor::withExtension(ext, "hello"), "hello.c", "with no sources yet, C");
        ext.addFile("a.cpp", "Sources");
        ext.addFile("b.cpp", "Sources");
        ext.addFile("c.c", "Sources");
        checkEqual(editor::withExtension(ext, "hello"), "hello.cpp", "among C++ sources, C++");
        checkEqual(editor::withExtension(ext, "hello.c"), "hello.c", "and a name with one keeps it");
        checkEqual(editor::withExtension(ext, "hello", editor::ToolShc), "hello.shl",
                   "the chosen compiler's extension wins - shc's");
        checkEqual(editor::withExtension(ext, "hello", editor::ToolCc1), "hello.c", "cc1's");
        checkEqual(editor::withExtension(ext, "hello", editor::ToolMsvc), "hello.cpp", "cl's");
        editor::Outcome added = editor::createFile(ext, "sub/main", "Sources");
        check(added.ok && added.path.find("sub/main.cpp") != std::string::npos, "New File adds it");
        check(added.message.find("extension") != std::string::npos, "and says so");
        std::remove(added.path.c_str());
        std::remove(ext.file().c_str());
    }

    // Which file opens with the project: the one it names, else the one
    // defining main, else the first. Headers and declarations do not count.
    {
        file::path where = dir / "opens";
        file::create_directories(where);
        writeSource((where / "util.c").string(),
                    "/* once this held\nint main(void) { return 2; }\n*/\n// int main(void) {}\nint helper(void) { return 1; }\n");
        writeSource((where / "util.h").string(), "int main(void);\n");
        writeSource((where / "prog.c").string(), "/* main lives here */\nint main(void) { return 0; }\n");
        writeSource((where / "run.shl").string(), "fun <> = main()\n{\n    ? \"hi\"\n}\n");
        editor::Project opens;
        opens.begin(where.string(), "Opens");
        opens.addFile("util.h", "Headers");
        opens.addFile("util.c", "Sources");
        opens.addFile("prog.c", "Sources");
        checkEqual(opens.mainFile(), "prog.c",
                   "the file defining main is found, past a declaration and two commented ones");
        checkEqual(opens.fileToOpen(), "prog.c", "and is the one to open");
        opens.setOpenFile("util.c");
        checkEqual(opens.fileToOpen(), "util.c", "unless the project names another");
        check(opens.save(error), "which is written");
        editor::Project back;
        check(back.load(opens.file(), error) && back.openFile() == "util.c", "and read back");
        std::remove(opens.file().c_str());

        editor::Project shalimar;
        shalimar.begin(where.string(), "Shl");
        shalimar.addFile("run.shl", "Shalimar");
        checkEqual(shalimar.mainFile(), "run.shl", "a Shalimar main is found too");

        editor::Outcome kept = editor::rememberOpen(opens, (where / "prog.c").string());
        check(kept.ok && opens.openFile() == "prog.c", "closing on a file of the project records it");
        check(!editor::rememberOpen(opens, (where / "prog.c").string()).ok, "and not again when it is the same");
        check(!editor::rememberOpen(opens, (where / "elsewhere.c").string()).ok, "nor for a file the project lacks");
        std::remove(opens.file().c_str());
    }

    // The installation's settings.json: include/ and lib/ above the binary,
    // written on first sight, read for every compile, and each compiler
    // given its own - through the window's bridge as through the editor.
    {
        file::path app = dir / "app";
        file::create_directories(app / "bin");
        file::create_directories(app / "include");
        file::create_directories(app / "lib");
        editor::settings::pretendInstalledAt(app.string());
        check(editor::settings::installFile() == editor::path::join(app.string(), "settings.json"),
              "the settings file sits above bin/");
        check(editor::settings::writeInstallFileIfAbsent(), "and is written where there is none");
        check(file::exists(app / "settings.json"), "as a file");
        checkEqual(editor::settings::includeDir(), editor::path::absolute((app / "include").string()),
                   "include/ is where cxx1's headers are");
        checkEqual(editor::settings::libDir(), editor::path::absolute((app / "lib").string()),
                   "lib/ where cc1's are");
        check(editor::settings::vcvars().empty(), "and no vcvars until one is named");
        checkEqual(editor::settings::defaultCompiler(), "auto", "the compiler starts on automatic");
        check(editor::settings::indentWidth() == 4 && !editor::settings::indentTabs(),
              "and the indentation at four spaces");
        check(editor::settings::rememberIndent(2, true) && editor::settings::indentWidth() == 2 &&
              editor::settings::indentTabs(), "which can be changed there");
        check(editor::settings::rememberCodeFont("Consolas 12") && editor::settings::codeFont() == "Consolas 12",
              "and the window's font is kept there too");
        editor::settings::rememberIndent(4, false);
        std::vector<std::string> shared;
        shared.push_back("common/include");
        shared.push_back("/abs/elsewhere");
        check(editor::settings::rememberIncludes(shared) && editor::settings::includes().size() == 2 &&
              editor::settings::includes()[0] == editor::path::absolute((app / "common" / "include").string()),
              "installation-wide include paths, relative to the file");
        std::string shownShared = ride_shown_command(0, "c90", "cl", "shalimar", "cpp11", editor::ToolCc1,
                                                        "a.c", editor::LangC, kDarwin.c_str(), editor::ConfigDebug);
        check(shownShared.find("common") != std::string::npos, "reach every compile");
        editor::settings::rememberIncludes(std::vector<std::string>());
        check(editor::settings::rememberDefaultCompiler("cpp11") && editor::settings::defaultCompiler() == "cpp11",
              "and a choice from the menu is written");
        check(ride_default_compiler() == editor::ToolCxx1, "which the window reads back as its kind");

        std::string shownC = ride_shown_command(0, "c90", "cl", "shalimar", "cpp11", editor::ToolCc1,
                                                   "a.c", editor::LangC, kDarwin.c_str(), editor::ConfigDebug);
        check(shownC.find("-I\"" + editor::settings::libDir() + "\"") != std::string::npos,
              "the window's cc1 command carries lib/");
        check(shownC.find("include") == std::string::npos, "and not include/");
        std::string shownCpp = ride_shown_command(0, "c90", "cl", "shalimar", "cpp11", editor::ToolCxx1,
                                                     "a.cpp", editor::LangCpp, kDarwin.c_str(), editor::ConfigDebug);
        check(shownCpp.find("-I\"" + editor::settings::includeDir() + "\"") != std::string::npos,
              "its cxx1 command carries include/");

        // Named elsewhere, absolute: that is what is used.
        file::create_directories(app / "other");
        check(editor::settings::rememberHeaderDirs((app / "other").string(), "lib"), "the directories can be changed");
        checkEqual(editor::settings::includeDir(), editor::path::absolute((app / "other").string()),
                   "an absolute one is taken as written");
        checkEqual(editor::settings::libDir(), editor::path::absolute((app / "lib").string()),
                   "a relative one against the file");
        check(editor::settings::rememberVcvars("no-such-file.bat") && editor::settings::vcvars().empty(),
              "a vcvars that is not there counts for nothing");
        // The two linkers - LINK's for x86_64-windows, LNK6X's for tms6747 -
        // are named the way the assembler is: a path that exists, or nothing.
        check(editor::settings::linker().empty() && editor::settings::tilinker().empty(),
              "no linker of the project's own until one is named");
        check(editor::settings::rememberLinker("no-such-link.exe") && editor::settings::linker().empty(),
              "a linker that is not there counts for nothing");
        std::string self = editor::path::absolute((app / "settings.json").string());
        check(editor::settings::rememberLinker(self) && editor::settings::linker() == self,
              "one that is there is the linker for x86_64-windows");
        check(editor::settings::rememberTilinker(self) && editor::settings::tilinker() == self,
              "and, named apart, the linker for tms6747");
        // The installer writes the assembler as "bin/masm.exe": relative to
        // the settings file, as include/ and lib/ are, so the installation
        // can be put anywhere. What is written is kept as written.
        std::ofstream((app / "bin" / "masm.exe").string().c_str()) << "not an assembler, but a file that is there\n";
        check(editor::settings::rememberAssembler("bin/masm.exe"), "the assembler can be named relative to the file");
        checkEqual(editor::settings::assembler(),
                   editor::path::absolute((app / "bin" / "masm.exe").string()),
                   "and is found beside the editor, made absolute against it");
        // With the assembler named, every Release recipe that assembles x86_64-windows
        // C++ has to tell cpp11 to write MASM's spelling - F5's Run file was
        // the one that did not, and masm.exe got clang's command line.
        {
            editor::Toolchain tool;
            tool.cxx1 = "cpp11.exe";
            std::vector<std::string> srcs(1, "a.cpp"), objs;
            std::vector<std::string> lines;
            lines.push_back(editor::programRecipe(tool, editor::ToolCxx1, "a.cpp", editor::LangCpp,
                                                  "x86_64-windows", editor::ConfigRelease).command);
            lines.push_back(editor::shownProgramCommand(tool, editor::ToolCxx1, "a.cpp", editor::LangCpp,
                                                        "x86_64-windows", editor::ConfigRelease));
            lines.push_back(editor::targetRecipe(tool, editor::ToolCxx1, srcs, editor::LangCpp,
                                                 "x86_64-windows", editor::ConfigRelease, "a.exe").command);
            lines.push_back(editor::objectRecipe(tool, editor::ToolCxx1, srcs, editor::LangCpp,
                                                 "x86_64-windows", editor::ConfigRelease, ".", objs).command);
            bool all = true;
            for (size_t i = 0; i < lines.size(); ++i)
                if (lines[i].find(" -masm=masm") == std::string::npos) all = false;
            check(all, "Run file, its shown line, F4 and a part's objects all pass -masm=masm to cpp11");
            std::string c = editor::programRecipe(tool, editor::ToolCc1, "a.c", editor::LangC,
                                                  "x86_64-windows", editor::ConfigRelease).command;
            check(c.find("-masm") == std::string::npos, "c90, which reads C90_AS alone, gets no flag");
            // M10: a Debug build carries CodeView, which only clang assembles, whatever masm is named.
            std::string debug = editor::programRecipe(tool, editor::ToolCxx1, "a.cpp", editor::LangCpp,
                                                      "x86_64-windows", editor::ConfigDebug).command;
            check(debug.find(" -masm=gnu") != std::string::npos && debug.find("-masm=masm") == std::string::npos,
                  "a Debug cpp11 build for x86_64-windows is the GNU spelling, for CodeView");
            std::string debugC = editor::programRecipe(tool, editor::ToolCc1, "a.c", editor::LangC,
                                                       "x86_64-windows", editor::ConfigDebug).command;
            check(debugC.find(" -masm=gnu") != std::string::npos, "and so is a Debug c90 one");
        }
        check(editor::settings::rememberAssembler("bin/no-such.exe") && editor::settings::assembler().empty(),
              "a relative one that is not there counts for nothing either");
        {
            editor::Toolchain tool;
            tool.cxx1 = "cpp11.exe";
            check(editor::programRecipe(tool, editor::ToolCxx1, "a.cpp", editor::LangCpp,
                                        "x86_64-windows", editor::ConfigRelease).command.find("-masm") == std::string::npos,
                  "and with no assembler named, a Release Run file leaves cpp11's spelling alone");
        }
        check(editor::settings::rememberAssembler(std::string()) && editor::settings::assembler().empty(),
              "and `-` puts ml64 back");
        editor::settings::overrideTilinker("for-this-run");
        checkEqual(editor::settings::tilinker(), "for-this-run", "--tilinker wins for the run");
        editor::settings::overrideTilinker(std::string());
        check(editor::settings::rememberLinker(std::string()) && editor::settings::rememberTilinker(std::string())
                  && editor::settings::linker().empty() && editor::settings::tilinker().empty(),
              "and `-` puts link.exe and lnk6x back");
        // A linker named for tms6747 that has since gone: tilinker() answers
        // nothing, so TI's would stand in for it - and the build has to say
        // so rather than change what made the .out without a word.
        {
            std::string bin = editor::path::join(dir.string(), "tibin");
            file::create_directories(editor::path::join(bin, "bin"));
            std::string theirs = editor::path::join(editor::path::join(bin, "bin"), "lnk6x.exe");
            std::ofstream(theirs.c_str()) << "not a linker, but a file that is there\n";

            editor::LinkerChoice plain = editor::tiLinker(std::string(), std::string(), bin);
            checkEqual(plain.path, theirs, "nothing named: TI's own lnk6x");
            check(plain.say.empty(), "and nothing to say about it");

            editor::LinkerChoice gone = editor::tiLinker(std::string(), "D:\\LNK6X\\lnk6x.exe", bin);
            checkEqual(gone.path, theirs, "named but gone: TI's stands in");
            check(gone.say.find("LNK6X") != std::string::npos && gone.say.find("not there") != std::string::npos,
                  "and the build says the named one is not there");

            editor::LinkerChoice ours = editor::tiLinker(theirs, theirs, bin);
            checkEqual(ours.path, theirs, "named and there: that one");
            check(ours.say.find(theirs) != std::string::npos,
                  "and it is named by its path, which is all that tells it from TI's");

            check(editor::tiLinker(std::string(), std::string(), editor::path::join(dir.string(), "no-ti")).path.empty(),
                  "no TI directory: no linker to run");

            // --tilinker is not checked before it gets here, so a path typed
            // wrong must be named as the thing that is wrong.
            editor::LinkerChoice typo = editor::tiLinker("D:\\LNK6X\\nosuch.exe", "D:\\LNK6X\\nosuch.exe", bin);
            check(typo.path.empty(), "a linker named for this run that is not there stops the build");
            check(typo.say.find("nosuch.exe") != std::string::npos,
                  "and the build names it, not TI's directory");
            check(typo.say.find(bin) == std::string::npos,
                  "TI's install is not blamed for a flag typed wrong");
        }
        // The question a build asks when the project's own tools failed it. Ours are the tools by
        // default; "askNative" decides whether a failure asks to go to the vendor's - only when they
        // are on this machine - and a yes builds again with the four settings answering nothing. tms6747 serves every host here, TI's lnk6x being the fake one under tibin.
        {
            check(editor::settings::askNative(), "askNative is true unless the file says otherwise");
            std::string ours = editor::path::absolute((app / "settings.json").string());
            std::string tiDir = editor::path::join(dir.string(), "tibin");
            std::string q;
            // What a run of our own lnk6x that failed leaves in the output.
            const std::string failed = "lnk6x: something it could not do\nlnk6x did not link it - see its messages above\n";
            editor::settings::rememberTilinker(ours);
            editor::settings::rememberTi(std::string(), std::string());
            if (editor::settings::detectedTi().empty())
                check(!editor::nativeFallbackWanted(false, false, "tms6747", failed, q) &&
                          q.find("not on this machine") != std::string::npos,
                      "with no TI directory named or installed, nothing asks - the line says TI's lnk6x is not here");
            else
                check(editor::nativeFallbackWanted(false, false, "tms6747", failed, q) &&
                          q.find("TI's lnk6x") != std::string::npos,
                      "with none named, the CCS installed here is found, and a failed build asks about it");
            editor::settings::rememberTi(tiDir, std::string());
            check(editor::nativeToolsAvailable("tms6747"), "TI's directory named, its lnk6x is the native tool");
            check(editor::nativeFallbackWanted(false, false, "tms6747", failed, q) &&
                      q.find("own lnk6x") != std::string::npos && q.find("TI's lnk6x") != std::string::npos,
                  "a tms6747 build ours failed asks, naming lnk6x and what would stand in");
            check(!editor::nativeFallbackWanted(true, false, "tms6747", failed, q) && q.empty(),
                  "a build that succeeded does not");
            check(!editor::nativeFallbackWanted(false, true, "tms6747", failed, q),
                  "nor one the compiler found a fault in - the source is the user's");
            check(!editor::nativeFallbackWanted(false, false, "arm64-darwin", failed, q),
                  "nor a target none of ours serve");
            check(!editor::nativeFallbackWanted(false, false, "tms6747",
                                                "lnk6x: undefined symbol main\nlnk6x did not link it\n", q),
                  "nor a link our lnk6x refused for the program's own undefined symbol");
            check(!editor::nativeFallbackWanted(false, false, "x86_64-windows",
                                                "a.exe : fatal error LNK1120: 7 unresolved externals\n", q) && q.empty(),
                  "nor a cl build: cl ran its own link, and no tool of ours was in play");
            check(!editor::nativeFallbackWanted(false, false, "x86_64-windows",
                                                "link: unresolved external symbol:\n  main\ncpp11.exe: the assembler or linker failed\n", q),
                  "nor a program with no main - Microsoft's link refuses it the same way");
            check(!editor::nativeFallbackWanted(false, false, "x86_64-windows",
                                                "link: libmathx.a: no symbol index\nc90.exe: the assembler or linker failed\n", q),
                  "nor a library made for another platform");
            editor::settings::rememberTilinker(std::string());
            check(!editor::nativeFallbackWanted(false, false, "tms6747", failed, q) && q.empty(),
                  "tms6747 with no tilinker: nothing of ours in play, nothing to say");
            editor::settings::rememberTilinker(ours);
            editor::settings::forceNative(true);
            check(editor::settings::assembler().empty() && editor::settings::linker().empty() &&
                      editor::settings::tilinker().empty() && editor::settings::namedTilinker().empty(),
                  "a yes makes the four settings answer nothing, so every recipe reaches for the vendor's");
            check(!editor::nativeFallbackWanted(false, false, "tms6747", failed, q), "and the second build does not ask again");
            editor::settings::forceNative(false);
            check(editor::settings::rememberAskNative(false) && !editor::settings::askNative(),
                  "askNative false is kept");
            check(!editor::nativeFallbackWanted(false, false, "tms6747", failed, q), "and then nothing asks: the build fails as it failed");
            editor::settings::rememberAskNative(true);
#ifdef _WIN32
            editor::settings::rememberAssembler("bin/masm.exe");
            editor::settings::rememberLinker(ours);
            // What our own masm or link leaves when it fails an x86_64-windows build - since 4.51 only that asks.
            const std::string winFailed = "masm: something it could not do\nc90.exe: the assembler or linker failed\n";
            if (editor::nativeToolsAvailable("x86_64-windows")) {
                check(editor::nativeFallbackWanted(false, false, "x86_64-windows", winFailed, q) &&
                          q.find("masm and link") != std::string::npos && q.find("ml64 and link.exe") != std::string::npos,
                      "a Windows build ours failed asks, naming masm and link and what would stand in");
                editor::settings::rememberLinker(std::string());
                check(editor::nativeFallbackWanted(false, false, "x86_64-windows", winFailed, q) &&
                          q.find("own masm ") != std::string::npos && q.find("link.exe") == std::string::npos,
                      "with only the assembler named, the question names only it");
            } else {
                check(!editor::nativeFallbackWanted(false, false, "x86_64-windows", winFailed, q) &&
                          q.find("no Visual Studio") != std::string::npos,
                      "with no Visual Studio, nothing asks - the line says so");
            }
#endif

            // Through a build: a compiler that is not there is no fault of our lnk6x, and nothing asks.
            askedQuestion.clear(); askedAnswer = true;
            editor::setAskNative(rememberTheQuestion, 0);
            editor::Toolchain tool;
            tool.cc1 = "cc1-that-is-not-there";
            std::vector<std::string> srcs(1, editor::path::join(dir.string(), "any.c"));
            writeSource(srcs[0], "int main(void) { return 0; }\n");
            std::string any = editor::path::join(dir.string(), "any.exe");
            editor::Built missing = editor::buildTarget(tool, editor::ToolCc1, srcs, editor::LangC, "tms6747",
                                                        editor::ConfigRelease, any);
            check(askedQuestion.empty() && missing.output.find("did not build it") == std::string::npos,
                  "a compiler that is not there asks nothing - our own tools were not what failed");
            // The question put, a yes building again - the wrapper, on every host - with a stand-in
            // compiler whose output is what our lnk6x leaves when it fails.
#ifdef _WIN32
            tool.cc1 = editor::path::join(dir.string(), "failing-lnk6x.cmd");
            writeSource(tool.cc1, "@echo off\r\necho lnk6x did not link it - see its messages above\r\nexit /b 1\r\n");
#else
            tool.cc1 = editor::path::join(dir.string(), "failing-lnk6x.sh");
            writeSource(tool.cc1, "#!/bin/sh\necho 'lnk6x did not link it - see its messages above'\nexit 1\n");
            chmod(tool.cc1.c_str(), 0755);
#endif
            editor::Built twice = editor::buildTarget(tool, editor::ToolCc1, srcs, editor::LangC, "tms6747",
                                                      editor::ConfigRelease, any);
            check(!askedQuestion.empty() && askedQuestion.find("lnk6x") != std::string::npos,
                  "a failed build puts the question to the front end");
            check(twice.output.find("building again with TI's lnk6x in place of the project's lnk6x, as asked") != std::string::npos,
                  "and a yes builds again, saying with what (D5):\n" + twice.output);
            check(twice.output.find("[not built: TI's lnk6x failed as well]") != std::string::npos,
                  "and the second build failing is said as such (D5)");
            check(!editor::settings::nativeForced(), "the switch is back off afterwards");
            askedQuestion.clear(); askedAnswer = false;
            editor::Built once = editor::buildTarget(tool, editor::ToolCc1, srcs, editor::LangC, "tms6747",
                                                     editor::ConfigRelease, any);
            check(!askedQuestion.empty() && once.output.find("building again") == std::string::npos,
                  "a no leaves the first answer standing");
            editor::setAskNative(0, 0);
            askedQuestion.clear();
            editor::Built unasked = editor::buildTarget(tool, editor::ToolCc1, srcs, editor::LangC, "tms6747",
                                                        editor::ConfigRelease, any);
            check(askedQuestion.empty() && unasked.output.find("nothing here can ask") != std::string::npos,
                  "with no front end to ask - --build, --run - the line says what would have been asked");
#ifdef _WIN32
            // And for real, where c90 and ml64 are: a masm.exe that is not an
            // assembler fails c90, the yes goes to ml64, and the program is made.
            const char* cc1Here = std::getenv("CC1");
            if (cc1Here && *cc1Here && editor::path::exists(cc1Here) && editor::nativeToolsAvailable("x86_64-windows")) {
                editor::setAskNative(rememberTheQuestion, 0);
                askedQuestion.clear(); askedAnswer = true;
                editor::settings::rememberAssembler("bin/masm.exe");
                editor::settings::rememberLinker(std::string());
                editor::Toolchain real;
                real.cc1 = cc1Here;
                editor::Built forReal = editor::buildTarget(real, editor::ToolCc1, srcs, editor::LangC, "x86_64-windows",
                                                         editor::ConfigRelease, any);
                check(askedQuestion.find("own masm") != std::string::npos, "for real: a masm.exe that is no assembler fails c90 and asks");
                check(forReal.ok, "and the yes builds the program through ml64");
                check(forReal.output.find("[built with Visual Studio's ml64, not RIDE's masm]") != std::string::npos,
                      "and the program is marked as the vendor's tools' (D5):\n" + forReal.output);
                if (forReal.ok) editor::path::remove(forReal.program);
                editor::setAskNative(0, 0);
            }
            editor::settings::rememberAssembler(std::string());
            editor::settings::rememberLinker(std::string());
#endif
            editor::settings::rememberTilinker(std::string());
            editor::settings::rememberTi(std::string(), std::string());
        }
        editor::settings::pretendInstalledAt(std::string());
        check(editor::settings::includeDir().empty() || true, "and the suite's own binary is back in charge");
    }

    // A directory with no project file is not a failure - it means there is no
    // project, and the pane shows the directory instead.
    file::path bare = dir / "empty";
    file::create_directories(bare);
    editor::Project none;
    check(!none.load(bare.string(), error), "a directory with no project file");
    check(error.empty(), "is not an error");

    // The smallest file that works. Everything has a default, so an empty
    // object is a valid project.
    file::path tiny = dir / "tiny";
    file::create_directories(tiny);
    { std::ofstream f((tiny / "project.pro").string().c_str()); f << "{}\n"; }
    editor::Project small;
    check(small.load(tiny.string(), error), "an empty object is a project");
    check(small.builds() && small.target().groups.size() == 1 && small.target().groups[0] == "Sources",
          "and, saying nothing about a build, builds its Sources");
    check(error.empty() && small.indent().width == 4 && !small.indent().tabs,
          "and every setting falls back to its default");
    // A project written on another machine names that machine's target; opened
    // here it is this machine's, and the emulated target is kept as written.
    file::path moved = dir / "moved";
    file::create_directories(moved);
    const char* elsewhere = std::string(editor::hostArch()) == "x86_64-linux" ? "arm64-darwin" : "x86_64-linux";
    { std::ofstream f((moved / "project.pro").string().c_str()); f << "{ \"arch\": \"" << elsewhere << "\" }\n"; }
    editor::Project came;
    check(came.load(moved.string(), error) && came.arch() == editor::hostArch(),
          "another host's target reads as this host's");
    { std::ofstream f((moved / "project.pro").string().c_str()); f << "{ \"arch\": \"tms6747\" }\n"; }
    editor::Project emulated;
    check(emulated.load(moved.string(), error) && emulated.arch() == "tms6747",
          "and the emulated target stays as written");
    // From a home of its own: the live one says whatever was chosen last,
    // which failed this on the box every time Release was selected there.
    std::string wasHome = editor::path::homeDir();
    file::create_directories(dir / "home");
    sayWhereHomeIs((dir / "home").string());
    checkEqual(editor::settings::configuration(), "debug",
               "and the configuration defaults to debug, which is what you want "
               "while the code is still being written");
    sayWhereHomeIs(wasHome);

    file::remove_all(dir);
}

void operations() {
    std::printf("changing what the project holds\n");

    file::path dir = file::temp_directory_path() / "ride-workspace-test";
    file::remove_all(dir);
    file::create_directories(dir);

    editor::Project project;
    project.begin(dir.string(), "Work");

    // Making a file: on disk, in the project, and the project written back.
    editor::Outcome made = editor::createFile(project, "src/one.c", "Sources");
    check(made.ok, "a file is made");
    check(file::exists(dir / "src" / "one.c"), "and it is there on disk");
    check(project.groupOf("src/one.c") < project.groups().size(), "and in the project");
    // Named after the project, not the directory: begin() was given "Work".
    check(file::exists(dir / "Work.pro"), "and the project was written, under its own name");
    check(!made.path.empty(), "and it says where the file went");

    check(!editor::createFile(project, "src/one.c", "Sources").ok, "twice is refused");
    editor::Outcome deep = editor::createFile(project, "a/b/c.c", "Sources");
    check(!deep.ok, "and so is anything two directories down");
    check(deep.message.find("two levels") != std::string::npos, "with the rule as the reason");
    check(!file::exists(dir / "a"), "and nothing was written for it");

    // Renaming follows on disk and in the list.
    editor::Outcome moved =
        editor::renameFile(project, (dir / "src" / "one.c").string(), "src/two.c");
    check(moved.ok, "renaming works");
    check(!file::exists(dir / "src" / "one.c"), "the old name is gone");
    check(file::exists(dir / "src" / "two.c"), "the new one is there");
    check(project.groupOf("src/two.c") < project.groups().size(), "and the project followed");

    // Regrouping changes the lists and nothing else.
    editor::Outcome grouped =
        editor::moveToGroup(project, (dir / "src" / "two.c").string(), "Extras");
    check(grouped.ok, "regrouping works");
    checkEqual(project.groups()[project.groupOf("src/two.c")].name, "Extras",
               "into the group asked for");
    check(file::exists(dir / "src" / "two.c"), "and the file has not moved");

    // Adding something that already exists.
    { std::ofstream f((dir / "src" / "three.c").string().c_str()); f << "int three;\n"; }
    check(editor::addExisting(project, (dir / "src" / "three.c").string(), "Sources").ok,
          "a file already on disk can be added");
    check(!editor::addExisting(project, (dir / "src" / "three.c").string(), "Sources").ok,
          "but not twice");

    // Deleting.
    editor::Outcome gone = editor::deleteFile(project, (dir / "src" / "two.c").string());
    check(gone.ok, "deleting works");
    check(!file::exists(dir / "src" / "two.c"), "the file is gone");
    check(project.groupOf("src/two.c") == project.groups().size(), "and so is the entry");

    // And what was written survives being read again.
    editor::Project again;
    std::string error;
    check(again.load(dir.string(), error), "the project reads back");
    check(again.groupOf("src/three.c") < again.groups().size(), "with what was added to it");
    check(again.groupOf("src/two.c") == again.groups().size(), "and without what was deleted");

    // With no project file there is nothing to write, and the disk work still
    // stands - which is what lets these run before anyone has made a project.
    file::path bare = dir / "bare";
    file::create_directories(bare);
    editor::Project none;
    none.setRoot(bare.string());
    editor::Outcome loose = editor::createFile(none, "loose.c", "Sources");
    check(loose.ok, "a file can be made without a project");
    check(file::exists(bare / "loose.c"), "and it is really there");
    check(editor::Project::projectFilesIn(bare.string()).empty(),
          "and no project file was invented");
    // Renamed with no project: beside itself, by its bare name.
    {
        editor::Project alone;
        std::string here = (file::temp_directory_path() / "ride-rename-alone").string();
        editor::path::makeDirectories(here);
        writeSource(editor::path::join(here, "was.c"), "int main(void) { return 0; }\n");
        editor::Outcome renamed = editor::renameFile(alone, editor::path::join(here, "was.c"), "now.c");
        check(renamed.ok && editor::path::exists(editor::path::join(here, "now.c")) &&
              !editor::path::exists(editor::path::join(here, "was.c")),
              "a file of no project is renamed where it is");
        check(!editor::renameFile(alone, editor::path::join(here, "now.c"), "now.c").ok,
              "and not over itself");
        editor::path::removeTree(here);
    }

    file::remove_all(dir);
}

void whatTheBuildMade() {
    std::printf("reading a build out of its assembly\n");

    // cc1's own output, in the GNU spelling it uses for arm64 and Linux.
    std::vector<std::string> gnu = split(
        ".L.str.0:\n"
        "  .ascii \"as expected\\000\"\n"
        "  .globl _main\n"
        "_factorial:\n"
        "  stp x29, x30, [sp, #-16]!\n"
        "  mov x9, #16\n"
        "  sub sp, sp, x9\n"
        "L.factorial.end.0:\n"
        "  ret\n");

    std::vector<editor::Symbol> gnuFound = editor::symbolsIn(gnu);

    int functions = 0, exported = 0, strings = 0;
    std::string frame;
    for (size_t i = 0; i < gnuFound.size(); ++i) {
        if (gnuFound[i].kind == editor::SymbolFunction) {
            ++functions;
            if (gnuFound[i].name == "_factorial") frame = gnuFound[i].detail;
        }
        if (gnuFound[i].kind == editor::SymbolExported) ++exported;
        if (gnuFound[i].kind == editor::SymbolText) ++strings;
    }
    check(functions == 1, "a function is found in the GNU spelling");
    check(exported == 1, "and what is exported");
    check(strings == 1, "and a string");
    checkEqual(frame, "stack 16 bytes",
               "and the stack it takes, though arm64 puts the number in a register first");

    // The compiler's own labels are not symbols anyone asked for.
    for (size_t i = 0; i < gnuFound.size(); ++i)
        check(gnuFound[i].name.compare(0, 2, "L.") != 0,
              "the compiler's own labels are left out");

    // MASM, which cc1 writes for Windows and cl writes always.
    std::vector<std::string> masm = split(
        "PUBLIC main\n"
        "EXTERN puts:PROC\n"
        "$_L_str_1 DB 115, 111, 109, 101, 116, 104, 105, 110, 103, 32, 105, 115, 32, 119\n"
        "  DB 114, 111, 110, 103, 0\n"
        "factorial PROC FRAME\n"
        "  sub rsp, 16\n"
        "  .ENDPROLOG\n"
        "factorial ENDP\n");

    std::vector<editor::Symbol> masmFound = editor::symbolsIn(masm);

    std::string said, stack;
    bool sawExternal = false;
    for (size_t i = 0; i < masmFound.size(); ++i) {
        if (masmFound[i].kind == editor::SymbolText) said = masmFound[i].detail;
        if (masmFound[i].kind == editor::SymbolExternal) sawExternal = true;
        if (masmFound[i].kind == editor::SymbolFunction) stack = masmFound[i].detail;
    }
    checkEqual(said, "something is wrong",
               "a string broken across two DB lines is put back together");
    check(sawExternal, "what is called but not defined is found");
    checkEqual(stack, "stack 16 bytes", "and the stack a MASM function takes");

    // cl's own way of writing a string.
    std::vector<std::string> cl = split("$SG5346 DB 'first is %d', 0aH, 00H\n");
    std::vector<editor::Symbol> clFound = editor::symbolsIn(cl);
    check(clFound.size() == 1 && clFound[0].detail.compare(0, 8, "first is") == 0,
          "and cl's quoted form of the same thing");

    // Nothing in, nothing claimed.
    std::vector<std::string> nothing;
    check(editor::symbolsIn(nothing).empty(), "no assembly means no symbols");
    check(!editor::describe(editor::symbolsIn(nothing)).empty(),
          "and it says so rather than showing a blank");
}

}  // namespace

// The paths, which used to be std::filesystem's business and are now this
// project's. Everything above this leans on them, so they are worth pinning
// down on their own rather than only through what uses them.
void paths() {
    std::printf("paths, without <filesystem>\n");

    namespace p = editor::path;

    check(p::withSlashes("a\\b\\c") == "a/b/c", "backslashes are turned round on the way in");
    check(p::join("a", "b") == "a/b", "joining puts one slash between");
    check(p::join("a/", "b") == "a/b", "and not two when there is one already");
    check(p::join("", "b") == "b", "and none in front of nothing");
    check(p::filename("a/b/c.c") == "c.c", "the name is what is after the last slash");
    check(p::filename("c.c") == "c.c", "or the whole of it when there is none");
    check(p::parent("a/b/c.c") == "a/b", "the parent is what is before it");
    check(p::parent("c.c").empty(), "and nothing when there is nothing before it");
    check(p::parent("/c.c") == "/", "the root being its own parent's whole name");

    // . and .. are taken out the way a filesystem takes them out, and a path
    // already absolute is left where it is.
    check(p::absolute("/a/b/../c") == "/a/c", "'..' cancels the name before it");
    check(p::absolute("/a/./b") == "/a/b", "and '.' cancels itself");
    check(p::absolute("/a//b") == "/a/b", "a doubled slash is one slash");
    check(p::absolute("/a/b/") == "/a/b", "and a trailing one is none");

    // What a project file holds: the way from the project's directory to a
    // file in it, which is the one thing here with real work in it.
    check(p::relativeTo("/w/src/one.c", "/w") == "src/one.c", "down into the project");
    check(p::relativeTo("/w/one.c", "/w") == "one.c", "or straight into it");
    check(p::relativeTo("/w", "/w") == ".", "a directory against itself is here");
    check(p::relativeTo("/w/one.c", "/w/src") == "../one.c", "and up when it has to be");
    check(p::relativeTo("/a/one.c", "/b/deep/er") == "../../../a/one.c",
          "up as many times as it takes, then down");

    // On disk. A directory made several deep at once, a file moved, a file
    // taken away, and the whole lot removed at the end.
    std::string dir = p::join(p::tempDir(), "ride-path-test");
    p::removeTree(dir);
    check(!p::exists(dir), "the temporary directory starts absent");

    check(p::makeDirectories(p::join(dir, "one/two/three")),
          "every directory on the way is made");
    check(p::isDirectory(p::join(dir, "one/two/three")), "and the last of them is there");
    check(p::makeDirectories(p::join(dir, "one/two")), "making one twice is not a failure");

    std::string file = p::join(dir, "one/two/three/a.c");
    FILE* made = std::fopen(file.c_str(), "wb");
    if (made) std::fclose(made);
    check(p::exists(file), "a file written into it is there");
    check(!p::isDirectory(file), "and is not a directory");

    std::string moved = p::join(dir, "one/two/three/b.c");
    check(p::rename(file, moved), "it can be renamed");
    check(!p::exists(file) && p::exists(moved), "which takes the old name away");

    // One name for a file, whatever spelling it arrives in. This is what keeps
    // one file to one tab, and one file to one set of breakpoints.
    check(p::same(moved, p::withSlashes(moved)), "a path is the same file as itself");
    check(p::same(moved, p::join(dir, "one/two/./three/b.c")),
          "and so is the same path written through a dot");
    check(p::same(moved, p::join(dir, "one/two/three/../three/b.c")),
          "and one written through a step up and back");
    check(!p::same(moved, p::join(dir, "one/two/three/c.c")),
          "two different files are not the same file");
    check(p::oneName(moved) == p::oneName(moved), "the one name is stable");

    // What is in a directory, without . and .., and saying which are which.
    bool readable = false;
    std::vector<p::Entry> inside = p::entries(p::join(dir, "one"), &readable);
    check(readable, "a directory that is there can be read");
    check(inside.size() == 1 && inside[0].name == "two" && inside[0].directory,
          "and holds the one directory that was made in it");

    p::entries(p::join(dir, "nowhere"), &readable);
    check(!readable, "and one that is not there says so rather than looking empty");

    check(p::remove(moved), "a file can be removed");
    check(!p::exists(moved), "and is gone afterwards");

    // The recursive one, and the two things it refuses to do.
    check(!p::removeTree(""), "nothing is removed when nothing is named");
    check(!p::removeTree("/"), "and a root is refused outright");
    check(p::removeTree(dir), "a directory goes, and everything under it");
    check(!p::exists(dir), "leaving nothing behind");
}

// Where the running program is, and what is next to it. This is how the editor
// finds a compiler installed alongside it, so the answer has to be the
// program's own directory whatever directory it was started in.
void whereTheProgramIs(const char* argv0) {
    std::printf("where the program is, and what is beside it\n");

    namespace p = editor::path;

    const std::string where = p::programDirectory();
    check(!where.empty(), "the machine says where the running program is");
    check(p::isDirectory(where), "and it is a directory");
    checkEqual(where, p::withSlashes(where), "in forward slashes, like everything here");
    checkEqual(where, p::absolute(where), "and absolute, with nothing left to resolve");

    // Whatever this binary is called - test on a Mac or a Linux box, test.exe on Windows, and
    // whatever anyone renames it to - it is beside itself, so asking for its own name has to find
    // it. Taking the name from argv[0] rather than writing "test" here keeps that true.
    std::string me = p::filename(p::withSlashes(argv0 ? argv0 : ""));
    if (me.size() > 4 && me.compare(me.size() - 4, 4, ".exe") == 0) me.resize(me.size() - 4);
    check(!me.empty(), "this test knows what it was called");

    const std::string found = p::besideProgram(me);
    check(!found.empty(), "a program beside the running one is found");
    check(p::exists(found), "and what comes back is really there");
    checkEqual(p::parent(found), where, "in the directory the program is in");

    // And the name exactly as it was invoked, suffix and all. Windows used to append .exe
    // unconditionally, so a caller naming "test.exe" - what the binaries are called everywhere now -
    // was asking for test.exe.exe. About did that for all three compilers and reported them missing.
    const std::string asInvoked = p::filename(p::withSlashes(argv0 ? argv0 : ""));
    check(!p::besideProgram(asInvoked).empty(),
          "the name as it was invoked is found, suffix and all");

    check(p::besideProgram("").empty(), "nothing is beside nothing");
    check(p::besideProgram("cc1-nobody-has-installed").empty(),
          "a name that is not there is not answered with a path");

    // A directory of the right name is not a program, and answering with one
    // would put it on a command line to be run.
#ifdef _WIN32
    const std::string decoyLeaf = "beside-decoy.exe";
#else
    const std::string decoyLeaf = "beside-decoy";
#endif
    const std::string decoy = p::join(where, decoyLeaf);
    if (p::makeDirectories(decoy)) {
        check(p::besideProgram("beside-decoy").empty(),
              "a directory of that name is not a program");
        p::removeTree(decoy);
    }
}

// Talking to a child rather than only listening to one. Everything else here
// runs a command with popen, says nothing to it and reads until it ends; a
// debugger needs the other direction as well.
void talkingToAChild() {
    std::printf("a child that answers back\n");

    // Something that reads lines and writes them straight back. cat does it on one machine; on the
    // other findstr looks like the answer and is not - it holds its output until it exits, so a
    // marker sent to it never comes back, and the wait for it hung this suite on the Windows box.
#ifdef _WIN32
    const char* echoes =
        "powershell -NoProfile -Command \"while (($l = [Console]::In.ReadLine()) -ne $null)"
        " { [Console]::Out.WriteLine($l); [Console]::Out.Flush() }\"";
#else
    const char* echoes = "cat";
#endif

    editor::Process child;
    check(child.start(echoes), "a child starts");
    check(child.running(), "and says it is running");

    check(child.say("first <<mark>>"), "a line can be said to it");
    bool found = false;
    std::string answer = child.readUntil("<<mark>>", &found);
    check(found, "and the marker in the answer is found");
    check(answer.find("first") != std::string::npos, "with what came before it");

    // The second answer must not carry the first: what was read past the
    // marker last time is kept for this time rather than thrown away.
    check(child.say("second <<mark>>"), "and another after it");
    answer = child.readUntil("<<mark>>", &found);
    check(found, "which is found too");
    check(answer.find("second") != std::string::npos, "with its own line");
    check(answer.find("first") == std::string::npos, "and not the one before it");

    child.stop();
    check(!child.running(), "it stops when it is told to");

    // A marker that will never arrive ends when the child does, rather than
    // waiting for it forever.
    editor::Process brief;
    check(brief.start("exit 0"), "a child that does nothing starts");
    brief.readUntil("<<never>>", &found);
    check(!found, "a marker that never comes is not reported as found");
    check(!brief.running(), "and the child is known to have gone");

    editor::Process missing;
    // The shell is what fails here, not this - it is started either way and
    // says its piece on the same stream.
    missing.start("no-such-program-ride-test");
    missing.readUntil("<<never>>", &found);
    check(!found, "a command that is not there answers nothing");
    missing.stop();
}

// Where output goes when nobody wants it. /dev/null is not a path on Windows - cmd has NUL - so a
// command redirecting to it there fails to run at all, and a case that built with one silently
// reported "shc did not build it", on the machine where shc had only just been made to exist.
#ifdef _WIN32
const char* const kNowhere = " > NUL 2>&1";
#else
const char* const kNowhere = " > /dev/null 2>&1";
#endif

// A command for std::system, which on Windows goes through `cmd /c`. cmd strips the first and last
// quote when a command has more than two, so `"shc.exe" "in.shl" -o "out.exe"` reaches the shell as
// garbage; an extra pair round the whole thing is what cmd then eats. src/compile.cpp's runCaptured has said this since it was written; this suite did not, and built nothing with an empty log.
std::string shellCommand(const std::string& command) {
#ifdef _WIN32
    return "\"" + command + "\"";
#else
    return command;
#endif
}

std::string readWholeFile(const std::string& where) {
    std::ifstream in(where.c_str(), std::ios::binary);
    std::stringstream all;
    all << in.rdbuf();
    return all.str();
}


// What the two debuggers say when they stop, which is the fiddly half of
// driving them and needs neither a debugger nor a built program to check.
// Both of these are what they actually printed, kept as they came.
const char* const kLldbStop =
    "Process 10819 stopped\n"
    "* thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.1\n"
    "    frame #0: 0x0000000100000508 dbg`main at dbg.c:13:9\n"
    "   12  \tfor (int i = 1; i <= 3; ++i) {\n"
    "-> 13  \t    total = total + twice(i);\n";

// A function with two arguments, which is where the name used to be lost: the comma inside the
// argument list was the last one on the line, and the reader cut there. Every other recording here
// takes one argument or none, so nothing noticed until a two-argument function was stepped into.
const char* const kLldbStopTwoArgs =
    "Process 41207 stopped\n"
    "* thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.1\n"
    "    frame #0: 0x0000000100000440 sums`addUp(a=2, b=40) at sum.c:3:27\n"
    "   2   \t\n"
    "-> 3   \tint addUp(int a, int b) { return a + b; }\n";

// The same shape from gdb, which writes the address and " in " when it did not
// stop at the start of a line.
const char* const kGdbStopTwoArgs =
    "Breakpoint 1, addUp (a=2, b=40) at sum.c:3\n"
    "3\tint addUp(int a, int b) { return a + b; }\n";

const char* const kGdbStop =
    "Breakpoint 1, main () at dbg.c:13\n"
    "13\t        total = total + twice(i);\n";

// Four goes at one step, exactly as lldb printed them, from the project the session suite builds:
// the breakpoint on sum.c:3 and each of these one `next` after the last. The first two go nowhere -
// the line stays, the addresses climb - and only the fourth arrives in main, where gdb on the same DWARF arrives in one. That is what dbg_wentNowhere is for.
const char* const kLldbStopAtBreak =
    "Process 20033 stopped\n"
    "* thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.1\n"
    "    frame #0: 0x0000000100000440 sums`addUp(a=2, b=40) at sum.c:3:27\n"
    "   2   \t\n"
    "-> 3   \tint addUp(int a, int b) { return a + b; }\n"
    "Target 0: (sums) stopped.\n";

const char* const kLldbStepStillThere =
    "Process 20033 stopped\n"
    "* thread #1, queue = 'com.apple.main-thread', stop reason = step over\n"
    "    frame #0: 0x000000010000046c sums`addUp(a=2, b=40) at sum.c:3:27\n"
    "   2   \t\n"
    "-> 3   \tint addUp(int a, int b) { return a + b; }\n"
    "Target 0: (sums) stopped.\n";

const char* const kLldbStepStillThereAgain =
    "Process 20033 stopped\n"
    "* thread #1, queue = 'com.apple.main-thread', stop reason = step over\n"
    "    frame #0: 0x000000010000047c sums`addUp(a=1, b=-253525928) at sum.c:3:27\n"
    "   2   \t\n"
    "-> 3   \tint addUp(int a, int b) { return a + b; }\n"
    "Target 0: (sums) stopped.\n";

const char* const kLldbStepIntoTheCaller =
    "Process 20033 stopped\n"
    "* thread #1, queue = 'com.apple.main-thread', stop reason = step over\n"
    "    frame #0: 0x00000001000004ac sums`main at main.c:7:5\n"
    "   6   \t{\n"
    "-> 7   \t    printf(\"answer %d\\n\", addUp(2, 40));\n"
    "   8   \t    return 0;\n"
    "Target 0: (sums) stopped.\n";

// The same one press of `next`, on the Linux box, on the same two files built by the same cc1.
// Here so that the two transcripts sit beside each other: gdb names the caller straight away,
// which is what F7 is supposed to do and what dbg_wentNowhere makes lldb do as well.
const char* const kGdbStepIntoTheCaller =
    "main () at /home/ec2-user/sumsprobe/src/main.c:8\n"
    "8\t    return 0;\n";

// Recursion, the one thing a rule of "the same line twice is not a move" would get wrong: fact
// calls itself on the line it is defined on, so stepping into itself is the same file, line and
// function, and a real arrival. The address tells them apart - a shuffle goes forward, a step into the call goes back to the prologue. Four `step`s from cc1's arm64-darwin build, the breakpoint left in main.
const char* const kLldbInFactFirst =
    "Process 25713 stopped\n"
    "* thread #1, queue = 'com.apple.main-thread', stop reason = step in\n"
    "    frame #0: 0x0000000100000430 r`fact(n=4) at fact.c:1:19\n"
    "-> 1   \tint fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }\n"
    "Target 0: (r) stopped.\n";

const char* const kLldbInFactAgain =
    "Process 25713 stopped\n"
    "* thread #1, queue = 'com.apple.main-thread', stop reason = step in\n"
    "    frame #0: 0x0000000100000430 r`fact(n=3) at fact.c:1:19\n"
    "-> 1   \tint fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }\n"
    "Target 0: (r) stopped.\n";

const char* const kLldbInFactShuffled =
    "Process 25713 stopped\n"
    "* thread #1, queue = 'com.apple.main-thread', stop reason = step in\n"
    "    frame #0: 0x0000000100000460 r`fact(n=2) at fact.c:1:19\n"
    "-> 1   \tint fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }\n"
    "Target 0: (r) stopped.\n";

const char* const kLldbInFactDeeper =
    "Process 25713 stopped\n"
    "* thread #1, queue = 'com.apple.main-thread', stop reason = step in\n"
    "    frame #0: 0x0000000100000430 r`fact(n=1) at fact.c:1:19\n"
    "-> 1   \tint fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }\n"
    "Target 0: (r) stopped.\n";

// vm6747 --debug and sim6747 --run --debug answer in gdb's shapes (VM6747 src/Debug.h), and the gdb parsers
// read them: these are the emulator's own lines, from VM6747's tests/debug/step.expected.
void whatTheC6xEmulatorSays() {
    std::printf("what the C6747 emulator says, read as gdb\n");

    check(editor::dbg_gdbLike(editor::DebuggerC6x), "the C6x kind speaks as gdb does");
    check(!editor::dbg_gdbLike(editor::DebuggerCdb), "and cdb does not");

    editor::Stop hit = editor::dbg_readStop(editor::DebuggerC6x,
        "Starting program: step.s\n\nBreakpoint 1, main () at step.cpp:18\n18\t    int x = add(1, 2);\n");
    check(hit.stopped && !hit.exited, "a breakpoint hit is a stop");
    checkEqual(hit.function, "main", "C6x: in the function the emulator names");
    checkEqual(hit.file, "step.cpp", "C6x: in the file the .dwpsn named");
    check(hit.line == 18, "C6x: on its line");

    editor::Stop stepped = editor::dbg_readStop(editor::DebuggerC6x, "19\t    printf(\"x=%d\\n\", x);\n");
    check(stepped.stopped && stepped.line == 19, "C6x: a step within the function is the echo alone, and its line is read");

    editor::Stop into = editor::dbg_readStop(editor::DebuggerC6x, "_Z3addii () at step.cpp:11\n11\t    int c = a + b;\n");
    checkEqual(into.function, "_Z3addii", "C6x: a step into a function names it (as mangled, which is what the label is)");
    check(into.line == 11, "C6x: at its first statement");

    editor::Stop done = editor::dbg_readStop(editor::DebuggerC6x, "y=14\n[Inferior 1 (process 1) exited with code 06]\n");
    check(done.exited && !done.stopped, "C6x: the end is an exit");
    check(done.status == 6, "C6x: with the status main returned, read as gdb's octal");

    editor::Stop clean = editor::dbg_readStop(editor::DebuggerC6x, "[Inferior 1 (process 1) exited normally]\n");
    check(clean.exited && clean.status == 0, "C6x: and a zero status is 'normally'");

    std::vector<editor::StackFrame> stack = editor::dbg_readFrames(editor::DebuggerC6x,
        "#0  _ZL5twicei () at step.cpp:5\n#1  0x000010c0 in _Z3addii () at step.cpp:12\n#2  0x00001148 in main () at step.cpp:18\n");
    check(stack.size() == 3, "C6x: three frames read");
    if (stack.size() == 3) {
        checkEqual(stack[1].function, "_Z3addii", "C6x: the caller, past its address");
        check(stack[2].line == 18, "C6x: main's line");
    }

    std::vector<editor::Variable> none = editor::dbg_readVariables(editor::DebuggerC6x, "No symbol table info available.\n");
    check(none.empty(), "C6x: no locals - the emulator has no variable information to give");

    checkEqual(editor::dbg_readValue(editor::DebuggerC6x, "$1 = 10\n"), "10", "C6x: a register's value, as gdb prints one");
    checkEqual(editor::dbg_readType(editor::DebuggerC6x, "type = int\n"), "int", "C6x: and its type");

    std::string output = editor::dbg_programOutput(editor::DebuggerC6x,
        "Continuing.\nx=6\n20\t    int y = add(x, 4);\n\nBreakpoint 2, _ZL5twicei () at step.cpp:3\n3\tstatic int twice(int n)\n<<ride-done>>\n");
    checkEqual(output, "x=6\n", "C6x: the program's own output, the emulator's lines and the echo taken out");

    checkEqual(editor::dbg_name(editor::DebuggerC6x), "vm6747", "the C6x kind is named after the emulator");
    check(editor::emitsDebugInfo(editor::ToolCxx1, "tms6747"), "cpp11 writes a line table for tms6747 now");
    check(editor::emitsDebugInfo(editor::ToolCc1, "tms6747"), "and so does c90, since 2026-10-13");
}

void whatADebuggerSays() {
    std::printf("where a debugger says it stopped\n");

    editor::Stop twoArgs = editor::dbg_readStop(editor::DebuggerLldb, kLldbStopTwoArgs);
    checkEqual(twoArgs.function, "addUp", "lldb: a two-argument function keeps its name");
    checkEqual(twoArgs.file, "sum.c", "lldb: and the file it is in");
    check(twoArgs.line == 3, "lldb: on the line it stopped at");

    editor::Stop twoArgsGdb = editor::dbg_readStop(editor::DebuggerGdb, kGdbStopTwoArgs);
    checkEqual(twoArgsGdb.function, "addUp", "gdb: the same, past its own preamble");
    check(twoArgsGdb.line == 3, "gdb: and the line");

    editor::Stop lldb = editor::dbg_readStop(editor::DebuggerLldb, kLldbStop);
    check(lldb.stopped, "lldb's stop is read as a stop");
    check(lldb.file == "dbg.c", "with the file it names");
    check(lldb.line == 13, "and the line");
    check(lldb.function == "main", "and the function, without the program in front of it");
    check(!lldb.exited, "and it has not exited");

    editor::Stop gdb = editor::dbg_readStop(editor::DebuggerGdb, kGdbStop);
    check(gdb.stopped && gdb.file == "dbg.c" && gdb.line == 13,
          "gdb says the same thing in its own words");
    check(gdb.function == "main", "including the function, without its empty brackets");

    // lldb writes file:line:column and gdb writes file:line. The column must
    // not be read as the line, which is the one way this goes quietly wrong.
    editor::Stop inside = editor::dbg_readStop(
        editor::DebuggerLldb, "    frame #0: 0x100 dbg`twice(n=1) at dbg.c:5:9\n");
    check(inside.line == 5, "a column after the line is not mistaken for it");
    check(inside.function == "twice", "and arguments are not part of the name");

    // Gone, and what it went with.
    editor::Stop doneLldb = editor::dbg_readStop(
        editor::DebuggerLldb, "Process 10819 exited with status = 3 (0x00000003)\n");
    check(doneLldb.exited && !doneLldb.stopped, "a program that ended is not stopped");
    check(doneLldb.status == 3, "and what it returned is read");

    editor::Stop doneGdb = editor::dbg_readStop(
        editor::DebuggerGdb, "[Inferior 1 (process 41) exited with code 03]\n");
    check(doneGdb.exited && doneGdb.status == 3, "gdb's way of saying it is read too");

    // gdb prints that code in octal, so the two agree on three and disagree on
    // anything above seven. Twelve is where it would have gone wrong quietly.
    check(editor::dbg_readStop(editor::DebuggerGdb,
                           "[Inferior 1 (process 41) exited with code 014]\n").status == 12,
          "and it is read as the octal gdb wrote");
    check(editor::dbg_readStop(editor::DebuggerLldb,
                           "Process 41 exited with status = 12 (0x0000000c)\n").status == 12,
          "while lldb's is the decimal lldb wrote");
    check(editor::dbg_readStop(editor::DebuggerGdb,
                           "[Inferior 1 (process 41) exited normally]\n").status == 0,
          "and normally means nothing went wrong");

    // The variables, which each spells with the type in a different place.
    std::vector<editor::Variable> mine = editor::dbg_readVariables(
        editor::DebuggerLldb, "(int) total = 0\n(int) i = 1\n");
    check(mine.size() == 2, "lldb's variables are read");
    check(mine[0].name == "total" && mine[0].type == "int" && mine[0].value == "0",
          "with name, type and value apart");

    std::vector<editor::Variable> theirs = editor::dbg_readVariables(
        editor::DebuggerGdb, "total = 0\ni = 1\n");
    check(theirs.size() == 2 && theirs[1].name == "i" && theirs[1].value == "1",
          "and gdb's, which say no type");
    check(theirs[0].type.empty(), "so none is invented for them");

    check(editor::dbg_readVariables(editor::DebuggerGdb, "No symbol table info available.\n").empty(),
          "and a line that is not a variable is not read as one");

    // cdb, which answers a move with an address and has to be asked separately
    // where that is. This is what it actually printed for `ln`.
    editor::Stop cdb = editor::dbg_readStop(
        editor::DebuggerCdb,
        "0:000> C:\\Users\\me\\seam.cpp(10)+0x9\n"
        "(00007ff6`44e87160)   seam!main+0x27   |  (00007ff6`44e871c0)   seam!pre_c_init\n");
    check(cdb.stopped, "cdb's answer is read as a stop");
    check(cdb.file == "C:\\Users\\me\\seam.cpp", "with the whole Windows path, drive and all");
    check(cdb.line == 10, "and the line in brackets after it");
    check(cdb.function == "main", "and the function, without its module or its offset");

    // Its program ending is a break in ntdll rather than a message, and what
    // the program returned is in edx - printed in hex, whatever the radix.
    editor::Stop cdbEnd = editor::dbg_readStop(
        editor::DebuggerCdb,
        "ntdll!NtTerminateProcess+0x14:\n00007ffb`d6460904 c3   ret\n"
        "0:000> Last event: 8ec.1ff0: Exit process 0:8ec, code c\n");
    check(cdbEnd.exited && !cdbEnd.stopped, "cdb's ending is read as an ending");
    check(cdbEnd.status == 12, "and the code it gives is read as the hex it is");

    // Which thread it happens to break on when the program ends is not fixed,
    // so the ending must be recognised without depending on that at all.
    editor::Stop onAnother = editor::dbg_readStop(
        editor::DebuggerCdb,
        "ntdll!ZwWaitForWorkViaWorkerFactory+0x14:\n00007ffb`d6464034 c3   ret\n"
        "0:001> Last event: 8ec.1ff0: Exit process 0:8ec, code c\n");
    check(onAnother.exited && onAnother.status == 12,
          "including when it ends on a worker thread rather than the main one");

    std::vector<editor::Variable> cdbLocals = editor::dbg_readVariables(
        editor::DebuggerCdb, "0:000>               i = 0n1\n          total = 0n0\n");
    check(cdbLocals.size() == 2, "cdb's variables are read");
    check(cdbLocals[0].name == "i" && cdbLocals[0].value == "1",
          "with the 0n it puts in front of a decimal taken off again");

    // dv /t /V, as cdb 10.0 printed it on the Windows box: address, where, type, name, value.
    std::vector<editor::Variable> placed = editor::dbg_readVariables(
        editor::DebuggerCdb,
        "0000006b`f46ff950 @rsp+0x0020       int k = 0n8\n"
        "0000006b`f46ff958 @rsp+0x0028       struct pt * p = 0x0000006b`f46ff978\n");
    check(placed.size() == 2 && placed[0].name == "k" && placed[0].value == "8" &&
              placed[0].type == "int" && placed[0].address == "0x0000006bf46ff950",
          "cdb's dv /t /V gives each variable its type and address");
    check(placed.size() == 2 && placed[1].name == "p" && placed[1].type == "struct pt *",
          "a type with spaces in it stays whole");
    std::vector<editor::Variable> located = editor::dbg_readVariables(
        editor::DebuggerLldb, "0x000000016fdff0ec: (int) x = 5\n");
    check(located.size() == 1 && located[0].address == "0x000000016fdff0ec" &&
              located[0].type == "int" && located[0].value == "5",
          "lldb's frame variable -L gives the address in front");
    check(editor::dbg_addressIn("int * 0x0000006b`f46ff930") == "0x0000006bf46ff930" &&
              editor::dbg_addressIn("No address for operator&").empty(),
          "an address is read out of an answer, and none out of a refusal");
    check(editor::dbg_readType(editor::DebuggerCdb, "0:000> ?? total\nint 0n12\n") == "int" &&
              editor::dbg_readType(editor::DebuggerGdb, "(gdb) type = struct pt *\n") == "struct pt *" &&
              editor::dbg_readType(editor::DebuggerLldb, "(double) $0 = 4.5\n") == "double",
          "a watch's type is read from each engine's answer");

    // Both print their prompt and then, on the same line, the first line of the
    // answer. Left on, it is read as part of the name - which showed up as the
    // first variable of every gdb listing being missing and nothing else.
    std::vector<editor::Variable> prompted =
        editor::dbg_readVariables(editor::DebuggerGdb, "(gdb) i = 1\ntotal = 0\n");
    check(prompted.size() == 2 && prompted[0].name == "i",
          "a prompt in front of the first variable is not part of its name");

    // Stepping out, where gdb says what it is leaving before it says where it
    // arrived, and names the address because it did not land on a line start.
    editor::Stop out = editor::dbg_readStop(
        editor::DebuggerGdb,
        "(gdb) Run till exit from #0  twice (n=1) at s.c:3\n"
        "0x00000000004011b3 in main () at s.c:11\n"
        "11\t        total = total + twice(i);\n"
        "Value returned is $1 = 2\n");
    check(out.function == "main" && out.line == 11,
          "stepping out reports where it came back to, not what it left");

    editor::Stop afterPrompt = editor::dbg_readStop(
        editor::DebuggerGdb, "(gdb) twice (n=1) at s.c:3\n3\t    int doubled = n * 2;\n");
    check(afterPrompt.stopped && afterPrompt.function == "twice" && afterPrompt.line == 3,
          "nor of the function it stopped in");

    // A step that returns into the caller, which reads no differently from any
    // other stop - the name has no brackets after it because main takes no
    // arguments, and that was never the difficulty.
    editor::Stop caller = editor::dbg_readStop(editor::DebuggerLldb, kLldbStepIntoTheCaller);
    checkEqual(caller.function, "main", "lldb: a step back into the caller names it");
    checkEqual(caller.file, "main.c", "lldb: and the file the caller is in");
    check(caller.line == 7, "lldb: and the line the call was made from");

    editor::Stop callerGdb = editor::dbg_readStop(editor::DebuggerGdb, kGdbStepIntoTheCaller);
    checkEqual(callerGdb.function, "main", "gdb: the same, in its own words");
    check(callerGdb.line == 8, "gdb: and the line it carried on at");
}

// A step that did not go anywhere, which only lldb does: the transcripts above are one `next`
// after another, and the first two land back on the line they started on. Checked here so it
// needs no debugger - it is why F7 had to be pressed three times on a Mac to leave a function and once on the Linux box.
void aStepThatWentNowhere() {
    std::printf("a step that did not go anywhere\n");

    editor::Stop atBreak = editor::dbg_readStop(editor::DebuggerLldb, kLldbStopAtBreak);
    editor::Stop still = editor::dbg_readStop(editor::DebuggerLldb, kLldbStepStillThere);
    editor::Stop stillAgain = editor::dbg_readStop(editor::DebuggerLldb, kLldbStepStillThereAgain);
    editor::Stop arrived = editor::dbg_readStop(editor::DebuggerLldb, kLldbStepIntoTheCaller);

    check(editor::dbg_wentNowhere(editor::DebuggerLldb, atBreak, still),
          "the same line at a later address is a step that went nowhere");
    check(editor::dbg_wentNowhere(editor::DebuggerLldb, still, stillAgain),
          "and so is the one after it");
    check(!editor::dbg_wentNowhere(editor::DebuggerLldb, stillAgain, arrived),
          "but arriving in another function is not");

    // The other direction, which would be a step repeated until the program
    // ran out: gdb answers once and its answer must never be asked again.
    editor::Stop gdbBreak = editor::dbg_readStop(editor::DebuggerGdb, kGdbStopTwoArgs);
    check(!editor::dbg_wentNowhere(editor::DebuggerGdb, gdbBreak, gdbBreak),
          "gdb is never asked again, even for the very same stop");
    check(!editor::dbg_wentNowhere(editor::DebuggerCdb, atBreak, still),
          "nor is cdb, on what is not cdb's");

    // cdb's own, from a Shalimar program on the Windows box (2026-10-13): stopped on line 8, `t` steps over
    // the shm_ helper the line calls and stops further on in it, and the next `t` is in twice.
    const char* cdbAtBreak = "Breakpoint 0 hit\n>    8:   int b : twice(a)\nsteps!main+0x41:\n00007ff6`57691111 c744242001000000 mov dword ptr [rsp+20h],1\n"
                             "C:\\w\\steps.shl(8)\n(00007ff6`576910d0)   steps!main+0x41   |  (00007ff6`57691170)   steps!other\n";
    const char* cdbStill = ">    8:   int b : twice(a)\nsteps!main+0x54:\n00007ff6`57691124 8b442420        mov     eax,dword ptr [rsp+20h]\n"
                           "C:\\w\\steps.shl(8)+0x13\n(00007ff6`576910d0)   steps!main+0x54   |  (00007ff6`57691170)   steps!other\n";
    const char* cdbInside = ">    1: fun <int> = twice(n: int) {\nsteps!twice:\n00007ff6`57691040 55              push    rbp\n"
                            "C:\\w\\steps.shl(1)\n(00007ff6`57691040)   steps!twice   |  (00007ff6`576910d0)   steps!main\n";
    editor::Stop cBreak = editor::dbg_readStop(editor::DebuggerCdb, cdbAtBreak);
    editor::Stop cStill = editor::dbg_readStop(editor::DebuggerCdb, cdbStill);
    editor::Stop cInside = editor::dbg_readStop(editor::DebuggerCdb, cdbInside);
    check(cStill.line == 8 && cStill.function == "main", "cdb's stop further on in line 8 is read as main, line 8");
    check(editor::dbg_wentNowhere(editor::DebuggerCdb, cBreak, cStill),
          "and for cdb too the same line at a later address is a step that went nowhere");
    check(!editor::dbg_wentNowhere(editor::DebuggerCdb, cStill, cInside),
          "but cdb arriving in the callee is not");
    check(!editor::dbg_wentNowhere(editor::DebuggerCdb, cStill, cBreak),
          "nor is a stop on a breakpoint");

    // Recursion on one line: the same file, the same line and the same
    // function every time, and only some of them a move. Nothing here is a
    // breakpoint stop, so the address is the only thing that can decide.
    editor::Stop first = editor::dbg_readStop(editor::DebuggerLldb, kLldbInFactFirst);
    editor::Stop again = editor::dbg_readStop(editor::DebuggerLldb, kLldbInFactAgain);
    editor::Stop shuffled = editor::dbg_readStop(editor::DebuggerLldb, kLldbInFactShuffled);
    editor::Stop deeper = editor::dbg_readStop(editor::DebuggerLldb, kLldbInFactDeeper);

    check(editor::dbg_wentNowhere(editor::DebuggerLldb, again, shuffled),
          "the same line further along is still a step that went nowhere");
    check(!editor::dbg_wentNowhere(editor::DebuggerLldb, shuffled, deeper),
          "stepping into a recursive call on the same line is an arrival");
    check(!editor::dbg_wentNowhere(editor::DebuggerLldb, first, again),
          "and so is one that arrives at the address it started from");

    // And a breakpoint is a stop whatever else is true of it.
    check(!editor::dbg_wentNowhere(editor::DebuggerLldb, atBreak, atBreak),
          "a breakpoint is never stepped past");

    // A stop that could not be read is a debugger to report, not one to ask
    // again - see the loop in Debugger::afterStepping, which would otherwise
    // sit there saying `next` to something that has stopped answering.
    check(!editor::dbg_wentNowhere(editor::DebuggerLldb, atBreak, editor::Stop()),
          "and a stop that was not read at all is not a step to repeat");
}

// The call stack, in the three spellings it comes in. These are transcripts each of them actually
// printed, prompts and all, rather than tidied versions of them - the prompt on the front of the
// first line is exactly the sort of thing that goes wrong.
void whatACallStackLooksLike() {
    std::printf("who called what the program is standing in\n");

    std::vector<editor::StackFrame> lldb = editor::dbg_readFrames(
        editor::DebuggerLldb,
        "(lldb) thread backtrace\n"
        "* thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.1\n"
        "  * frame #0: 0x00000001000002f8 stepped`twice(n=1) at stepped.c:3:5\n"
        "    frame #1: 0x00000001000003f8 stepped`main at stepped.c:11:9\n"
        "    frame #2: 0x000000018b6584e4 dyld`start + 6992\n");
    check(lldb.size() == 2, "lldb: the stack is read, and stops at main");
    checkEqual(lldb[0].function, "twice", "lldb: standing in the one it stopped in");
    check(lldb[0].line == 3, "lldb: on the line it stopped on, not the column after it");
    checkEqual(lldb[1].function, "main", "lldb: called from the one above it");
    checkEqual(lldb[1].file, "stepped.c", "lldb: which names its own file");
    check(lldb[1].line == 11, "lldb: and the line that is waiting for the call");

    std::vector<editor::StackFrame> gdb = editor::dbg_readFrames(
        editor::DebuggerGdb,
        "(gdb) #0  twice (n=1) at stepped.c:3\n"
        "#1  0x00000000004011b3 in main () at stepped.c:11\n"
        "#2  0x00007ffff7829d90 in __libc_start_call_main () at ../sysdeps/nptl/libc_start.c:58\n"
        "#3  0x00007ffff7829e40 in __libc_start_main_impl () at ../csu/libc-start.c:392\n"
        "#4  0x0000000000401085 in _start ()\n");
    check(gdb.size() == 2, "gdb: the same stack, and libc is not part of it");
    checkEqual(gdb[0].function, "twice", "gdb: past the prompt on the first frame");
    checkEqual(gdb[1].function, "main", "gdb: and past the address in front of the second");
    check(gdb[1].line == 11, "gdb: with the line it is waiting on");

    // cdb prints a table, the source in brackets at the end of each row because .lines -e was asked
    // for at start. This is what it printed, and the shape is the point: `k` numbers no frames, an
    // inlined call has dashes where the stack pointer would be, and the heading and CRT frames name no source.
    std::vector<editor::StackFrame> cdb = editor::dbg_readFrames(
        editor::DebuggerCdb,
        "0:000> k\n"
        "Child-SP          RetAddr               Call Site\n"
        "000000f5`5e4ffbd8 00007ff6`9bb77190     counted!twice+0x4 "
        "[C:\\Users\\me\\counted.cpp @ 3]\n"
        "000000f5`5e4ffbe0 00007ff6`9bb773fc     counted!main+0x30 "
        "[C:\\Users\\me\\counted.cpp @ 10]\n"
        "(Inline Function) --------`--------     counted!invoke_main+0x22 "
        "[D:\\a\\_work\\1\\s\\src\\vctools\\crt\\vcstartup\\src\\startup\\exe_common.inl @ 78]\n"
        "000000f5`5e4ffc60 00007ffb`d63aad6c     KERNEL32!BaseThreadInitThunk+0x17\n"
        "000000f5`5e4ffc90 00000000`00000000     ntdll!RtlUserThreadStart+0x2c\n");
    check(cdb.size() == 2, "cdb: its table is read, heading and all");
    checkEqual(cdb[0].function, "twice", "cdb: without the module in front or the offset after");
    checkEqual(cdb[1].file, "C:\\Users\\me\\counted.cpp", "cdb: the whole Windows path, drive and all");
    check(cdb[1].line == 10, "cdb: and the line after its at-sign");

    // An inlined call is a frame like any other, and is read from the same row
    // that has dashes where every other row has a stack pointer.
    std::vector<editor::StackFrame> inlined = editor::dbg_readFrames(
        editor::DebuggerCdb,
        "(Inline Function) --------`--------     counted!doubled+0x11 "
        "[C:\\Users\\me\\counted.cpp @ 4]\n"
        "000000f5`5e4ffbe0 00007ff6`9bb773fc     counted!main+0x30 "
        "[C:\\Users\\me\\counted.cpp @ 10]\n");
    check(inlined.size() == 2 && inlined[0].function == "doubled",
          "cdb: a call that was inlined is still a frame");

    // How a frame is written in the tab, and read back off it. The two front
    // ends compose that tab separately, so the line is written by one function
    // and matched by another rather than by either of them counting rows.
    editor::StackFrame one;
    one.function = "main";
    one.file = "/home/me/work/stepped.c";
    one.line = 11;
    checkEqual(editor::dbg_frameLine(one), "  main   stepped.c:11",
               "a frame is written with its file's name, not its whole path");

    std::vector<editor::StackFrame> two;
    editor::StackFrame inner;
    inner.function = "twice";
    inner.file = "/home/me/work/stepped.c";
    inner.line = 3;
    two.push_back(inner);
    two.push_back(one);
    check(editor::dbg_frameOnLine(two, editor::dbg_frameLine(one)) == 1,
          "and the line it wrote is read back as that frame");
    check(editor::dbg_frameOnLine(two, "  main   stepped.c:11  ") == 1,
          "with what the tab pads it with taken off");

    // The frame being looked at wears a mark, and is still that frame when the
    // line is read back - which is what lets enter on it be pressed twice.
    checkEqual(editor::dbg_frameLine(one, true), "> main   stepped.c:11",
               "the frame being looked at is written with a mark");
    check(editor::dbg_frameOnLine(two, editor::dbg_frameLine(one, true)) == 1,
          "and is read back as the frame it marks");
    checkEqual(editor::dbg_lookingAt(one), "the variables are main's, at stepped.c:11",
               "and the tab says whose variables it is showing");

    // A variable is written and read back the same way a frame is, for the
    // same reason: enter on one of those lines is how it is set.
    editor::Variable counted;
    counted.name = "total";
    counted.value = "0";
    counted.type = "int";
    checkEqual(editor::dbg_variableLine(counted), "  total = 0   [int]",
               "a variable is written with its type after it");

    editor::Variable untyped;
    untyped.name = "i";
    untyped.value = "1";
    checkEqual(editor::dbg_variableLine(untyped), "  i = 1",
               "and without one where the debugger gave none");

    std::vector<editor::Variable> inScope;
    inScope.push_back(counted);
    inScope.push_back(untyped);
    check(editor::dbg_variableOnLine(inScope, editor::dbg_variableLine(untyped)) == 1,
          "and the line it wrote is read back as that variable");
    check(editor::dbg_variableOnLine(inScope, "called from") == inScope.size(),
          "while a line that is not a variable is not read as one");
    check(editor::dbg_variableOnLine(inScope, editor::dbg_frameLine(one)) == inScope.size(),
          "and neither is a frame, which the same tab is full of");

    // The value out of an answer, in the three spellings. These are what they printed: lldb echoes
    // the command over a pipe and puts its caret line above the words, which is why the reader
    // looks for the $ and not for the first line with an = in it.
    checkEqual(editor::dbg_readValue(editor::DebuggerLldb,
                                     "(lldb) expression total + i\n(int) $0 = 1\n"),
               "1", "lldb: the value is read past its own echo of the command");
    checkEqual(editor::dbg_readValue(editor::DebuggerGdb, "(gdb) $1 = 12\n"),
               "12", "gdb: and past its prompt");
    checkEqual(editor::dbg_readValue(editor::DebuggerCdb, "0:000> ?? total\nint 0n12\n"),
               "12", "cdb: with the 0n it puts in front of a decimal taken off");
    check(editor::dbg_readValue(editor::DebuggerLldb,
                                "(lldb) expression nosuch\n                  ^\n"
                                "                  error: use of undeclared identifier\n")
              .empty(),
          "and a complaint holds no value at all");

    // A watch that could not be answered shows what the debugger said, in
    // brackets, so the tab never has an expression with nothing after it.
    editor::Watch answered;
    answered.expression = "total + i";
    answered.value = "1";
    answered.ok = true;
    checkEqual(editor::dbg_watchLine(answered), "  total + i = 1", "a watch is written with its value");

    editor::Watch refused;
    refused.expression = "nosuch";
    refused.value = "use of undeclared identifier 'nosuch'";
    checkEqual(editor::dbg_watchLine(refused), "  nosuch = [use of undeclared identifier 'nosuch']",
               "and one that could not be answered says why, in brackets");

    std::vector<editor::Watch> watching;
    watching.push_back(answered);
    watching.push_back(refused);
    check(editor::dbg_watchOnLine(watching, editor::dbg_watchLine(refused)) == 1,
          "and either line is read back as the watch it was written for");
    check(editor::dbg_watchOnLine(watching, "  total = 0   [int]") == watching.size(),
          "while a variable is not read as a watch");

    // The tab's first line names frame 0, and is written here because pressing
    // enter on it is how either front end goes back to the stop.
    checkEqual(editor::dbg_stopLine("/home/me/work/stepped.c", 3, "twice"),
               "stopped at stepped.c:3 in twice", "the tab's top line names where it stopped");
    checkEqual(editor::dbg_stopLine("/home/me/work/stepped.c", 3, ""),
               "stopped at stepped.c:3",
               "and says only where when the debugger named no function");
    check(editor::dbg_frameOnLine(two, "  total = 0   [int]") == two.size(),
          "a line that is not a frame is not read as one");
    check(editor::dbg_frameOnLine(two, "called from") == two.size(),
          "and neither is the heading over them");

    // A function that called itself writes the same line twice. The first of
    // them is answered, which is right for going there: both name one place.
    std::vector<editor::StackFrame> again;
    again.push_back(inner);
    again.push_back(inner);
    check(editor::dbg_frameOnLine(again, editor::dbg_frameLine(inner)) == 0,
          "a recursive call is two frames on one line, and the first answers");

    // A stack with nothing above it is one frame, and the editor says nothing
    // about it: standing in main having been called by nobody is the ordinary
    // case, and a "called from" with one name under it is noise.
    std::vector<editor::StackFrame> alone = editor::dbg_readFrames(
        editor::DebuggerLldb,
        "  * frame #0: 0x000000010000038c stepped`main at stepped.c:9:5\n"
        "    frame #1: 0x000000018b6584e4 dyld`start + 6992\n");
    check(alone.size() == 1, "a program standing in main has a stack of one");
}

// The whole conversation, against a program the machine's *C++* compiler built - cl with cdb on
// Windows, clang++ with lldb on a Mac, g++ with gdb on Linux - needing a debugger and a compiler, so
// it says when it skips. cc1's Windows target is MASM with no line table, and what that hid is that cl's .pdb and cdb work there; the marker rename had to be driven by hand to prove it.
void stoppingTheHostsOwnCompiler() {
    std::printf("stopping what the machine's own C++ compiler built\n");

    editor::Toolchain tool;
    editor::ToolchainKind kind = editor::hostCppToolchain();
    editor::DebuggerKind which = editor::dbg_for(kind, editor::hostArch());
    if (which == editor::DebuggerNone) {
        skipped("no debugger for %s here", editor::toolchainName(kind));
        return;
    }

    std::string dir = editor::path::join(editor::path::tempDir(), "ride-hostcpp-debug");
    editor::path::removeTree(dir);
    editor::path::makeDirectories(dir);

    std::string source = editor::path::join(dir, "stepped.cpp");
    writeSource(source,
              "static int twice(int n)\n"
              "{\n"
              "    int doubled = n * 2;\n"
              "    return doubled;\n"
              "}\n"
              "\n"
              "int main()\n"
              "{\n"
              "    int total = 0;\n"
              "    for (int i = 1; i <= 3; ++i) {\n"
              "        total = total + twice(i);\n"
              "    }\n"
              "    return total;\n"
              "}\n");

    // Built the way the editor builds it, so the flags that carry the debug
    // information are the editor's own rather than this test's idea of them.
    editor::prepareFor(kind, editor::ConfigDebug);
    editor::Recipe recipe = editor::programRecipe(tool, kind, source, editor::LangCpp,
                                                  editor::hostArch(), editor::ConfigDebug);
    if (std::system(shellCommand(recipe.command + kNowhere).c_str()) != 0 ||
        !editor::path::exists(recipe.assemblyPath)) {
        skipped("%s built nothing to debug", editor::toolchainName(kind));
        editor::path::removeTree(dir);
        return;
    }

    editor::Debugger debugger;
    check(debugger.start(which, recipe.assemblyPath),
          "the debugger starts on what the host's C++ compiler built");
    if (!debugger.running()) {
        editor::path::removeTree(dir);
        for (size_t i = 0; i < recipe.leftovers.size(); ++i)
            editor::path::remove(recipe.leftovers[i]);
        return;
    }

    check(debugger.breakAt(source, 11), "a breakpoint is set on a line of C++");

    editor::Stop at = debugger.run();
    check(at.stopped, "and running stops on it");
    check(at.line == 11, "on the line it was asked for");
    check(at.function == "main", "in the function that line is in");

    editor::Stop into = debugger.stepInto();
    check(into.stopped && into.function == "twice", "stepping into the call arrives inside it");

    std::vector<editor::StackFrame> stack = debugger.frames();
    bool sawCaller = false;
    for (size_t i = 0; i < stack.size(); ++i)
        if (stack[i].function == "main") sawCaller = true;
    check(sawCaller, "and the stack says which function it was called from");

    debugger.stop();
    check(!debugger.running(), "and it stops when it is told to");

    // Through removeProgram rather than by hand, so that the .dSYM a debug
    // build leaves beside the program on a Mac goes with it.
    editor::Built asBuilt;
    asBuilt.program = recipe.assemblyPath;
    asBuilt.leftovers = recipe.leftovers;
    editor::removeProgram(asBuilt);
    editor::path::removeTree(dir);
}

// The C6747 emulator as a debugger, for real: cpp11 -g's assembly, vm6747 --debug started by Debugger, and
// the stops, steps and frames read back through the gdb parser. $CXX1 and $VM6747 name the two; a cpp11
// that still refuses -g for tms6747 is a skip that says so.
// The same on a C program: c90 -g's .dwpsn, the Debug session on vm6747 and, with the image linked, on
// sim6747 - one session each way, driven by $CC1 beside the tools debuggingOnTheEmulator names.
void debuggingCOnTheEmulator() {
    std::printf("stopping and stepping C on the C6747 emulator and simulator, for real\n");

    const char* cc1 = std::getenv("CC1");
    const char* vm = std::getenv("VM6747");
    if (!cc1 || !*cc1 || !editor::path::exists(cc1)) { skipped("no $CC1, so no C is built for the emulator"); return; }
    if (!vm || !*vm || !editor::path::exists(vm)) { skipped("no $VM6747, so there is no emulator to stop"); return; }

    std::string dir = editor::path::join(editor::path::tempDir(), "ride-c6x-c-debug-test");
    editor::path::removeTree(dir);
    editor::path::makeDirectories(dir);
    std::string source = editor::path::join(dir, "stepped.c");
    writeSource(source,
              "static int twice(int n)\n"
              "{\n"
              "    int doubled = n * 2;\n"
              "    return doubled;\n"
              "}\n"
              "\n"
              "int main(void)\n"
              "{\n"
              "    int total = 0;\n"
              "    for (int i = 1; i <= 3; ++i) {\n"
              "        total = total + twice(i);\n"
              "    }\n"
              "    return total;\n"
              "}\n");
    std::string program = editor::path::join(dir, "stepped.s");
    std::string build = "\"" + std::string(cc1) + "\" -S -arch tms6747 -g \"" + source + "\" -o \"" + program + "\"" + kNowhere;
    if (std::system(shellCommand(build).c_str()) != 0 || !editor::path::exists(program)) {
        skipped("c90 built nothing with -g for tms6747 (a c90 before .dwpsn, 2026-10-13)");
        editor::path::removeTree(dir);
        return;
    }

    check(editor::dbg_for(editor::ToolCc1, "tms6747") == editor::DebuggerC6x, "C on tms6747 with a vm6747 named is the C6x kind");
    editor::Debugger debugger;
    check(debugger.start(editor::DebuggerC6x, program), "vm6747 --debug starts on what c90 built");
    if (!debugger.running()) { editor::path::removeTree(dir); return; }
    check(debugger.breakAt(source, 11), "a breakpoint is set on a line of C");
    editor::Stop at = debugger.run();
    check(at.stopped && at.line == 11 && at.function == "main", "C: running stops on it, in main");
    editor::Stop into = debugger.stepInto();
    check(into.stopped && into.line == 3 && into.function == "twice", "C: stepping into the call arrives on its first statement, in twice");
    editor::Stop out = debugger.stepOut();
    check(out.stopped && out.function == "main", "C: finishing the call lands back in main");
    checkEqual(debugger.backend(), "vm6747", "C: with no .out beside the assembly the emulator was the backend");
    debugger.stop();

    const char* asm6x = std::getenv("ASM6X");
    const char* lnk6x = std::getenv("LNK6X");
    const char* tirts = std::getenv("TIRTS");
    const char* sim = std::getenv("SIM6747");
    if (!asm6x || !*asm6x || !lnk6x || !*lnk6x || !tirts || !*tirts || !sim || !*sim ||
        !editor::path::exists(asm6x) || !editor::path::exists(lnk6x) || !editor::path::exists(sim)) {
        skipped("no $ASM6X, $LNK6X, $TIRTS and $SIM6747, so no C image is linked to choose sim6747 by");
        editor::path::removeTree(dir);
        return;
    }
    std::string cmd = editor::path::join(dir, "link.cmd");
    writeSource(cmd, "--rom_model\n--stack_size=0x100000\n--heap_size=0x100000\n"
                     "MEMORY { RAM : origin = 0xC0000000, length = 0x04000000 }\nSECTIONS\n{\n"
                     "    .text > RAM  .const > RAM  .data > RAM  .bss > RAM  .far > RAM  .fardata > RAM\n"
                     "    .neardata > RAM  .rodata > RAM  .cinit > RAM  .init_array > RAM  .switch > RAM\n"
                     "    .cio > RAM  .stack > RAM  .sysmem > RAM  .vm6747.eh > RAM\n}\n");
    std::string obj = editor::path::join(dir, "stepped.obj"), image = editor::path::join(dir, "stepped.out");
    std::string link = "\"" + std::string(asm6x) + "\" \"" + program + "\" -o \"" + obj + "\" && \"" + std::string(lnk6x) +
                       "\" -mv6740 --abi=eabi -i \"" + tirts + "\" \"" + cmd + "\" \"" + obj + "\" -l rts6x.lib -o \"" + image + "\"" + kNowhere;
    if (std::system(shellCommand(link).c_str()) != 0 || !editor::path::exists(image)) {
        skipped("asm6x and lnk6x made no C image to choose sim6747 by");
        editor::path::removeTree(dir);
        return;
    }
    std::string why;
    check(editor::dbg_imageHasLines(image, &why), "the C image carries a .debug_line with rows in the program's code: " + why);
    checkEqual(editor::dbg_c6xBackend(program, &why), "sim6747", "and with it beside the assembly the simulator is chosen for C");
    editor::Debugger onImage;
    check(onImage.start(editor::DebuggerC6x, program), "sim6747 --run --debug starts on the C image");
    checkEqual(onImage.backend(), "sim6747", "and the session says so");
    if (onImage.running()) {
        check(onImage.breakAt(source, 11), "a breakpoint on a line of C, in the image");
        editor::Stop there = onImage.run();
        check(there.stopped && there.line == 11 && there.function == "main", "and the simulator stops on it, in main");
        editor::Stop in = onImage.stepInto();
        check(in.stopped && in.line == 3, "stepping into the call lands on its first statement, on the C image too");
        onImage.stop();
    }
    editor::path::removeTree(dir);
}

void debuggingOnTheEmulator() {
    std::printf("stopping and stepping on the C6747 emulator, for real\n");

    const char* cxx1 = std::getenv("CXX1");
    const char* vm = std::getenv("VM6747");
    if (!cxx1 || !*cxx1 || !editor::path::exists(cxx1)) { skipped("no $CXX1, so nothing is built for the emulator"); return; }
    if (!vm || !*vm || !editor::path::exists(vm)) { skipped("no $VM6747, so there is no emulator to stop"); return; }

    std::string dir = editor::path::join(editor::path::tempDir(), "ride-c6x-debug-test");
    editor::path::removeTree(dir);
    editor::path::makeDirectories(dir);
    std::string source = editor::path::join(dir, "stepped.cpp");
    writeSource(source,
              "static int twice(int n)\n"
              "{\n"
              "    int doubled = n * 2;\n"
              "    return doubled;\n"
              "}\n"
              "\n"
              "int main()\n"
              "{\n"
              "    int total = 0;\n"
              "    for (int i = 1; i <= 3; ++i) {\n"
              "        total = total + twice(i);\n"
              "    }\n"
              "    return total;\n"
              "}\n");
    std::string program = editor::path::join(dir, "stepped.s");
    std::string build = "\"" + std::string(cxx1) + "\" -S -arch tms6747 -g -nologo \"" + source + "\" -o \"" + program + "\"" + kNowhere;
    if (std::system(shellCommand(build).c_str()) != 0 || !editor::path::exists(program)) {
        skipped("cpp11 built nothing with -g for tms6747 (a cpp11 before .dwpsn, 2026-10-13)");
        editor::path::removeTree(dir);
        return;
    }

    editor::Debugger debugger;
    check(editor::dbg_for(editor::ToolCxx1, "tms6747") == editor::DebuggerC6x, "tms6747 with a vm6747 named is the C6x kind");
    check(debugger.start(editor::DebuggerC6x, program), "vm6747 --debug starts on what cpp11 built");
    if (!debugger.running()) { editor::path::removeTree(dir); return; }

    check(debugger.breakAt(source, 11), "a breakpoint is set on a line of C++");
    editor::Stop at = debugger.run();
    check(at.stopped, "and running stops on it");
    check(at.line == 11, "on the line it was asked for");
    check(at.function == "main", "in the function that line is in");

    editor::Stop into = debugger.stepInto();
    check(into.stopped && into.line == 3, "stepping into the call arrives on its first statement");
    check(into.function == "_ZL5twicei", "in the callee, named as its label is");
    std::vector<editor::StackFrame> stack = debugger.frames();
    check(stack.size() == 2 && stack[1].function == "main" && stack[1].line == 11, "and the stack says main called it");

    editor::Stop out = debugger.stepOut();
    check(out.stopped && out.function == "main", "finishing the call lands back in main");
    editor::Stop over = debugger.stepOver();
    check(over.stopped && over.line == 10, "a step over goes to the loop's next turn");

    bool ok = false;
    std::string b15 = debugger.evaluate("$B15", &ok);
    check(ok && !b15.empty(), "a register reads");
    check(debugger.locals().empty(), "and there are no locals to show: the assembly carries none");

    debugger.clearBreakpoints();
    editor::Stop done = debugger.resume();
    check(done.exited && done.status == 12, "running on, the program ends with main's return");
    checkEqual(debugger.backend(), "vm6747", "with no .out beside the assembly the emulator was the backend");
    debugger.stop();

    // The choice between the two (dbg_c6xBackend): with no image, the emulator and the reason; with an image
    // the build left carrying the program's lines, the simulator; forced either way by the environment; and
    // an image built without -g sends the session back to the emulator, saying the image has no lines.
    std::string why;
    checkEqual(editor::dbg_c6xBackend(program, &why), "vm6747", "no .out: the emulator");
    check(why.find("no stepped.out") != std::string::npos || why.find("no sim6747") != std::string::npos, "and the reason names what is missing: " + why);
    const char* asm6x = std::getenv("ASM6X");
    const char* lnk6x = std::getenv("LNK6X");
    const char* tirts = std::getenv("TIRTS");
    const char* sim = std::getenv("SIM6747");
    if (!asm6x || !*asm6x || !lnk6x || !*lnk6x || !tirts || !*tirts || !sim || !*sim ||
        !editor::path::exists(asm6x) || !editor::path::exists(lnk6x) || !editor::path::exists(sim)) {
        skipped("no $ASM6X, $LNK6X, $TIRTS and $SIM6747, so no image is linked to choose sim6747 by");
        editor::path::removeTree(dir);
        return;
    }
    std::string cmd = editor::path::join(dir, "link.cmd");
    writeSource(cmd, "--rom_model\n--stack_size=0x100000\n--heap_size=0x100000\n"
                     "MEMORY { RAM : origin = 0xC0000000, length = 0x04000000 }\nSECTIONS\n{\n"
                     "    .text > RAM  .const > RAM  .data > RAM  .bss > RAM  .far > RAM  .fardata > RAM\n"
                     "    .neardata > RAM  .rodata > RAM  .cinit > RAM  .init_array > RAM  .switch > RAM\n"
                     "    .cio > RAM  .stack > RAM  .sysmem > RAM  .vm6747.eh > RAM\n}\n");
    std::string obj = editor::path::join(dir, "stepped.obj"), image = editor::path::join(dir, "stepped.out");
    std::string link = "\"" + std::string(asm6x) + "\" \"" + program + "\" -o \"" + obj + "\" && \"" + std::string(lnk6x) +
                       "\" -mv6740 --abi=eabi -i \"" + tirts + "\" \"" + cmd + "\" \"" + obj + "\" -l rts6x.lib -o \"" + image + "\"" + kNowhere;
    if (std::system(shellCommand(link).c_str()) != 0 || !editor::path::exists(image)) {
        skipped("asm6x and lnk6x made no stepped.out to choose sim6747 by");
        editor::path::removeTree(dir);
        return;
    }
    check(editor::dbg_imageHasLines(image, &why), "the -g image carries a .debug_line with rows in the program's code: " + why);
    checkEqual(editor::dbg_c6xBackend(program, &why), "sim6747", "and with it beside the assembly the simulator is chosen");

    editor::Debugger onImage;
    check(onImage.start(editor::DebuggerC6x, program), "sim6747 --run --debug starts on the image");
    checkEqual(onImage.backend(), "sim6747", "and the session says so");
    if (onImage.running()) {
        check(onImage.breakAt(source, 11), "a breakpoint on a line of C++, in the image");
        editor::Stop there = onImage.run();
        check(there.stopped && there.line == 11 && there.function == "main", "and the simulator stops on it, in main");
        editor::Stop in = onImage.stepInto();
        check(in.stopped && in.line == 3, "stepping into the call lands on its first statement, on the image too");
        onImage.stop();
    }

#ifdef _WIN32
    _putenv_s("RIDE_DEBUG_VM6747", "1");
#else
    setenv("RIDE_DEBUG_VM6747", "1", 1);
#endif
    checkEqual(editor::dbg_c6xBackend(program, &why), "vm6747", "RIDE_DEBUG_VM6747 forces the emulator");
    check(why.find("RIDE_DEBUG_VM6747") != std::string::npos, "and says so");
#ifdef _WIN32
    _putenv_s("RIDE_DEBUG_VM6747", "");
#else
    unsetenv("RIDE_DEBUG_VM6747");
#endif

    // Without -g: the same chain, no .dwpsn, no .debug_line - the emulator, for a reason that names it.
    std::string plain = editor::path::join(dir, "plain.s"), plainObj = editor::path::join(dir, "plain.obj"), plainOut = editor::path::join(dir, "plain.out");
    std::string again = "\"" + std::string(cxx1) + "\" -S -arch tms6747 -nologo \"" + source + "\" -o \"" + plain + "\" && \"" + std::string(asm6x) + "\" \"" + plain +
                        "\" -o \"" + plainObj + "\" && \"" + std::string(lnk6x) + "\" -mv6740 --abi=eabi -i \"" + tirts + "\" \"" + cmd + "\" \"" + plainObj + "\" -l rts6x.lib -o \"" + plainOut + "\"" + kNowhere;
    if (std::system(shellCommand(again).c_str()) == 0 && editor::path::exists(plainOut)) {
        check(!editor::dbg_imageHasLines(plainOut, &why) && why.find("no .debug_line") != std::string::npos, "an image built without -g has no line table: " + why);
        checkEqual(editor::dbg_c6xBackend(plain, &why), "vm6747", "so the emulator is chosen for it");
        check(why.find("no .debug_line") != std::string::npos, "and the reason is the missing line table");
    } else skipped("no plain image was linked for the fallback");
    editor::path::removeTree(dir);
}

void debuggingForReal() {
    std::printf("stopping, stepping and looking, for real\n");

    const char* cc1 = std::getenv("CC1");
    // Named but not there counts as not named, and the path is printed: a $CC1 with a ~ in it never
    // expands, and a build with an unfindable compiler fails in a way that reads as a broken editor
    // rather than as a path nobody resolved. That cost most of a day once.
    if (cc1 && *cc1 && !editor::path::exists(cc1)) {
        skipped("no cc1 at %s, so nothing is built to debug", cc1);
        return;
    }
    if (!cc1 || !*cc1) {
        skipped("no $CC1, so nothing is built to debug");
        return;
    }
    if (editor::dbg_here() == editor::DebuggerNone) {
        skippedNotHere("no gdb or lldb on this platform - cdb's cases are below");
        return;
    }

    std::string dir = editor::path::join(editor::path::tempDir(), "ride-debug-test");
    editor::path::removeTree(dir);
    editor::path::makeDirectories(dir);

    std::string source = editor::path::join(dir, "stepped.c");
    writeSource(source,
              "static int twice(int n)\n"
              "{\n"
              "    int doubled = n * 2;\n"
              "    return doubled;\n"
              "}\n"
              "\n"
              "int main(void)\n"
              "{\n"
              "    int total = 0;\n"
              "    for (int i = 1; i <= 3; ++i) {\n"
              "        total = total + twice(i);\n"
              "    }\n"
              "    return total;\n"
              "}\n");

    std::string program = editor::path::join(dir, "stepped");
    std::string build = "\"" + std::string(cc1) + "\" \"" + source + "\" -o \"" + program +
                        "\" -g" + kNowhere;
    if (std::system(shellCommand(build).c_str()) != 0 || !editor::path::exists(program)) {
        skipped("cc1 built nothing to debug");
        editor::path::removeTree(dir);
        return;
    }

    editor::Debugger debugger;
    check(debugger.start(editor::dbg_for(editor::ToolCc1, editor::hostArch()), program),
          "the debugger starts on what cc1 built");
    if (!debugger.running()) { editor::path::removeTree(dir); return; }

    check(debugger.breakAt(source, 11), "a breakpoint is set on a line of C");

    editor::Stop at = debugger.run();
    check(at.stopped, "and running stops on it");
    check(at.line == 11, "on the line it was asked for");
    check(at.function == "main", "in the function that line is in");

    // The variables are the point of the whole exercise: this is cc1's DWARF
    // being read back by somebody else's debugger.
    std::vector<editor::Variable> locals = debugger.locals();
    bool sawTotal = false, sawCounter = false;
    for (size_t i = 0; i < locals.size(); ++i) {
        if (locals[i].name == "total" && locals[i].value == "0") sawTotal = true;
        if (locals[i].name == "i" && locals[i].value == "1") sawCounter = true;
    }
    check(sawTotal, "the local it declared is there, with the value it has");
    check(sawCounter, "and so is the one the loop declared");

    editor::Stop into = debugger.stepInto();
    check(into.stopped && into.function == "twice", "stepping into a call arrives inside it");

    // And from in there the stack says how it got in, which is the whole of
    // what a call stack is for and cannot be seen from main.
    std::vector<editor::StackFrame> stack = debugger.frames();
    check(stack.size() == 2, "the stack inside a call is that call and what called it");
    if (stack.size() == 2) {
        checkEqual(stack[0].function, "twice", "standing in the function stepped into");
        checkEqual(stack[1].function, "main", "called from the one that called it");
        check(stack[1].line == 11, "on the line that is waiting for it to come back");
    }

    // And the caller's own variables, which is what selecting a frame is for:
    // total and i belong to main and are not in scope in twice at all.
    check(debugger.selectFrame(1), "the debugger goes to the frame that called it");
    std::vector<editor::Variable> caller = debugger.locals();
    bool sawCallersTotal = false, sawInner = false;
    for (size_t i = 0; i < caller.size(); ++i) {
        if (caller[i].name == "total") sawCallersTotal = true;
        if (caller[i].name == "doubled") sawInner = true;
    }
    check(sawCallersTotal, "and the variables read there are the caller's");
    check(!sawInner, "with nothing of the frame it was called from still in the list");

    check(debugger.selectFrame(0), "and it goes back to the frame it stopped in");
    std::vector<editor::Variable> back = debugger.locals();
    bool sawArgument = false;
    for (size_t i = 0; i < back.size(); ++i)
        if (back[i].name == "n") sawArgument = true;
    check(sawArgument, "where the argument is in scope again");

    editor::Stop out = debugger.stepOut();
    check(out.stopped && out.function == "main", "and stepping out comes back");

    editor::Stop again = debugger.resume();
    check(again.stopped && again.line == 11, "a breakpoint in a loop is hit again");

    // A watch, which is the same question asked again at every stop. What makes it a watch rather
    // than an answer is that nobody asks for it twice: the value below changes because the program
    // moved, and for no other reason. Standing on the second time round the loop, total 2 and i 2.
    debugger.addWatch("total + i");
    check(debugger.watches().size() == 1, "an expression can be watched");
    check(debugger.watches()[0].ok && debugger.watches()[0].value == "4",
          "and is answered where the program is standing - total 2 plus i 2");

    editor::Stop roundAgain = debugger.resume();
    check(roundAgain.stopped, "the program carries on to the next time round");
    check(debugger.watches()[0].value == "9",
          "and the watch has followed it without being asked - total 6 plus i 3");

    debugger.addWatch("nosuch + 1");
    check(!debugger.watches()[1].ok, "an expression it cannot answer is kept as a watch");
    check(!debugger.watches()[1].value.empty(), "with what it said about it for a value");

    debugger.removeWatch(1);
    check(debugger.watches().size() == 1, "and a watch can be taken away again");

    debugger.clearBreakpoints();
    editor::Stop ended = debugger.resume();
    check(ended.exited, "and with none left the program runs to the end");
    check(ended.status == 12, "returning what it worked out - 2 + 4 + 6");

    debugger.stop();
    check(!debugger.running(), "the debugger goes when it is told to");

    // Writing a variable back, on a second run of the same program - a run of its own so the one
    // above still measures what the program does when nothing has been written into it. What is
    // checked is what the program returned: a variable reading back 100 with no effect on the answer is a debugger showing its own idea of the program.
    editor::Debugger writing;
    check(writing.start(editor::dbg_for(editor::ToolCc1, editor::hostArch()), program),
          "the debugger starts again on the same program");
    if (writing.running()) {
        writing.breakAt(source, 11);
        check(writing.run().stopped, "and stops in the loop again");

        std::string said;
        check(writing.setVariable("total", "100", &said), "a variable is written back");
        std::vector<editor::Variable> after = writing.locals();
        bool now = false;
        for (size_t i = 0; i < after.size(); ++i)
            if (after[i].name == "total" && after[i].value == "100") now = true;
        check(now, "and reads back as what it was set to");

        check(!writing.setVariable("nosuch", "1", &said),
              "a name that is not in scope is refused");
        check(!said.empty(), "with the debugger's own words for why");

        writing.clearBreakpoints();
        editor::Stop ran = writing.resume();
        check(ran.exited && ran.status == 112,
              "and what was written reached the program - 100 + 2 + 4 + 6");
        writing.stop();
    }

    editor::path::removeTree(dir);
}

// Standing somewhere with no source is not a debugger that died. Both front
// ends have to tell those apart the same way, which is why the question is one
// function rather than three lines written out in one of them.
void steppingOffTheEnd() {
    std::printf("a stop with no source, told from a debugger that died\n");

    // lldb, having stepped past the end of main into dyld.
    const std::string lldbSaid =
        "Process 10488 stopped\n"
        "* thread #1, queue = 'com.apple.main-thread', stop reason = instruction step over\n"
        "    frame #0: 0x0000000180a3b154 dyld`start + 7000\n";
    check(editor::dbg_stoppedWithNoSource(lldbSaid),
          "lldb standing in the loader is a place, not a failure");

    // gdb, the same step.
    const std::string gdbSaid =
        "0x00007ffff7829d90 in __libc_start_call_main () from /lib64/libc.so.6\n"
        "#0  0x00007ffff7829d90 in __libc_start_call_main ()\n";
    check(editor::dbg_stoppedWithNoSource(gdbSaid), "and so is gdb in libc's start");

    // A debugger that has gone says none of those things.
    check(!editor::dbg_stoppedWithNoSource(""),
          "a debugger that said nothing at all has died, not arrived");
    check(!editor::dbg_stoppedWithNoSource("The system cannot find the file specified."),
          "and neither is a shell's complaint a place to be standing");
}

// What a console puts around what a debugger says. The escape sequences below
// are real: captured from a pseudo-console the first time cdb was given one.
void whatAConsoleAdds() {
    std::printf("a console's own marks, taken back off\n");

    // The burst a console writes before the first word of output: hide the
    // cursor, clear, reset colour, go home - then the text, then a window
    // title in an OSC sequence ended by a bell, then show the cursor.
    const std::string dressed =
        "\x1b[?9001h\x1b[?1004h\x1b[?25l\x1b[2J\x1b[m\x1b[HLINE-ONE\r\n"
        "\x1b]0;C:\\Temp\\slowtalk.exe\x07\x1b[?25hLINE-TWO\r\n";
    checkEqual(editor::dbg_withoutEscapes(dressed), "LINE-ONE\r\nLINE-TWO\r\n",
               "the console's escape sequences come off and the words stay");

    checkEqual(editor::dbg_withoutEscapes("plain text"), "plain text",
               "text with none of them is left exactly as it is");
    checkEqual(editor::dbg_withoutEscapes(""), "", "and nothing is nothing");

    // A sequence cut off at the end of what has been read so far must not take
    // the reader past the end of it.
    checkEqual(editor::dbg_withoutEscapes("done\x1b["), "done",
               "an unfinished sequence ends the text rather than running off it");

    // The echo: a console gives back what was typed at it, so an answer starts
    // with its own question. Only the first line matching goes - a program
    // that prints the same word keeps it.
    const std::string echoed = "p\r\nx = 3\r\n.printf \"<<ride%cdone>>\\n\", 0x2d\r\n";
    checkEqual(editor::dbg_withoutEcho(echoed, "p", ".printf \"<<ride%cdone>>\\n\", 0x2d"),
               "x = 3\n", "the question and the marker command come off the answer");

    const std::string twice = "g\r\ng\r\ng\r\n";
    checkEqual(editor::dbg_withoutEcho(twice, "g", "marker"), "g\ng\n",
               "and only the first of them, since the rest are the program's");
}

// Taking the program's words out of a debugger's transcript. The three fixtures below are real:
// each was captured from the debugger it names, driving a program that prints a marker, flushes,
// stops, and prints another on the way out - kept verbatim so the filter is checked against all three shapes on every machine.
void whatTheProgramSaid() {
    std::printf("the program's words, out of the debugger's transcript\n");

    // lldb. Note the source echo: it prints the lines around the stop, and those lines contain the
    // program's own string literals - which is why looking for the program's output rather than
    // removing the debugger's cannot work.
    const std::string lldbSaid =
        "\n(lldb) run\n"
        "MARKER-ONE\n"
        "Process 10488 launched: '/var/folders/sb/T/ride-run-10477' (arm64)\n"
        "Process 10488 stopped\n"
        "* thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.1\n"
        "    frame #0: 0x0000000100000470 ride-run-10477`main at talker.c:8:5\n"
        "   5   \tprintf(\"MARKER-ONE\\n\");\n"
        "   6   \tfflush(stdout);\n"
        "   7   \tint x = 1;\n"
        "-> 8   \tx = x + 1;\n"
        "    \t    ^\n"
        "   9   \tprintf(\"MARKER-TWO %d\\n\", x);\n"
        "Target 0: (ride-run-10477) stopped.\n"
        "(lldb) script print(\"<<ride\" + \"-done>>\")\n";
    checkEqual(editor::dbg_programOutput(editor::DebuggerLldb, lldbSaid), "MARKER-ONE\n",
               "lldb: the program's line, and none of the source it echoed");

    // gdb. Its prompt carries its own words after it, so the whole line goes.
    const std::string gdbSaid =
        "\n(gdb) Starting program: /tmp/ride-run-2546618 < /dev/null\n"
        "[Thread debugging using libthread_db enabled]\n"
        "Using host libthread_db library \"/lib64/libthread_db.so.1\".\n"
        "MARKER-ONE\n"
        "\n"
        "Breakpoint 1, main () at /tmp/ride-said-probe/talker.c:8\n"
        "8\t    x = x + 1;\n"
        "Missing rpms, try: dnf --enablerepo='*debug*' install glibc-debuginfo\n"
        "(gdb) \n";
    checkEqual(editor::dbg_programOutput(editor::DebuggerGdb, gdbSaid), "MARKER-ONE\n",
               "gdb: the program's line, without the thread and debuginfo chatter");

    // cdb, where the program's output arrives after the prompt on the prompt's
    // own line - so there the prompt is taken off and the rest is kept.
    const std::string cdbSaid =
        "\n0:000> MARKER-ONE\n"
        "Breakpoint 0 hit\n"
        "ride_run_4116!main+0x2a:\n"
        "00007ff6`08a8718a 8b442420        mov     eax,dword ptr [rsp+20h]\n"
        "0:000> \n"
        "\n0:000> Last event: 2b0c.34b8: Hit breakpoint 0\n"
        "  debugger time: Fri Aug 21 18:06:58.745 2026 (UTC + 5:00)\n"
        "0:000> \n"
        "\n0:000> C:\\Users\\G_R_AKHTAR\\AppData\\Local\\Temp\\talker.cpp(8)\n"
        "(00007ff6`08a87160)   ride_run_4116!main+0x2a   |  (00007ff6`08a871e0)   ride_run!printf\n"
        "0:000> \n";
    checkEqual(editor::dbg_programOutput(editor::DebuggerCdb, cdbSaid), "MARKER-ONE\n",
               "cdb: the line after its prompt is the program's, and is kept");

    const std::string cdbExit =
        "\n0:000> MARKER-TWO 2\n"
        "ModLoad: 00007ffb`d2200000 00007ffb`d221b000   C:\\WINDOWS\\SYSTEM32\\kernel.appcore.dll\n"
        "ntdll!NtTerminateProcess+0x14:\n"
        "00007ffb`d6460904 c3              ret\n"
        "0:000> \n"
        "\n0:000> Last event: 2b0c.34b8: Exit process 0:2b0c, code 0\n";
    checkEqual(editor::dbg_programOutput(editor::DebuggerCdb, cdbExit), "MARKER-TWO 2\n",
               "and what it printed on the way out comes through the same way");

    // The same gdb, once the program is not buffered: its output arrives the instant it is printed,
    // which is after gdb's prompt on gdb's own line. Captured with the exec-wrapper in place.
    // Dropping prompt lines whole - which is right for lldb - lost this one entirely.
    const std::string gdbLive =
        "\n(gdb) MARKER-TWO 2\n"
        "11\t    return 0;\n"
        "(gdb) \n";
    checkEqual(editor::dbg_programOutput(editor::DebuggerGdb, gdbLive), "MARKER-TWO 2\n",
               "gdb: output printed after its prompt is the program's, not gdb's");

    // cdb on a console gives back what the editor typed at it, hard against
    // the prompt. Captured from the window the first time it stepped with the
    // console in place, where these three lines sat in among the program's.
    const std::string echoedBack =
        "\n0:000>p\n"
        "Counter(100) made\n"
        "0:000>ln\n"
        ".lastevent\n";
    checkEqual(editor::dbg_programOutput(editor::DebuggerCdb, echoedBack),
               "Counter(100) made\n",
               "cdb: the editor's own commands, echoed by the console, are not output");

    // What is not recognised is kept. A debugger line nobody has taught this
    // about is a smaller fault in the console than a line of output that never
    // arrives, and this says which way that trade goes.
    const std::string strange = "\n(gdb) Continuing.\nsomething nobody has seen before\n(gdb) \n";
    checkEqual(editor::dbg_programOutput(editor::DebuggerGdb, strange),
               "something nobody has seen before\n",
               "a line the filter does not know is kept rather than dropped");

    // A program that prints something shaped like a source echo keeps it: the
    // tab is what makes an echo an echo.
    const std::string counting = "\n(gdb) Continuing.\n8 apples\n(gdb) \n";
    checkEqual(editor::dbg_programOutput(editor::DebuggerGdb, counting), "8 apples\n",
               "and a number at the start of a line is not a source echo without the tab");

    checkEqual(editor::dbg_programOutput(editor::DebuggerLldb, ""), "",
               "nothing said is nothing printed");
}

// What the debugger said, across the seam the window uses. A debugged program writes down the
// debugger's own stream, so what it printed is in `said` with the debugger's words - and the window
// could not read `said` until ride_stop_said existed. That property is checked rather than assumed: the program's output has to be in there.
void whatTheDebuggerHeard() {
    std::printf("what the debugger said, and the program with it\n");

    const std::string host = editor::hostArch();
    if (editor::dbg_for(editor::ToolCc1, host) == editor::DebuggerNone) {
        skipped("no debugger on this machine, so nothing is listened to");
        return;
    }
    const char* cc1 = std::getenv("CC1");
    if (!cc1 || !*cc1 || !editor::path::exists(cc1)) {
        skipped("no cc1, so nothing is built to listen to");
        return;
    }

    std::string dir = editor::path::join(editor::path::tempDir(), "ride-said-test");
    editor::path::removeTree(dir);
    editor::path::makeDirectories(dir);
    std::string source = editor::path::join(dir, "talker.c");

    // It flushes after printing, so a marker that is missing from `said` is
    // missing because it never travelled - not because it was in a buffer.
    writeSource(source,
                "#include <stdio.h>\n"
                "\n"
                "int main(void)\n"
                "{\n"
                "    printf(\"MARKER-BEFORE\\n\");\n"
                "    fflush(stdout);\n"
                "    int x = 1;\n"
                "    x = x + 1;\n"
                "    return x;\n"
                "}\n");

    RIDEProgram* built = ride_build_program(0, cc1, "cl", "shc", "cxx1", editor::ToolCc1,
                                          source.c_str(), editor::LangC, host.c_str(),
                                          editor::ConfigDebug);
    if (ride_program_ok(built) == 0) {
        skipped("cc1 did not build it, so there is nothing to stop inside");
        ride_program_free(built);
        editor::path::removeTree(dir);
        return;
    }

    RIDEDebugger* debugger = ride_debugger_new();
    check(ride_debugger_start(debugger, editor::ToolCc1, host.c_str(),
                             ride_program_path(built)) != 0,
          "the debugger starts on a program that talks");

    // Line 8 is x = x + 1, after the printf and its flush.
    check(ride_debugger_break(debugger, source.c_str(), 8) != 0, "a breakpoint after the printing");

    ride_debugger_run(debugger);
    check(ride_stop_stopped(debugger) != 0, "and it stops there");

    const std::string said = ride_stop_said(debugger);
    check(!said.empty(), "what the debugger said comes across the seam");
    check(said.find("MARKER-BEFORE") != std::string::npos,
          "and the program's own output is in it, which is why the window wants it");

    ride_debugger_stop(debugger);
    ride_debugger_free(debugger);
    ride_program_free(built);
    editor::path::removeTree(dir);
}

// C++ on Windows, where none of the chain is ours: cl writes the .pdb, cdb reads it, and the editor
// only drives them. This is the other half of the same machine - the C file next to it goes to
// cc1 and cannot be debugged at all, because MASM carries no line table.
void debuggingCppForReal() {
    std::printf("stopping inside what cl built\n");

    if (editor::dbg_for(editor::ToolMsvc, editor::hostArch()) == editor::DebuggerNone) {
        skippedNotHere("%s",
                    editor::dbg_whyNot(editor::ToolMsvc, editor::hostArch()).c_str());
        return;
    }

    std::string dir = editor::path::join(editor::path::tempDir(), "ride-cpp-debug-test");
    editor::path::removeTree(dir);
    editor::path::makeDirectories(dir);
    std::string source = editor::path::join(dir, "counted.cpp");
    writeSource(source,
                "static int twice(int n)\n"
                "{\n"
                "    return n * 2;\n"
                "}\n"
                "\n"
                "int main(void)\n"
                "{\n"
                "    int total = 0;\n"
                "    for (int i = 1; i <= 3; ++i) {\n"
                "        total = total + twice(i);\n"
                "    }\n"
                "    return total;\n"
                "}\n");

    editor::Toolchain tool;
    editor::Built made = editor::buildProgram(tool, editor::ToolMsvc, source, editor::LangCpp,
                                              editor::hostArch(), editor::ConfigDebug);
    check(made.ok, "cl builds a program from the C++ file");
    if (!made.ok) { editor::removeProgram(made); editor::path::removeTree(dir); return; }

    editor::Debugger debugger;
    check(debugger.start(editor::dbg_for(editor::ToolMsvc, editor::hostArch()), made.program),
          "cdb starts on it");
    if (!debugger.running()) { editor::removeProgram(made); editor::path::removeTree(dir); return; }

    check(debugger.breakAt(source, 10), "a breakpoint is set on a line of C++");

    editor::Stop at = debugger.run();
    check(at.stopped, "and running stops on it");
    check(at.line == 10, "on the line asked for");
    check(at.function == "main", "in the function that line is in");

    // These come out of the .pdb cl wrote, read by Microsoft's own debugger.
    std::vector<editor::Variable> locals = debugger.locals();
    bool sawTotal = false, sawCounter = false;
    for (size_t i = 0; i < locals.size(); ++i) {
        if (locals[i].name == "total" && locals[i].value == "0") sawTotal = true;
        if (locals[i].name == "i" && locals[i].value == "1") sawCounter = true;
    }
    check(sawTotal, "the local it declared is there, with the value it has");
    check(sawCounter, "and so is the one the loop declared");

    editor::Stop into = debugger.stepInto();
    check(into.stopped && into.function == "twice", "stepping into a call arrives inside it");

    // And from in there the stack says how it got in, which is the whole of
    // what a call stack is for and cannot be seen from main.
    std::vector<editor::StackFrame> stack = debugger.frames();
    check(stack.size() == 2, "the stack inside a call is that call and what called it");
    if (stack.size() == 2) {
        checkEqual(stack[0].function, "twice", "standing in the function stepped into");
        checkEqual(stack[1].function, "main", "called from the one that called it");
        check(stack[1].line == 10, "on the line that is waiting for it to come back");
    }

    // And that frame's variables, which is cdb's .frame rather than lldb's
    // frame select or gdb's frame. The three spell it differently and only one
    // machine can say whether this one is spelled right.
    check(debugger.selectFrame(1), "the debugger goes to the frame that called it");
    std::vector<editor::Variable> caller = debugger.locals();
    bool sawCallersTotal = false;
    for (size_t i = 0; i < caller.size(); ++i)
        if (caller[i].name == "total") sawCallersTotal = true;
    check(sawCallersTotal, "and the variables read there are the caller's");

    check(debugger.selectFrame(0), "and it goes back to the frame it stopped in");
    std::vector<editor::Variable> back = debugger.locals();
    bool sawArgument = false;
    for (size_t i = 0; i < back.size(); ++i)
        if (back[i].name == "n") sawArgument = true;
    check(sawArgument, "where the argument is in scope again");

    editor::Stop out = debugger.stepOut();
    check(out.stopped && out.function == "main", "and stepping out comes back");

    // A watch, which under cdb is ?? again - the same evaluator the writing
    // below uses, and the one spelling only this machine can prove. Standing
    // in main with the loop's first addition still to come, so total is 0.
    debugger.addWatch("total + 1");
    check(debugger.watches().size() == 1, "an expression can be watched");
    check(debugger.watches()[0].ok, "and cdb answers it");
    checkEqual(debugger.watches()[0].value, "1", "with what it comes to - total 0 plus 1");

    // Writing one back, which is cdb's ?? - its C++ expression evaluator - rather than lldb's
    // expression or gdb's set variable. Only this machine can say whether that spelling is right,
    // and what is checked is what the program returns: a variable reading back 100 and changing nothing is a debugger showing its own idea of the program.
    std::string said;
    check(debugger.setVariable("total", "100", &said), "a variable is written back");
    std::vector<editor::Variable> now = debugger.locals();
    bool written = false;
    for (size_t i = 0; i < now.size(); ++i)
        if (now[i].name == "total" && now[i].value == "100") written = true;
    check(written, "and reads back as what it was set to");

    check(!debugger.setVariable("nosuch", "1", &said), "a name that is not in scope is refused");
    check(!said.empty(), "with the debugger's own words for why");

    // And the watch followed the write without being asked: a write is a move as far as an expression is concerned.
    checkEqual(debugger.watches()[0].value, "101", "the watch followed what was written");

    debugger.clearBreakpoints();
    editor::Stop ended = debugger.resume();
    check(ended.exited, "and with none left the program runs to the end");
    check(ended.status == 112, "returning what was written into it - 100 + 2 + 4 + 6");

    debugger.stop();
    editor::removeProgram(made);
    editor::path::removeTree(dir);
}

// The window asks for the project's build through the same seam, and the checks below are the same
// questions the terminal's F4 asks - which is the point of there being one core and two front ends
// rather than two editors.
void theWindowsProjectBuild() {
    std::printf("what the window asks about building a project\n");

    file::path dir = file::temp_directory_path() / "ride-bridge-target";
    file::remove_all(dir);
    file::create_directories(dir);
    writeSource((dir / "add.c").string(), "int add(int a, int b) { return a + b; }\n");
    writeSource((dir / "main.c").string(), "int add(int, int);\nint main(void) { return add(1, 2); }\n");
    writeSource((dir / "project.pro").string(),
                "{\n  \"name\": \"sums\",\n"
                "  \"groups\": { \"Sources\": [\"add.c\", \"main.c\"] },\n"
                "  \"build\": { \"target\": \"sums\", \"groups\": [\"Sources\"] }\n}\n");

    RIDEProject* project = ride_project_new();
    char trouble[512] = {0};
    check(ride_project_load(project, dir.string().c_str(), trouble, sizeof trouble) != 0,
          "the window loads a project that says what it builds");
    check(ride_project_builds(project) != 0, "and is told that it builds something");
    check(ride_project_target_ready(project) != 0, "the sources come back through the seam");
    check(ride_project_target_sources(project) == 2, "both of them");
    check(std::string(ride_project_target_program(project)).find("sums") != std::string::npos,
          "with the program named after the target");

    // A target of both languages, which the window used to be told was a refusal and is now told is
    // two parts. The window has to be able to build what the terminal can: an editor with two front
    // ends that disagree about what a project is, is two editors.
    writeSource((dir / "extra.cpp").string(), "int twice(int n) { return n * 2; }\n");
    writeSource((dir / "project.pro").string(),
                "{\n  \"name\": \"sums\",\n"
                "  \"groups\": { \"Sources\": [\"add.c\", \"main.c\", \"extra.cpp\"] },\n"
                "  \"build\": { \"target\": \"sums\", \"groups\": [\"Sources\"] }\n}\n");
    check(ride_project_load(project, dir.string().c_str(), trouble, sizeof trouble) != 0,
          "a project of both languages loads");
    check(ride_project_target_ready(project) != 0, "and is ready to build rather than refused");
    check(ride_project_target_sources(project) == 3, "with all three of its sources");
    check(ride_project_target_parts(project) == 2, "in two parts, one per language");
    check(ride_project_part_language(project, 0) == editor::LangC, "the C first");
    check(ride_project_part_language(project, 1) == editor::LangCpp, "and the C++ after it");
    check(ride_project_part_toolchain(project, 0, "cc1", "cl", "shc", "cxx1", editor::ToolAuto) ==
              editor::ToolCc1,
          "which go to cc1");
    // Run file on one of them is Run project: alone it links against nothing.
    check(ride_project_runs_as_project(project, (dir / "main.c").string().c_str()) == 3,
          "Run file on one of three sources runs the project - the window is told three");
    check(ride_project_runs_as_project(project, (dir / "other.c").string().c_str()) == 0,
          "and a file outside the build runs as itself");
    check(ride_project_part_toolchain(project, 1, "cc1", "cl", "shc", "cxx1", editor::ToolAuto) ==
              (editor::resolve(editor::Toolchain(), editor::LangCpp)),
          "and to this machine's C++ compiler, without the window being told which");
    checkEqual(ride_project_part_group(project, 0), "Sources",
               "both out of the one group, which is where they were");

    // A group that names its compiler is taken at its word, and the whole group
    // goes there - which is the override, and the only way to make one compiler
    // take another's language on purpose.
    writeSource((dir / "project.pro").string(),
                "{\n  \"name\": \"sums\",\n"
                "  \"groups\": { \"Sources\": { \"files\": [\"add.c\", \"main.c\", "
                "\"extra.cpp\"], \"toolchain\": \"cl\" } },\n"
                "  \"build\": { \"target\": \"sums\", \"groups\": [\"Sources\"] }\n}\n");
    check(ride_project_load(project, dir.string().c_str(), trouble, sizeof trouble) != 0,
          "a group that names its compiler loads");
    check(ride_project_target_ready(project) != 0, "and is ready");
    check(ride_project_target_parts(project) == 1, "as one part, because one compiler takes it");
    check(ride_project_part_toolchain(project, 0, "cc1", "cl", "shc", "cxx1", editor::ToolAuto) ==
              editor::ToolMsvc,
          "the one the group named");

    // The Target and Tools menus write the project's own target and compiler
    // while it is open (the audit of 2026-09-19: they had never reached the
    // file), and read back as written.
    check(ride_project_set_arch(project, "tms6747") != 0, "the window sets the project's target");
    check(ride_project_set_toolchain(project, editor::ToolCxx1) != 0, "and its compiler");
    check(ride_project_load(project, dir.string().c_str(), trouble, sizeof trouble) != 0 &&
              std::string(ride_project_arch(project)) == "tms6747" &&
              ride_project_toolchain(project) == editor::ToolCxx1,
          "and both are in the .pro when it is read again");
    RIDEProject* none = ride_project_new();
    check(ride_project_set_arch(none, "tms6747") == 0 && ride_project_set_toolchain(none, editor::ToolCc1) == 0,
          "with no project open there is nothing to write, and the window is told so");
    ride_project_free(none);

    ride_project_free(project);
    file::remove_all(dir);
}

// Everything the Windows front end does to stop a program on a line, done through the same seam it
// uses, on a machine where a debugger exists.
void theSeamTheWindowUses() {
    std::printf("what the window asks the core to do\n");

    std::string host = editor::hostArch();
    check(ride_debugger_for(editor::ToolCc1, host.c_str()) ==
              static_cast<int>(editor::dbg_for(editor::ToolCc1, host)),
          "the window is told the same debugger the editor found");
    check(std::string(ride_debugger_name(ride_debugger_for(editor::ToolCc1, host.c_str()))) ==
              editor::dbg_name(editor::dbg_for(editor::ToolCc1, host)),
          "and the same name for it");

    // The two compilers are not in the same position on the same machine, and
    // the reason given has to say which one it is talking about.
    // M10: what cc1 builds for Windows is debugged in cdb, where Debugging Tools for Windows is.
    const bool cdbHere = editor::dbg_for(editor::ToolCc1, "x86_64-windows") == editor::DebuggerCdb;
    check(ride_debugger_for(editor::ToolCc1, "x86_64-windows") == (cdbHere ? static_cast<int>(editor::DebuggerCdb) : 0),
          "what cc1 builds for Windows is debugged in cdb where cdb is");
    check(cdbHere ? std::string(ride_no_debugger_because(editor::ToolCc1, "x86_64-windows")).empty()
                  : std::string(ride_no_debugger_because(editor::ToolCc1, "x86_64-windows")).find("cdb") != std::string::npos,
          "and where it is not, the reason names cdb");
    // cl is a different matter on the same machine, and which way it goes
    // depends on whether Microsoft's own debugger is installed - so the check
    // is that the answer and the reason agree, not that either is fixed.
    int forCl = ride_debugger_for(editor::ToolMsvc, "x86_64-windows");
    std::string whyNotCl = ride_no_debugger_because(editor::ToolMsvc, "x86_64-windows");
    if (forCl == static_cast<int>(editor::DebuggerCdb)) {
        check(whyNotCl.empty(), "where cdb is installed, cl's C++ has nothing standing in its way");
    } else {
        check(whyNotCl.find("cdb") != std::string::npos,
              "and where it is not, the reason names the debugger that is missing");
    }

    if (editor::dbg_for(editor::ToolCc1, host) == editor::DebuggerNone) {
        skipped("no debugger on this machine, so the rest is not tried");
        return;
    }

    const char* cc1 = std::getenv("CC1");
    if (cc1 && *cc1 && !editor::path::exists(cc1)) {
        skipped("no cc1 at %s, so nothing is built to stop inside", cc1);
        return;
    }
    if (!cc1 || !*cc1) {
        skipped("no $CC1, so nothing is built to stop inside");
        return;
    }

    std::string dir = editor::path::join(editor::path::tempDir(), "ride-bridge-test");
    editor::path::removeTree(dir);
    editor::path::makeDirectories(dir);
    std::string source = editor::path::join(dir, "seam.c");
    writeSource(source,
                "static int twice(int n)\n"
                "{\n"
                "    return n * 2;\n"
                "}\n"
                "\n"
                "int main(void)\n"
                "{\n"
                "    int total = 0;\n"
                "    for (int i = 1; i <= 3; ++i) {\n"
                "        total = total + twice(i);\n"
                "    }\n"
                "    return total;\n"
                "}\n");

    // Built through the bridge, exactly as the window builds it.
    RIDEProgram* built = ride_build_program(0, cc1, "cl", "shc", "cxx1", editor::ToolCc1,
                                          source.c_str(), editor::LangC,
                                          editor::hostArch(), editor::ConfigDebug);
    check(ride_program_ok(built) != 0, "the window's build makes a program");
    if (ride_program_ok(built) == 0) { ride_program_free(built); editor::path::removeTree(dir); return; }
    check(editor::path::exists(ride_program_path(built)),
          "and leaves it where it said it did, for a debugger to open");

    RIDEDebugger* debugger = ride_debugger_new();
    check(ride_debugger_start(debugger, editor::ToolCc1, host.c_str(),
                             ride_program_path(built)) != 0,
          "the debugger starts on it");
    check(ride_debugger_running(debugger) != 0, "and says it is running");

    check(ride_debugger_break(debugger, source.c_str(), 10) != 0, "a breakpoint is set");

    ride_debugger_run(debugger);
    check(ride_stop_stopped(debugger) != 0, "running stops on it");
    check(ride_stop_line(debugger) == 10, "on the line asked for");
    check(std::string(ride_stop_function(debugger)) == "main", "in the right function");

    // The locals are read once when it stops and handed over one string at a
    // time, because the managed side cannot hold a std::vector.
    int howMany = ride_locals_count(debugger);
    bool sawTotal = false;
    for (int i = 0; i < howMany; ++i)
        if (std::string(ride_local_name(debugger, i)) == "total" &&
            std::string(ride_local_value(debugger, i)) == "0") sawTotal = true;
    check(howMany > 0 && sawTotal, "and what is in scope comes back one name at a time");
    check(std::string(ride_local_name(debugger, howMany + 5)).empty(),
          "an index past the end answers with nothing rather than reading past it");

    ride_debugger_step_into(debugger);
    check(std::string(ride_stop_function(debugger)) == "twice", "stepping into arrives inside");

    // And the stack, handed over the same way and for the same reason. The
    // window has nowhere to put a vector either.
    check(ride_stack_count(debugger) == 2, "the stack inside the call has two frames");
    check(std::string(ride_stack_function(debugger, 1)) == "main",
          "the second of them being what called it");
    check(ride_stack_line(debugger, 1) == 10, "on the line waiting for it to come back");
    check(std::string(ride_stack_function(debugger, ride_stack_count(debugger))).empty() &&
              ride_stack_line(debugger, -1) == 0,
          "and an index off either end answers with nothing");

    // And the line the window writes for a frame, read back to say which frame
    // the row it was clicked on is - the window counts no rows of its own.
    std::string written = ride_stack_text(debugger, 1);
    check(written == "  main   seam.c:10", "the window is given the line to write");
    check(ride_stack_on_line(debugger, written.c_str()) == 1,
          "and reads it back as the frame it was written for");
    check(ride_stack_on_line(debugger, "  (nothing in scope here)") == -1,
          "while a row that is not a frame answers -1 rather than a frame");

    // Looking at a caller through the seam: the locals the window reads
    // afterwards are that frame's, and the line it writes for it is marked.
    check(ride_looking_at(debugger) == 0, "the window starts at the frame it stopped in");
    check(ride_debugger_look_at(debugger, 1) != 0, "and can be told to look at the caller");
    check(ride_looking_at(debugger) == 1, "which is where it says it is looking");
    check(std::string(ride_looking_text(debugger)).find("main's") != std::string::npos,
          "with a line saying whose variables these now are");
    check(std::string(ride_stack_text(debugger, 1)).compare(0, 1, ">") == 0,
          "and that frame written with its mark");

    bool sawCaller = false;
    for (int i = 0; i < ride_locals_count(debugger); ++i)
        if (std::string(ride_local_name(debugger, i)) == "total") sawCaller = true;
    check(sawCaller, "the variables read through the seam are the caller's");

    check(ride_debugger_look_at(debugger, 0) != 0, "and it goes back to the stop");
    check(std::string(ride_looking_text(debugger)).empty(),
          "where nothing is said about whose variables they are, the top line saying it");
    check(ride_debugger_look_at(debugger, 9) == 0,
          "a frame that is not there is refused rather than answered with another's");

    // Setting one through the seam, which is the window's only way to it. The
    // line it writes for a variable is read back the same way a frame's is.
    check(ride_debugger_look_at(debugger, 0) != 0, "back at the frame it stopped in");
    std::string variableLine = ride_local_text(debugger, 0);   // n, inside twice
    check(!variableLine.empty(), "the window is given the line to write for a variable");
    check(ride_locals_on_line(debugger, variableLine.c_str()) == 0,
          "and reads it back as the variable it was written for");
    check(ride_locals_on_line(debugger, "called from") == -1,
          "while a row that is not a variable answers -1");

    // In the caller's frame, where the window's own gesture would be aimed as often as not - and
    // put back afterwards, so what the program returns at the end of this test is still what it
    // worked out. The reaching-the-program half is checked on its own run in debuggingForReal.
    check(ride_debugger_look_at(debugger, 1) != 0, "looking at the caller to set one of its own");
    check(ride_set_variable(debugger, "total", "100") != 0, "a variable is set through it");
    bool setThrough = false;
    for (int i = 0; i < ride_locals_count(debugger); ++i)
        if (std::string(ride_local_name(debugger, i)) == "total" &&
            std::string(ride_local_value(debugger, i)) == "100") setThrough = true;
    check(setThrough, "and the locals it reads afterwards say so");

    check(ride_set_variable(debugger, "nosuch", "1") == 0, "a name that is not there is refused");
    check(std::string(ride_set_complaint(debugger)).size() > 0,
          "with the debugger's own words to show for it");

    check(ride_set_variable(debugger, "total", "0") != 0, "and it is put back where it was");
    check(ride_debugger_look_at(debugger, 0) != 0, "with the frame put back too");

    // A watch through the seam, which is the window's only way to one. The list is the core's, so
    // what is checked here is that the window can put one in it, read the line to write for it, and
    // find it again from that line - the same three questions it asks about a frame.
    ride_watch_add(debugger, "n + 1");
    check(ride_watch_count(debugger) == 1, "the window can add a watch");
    std::string watchLine = ride_watch_text(debugger, 0);
    check(watchLine.find("n + 1 = ") != std::string::npos,
          "and is given the line to write for it, answered");
    check(ride_watch_on_line(debugger, watchLine.c_str()) == 0,
          "which reads back as the watch it was written for");
    check(ride_watch_on_line(debugger, "  total = 0   [int]") == -1,
          "while a variable is not read as one");
    checkEqual(std::string(ride_watch_expression(debugger, 0)), "n + 1",
               "and the expression itself comes back for the box that changes it");

    ride_watch_set(debugger, 0, "");
    check(ride_watch_count(debugger) == 0, "an empty answer takes the watch away");

    // And the line the window writes at the top, which it compares a clicked
    // row against to know that the row means the frame it stopped in.
    checkEqual(std::string(ride_stop_line_text("/tmp/seam.c", 10, "main")),
               "stopped at seam.c:10 in main", "the window is given that line too");

    ride_debugger_step_out(debugger);
    check(std::string(ride_stop_function(debugger)) == "main", "and stepping out comes back");

    ride_debugger_clear(debugger);
    ride_debugger_resume(debugger);
    check(ride_stop_exited(debugger) != 0, "with no breakpoints left it runs to the end");
    check(ride_stop_status(debugger) == 12, "returning what it worked out");

    ride_debugger_stop(debugger);
    check(ride_debugger_running(debugger) == 0, "and stops when it is told to");
    ride_debugger_free(debugger);

    // Freeing the handle takes the program with it, which is what stops a
    // debugging session leaving one behind in the temporary directory.
    std::string was = ride_program_path(built);
    ride_program_free(built);
    check(!editor::path::exists(was), "freeing the build removes the program it made");

    editor::path::removeTree(dir);
}

// A directory with no project file gets one made, rather than the editor
// opening without a project at all.
void aProjectMadeFromWhatIsThere() {
    std::printf("a project made where there was none\n");

    namespace pth = editor::path;
    std::string dir = pth::join(pth::tempDir(), "ride-made-project");
    pth::removeTree(dir);
    pth::makeDirectories(pth::join(dir, "src"));
    pth::makeDirectories(pth::join(dir, "obj"));

    writeSource(pth::join(dir, "one.c"), "int one;\n");
    writeSource(pth::join(dir, "notes.txt"), "not source\n");
    writeSource(pth::join(dir, "src/two.cpp"), "int two;\n");
    writeSource(pth::join(dir, "src/two.h"), "extern int two;\n");
    writeSource(pth::join(dir, "obj/two.o"), "not source either\n");

    editor::Project project;
    editor::Outcome made = editor::beginFromWhatIsThere(project, dir);
    check(made.ok, "a project is made where there was none");
    check(project.loaded(), "and the project says it is loaded");
    check(pth::exists(pth::join(dir, "ride-made-project.pro")),
          "and the file is written, named after the directory");
    check(project.name() == "ride-made-project", "named after the directory it is in");

    // What it picked up, and what it left alone.
    std::string written = readWholeFile(pth::join(dir, "ride-made-project.pro"));
    check(written.find("one.c") != std::string::npos, "source in the directory is in it");
    check(written.find("src/two.cpp") != std::string::npos, "and source one level down");
    check(written.find("src/two.h") != std::string::npos, "headers as well as sources");
    check(written.find("notes.txt") == std::string::npos, "what is not source is left out");
    check(written.find("two.o") == std::string::npos, "and so is anything under obj");

    // Read back by the thing that will read it tomorrow.
    editor::Project again;
    std::string why;
    check(again.load(dir, why), "and what was written can be read again");
    check(why.empty(), "without complaint");
    check(again.groups().size() == 2, "into the two groups it was given");

    // Headers and sources are different things to look at, so they are in
    // different groups even when nobody said so.
    bool headersHoldTheHeader = false, sourcesHoldTheSource = false, headersHoldNoSource = true;
    for (size_t i = 0; i < again.groups().size(); ++i) {
        const editor::Group& group = again.groups()[i];
        for (size_t j = 0; j < group.files.size(); ++j) {
            if (group.name == "Headers" && group.files[j] == "src/two.h") headersHoldTheHeader = true;
            if (group.name == "Headers" && group.files[j] == "one.c") headersHoldNoSource = false;
            if (group.name == "Sources" && group.files[j] == "one.c") sourcesHoldTheSource = true;
        }
    }
    check(headersHoldTheHeader, "the header is in Headers");
    check(sourcesHoldTheSource, "the source is in Sources");
    check(headersHoldNoSource, "and neither is in the other");

    pth::removeTree(dir);
}


// One rule for which group a file belongs in, asked by three callers.
// JSON, which the editor colours and compiles nothing from.
void jsonIsALanguage() {
    std::printf("JSON, and the project file being legible\n");

    check(editor::languageFor("prime.pro") == editor::LangJson, "a .pro is JSON");
    check(editor::languageFor("settings.json") == editor::LangJson, "and so is a .json");
    check(editor::languageFor("PRIME.PRO") == editor::LangJson, "whatever the case");
    checkEqual(editor::languageName(editor::LangJson), "JSON", "and it is named");

    // Nothing compiles it, and the refusal says what it is rather than
    // "cannot compile this file".
    editor::Toolchain tool;
    check(!editor::canCompile(editor::ToolCc1, editor::LangJson), "cc1 does not compile it");
    check(!editor::canCompile(editor::ToolShc, editor::LangJson), "nor does shc");
    check(!editor::canCompile(editor::hostCppToolchain(), editor::LangJson),
          "nor the machine's C++ compiler");
    check(editor::refusal(editor::ToolCc1, editor::LangJson).find("JSON") != std::string::npos,
          "and the refusal names the language");
    (void)tool;

    // A key is coloured apart from a value, which is the whole of what makes a
    // project file scannable: the thing before the colon is the name.
    editor::SyntaxState state;
    std::string line = "  \"name\": \"sample\",";
    std::vector<unsigned char> kind = editor::highlight(line, editor::LangJson, state);
    check(kind[2] == editor::KindType, "a string before a colon is marked as a key");
    check(kind[line.find("\"sample\"")] == editor::KindString, "and the value after it as a string");

    std::string numbers = "  \"indent\": 4, \"tabs\": true, \"none\": null";
    std::vector<unsigned char> marks = editor::highlight(numbers, editor::LangJson, state);
    check(marks[numbers.find('4')] == editor::KindNumber, "a number is a number");
    check(marks[numbers.find("true")] == editor::KindKeyword, "true is one of the three words");
    check(marks[numbers.find("null")] == editor::KindKeyword, "and so is null");

    // An escaped quote does not end the string, which is the one place JSON's
    // rules differ from Shalimar's.
    std::string escaped = "\"a\\\"b\": 1";
    std::vector<unsigned char> got = editor::highlight(escaped, editor::LangJson, state);
    // "a\"b": 1  - the key is characters 0 to 5, the escaped quote being 2 and 3.
    check(got[0] == editor::KindType && got[3] == editor::KindType &&
              got[5] == editor::KindType,
          "an escaped quote does not end the key early");
    check(got[6] != editor::KindType, "and the colon after it is not part of the key");
}

void whereAFileBelongs() {
    std::printf("which group a file's name puts it in\n");

    checkEqual(editor::groupForFile("main.c"), "Sources", "a .c is a source");
    checkEqual(editor::groupForFile("engine.cpp"), "Sources", "and so is a .cpp");
    checkEqual(editor::groupForFile("wide.cc"), "Sources", "and a .cc");
    checkEqual(editor::groupForFile("counter.h"), "Headers", "a .h is a header");
    checkEqual(editor::groupForFile("vector3.hpp"), "Headers", "and a .hpp");
    checkEqual(editor::groupForFile("gcd.shl"), "Sources",
               "and a .shl - Sources is the group a new project builds, and a group "
               "of its own kept every Shalimar file made here out of the build");
    check(editor::groupForFile("notes.txt").empty(),
          "and something this editor does not compile belongs nowhere");

    // The suffix decides, not the case of it.
    checkEqual(editor::groupForFile("READ.H"), "Headers", "whatever the case of the suffix");
}

// Named project files: prime.pro, one file of its own per project.
void namedProjectFiles() {
    std::printf("named project files\n");
    namespace pth = editor::path;

    std::string dir = pth::join(pth::tempDir(), "ride-named-projects");
    pth::removeTree(dir);
    pth::makeDirectories(dir);
    writeSource(pth::join(dir, "a.c"), "int a(void) { return 1; }\n");
    writeSource(pth::join(dir, "b.c"), "int b(void) { return 2; }\n");

    checkEqual(editor::Project::suffix(), ".pro", "the suffix is .pro");
    check(editor::Project::projectFilesIn(dir).empty(), "a directory with none has none");

    writeSource(pth::join(dir, "alpha.pro"),
                "{\n  \"name\": \"alpha\",\n  \"groups\": { \"Sources\": [\"a.c\"] }\n}\n");
    writeSource(pth::join(dir, "beta.pro"),
                "{\n  \"name\": \"beta\",\n  \"groups\": { \"Sources\": [\"b.c\"] }\n}\n");

    std::vector<std::string> named = editor::Project::projectFilesIn(dir);
    check(named.size() == 2, "both named projects are found");
    check(named.size() == 2 && pth::filename(named[0]) == "alpha.pro",
          "sorted, so which one is found first does not depend on the filesystem");

    // A directory can be opened, and answers with the first by name.
    std::string error;
    editor::Project byDirectory;
    check(byDirectory.load(dir, error), "a directory holding named projects opens");
    check(byDirectory.name() == "alpha", "at the first of them by name");

    // Or one of them can be named outright, which is what the picker and the
    // remembered last project both do.
    editor::Project byName;
    check(byName.load(pth::join(dir, "beta.pro"), error), "and one can be opened by name");
    check(byName.name() == "beta", "which is the one that opens");
    check(byName.groupOf("b.c") < byName.groups().size(), "with its own files");

    // A .pro one directory down is not a project of this directory. That is
    // what keeps docs/sample.pro a template rather than somebody's project.
    pth::makeDirectories(pth::join(dir, "inner"));
    writeSource(pth::join(pth::join(dir, "inner"), "hidden.pro"), "{ \"name\": \"hidden\" }\n");
    check(editor::Project::projectFilesIn(dir).size() == 2,
          "a .pro one directory down is not found - the search is this directory only");

    // Saving under a new name keeps saving there, and leaves the old file be.
    editor::Project moved;
    check(moved.load(pth::join(dir, "alpha.pro"), error), "a project opens");
    check(moved.saveAs(pth::join(dir, "renamed.pro"), error), "and saves under another name");
    check(pth::exists(pth::join(dir, "renamed.pro")), "the new file is written");
    check(pth::exists(pth::join(dir, "alpha.pro")), "and the old one is left alone");
    check(pth::filename(moved.file()) == "renamed.pro", "further saves go to the new one");

    pth::removeTree(dir);
}

void whatItRemembers() {
    std::printf("what it remembers, and where a first run opens\n");

    namespace pth = editor::path;
    std::string realHome = pth::homeDir();

    std::string home = pth::join(pth::tempDir(), "ride-home-test");
    pth::removeTree(home);
    pth::makeDirectories(home);
    sayWhereHomeIs(home);

    check(pth::homeDir() == home, "home is where the machine says it is");
    check(editor::settings::fileName() ==
              pth::join(pth::join(home, editor::product::kStateDirectory), editor::product::kStateFile),
          "what the editor remembers is under a directory of its own, named for the product");
    check(editor::settings::lastProject().empty(), "and remembers nothing to begin with");

    // A configuration that will not parse is not silently buried. It is kept under .error, a fresh
    // one is written in its place, and the editor can say where the old one went. Before this, the
    // first setting changed after a bad file wrote straight over it.
    {
        std::string config = editor::settings::fileName();
        // The editor makes this directory when it writes; a test that writes
        // the file itself has to make it too.
        pth::makeDirectories(pth::parent(config));
        writeSource(config, "{ this is not json at all");
        editor::settings::rememberProject(home);   // any read is enough to trip it

        check(pth::exists(config + ".error"), "an unreadable configuration is kept as .error");
        check(pth::exists(config), "and a fresh one is written in its place");
        check(editor::settings::setAside() == config + ".error",
              "and the editor can say where the old one went");
        check(editor::settings::lastProject() == home,
              "the fresh one is readable, and takes what is written to it");

        // An empty file is nobody's work: it is not worth keeping a copy of.
        pth::remove(config + ".error");
        writeSource(config, "");
        check(editor::settings::lastProject().empty(), "an empty configuration reads as nothing");
        check(!pth::exists(config + ".error"), "and is not kept aside");
    }

    // A first run has nothing to go back to, so it is given something to open.
    std::string demo = editor::demoDirectory();
    check(!demo.empty(), "a first run is given a project of its own");
    check(pth::exists(pth::join(demo, "src/first.c")), "with one program in it");

    // Two groups, and Headers empty until there is a header - the place to put
    // one exists before the first one does.
    editor::Project made;
    check(editor::beginFromWhatIsThere(made, demo).ok, "and a project over it");
    check(made.groups().size() == 2, "with two groups");
    for (size_t i = 0; i < made.groups().size(); ++i)
        if (made.groups()[i].name == "Headers")
            check(made.groups()[i].files.empty(), "Headers empty until there is a header");

    std::string was = readWholeFile(pth::join(demo, "src/first.c"));
    check(was.find("t_flight") != std::string::npos,
          "which works something out a line at a time, to step through");
    check(was.find("#ifndef M_PI") != std::string::npos,
          "and guards M_PI, which MSVC keeps behind a define");
    check(was.find("3.14") != std::string::npos,
          "with a value only Windows ever uses, and two decimals do not notice");

    // Made once: asking again must not write over what you have done to it.
    writeSource(pth::join(demo, "src/first.c"), "int mine(void) { return 1; }\n");
    check(editor::demoDirectory() == demo, "asking again gives the same directory");
    check(readWholeFile(pth::join(demo, "src/first.c")).find("mine") != std::string::npos,
          "and leaves what you have done to it alone");

    // Remembering, and forgetting what has gone away.
    check(editor::settings::rememberProject(demo), "a project can be remembered");
    check(editor::settings::lastProject() == pth::absolute(demo), "and is given back");

    std::string gone = pth::join(home, "taken-away");
    pth::makeDirectories(gone);
    check(editor::settings::rememberProject(gone), "another can be remembered over it");
    pth::removeTree(gone);
    check(editor::settings::lastProject() == pth::absolute(demo),
          "one that has since been deleted is not offered - the one before it is");

    // Three are kept, most recent first, and a project opened again moves
    // to the front rather than appearing twice.
    std::string second = pth::join(home, "second"), third = pth::join(home, "third"),
                fourth = pth::join(home, "fourth");
    pth::makeDirectories(second); pth::makeDirectories(third); pth::makeDirectories(fourth);
    editor::settings::rememberProject(second);
    editor::settings::rememberProject(third);
    std::vector<std::string> recent = editor::settings::recentProjects();
    check(recent.size() == 3 && recent[0] == pth::absolute(third) && recent[2] == pth::absolute(demo),
          "three are kept, most recent first");
    editor::settings::rememberProject(fourth);
    recent = editor::settings::recentProjects();
    check(recent.size() == 3 && recent[2] == pth::absolute(second), "a fourth pushes the oldest out");
    editor::settings::rememberProject(second);
    recent = editor::settings::recentProjects();
    check(recent.size() == 3 && recent[0] == pth::absolute(second) && recent[1] == pth::absolute(fourth),
          "and one opened again comes to the front, once");

    // A CCS project opened by its own .ccsproject is the project in that folder: one entry, not
    // two, and one with a name - the Windows menu showed it blank, a name that is all extension.
    std::string ccs = pth::join(home, "SampleExt");
    pth::makeDirectories(ccs);
    writeSource(pth::join(ccs, ".ccsproject"), "<?xml version=\"1.0\"?>\n");
    editor::settings::rememberProject(ccs);
    editor::settings::rememberProject(pth::join(ccs, ".ccsproject"));
    recent = editor::settings::recentProjects();
    check(recent.size() == 3 && recent[0] == pth::absolute(ccs) && recent[1] == pth::absolute(second),
          "a CCS project opened by its .ccsproject is remembered as its folder, once");

    sayWhereHomeIs(realHome);
    pth::removeTree(home);
}

// cc1 says it in two shapes, and the editor only ever read one of them: a missing header - the most
// ordinary mistake there is - left the caret where it was and the console to be read by eye.
void theOtherShapeOfDiagnostic() {
    std::printf("the second shape a diagnostic comes in\n");

    // What cc1's preprocessor actually writes, caret line and all.
    std::string said =
        "/tmp/p/src/main.c:1: #include \"shapes.h\"\n"
        "                               ^ cannot find \"shapes.h\" - looked in /tmp/p/src\n";
    editor::Diagnostic d = editor::parseDiagnostic(said);
    check(d.present, "a preprocessor diagnostic is read at all");
    checkEqual(d.file, "/tmp/p/src/main.c", "with the file it is about");
    check(d.line == 1, "and the line");
    check(d.col > 1, "and a column worked out from where the caret points");
    check(d.message.find("cannot find") != std::string::npos, "and what it says");

    // A Windows path has a colon after the drive letter that means nothing
    // here, and the line number is still the last one.
    std::string onWindows =
        "C:\\work\\src\\main.c:7: #include \"gone.h\"\n"
        "                              ^ cannot find \"gone.h\"\n";
    editor::Diagnostic w = editor::parseDiagnostic(onWindows);
    check(w.present, "a Windows path is read too");
    checkEqual(w.file, "C:\\work\\src\\main.c", "with the drive letter kept");
    check(w.line == 7, "and the right line");

    // The first shape still wins where both could be read.
    editor::Diagnostic ordinary =
        editor::parseDiagnostic("/tmp/a.c:3:9: error: no such thing\n");
    check(ordinary.present && ordinary.line == 3 && ordinary.col == 9,
          "the shape with a severity word still reads as it did");

    // A caret with nothing after it is somebody underlining, not an error.
    editor::Diagnostic bare = editor::parseDiagnostic("/tmp/a.c:3: int x = ;\n        ^\n");
    check(!bare.present, "a caret with nothing to say is not a diagnostic");
}

// A link that fails is a compiler that ran, and used to be reported as one that could not be
// started: "ld: symbol(s) not found" contains "not found", and the advice that followed - name it
// with --cc1, put it on PATH - sent anybody who read it looking in the wrong place.
void whereTheProgramCannotGo() {
    std::printf("a build whose program cannot be written where it would go\n");

    // The installer's examples\ under Program Files, as a normal user sees
    // it: readable, not writable. The portable stand-in is a directory that
    // is a file - nothing can be made inside one anywhere.
    std::string dir = editor::path::join(editor::path::tempDir(), "ride-unwritable-test");
    editor::path::removeTree(dir);
    editor::path::makeDirectories(dir);
    std::string notADir = editor::path::join(dir, "examples");
    writeSource(notADir, "a file standing where the project directory would be\n");
    std::string source = editor::path::join(dir, "hello.c");
    writeSource(source, "int main(void) { return 0; }\n");
    std::string program = editor::path::join(notADir, "hello.exe");

    editor::Toolchain tool;
    tool.cc1 = "cc1-that-must-not-run";
    std::vector<std::string> sources(1, source);
    editor::Built one = editor::buildTarget(tool, editor::ToolCc1, sources, editor::LangC,
                                            editor::hostArch(), editor::ConfigRelease, program);
    check(!one.ok, "a target whose program directory cannot be written is refused");
    check(one.output.find(notADir) != std::string::npos, "naming the directory");
    check(one.output.find("copy the project") != std::string::npos, "and saying what to do");
    check(one.output.find("could not be run") == std::string::npos &&
              one.output.find("LNK") == std::string::npos,
          "before any compiler or linker runs, so neither is blamed");

    std::vector<editor::Part> parts(1);
    parts[0].group = "Sources";
    parts[0].lang = editor::LangC;
    parts[0].sources = sources;
    editor::Built whole = editor::buildParts(tool, parts, editor::hostArch(), editor::ConfigRelease, program);
    check(!whole.ok && whole.output.find("cannot be written to") != std::string::npos,
          "a project build is refused the same way");

    editor::Built alone = editor::buildProgram(tool, editor::ToolCc1, source, editor::LangC,
                                               editor::hostArch(), editor::ConfigRelease);
    check(alone.ok || alone.output.find("cannot be written to") == std::string::npos,
          "and a file beside a writable directory is not");

    editor::path::removeTree(dir);
}

void whatALinkFailureSays() {
    std::printf("what a link failure is called\n");

    const char* cc1 = std::getenv("CC1");
    if (cc1 && *cc1 && !editor::path::exists(cc1)) {
        skipped("no cc1 at %s, so nothing is linked", cc1);
        return;
    }
    if (!cc1 || !*cc1) {
        skipped("no $CC1, so nothing is linked");
        return;
    }

    std::string dir = editor::path::join(editor::path::tempDir(), "ride-link-test");
    editor::path::removeTree(dir);
    editor::path::makeDirectories(dir);
    std::string source = editor::path::join(dir, "calls.c");
    writeSource(source,
                "extern int area(int side);\n"
                "int main(void) { return area(2); }\n");

    editor::Toolchain tool;
    tool.cc1 = cc1;
    // buildProgram, not build: the second stops at compiling, and a link that
    // never happens cannot fail.
    editor::Built made = editor::buildProgram(tool, editor::ToolCc1, source, editor::LangC,
                                              editor::hostArch(), editor::ConfigDebug);
    check(!made.ok, "a function declared and never defined does not link");
    check(made.output.find("could not be run") == std::string::npos,
          "and the compiler is not blamed for not being installed");
    check(made.output.find("area") != std::string::npos,
          "the console names the symbol nothing defined");

    editor::removeProgram(made);
    editor::path::removeTree(dir);
}

// The manual's contents, against the manual. Help > Contents lists the pages by name and where each
// is, and nothing stops the two parting company but this: a page renamed in help/ and not here
// points at nothing, one added there and not here is invisible - the house bug, and one check.
void theManualsContents() {
    std::printf("the manual, and what the editor says is in it\n");

    if (!editor::path::isDirectory("help")) {
        skipped("no help/ from here, so the manual is not checked");
        return;
    }

    const std::vector<editor::help::Page>& pages = editor::help::pages();
    check(pages.size() >= 13, "there are the ten pages, the languages and the appendix");

    // Every page the editor names is on disk.
    size_t missing = 0;
    for (size_t i = 0; i < pages.size(); ++i) {
        std::string where = editor::path::join("help", pages[i].file);
        if (!editor::path::exists(where)) {
            std::printf("  the editor names help/%s and it is not there\n", pages[i].file);
            ++missing;
        }
    }
    check(missing == 0, "every page Help > Contents names is a file that exists");

    // And every page on disk is named by the editor. README.md is the contents
    // in Markdown and is not itself a page.
    size_t unlisted = 0;
    std::vector<editor::path::Entry> inside = editor::path::entries("help");
    for (size_t i = 0; i < inside.size(); ++i) {
        const std::string& leaf = inside[i].name;
        if (leaf.size() < 3 || leaf.substr(leaf.size() - 3) != ".md") continue;
        if (leaf == "README.md") continue;
        bool named = false;
        for (size_t p = 0; p < pages.size(); ++p)
            if (leaf == pages[p].file) named = true;
        if (!named) {
            std::printf("  help/%s exists and Help > Contents does not name it\n", leaf.c_str());
            ++unlisted;
        }
    }
    check(unlisted == 0, "and every page on disk is one the editor names");

    // The panel is eighty columns wide and the contents has to fit it. A title
    // long enough to push the third column off the edge is caught here rather
    // than by somebody noticing a ragged screen.
    std::vector<std::string> said = editor::help::contents();
    size_t longest = 0;
    for (size_t i = 0; i < said.size(); ++i)
        if (said[i].size() > longest) longest = said[i].size();
    check(longest <= 74, "and none of its lines is too wide for the panel");

    check(said[0].find(editor::about::version()) != std::string::npos,
          "the contents names the version, so a printed page and the editor agree");
}

// Help > Environment: one row per thing a build reaches for, each saying where its answer came
// from, and CCS's compiler named when it is detected rather than told.
void environmentReport() {
    std::printf("Help > Environment, and where each answer came from\n");
    std::vector<std::string> said = editor::about::environment();
    std::string all;
    for (size_t i = 0; i < said.size(); ++i) all += said[i] + "\n";
    const char* rows[] = {"settings.json", "c90", "cpp11", "shalimar", "c2s", "vm6747", "asm6x",
                          "assembler", "linker", "Visual Studio", "TI compiler", "C6000 linker", "runtime",
                          "cpp11 headers", "c90 headers"};
    int missing = 0;
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i)
        if (all.find("  " + std::string(rows[i])) == std::string::npos) {
            std::printf("  no row for %s\n", rows[i]);
            ++missing;
        }
    check(missing == 0, "a row for every tool, header directory and runtime");
    int unexplained = 0;
    for (size_t i = 0; i < said.size(); ++i)
        if (said[i].compare(0, 2, "  ") == 0 && said[i].find("   (") == std::string::npos) ++unexplained;
    check(unexplained == 0, "and every row says where its answer came from");
    const std::string detected = editor::settings::detectedTi();
    const std::string bin = editor::path::join(detected, "bin");
    check(detected.empty() || editor::path::exists(editor::path::join(bin, "lnk6x")) ||
              editor::path::exists(editor::path::join(bin, "lnk6x.exe")),
          "a detected TI compiler has its lnk6x");
    if (editor::settings::namedTi().empty())
        check(editor::settings::ti() == detected, "with none named, the TI compiler used is the one detected");
}

// A compiler per group: what the .pro says, what survives being written back, and the two commands
// that used to be one.
void aCompilerPerGroup() {
    std::printf("a compiler per group, and the link at the end\n");

    file::path dir = file::temp_directory_path() / "ride-per-group-test";
    file::remove_all(dir);
    file::create_directories(dir);

    writeSource((dir / "main.c").string(), "int main(void) { return 0; }\n");
    writeSource((dir / "engine.cpp").string(), "int spin(void) { return 1; }\n");

    // Two spellings of a group, and the plain one is not deprecated: a project
    // written before any of this has to keep working, and has to keep looking
    // the way its author left it after the editor saves it.
    writeSource((dir / "project.pro").string(),
                "{\n  \"name\": \"mix\",\n"
                "  \"groups\": {\n"
                "    \"Sources\": [\"main.c\"],\n"
                "    \"Engine\": { \"files\": [\"engine.cpp\"], \"toolchain\": \"cl\" }\n"
                "  },\n"
                "  \"build\": { \"target\": \"mix\", \"groups\": [\"Sources\", \"Engine\"] }\n}\n");

    std::string error;
    editor::Project project;
    check(project.load(dir.string(), error), "a project with a group that names a compiler loads");
    check(project.toolchainFor("Sources") == editor::ToolAuto,
          "a group that says nothing about a compiler says nothing");
    check(project.toolchainFor("Engine") == editor::ToolMsvc, "and one that names cl means cl");

    std::string why, detail;
    std::vector<editor::Part> parts;
    check(project.targetParts(parts, why, &detail), "the target comes back in parts");
    check(parts.size() == 2, "one per group");
    checkEqual(parts[0].group, "Sources", "in the order the build entry names them");
    checkEqual(parts[1].group, "Engine", "not the order the groups happen to be written in");

    editor::Toolchain tool;
    check(editor::toolchainOf(tool, parts[0]) == editor::ToolCc1,
          "the group that named nothing goes by its language");
    check(editor::toolchainOf(tool, parts[1]) == editor::ToolMsvc,
          "and the group that named cl goes to cl");

    // The editor's own override still beats both, which is what --cc1 and the
    // Language menu are: a group's word is a default for the file, not a lock.
    editor::Toolchain forced;
    forced.kind = editor::ToolCc1;
    check(editor::toolchainOf(forced, parts[1]) == editor::ToolMsvc,
          "a group that named its compiler keeps it");

    // Written back: the plain group stays plain, the named one keeps its name.
    check(project.save(error), "it saves");
    std::string written = readWholeFile((dir / "project.pro").string());
    check(written.find("\"Sources\"") != std::string::npos, "Sources is still there");
    // "msvc", not "cl": both are read and msvc is what the project file has always written for that
    // compiler, at the top level as well as here. One word out and one word in beats a file whose
    // spelling depends on which version of the editor last saved it.
    check(written.find("\"toolchain\": \"msvc\"") != std::string::npos,
          "and the group that named cl still names it, in the word the file writes");
    editor::Project again;
    check(again.load(dir.string(), error), "and reads back");
    check(again.toolchainFor("Engine") == editor::ToolMsvc, "with the compiler where it was");
    check(again.toolchainFor("Sources") == editor::ToolAuto,
          "and the other group no more opinionated than it was");

    // The two commands. objectRecipe stops at objects and names them; the link
    // is the editor's own, because no compiler here takes an object as an
    // input - hand cc1 a .o and it reads it as C.
    {
        std::vector<std::string> sources;
        sources.push_back((dir / "main.c").string());
        std::vector<std::string> objects;
        editor::Recipe compiled = editor::objectRecipe(
            tool, editor::ToolCc1, sources, editor::LangC, editor::hostArch(),
            editor::ConfigDebug, dir.string(), objects);
        check(objects.size() == 1, "one object per source");
        check(objects[0].find("main.o") != std::string::npos, "named after the source");
        check(compiled.command.find("-c") != std::string::npos, "and the command stops at it");
        // cc1 -c writes into the current directory and has no flag for another,
        // so the compiler is run from where the objects are meant to go.
        check(compiled.command.find("cd ") != std::string::npos,
              "run from the object directory, which is the only way to place them");

        editor::Recipe linked = editor::linkRecipe(tool, objects, false, editor::hostArch(),
                                                   editor::ConfigDebug,
                                                   (dir / "mix").string());
        check(linked.command.find("main.o") != std::string::npos, "the link names the objects");
        check(linked.command.find("mix") != std::string::npos, "and where the program goes");
        checkEqual(linked.assemblyPath, (dir / "mix").string(),
                   "which is what the recipe says it produced");
#ifdef _WIN32
        check(linked.command.find("libcmtd.lib") != std::string::npos,
              "with the C runtime named, since cc1's objects do not name one");
        check(linked.command.find("legacy_stdio_definitions") != std::string::npos,
              "including the one printf needs under the UCRT");
#else
        check(linked.command.find("-lm") != std::string::npos,
              "with -lm always, which is what cc1's own driver does");
        check(linked.command.find(" -g") != std::string::npos,
              "and -g under debug - the .dSYM itself is buildParts' own step, since a "
              "link of objects alone never runs dsymutil");
#endif
    }

    file::remove_all(dir);
}

// The window's one rule about statics, held where every machine can check it: in the mixed image a
// function-local static with a destructor corrupts the heap when first reached - the atexit
// registration, not the object - and the cure is bridge.cpp's scratch(): made once with new, never destroyed. Found on the window's first day and again on 2026-09-11; checked over the files RIDEGui.vcxproj compiles, read out of that file.
void theWindowsRuleAboutStatics() {
    std::printf("no static with a destructor in what the window compiles\n");

    if (!editor::path::isDirectory("winforms")) {
        skipped("no winforms/ from here, so the window's sources are not scanned");
        return;
    }
    std::string project = readWholeFile("winforms/RIDEGui.vcxproj");
    check(!project.empty(), "the window's project can be read");

    std::vector<std::string> sources;
    const std::string mark = "<ClCompile Include=\"";
    for (size_t at = project.find(mark); at != std::string::npos; at = project.find(mark, at + 1)) {
        size_t from = at + mark.size();
        size_t to = project.find('"', from);
        if (to == std::string::npos) break;
        std::string named = project.substr(from, to - from);
        for (size_t i = 0; i < named.size(); ++i)
            if (named[i] == '\\') named[i] = '/';
        sources.push_back(named);
    }
    check(sources.size() >= 20, "and names the core it compiles");

    int offenders = 0;
    for (size_t i = 0; i < sources.size(); ++i) {
        std::string text = readWholeFile("winforms/" + sources[i]);
        check(!text.empty(), "each file it names can be read: " + sources[i]);
        size_t line = 1;
        size_t start = 0;
        while (start < text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) end = text.size();
            std::string one = text.substr(start, end - start);
            size_t word = one.find_first_not_of(" \t");
            if (word != std::string::npos && word > 0 && one.compare(word, 7, "static ") == 0) {
                std::string rest = one.substr(word + 7);
                if (rest.compare(0, 6, "const ") == 0) rest = rest.substr(6);
                bool classType = rest.compare(0, 5, "std::") == 0 ||
                                 rest.compare(0, 8, "editor::") == 0 ||
                                 rest.compare(0, 10, "shalimar::") == 0;
                if (classType && rest.find('*') == std::string::npos &&
                    rest.find('&') == std::string::npos) {
                    ++offenders;
                    std::printf("  %s:%zu: %s\n", sources[i].c_str(), line, one.c_str());
                }
            }
            line += 1;
            start = end + 1;
        }
    }
    check(offenders == 0, "and none of them holds a function-local static of class type");

    // The same death from a header: <fstream> in debugger.cpp (2026-10-13) killed the window on every
    // start, 0xC0000374 or 0xE0434352, until the file was read with <cstdio>. No iostream there.
    int streams = 0;
    const char* const banned[] = { "<fstream>", "<iostream>", "<sstream>", "<istream>", "<ostream>" };
    for (size_t i = 0; i < sources.size(); ++i) {
        std::string text = readWholeFile("winforms/" + sources[i]);
        for (size_t b = 0; b < sizeof banned / sizeof banned[0]; ++b)
            if (text.find(std::string("#include ") + banned[b]) != std::string::npos) {
                ++streams;
                std::printf("  %s includes %s\n", sources[i].c_str(), banned[b]);
            }
    }
    check(streams == 0, "and none of them includes an iostream header");
}

// The fourth compiler, driven for real. Everything above about cxx1 is the editor's own rules and
// runs no compiler; this does, when $CXX1 names one: a file built and run, the two-compiler
// project, and a stop inside the C++ half under the machine's debugger - the reading of cxx1's DWARF the Debug tab depends on.
void theFourthCompiler() {
    std::printf("cxx1, driven for real\n");

    const char* cxx1 = std::getenv("CXX1");
    if (cxx1 && *cxx1 && !editor::path::exists(cxx1)) {
        skipped("no cxx1 at %s, so nothing is built", cxx1);
        return;
    }
    if (!cxx1 || !*cxx1) {
        skipped("no $CXX1, so nothing is built");
        return;
    }
    const std::string host = editor::hostArch();

    std::string dir = editor::path::join(editor::path::tempDir(), "ride-cxx1-test");
    editor::path::removeTree(dir);
    editor::path::makeDirectories(dir);
    std::string source = editor::path::join(dir, "owned.cpp");
    // Plain C++11 with a class in it, so that it is C++ cxx1 is being asked
    // for and not C that happens to end in .cpp.
    writeSource(source,
                "#include <cstdio>\n"
                "\n"
                "class Counter {\n"
                "public:\n"
                "    Counter() : n_(0) {}\n"
                "    void add(int by) { n_ = n_ + by; }\n"
                "    int value() const { return n_; }\n"
                "private:\n"
                "    int n_;\n"
                "};\n"
                "\n"
                "int main()\n"
                "{\n"
                "    Counter c;\n"
                "    for (int i = 1; i <= 3; ++i)\n"
                "        c.add(i);\n"
                "    std::printf(\"counted %d\\n\", c.value());\n"
                "    return c.value();\n"
                "}\n");

    editor::Toolchain tool;
    tool.cxx1 = cxx1;

    // Ctrl-B: the assembly, for the tab.
    editor::Build made = editor::build(tool, editor::ToolCxx1, source, editor::LangCpp, host,
                                       editor::ConfigDebug);
    check(made.ok, "cxx1 compiles the file to assembly");
    check(!made.asmLines.empty(), "and the Assembly tab has something to show");
    // cxx1 says who it is on stderr before every compile, and the editor shows what the compiler
    // said, banner included - cxx1's own behaviour, not edited here (decided 2026-09-11, after -nologo
    // had been passed for a day). What matters is that the banner is not read as a diagnostic, since it sits where an error would.
    check(!made.diag.present, "with nothing read as a diagnostic - its banner is not one");
    check(made.output.find("C++11") != std::string::npos,
          "and the banner is in the console, as cxx1 wrote it");
    check(editor::assemblyRecipe(tool, editor::ToolCxx1, source, editor::LangCpp, host,
                                 editor::ConfigDebug).command.find("nologo") == std::string::npos,
          "because the editor passes no -nologo");

    // F5: built, and run, with what it printed and what it returned.
    editor::Ran ran = editor::runProgram(tool, editor::ToolCxx1, source, editor::LangCpp, host,
                                         editor::ConfigDebug);
    check(ran.built && ran.ran, "cxx1 builds a program from it, and it runs");
    check(ran.output.find("counted 6") != std::string::npos, "printing what it printed");
    check(ran.status == 6, "and returning what it returned");

    // A diagnostic lands where cc1's does: file, line, column.
    std::string broken = editor::path::join(dir, "broken.cpp");
    writeSource(broken, "int main()\n{\n    int x = ;\n    return x;\n}\n");
    editor::Build bad = editor::build(tool, editor::ToolCxx1, broken, editor::LangCpp, host,
                                      editor::ConfigDebug);
    check(!bad.ok && bad.diag.present, "an error is an error");
    check(bad.diag.line == 3, "on the line cxx1 named");
    check(bad.diag.file.find("broken.cpp") != std::string::npos, "in the file it named");

    // F4 on a project of both: C to cc1, C++ to cxx1, one link at the end.
    // This is the case 3.0 is for - a target holding C and C++ where neither
    // half goes to the host's compiler.
    const char* cc1 = std::getenv("CC1");
    if (cc1 && *cc1 && editor::path::exists(cc1)) {
        tool.cc1 = cc1;
        std::string cpart = editor::path::join(dir, "twice.c");
        std::string cpppart = editor::path::join(dir, "main.cpp");
        writeSource(cpart, "int twice(int n) { return n * 2; }\n");
        writeSource(cpppart,
                    "extern \"C\" int twice(int n);\n"
                    "int main()\n"
                    "{\n"
                    "    int total = 0;\n"
                    "    for (int i = 1; i <= 3; ++i)\n"
                    "        total = total + twice(i);\n"
                    "    return total;\n"
                    "}\n");
        std::vector<editor::Part> parts(2);
        parts[0].group = "C";
        parts[0].lang = editor::LangC;
        parts[0].sources.push_back(cpart);
        parts[1].group = "C++";
        parts[1].lang = editor::LangCpp;
        parts[1].sources.push_back(cpppart);
        check(editor::toolchainOf(tool, parts[0]) == editor::ToolCc1 &&
                  editor::toolchainOf(tool, parts[1]) == editor::ToolCxx1,
              "a mixed target goes to cc1 and cxx1, by language, with nothing named");

        // Named the way Project::targetProgram names one: with .exe on Windows, since cmd will not
        // run a file that has no extension - a link into "both" there succeeds and the run then
        // fails, which is not the link's fault and reads exactly as if it were.
#ifdef _WIN32
        const char* bothName = "both.exe";
#else
        const char* bothName = "both";
#endif
        editor::Built both = editor::buildParts(tool, parts, host, editor::ConfigDebug,
                                                editor::path::join(dir, bothName));
        check(both.ok, "and the two compile and link into one program");
        if (both.ok) {
            editor::Ran together = editor::runBuilt(both.program);
            check(together.ran && together.status == 12,
                  "which runs, the C++ half having called the C half");
        }

        // And stopped in the C++ half, on a target cxx1 writes DWARF for.
        if (editor::dbg_for(editor::ToolCxx1, host) != editor::DebuggerNone && both.ok) {
            editor::Debugger debugger;
            check(debugger.start(editor::dbg_for(editor::ToolCxx1, host), both.program),
                  "the debugger starts on it");
            if (debugger.running()) {
                check(debugger.breakAt(cpppart, 6), "a breakpoint on a line of the C++");
                editor::Stop at = debugger.run();
                check(at.stopped && at.line == 6, "and running stops on it");
                check(at.function == "main", "in the function that line is in");
                std::vector<editor::Variable> locals = debugger.locals();
                bool sawTotal = false;
                for (size_t i = 0; i < locals.size(); ++i)
                    if (locals[i].name == "total") sawTotal = true;
                check(sawTotal, "with the C++ local read out of cxx1's DWARF");
                debugger.stop();
            }
        } else if (both.ok) {
            skipped("%s", editor::dbg_whyNot(editor::ToolCxx1, host).c_str());
        }
        editor::removeProgram(both);
    } else {
        skipped("no $CC1, so the mixed project is not built");
    }

    editor::path::removeTree(dir);
}

// A directory handed to cl's /Fo, which wants a separator on the end - and on the machine where
// /Fo means anything that separator is a backslash, sitting before a closing quote where it escapes
// it. cl then answers "D8003: missing source filename", which reads as a command with no file in it. An afternoon on the Windows box; one check here.
void aDirectoryInAQuotedArgument() {
    std::printf("a directory on the end of a quoted argument\n");

    editor::Toolchain tool;
    std::vector<std::string> sources;
    sources.push_back("src\\one.cpp");
    std::vector<std::string> objects;
    editor::Recipe recipe = editor::objectRecipe(
        tool, editor::ToolMsvc, sources, editor::LangCpp, "x86_64-windows",
        editor::ConfigDebug, "C:\\work\\objs", objects);

    // Only where the separator is a backslash - which is only where cl is.
#ifdef _WIN32
    check(recipe.command.find("\\\\\"") != std::string::npos,
          "a trailing backslash is doubled, so it cannot escape the closing quote");
    check(recipe.command.find("one.cpp") != std::string::npos,
          "and the source is still an argument of its own");
#else
    check(recipe.command.find("one.cpp") != std::string::npos,
          "the source is named; the quoting question is the Windows separator's");
#endif
}

// What a project says it builds, and the three ways it can say something that cannot be built. The
// compiling itself is the session suite's job; this is about the reading and the refusing, which is
// where the rules live.
void whatTheProjectBuilds() {
    std::printf("what a project says it builds\n");

    file::path dir = file::temp_directory_path() / "ride-target-test";
    file::remove_all(dir);
    file::create_directories(dir);

    writeSource((dir / "project.pro").string(),
              "{\n"
              "  \"name\": \"sums\",\n"
              "  \"groups\": {\n"
              "    \"Sources\": [\"add.c\", \"main.c\"],\n"
              "    \"Headers\": [\"add.h\"],\n"
              "    \"Notes\": [\"README.md\"]\n"
              "  },\n"
              "  \"build\": { \"target\": \"sums\", \"groups\": [\"Sources\"] }\n"
              "}\n");

    // The files themselves, because a project that lists what is not there is
    // refused now - which is the case a few checks further down.
    writeSource((dir / "add.c").string(), "int add(int a, int b) { return a + b; }\n");
    writeSource((dir / "main.c").string(), "int main(void) { return 0; }\n");
    writeSource((dir / "add.h").string(), "int add(int a, int b);\n");
    writeSource((dir / "README.md").string(), "notes\n");

    editor::Project project;
    std::string error;
    check(project.load(dir.string(), error), "a project with a build entry loads");
    check(project.builds(), "and says it builds something");
    checkEqual(project.target().name, "sums", "under the name it gives");

    std::vector<std::string> sources;
    editor::Language lang = editor::LangPlain;
    std::string why, detail;
    check(project.targetSources(sources, lang, why, &detail), "its sources come back");
    check(sources.size() == 2, "the two in the group it named");
    check(lang == editor::LangC, "in the language they are in");
    check(why.empty(), "with nothing to complain about");
    check(sources[0].find("add.c") != std::string::npos, "in the order the group has them");

    // The header is in the project to be opened, not to be compiled, and the
    // group of notes is not source at all.
    for (size_t i = 0; i < sources.size(); ++i)
        check(sources[i].find(".h") == std::string::npos, "and no header is handed to a compiler");

    // Both languages at once is the one worth refusing: cc1 compiles the C and
    // cl compiles the C++, and neither can be given the other's files.
    editor::Target both;
    both.name = "mixed";
    both.groups.push_back("Sources");
    project.setTarget(both);
    writeSource((dir / "extra.cpp").string(), "int twice(int n) { return n * 2; }\n");
    check(project.addFile("extra.cpp", "Sources"), "a C++ file joins the group");
    check(!project.targetSources(sources, lang, why, &detail), "and the build is refused");
    check(why.find("C++") != std::string::npos, "saying which two languages");
    check(!detail.empty(), "with the rest of it for the console");
    check(sources.empty(), "and nothing handed back to compile");

    // A file the project lists and the disk has not got. The compiler would
    // say "cannot open" with no line to go to and nothing about the project;
    // this is a fault in the configuration and the editor holds the list.
    editor::Target onlyThere;
    onlyThere.name = "gone";
    onlyThere.groups.push_back("Absent");
    project.setTarget(onlyThere);
    check(project.addFile("nowhere.c", "Absent"), "a file is listed that is not on disk");
    check(!project.targetSources(sources, lang, why, &detail),
          "and the build is refused before a compiler runs");
    check(why.find("nowhere.c") != std::string::npos, "naming the file that is not there");
    check(why.find("not on disk") != std::string::npos, "and saying what is wrong with it");
    check(detail.find("project.pro") != std::string::npos, "with where the list lives");
    check(sources.empty(), "and nothing handed back to compile");

    // A group that is not there, and a target with no source in it.
    editor::Target missing;
    missing.name = "sums";
    missing.groups.push_back("Nowhere");
    project.setTarget(missing);
    check(!project.targetSources(sources, lang, why, &detail), "an unknown group is refused");
    check(why.find("Nowhere") != std::string::npos, "and named");

    editor::Target empty;
    empty.name = "sums";
    empty.groups.push_back("Notes");
    project.setTarget(empty);
    check(!project.targetSources(sources, lang, why, &detail), "a group with no source is refused");

    // Nothing said at all means the Sources group, since 2026-09-16 - the
    // window wrote every project without a build entry until then. An
    // empty Sources is then a refusal with a reason, not a silence.
    editor::Project quiet;
    file::path bare = file::temp_directory_path() / "ride-target-bare";
    file::remove_all(bare);
    file::create_directories(bare);
    writeSource((bare / "project.pro").string(),
                "{ \"name\": \"quiet\", \"groups\": { \"Sources\": [] } }\n");
    check(quiet.load(bare.string(), error), "a project with no build entry still loads");
    check(quiet.builds(), "and builds its Sources group by default");
    check(!quiet.targetSources(sources, lang, why, &detail), "which, empty, hands nothing back");
    check(!why.empty(), "and it says so rather than saying nothing");
    // Said and empty is a different thing: that project builds nothing.
    writeSource((bare / "project.pro").string(),
                "{ \"name\": \"quiet\", \"groups\": { \"Sources\": [\"a.c\"] }, \"build\": {} }\n");
    check(quiet.load(bare.string(), error) && !quiet.builds(), "a build entry that names no group builds nothing");

    // What it says it builds survives being written and read again.
    editor::Target kept;
    kept.name = "sums";
    kept.groups.push_back("Sources");
    project.setTarget(kept);
    check(project.save(error), "the project writes itself back");

    editor::Project again;
    check(again.load(dir.string(), error), "and reads again");
    check(again.builds(), "still building");
    checkEqual(again.target().name, "sums", "the same program");
    check(again.target().groups.size() == 1 && again.target().groups[0] == "Sources",
          "out of the same groups");

    file::remove_all(dir);
    file::remove_all(bare);
}

// Shalimar: the third language, and the one that is not C with fewer rules.
// Two of its punctuation marks would be read wrongly by a C scanner, and the
// first of them silently - which is what most of this is about.
void theThirdLanguage() {
    std::printf("Shalimar, as a language the editor knows\n");

    check(editor::languageFor("a.shl") == editor::LangShalimar, ".shl is Shalimar");

    // .shm was Shalimar here until 2026-08-23. The phone app writes that suffix, but it is not
    // accepted everywhere a Shalimar file has to go, so the editor knows one name for the language
    // and the Language menu is what opens an app-written file without renaming it first.
    check(editor::languageFor("a.shm") == editor::LangPlain,
          ".shm is not, since the editor knows one suffix for Shalimar");
    check(editor::languageFor("a.SHL") == editor::LangShalimar, "whatever the case");
    check(editor::languageFor("a.sh") == editor::LangPlain,
          "a shell script is not, however close the name looks");
    checkEqual(editor::languageName(editor::LangShalimar), "Shalimar", "and it is named");

    {
        editor::SyntaxState state;
        std::string line = "  fun <real> = area(w: real, h: real) {";
        std::vector<unsigned char> k = editor::highlight(line, editor::LangShalimar, state);
        check(k[2] == editor::KindKeyword, "fun is a keyword");
        check(k[7] == editor::KindType, "and real is a type");
    }
    {
        editor::SyntaxState state;
        std::vector<unsigned char> k =
            editor::highlight("? prec(12) sqrt(x) pi", editor::LangShalimar, state);
        check(k[0] == editor::KindPreproc, "'?' is a command");
        check(k[2] == editor::KindPreproc, "prec before a bracket is a directive");
        check(k[11] == editor::KindType, "sqrt is a built-in");
        check(k[19] == editor::KindNumber, "and pi is a constant, which is a value");
    }
    {
        editor::SyntaxState state;
        std::vector<unsigned char> k =
            editor::highlight("?? A.row prec", editor::LangShalimar, state);
        check(k[0] == editor::KindPreproc && k[1] == editor::KindPreproc,
              "'?\\?' is one command and not two");
        check(k[5] == editor::KindType, "row after a dot is an attribute");
        check(k[9] != editor::KindPreproc,
              "and prec with no bracket is an ordinary name");
    }
    {
        // No escapes: the first closing quote ends the literal, so what
        // follows a backslash-quote is code and not more string.
        editor::SyntaxState state;
        std::vector<unsigned char> k =
            editor::highlight("s : \"a\\\" + b", editor::LangShalimar, state);
        check(k[4] == editor::KindString, "a literal opens");
        check(k[7] == editor::KindString, "and closes at the first quote after it");
        check(k[9] != editor::KindString, "leaving what follows as code");
    }
    {
        // The one that matters. 'x : 5' is an assignment in Shalimar and a
        // goto label in C, and a label is laid out in the function's own
        // column - so read as C, every assignment walks left.
        editor::IndentStyle shalimar;
        shalimar.dialect = editor::DialectShalimar;
        std::vector<std::string> lines;
        lines.push_back("fun <> = main() {");
        lines.push_back("int n : 5");
        lines.push_back("if n < 2 {");
        lines.push_back("n : n + 1");
        lines.push_back("}");
        lines.push_back("}");
        std::vector<std::string> out = editor::reindent(lines, shalimar);
        checkEqual(out[1], "    int n : 5", "a declaration sits one step in");
        checkEqual(out[3], "        n : n + 1", "and an assignment inside an if, two");

        // Read as C, 'n : n + 1' is a goto label and goes in the function's own column - four
        // spaces where it belongs at eight. The declaration above it is safe either way, because
        // 'int n' is two words before the colon and a label is one.
        editor::IndentStyle asC;
        std::vector<std::string> wrong = editor::reindent(lines, asC);
        checkEqual(wrong[3], "    n : n + 1",
                   "which is what the C rules would have made of it");
    }
    {
        // No block comment, so a '/' is a divide and nothing is carried on.
        editor::IndentStyle style;
        style.dialect = editor::DialectShalimar;
        std::vector<std::string> lines;
        lines.push_back("fun <> = main() {");
        lines.push_back("x : 1. /* 2.");
        lines.push_back("y : 3.");
        lines.push_back("}");
        std::vector<std::string> out = editor::reindent(lines, style);
        checkEqual(out[2], "    y : 3.", "a line after a /* is laid out like any other");
    }

    // Routing, and the refusals that go with it.
    editor::Toolchain automatic;
    check(editor::resolve(automatic, editor::LangShalimar) == editor::ToolShc,
          "Shalimar goes to shc");
    check(editor::canCompile(editor::ToolShc, editor::LangShalimar), "which takes it");
    check(!editor::canCompile(editor::ToolShc, editor::LangC),
          "and takes nothing else");
    check(!editor::canCompile(editor::ToolCc1, editor::LangShalimar),
          "nor does cc1 take Shalimar");
    check(!editor::canCompile(editor::ToolMsvc, editor::LangShalimar), "nor cl");
    check(editor::usesArch(editor::ToolShc),
          "shc generates for the same three architectures cc1 does");

    // DWARF on the two GNU targets since D1 (2026-10-13); x86_64-windows is rung D4, not landed.
    check(editor::emitsDebugInfo(editor::ToolShc, "x86_64-linux") &&
              editor::emitsDebugInfo(editor::ToolShc, "arm64-darwin"),
          "shalimar writes DWARF for the two GNU targets (D1)");
    check(editor::emitsDebugInfo(editor::ToolShc, "x86_64-windows"),
          "and CodeView for x86_64-windows (D4)");
    check(editor::emitsDebugInfo(editor::ToolShc, "tms6747"),
          "and cl6x's .dwpsn for tms6747 (D5), as cpp11 writes");
    check(editor::debugsWithCodeView(editor::ToolShc, "x86_64-windows", editor::ConfigDebug),
          "so a Windows Debug build of shalimar goes through clang and link.exe /DEBUG");
    checkEqual(editor::assemblerFlag(editor::ToolShc, "x86_64-windows", editor::ConfigDebug), "",
               "with no -masm flag, which shalimar has not: its -g alone chooses the spelling");
    // A debug build links the runtime that can stop the program AND carries -g: --debug chooses the
    // archive, and only the debug one has any code in it for stopping the program; -g adds the sections
    // lldb and gdb read and changes no instruction.
    checkEqual(editor::configFlags(editor::ToolShc, editor::ConfigDebug, "arm64-darwin"),
               " --debug -g",
               "and a debug build links the runtime that can stop it, and asks for -g");
    checkEqual(editor::configFlags(editor::ToolShc, editor::ConfigRelease, "arm64-darwin"),
               "", "while release asks for nothing and cannot be stopped at all");
    checkEqual(editor::configFlags(editor::ToolShc, editor::ConfigRelease, "arm64-darwin"),
               "", "nor a release one - Shalimar has no preprocessor to define into");

    // What the Debug panel says: DWARF on the two GNU targets since D1, and for the other two that
    // there is none yet - rather than nothing, since a blank panel reads as a panel that is broken.
    {
        std::vector<std::string> said = editor::debugNote(editor::ToolShc, "arm64-darwin");
        std::string all;
        for (size_t i = 0; i < said.size(); ++i) all += said[i] + " ";
        check(all.find("writes DWARF") != std::string::npos && all.find("since D1") != std::string::npos,
              "the Debug panel says shc writes DWARF for arm64-darwin, since D1");
        said = editor::debugNote(editor::ToolShc, "tms6747");
        all.clear();
        for (size_t i = 0; i < said.size(); ++i) all += said[i] + " ";
        check(all.find(".dwpsn") != std::string::npos && all.find("D5") != std::string::npos,
              "and that it writes .dwpsn for tms6747, naming the rung");
    }

    // The command, and the one place it differs from cc1's.
    {
        editor::Toolchain tool;
        editor::Recipe recipe = editor::assemblyRecipe(
            tool, editor::ToolShc, "prog.shl", editor::LangShalimar, "x86_64-linux",
            editor::ConfigDebug);
        check(recipe.command.find("--target=x86_64-linux") != std::string::npos,
              "shc spells the target --target= where cc1 spells it -arch");
        check(recipe.command.find(" -g") == std::string::npos,
              "and is never asked for -g");
        check(recipe.assemblyPath.size() > 2 &&
                  recipe.assemblyPath.compare(recipe.assemblyPath.size() - 2, 2, ".s") == 0,
              "the assembly lands in a .s");

        editor::Recipe masm = editor::assemblyRecipe(
            tool, editor::ToolShc, "prog.shl", editor::LangShalimar, "x86_64-windows",
            editor::ConfigRelease);
        check(masm.assemblyPath.size() > 4 &&
                  masm.assemblyPath.compare(masm.assemblyPath.size() - 4, 4, ".asm") == 0,
              "except for the Windows target, where it is MASM and ml64 wants .asm");
    }

    // A project made of Shalimar is not the shape a project made of C is, and the difference is the
    // language's rather than the editor's: no include, no import, no way to name another file, and
    // shc takes one program at a time. Several .shl in a group are several programs.
    {
        editor::Project project;
        std::string error;
        std::string dir = editor::path::join(editor::path::tempDir(), "ride-shmproj");
        editor::path::removeTree(dir);
        editor::path::makeDirectories(editor::path::join(dir, "src"));
        writeSource(editor::path::join(dir, "src/hello.shl"),
                    "fun <> = main() {\n  ? 1\n}\n");
        writeSource(editor::path::join(dir, "src/other.shl"),
                    "fun <> = main() {\n  ? 2\n}\n");

        writeSource(editor::path::join(dir, "project.pro"),
                    "{\n  \"name\": \"hello\",\n"
                    "  \"build\": { \"target\": \"hello\", \"groups\": [\"Sources\"] },\n"
                    "  \"groups\": { \"Sources\": [\"src/hello.shl\"] }\n}\n");
        check(project.load(dir, error), "a project of one Shalimar source loads");

        std::vector<std::string> sources;
        editor::Language lang = editor::LangPlain;
        std::string why;
        check(project.targetSources(sources, lang, why),
              "and says what it builds");
        check(lang == editor::LangShalimar, "which is Shalimar");
        check(sources.size() == 1, "and is one source");

        // Two programs, and the target names one of them.
        writeSource(editor::path::join(dir, "project.pro"),
                    "{\n  \"name\": \"hello\",\n"
                    "  \"build\": { \"target\": \"hello\", \"groups\": [\"Sources\"] },\n"
                    "  \"groups\": { \"Sources\": [\"src/other.shl\", \"src/hello.shl\"] }\n}\n");
        editor::Project two;
        check(two.load(dir, error), "a project of two loads");
        sources.clear();
        check(two.targetSources(sources, lang, why), "and still says what it builds");
        check(sources.size() == 2, "keeping both, because the second is where to look");
        check(sources[0].find("hello") != std::string::npos,
              "with the target's own first, not the one listed first");

        // Two programs and a target named after neither: refused, not guessed.
        writeSource(editor::path::join(dir, "project.pro"),
                    "{\n  \"name\": \"hello\",\n"
                    "  \"build\": { \"target\": \"neither\", \"groups\": [\"Sources\"] },\n"
                    "  \"groups\": { \"Sources\": [\"src/other.shl\", \"src/hello.shl\"] }\n}\n");
        editor::Project neither;
        check(neither.load(dir, error), "a project naming neither loads");
        sources.clear();
        std::string detail;
        check(!neither.targetSources(sources, lang, why, &detail),
              "and is refused rather than guessed at");
        check(why.find("programs and builds one") != std::string::npos,
              "with a reason that says how many there were");
        check(detail.find("Every Shalimar file has a main()") != std::string::npos,
              "and a detail that says why the project has to choose");

        // Shalimar beside C is the refusal C beside C++ already had.
        writeSource(editor::path::join(dir, "src/bit.c"), "int bit(void) { return 1; }\n");
        writeSource(editor::path::join(dir, "project.pro"),
                    "{\n  \"name\": \"hello\",\n"
                    "  \"build\": { \"target\": \"hello\", \"groups\": [\"Sources\"] },\n"
                    "  \"groups\": { \"Sources\": [\"src/hello.shl\", \"src/bit.c\"] }\n}\n");
        editor::Project mixed;
        check(mixed.load(dir, error), "a project of two languages loads");
        sources.clear();
        check(!mixed.targetSources(sources, lang, why),
              "and is refused: no one compiler can make one program of them");
        check(why.find("one group") != std::string::npos,
              "naming the group, so the reader knows which list to split");

        // And in two groups it is refused for a different reason, the one that is not about the
        // editor: a Shalimar object exports the same three startup symbols whatever file it came
        // from, so two collide, and the language has no declarations to check a call across a link. Compiler-S/docs/LINKING.md, in full.
        writeSource(editor::path::join(dir, "project.pro"),
                    "{\n  \"name\": \"hello\",\n"
                    "  \"build\": { \"target\": \"hello\", \"groups\": [\"S\", \"C\"] },\n"
                    "  \"groups\": { \"S\": [\"src/hello.shl\"], \"C\": [\"src/bit.c\"] }\n}\n");
        editor::Project apart;
        check(apart.load(dir, error), "the same two in groups of their own load");
        std::vector<editor::Part> parts;
        check(!apart.targetParts(parts, why),
              "and are still refused, where C and C++ in two groups now are not");
        check(why.find("whole program") != std::string::npos,
              "for what a Shalimar object is, rather than for being a second language");

        editor::path::removeTree(dir);
    }

    // shc names neither the file nor the column, so the caller supplies one.
    {
        editor::Diagnostic d = editor::parseDiagnostic(
            "Error: line 45: Index 1 out of range 0...4\n", "prog.shl");
        check(d.present, "a Shalimar diagnostic is recognised");
        checkEqual(d.file, "prog.shl", "and takes the file it was asked about");
        check(d.line == 45 && d.col == 1, "the line it names, and the first column");
        checkEqual(d.message, "Index 1 out of range 0...4", "message");
    }
    {
        editor::Diagnostic d = editor::parseDiagnostic(
            "Warning: line 3: 'f' is never called\n", "prog.shl");
        check(!d.present, "a warning is not what the editor stands on");
    }
}

// The Shalimar session, driven against a real program. There is no debugger process here and
// nothing to install: the program stops itself, because the compiler already emits a call before
// every statement so a runtime error can name its line, and a debug build offers that same position to a session inside it.
void steppingShalimar() {
    std::printf("stopping a Shalimar program\n");

    const char* shc = std::getenv("SHC");
    if (!shc || !*shc) {
        skipped("no $SHC, so nothing is built to stop inside");
        return;
    }

    std::string dir = editor::path::join(editor::path::tempDir(), "ride-shm-step");
    editor::path::removeTree(dir);
    if (!editor::path::makeDirectories(dir)) {
        skipped("could not make %s - this case is about shc, not about that",
                    dir.c_str());
        return;
    }
    const std::string source = editor::path::join(dir, "steps.shl");
    writeSource(source,
                "fun <int> = twice(n: int) {\n"
                "  int d : n + n\n"
                "  return d\n"
                "}\n"
                "\n"
                "fun <> = main() {\n"
                "  int a : 1\n"
                "  int b : twice(a)\n"
                "  ? b\n"
                "}\n");

    // Checked, because writeSource does not: an ofstream that could not open
    // its file says nothing, and shc then reports "cannot read" and takes the
    // blame for a file this suite never wrote.
    if (!editor::path::exists(source)) {
        skipped("could not write %s - this case is about shc, not about that",
                    source.c_str());
        editor::path::removeTree(dir);
        return;
    }

    // .exe on Windows, and not because the compiler needs it: nothing there will *run* a file
    // without one, so a program named "steps" builds perfectly and then cannot be started. -o is
    // taken as given by shc, as it is by cc1, so the caller is the one that has to know this.
#ifdef _WIN32
    const std::string program = editor::path::join(dir, "steps.exe");
#else
    const std::string program = editor::path::join(dir, "steps");
#endif
    // Into a file rather than into nowhere, so that a build which does not work can say why.
    // Discarding it is how this case spent an afternoon reporting "shc did not build it" on the
    // machine where shc had only just been made to exist, with the reason thrown away every run.
    const std::string log = editor::path::join(dir, "build.log");
    const std::string build = std::string("\"") + shc + "\" \"" + source +
                              "\" --debug -o \"" + program + "\" > \"" + log +
                              "\" 2>&1";
    if (std::system(shellCommand(build).c_str()) != 0) {
        skipped("shc did not build it, so there is nothing to stop inside");
        std::printf("   %s\n", build.c_str());
        std::string said = readWholeFile(log);
        if (!said.empty()) std::printf("   it said: %s\n", said.c_str());
        editor::path::removeTree(dir);
        return;
    }

    shalimar::Session session;
    check(session.start(program), "the program starts, armed, and says it is ready");

    check(session.breakAt(source, 8), "a breakpoint is set by file and line");
    editor::Stop at = session.run();
    check(at.stopped, "and the program stops");
    check(at.line == 8, "on the line it was set on");
    check(editor::path::filename(at.file) == "steps.shl", "in the file it was set in");

    std::vector<editor::StackFrame> stack = session.frames();
    check(stack.size() == 1, "one frame: where it is standing");
    check(stack[0].function.find("1 call") != std::string::npos,
          "which says how deep it is, rather than inventing a name for it");

    editor::Stop into = session.stepInto();
    check(into.stopped && into.line == 2, "stepping into the call reaches its first line");

    // The line after the call, not the line of it: the call's own statement
    // was entered before the call was made, and stepping out looks for the
    // next statement shallower than where it is.
    editor::Stop out = session.stepOut();
    check(out.stopped && out.line == 9,
          "and stepping out comes back to the statement after the call");

    editor::Stop over = session.stepOver();
    check(over.exited, "carrying on from the last statement ends the program");
    check(over.said.find("2") != std::string::npos,
          "and what it printed came back with it");

    session.stop();

    // A release build has no code for any of this, so a session on one gets
    // a program that simply runs. That is the boundary, checked rather than
    // asserted.
    const std::string plain = editor::path::join(dir, "plain");
    const std::string release = std::string("\"") + shc + "\" \"" + source +
                                "\" -o \"" + plain + "\"" + kNowhere;
    if (std::system(shellCommand(release).c_str()) == 0) {
        shalimar::Session other;
        check(!other.start(plain),
              "a release build cannot be stopped: it has no code for it");
        other.stop();
    }

    editor::path::removeTree(dir);
}

// The same program stopped through the seam the window uses, rather than through the Session
// directly - which is the whole of what the window could not do. Everything here is a ride_ call
// and nothing names a C++ type, because that is the only vocabulary the form has.
void theWindowStoppingShalimar() {
    std::printf("the window stopping a Shalimar program\n");

    // The order the window has to ask in. A Shalimar program can stop itself, and does so where no
    // debugger reads it - x86_64-windows and tms6747 until rungs D4 and D5; on the two GNU targets
    // dbg_for answers lldb or gdb since D1 (2026-10-13), and the program is driven as a C one is.
    check(ride_debugger_stops_itself(editor::ToolShc) != 0,
          "a Shalimar program is known to stop itself");
    check(ride_debugger_stops_itself(editor::ToolCc1) == 0 &&
              ride_debugger_stops_itself(editor::ToolMsvc) == 0 &&
              ride_debugger_stops_itself(editor::ToolCxx) == 0,
          "and nothing else is - the other three need a debugger");
    check(ride_debugger_for(editor::ToolShc, editor::hostArch()) ==
              static_cast<int>(editor::dbg_for(editor::ToolCc1, editor::hostArch())),
          "and on this host the debugger that reads c90's DWARF reads shalimar's too (D1)");
    check(ride_debugger_for(editor::ToolShc, "tms6747") ==
              static_cast<int>(editor::dbg_for(editor::ToolCxx1, "tms6747")),
          "and for tms6747 the emulator that stops cpp11's program stops shalimar's (D5)");

    // The session is what a Shalimar program keeps where no debugger reads it - a tms6747 with
    // no vm6747 beside the editor - and what it says then names shc rather than the compiler
    // that has nothing to do with it. This used to answer with cc1's MASM.
    if (editor::dbg_for(editor::ToolShc, "tms6747") == editor::DebuggerNone)
        check(std::string(ride_no_debugger_because(editor::ToolShc, "tms6747"))
                  .find("stops itself") != std::string::npos,
              "and asking why there is no debugger says so, naming no other compiler");

    // The two release refusals are different sentences because they are
    // different facts: one build is missing -g and the other never had one.
    check(std::string(ride_release_cannot_stop(editor::ToolShc)).find("no debugger in it") !=
              std::string::npos,
          "release says what a Shalimar build has not got");
    check(std::string(ride_release_cannot_stop(editor::ToolCc1)).find("-g") !=
              std::string::npos,
          "where a C build says what was left out of it");
    if (editor::dbg_for(editor::ToolShc, "tms6747") == editor::DebuggerNone)
        check(std::string(ride_why_it_did_not_start(editor::ToolShc, "tms6747"))
                  .find("did not arm") != std::string::npos,
              "and a start that failed is a program that did not arm, not a debugger to install");
    check(std::string(ride_why_it_did_not_start(editor::ToolShc, editor::hostArch()))
              .find("could not be started") != std::string::npos,
          "where on this host it is the debugger that could not be started (D1)");

    // Nothing is running yet, so nothing is refused yet: the window may put a
    // watch down before it starts, as the terminal may.
    RIDEDebugger* idle = ride_debugger_new();
    check(std::string(ride_cannot_watch(idle)).empty(),
          "with nothing running, watching is not refused");
    check(std::string(ride_locals_none_because(idle)) == "  (nothing in scope here)",
          "and an empty scope is this place having none");
    ride_debugger_free(idle);

    const char* shc = std::getenv("SHC");
    if (!shc || !*shc) {
        skipped("no $SHC, so nothing is built to stop inside");
        return;
    }

    std::string dir = editor::path::join(editor::path::tempDir(), "ride-window-shm");
    editor::path::removeTree(dir);
    if (!editor::path::makeDirectories(dir)) {
        skipped("could not make %s - this case is about the seam, not about that",
                    dir.c_str());
        return;
    }
    const std::string source = editor::path::join(dir, "steps.shl");
    writeSource(source,
                "fun <int> = twice(n: int) {\n"
                "  int d : n + n\n"
                "  return d\n"
                "}\n"
                "\n"
                "fun <> = main() {\n"
                "  int a : 1\n"
                "  int b : twice(a)\n"
                "  ? b\n"
                "}\n");
    if (!editor::path::exists(source)) {
        skipped("could not write %s - this case is about the seam, not about that",
                    source.c_str());
        editor::path::removeTree(dir);
        return;
    }

    // Built through the bridge, exactly as the window builds it - and in the
    // debug configuration, which for shc is what --debug means: the compiler's
    // output is the same either way and the runtime archive is not.
    RIDEProgram* built = ride_build_program(0, "cc1", "cl", shc, "cxx1", editor::ToolShc, source.c_str(),
                                          editor::LangShalimar, editor::hostArch(),
                                          editor::ConfigDebug);
    check(ride_program_ok(built) != 0, "the window's build makes a program");
    if (ride_program_ok(built) == 0) {
        std::printf("   it said: %s\n", ride_program_output(built));
        ride_program_free(built);
        editor::path::removeTree(dir);
        return;
    }

    // **Since D1 the host's own debugger drives it**, reading the DWARF -g wrote: the program is
    // not armed, lldb or gdb is started on it, and the stack has a name in it rather than a depth.
    // The session half - no variables, the refusals in its own voice - is the x86_64-windows and
    // tms6747 story until D4 and D5, and shalimar::Session's own cases above still hold it.
    RIDEDebugger* debugger = ride_debugger_new();
    check(ride_debugger_start(debugger, editor::ToolShc, editor::hostArch(),
                             ride_program_path(built)) != 0,
          "the program starts under the host's debugger");
    check(ride_debugger_running(debugger) != 0,
          "and the window is told something is running, as it is for the other three");
    check(ride_debugging_shalimar(debugger) == 0, "and which of the two halves it is: not the session");

    check(ride_debugger_break(debugger, source.c_str(), 8) != 0,
          "a breakpoint is set by file and line");
    ride_debugger_run(debugger);
    check(ride_stop_stopped(debugger) != 0 && ride_stop_line(debugger) == 8,
          "running stops on the line it was set on");
    check(std::string(editor::path::filename(ride_stop_file(debugger))) == "steps.shl",
          "in the file it was set in");

    check(ride_stack_count(debugger) >= 1, "the stack has the frame it is standing in");
    check(std::string(ride_stack_function(debugger, 0)).find("main") != std::string::npos,
          "named as the program names it");

    ride_debugger_step_into(debugger);
    check(ride_stop_stopped(debugger) != 0 && ride_stop_line(debugger) == 2,
          "stepping into the call reaches its first line");
    check(std::string(ride_stack_function(debugger, 0)).find("twice") != std::string::npos,
          "and the frame is the callee's, by name");
    ride_debugger_step_out(debugger);
    check(ride_stop_stopped(debugger) != 0 && ride_stop_line(debugger) == 8,
          "and stepping out comes back to the statement that called");

    ride_debugger_step_over(debugger);
    check(ride_stop_stopped(debugger) != 0 && ride_stop_line(debugger) == 9,
          "stepping over reaches the next statement");
    ride_debugger_resume(debugger);
    check(ride_stop_exited(debugger) != 0, "carrying on from the last statement ends it");
    if (ride_stop_status(debugger) != 0)
        std::printf("   it ended with status %d, saying: %s\n", ride_stop_status(debugger), ride_stop_said(debugger));
    check(ride_stop_status(debugger) == 0, "with the status main returned");

    // The debugger outlives the program, as it does for a C one: it is stopped when told to.
    ride_debugger_stop(debugger);
    check(ride_debugger_running(debugger) == 0, "and nothing is running once it is stopped");

    ride_debugger_stop(debugger);
    ride_debugger_free(debugger);
    ride_program_free(built);
    editor::path::removeTree(dir);
}

// c2s, driven from the Language menu. The editor runs it over the open file and opens what it
// wrote; these are the decisions made before it is run, which are the ones that can be wrong
// without anybody noticing until a file has been written over.
void namingAConversion() {
    checkEqual(editor::convertedName("prime.c", true), "prime.shl",
               "a C file converts to .shl beside itself");
    checkEqual(editor::convertedName("prime.shm", false), "prime.c",
               "and a Shalimar one back to .c");
    checkEqual(editor::convertedName("prime.shl", false), "prime.c",
               "and .shl is what gets written, being the only suffix read as\n"
               "               Shalimar - a written .shm opened as plain text");

    // The whole path is kept, not just the name: the converted file belongs
    // beside the original and not in whatever directory the editor was
    // started from.
    checkEqual(editor::convertedName("/a/b/prime.c", true), "/a/b/prime.shl",
               "the directory comes with it");

    // A dot in a directory name is not an extension of the file. Getting this
    // wrong would truncate the path and write somewhere else entirely.
    checkEqual(editor::convertedName("/a.b/prime", true), "/a.b/prime.shl",
               "a dot in a directory is not the file's extension");
    checkEqual(editor::convertedName("/a.b/prime.c", false), "/a.b/prime.c",
               "and the same file asked for in the direction it is already in");

    checkEqual(editor::convertedName("", true), "",
               "an unsaved file has no name to convert");

    // The one the editor turns away: converting a .c into a .c would write
    // over the file it is reading.
    check(editor::convertedName("prime.c", false) == "prime.c",
          "the wrong direction names the file itself, which the editor refuses");
}

void theConversionSeam() {
    std::printf("what the window asks the core to convert\n");

    int toShalimar = -1;
    check(ride_converts_from(editor::LangC, &toShalimar) == 1 && toShalimar == 1,
          "C converts to Shalimar");
    toShalimar = -1;
    check(ride_converts_from(editor::LangShalimar, &toShalimar) == 1 && toShalimar == 0,
          "and Shalimar back to C");
    check(ride_converts_from(editor::LangCpp, &toShalimar) == 0,
          "C++ has no other side");
    check(ride_converts_from(editor::LangPlain, &toShalimar) == 0,
          "and neither has plain text");

    char* named = ride_converted_name("/a/b/prime.c", 1);
    checkEqual(named, "/a/b/prime.shl", "the window is told the same name the editor uses");
    ride_free(named);

    named = ride_converted_name("/a.b/prime", 1);
    checkEqual(named, "/a.b/prime.shl", "including that a dot in a directory is not an extension");
    ride_free(named);

    char* found = ride_find_converter();
    check(found != 0, "asking where c2s is always answers something");
    ride_free(found);

    RIDEConversion* made = ride_convert("", "", "", 1);
    check(ride_conversion_ran(made) == 0, "a conversion with nothing named did not run");
    check(ride_conversion_ok(made) == 0, "and is not ok");
    check(std::string(ride_conversion_produced(made)).empty(),
          "and wrote no file");
    ride_conversion_free(made);
}

// How the window is told a project's program is stepped through - a different question from how
// one file is, and the one the window could not ask at all until it had a Debug project item.
void theWindowsProjectDebug() {
    std::printf("what the window asks about debugging a project\n");

    file::path dir = file::temp_directory_path() / "ride-bridge-debug";
    file::remove_all(dir);
    file::create_directories(dir);
    writeSource((dir / "add.c").string(), "int add(int a, int b) { return a + b; }\n");
    writeSource((dir / "main.c").string(),
                "int add(int, int);\nint main(void) { return add(1, 2); }\n");
    writeSource((dir / "project.pro").string(),
                "{\n  \"name\": \"sums\",\n"
                "  \"groups\": { \"Sources\": [\"add.c\", \"main.c\"] },\n"
                "  \"build\": { \"target\": \"sums\", \"groups\": [\"Sources\"] }\n}\n");

    const std::string host = editor::hostArch();
    RIDEProject* project = ride_project_new();
    char trouble[512] = {0};
    check(ride_project_load(project, dir.string().c_str(), trouble, sizeof trouble) != 0,
          "a project that says what it builds loads");

    int can = ride_project_debug_plan(project, "cc1", "cl", "shc", "cxx1", editor::ToolAuto,
                                     host.c_str());
    if (ride_debugger_for(editor::ToolCc1, host.c_str()) == 0) {
        // Windows, where cc1 generates MASM and MASM carries no line table.
        check(can == 0, "a C project cannot be debugged where nothing reads what cc1 writes");
        check(std::string(ride_project_why_not_debug(project)).find("MASM") != std::string::npos,
              "and the reason names the MASM that has no line table");
    } else {
        check(can != 0, "a C project can be debugged where cc1's DWARF can be read");
        check(ride_project_debug_kind(project) == editor::ToolCc1,
              "through cc1, whose debug information it is");
        check(ride_project_blind_groups(project) == 0,
              "with no group the debugger would be blind in");
    }

    // Two languages, two compilers, and debug information that does not mix. Which groups are
    // invisible depends on the machine, so what is checked is the rule rather than a number: every
    // part with no debugger of its own is named, and the one being read is a part that has one.
    writeSource((dir / "extra.cpp").string(), "int twice(int n) { return n * 2; }\n");
    writeSource((dir / "project.pro").string(),
                "{\n  \"name\": \"sums\",\n"
                "  \"groups\": { \"Sources\": [\"add.c\", \"main.c\", \"extra.cpp\"] },\n"
                "  \"build\": { \"target\": \"sums\", \"groups\": [\"Sources\"] }\n}\n");
    check(ride_project_load(project, dir.string().c_str(), trouble, sizeof trouble) != 0,
          "a project of both languages loads");
    can = ride_project_debug_plan(project, "cc1", "cl", "shc", "cxx1", editor::ToolAuto, host.c_str());

    int parts = ride_project_target_parts(project);
    int sightless = 0;
    for (int i = 0; i < parts; ++i) {
        int theirs = ride_project_part_toolchain(project, i, "cc1", "cl", "shc", "cxx1",
                                                editor::ToolAuto);
        if (ride_debugger_for(theirs, host.c_str()) == 0) ++sightless;
    }
    check(ride_project_blind_groups(project) == sightless,
          "every part with no debugger of its own is named as one the debugger is blind in");
    check(can != 0 || sightless == parts,
          "and the program is refused only when none of its parts can be seen at all");
    if (can != 0)
        check(ride_debugger_for(ride_project_debug_kind(project), host.c_str()) != 0,
              "the compiler chosen to read is one whose debug information can be read");

    ride_project_free(project);
    file::remove_all(dir);

    // Shalimar, which is the case none of the above describes: no debugger anywhere, nothing
    // missing, and the program stops itself. It has to come out possible on every machine this runs
    // on - that is the whole point of asking dbg_stopsItself before dbg_for.
    const char* shc = std::getenv("SHC");
    file::path shmDir = file::temp_directory_path() / "ride-bridge-debug-shm";
    file::remove_all(shmDir);
    file::create_directories(shmDir);
    writeSource((shmDir / "steps.shl").string(),
                "fun <int> = twice(n: int) {\n"
                "  int d : n + n\n"
                "  return d\n"
                "}\n"
                "\n"
                "fun <> = main() {\n"
                "  int a : 1\n"
                "  int b : twice(a)\n"
                "  ? b\n"
                "}\n");
    writeSource((shmDir / "project.pro").string(),
                "{\n  \"name\": \"steps\",\n"
                "  \"groups\": { \"Sources\": [\"steps.shl\"] },\n"
                "  \"build\": { \"target\": \"steps\", \"groups\": [\"Sources\"] }\n}\n");

    RIDEProject* shm = ride_project_new();
    check(ride_project_load(shm, shmDir.string().c_str(), trouble, sizeof trouble) != 0,
          "a Shalimar project loads");
    check(ride_project_debug_plan(shm, "cc1", "cl", shc && *shc ? shc : "shc", "cxx1",
                                 editor::ToolAuto, host.c_str()) != 0,
          "and can be debugged on every machine, needing nothing installed");
    check(ride_project_debug_kind(shm) == editor::ToolShc, "by shc, which reads nothing");

    // The fix this test exists for. The walk over the parts puts every group with no debugger into
    // the blind list, and shc has none - so a Shalimar project was told its own group carried no
    // debug information and the debugger could not stop in it, immediately before stopping in it.
    check(ride_project_blind_groups(shm) == 0,
          "and is not warned that the debugger cannot stop where it is about to stop");

    if (shc && *shc) {
        // And the whole of the window's new path: build the project's program,
        // attach to that rather than to a temporary one, and stop in it.
        RIDEBuild* made = ride_build_target(shm, "cc1", "cl", shc, "cxx1", editor::ToolAuto,
                                          host.c_str(), editor::ConfigDebug);
        check(made != nullptr && ride_build_ok(made) != 0, "the project's program builds");
        if (made != nullptr && ride_build_ok(made) != 0) {
            std::string program = ride_project_target_program(shm);
            check(editor::path::exists(program),
                  "and is left where the project keeps it, for a debugger to open");

            RIDEDebugger* debugger = ride_debugger_new();
            check(ride_debugger_start(debugger, ride_project_debug_kind(shm), host.c_str(),
                                     program.c_str()) != 0,
                  "the session starts on the project's own program");
            check(ride_debugger_break(debugger,
                                     (shmDir / "steps.shl").string().c_str(), 8) != 0,
                  "a breakpoint is set in one of its files");
            ride_debugger_run(debugger);
            check(ride_stop_stopped(debugger) != 0 && ride_stop_line(debugger) == 8,
                  "and it stops there");
            ride_debugger_stop(debugger);
            ride_debugger_free(debugger);
        }
        if (made != nullptr) ride_build_free(made);
    } else {
        skipped("no $SHC, so the project's program is not built and stopped");
    }

    ride_project_free(shm);
    file::remove_all(shmDir);
}

// ---- a program that runs while the window watches ----------------------------------------------

// What the running-program callback heard, gathered on its thread and read on this one.
struct Heard {
    std::mutex lock;
    std::string out, err, build;
    int pieces = 0;
    bool last = false;
};

void hear(void* user, const char* bytes, int size, int stream) {
    Heard* heard = static_cast<Heard*>(user);
    std::lock_guard<std::mutex> held(heard->lock);
    if (bytes == 0) { heard->last = true; return; }
    std::string piece(bytes, static_cast<size_t>(size));
    if (stream == RIDE_STREAM_ERR) heard->err += piece;
    else if (stream == RIDE_STREAM_BUILD) heard->build += piece;
    else heard->out += piece;
    ++heard->pieces;
}

// Whether the program's output came to hold `what` within the time - it is read as it comes.
bool heardIt(Heard& heard, const std::string& what, int timeoutMs = 10000, bool fromErr = false) {
    for (int waited = 0; waited <= timeoutMs; waited += 10) {
        {
            std::lock_guard<std::mutex> held(heard.lock);
            if ((fromErr ? heard.err : heard.out).find(what) != std::string::npos) return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

std::string heardOut(Heard& heard) {
    std::lock_guard<std::mutex> held(heard.lock);
    return heard.out;
}

// The addendum to both reviews: scanf saw end of file at once, because every run had the null
// device for its input. A script stands in for a program here, and a real one is built when $CC1
// names a compiler; each reads what is sent, sees end of file when told, and is stopped when told.
void aProgramThatReads() {
    std::printf("a program that reads what it is sent\n");

    std::string dir = editor::path::join(editor::path::tempDir(), "ride-input-test");
    editor::path::removeTree(dir);
    editor::path::makeDirectories(dir);

#ifdef _WIN32
    std::string talker = editor::path::join(dir, "talker.cmd");
    writeSource(talker,
                "@echo off\r\n"
                "echo first\r\n"
                "set /p line=name? \r\n"
                "echo got %line%\r\n"
                "set second=\r\n"
                "set /p second=\r\n"
                "if \"%second%\"==\"\" echo end of input\r\n"
                "echo err line 1>&2\r\n"
                "exit /b 3\r\n");
    std::string spinner = editor::path::join(dir, "spinner.cmd");
    writeSource(spinner, "@echo off\r\n:again\r\ngoto again\r\n");
#else
    std::string talker = editor::path::join(dir, "talker.sh");
    writeSource(talker,
                "#!/bin/sh\n"
                "echo first\n"
                "printf 'name? '\n"
                "read line\n"
                "echo \"got $line\"\n"
                "read second || echo \"end of input\"\n"
                "echo \"err line\" >&2\n"
                "exit 3\n");
    std::string spinner = editor::path::join(dir, "spinner.sh");
    writeSource(spinner, "#!/bin/sh\nsleep 300 &\nwhile :; do :; done\n");
    chmod(talker.c_str(), 0755);
    chmod(spinner.c_str(), 0755);
#endif

    Heard heard;
    RIDERunning* running = ride_run_built_start(talker.c_str(), hear, &heard);
    check(running != 0, "a program starts with an input of its own");
    check(heardIt(heard, "first"), "its output arrives while it is still running");
    check(heardIt(heard, "name? "), "and a prompt with no newline arrives before anything is sent");
    check(ride_running_done(running) == 0, "and it is still running, waiting to be told something");
    check(ride_running_send(running, "hello\n", 6) != 0, "a line is sent to it");
    check(heardIt(heard, "got hello"), "and what it read is the line that was sent");
    ride_running_close_input(running);
    check(heardIt(heard, "end of input"), "closing the input is end of file to its next read");
#ifdef _WIN32
    // A console has one output, so on a pseudo-console stderr comes with stdout.
    check(heardIt(heard, "err line", 10000, true) || heardIt(heard, "err line"),
          "what it says on stderr comes too");
#else
    check(heardIt(heard, "err line", 10000, true), "what it says on stderr comes apart");
#endif
    check(ride_running_wait(running, 10000) != 0, "and then it ends");
    check(ride_running_status(running) == 3, "with the status it exited with");
    check(ride_running_ran(running) != 0 && ride_running_stopped(running) == 0,
          "having run, and not been stopped");
    check(ride_running_send(running, "late\n", 5) == 0, "nothing more can be sent to it");
    {
        std::lock_guard<std::mutex> held(heard.lock);
        check(heard.last, "and the last call has said the run is over");
    }
    ride_running_free(running);

    Heard spun;
    running = ride_run_built_start(spinner.c_str(), hear, &spun);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    check(ride_running_done(running) == 0, "a program that never ends is still going");
    ride_running_stop(running);
    check(ride_running_wait(running, 5000) != 0, "Stop ends it promptly");
    check(ride_running_stopped(running) != 0, "and says it was stopped");
    ride_running_free(running);

    // Stop before anything was built: the build is not run, and the run says it was stopped.
    Heard early;
    running = ride_run_built_start(talker.c_str(), hear, &early);
    ride_running_stop(running);
    check(ride_running_wait(running, 10000) != 0, "Stop at once ends the run too");
    ride_running_free(running);

    // A build is stopped from another thread: the command running is killed and the build fails.
    {
        std::string output;
        std::thread stopper([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            ride_cancel_builds();
        });
        editor::BuildScope scope;
#ifdef _WIN32
        int status = editor::runCaptured("ping -n 30 127.0.0.1", output);
#else
        int status = editor::runCaptured("sleep 30", output);
#endif
        stopper.join();
        check(status == editor::kStoppedStatus, "a build's command is stopped when asked");
        check(output.find("[stopped]") != std::string::npos, "and its output says so");
        check(editor::buildCancelled(), "and the build knows it was stopped");
        output.clear();
        editor::runCaptured("echo never", output);
        check(output.find("never") == std::string::npos,
              "nothing more of that build runs after it");
    }
    {
        std::string output;
        editor::BuildScope fresh;
        editor::runCaptured("echo after", output);
        check(output.find("after") != std::string::npos, "and a build begun afterwards runs");
    }

    const char* cc1 = std::getenv("CC1");
    if (!cc1 || !*cc1 || !editor::path::exists(cc1)) {
        skipped("no $CC1, so no C program reads with scanf here");
        editor::path::removeTree(dir);
        return;
    }
    std::string source = editor::path::join(dir, "asks.c");
    writeSource(source,
                "#include <stdio.h>\n"
                "\n"
                "int main(void)\n"
                "{\n"
                "    int n = 0;\n"
                "    char line[100];\n"
                "    printf(\"a number? \");\n"
                "    if (scanf(\"%d\", &n) != 1) { printf(\"no number\\n\"); return 1; }\n"
                "    getchar();\n"
                "    printf(\"twice is %d\\n\", 2 * n);\n"
                "    if (fgets(line, sizeof line, stdin)) printf(\"read: %s\", line);\n"
                "    if (fgets(line, sizeof line, stdin) == 0) printf(\"and then the end\\n\");\n"
                "    return 0;\n"
                "}\n");
    Heard asked;
    running = ride_run_start(0, cc1, "cl", "shc", "cxx1", RIDE_TOOL_CC1, source.c_str(),
                             RIDE_LANG_C, editor::hostArch(), RIDE_CONFIG_DEBUG, hear, &asked);
    check(heardIt(asked, "a number? ", 60000),
          "a C program's prompt shows before it reads, with no fflush");
    check(ride_running_send(running, "21\n", 3) != 0, "scanf is sent a number");
    check(heardIt(asked, "twice is 42", 60000), "and scanf reads it");
    ride_running_send(running, "a line\n", 7);
    check(heardIt(asked, "read: a line"), "fgets reads the next line");
    ride_running_close_input(running);
    check(heardIt(asked, "and then the end"), "and sees end of file when the input is closed");
    check(ride_running_wait(running, 10000) != 0 && ride_running_status(running) == 0,
          "and the program ends as it meant to");
    check(ride_running_built(running) != 0 && ride_running_has_error(running) == 0,
          "built, with nothing to say about the source");
    ride_running_free(running);
    editor::path::removeTree(dir);
}

// The seam's smaller promises (README.md, "The seam"): every accessor takes NULL, a message cut to
// fit a buffer is cut between characters, the error's own file comes across for every result, and
// the diagnostic reader and the compilers' names the Mac window used to take from the core.
void theSeamsSmallPromises() {
    std::printf("the seam's smaller promises\n");

    check(ride_build_ok(0) == 0 && std::string(ride_build_output(0)).empty() &&
              std::string(ride_build_error_file(0)).empty(),
          "a build that is not there reads as one that failed");
    check(ride_ran_ran(0) == 0 && std::string(ride_ran_error_file(0)).empty() &&
              std::string(ride_program_error_file(0)).empty(),
          "and so do a run and a program");
    check(ride_project_loaded(0) == 0 && std::string(ride_project_name(0)).empty() &&
              ride_project_groups(0) == 0,
          "a project that is not there has nothing in it");
    check(ride_stop_stopped(0) == 0 && std::string(ride_stop_file(0)).empty() &&
              ride_locals_count(0) == 0,
          "and a debugger that is not there has not stopped");
    check(ride_running_done(0) == 0 && ride_running_send(0, "x", 1) == 0,
          "and a run that is not there takes nothing");

    std::string dir = editor::path::join(editor::path::tempDir(), "ride-seam-test");
    editor::path::removeTree(dir);
    editor::path::makeDirectories(dir);
    std::string pro = editor::path::join(dir, "a\xC3\xA9\xC3\xA9\xC3\xA9.pro");
    writeSource(pro, "not json");
    RIDEProject* project = ride_project_new();
    char why[5];
    ride_project_load(project, pro.c_str(), why, (int)sizeof why);
    checkEqual(why, "a\xC3\xA9", "a message cut to fit is cut between characters, not inside one");
    ride_project_free(project);
    editor::path::removeTree(dir);

    int line = 0, column = 0;
    const char* file = 0;
    const char* message = 0;
    check(ride_parse_diagnostic("main.c:3:7: error: expected ';'", "main.c", &line, &column,
                                &file, &message) != 0,
          "the diagnostic reader finds a GNU error");
    check(line == 3 && column == 7 && std::string(file) == "main.c" &&
              std::string(message) == "expected ';'",
          "where it is and what it says");
    check(ride_parse_diagnostic("nothing to see", "main.c", &line, &column, &file, &message) == 0 &&
              line == 0 && std::string(file).empty(),
          "and finds nothing in a line that is not one");
    checkEqual(ride_compiler_name(RIDE_COMPILER_C), editor::product::kCompilerC,
               "the C compiler's name");
    checkEqual(ride_compiler_name(RIDE_COMPILER_CPP), editor::product::kCompilerCpp,
               "the C++ compiler's name");
    checkEqual(ride_compiler_name(RIDE_COMPILER_SHALIMAR), editor::product::kCompilerShalimar,
               "the Shalimar compiler's name");

#ifndef _WIN32
    check(editor::path::onPath("sh").find('/') == 0, "a tool on PATH is found by its full path");
#endif
    check(editor::path::onPath("no-such-tool-ride-test").empty(), "and one that is not, is not");
}

// Compiler Options (options.h): the defaults are the flags RIDE passed before the dialog, a value
// set reaches the command line, one set back to its default is not written, and the JSON round-trips.
static void compilerOptions() {
    using namespace editor;
    options::Store store;
    checkEqual(options::flags(store, ToolCxx1, ConfigRelease, "x86_64-linux"), " -O2 -DNDEBUG=1", "cpp11 release defaults");
    checkEqual(options::flags(store, ToolCc1, ConfigDebug, "x86_64-linux"), " -g -D_DEBUG=1", "c90 debug defaults");
    checkEqual(options::flags(store, ToolCc1, ConfigDebug, "tms6747"), " -g -D_DEBUG=1", "-g where there is a line table (c90 on the C6000: .dwpsn, since 2026-10-13)");
    checkEqual(options::flags(store, ToolCxx1, ConfigDebug, "tms6747"), " -g -D_DEBUG=1", "and -g where there is one (cpp11 on the C6000: .dwpsn)");
    checkEqual(options::flags(store, ToolShc, ConfigDebug, "x86_64-linux"), " --debug -g", "shalimar debug runtime, and -g since D1");
    checkEqual(options::flags(store, ToolShc, ConfigDebug, "tms6747"), " -g", "-g on the emulator (the .dwpsn of D5), and no debug runtime there");
    checkEqual(options::flags(store, ToolShc, ConfigRelease, "x86_64-linux"), "", "shalimar release defaults");
    store.set(ConfigRelease, "cpp11.opt", "-O1");
    store.set(ConfigRelease, "cpp11.defines", "NDEBUG=1; TRACE");
    store.set(ConfigRelease, "general.nologo", "1");
    store.set(ConfigRelease, "general.jobs", "4");
    checkEqual(options::flags(store, ToolCxx1, ConfigRelease, "x86_64-windows"),
               " -O1 -DNDEBUG=1 -DTRACE -nologo -j4", "cpp11 options set");
    store.set(ConfigDebug, "shc.search", "0");
    checkEqual(options::flags(store, ToolShc, ConfigDebug, "x86_64-linux"),
               " --debug -g --no-search", "shalimar options set");
    store.set(ConfigDebug, "cpp11.compress", "0");
    checkEqual(options::flags(store, ToolCxx1, ConfigDebug, "tms6747"), " -g --no_compress -D_DEBUG=1",
               "cpp11 without compact instructions on the C6000");
    checkEqual(options::flags(store, ToolCxx1, ConfigDebug, "x86_64-linux"), " -g -D_DEBUG=1",
               "no --no_compress where there are no compact instructions");
    store.set(ConfigDebug, "cpp11.compress", "1");
    options::Store back;
    back.fromJson(store.toJson());
    checkEqual(back.toJson().write(), store.toJson().write(), "options round-trip through JSON");
    store.set(ConfigRelease, "cpp11.opt", "-O2");
    check(store.toJson().get("release").has("cpp11.opt") == false, "a default is not written");
    store.set(ConfigDebug, "cpp11.declines", "1");
    checkEqual(options::environment(store, ConfigDebug)["CPP11_DECLINES"], "1", "CPP11_DECLINES asked for");
    checkEqual(options::environment(store, ConfigRelease)["CPP11_DECLINES"], "", "CPP11_DECLINES removed");
    std::string why;
    check(options::available("c90.g", "tms6747", why), "-g available on tms6747 (c90's .dwpsn, since 2026-10-13)");
    check(options::find("cpp11.masm") == 0 && options::find("shc.with") == 0, "what a menu decides is not an option");
    size_t onTabs = 0;
    for (size_t t = 0; t < options::tabCount(); ++t)
        for (size_t i = 0; i < options::count(); ++i) if (std::string(options::at(i).tab) == options::tabName(t)) ++onTabs;
    checkEqual(std::to_string(onTabs), std::to_string(options::count()), "every option is on a tab");
}

// **A CCS project opened as it is** (src/ccs/ccsproject.h), read from the projects CCS 7.4 and
// 5.5 made and built themselves (docs/ccs-reference): the device, the two configurations, every
// option that maps, the ones that do not - named, never dropped in silence - the source list with
// its exclusion and its linked file, the macros, a device that is refused, and that no file of CCS's is ever written.
namespace {

std::string ccsReference() {
    std::string program = editor::path::programDirectory();
    const std::string tried[] = {
        editor::path::join(editor::path::join(program, "docs"), "ccs-reference"),
        editor::path::join(editor::path::join(editor::path::parent(program), "docs"), "ccs-reference"),
    };
    for (size_t i = 0; i < sizeof tried / sizeof tried[0]; ++i)
        if (editor::path::isDirectory(editor::path::join(tried[i], "ccs74"))) return tried[i];
    return std::string();
}

bool hasEntry(const std::vector<std::string>& list, const std::string& one) {
    for (size_t i = 0; i < list.size(); ++i) if (list[i] == one) return true;
    return false;
}

bool anyContains(const std::vector<std::string>& list, const std::string& piece) {
    for (size_t i = 0; i < list.size(); ++i) if (list[i].find(piece) != std::string::npos) return true;
    return false;
}

// A folder copied whole, for the end-to-end builds: the reference tree is read and never built in.
void copyTree(const std::string& from, const std::string& to) {
    editor::path::makeDirectories(to);
    std::vector<editor::path::Entry> entries = editor::path::entries(from);
    for (size_t i = 0; i < entries.size(); ++i) {
        std::string a = editor::path::join(from, entries[i].name), b = editor::path::join(to, entries[i].name);
        if (entries[i].directory) copyTree(a, b);
        else { std::ofstream out(b.c_str(), std::ios::binary); out << readWholeFile(a); }
    }
}

// The three CCS files and the folder's listing, as one string: what must be the same afterwards.
std::string ccsFingerprint(const std::string& dir) {
    std::string all;
    const char* files[] = { ".project", ".ccsproject", ".cproject" };
    for (size_t i = 0; i < 3; ++i) all += readWholeFile(editor::path::join(dir, files[i])) + "\n--\n";
    std::vector<editor::path::Entry> entries = editor::path::entries(dir);
    for (size_t i = 0; i < entries.size(); ++i) all += entries[i].name + (entries[i].directory ? "/" : "") + "\n";
    return all;
}

}

void ccsProjectsAsTheyAre() {
    std::printf("CCS projects, opened as they are\n");
    using editor::ccs::Reading;
    using editor::ccs::Config;

    const std::string reference = ccsReference();
    if (reference.empty()) { skipped("no docs/ccs-reference beside the suite, so nothing is read"); return; }
    const std::string k74c = editor::path::join(editor::path::join(reference, "ccs74"), "K6747c");
    const std::string k74cpp = editor::path::join(editor::path::join(reference, "ccs74"), "K6747cpp");
    const std::string k55c = editor::path::join(editor::path::join(reference, "ccs55"), "K6747c");
    const std::string k55cpp = editor::path::join(editor::path::join(reference, "ccs55"), "K6747cpp");
    const std::string p7 = editor::path::join(editor::path::join(reference, "ccs74"), "P7misc");
    editor::ccs::setDefinitionsDir(editor::path::join(reference, "ti-option-definitions"));

    file::path dir = file::temp_directory_path() / "ride-ccs-test";
    file::remove_all(dir);
    file::create_directories(dir / "app" / "include");
    file::create_directories(dir / "app" / "lib");
    // A CCS install with two compilers in it, as TI lays one out, for ${CG_TOOL_ROOT}.
    file::create_directories(dir / "ccsv7" / "tools" / "compiler" / "ti-cgt-c6000_8.2.2" / "include");
    file::create_directories(dir / "ccsv7" / "tools" / "compiler" / "c6000_7.4.4" / "include");
    const std::string root = editor::path::withSlashes((dir / "ccsv7").string());
    editor::settings::pretendInstalledAt((dir / "app").string());
    editor::settings::writeInstallFileIfAbsent();

    // -- the switch
    check(editor::settings::ccsEnabled() && editor::settings::ccsRoot().empty(), "the switch starts on, with no root");
    check(editor::settings::rememberCcs(false, "") && !editor::settings::ccsEnabled(), "and is off where settings.json says false");
    check(editor::settings::rememberCcs(true, root) && editor::settings::ccsEnabled() &&
              editor::settings::ccsRoot() == root, "\"ccs\": { \"enabled\", \"root\" } in settings.json turns it on");

    // -- what is a CCS project
    check(editor::ccs::isProject(k74c) && editor::ccs::isProject(k55cpp) && editor::ccs::isProject(p7),
          "a folder holding .project and .ccsproject is a CCS project");
    check(!editor::ccs::isProject((dir / "app").string()), "and one without them is not");
    check(editor::ccs::isProjectFile(editor::path::join(k74c, ".cproject")) && !editor::ccs::isProjectFile(editor::path::join(k74c, "main.c")),
          "the three files are its, and a source is not");

    // -- TI's option definitions, read as data
    {
        editor::ccs::OptionDefs defs;
        std::string why;
        check(defs.load("8.2.2", why), "the CCS 7.4 definitions load: " + why);
        checkEqual(defs.defaultOf("compilerID.OPT_LEVEL.release"), "2", "Release's unstored optimisation is level 2 by definition");
        checkEqual(defs.defaultOf("compilerID.OPT_LEVEL"), "", "and Debug's has no default");
        checkEqual(defs.defaultOf("linkerID.INITIALIZATION_MODEL"), "ROM_MODEL", "--rom_model is the unstored default");
        std::vector<std::string> none;
        checkEqual(defs.spell("compilerID.OPT_FOR_SPEED", "com.ti.ccstudio.buildDefinitions.C6000_8.2.compilerID.OPT_FOR_SPEED.5", none),
                   "--opt_for_speed=5", "an enumerated value is spelled by its own flag");
        checkEqual(defs.spell("compilerID.OPT_LEVEL.release", "com.ti.ccstudio.buildDefinitions.C6000_8.2.compilerID.OPT_LEVEL.3", none),
                   "-O3", "an instance spells as the option it was derived from");
        checkEqual(defs.spell("linkerID.HEAP_SIZE", "0x800", none), "--heap_size=0x800", "a string is its command and value");
        checkEqual(defs.spell("compilerID.NO_COMPRESS", "true", none), "--no_compress", "a boolean its command");
        std::vector<std::string> two; two.push_back("A"); two.push_back("B=1");
        checkEqual(defs.spell("compilerID.DEFINE", "", two), "--define=A --define=B=1", "a list its command per item");
        editor::ccs::OptionDefs old;
        check(old.load("7.4.4", why) && old.find("compilerID.ABI") != 0, "the CCS 5.5 definitions load, ABI among them");
        checkEqual(editor::ccs::idTail("com.ti.ccstudio.buildDefinitions.C6000_8.2.compilerID.OPT_LEVEL.release"),
                   "compilerID.OPT_LEVEL.release", "an id's tail is past the version, whose own dot is not a separator");
        checkEqual(editor::ccs::idTail("com.ti.ccstudio.buildDefinitions.core.OPT_TAGS"), "core.OPT_TAGS", "and core's keeps its word");
    }

    // -- K6747c, CCS 7.4: the C project with a linked file
    {
        Reading r;
        std::string error;
        check(editor::ccs::read(k74c, r, error), "K6747c (7.4) reads: " + error);
        checkEqual(r.name, "K6747c", "its name is .project's");
        checkEqual(r.device, "TMS320C67XX.TMS320C6747", "its device is .ccsproject's");
        checkEqual(r.family, "C6000", "and its family");
        checkEqual(r.cgtVersion, "8.2.2", "and the compiler version ${CG_TOOL_ROOT} stands for");
        checkEqual(r.ccsVersion, "7.4.0", "and the CCS release");
        checkEqual(r.cgToolRoot, root + "/tools/compiler/ti-cgt-c6000_8.2.2", "${CG_TOOL_ROOT} is that compiler under the root");
        check(r.elf, "it builds ELF");
        check(r.configs.size() == 2 && r.configs[0].which == editor::ConfigDebug && r.configs[1].which == editor::ConfigRelease,
              "Debug and Release, in that order");
        checkEqual(r.configs[0].name, "Debug", "CCS's Debug is RIDE's Debug");
        checkEqual(r.configs[1].name, "Release", "and its Release RIDE's Release");
        const Config& rel = r.configs[1];
        checkEqual(rel.opt, "-O2", "Release's -O2, stored nowhere, comes from the definition");
        check(rel.debugSaid && !rel.debug, "--symdebug:none is no -g");
        check(rel.defines.size() == 3 && rel.defines[0] == "c6747" && rel.defines[1] == "NDEBUG" && rel.defines[2] == "LEVEL=2",
              "--define=c6747 --define=NDEBUG --define=LEVEL=2 are the defines, in order");
        check(rel.undefines.size() == 1 && rel.undefines[0] == "OLDAPI", "--undefine=OLDAPI the undefine");
        check(rel.includes.size() == 2 && rel.includes[0] == r.dir && rel.includes[1] == r.dir + "/inc",
              "${PROJECT_ROOT} and ${PROJECT_ROOT}/inc are the include paths, resolved; ${CG_TOOL_ROOT}/include is RIDE's own headers");
        check(rel.noCompress, "--no_compress is kept");
        check(rel.link.given, "the link is the project's");
        checkEqual(rel.link.heap, "0x800", "--heap_size=0x800");
        checkEqual(rel.link.stack, "0x1000", "--stack_size=0x1000");
        check(rel.link.searchPaths.size() == 1 && rel.link.searchPaths[0] == "C:/Users/GRA/Documents/VM6747/tilib",
              "the -i path that is the project's own stays; ${CG_TOOL_ROOT}'s two are RIDE's runtime directory");
        check(rel.link.libraries.size() == 1 && rel.link.libraries[0] == "rts6740_elf_eh.lib",
              "-lrts6740_elf_eh.lib -llibc.a is one library: TI's index maps to the runtime LNK6x reads");
        check(rel.link.romModel == 1, "--rom_model, stored nowhere, comes from the definition");
        check(rel.link.cmdFiles.size() == 1 && rel.link.cmdFiles[0] == r.dir + "/C6747.cmd", "the .cmd in the folder is the linker command file");
        check(rel.unsupported.size() == 1 && rel.unsupported[0] == "--opt_for_speed=5", "--opt_for_speed=5 is the one option not supported");
        check(hasEntry(rel.ownInstead, "-mv6740") && hasEntry(rel.ownInstead, "--diag_warning=225") && hasEntry(rel.ownInstead, "-m${ProjName}.map"),
              "the target, the diagnostics and the map file are RIDE's own to decide");
        check(anyContains(rel.mapped, "-O2 (CCS's default for Release) -> -O2"), "the mapping says where the default came from");
        const Config& dbg = r.configs[0];
        checkEqual(dbg.opt, "", "Debug stores no optimisation and the definition has none: RIDE's default");
        check(dbg.debugSaid && dbg.debug, "-g is Debug's");
        check(dbg.defines.size() == 1 && dbg.defines[0] == "c6747", "and c6747 its one define");
        checkEqual(dbg.link.stack, "0x800", "Debug's own stack");
        check(dbg.unsupported.empty(), "Debug has nothing unsupported");
        check(r.linked.size() == 1 && r.linked[0] == "C:/ti/ccsref/src/util.c", "util.c is a linked resource, at its own absolute path");
        check(rel.sources.size() == 2 && rel.sources[0] == r.dir + "/main.c" && rel.sources[1] == "C:/ti/ccsref/src/util.c",
              "the sources are the folder's main.c and the linked util.c");
        check(r.notBuilt.empty() && rel.excluded.empty(), "nothing is excluded and nothing is of a kind RIDE does not build");

        std::string line = editor::ccs::report(r, editor::ConfigRelease);
        check(line.find("CCS project K6747c (Release): 1 option not supported, using RIDE's defaults: --opt_for_speed=5") == 0,
              "the Messages line names the option and the project: " + line);
        check(line.find("; RIDE's own instead of: -mv6740, --diag_warning=225") != std::string::npos, "and then what RIDE decides for itself");
        checkEqual(editor::ccs::report(r, editor::ConfigDebug).substr(0, 54), "CCS project K6747c (Debug): no unsupported options; RI", "Debug's line says nothing is unsupported");
        // The linked file is the Windows box's; there it is present and the line is empty.
        const bool linkedHere = editor::path::exists("C:/ti/ccsref/src/util.c");
        std::string sources = editor::ccs::sourceReport(r);
        check(linkedHere ? sources.empty() : sources.find("linked file not on this machine: C:/ti/ccsref/src/util.c") != std::string::npos,
              "the sources line says the linked file is not here, or nothing where it is: " + sources);
        std::string text = editor::ccs::mappingText(r, editor::ConfigRelease);
        check(text.find("--define=c6747 --define=NDEBUG --define=LEVEL=2 -> -Dc6747 -DNDEBUG -DLEVEL=2") != std::string::npos &&
                  text.find("edit them in CCS") != std::string::npos, "the dialog's text has each mapping and says where to edit");

        // -- macros
        checkEqual(editor::ccs::resolveMacros("${PROJECT_ROOT}/x/${ProjName}.out", r), r.dir + "/x/K6747c.out", "${PROJECT_ROOT} and ${ProjName}");
        checkEqual(editor::ccs::resolveMacros("${workspace_loc:/Other/inc}", r), editor::path::parent(r.dir) + "/Other/inc", "${workspace_loc:/P/x} is a sibling project's");
        checkEqual(editor::ccs::resolveMacros("${CG_TOOL_ROOT}/lib", r), r.cgToolRoot + "/lib", "${CG_TOOL_ROOT} the compiler's");
        checkEqual(editor::ccs::resolveMacros("${NOPE}/x", r), "${NOPE}/x", "a macro nobody knows is left as written");
        checkEqual(editor::ccs::cgToolRootFor("", "8.2.2"), editor::settings::ti(), "with no root, ${CG_TOOL_ROOT} is the TI directory tms6747 builds use");
        checkEqual(editor::ccs::cgToolRootFor(root, "9.1.0"), root + "/tools/compiler/ti-cgt-c6000_9.1.0", "a version not installed is named as TI would");
    }

    // -- K6747cpp, CCS 7.4: -O3, -ms3, a define with a space, --rom_model stored
    {
        Reading r;
        std::string error;
        check(editor::ccs::read(k74cpp, r, error), "K6747cpp (7.4) reads: " + error);
        const Config& rel = r.configs[1];
        checkEqual(rel.opt, "-O2", "-O3 becomes -O2, the highest cpp11 has");
        check(anyContains(rel.mapped, "-O3 -> -O2 (cpp11 and c90 have no -O3)"), "and the mapping says so");
        check(rel.unsupported.size() == 1 && rel.unsupported[0] == "-ms3", "-ms3 is not supported");
        check(hasEntry(rel.defines, "GREETING=hi there") && hasEntry(rel.defines, "LEVEL=3"), "\"GREETING=hi there\" loses CCS's outer quotes");
        checkEqual(rel.link.heap, "0x2000", "its heap");
        check(rel.link.romModel == 1 && anyContains(rel.mapped, "--rom_model (CCS's default) -> lnk6x --rom_model"),
              "--rom_model, which CCS did not store because it is the default, maps all the same");
        check(rel.link.libraries.size() == 1 && rel.link.libraries[0] == "rts6740_elf_eh.lib", "rts6740_elf_eh.lib is the library");
        check(hasEntry(rel.ownInstead, "--exceptions"), "--exceptions is always on in cpp11: RIDE's own");
        check(rel.sources.size() == 1 && rel.sources[0] == r.dir + "/main.cpp", "main.cpp is the source");
    }

    // -- CCS 5.5: quoted values, --abi=eabi, no ${PROJECT_ROOT} include
    {
        Reading r;
        std::string error;
        check(editor::ccs::read(k55c, r, error), "K6747c (5.5) reads: " + error);
        checkEqual(r.cgtVersion, "7.4.4", "its compiler is 7.4.4");
        checkEqual(r.ccsVersion, "", "and 5.5 records no CCS version");
        checkEqual(r.cgToolRoot, root + "/tools/compiler/c6000_7.4.4", "${CG_TOOL_ROOT} is the 7.4.4 compiler, named the old way");
        const Config& rel = r.configs[1];
        check(rel.includes.size() == 1 && rel.includes[0] == r.dir + "/inc", "\"${PROJECT_ROOT}/inc\" loses its quotes and resolves; there is no bare ${PROJECT_ROOT} in 5.5");
        check(hasEntry(rel.ownInstead, "--abi=eabi"), "--abi=eabi is RIDE's own");
        check(rel.link.libraries.size() == 1 && rel.link.libraries[0] == "rts6740_elf_eh.lib", "\"libc.a\" and \"rts6740_elf_eh.lib\" are one runtime");
        check(rel.link.searchPaths.size() == 1 && rel.link.searchPaths[0] == "C:/ti/c6747-lib", "the quoted -i path is unquoted");
        checkEqual(rel.opt, "-O2", "Release is -O2 by 5.5's definition too");
        Reading cpp;
        check(editor::ccs::read(k55cpp, cpp, error) && cpp.configs[1].unsupported.size() == 1 && cpp.configs[1].unsupported[0] == "-ms3",
              "K6747cpp (5.5) reads, -ms3 unsupported");
    }

    // -- P7misc: an exclusion, a build variable, a post-build step, per-file options
    {
        Reading r;
        std::string error;
        check(editor::ccs::read(p7, r, error), "P7misc reads: " + error);
        check(r.macros.count("MYLIBDIR") == 1 && r.macros["MYLIBDIR"] == "C:/ti/c6747-lib", "the build variable is read");
        const Config& dbg = r.configs[0];
        check(dbg.link.searchPaths.size() == 1 && dbg.link.searchPaths[0] == "C:/ti/c6747-lib", "and ${MYLIBDIR} resolves in the search path");
        checkEqual(dbg.postbuild, "echo post-build P7misc.out", "the post-build step, ${BuildArtifactFileName} resolved");
        check(dbg.excluded.size() == 1 && dbg.excluded[0] == "extra.c", "extra.c is excluded");
        bool noExtra = true;
        for (size_t i = 0; i < dbg.sources.size(); ++i) if (dbg.sources[i] == r.dir + "/extra.c") noExtra = false;
        check(dbg.sources.size() == 2 && dbg.sources[0] == r.dir + "/lib/util.c" && dbg.sources[1] == r.dir + "/main.c" && noExtra,
              "so main.c and lib/util.c are the sources, and the #error in extra.c is never compiled");
        check(dbg.unsupported.size() == 1 && dbg.unsupported[0].compare(0, 11, "lib/util.c:") == 0 &&
                  dbg.unsupported[0].find("-O3") != std::string::npos && dbg.unsupported[0].find("--define=PERFILE=1") != std::string::npos &&
                  dbg.unsupported[0].find("(per-file options)") != std::string::npos,
              "the file's own options are named as unsupported, only where they differ: " + (dbg.unsupported.empty() ? "" : dbg.unsupported[0]));
        check(r.configs[1].unsupported.empty(), "Release, with no per-file options, has none");
    }

    // -- a project that is not a C674x, a COFF one, a big-endian one: refused, naming why
    {
        file::path arm = dir / "ArmThing";
        file::create_directories(arm);
        writeSource((arm / ".project").string(), "<?xml version=\"1.0\"?>\n<projectDescription><name>ArmThing</name></projectDescription>\n");
        writeSource((arm / ".ccsproject").string(),
                    "<?xml version=\"1.0\"?>\n<projectOptions><deviceVariant value=\"Cortex A.AM3358\"/><deviceFamily value=\"ARM\"/>"
                    "<codegenToolVersion value=\"16.9.6\"/><isElfFormat value=\"true\"/></projectOptions>\n");
        writeSource((arm / ".cproject").string(), "<?xml version=\"1.0\"?>\n<cproject/>\n");
        Reading r;
        std::string error;
        check(!editor::ccs::read(arm.string(), r, error) && error.find("Cortex A.AM3358") != std::string::npos &&
                  error.find("C674x") != std::string::npos, "an ARM project is refused, its device named: " + error);
        std::string ccsproject = readWholeFile(editor::path::join(k74c, ".ccsproject"));
        file::path coff = dir / "CoffThing";
        file::create_directories(coff);
        writeSource((coff / ".project").string(), "<?xml version=\"1.0\"?>\n<projectDescription><name>CoffThing</name></projectDescription>\n");
        std::string asCoff = ccsproject;
        asCoff.replace(asCoff.find("isElfFormat value=\"true\""), 24, "isElfFormat value=\"false\"");
        writeSource((coff / ".ccsproject").string(), asCoff.c_str());
        writeSource((coff / ".cproject").string(), readWholeFile(editor::path::join(k74c, ".cproject")).c_str());
        check(!editor::ccs::read(coff.string(), r, error) && error.find("COFF") != std::string::npos, "a COFF project is refused: " + error);
        std::string asBig = ccsproject;
        asBig.replace(asBig.find("deviceEndianness value=\"little\""), 31, "deviceEndianness value=\"big\"");
        writeSource((coff / ".ccsproject").string(), asBig.c_str());
        check(!editor::ccs::read(coff.string(), r, error) && error.find("big-endian") != std::string::npos, "and a big-endian one: " + error);
        writeSource((coff / ".cproject").string(), "<cproject><a>\n");
        writeSource((coff / ".ccsproject").string(), ccsproject.c_str());
        check(!editor::ccs::read(coff.string(), r, error) && error.find(".cproject") != std::string::npos && error.find("line") != std::string::npos,
              "a file that will not parse is refused with its line: " + error);
    }

    // -- through Project: the switch, the groups, the options store, and nothing of CCS's written
    {
        const std::string before = ccsFingerprint(k74c);
        editor::settings::rememberCcs(false, root);
        editor::Project off;
        std::string error;
        check(!off.load(k74c, error) && error.empty(), "with the switch off a CCS folder is no project, as before");
        editor::settings::rememberCcs(true, root);
        editor::Project project;
        check(project.load(k74c, error), "with it on, the folder opens as a project: " + error);
        check(project.isCcs() && project.loaded(), "and knows what it is");
        checkEqual(project.name(), "K6747c", "named as CCS named it");
        checkEqual(project.arch(), "tms6747", "for the C6747");
        check(project.toolchain() == editor::ToolAuto, "each file to the compiler its extension picks");
        check(project.groups().size() == 3 && project.groups()[0].name == "Sources" &&
                  project.groups()[1].name == "Headers" && project.groups()[2].name == "Linked",
              "Sources, Headers and Linked are its groups");
        check(project.groups()[1].files.size() == 1 && project.groups()[1].files[0] == "inc/config.h",
              "inc/config.h under Headers, to be opened, and in no target group");
        check(std::find(project.target().groups.begin(), project.target().groups.end(), "Headers") == project.target().groups.end(),
              "a header is never handed to a compiler");
        check(project.groups()[0].files.size() == 1 && project.groups()[0].files[0] == "main.c", "main.c in Sources, by its relative name");
        check(project.groups()[2].files.size() == 1 && project.groups()[2].files[0] == "C:/ti/ccsref/src/util.c", "util.c in Linked, by its absolute one");
        checkEqual(project.absolute("C:/ti/ccsref/src/util.c"), "C:/ti/ccsref/src/util.c", "which absolute() leaves whole");
        check(project.builds() && project.target().groups.size() == 2, "and both groups are built");
        std::vector<std::string> includes = project.includes();
        check(includes.size() == 2 && includes[1] == project.root() + "/inc", "the include paths are the project's, resolved");
        const editor::options::Store& store = project.compilerOptions();
        checkEqual(store.value(editor::ConfigRelease, "c90.opt"), "-O2", "Compiler Options: Release -O2");
        checkEqual(store.value(editor::ConfigRelease, "c90.defines"), "c6747;NDEBUG;LEVEL=2", "the defines");
        checkEqual(store.value(editor::ConfigRelease, "c90.undefines"), "OLDAPI", "the undefine");
        checkEqual(store.value(editor::ConfigRelease, "c90.g"), "0", "no -g in Release");
        checkEqual(store.value(editor::ConfigRelease, "cpp11.compress"), "0", "--no_compress");
        checkEqual(store.value(editor::ConfigDebug, "c90.opt"), "-O0", "Debug is RIDE's default -O0");
        checkEqual(store.value(editor::ConfigDebug, "c90.g"), "1", "with -g");
        checkEqual(store.value(editor::ConfigDebug, "c90.defines"), "c6747", "and CCS's one define");
        std::string flags = editor::options::flags(store, editor::ToolCc1, editor::ConfigRelease, "tms6747");
        checkEqual(flags, " -O2 -Dc6747 -DNDEBUG -DLEVEL=2 -UOLDAPI", "what c90 is given for Release");
        checkEqual(editor::options::flags(store, editor::ToolCxx1, editor::ConfigRelease, "tms6747"),
                   " -O2 --no_compress -Dc6747 -DNDEBUG -DLEVEL=2 -UOLDAPI", "and cpp11, --no_compress included");
        editor::TiLink link = project.tiLink(editor::ConfigRelease);
        check(link.given && link.stack == "0x1000" && link.cmdFiles.size() == 1, "the link is handed the project's");
        check(!editor::Project().tiLink(editor::ConfigRelease).given, "and a .pro project hands nothing");
        std::vector<std::string> report = project.ccsReport(editor::ConfigRelease);
        const bool linkedHere = editor::path::exists("C:/ti/ccsref/src/util.c");
        check(report.size() == (linkedHere ? 1u : 2u) && report[0].compare(0, 28, "CCS project K6747c (Release)") == 0 &&
                  (linkedHere || report[1].find("linked file") != std::string::npos),
              "the Messages lines are the options line and, where the linked file is missing, the sources line");
        check(project.ccsMapping(editor::ConfigDebug).find("Debug - what CCS set") != std::string::npos, "the dialog's text is per configuration");
        std::string program = project.targetProgram();
        check(program.compare(0, project.root().size(), project.root()) != 0 && program.find("ride-ccs-K6747c") != std::string::npos,
              "the program is built outside the CCS folder, never into it: " + program);
        check(project.ccsConfiguration() == -1, "no configuration is remembered until one is chosen");
        check(project.rememberConfiguration(editor::ConfigRelease) && project.ccsConfiguration() == editor::ConfigRelease,
              "choosing one keeps it in settings.json");
        project.setOpenFile("main.c");
        check(project.save(error), "save() keeps RIDE's state: " + error);
        editor::Json state = editor::settings::ccsProjectState(k74c);
        checkEqual(state.get("open").text(), "main.c", "the open file, in settings.json under the project's path");
        checkEqual(state.get("config").text(), "release", "beside the configuration");
        checkEqual(readWholeFile(editor::settings::installFile()).find("\"projects\"") != std::string::npos ? "yes" : "no", "yes", "under \"ccs\": { \"projects\" }");
        check(project.reloadIfCcs(error) && project.isCcs() && project.openFile() == "main.c", "a reload reads the files again and keeps the open file");
        editor::Project again;
        check(again.load(k74c, error) && again.openFile() == "main.c" && again.ccsConfiguration() == editor::ConfigRelease,
              "and the next open finds both again");
        editor::Project byFile;
        check(byFile.load(editor::path::join(k74c, ".cproject"), error) && byFile.isCcs(), "naming .cproject opens its folder");
        checkEqual(ccsFingerprint(k74c), before, "and not a byte of CCS's files, nor the folder's listing, has changed");

        editor::Project misc;
        check(misc.load(p7, error) && misc.groups().size() == 3 && misc.groups()[1].name == "Headers" &&
                  misc.groups()[2].name == "Excluded" &&
                  misc.groups()[2].files.size() == 1 && misc.groups()[2].files[0] == "extra.c",
              "P7misc's extra.c is shown under Excluded");
        check(misc.target().groups.size() == 1 && misc.target().groups[0] == "Sources", "which the target does not build");

        // The window's seam.
        RIDEProject* bridged = ride_project_new();
        char why[256] = {0};
        check(ride_project_load(bridged, k74c.c_str(), why, sizeof why) != 0 && ride_project_is_ccs(bridged) != 0, "the bridge opens it and says it is CCS's");
        check(std::string(ride_project_ccs_report(bridged, RIDE_CONFIG_RELEASE)).compare(0, 28, "CCS project K6747c (Release)") == 0, "and answers the report");
        check(std::string(ride_project_ccs_mapping(bridged, RIDE_CONFIG_RELEASE)).find("edit them in CCS") != std::string::npos, "and the mapping");
        ride_options_begin(bridged);
        ride_options_set(bridged, RIDE_CONFIG_RELEASE, "c90.opt", "-O0");
        check(ride_options_commit(bridged) == 0, "the options dialog commits nothing for it");
        checkEqual(ride_options_value(bridged, RIDE_CONFIG_RELEASE, "c90.opt"), "-O0", "though the draft it showed was editable in memory");
        check(ride_project_ccs_configuration(bridged) == RIDE_CONFIG_RELEASE, "and the remembered configuration is answered");
        ride_project_remember_configuration(bridged, RIDE_CONFIG_DEBUG);
        check(ride_project_ccs_configuration(bridged) == RIDE_CONFIG_DEBUG, "and changed");
        ride_project_free(bridged);
        checkEqual(ccsFingerprint(k74c), before, "still nothing of CCS's written");
        check(!editor::Project().isCcs(), "a project that is not CCS's says so");
    }

    editor::settings::rememberCcs(false, "");
    editor::settings::pretendInstalledAt(std::string());
    editor::ccs::setDefinitionsDir(std::string());
    file::remove_all(dir);
}

// **The reference projects built and run through the console**, as RIDE --build does: K6747c and
// K6747cpp copied out of the reference tree - the linked util.c pointed at the copy shipped beside
// them, since C:/ti/ccsref is the Windows box's - built for tms6747 through c90 or cpp11, asm6x
// and lnk6x against the project's own C6747.cmd, and run on vm6747. Every tool is named outright
// or found beside RIDE.exe; what is missing is said and the case is skipped.
void ccsProjectsBuiltAndRun() {
    std::printf("CCS projects, built and run\n");
    const std::string reference = ccsReference();
    std::string program = editor::path::programDirectory();
    // The console: RIDE.exe beside the suite or above it, RIDEConsole.exe in bin\ on Windows.
    const std::string consoles[] = {
        editor::path::join(program, "RIDE.exe"), editor::path::join(editor::path::parent(program), "RIDE.exe"),
        editor::path::join(editor::path::join(program, "bin"), "RIDEConsole.exe"),
        editor::path::join(editor::path::join(editor::path::parent(program), "bin"), "RIDEConsole.exe"),
    };
    std::string ride = consoles[0];
    for (size_t i = 0; i < sizeof consoles / sizeof consoles[0]; ++i)
        if (editor::path::exists(consoles[i])) { ride = consoles[i]; break; }
    const char* cc1 = std::getenv("CC1");
    const char* cxx1 = std::getenv("CXX1");
    std::string beside = editor::path::parent(ride);
    const char* fromEnv = std::getenv("ASM6X");
    std::string asm6x = fromEnv && *fromEnv ? fromEnv : editor::path::join(beside, "asm6x.exe");
    fromEnv = std::getenv("VM6747");
    std::string vm = fromEnv && *fromEnv ? fromEnv : editor::path::join(beside, "vm6747.exe");
    fromEnv = std::getenv("LNK6X");
    std::string lnk = fromEnv && *fromEnv ? fromEnv : editor::path::join(beside, "lnk6x.exe");
    fromEnv = std::getenv("SIM6747");
    std::string sim = fromEnv && *fromEnv ? fromEnv : editor::path::join(beside, "sim6747.exe");
    fromEnv = std::getenv("C6747_EHLIB");
    std::string ehlib = fromEnv && *fromEnv ? fromEnv : editor::path::join(editor::path::homeDir(), "c6747-lib");
    std::string missing;
    if (reference.empty()) missing += " docs/ccs-reference";
    if (!editor::path::exists(ride)) missing += " RIDE.exe";
    if (!cc1 || !*cc1 || !editor::path::exists(cc1)) missing += " $CC1";
    if (!cxx1 || !*cxx1 || !editor::path::exists(cxx1)) missing += " $CXX1";
    if (!editor::path::exists(asm6x)) missing += " asm6x";
    if (!editor::path::exists(vm)) missing += " vm6747";
    if (!editor::path::exists(lnk)) missing += " lnk6x";
    if (!editor::path::exists(sim)) missing += " sim6747";
    if (!missing.empty()) { skipped("not here, so nothing is built:%s", missing.c_str()); return; }
    // TI's runtime is a third party's, and RIDE does not ship it: its absence is not RIDE's (D14).
    if (!editor::path::exists(editor::path::join(ehlib, "rts6740_elf_eh.lib"))) {
        skippedExternal("no rts6740_elf_eh.lib in %s, so the CCS projects are not linked against TI's runtime", ehlib.c_str());
        return;
    }

    file::path dir = file::temp_directory_path() / "ride-ccs-run";
    file::remove_all(dir);
    file::create_directories(dir / "home");
    file::create_directories(dir / "ti" / "bin");
    std::string util = editor::path::withSlashes(editor::path::join(editor::path::join(reference, "src"), "util.c"));
    const char* names[2] = { "K6747c", "K6747cpp" };
    const char* release[2] = { "K6747c: level 2, twice(21) = 42", "K6747cpp: level 3, counter 42" };
    const char* debug[2] = { "K6747c: level 0, twice(21) = 42", "K6747cpp: level 0, counter 42" };
    std::string homeWas = editor::path::homeDir();
    sayWhereHomeIs((dir / "home").string());
#ifdef _WIN32
    _putenv_s("ASM6X", asm6x.c_str());
    _putenv_s("VM6747", vm.c_str());
    _putenv_s("SIM6747", sim.c_str());
#else
    setenv("ASM6X", asm6x.c_str(), 1);
    setenv("VM6747", vm.c_str(), 1);
    setenv("SIM6747", sim.c_str(), 1);
#endif
    for (int i = 0; i < 2; ++i) {
        std::string from = editor::path::join(editor::path::join(reference, "ccs74"), names[i]);
        std::string to = (dir / names[i]).string();
        copyTree(from, to);
        std::string project = readWholeFile(editor::path::join(to, ".project"));
        const std::string linked = "C:/ti/ccsref/src/util.c";
        size_t at = project.find(linked);
        if (at != std::string::npos) project.replace(at, linked.size(), util);
        writeSource(editor::path::join(to, ".project"), project.c_str());
        const std::string before = ccsFingerprint(to);
        for (int r = 0; r < 2; ++r) {
            std::string config = r ? "release" : "debug";
            std::string command = "\"" + ride + "\" \"" + to + "\" --ccs --run --config " + config +
                                  " --c90 \"" + std::string(cc1) + "\" --cpp11 \"" + std::string(cxx1) + "\"" +
                                  " --ti \"" + (dir / "ti").string() + "\" --tilib \"" + ehlib + "\" --tilinker \"" + lnk + "\"";
            std::string output;
            int status = editor::runCaptured(command, output);
            std::string what = std::string(names[i]) + " " + config;
            check(status == 0, what + " builds and runs through the console (status " + std::to_string(status) + "):\n" + output);
            check(output.find("CCS project " + std::string(names[i]) + " (" + (r ? "Release" : "Debug") + ")") != std::string::npos,
                  what + ": the console carries the CCS line");
            check(output.find("C6747.cmd -o") != std::string::npos, what + ": lnk6x links against the project's own C6747.cmd");
            check(output.find(r ? release[i] : debug[i]) != std::string::npos, what + ": prints what CCS's build of it prints");
            check(output.find("$ sim6747 --run") != std::string::npos, what + ": Run is the .out on sim6747 (R5):\n" + output);
            checkEqual(ccsFingerprint(to), before, what + ": and leaves the CCS folder as it was");
            // Emulate on vm6747: the assembly run, and no .out linked for it.
            command.replace(command.find(" --run "), 7, " --emulate ");
            output.clear();
            status = editor::runCaptured(command, output);
            check(status == 0 && output.find(r ? release[i] : debug[i]) != std::string::npos &&
                  output.find("$ vm6747 ") != std::string::npos && output.find("[no .out: Emulate on vm6747") != std::string::npos,
                  what + ": --emulate runs the assembly on vm6747 and links nothing:\n" + output);
            checkEqual(ccsFingerprint(to), before, what + ": and Emulate leaves the CCS folder as it was");
        }
    }
    sayWhereHomeIs(homeWas);
    file::remove_all(dir);
}

// **RTS6x links the .out, with nothing of TI's named** (5.1, M7): a project for tms6747 built through
// the console with no TI compiler under Tools - cpp11, asm6x and RIDE's lnk6x against rts6x.lib - and
// the .out run on sim6747. The tools are named or found beside RIDE.exe; what is missing skips the case.
void rts6xLinksTheOut() {
    std::printf("RTS6x links the .out\n");
    std::string program = editor::path::programDirectory();
    const std::string consoles[] = {
        editor::path::join(program, "RIDE.exe"), editor::path::join(editor::path::parent(program), "RIDE.exe"),
        editor::path::join(editor::path::join(program, "bin"), "RIDEConsole.exe"),
        editor::path::join(editor::path::join(editor::path::parent(program), "bin"), "RIDEConsole.exe"),
        editor::path::join(editor::path::join(editor::path::parent(program), "bin"), "RIDE.exe"),
    };
    std::string ride = consoles[0];
    for (size_t i = 0; i < sizeof consoles / sizeof consoles[0]; ++i)
        if (editor::path::exists(consoles[i])) { ride = consoles[i]; break; }
    const std::string beside = editor::path::parent(ride);
    const char* cxx1 = std::getenv("CXX1");
    const char* fromEnv = std::getenv("ASM6X");
    std::string asm6x = fromEnv && *fromEnv ? fromEnv : editor::path::join(beside, "asm6x.exe");
    fromEnv = std::getenv("LNK6X");
    std::string lnk = fromEnv && *fromEnv ? fromEnv : editor::path::join(beside, "lnk6x.exe");
    fromEnv = std::getenv("SIM6747");
    std::string sim = fromEnv && *fromEnv ? fromEnv : editor::path::join(beside, "sim6747.exe");
    fromEnv = std::getenv("RTS6X");
    std::string rts = fromEnv && *fromEnv ? fromEnv : editor::path::join(editor::path::join(beside, "lib"), "rts6x-tms6747");
    std::string missing;
    if (!editor::path::exists(ride)) missing += " RIDE.exe";
    if (!cxx1 || !*cxx1 || !editor::path::exists(cxx1)) missing += " $CXX1";
    if (!editor::path::exists(asm6x)) missing += " asm6x";
    if (!editor::path::exists(lnk)) missing += " lnk6x";
    if (!editor::path::exists(sim)) missing += " sim6747";
    if (!editor::path::exists(editor::path::join(rts, "rts6x.lib"))) missing += " rts6x.lib";
    if (!editor::path::exists(editor::path::join(rts, "rts6xd.lib"))) missing += " rts6xd.lib";
    if (!missing.empty()) { skipped("not here, so nothing is built:%s", missing.c_str()); return; }

    file::path dir = file::temp_directory_path() / "ride-rts6x-run";
    file::remove_all(dir);
    file::create_directories(dir / "home");
    file::create_directories(dir / "p");
    writeSource((dir / "p" / "project.pro").string(),
                "{ \"arch\": \"tms6747\", \"groups\": { \"Sources\": [\"main.cpp\"] } }\n");
    writeSource((dir / "p" / "main.cpp").string(),
                "extern \"C\" int printf(const char *, ...);\n"
                "struct Oops { int code; Oops(int c) : code(c) {} };\n"
                "static int risky(int n) { if (n > 2) throw Oops(n * 7); return n; }\n"
                "int main() {\n"
                "    int sum = 0;\n"
                "    for (int i = 0; i < 5; ++i) {\n"
                "        try { sum += risky(i); } catch (const Oops &o) { printf(\"caught %d\\n\", o.code); }\n"
                "    }\n"
                "    printf(\"sum %d %.2f\\n\", sum, sum / 4.0);\n"
                "    return 0;\n"
                "}\n");
    // A home of its own: the user's settings.json may name a TI compiler, which would put TI's runtime back.
    std::string homeWas = editor::path::homeDir();
    sayWhereHomeIs((dir / "home").string());
#ifdef _WIN32
    _putenv_s("ASM6X", asm6x.c_str()); _putenv_s("SIM6747", sim.c_str()); _putenv_s("RTS6X", rts.c_str());
#else
    setenv("ASM6X", asm6x.c_str(), 1); setenv("SIM6747", sim.c_str(), 1); setenv("RTS6X", rts.c_str(), 1);
#endif
    // Release links rts6x.lib, built at -O2; Debug links rts6xd.lib, built at -O0 with _DEBUG.
    const char* configs[2] = { "release", "debug" };
    const char* libs[2] = { "rts6x.lib", "rts6xd.lib" };
    for (int c = 0; c < 2; ++c) {
        std::string command = "\"" + ride + "\" \"" + (dir / "p").string() + "\" --simulate --config " + configs[c] +
                              " --cpp11 \"" + std::string(cxx1) + "\" --tilinker \"" + lnk + "\"";
        std::string output, what = configs[c];
        int status = editor::runCaptured(command, output);
        check(status == 0, what + " builds and runs on sim6747 (status " + std::to_string(status) + "):\n" + output);
        check(output.find(std::string("objects, ") + libs[c]) != std::string::npos, what + ": lnk6x links against " + libs[c] + ":\n" + output);
        check(output.find("rts6740") == std::string::npos, what + ": and nothing of TI's runtime is named:\n" + output);
        check(output.find("[linked ") != std::string::npos, what + ": the .out is linked");
        check(output.find("caught 21") != std::string::npos && output.find("caught 28") != std::string::npos,
              what + ": both exceptions are caught by RTS6x's unwinder:\n" + output);
        check(output.find("sum 3 0.75") != std::string::npos, what + ": and printf6x prints what the program computed:\n" + output);
    }
    // Shalimar beside it: the packed runtime, shmrt6x.lib or shmrt6xd.lib, linked ahead of RTS6x's (M9).
    const char* shmLibs[2] = { "shmrt6x.lib", "shmrt6xd.lib" };
    const char* shc = std::getenv("SHC");
    if (shc && *shc && editor::path::exists(shc) && editor::path::exists(editor::path::join(rts, "shmrt6x.lib"))) {
        file::create_directories(dir / "s");
        writeSource((dir / "s" / "count.shl").string(),
                    "fun <> = main() {\n  n : 1\n  while n < 4 {\n    ? n\n    n : n + 1\n  }\n  ? \"done\"\n}\n");
        for (int c = 0; c < 2; ++c) {
            std::string command = "\"" + ride + "\" \"" + (dir / "s").string() + "\" --arch tms6747 --simulate --config " +
                                  configs[c] + " --shalimar \"" + std::string(shc) + "\" --tilinker \"" + lnk + "\"";
            std::string output, what = std::string("shalimar ") + configs[c];
            int status = editor::runCaptured(command, output);
            check(status == 0, what + " builds and runs on sim6747 (status " + std::to_string(status) + "):\n" + output);
            check(output.find(std::string(shmLibs[c]) + ", " + libs[c]) != std::string::npos,
                  what + ": lnk6x links " + shmLibs[c] + " ahead of " + libs[c] + ":\n" + output);
            check(output.find("$ asm6x 1 sources") != std::string::npos, what + ": only the program is assembled:\n" + output);
            check(output.find("done") != std::string::npos, what + ": and it runs to its end:\n" + output);
        }
    } else {
        skipped("no $SHC or no shmrt6x.lib, so the Shalimar half is not built");
    }
    sayWhereHomeIs(homeWas);
    file::remove_all(dir);
}

// **A CCS workspace, one project at a time** (RIDE 4.7): a workspace laid out as CCS 7.4 left one on the
// Windows box on 02-10-2026 - a project copied in, one registered where it was through a .location, a
// project that is not CCS's, a path variable and a build macro - opened through <workspace>/<project>.pro.
void ccsWorkspacesOneProjectAtATime() {
    std::printf("CCS workspaces, one project at a time\n");
    const std::string reference = ccsReference();
    if (reference.empty()) { skipped("no docs/ccs-reference beside the suite, so nothing is read"); return; }
    const std::string k74c = editor::path::join(editor::path::join(reference, "ccs74"), "K6747c");
    const std::string k74cpp = editor::path::join(editor::path::join(reference, "ccs74"), "K6747cpp");

    file::path dir = file::temp_directory_path() / "ride-ccs-workspace-test";
    file::remove_all(dir);
    file::path ws = dir / "workspace_v7", away = dir / "else where";
    file::path registry = ws / ".metadata" / ".plugins" / "org.eclipse.core.resources" / ".projects";
    file::path prefs = ws / ".metadata" / ".plugins" / "org.eclipse.core.runtime" / ".settings";
    file::create_directories(registry / "K6747c");
    file::create_directories(registry / "K6747cpp");
    file::create_directories(registry / "RemoteSystemsTempFiles");
    file::create_directories(ws / "RemoteSystemsTempFiles");
    file::create_directories(prefs);
    file::create_directories(away);
    copyTree(k74c, (ws / "K6747c").string());
    copyTree(k74cpp, (away / "K6747cpp").string());
    {
        // The .location CCS 7.4 wrote, its 16-byte marker and writeUTF of the URI; the space as %20.
        std::string where = editor::path::withSlashes((away / "K6747cpp").string());
        std::string uri = "URI//file:" + std::string(where[0] == '/' ? "" : "/");
        for (size_t i = 0; i < where.size(); ++i) uri += where[i] == ' ' ? std::string("%20") : std::string(1, where[i]);
        std::string record("\x40\xB1\x8B\x81\x23\xBC\x00\x14\x1A\x25\x96\xE7\xA3\x93\xBE\x1E", 16);
        record += static_cast<char>(uri.size() >> 8);
        record += static_cast<char>(uri.size() & 0xFF);
        record += uri + std::string(14, '\0') + "\xC0\x58\xFB\xF3\x23\xBC\x00\x14\x1A\x51\xF3\x8C\x7B\xBB\x77\xC6";
        std::ofstream((registry / "K6747cpp" / ".location").string(), std::ios::binary) << record;
        std::ofstream((prefs / "org.eclipse.core.resources.prefs").string(), std::ios::binary)
            << "eclipse.preferences.version=1\r\npathvariable.PROBE_ROOT=C\\:/Users/GRA/ws47src\r\n";
        std::ofstream((prefs / "org.eclipse.cdt.core.prefs").string(), std::ios::binary)
            << "eclipse.preferences.version=1\r\nmacros/workspace=<?xml version\\=\"1.0\" encoding\\=\"UTF-8\" standalone\\=\"no\"?>\\r\\n"
               "<macros>\\r\\n<stringMacro name\\=\"PROBE_ROOT\" type\\=\"VALUE_PATH_DIR\" value\\=\"C\\:/Users/GRA/ws47src\"/>\\r\\n</macros>\\r\\n\r\n";
    }
    const std::string wsDir = editor::path::withSlashes(ws.string());
    const std::string awayDir = editor::path::withSlashes((away / "K6747cpp").string());

    // -- the reading
    check(editor::ccs::isWorkspace(wsDir) && !editor::ccs::isWorkspace(k74c), "a folder with .metadata is a workspace, a project folder is not");
    editor::ccs::Workspace read;
    std::string error;
    check(editor::ccs::readWorkspace(wsDir, read, error), "the workspace reads: " + error);
    check(read.projects.size() == 2 && read.projects[0].name == "K6747c" && read.projects[1].name == "K6747cpp",
          "its two CCS projects, by name; RemoteSystemsTempFiles is not CCS's");
    check(read.projects.size() == 2 && read.projects[0].inside && editor::path::same(read.projects[0].location, wsDir + "/K6747c"),
          "the copied one is in the workspace folder");
    check(read.projects.size() == 2 && !read.projects[1].inside && editor::path::same(read.projects[1].location, awayDir),
          "the other where its .location says, %20 a space: " + (read.projects.size() == 2 ? read.projects[1].location : std::string()));
    checkEqual(read.pathVariables["PROBE_ROOT"], "C:/Users/GRA/ws47src", "a path variable, its escapes undone");
    checkEqual(read.macros["PROBE_ROOT"], "C:/Users/GRA/ws47src", "a workspace build macro, out of the XML in the preference");
    checkEqual(editor::ccs::workspaceOf(wsDir + "/K6747c"), wsDir, "a project folder knows the workspace it is registered in");
    checkEqual(editor::ccs::workspaceOf(awayDir), "", "one imported from elsewhere cannot tell, and opens alone");

    // -- the .pro: workspace and project and nothing else
    std::string pro;
    check(editor::ccs::writePro(wsDir, "K6747cpp", pro, error) && pro == wsDir + "/K6747cpp.pro", "the .pro is <workspace>/<project>.pro: " + error);
    {
        std::ifstream in(pro, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        std::string why;
        editor::Json root = editor::Json::parse(text.str(), why);
        check(root.size() == 1 && root.get("ccs").size() == 2 && root.get("ccs").get("workspace").text() == "." &&
                  root.get("ccs").get("project").text() == "K6747cpp", "and holds only the workspace and the project: " + text.str());
    }

    // -- opened through it, with the switch off: the .pro is RIDE's own word
    editor::settings::rememberCcs(false, std::string());
    const std::string before = ccsFingerprint(awayDir);
    {
        editor::Project project;
        check(project.load(pro, error), "the .pro opens its project: " + error);
        check(project.isCcs() && project.name() == "K6747cpp" && editor::path::same(project.root(), awayDir), "the one registered elsewhere, where it is");
        check(editor::path::same(project.ccsWorkspace(), wsDir) && editor::path::same(project.file(), pro),
              "remembered as its .pro, a member of its workspace");
        const editor::ccs::Reading& r = project.ccsReading();
        checkEqual(editor::ccs::resolveMacros("${workspace_loc}", r), wsDir, "${workspace_loc} is the workspace, not the folder's parent");
        checkEqual(editor::ccs::resolveMacros("${workspace_loc:/K6747c/inc}", r), wsDir + "/K6747c/inc", "${workspace_loc:/P/x} a member's, by name");
        checkEqual(editor::ccs::resolveMacros("${PROBE_ROOT}/lib", r), "C:/Users/GRA/ws47src/lib", "and a workspace build macro resolves");
        check(project.reloadIfCcs(error) && editor::path::same(project.file(), pro) && editor::path::same(project.ccsWorkspace(), wsDir),
              "read again for a build, it stays the workspace's: " + error);
    }
    checkEqual(ccsFingerprint(awayDir), before, "and nothing of CCS's is written");

    // -- a workspace is not a project; a member's folder opens through its .pro
    {
        editor::Project project;
        check(!project.load(wsDir, error) && error.find("is a CCS workspace") != std::string::npos &&
                  error.find("K6747c, K6747cpp") != std::string::npos, "a workspace opened says it is one and names its projects: " + error);
        check(!file::exists(ws / "K6747c.pro"), "K6747c has no .pro yet");
        check(project.load(wsDir + "/K6747c", error) && project.isCcs() && project.name() == "K6747c", "its folder opens, switch off: " + error);
        check(file::exists(ws / "K6747c.pro") && editor::path::same(project.file(), wsDir + "/K6747c.pro"), "through the .pro it writes beside the others");
        check(project.load(wsDir + "/K6747cpp.pro", error) && project.name() == "K6747cpp", "an existing .pro named on its own: " + error);
        file::remove_all(ws / "K6747cpp.pro");
        check(project.load(wsDir + "/K6747cpp.pro", error) && file::exists(ws / "K6747cpp.pro") && project.name() == "K6747cpp",
              "a .pro not yet written, named on a command line, is written for the workspace's project: " + error);
        check(!project.load(wsDir + "/Nobody.pro", error) && error.find("no project Nobody") != std::string::npos && !file::exists(ws / "Nobody.pro"),
              "and one the workspace has no project for is refused, writing nothing: " + error);
        std::string bad;
        editor::ccs::writePro(wsDir, "Gone", bad, error);
        check(!project.load(bad, error) && error.find("has no CCS project Gone") != std::string::npos, "a .pro naming no member says so: " + error);
    }
    file::remove_all(dir);
}

// **Build > Clean** removes what a build made and nothing it did not: the program and what sits
// beside it, the emulated target's .vm, RIDE's own folder for a CCS project, this process's scratch -
// and never a source, the .pro, a folder of the project's that shares the program's name, or another
// process's scratch.
// About ends with this program's own file: its name, the PKT time it was last written, and its CRC-32.
// Build > Command-line arguments: one line split as a shell would, at spaces outside quotes.
// Edit > Find in Files: the text in each file whose name fits, and the files whose names hold it.
void findingInFiles() {
    namespace pth = editor::path;
    const std::string home = pth::join(pth::absolute("."), "find-in-files-test");
    pth::removeTree(home);
    pth::makeDirectories(pth::join(home, "sub"));
    pth::makeDirectories(pth::join(home, "obj"));
    { std::ofstream a(pth::join(home, "one.cpp").c_str()); a << "int Total = 1;\nint total2;\n// total\n"; }
    { std::ofstream b(pth::join(home, "sub/two.h").c_str()); b << "extern int Total;\n"; }
    { std::ofstream c(pth::join(home, "notes.txt").c_str()); c << "Total in notes\n"; }
    { std::ofstream d(pth::join(home, "obj/skip.cpp").c_str()); d << "Total in a build folder\n"; }
    editor::FindInFiles q;
    q.text = "total";
    q.folder = home;
    q.patterns = "*.cpp;*.h";
    size_t files = 0; bool cut = false;
    std::vector<editor::FileHit> hits = editor::findInFiles(q, 100, files, cut);
    check(hits.size() == 4 && files == 2, "find in files: every line in *.cpp and *.h, a build folder passed over");
    check(!hits.empty() && hits[0].line == 1 && hits[0].col == 5, "each hit with its line and column");
    q.matchCase = true;
    hits = editor::findInFiles(q, 100, files, cut);
    check(hits.size() == 2, "matching case finds 'total' twice and not 'Total'");
    q.matchCase = false; q.wholeWord = true;
    hits = editor::findInFiles(q, 100, files, cut);
    check(hits.size() == 3, "a whole word does not find 'total2'");
    q.wholeWord = false; q.subfolders = false;
    hits = editor::findInFiles(q, 100, files, cut);
    check(hits.size() == 3 && files == 1, "without subfolders only the folder itself is searched");
    q.subfolders = true; q.namesOnly = true; q.text = "two"; q.patterns = "";
    hits = editor::findInFiles(q, 100, files, cut);
    check(hits.size() == 1 && hits[0].text == "two.h", "find files: the files whose names hold the text");
    q.namesOnly = false; q.text = "total"; q.patterns = "";
    hits = editor::findInFiles(q, 2, files, cut);
    check(hits.size() == 2 && cut, "and no more than asked for, saying it stopped there");
    std::atomic<bool> stop(true);
    q.stop = &stop;
    hits = editor::findInFiles(q, 100, files, cut);
    check(hits.empty() && cut && files == 0, "a search told to stop ends before its first file");
    pth::removeTree(home);
}

void commandLineArguments() {
    std::vector<std::string> got = editor::Project::splitArguments("-run sample.cpp  \"my file.cpp\" \"\" x");
    check(got.size() == 5 && got[0] == "-run" && got[1] == "sample.cpp" && got[2] == "my file.cpp" &&
              got[3].empty() && got[4] == "x",
          "command-line arguments split at spaces outside quotes, an empty pair of quotes kept");
    check(editor::Project::splitArguments("   ").empty(), "and a blank line is no arguments at all");
}

void aboutSelf() {
    const std::vector<std::string> said = editor::about::lines();
    // The last two lines: the release and the time, then the CRC and what the record says.
    const std::string last = said.size() < 2 ? std::string() : said[said.size() - 2] + "  " + said.back();
    std::printf("about's last line: %s\n", last.c_str());
    const std::string self = editor::path::programFile();
    check(!self.empty(), "the program knows its own file");
    const std::string named = std::string(editor::about::name()) + " " + editor::about::version() + "  ";
    check(last.compare(0, named.size(), named) == 0, "About's last line names the release, RIDE 5.17");
    check(last.find(" PKT") != std::string::npos, "and the time it was written, in PKT");
    const size_t at = last.find("CRC32 ");
    check(at != std::string::npos && last.size() >= at + 14 && last.find_first_not_of("0123456789ABCDEF", at + 6) == at + 14,
          "and its CRC-32, eight hex digits");
    // The CRC asked of the bytes a second way: zlib's check value of "123456789" is CBF43926.
    FILE* f = std::fopen(self.c_str(), "rb");
    unsigned long c = 0xFFFFFFFFUL; int ch;
    while (f && (ch = std::fgetc(f)) != EOF) { c ^= (unsigned)ch; for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320UL ^ (c >> 1) : c >> 1; }
    if (f) std::fclose(f);
    char hex[16]; std::snprintf(hex, sizeof hex, "%08lX", (c ^ 0xFFFFFFFFUL) & 0xFFFFFFFFUL);
    check(at != std::string::npos && last.substr(at + 6, 8) == hex, "the CRC-32 is the file's");

    // Against the release record beside the program: none, the same, or changed since.
    const std::string dir = editor::path::programDirectory();
    const std::string record = editor::path::join(dir, "release.crc");
    std::remove(record.c_str());
    check(editor::about::lines().back().find("- no release record") != std::string::npos,
          "with no release record About says so");
    check(editor::about::writeReleaseRecord(dir) > 0, "the release record is written for the programs beside it");
    check(editor::about::lines().back().find("- matches the release") != std::string::npos,
          "and About then finds this program unchanged");
    {
        std::ifstream in(record.c_str());
        std::stringstream all; all << in.rdbuf();
        std::string text = all.str();
        const size_t mine = text.find(" " + self.substr(self.find_last_of('/') + 1) + "\n");
        const size_t start = text.rfind('\n', mine) + 1;
        text.replace(start, 8, std::string(hex) == "00000000" ? "11111111" : "00000000");
        std::ofstream outFile(record.c_str()); outFile << text;
    }
    const std::string changed = editor::about::lines().back();
    check(changed.find("- CHANGED, released ") != std::string::npos,
          "and a record that disagrees says the program has CHANGED, with what it was");
    std::remove(record.c_str());
}

void cleaning() {
    std::printf("Build > Clean\n");
    file::path dir = file::temp_directory_path() / "ride-clean-test";
    file::remove_all(dir);
    file::create_directories(dir / "demo.vm");
    file::create_directories(dir / "demo.dSYM");
    file::create_directories(dir / "src");
    const char* made[] = { "demo", "demo.pdb", "demo.ilk", "demo.map", "demo.out" };
    for (size_t i = 0; i < sizeof made / sizeof *made; ++i) std::ofstream((dir / made[i]).string().c_str()) << "built\n";
    std::ofstream((dir / "demo.vm" / "main.s").string().c_str()) << "; assembly\n";
    std::ofstream((dir / "main.c").string().c_str()) << "int main(void) { return 0; }\n";
    std::ofstream((dir / "demo.pro").string().c_str()) << "{}\n";
    std::ofstream((dir / "src" / "a.c").string().c_str()) << "int a;\n";

    std::vector<std::string> removed = editor::cleanBuilt((dir / "demo").string(), false);
    for (size_t i = 0; i < sizeof made / sizeof *made; ++i)
        check(!file::exists(dir / made[i]), std::string("the build's ") + made[i] + " is removed");
    check(!file::exists(dir / "demo.vm") && !file::exists(dir / "demo.dSYM"), "and the .vm and .dSYM folders");
    check(file::exists(dir / "main.c") && file::exists(dir / "demo.pro") && file::exists(dir / "src" / "a.c"),
          "the sources and the .pro stay");
    check(removed.size() >= 7, "and each removed is named: " + std::to_string(removed.size()));

    // A project named as one of its own folders: the folder is not the program.
    std::vector<std::string> again = editor::cleanBuilt((dir / "src").string(), false);
    check(file::exists(dir / "src" / "a.c"), "a folder that shares the program's name is never removed");
    (void)again;

    // RIDE's folder for a CCS project goes with itsFolder; any other folder does not.
    file::path ccs = file::temp_directory_path() / (std::string(editor::product::kLower) + "-ccs-CleanTest");
    file::create_directories(ccs);
    std::ofstream((ccs / "CleanTest").string().c_str()) << "built\n";
    editor::cleanBuilt((ccs / "CleanTest").string(), true);
    check(!file::exists(ccs), "a CCS project's own build folder is removed whole");
    std::ofstream((dir / "other").string().c_str()) << "built\n";
    editor::cleanBuilt((dir / "other").string(), true);
    check(file::exists(dir) && file::exists(dir / "main.c"), "and a folder that is not one never is");

    // Another process's scratch stays; this one's goes.
    file::path theirs = file::temp_directory_path() / (std::string(editor::product::kLower) + "-run-1.s");
    std::ofstream(theirs.string().c_str()) << "theirs\n";
    editor::cleanBuilt(std::string(), false);
    check(file::exists(theirs), "another RIDE's scratch is left alone");
    editor::path::remove(theirs.string());
    file::remove_all(dir);
}

namespace {
void putEnv(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    if (value.empty()) unsetenv(name); else setenv(name, value.c_str(), 1);
#endif
}
std::string envOr(const char* name) { const char* v = std::getenv(name); return v ? v : ""; }
}

// **A tms6747 build without a .out is a failed build (D2)**, unless the project says emulateOnly or
// the run is an Emulate on vm6747 (R5). A stand-in compiler writes each .s; the states are the
// assembler gone, the assembler named and gone, no runtime to link against, and the two that succeed.
void aTiProgramOrAFailedBuild() {
    std::printf("a tms6747 build makes a .out or fails\n");
    file::path dir = file::temp_directory_path() / "ride-ti-or-fail";
    file::remove_all(dir);
    file::create_directories(dir / "home");
    file::create_directories(dir / "noruntime");
    const std::string homeWas = editor::path::homeDir();
    sayWhereHomeIs((dir / "home").string());
    const std::string asWas = envOr("ASM6X"), rtsWas = envOr("RTS6X");
    editor::Toolchain tool;
#ifdef _WIN32
    tool.cc1 = (dir / "fake-cc.cmd").string();
    writeSource(tool.cc1, "@echo off\r\n:loop\r\nif \"%~1\"==\"\" exit /b 0\r\n"
                          "if \"%~1\"==\"-o\" echo ; a TI program, in name> \"%~2\"\r\nshift\r\ngoto loop\r\n");
    const std::string fakeAs = (dir / "fake-asm6x.cmd").string();
    writeSource(fakeAs, "@exit /b 0\r\n");
#else
    tool.cc1 = (dir / "fake-cc.sh").string();
    writeSource(tool.cc1, "#!/bin/sh\nwhile [ $# -gt 0 ]; do\n  if [ \"$1\" = -o ]; then echo '; a TI program, in name' > \"$2\"; fi\n"
                          "  shift\ndone\nexit 0\n");
    chmod(tool.cc1.c_str(), 0755);
    const std::string fakeAs = (dir / "fake-asm6x.sh").string();
    writeSource(fakeAs, "#!/bin/sh\nexit 0\n");
    chmod(fakeAs.c_str(), 0755);
#endif
    std::vector<std::string> srcs(1, (dir / "main.c").string());
    writeSource(srcs[0], "int main(void) { return 0; }\n");
    const std::string program = (dir / "main.exe").string();
    auto made = [&](const editor::Toolchain& t) {
        return editor::buildTarget(t, editor::ToolCc1, srcs, editor::LangC, "tms6747", editor::ConfigRelease, program);
    };

    putEnv("ASM6X", (dir / "no-such-asm6x.exe").string());
    editor::Built gone = made(tool);
    check(!gone.ok && gone.output.find("asm6x named by $ASM6X is not there") != std::string::npos,
          "an asm6x named and gone fails the build, naming it:\n" + gone.output);

    putEnv("ASM6X", std::string());
    if (editor::c6xAssembler().empty()) {
        editor::Built none = made(tool);
        check(!none.ok && none.output.find("no asm6x beside") != std::string::npos &&
                  none.output.find("emulateOnly") != std::string::npos,
              "no asm6x fails the build, and says how to build without one:\n" + none.output);
    }

    putEnv("ASM6X", fakeAs);
    putEnv("RTS6X", (dir / "noruntime").string());
    editor::Built bare = made(tool);
    check(!bare.ok && bare.output.find("TI objects made] no .out:") != std::string::npos,
          "objects made and nothing to link them against fails the build, saying why:\n" + bare.output);

    editor::Toolchain only = tool;
    only.emulateOnly = true;
    editor::Built emulateOnly = made(only);
    check(emulateOnly.ok && emulateOnly.output.find("[no .out: the project says emulateOnly") != std::string::npos,
          "a project that says emulateOnly builds with no .out, and says so:\n" + emulateOnly.output);
    editor::Toolchain emulating = tool;
    emulating.emulating = true;
    putEnv("ASM6X", (dir / "no-such-asm6x.exe").string());
    editor::Built emulated = made(emulating);
    check(emulated.ok && emulated.output.find("[no .out: Emulate on vm6747") != std::string::npos,
          "an Emulate on vm6747 needs no asm6x and links nothing:\n" + emulated.output);

    file::create_directories(dir / "pro");
    writeSource((dir / "pro" / "project.pro").string(), "{ \"arch\": \"tms6747\", \"emulateOnly\": true }\n");
    editor::Project pro;
    std::string error;
    check(pro.load((dir / "pro").string(), error) && pro.emulateOnly(), "\"emulateOnly\": true is read from the .pro");
    std::string saveError;
    check(pro.save(saveError) && readWholeFile((dir / "pro" / "project.pro").string()).find("emulateOnly") != std::string::npos,
          "and kept when the project is saved");

    check(editor::runnerFor(editor::RunAsked, "tms6747", false) == editor::RunSimulator,
          "Run on tms6747 is the .out on sim6747 (R5)");
    check(editor::runnerFor(editor::RunAsked, "tms6747", true) == editor::RunEmulator,
          "unless the project says emulateOnly");
    check(editor::runnerFor(editor::RunAsked, editor::hostArch(), false) == editor::RunAsked,
          "and a host target's Run is the program, as it was");
    checkEqual(editor::runnerLine(editor::RunSimulator, "/x/p.vm"), "$ sim6747 --run p.out", "the line names sim6747 and the .out");
    checkEqual(editor::runnerLine(editor::RunEmulator, "/x/p.vm/"), "$ vm6747 p.vm", "or vm6747 and the assembly");

    putEnv("ASM6X", asWas);
    putEnv("RTS6X", rtsWas);
    sayWhereHomeIs(homeWas);
    file::remove_all(dir);
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--require-tools") requireTools = true;
        else { std::fprintf(stderr, "test: unknown option %s (only --require-tools)\n", argv[i]); return 2; }
    }
    // The flags the suite expects are the defaults, not what this machine's settings.json has chosen
    // in Compiler Options: an empty store stands in for the installation's for the whole run.
    editor::options::Store* none = new editor::options::Store();
    editor::options::setFallback(none);
    // Nothing the suite does may reach the user's own files: on macOS settings.json is ~/.ride's, so a
    // test writing a setting with no install directory pretended wrote the user's - "ccs" turned off on
    // 02-10-2026. Home is a scratch directory for the whole run; the tests that move it put this back.
    {
        file::path home = file::temp_directory_path() / "ride-test-home";
        file::remove_all(home);
        file::create_directories(home);
        sayWhereHomeIs(home.string());
    }
    paths();
    whereTheProgramIs(argc > 0 ? argv[0] : 0);
    whatTheDebuggerHeard();
    whatTheProgramSaid();
    whatAConsoleAdds();
    steppingOffTheEnd();
    aProjectMadeFromWhatIsThere();
    whatItRemembers();
    namedProjectFiles();
    whereAFileBelongs();
    jsonIsALanguage();
    compilerOptions();
    cleaning();
    aboutSelf();
    commandLineArguments();
    findingInFiles();
    talkingToAChild();
    aProgramThatReads();
    theSeamsSmallPromises();
    whatADebuggerSays();
    whatTheC6xEmulatorSays();
    aStepThatWentNowhere();
    whatACallStackLooksLike();
    debuggingForReal();
    debuggingOnTheEmulator();
    debuggingCOnTheEmulator();
    stoppingTheHostsOwnCompiler();
    debuggingCppForReal();
    theSeamTheWindowUses();
    theWindowsProjectBuild();
    namingAConversion();
    theConversionSeam();
    diagnostics();
    layout();
    typing();
    colours();
    whatTheBuildMade();
    routing();
    multiByte();
    ranges();
    undoing();
    savedState();
    searching();
    jsonReading();
    projects();
    operations();
    theOtherShapeOfDiagnostic();
    whereTheProgramCannotGo();
    whatALinkFailureSays();
    environmentReport();
    aCompilerPerGroup();
    theFourthCompiler();
    theWindowsRuleAboutStatics();
    theManualsContents();
    aDirectoryInAQuotedArgument();
    whatTheProjectBuilds();
    theThirdLanguage();
    steppingShalimar();
    theWindowStoppingShalimar();
    theWindowsProjectDebug();
    ccsProjectsAsTheyAre();
    ccsWorkspacesOneProjectAtATime();
    ccsProjectsBuiltAndRun();
    rts6xLinksTheOut();
    aTiProgramOrAFailedBuild();

    std::printf("\n%d checks, %d failed, %d skipped%s\n", checks, failures, skips,
                requireTools ? " (--require-tools: a skip of RIDE's own is a failure)" : "");
    return failures == 0 ? 0 : 1;
}
