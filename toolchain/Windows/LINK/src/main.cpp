/*  main.cpp - the command line, in link.exe's spelling, and the order of the passes.
 *
 *  Only the switches the probe bed uses are acted on; the rest are accepted and ignored so
 *  that a command line written for link.exe does not have to be rewritten to try this one.
 *  /timestamp: is this linker's own, and exists for the tests: link.exe stamps an image with
 *  the time of day, so holding an image to a reference byte for byte means being able to say
 *  which second to claim.
 */
#include "link.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>

static bool eat(const std::string &a, const char *sw, std::string &val)
{
    size_t n = strlen(sw);
    if (a.size() < n + 1) return false;
    for (size_t i = 0; i < n; i++) {
        char c = a[i + 1], s = sw[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != s) return false;
    }
    val = a.substr(n + 1);
    return true;
}

/*  A `/` starts a switch - unless the argument names a file that exists, which on a POSIX
 *  host is what a path looks like (the review's L17). */
static bool is_switch(const std::string &a)
{
    if (a.empty() || (a[0] != '/' && a[0] != '-')) return false;
    if (a[0] == '/') { FILE *f = fopen(a.c_str(), "rb"); if (f) { fclose(f); return false; } }
    return true;
}

/* a path made absolute against the current directory */
static std::string full_path(const std::string &p)
{
#ifdef _WIN32
    char buf[4096];
    if (_fullpath(buf, p.c_str(), sizeof buf)) return buf;
#endif
    return p;
}

/* reserve[,commit], decimal or 0x-hex, as link.exe reads /stack: and /heap: */
static void sizes(const std::string &v, u64 &reserve, u64 &commit)
{
    size_t comma = v.find(',');
    std::string r = v.substr(0, comma);
    if (!r.empty()) reserve = strtoull(r.c_str(), 0, 0);
    if (comma != std::string::npos && comma + 1 < v.size()) commit = strtoull(v.c_str() + comma + 1, 0, 0);
}

/* the directories LIB names, split on ';' */
static void lib_env(Options &o)
{
    const char *e = getenv("LIB");
    if (!e) return;
    std::string all = e;
    size_t at = 0;
    while (at <= all.size()) {
        size_t semi = all.find(';', at);
        if (semi == std::string::npos) semi = all.size();
        if (semi > at) o.libpath.push_back(all.substr(at, semi - at));
        at = semi + 1;
    }
}

bool Link::run()
{
    if (!read_inputs()) return false;
    if (!fold_identical()) return false;
    if (!lay_out())     return false;
    if (!address())     return false;
    if (!fix_up())      return false;
    if (!write_image()) return false;
    if (!write_pdb())   return false;
    return write_map();
}

/*  @file: the file's words in place of the argument, as link.exe reads a response file - split on
 *  white space, a "quoted" word kept whole. What lets a library of a few hundred objects be
 *  named on a command line Windows caps at 32,767 characters. */
static bool expand(const char *a, std::vector<std::string> &out)
{
    if (a[0] != '@') { out.push_back(a); return true; }
    FILE *f = fopen(a + 1, "rb");
    if (!f) { fprintf(stderr, "link: cannot open the response file %s\n", a + 1); return false; }
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    fclose(f);
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) i++;
        if (i >= text.size()) break;
        std::string w;
        bool quoted = false;
        while (i < text.size() && (quoted || (text[i] != ' ' && text[i] != '\t' && text[i] != '\r' && text[i] != '\n'))) {
            if (text[i] == '"') quoted = !quoted;
            else w += text[i];
            i++;
        }
        out.push_back(w);
    }
    return true;
}

int main(int argc, char **argv)
{
    Link lk;
    std::string v;

    std::vector<std::string> args;
    for (int i = 1; i < argc; i++)
        if (!expand(argv[i], args)) return 2;
    /* /LIB first: the librarian (librarian.cpp), as link.exe /LIB is lib.exe */
    if (!args.empty() && (args[0] == "/lib" || args[0] == "/LIB" || args[0] == "/Lib" || args[0] == "-lib" || args[0] == "-LIB"))
        return librarian(std::vector<std::string>(args.begin() + 1, args.end()));

    for (size_t i = 0; i < args.size(); i++) {
        std::string a = args[i];
        if (!is_switch(a)) { lk.opt.inputs.push_back(a); continue; }
        if (eat(a, "out:", v))        { lk.opt.out = v; continue; }
        if (eat(a, "entry:", v))      { lk.opt.entry = v; continue; }
        if (eat(a, "timestamp:", v))  { lk.opt.timestamp = (u32)strtoul(v.c_str(), 0, 16);
                                        lk.opt.have_timestamp = true; continue; }
        if (eat(a, "subsystem:", v))  {
            std::string s;
            for (size_t k = 0; k < v.size() && v[k] != ','; k++) s += (char)((v[k] >= 'A' && v[k] <= 'Z') ? v[k] - 'A' + 'a' : v[k]);
            lk.opt.subsystem = s == "windows" ? 2 : s == "native" ? 1 : 3;
            continue;
        }
        /* /base: is hexadecimal in link.exe's spelling, with or without 0x */
        if (eat(a, "base:", v))       { lk.opt.image_base = strtoull(v.c_str(), 0, 16); continue; }
        if (eat(a, "stack:", v))      { sizes(v, lk.opt.stack_reserve, lk.opt.stack_commit); continue; }
        if (eat(a, "heap:", v))       { sizes(v, lk.opt.heap_reserve, lk.opt.heap_commit); continue; }
        if (eat(a, "libpath:", v))    { lk.opt.libpath.push_back(v); continue; }
        if (eat(a, "defaultlib:", v)) { lk.opt.defaultlibs.push_back(v); continue; }
        if (eat(a, "nodefaultlib:", v)) { lk.opt.nodefaultlibs.push_back(v); continue; }
        /* /debug, /debug:full and the rest write a PDB; /debug:none does not */
        if (eat(a, "debugtype:", v))  continue;
        if (eat(a, "debug", v))       { lk.opt.debug = v != ":none" && v != ":NONE"; continue; }
        if (eat(a, "pdb:", v))        { lk.opt.pdb = v; continue; }
        if (eat(a, "include:", v))    { lk.opt.includes.push_back(v); continue; }
        if (eat(a, "opt:", v))        {
            /* /opt:ref,icf and the rest: ref and noref decide something, the others are accepted */
            std::string s;
            for (size_t k = 0; k < v.size(); k++) s += (char)((v[k] >= 'A' && v[k] <= 'Z') ? v[k] - 'A' + 'a' : v[k]);
            size_t at = 0;
            while (at <= s.size()) {
                size_t comma = s.find(',', at);
                if (comma == std::string::npos) comma = s.size();
                std::string one = s.substr(at, comma - at);
                if (one == "ref")   { lk.opt.optref = true;  lk.opt.optref_said = true; }
                if (one == "noref") { lk.opt.optref = false; lk.opt.optref_said = true; }
                if (one == "icf")   { lk.opt.opticf = true;  lk.opt.opticf_said = true; }
                if (one == "noicf") { lk.opt.opticf = false; lk.opt.opticf_said = true; }
                at = comma + 1;
            }
            continue;
        }
        if (eat(a, "align:", v))      { lk.opt.section_align = (u32)strtoul(v.c_str(), 0, 0); continue; }
        if (eat(a, "filealign:", v))  { lk.opt.file_align = (u32)strtoul(v.c_str(), 0, 0); continue; }
        if (eat(a, "nodefaultlib", v)) { lk.opt.nodefaultlib = true; continue; }
        if (eat(a, "fixed", v))        { lk.opt.fixed = true; continue; }
        if (eat(a, "dynamicbase", v))  { lk.opt.dynamicbase = true; continue; }
        if (eat(a, "verbose", v))      { lk.opt.verbose = true; continue; }
        if (eat(a, "map:", v))        { lk.opt.map = v; continue; }
        if (eat(a, "map", v) && v.empty()) { lk.opt.map = "*"; continue; }
        if (eat(a, "nologo", v) || eat(a, "incremental", v) ||
            eat(a, "machine:", v) || eat(a, "ignore:", v) ||
            eat(a, "release", v) || eat(a, "manifest", v) || eat(a, "nxcompat", v) ||
            eat(a, "largeaddressaware", v) || eat(a, "errorreport:", v) || eat(a, "pdbaltpath:", v) ||
            eat(a, "tlbid:", v) || eat(a, "brepro", v)) continue;
        fprintf(stderr, "link: unknown switch %s\n", a.c_str());
        return 2;
    }

    if (lk.opt.inputs.empty()) {
        fprintf(stderr,
            "usage: link /out:image.exe [/entry:sym] [/nodefaultlib] [/fixed]\n"
            "            [/subsystem:console|windows] [/timestamp:hex] object... library...\n"
            "       link /LIB /OUT:library.lib object...   (a library: see link /LIB)\n");
        return 2;
    }
    if (lk.opt.out.empty()) {
        std::string s = lk.opt.inputs[0];
        size_t d = s.rfind('.');
        lk.opt.out = (d == std::string::npos ? s : s.substr(0, d)) + ".exe";
    }
    if (!lk.opt.have_timestamp) lk.opt.timestamp = (u32)time(0);
    lib_env(lk.opt);                       /* after /libpath:, which is searched first */
    if (lk.opt.debug) {
        /*  The PDB beside the image unless /pdb: says where, named in the image by its full
         *  path, which is where a debugger looks first. */
        std::string p = lk.opt.pdb;
        if (p.empty()) {
            p = lk.opt.out;
            size_t d = p.rfind('.'), sl = p.find_last_of("/\\");
            if (d != std::string::npos && (sl == std::string::npos || d > sl)) p = p.substr(0, d);
            p += ".pdb";
        }
        lk.pdb_path = full_path(p);
        if (!lk.opt.optref_said) lk.opt.optref = false;     /* link.exe: /debug implies /opt:noref */
        if (!lk.opt.opticf_said) lk.opt.opticf = false;     /* and /opt:noicf, so a debugger sees each function once */
    }

    if (lk.opt.map == "*") {               /* /map with no name: beside the image */
        std::string s = lk.opt.out;
        size_t d = s.rfind('.');
        lk.opt.map = (d == std::string::npos ? s : s.substr(0, d)) + ".map";
    }
    if (!lk.run()) {
        fprintf(stderr, "link: %s\n", lk.err.c_str());
        return 1;
    }
    if (lk.opt.verbose) {
        for (size_t i = 0; i < lk.mods.size(); i++)
            printf("module %s%s\n", lk.mods[i].name.c_str(), lk.mods[i].from_archive ? " (archive)" : "");
        for (size_t i = 0; i < lk.outs.size(); i++)
            printf("%-8s rva %08x  virtual %6x  raw %6x at %6x\n",
                   lk.outs[i].name.c_str(), lk.outs[i].rva, lk.outs[i].virt_size,
                   lk.outs[i].raw_size, lk.outs[i].fileoff);
    }
    return 0;
}
