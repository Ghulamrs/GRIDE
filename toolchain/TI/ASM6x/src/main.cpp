#include "asm.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <glob.h>
#include <sys/stat.h>
#endif

/* asm6x: assemble each file named, on its own thread, into <name>.obj beside it or where -o
   says for a single file. The warnings and errors of every job are printed after all have
   joined, each line as file: line N: message, and the exit status is the number of files
   that failed - a warning is a mended value, as asm6x mends it, not a failure. */

// A name with * or ? is expanded here, as cpp11 does: cmd hands the pattern through as written,
// and a POSIX shell when it was quoted. Sorted; a file named twice is assembled once.
static bool expand(const std::string &arg, std::vector<std::string> &into)
{
    std::vector<std::string> found;
    if (arg.find_first_of("*?") == std::string::npos || std::ifstream(arg.c_str()).good()) {
        found.push_back(arg);
    } else {
#ifdef _WIN32
        size_t slash = arg.find_last_of("/\\");
        std::string dir = slash == std::string::npos ? std::string() : arg.substr(0, slash + 1);
        WIN32_FIND_DATAA entry;
        HANDLE h = FindFirstFileA(arg.c_str(), &entry);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) found.push_back(dir + entry.cFileName);
            } while (FindNextFileA(h, &entry));
            FindClose(h);
        }
        std::sort(found.begin(), found.end());
#else
        glob_t g;
        if (glob(arg.c_str(), 0, nullptr, &g) == 0)
            for (size_t k = 0; k < g.gl_pathc; k++) found.push_back(g.gl_pathv[k]);
        globfree(&g);
#endif
    }
    for (size_t k = 0; k < found.size(); k++)
        if (std::find(into.begin(), into.end(), found[k]) == into.end()) into.push_back(found[k]);
    return !found.empty();
}

// -o names a directory when it ends in a separator or is one already.
static bool isDirectory(const std::string &path)
{
    if (!path.empty() && (path.back() == '/' || path.back() == '\\')) return true;
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static int usage()
{
    fprintf(stderr, "usage: asm6x [--no_compress] file.s ... [-o out.obj | -o dir/]\n"
                    "       one thread per file; dir/*.s is expanded here; -o names the object of a\n"
                    "       single file, or the directory the objects of several go to;\n"
                    "       code is laid out in header-based fetch packets with 16-bit C64x+ compact\n"
                    "       instructions, which the C674x runs natively, as TI's asm6x does by default;\n"
                    "       --no_compress writes every instruction in 32 bits (--compress is accepted)\n");
    return 2;
}

int main(int argc, char **argv)
{
    std::vector<std::string> inputs;
    std::string output;
    bool compress = true;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) output = argv[++i];
        else if (strcmp(argv[i], "--compress") == 0) compress = true;
        else if (strcmp(argv[i], "--no_compress") == 0) compress = false;
        else if (strcmp(argv[i], "--version") == 0) { printf("\xc2\xa9" "2026 G. R. Akhtar - asm6x 1.4, a TMS320C6000 assembler writing TI ELF\n"); return 0; }
        else if (argv[i][0] == '-') return usage();
        else if (!expand(argv[i], inputs)) { fprintf(stderr, "asm6x: no file matches %s\n", argv[i]); return 2; }
    }
    if (inputs.empty()) return usage();
    std::string outdir;
    if (!output.empty() && isDirectory(output)) {
        outdir = output;
        if (outdir.back() != '/' && outdir.back() != '\\') outdir += '/';
        output.clear();
    }
    if (!output.empty() && inputs.size() != 1) { fprintf(stderr, "asm6x: -o names one object, for one input - end it in / for a directory\n"); return 2; }
    std::vector<Assembler *> jobs;
    for (size_t i = 0; i < inputs.size(); i++) {
        std::string out = output;
        if (out.empty()) {
            out = inputs[i];
            size_t dot = out.find_last_of('.'), slash = out.find_last_of("/\\");
            if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) out.erase(dot);
            out += ".obj";
            if (!outdir.empty()) out = outdir + out.substr(slash == std::string::npos ? 0 : slash + 1);
        }
        jobs.push_back(new Assembler(inputs[i], out, compress));
    }
    std::vector<std::thread> threads;
    for (size_t i = 0; i < jobs.size(); i++) {
        Assembler *job = jobs[i];
        threads.push_back(std::thread([job]() {
            try { job->run(); }
            catch (const std::exception &e) { job->fail(std::string("internal: ") + e.what()); }
        }));
    }
    for (size_t i = 0; i < threads.size(); i++) threads[i].join();
    int failed = 0;
    for (size_t i = 0; i < jobs.size(); i++) {
        const std::vector<std::string> &w = jobs[i]->warnings();
        for (size_t k = 0; k < w.size(); k++) fprintf(stderr, "%s: %s\n", inputs[i].c_str(), w[k].c_str());
        const std::vector<std::string> &e = jobs[i]->errors();
        if (!e.empty()) failed++;
        for (size_t k = 0; k < e.size(); k++) fprintf(stderr, "%s: %s\n", inputs[i].c_str(), e[k].c_str());
        delete jobs[i];
    }
    return failed;
}
