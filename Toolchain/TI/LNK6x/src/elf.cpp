/*  elf.cpp - reading one TI ELF32 object for the C6000.
 *
 *  Little-endian, EM_TI_C6000 (140), and both spellings of a relocation table: asm6x writes
 *  SHT_RELA where it has an addend to carry and SHT_REL where the addend is already in the
 *  field, and the same object can hold one of each. Nothing here decides anything - the
 *  sections, their relocations and the symbol table come out as they went in.
 */
#include "lnk.h"
#include <cstring>
#include <cstdio>

bool elf_read(const u8 *p, size_t n, const std::string &name, Module &m, std::string &err)
{
    if (n < 52 || memcmp(p, "\177ELF", 4) != 0) { err = name + ": not an ELF file"; return false; }
    if (p[4] != 1 || p[5] != 1) { err = name + ": not 32-bit little-endian"; return false; }
    u16 machine = rd16(p + 18);
    if (machine != 140) {
        char b[64]; snprintf(b, sizeof b, ": machine %u, not the C6000's 140", machine);
        err = name + b; return false;
    }
    u32 shoff = rd32(p + 32);
    u16 shentsize = rd16(p + 46), shnum = rd16(p + 48), shstrndx = rd16(p + 50);
    if (!shoff || !shnum || shoff + (size_t)shnum * shentsize > n) { err = name + ": broken section table"; return false; }

    const u8 *sh = p + shoff;
    u32 stroff = rd32(sh + (size_t)shstrndx * shentsize + 16);
    const char *shstr = (const char *)p + stroff;

    m.name = name;
    m.file_sym = -1;
    m.secs.resize(shnum);
    for (u16 i = 0; i < shnum; i++) {
        const u8 *s = sh + (size_t)i * shentsize;
        InSec &c = m.secs[i];
        c.name    = std::string(shstr + rd32(s));
        c.type    = rd32(s + 4);
        c.flags   = rd32(s + 8);
        c.size    = rd32(s + 20);
        c.align   = rd32(s + 32);
        c.entsize = rd32(s + 36);
        c.link    = rd32(s + 24);
        c.module  = -1;
        c.index   = i;
        c.live    = false;
        c.dropped = false;
        c.out     = -1;
        c.addr    = 0;
        c.load    = 0;
        u32 off = rd32(s + 16);
        /*  **Every section with bytes is loaded, not only PROGBITS.** `.c6xabi.exidx` is
         *  SHT_C6000_UNWIND in every object, and loading PROGBITS alone left its data empty:
         *  fix_up skipped its PREL31s and write_image its bytes, so every unwind index this
         *  linker ever wrote was zeros and no exception could be caught (the review's A1). */
        bool table = (c.type == SHT_SYMTAB || c.type == SHT_STRTAB || c.type == SHT_RELA || c.type == SHT_REL);
        if (c.type != SHT_NOBITS && c.type != SHT_NULL && !table && c.size) {
            if (off + c.size > n) { err = name + ": " + c.name + " runs past the file"; return false; }
            c.data.assign(p + off, p + off + c.size);
        }
    }

    /* the symbol table, and its own string table */
    for (u16 i = 0; i < shnum; i++) {
        const u8 *s = sh + (size_t)i * shentsize;
        if (rd32(s + 4) != SHT_SYMTAB) continue;
        u32 off = rd32(s + 16), size = rd32(s + 20), link = rd32(s + 24), es = rd32(s + 36);
        if (!es) es = 16;
        const char *str = (const char *)p + rd32(sh + (size_t)link * shentsize + 16);
        for (u32 k = 0; k * es < size; k++) {
            const u8 *sp = p + off + k * es;
            Sym y;
            y.name  = std::string(str + rd32(sp));
            y.value = rd32(sp + 4);
            y.size  = rd32(sp + 8);
            y.info  = sp[12];
            y.other = sp[13];
            y.shndx = rd16(sp + 14);
            y.out   = -1;
            if ((y.info & 0xF) == STT_FILE && m.file_sym < 0) m.file_sym = (int)m.syms.size();
            m.syms.push_back(y);
        }
        break;
    }

    /*  .TI.symbol.alias: a u32 version, a u16 count, "TI\0", then count pairs of u32 symbol
     *  indices, alias first - remove.obj's says (remove, unlink), typeinfo_.obj's (C2, C1). */
    for (u16 i = 0; i < shnum; i++) {
        const u8 *s = sh + (size_t)i * shentsize;
        if (rd32(s + 4) != SHT_TI_SYMALIAS) continue;
        u32 off = rd32(s + 16), size = rd32(s + 20);
        if (size < 9 || off + size > n || rd32(p + off) != 1) continue;
        u32 count = rd16(p + off + 4);
        for (u32 k = 0; k < count && 9 + 8 * (k + 1) <= size; k++) {
            u32 a = rd32(p + off + 9 + 8 * k), t = rd32(p + off + 13 + 8 * k);
            if (a < m.syms.size() && t < m.syms.size() && a != t) m.syms[a].alias = (int)t;
        }
    }

    /* the relocations, filed against the section they apply to */
    for (u16 i = 0; i < shnum; i++) {
        const u8 *s = sh + (size_t)i * shentsize;
        u32 type = rd32(s + 4);
        if (type != SHT_RELA && type != SHT_REL) continue;
        u32 off = rd32(s + 16), size = rd32(s + 20), info = rd32(s + 28), es = rd32(s + 36);
        if (!es) es = (type == SHT_RELA) ? 12u : 8u;
        if (info >= shnum) { err = name + ": a relocation table names no section"; return false; }
        InSec &t = m.secs[info];
        for (u32 k = 0; k * es < size; k++) {
            const u8 *rp = p + off + k * es;
            Rel r;
            r.offset = rd32(rp);
            u32 ri   = rd32(rp + 4);
            r.sym    = ri >> 8;
            r.type   = ri & 0xFF;
            r.addend = (type == SHT_RELA) ? (i32)rd32(rp + 8) : 0;
            /*  **A REL relocation keeps its addend in the place it patches**, and TI's runtime
             *  is REL for its data words: a type_info's vptr is `vtable + 8`, and an unwind
             *  table's PREL31 field holds its offset. Read here, from the object's own bytes,
             *  because fix_up runs twice under --rom_model and would read its own result. */
            if (type == SHT_REL && r.offset + 4 <= t.data.size()) {
                u32 w = rd32(&t.data[r.offset]);
                if (r.type == R_C6000_ABS32 || r.type == R_C6000_EHTYPE) r.addend = (i32)w;
                else if (r.type == R_C6000_PREL31) r.addend = (i32)(w << 1);   /* halfwords: x2 */
            }
            t.relocs.push_back(r);
        }
    }
    return true;
}
