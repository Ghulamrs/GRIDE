/* The .debug_line section, built from the .dwpsn rows once the layout is final - as asm6x builds one
   from a cl6x -g file, measured on cl6x 8.2.2 and 7.4.4 (tests/debug-line.sh): DWARF version 3,
   header_length, min_inst_length 4 (2 once a compact instruction puts a row on a halfword), default_is_stmt
   1, line_base 0, line_range 48, opcode_base 13 with the standard lengths 0 1 1 1 1 0 0 0 0 0 0 1, no
   include directory, the file names as the .dwpsn wrote them, and one sequence per function: a
   DW_LNE_set_address relocated R_C6000_ABS32 against the code section's own symbol with the offset in
   place, DW_LNS_set_column and a special opcode per row (DW_LNS_advance_pc or advance_line first where
   the special cannot reach), then negate_stmt, advance_pc to the function's end and end_sequence. A
   row's address is that of the instruction after the directive. cpp11's rows carry no `address`, so a
   section is one sequence there. */

#include "asm.h"

#include <map>

namespace {

struct Bytes {
    std::vector<unsigned char> b;
    void u8(unsigned v) { b.push_back((unsigned char)v); }
    void u16(unsigned v) { u8(v); u8(v >> 8); }
    void u32(unsigned long v) { u16((unsigned)(v & 0xFFFF)); u16((unsigned)(v >> 16)); }
    void uleb(unsigned long v) { do { unsigned char c = (unsigned char)(v & 0x7F); v >>= 7; if (v) c |= 0x80; u8(c); } while (v); }
    void sleb(long v) {
        for (;;) {
            unsigned char c = (unsigned char)(v & 0x7F);
            v >>= 7;
            bool done = (v == 0 && !(c & 0x40)) || (v == -1 && (c & 0x40));
            if (!done) c |= 0x80;
            u8(c);
            if (done) break;
        }
    }
    size_t size() const { return b.size(); }
    void str(const std::string &s) { for (size_t i = 0; i < s.size(); i++) u8((unsigned char)s[i]); u8(0); }
};

const int kLineBase = 0, kLineRange = 48, kOpcodeBase = 13;
const unsigned char kStdLengths[12] = { 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1 };

}

void build_debug_line(Unit &u)
{
    /* the rows by section, in source order; a row's address from where its instruction landed */
    struct Row { unsigned long addr; const DwRow *r; };
    std::map<int, std::vector<Row> > bySection;
    std::vector<int> order;
    unsigned minInst = 4;
    for (size_t i = 0; i < u.dwRows.size(); i++) {
        const DwRow &r = u.dwRows[i];
        if (r.section < 0 || r.section >= (int)u.sections.size()) continue;
        const Section &s = u.sections[(size_t)r.section];
        if (!s.hasCode) continue;
        Row row;
        row.addr = r.insn < s.insns.size() ? s.insns[r.insn] : (s.codeEnd ? s.codeEnd : (unsigned long)s.bytes.size());
        row.r = &r;
        if (bySection.find(r.section) == bySection.end()) order.push_back(r.section);
        bySection[r.section].push_back(row);
        if (row.addr % 4) minInst = 2;
    }
    if (order.empty()) return;

    /* the file table: each name once, in order of first use, numbered from 1 */
    std::vector<std::string> files;
    std::map<std::string, unsigned> fileIndex;
    for (size_t i = 0; i < u.dwRows.size(); i++)
        if (fileIndex.find(u.dwRows[i].file) == fileIndex.end()) { files.push_back(u.dwRows[i].file); fileIndex[u.dwRows[i].file] = (unsigned)files.size(); }

    Bytes program;
    std::vector<Reloc> relocs;
    for (size_t oi = 0; oi < order.size(); oi++) {
        int si = order[oi];
        const std::vector<Row> &rows = bySection[si];
        const Section &sec = u.sections[(size_t)si];
        unsigned long sectionEnd = sec.codeEnd ? sec.codeEnd : (unsigned long)sec.bytes.size();   /* before the fetch-packet padding */
        size_t k = 0;
        while (k < rows.size()) {
            /* one sequence: from this row to the next that opens one, or the section's end */
            size_t end = k + 1;
            while (end < rows.size() && !rows[end].r->newSeq) end++;
            unsigned long seqEnd = end < rows.size() ? rows[end].addr : sectionEnd;
            unsigned long addr = rows[k].addr;
            long line = 1;
            unsigned file = 1;
            bool stmt = true;
            Reloc rl; rl.offset = (unsigned long)program.size() + 3; rl.symbol = -1; rl.section = si; rl.kind = R_ABS32; rl.addend = 0; rl.order = ~2ul;
            relocs.push_back(rl);
            program.u8(0); program.uleb(5); program.u8(2); program.u32(addr);      /* DW_LNE_set_address, the offset in place */
            for (size_t i = k; i < end; i++) {
                const Row &row = rows[i];
                unsigned fi = fileIndex[row.r->file];
                if (fi != file) { program.u8(4); program.uleb(fi); file = fi; }
                program.u8(5); program.uleb((unsigned long)row.r->column);
                if (row.r->stmt != stmt) { program.u8(6); stmt = row.r->stmt; }
                long dline = (long)row.r->line - line;
                unsigned long dadv = (row.addr - addr) / minInst;
                if (dline < kLineBase || dline >= kLineBase + kLineRange) { program.u8(3); program.sleb(dline); line += dline; dline = 0; }
                if (dline - kLineBase + (long)(kLineRange * dadv) + kOpcodeBase > 255) { program.u8(2); program.uleb(dadv); addr += dadv * minInst; dadv = 0; }
                program.u8((unsigned)(dline - kLineBase + (long)(kLineRange * dadv) + kOpcodeBase));
                line += dline; addr += dadv * minInst;
            }
            if (stmt) program.u8(6);
            if (seqEnd > addr) { program.u8(2); program.uleb((seqEnd - addr) / minInst); }
            program.u8(0); program.uleb(1); program.u8(1);                          /* DW_LNE_end_sequence */
            k = end;
        }
    }

    Bytes header;                                                                   /* after header_length */
    header.u8(minInst); header.u8(1); header.u8((unsigned)(unsigned char)kLineBase); header.u8(kLineRange); header.u8(kOpcodeBase);
    for (int i = 0; i < 12; i++) header.u8(kStdLengths[i]);
    header.u8(0);                                                                    /* no include directory */
    for (size_t i = 0; i < files.size(); i++) { header.str(files[i]); header.uleb(0); header.uleb(0); header.uleb(0); }
    header.u8(0);

    Bytes unit;
    unit.u32((unsigned long)(2 + 4 + header.size() + program.size()));              /* unit_length */
    unit.u16(3);
    unit.u32((unsigned long)header.size());
    size_t programAt = unit.size() + header.size();
    unit.b.insert(unit.b.end(), header.b.begin(), header.b.end());
    unit.b.insert(unit.b.end(), program.b.begin(), program.b.end());

    Section s;
    s.name = ".debug_line";
    s.code = false; s.hasCode = false; s.bss = false; s.writable = false; s.alloc = false;
    s.kind = SEC_PROGBITS; s.align = 0; s.dataLast = false; s.dataEnd = 0; s.dataBytes = 0; s.compressed = false; s.codeEnd = 0;   /* align 0, as asm6x writes it */
    s.bytes = unit.b;
    for (size_t i = 0; i < relocs.size(); i++) { relocs[i].offset += (unsigned long)programAt; s.relocs.push_back(relocs[i]); }
    u.sections.push_back(s);
}
