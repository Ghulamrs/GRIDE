#include "asm.h"

#include <cstdio>

Unit::Unit() : line(0), pass(0), current(-1), defined(0), compressing(false)
{
}

/* a new pass over the source: the sections, fixups and errors start again; the symbols keep
   their definitions from the previous pass (so a forward reference is known) and remember
   where they were, for moved() */
void Unit::begin_pass(int n)
{
    pass = n;
    prev_sizes.clear();
    for (size_t i = 0; i < sections.size(); i++)
        prev_sizes.push_back((unsigned long)sections[i].bytes.size());
    sections.clear();
    depends.clear();
    dwRows.clear();
    pendingLabels.clear();
    defined = 0;
    /* asm6x numbers .text first whatever the source opens first; it is executable from the
       start, and its alignment is the fetch packet's once an instruction lands in it */
    current = -1;
    section(".text", false);
    sections[0].code = true;
    current = 0;
    fixups.clear();
    errors.clear();
    warnings.clear();
    line = 0;
    for (size_t i = 0; i < symbols.size(); i++) {
        symbols[i].prev_section = symbols[i].section;
        symbols[i].prev_value = symbols[i].value;
    }
}

/* did any section's size or any symbol's place change since the previous pass? */
bool Unit::moved() const
{
    if (sections.size() != prev_sizes.size())
        return true;
    for (size_t i = 0; i < sections.size(); i++)
        if (sections[i].bytes.size() != prev_sizes[i])
            return true;
    for (size_t i = 0; i < symbols.size(); i++) {
        const Symbol &s = symbols[i];
        if (s.defined && (s.value != s.prev_value || s.section != s.prev_section))
            return true;
    }
    return false;
}

void Unit::error(const std::string &msg)
{
    char buf[32];
    snprintf(buf, sizeof buf, "line %d: ", line);
    errors.push_back(buf + msg);
}

void Unit::warn(const std::string &msg)
{
    char buf[40];
    snprintf(buf, sizeof buf, "line %d: warning: ", line);
    warnings.push_back(buf + msg);
}

/* the section of that name, opened on first use. asm6x's rule is the content, not the name: a
   section is loaded and read-only unless `.sect "x", RW` or .data says it is writable; it
   becomes executable, 32-aligned and padded to 32 when an instruction lands in it; only .bss
   and .usect make it NOBITS; and an exidx table is SHT_C6000_UNWIND, linked to the code
   section its name ends with. */
int Unit::section(const std::string &name, bool writable)
{
    for (size_t i = 0; i < sections.size(); i++)
        if (sections[i].name == name) {
            current = (int)i;
            if (writable) sections[i].writable = true;
            return current;
        }
    Section s;
    s.name = name;
    std::string head = name.substr(0, name.find(':'));
    s.code = false;
    s.hasCode = false;
    s.bss = false;
    s.writable = writable;
    s.alloc = true;
    s.kind = head == ".c6xabi.exidx" ? SEC_UNWIND : SEC_PROGBITS;
    s.align = 1;
    s.codeEnd = 0;
    s.dataLast = false;
    s.dataEnd = 0;
    s.dataBytes = 0;
    s.compressed = false;
    sections.push_back(s);
    current = (int)sections.size() - 1;
    return current;
}

Section *Unit::cur()
{
    if (current < 0) {
        error("no section open");
        return 0;
    }
    return &sections[current];
}

unsigned long Unit::here()
{
    return current < 0 ? 0 : (unsigned long)sections[current].bytes.size();
}

int Unit::find(const std::string &name) const
{
    std::unordered_map<std::string, int>::const_iterator it = symIndex.find(name);
    return it == symIndex.end() ? -1 : it->second;
}

int Unit::ref(const std::string &name)
{
    int i = find(name);
    if (i >= 0)
        return i;
    Symbol s;
    s.name = name;
    s.bind = B_LOCAL;
    s.defined = false;
    s.type = SYM_NONE;
    s.section = -1;
    s.value = 0;
    s.line = line;
    s.pass = 0;
    s.prev_section = -1;
    s.prev_value = 0;
    s.referenced = false;
    s.size = 0;
    s.sized = false;
    s.mustDefine = false;
    s.alias = -1;
    s.aliasAdd = 0;
    s.order = 0;
    symbols.push_back(s);
    symIndex[name] = (int)symbols.size() - 1;
    return (int)symbols.size() - 1;
}

bool Unit::define(const std::string &name, int type)
{
    if (!cur())
        return false;
    int i = ref(name);
    Symbol &s = symbols[i];
    if (s.defined && s.pass == pass) {
        error("'" + name + "' is already defined");
        return false;
    }
    if (s.bind == B_EXTERN) s.bind = B_GLOBAL;   /* .ref'd and then defined: it is this file's */
    s.defined = true;
    s.type = type;
    s.section = current;
    s.value = (long long)here();
    s.line = line;
    s.pass = pass;
    s.order = defined++;
    return true;
}

/* .set: an absolute value, section -1; or an alias of a label */
bool Unit::constant(const std::string &name, long long v, int alias, long long aliasAdd)
{
    int i = ref(name);
    Symbol &s = symbols[i];
    if (s.defined && s.pass == pass && (s.section >= 0 || s.value != v || s.alias != alias)) {
        error("'" + name + "' is already defined");
        return false;
    }
    s.defined = true;
    s.section = -1;
    s.value = v;
    s.alias = alias;
    s.aliasAdd = aliasAdd;
    s.pass = pass;
    s.order = defined++;
    return true;
}

void Unit::align(int bytes)
{
    Section *s = cur();
    if (!s) return;
    if (bytes > s->align) s->align = bytes;
    while (s->bytes.size() % (size_t)bytes)
        s->bytes.push_back(0);
}

/* labels that stood alone on their lines take the address of what comes next, after that has
   aligned itself - asm6x's placing, which puts `fwd:` before an instruction after data at the
   padded word, not at the byte after the data - and are defined last first, which is the
   order asm6x lists two such labels in its symbol table */
void Unit::placeLabel()
{
    if (pendingLabels.empty()) return;
    std::vector<std::string> names;
    names.swap(pendingLabels);
    Section *s = cur();
    if (!s) return;
    for (size_t i = names.size(); i > 0; i--)
        define(names[i - 1], s->code ? SYM_FUNC : SYM_OBJECT);
}

void Unit::emit8(unsigned v)
{
    Section *s = cur();
    if (s)
        s->bytes.push_back((unsigned char)(v & 0xFF));
}

void Unit::emit16(unsigned v)
{
    emit8(v);
    emit8(v >> 8);
}

void Unit::emit32(unsigned long v)
{
    emit16((unsigned)(v & 0xFFFF));
    emit16((unsigned)((v >> 16) & 0xFFFF));
}

void Unit::emitData(int width, unsigned long long v)
{
    Section *s = cur();
    if (!s) return;
    for (int k = 0; k < width; k++) emit8((unsigned)(v >> (8 * k)));
    s->dataBytes += (unsigned long)width;
    s->dataLast = true;
    s->dataEnd = here();
}

void Unit::emitWord(unsigned long v)
{
    Section *s = cur();
    if (!s) return;
    s->insns.push_back((unsigned long)s->bytes.size());
    emit32(v);
    s->codeEnd = (unsigned long)s->bytes.size();
    s->dataLast = false;
}

void Unit::fixup(unsigned long at, int sym, RelKind kind, long long addend, int sub, int secref, int width)
{
    Fixup f;
    f.width = width;
    f.section = current;
    f.at = at;
    f.symbol = sym;
    f.secref = secref;
    f.sub = sub;
    f.kind = kind;
    f.addend = addend;
    f.line = line;
    f.half = false;
    f.settled = false;
    fixups.push_back(f);
}

namespace {

unsigned long word_at(const Section &sec, unsigned long at)
{
    return (unsigned long)sec.bytes[at] | ((unsigned long)sec.bytes[at + 1] << 8) |
           ((unsigned long)sec.bytes[at + 2] << 16) | ((unsigned long)sec.bytes[at + 3] << 24);
}

void put_word(Section &sec, unsigned long at, unsigned long w)
{
    for (int k = 0; k < 4; k++) sec.bytes[at + (unsigned long)k] = (unsigned char)(w >> (8 * k));
}

}

/* after a pass: every fixup becomes a value in place, or a relocation. A branch to a label
   of its own section is settled here, as asm6x settles it; its displacement is in words from
   the start of the fetch packet holding the branch, not from the branch. A label difference
   is a constant once both sit in one section. Everything else - an address in a data word,
   the halves of an MVKL/MVKH pair, a branch out of the section, an exception-table entry -
   goes to the linker: the addend written in place for every kind asm6x writes as REL (in the
   branch's own displacement field for PCR_S21 and PCR_S12), and in the entry, and the field
   too, for the three halves it writes as RELA. */
void Unit::resolve()
{
    for (size_t i = 0; i < symbols.size(); i++) {
        Symbol &s = symbols[i];
        if (!s.defined && s.bind != B_EXTERN && s.bind != B_GLOBAL && s.bind != B_WEAK && s.referenced) {
            line = s.line;
            error("'" + s.name + "' is not defined");
        }
        if (!s.defined && s.mustDefine) {
            line = s.line;
            error("'" + s.name + "' is declared with .def but not defined");
        }
    }
    for (size_t i = 0; i < fixups.size(); i++) {
        const Fixup &f = fixups[i];
        line = f.line;
        if (f.settled) continue;
        Section &sec = sections[f.section];
        if (f.symbol < 0) {
            /* $: against the section symbol, the offset as the addend; a NOCMP marker leaves
               the data word as it is */
            Reloc r;
            r.offset = f.at; r.symbol = -1; r.section = f.secref; r.kind = f.kind; r.addend = f.addend; r.order = (unsigned long)i;
            int width = f.kind == R_NOCMP ? 0 : f.width;
            for (int k = 0; k < width; k++) sec.bytes[f.at + (unsigned long)k] = (unsigned char)((unsigned long long)f.addend >> (8 * k));
            sec.relocs.push_back(r);
            continue;
        }
        const Symbol &s = symbols[f.symbol];
        if (f.sub >= 0) {
            const Symbol &b = symbols[f.sub];
            if (!s.defined || !b.defined) continue;
            if (s.section != b.section) { error("labels in different sections cannot be subtracted"); continue; }
            long long d = s.value - b.value + f.addend;
            for (int k = 0; k < f.width; k++)
                sec.bytes[f.at + (unsigned long)k] = (unsigned char)((unsigned long long)d >> (8 * k));
            continue;
        }
        if (s.defined && s.section < 0 && f.kind != R_PCR_S21 && f.kind != R_PCR_S12) {
            error("'" + s.name + "' is a constant, not an address");
            continue;
        }
        bool branch = f.kind == R_PCR_S21 || f.kind == R_PCR_S12;
        if (branch && s.defined && (s.section == f.section || s.section < 0) && s.bind != B_WEAK) {
            /* in the section - or a .set alias of a label, which asm6x takes as an offset here */
            long long target = s.value + f.addend;
            int unit = f.half ? 2 : 4;
            if (target % unit) { error("branch to '" + s.name + "' is not word-aligned"); continue; }
            long long disp = (target - (long long)(f.at & ~31ul)) / unit;
            int bits = f.kind == R_PCR_S21 ? 21 : 12, shift = f.kind == R_PCR_S21 ? 7 : 16;
            if (disp < -(1LL << (bits - 1)) || disp >= (1LL << (bits - 1))) { error("branch to '" + s.name + "' is too far"); continue; }
            put_word(sec, f.at, word_at(sec, f.at) | (((unsigned long)disp & ((1ul << bits) - 1)) << shift));
            continue;
        }
        Reloc r;
        r.offset = f.at;
        r.symbol = f.symbol;
        r.section = -1;
        r.kind = f.kind;
        r.addend = f.addend;
        r.order = (unsigned long)i;
        if (branch) {
            /* the addend in the displacement field, in words */
            if (f.addend % 4) { error("a branch addend must be a multiple of 4"); continue; }
            long long disp = f.addend / 4;
            int bits = f.kind == R_PCR_S21 ? 21 : 12, shift = f.kind == R_PCR_S21 ? 7 : 16;
            if (disp < -(1LL << (bits - 1)) || disp >= (1LL << (bits - 1))) { error("a branch addend does not fit its field"); continue; }
            put_word(sec, f.at, word_at(sec, f.at) | (((unsigned long)disp & ((1ul << bits) - 1)) << shift));
        } else if (f.kind == R_ABS_L16 || f.kind == R_ABS_H16 || f.kind == R_ABS_S16) {
            /* RELA, and the half in the constant field as well, as asm6x writes it */
            unsigned long half = f.kind == R_ABS_H16 ? ((unsigned long)f.addend >> 16) & 0xFFFF : (unsigned long)f.addend & 0xFFFF;
            put_word(sec, f.at, word_at(sec, f.at) | (half << 7));
        } else {
            for (int k = 0; k < f.width; k++)
                sec.bytes[f.at + (unsigned long)k] = (unsigned char)((unsigned long long)f.addend >> (8 * k));
        }
        sec.relocs.push_back(r);
    }
}
