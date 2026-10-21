/*  main.cpp - the command line, in lnk6x's spelling, and the order of the passes.
 *
 *  Only what the bed uses is acted on. -mv, --abi and -i are accepted and ignored: this
 *  linker is the C674x's and EABI's, and has no library path to search until it reads
 *  archives at all.
 */
#include "lnk.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

bool Link::run()
{
    if (!cmd.parse(opt.cmdfile, err)) return false;
    /*  What the command file said, where the command line did not say it itself. RIDE writes
     *  the model into the file and passes none on the line. Which of the two wins when both
     *  speak is not something the bed shows; the line is taken to, as the nearer word. */
    if (!opt.model_given && cmd.model) {
        opt.rom_model = (cmd.model == 2);
        opt.ram_model = !opt.rom_model;
    }
    if (!opt.entry_given && !cmd.entry.empty()) opt.entry = cmd.entry;
    if (opt.stack_size >= 0) cmd.stack_size = (u32)opt.stack_size;
    if (opt.heap_size >= 0)  cmd.heap_size  = (u32)opt.heap_size;
    if (opt.args_size >= 0)  cmd.args_size  = (u32)opt.args_size;
    if (!read_inputs())    return false;
    if (!eliminate())      return false;
    if (!build_sections()) return false;
    if (!layout())         return false;
    /*  Under --rom_model this first pass is provisional - .cinit is not laid yet - so a
     *  field out of range here is not yet a refusal; the pass that writes the image checks. */
    if (!fix_up(!opt.rom_model)) return false;
    /*  **--rom_model runs the middle of the link twice, and it has to.** A load image is
     *  the section's bytes *after* relocation - .fardata is full of pointers - so the
     *  images cannot be made until fix_up has run; but .cinit's size moves everything
     *  after it, so once they are made the addresses are wrong and it must all be done
     *  again. Relocation is idempotent here: every type writes its field rather than
     *  adding to it, so the second pass computes the same words from the new addresses. */
    if (opt.rom_model) {
        if (!compose_cinit()) return false;
        if (cinit_in >= 0) {
            if (!layout()) return false;
            if (!fix_up())   return false;
            place_cinit();
        } else if (!fix_up()) return false;       /* nothing moved: the same words, checked */
    }
    if (!opt.map.empty()) write_map();
    return write_image();
}

/*  A map in lnk6x's layout, as far as the sections go: each output section, then each input
 *  piece with its run address, size, module and section name. Enough to diff against the
 *  oracle's map line by line - which is how the differences below were found. */
void Link::write_map()
{
    FILE *f = fopen(opt.map.c_str(), "w");
    if (!f) return;
    fprintf(f, "SECTION ALLOCATION MAP\n\n");
    for (size_t i = 0; i < outs.size(); i++) {
        const OutSec &o = outs[i];
        if (!o.size && o.parts.empty()) continue;
        fprintf(f, "%-10s 0    %08x    %08x\n", o.name.c_str(), o.addr, o.size);
        for (size_t p = 0; p < o.parts.size(); p++) {
            const InSec *c = all[o.parts[p]];
            fprintf(f, "                  %08x    %08x     %s (%s)\n", c->addr, c->size,
                    mods[c->module].name.c_str(), c->name.c_str());
        }
        fprintf(f, "\n");
    }
    /*  lnk6x's own table, as q15-far.map lays it out: the callee named by its input section
     *  and offset, the trampoline by its symbol, then one line per call sent through it. */
    if (!tramps.empty()) {
        size_t ncalls = 0;
        fprintf(f, "FAR CALL TRAMPOLINES\n\n");
        fprintf(f, "callee name               trampoline name\n");
        fprintf(f, "   callee addr  tramp addr   call addr  call info\n");
        fprintf(f, "--------------  -----------  ---------  ----------------\n");
        for (size_t t = 0; t < tramps.size(); t++) {
            const Tramp &tr = tramps[t];
            const Module &tm = mods[tr.tmod];
            std::string sec = tr.tsec < (int)tm.secs.size() ? tm.secs[tr.tsec].name : "?";
            fprintf(f, "$%s:%s$0x%x  %s\n", sec.c_str(), tm.name.c_str(), (unsigned)tr.toff, tr.name.c_str());
            for (size_t k = 0; k < tr.calls.size(); k++) {
                const InSec *c = all[tr.calls[k].first];
                /* the callee and trampoline addresses on the first call's line only (q23) */
                if (k == 0) fprintf(f, "   %08x     %08x  ", tr.target, all[tr.in]->addr);
                else        fprintf(f, "%29s", "");
                fprintf(f, "   %08x   %s (%s)\n", c->addr + tr.calls[k].second,
                        mods[c->module].name.c_str(), c->name.c_str());
            }
            ncalls += tr.calls.size();
        }
        fprintf(f, "\n[%u trampolines]\n[%u trampoline calls]\n\n",
                (unsigned)tramps.size(), (unsigned)ncalls);
    }
    fclose(f);
}

static bool starts(const std::string &a, const char *p) { return a.compare(0, strlen(p), p) == 0; }

int main(int argc, char **argv)
{
    Link lk;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        // As cpp11, asm6x and vm6747 answer it: the banner seal.json names, and the version.
        if (a == "--version") { printf("\xc2\xa9" "2026 G. R. Akhtar - lnk6x 1.2, a TMS320C6000 linker\n"); return 0; }
        if (a == "-o" && i + 1 < argc)      { lk.opt.out = argv[++i]; continue; }
        if (starts(a, "--output_file="))    { lk.opt.out = a.substr(14); continue; }
        if (a == "-m" && i + 1 < argc)      { lk.opt.map = argv[++i]; continue; }
        if (a == "-i" && i + 1 < argc)      { lk.opt.libdirs.push_back(argv[++i]); continue; }
        if (starts(a, "-i"))                { lk.opt.libdirs.push_back(a.substr(2)); continue; }
        if (a == "-e" && i + 1 < argc)      { lk.opt.entry = argv[++i]; lk.opt.entry_given = true; continue; }
        if (starts(a, "--entry_point="))    { lk.opt.entry = a.substr(14); lk.opt.entry_given = true; continue; }
        if (a == "--ram_model")             { lk.opt.ram_model = true;  lk.opt.rom_model = false; lk.opt.model_given = true; continue; }
        if (a == "--rom_model")             { lk.opt.rom_model = true;  lk.opt.ram_model = false; lk.opt.model_given = true; continue; }
        if (a == "--verbose" || a == "-v")  { lk.opt.verbose = true; continue; }
        if (starts(a, "--stack_size="))     { lk.opt.stack_size = strtol(a.c_str() + 13, 0, 0); continue; }
        if (starts(a, "--heap_size="))      { lk.opt.heap_size = strtol(a.c_str() + 12, 0, 0); continue; }
        if (starts(a, "--args="))           { lk.opt.args_size = strtol(a.c_str() + 7, 0, 0); continue; }
        if (a == "-stack" && i + 1 < argc)  { lk.opt.stack_size = strtol(argv[++i], 0, 0); continue; }
        if (a == "-heap" && i + 1 < argc)   { lk.opt.heap_size = strtol(argv[++i], 0, 0); continue; }
        if (a == "--cgt=7.4.4")             { lk.opt.cgt744 = true;  continue; }
        if (a == "--cgt=8.2.2")             { lk.opt.cgt744 = false; continue; }
        if (starts(a, "-mv") || starts(a, "--abi=") || a == "-c" || a == "-cr"
            || a == "--no_compress" || starts(a, "--diag")) continue;
        if (a == "-l" && i + 1 < argc)      { lk.opt.inputs.push_back(argv[++i]); continue; }
        if (starts(a, "-l"))                { lk.opt.inputs.push_back(a.substr(2)); continue; }
        if (a.size() > 1 && a[0] == '-')    { fprintf(stderr, "lnk6x: unknown option %s\n", argv[i]); return 2; }
        if (a.size() > 4 && a.compare(a.size() - 4, 4, ".cmd") == 0) { lk.opt.cmdfile = a; continue; }
        lk.opt.inputs.push_back(a);
    }
    if (lk.opt.cmdfile.empty() || lk.opt.inputs.empty()) {
        fprintf(stderr, "usage: lnk6x [-mv6740] [--abi=eabi] <command file> object...\n"
                        "             [--ram_model] [-e sym] -o image.out [-m image.map]\n");
        return 2;
    }
    if (lk.opt.out.empty()) lk.opt.out = "a.out";

    if (!lk.run()) { fprintf(stderr, "lnk6x: %s\n", lk.err.c_str()); return 1; }
    if (lk.opt.verbose)
        for (size_t i = 0; i < lk.outs.size(); i++)
            if (lk.outs[i].size)
                printf("%-16s %08x  %6x bytes at file %6x\n", lk.outs[i].name.c_str(),
                       lk.outs[i].addr, lk.outs[i].size, lk.outs[i].offset);
    return 0;
}
