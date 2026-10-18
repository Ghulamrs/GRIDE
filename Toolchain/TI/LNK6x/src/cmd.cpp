/*  cmd.cpp - the linker command file.
 *
 *  TI's language, not cmd.exe's, and only the part the bed uses: option lines that begin with
 *  two dashes, a MEMORY block of named ranges, and a SECTIONS block in which each entry says
 *  where its section is loaded, where it is run if that is somewhere else, what it must be
 *  aligned to, and what to write into the gaps. The order of the SECTIONS block is not
 *  decoration: it is the order the output sections appear in the image.
 */
#include "lnk.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct Lex {
    std::string s;
    size_t i;
    std::string err;
    Lex(const std::string &t) : s(t), i(0) {}

    void skip() {
        for (;;) {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
            if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '*') {
                i += 2;
                while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) i++;
                i = (i + 2 < s.size()) ? i + 2 : s.size();
                continue;
            }
            if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') {
                while (i < s.size() && s[i] != '\n') i++;
                continue;
            }
            break;
        }
    }
    /* a token is a run of the characters a name, a number or an option can be made of,
       or one punctuation mark */
    std::string next() {
        skip();
        if (i >= s.size()) return std::string();
        char c = s[i];
        if (strchr("{}:,=><()", c)) { i++; return std::string(1, c); }
        size_t b = i;
        while (i < s.size() && !strchr(" \t\r\n{}:,=><()", s[i])) i++;
        return s.substr(b, i - b);
    }
    std::string peek() { size_t k = i; std::string t = next(); i = k; return t; }
};

u32 number(const std::string &t) { return (u32)strtoul(t.c_str(), 0, 0); }

/*  The body of a `{ ... }` input-section list, the opening brace already taken. Every
 *  `*(name)` - and a bare `name` - is recorded in the order it appears, because that order
 *  is the order lnk6x lays the parts down in. Anything else inside is stepped over. */
void input_list(Lex &lx, SecSpec &sp)
{
    int depth = 1;
    std::string prev;
    while (depth) {
        std::string k = lx.next();
        if (k.empty()) break;
        if (k == "{") { depth++; prev.clear(); continue; }
        if (k == "}") { depth--; prev.clear(); continue; }
        if (k == "(") {
            /*  The lexer breaks on `:`, so `.text:early` arrives as three tokens; the name is
             *  whatever stands between the parentheses, put back together. */
            std::string inner;
            for (;;) {
                std::string t = lx.next();
                if (t.empty() || t == ")") break;
                inner += t;
            }
            if (!inner.empty()) sp.inputs.push_back(inner);
            prev.clear();
            continue;
        }
        prev = k;
    }
}

} /* namespace */

bool sec_matches(const std::string &pattern, const std::string &name)
{
    if (!pattern.empty() && pattern[pattern.size() - 1] == '*')
        return name.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0;
    return pattern == name;
}

std::string base_section(const std::string &name)
{
    size_t c = name.find(':');
    return (c == std::string::npos) ? name : name.substr(0, c);
}

Range *Cmd::range(const std::string &name)
{
    for (size_t i = 0; i < mem.size(); i++) if (mem[i].name == name) return &mem[i];
    return 0;
}

bool Cmd::parse(const std::string &path, std::string &err)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) { err = path + ": cannot open"; return false; }
    std::string text;
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    fclose(f);

    Lex lx(text);
    for (;;) {
        std::string t = lx.next();
        if (t.empty()) break;

        /*  The single-dash spellings, which TI's own command files use: `-heap 0x1000`,
         *  `-stack 0x800`, `-e sym`, and `-c`/`-cr` for the model. */
        if (t == "-heap" || t == "-stack") {
            u32 v = number(lx.next());
            if (t == "-heap") heap_size = v; else stack_size = v;
            continue;
        }
        if (t == "-e") { entry = lx.next(); continue; }
        if (t == "-c") { model = 2; continue; }
        if (t == "-cr") { model = 1; continue; }
        if (t.size() > 2 && t[0] == '-' && t[1] == '-') {
            size_t eq = t.find('=');
            std::string k = (eq == std::string::npos) ? t : t.substr(0, eq);
            std::string v = (eq == std::string::npos) ? std::string() : t.substr(eq + 1);
            if (v.empty() && lx.peek() == "=") { lx.next(); v = lx.next(); }
            if (k == "--stack_size") stack_size = number(v);
            else if (k == "--heap_size") heap_size = number(v);
            else if (k == "--args") args_size = number(v);
            else if (k == "--ram_model") model = 1;
            else if (k == "--rom_model") model = 2;
            else if (k == "--entry_point") entry = v.empty() ? lx.next() : v;
            /*  Everything else in the file is read and left alone: --trampolines,
             *  --diag_*, and the rest have no effect on an image this linker writes yet. */
            continue;
        }

        if (t == "MEMORY") {
            if (lx.next() != "{") { err = path + ": MEMORY without a brace"; return false; }
            bool closed = false;
            for (;;) {
                std::string name = lx.next();
                if (name == "}") { closed = true; break; }
                /*  The file ended inside the block. Before this the loop simply stopped and
                 *  the refusal arrived much later as `.text: no memory range for it`, which
                 *  names the wrong thing entirely (the review's N17). */
                if (name.empty()) break;
                Range r; r.name = name; r.origin = r.length = r.used = 0;
                /*  `NAME (RWX) : origin = ...`. The attributes and the colon are both optional
                 *  to lnk6x: TI's own C6747.cmd writes `DSPL2ROM o = 0x00700000 l = ...`. */
                if (lx.peek() == "(") { while (!lx.peek().empty() && lx.next() != ")") {} }
                if (lx.peek() == ":") lx.next();
                for (;;) {
                    std::string k = lx.peek();
                    if (k == "origin" || k == "o" || k == "ORIGIN" || k == "O") {
                        lx.next(); if (lx.peek() == "=") lx.next();
                        r.origin = number(lx.next());
                    } else if (k == "length" || k == "l" || k == "LENGTH" || k == "L" || k == "len") {
                        lx.next(); if (lx.peek() == "=") lx.next();
                        r.length = number(lx.next());
                    } else if (k == ",") {
                        lx.next();
                    } else break;
                }
                mem.push_back(r);
            }
            if (!closed) { err = path + ": MEMORY block is not closed"; return false; }
            continue;
        }

        if (t == "SECTIONS") {
            if (lx.next() != "{") { err = path + ": SECTIONS without a brace"; return false; }
            bool closed = false;
            for (;;) {
                std::string name = lx.next();
                if (name == "}") { closed = true; break; }
                if (name.empty()) break;
                SecSpec sp;
                sp.name = name;
                sp.align = 0; sp.has_align = false;
                sp.fill = 0;  sp.has_fill = false;
                std::string t2 = lx.next();
                if (t2 == ">") {                       /* .name > RANGE */
                    sp.load = lx.next();
                } else if (t2 == ":") {                /* .name : load = R, run = R, align = n */
                    for (;;) {
                        std::string k = lx.peek();
                        if (k == "," ) { lx.next(); continue; }
                        if (k == ">") { lx.next(); sp.load = lx.next(); continue; }
                        /*  `.text : { *(.text:early) *(.text) } > RAM`. The list used to fall
                         *  through to the break below, leaving the braces for the outer loop
                         *  to read as section names and this entry with no range at all - the
                         *  review's N6, which stopped q09-subsect-named. */
                        if (k == "{") { lx.next(); input_list(lx, sp); continue; }
                        if (k == "load" || k == "LOAD") {
                            lx.next(); if (lx.peek() == "=" || lx.peek() == ">") lx.next();
                            sp.load = lx.next(); continue;
                        }
                        if (k == "run" || k == "RUN") {
                            lx.next(); if (lx.peek() == "=" || lx.peek() == ">") lx.next();
                            sp.run = lx.next(); continue;
                        }
                        if (k == "align" || k == "ALIGN") {
                            lx.next(); if (lx.peek() == "=") lx.next();
                            if (lx.peek() == "(") lx.next();
                            sp.align = number(lx.next()); sp.has_align = true;
                            if (lx.peek() == ")") lx.next();
                            continue;
                        }
                        if (k == "fill" || k == "FILL") {
                            lx.next(); if (lx.peek() == "=") lx.next();
                            if (lx.peek() == "(") lx.next();
                            sp.fill = number(lx.next()); sp.has_fill = true;
                            if (lx.peek() == ")") lx.next();
                            continue;
                        }
                        break;
                    }
                } else if (t2 == "{") {
                    input_list(lx, sp);
                    if (lx.peek() == ">") { lx.next(); sp.load = lx.next(); }
                }
                secs.push_back(sp);
            }
            if (!closed) { err = path + ": SECTIONS block is not closed"; return false; }
            continue;
        }
        /* anything else in the file is not something the bed asks for */
    }
    if (mem.empty()) { err = path + ": no MEMORY block"; return false; }
    return true;
}
