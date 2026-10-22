#include "asm.h"
#include "forms.h"

#include <cctype>
#include <cstdio>
#include <cstring>

/* The C674x instructions, encoded as asm6x encodes them. Every word here was read back from
   asm6x (cl6x --no_compress, dis6x): tests/probes/ holds the probes, tools/forms.py turns the
   register, constant and pair forms into src/forms.h, and the rest - the loads and stores,
   the constant moves, the branches, NOP/SWE/IDLE, MVC, ZERO, the field instructions, the
   15-bit address adds - is written out below from the same listings. The instruction word:

       31-29 creg   28 z   27-23 dst   22-18 src2   17-13 src1   12 x   11-2 the form   1 s   0 p

   with the form's ten bits naming the unit and the operation. A unit may be named after the
   mnemonic (.L1, .S2X, .D1T2, .M2); when it is not - the compilers never name one - the form
   asm6x chooses on its own is taken, which is what the default rows of the table record. s is
   the side of the destination (of the data register for a store, always B for a branch by
   register), x says the source in src2 came over the cross path, and p joins the word to the
   next in an execute packet. */

namespace {

/* forms asm6x has that tools/forms.py could not take from the probes: ADD of -16..-1, which
   dis6x lists as SUB; LMBD with a constant (asm6x's own listing goes wrong after it, see
   tests/probes/probe5.s); and SUB with the first source across on .L - the "xsrc1, src2"
   forms, map 6: src1 over the cross path - read back from review-probes-2026-09-19/edge */
const FormRow kMoreForms[] = {
    { "ADD", 'D', "crr", 0, 0x2BC, { 3, 1, 0 }, 's', 1 },     /* ADD -3,A1,A2: the signed .D form, which dis6x lists as SUB 3 */
    { "ADD", 'D', "rcr", 0, 0x2BC, { 1, 3, 0 }, 's', 1 },
    { "LMBD", 'L', "crr", 0, 0x356, { 3, 1, 0 }, 's', 1 },
    { "SUB", 'L', "rrp", 1, 0x1BE, { 6, 1, 0 }, '-', 1 },
    { "SUB", 'L', "rrr", 1, 0x0BE, { 6, 1, 0 }, '-', 0 },     /* where .S1 is taken in a packet: SUB.L1X B1,A2,A3 */
};

bool reg_name(const std::string &text, int &reg, int &side)
{
    std::string t = upper(text);
    if (t.size() < 2 || t.size() > 3 || (t[0] != 'A' && t[0] != 'B')) return false;
    for (size_t i = 1; i < t.size(); i++) if (t[i] < '0' || t[i] > '9') return false;
    int n = atoi(t.c_str() + 1);
    if (n > 31) return false;
    reg = n;
    side = t[0] == 'B';
    return true;
}

/* the creg field: which of A0-A2 and B0-B2 predicates the instruction */
int predicate_code(int reg, int side)
{
    static const int a[3] = { 6, 4, 5 }, b[3] = { 1, 2, 3 };
    if (reg > 2) return -1;
    return side ? b[reg] : a[reg];
}

bool parse_unit(const std::string &text, UnitName &u, std::string &why)
{
    u.letter = 0; u.side = -1; u.cross = false; u.dataSide = -1; u.loose = false;
    std::string t = upper(text);
    if (t.size() < 2 || strchr("LSDM", t[0]) == 0 || (t[1] != '1' && t[1] != '2')) { why = "a unit is .L1, .S2, .D1, .M2, with X for the cross path and T1/T2 for a load's side"; return false; }
    u.letter = t[0]; u.side = t[1] - '1';
    size_t i = 2;
    if (i < t.size() && t[i] == 'X') { u.cross = true; i++; }
    if (i + 1 < t.size() && t[i] == 'T' && (t[i + 1] == '1' || t[i + 1] == '2')) { u.dataSide = t[i + 1] - '1'; i += 2; }
    if (i != t.size()) { why = "'." + text + "' is not a unit this assembler knows"; return false; }
    return true;
}

void split_items(const std::vector<Token> &t, size_t from, std::vector<size_t> &cuts)
{
    cuts.clear();
    cuts.push_back(from);
    int depth = 0;
    for (size_t i = from; i < t.size(); i++) {
        if (is_punct(t, i, '(') || is_punct(t, i, '[')) depth++;
        else if (is_punct(t, i, ')') || is_punct(t, i, ']')) depth--;
        else if (depth == 0 && is_punct(t, i, ',')) cuts.push_back(i + 1);
    }
    cuts.push_back(t.size() + 1);
}

/* the operand count: none when nothing follows, and an empty item is an error */
bool count_items(Unit &u, const std::vector<Token> &t, size_t from, std::vector<size_t> &cuts, size_t &n)
{
    split_items(t, from, cuts);
    if (from >= t.size()) { n = 0; return true; }
    n = cuts.size() - 1;
    for (size_t k = 0; k < n; k++)
        if (cuts[k + 1] - 1 <= cuts[k]) { u.error("an operand is missing"); return false; }
    return true;
}

bool power_of_two(long long v) { return v > 0 && (v & (v - 1)) == 0; }

/* an alignment as asm6x takes it: a power of two, else the next one up with a warning,
   and nothing past 32K */
long long rounded_alignment(Unit &u, long long a)
{
    if (a > 32768) { u.warn("the maximum alignment is 32768; ignored"); return 1; }
    if (a < 1) { u.warn("the alignment must be a power of two; 1 assumed"); return 1; }
    if (power_of_two(a)) return a;
    long long p = 1;
    while (p < a) p <<= 1;
    u.warn("the alignment must be a power of two; " + std::to_string(p) + " assumed");
    return p;
}

/* C's escapes as asm6x's .cstring takes them: \a \b \f \n \r \t \v \\ \" \' and \ooo; not \xHH */
std::string c_escapes(const std::string &raw)
{
    std::string out;
    for (size_t i = 0; i < raw.size(); i++) {
        char c = raw[i];
        if (c != '\\' || i + 1 >= raw.size()) { out += c; continue; }
        char e = raw[++i];
        switch (e) {
        case 'a': out += '\a'; break; case 'b': out += '\b'; break; case 'f': out += '\f'; break;
        case 'n': out += '\n'; break; case 'r': out += '\r'; break; case 't': out += '\t'; break;
        case 'v': out += '\v'; break; case '\\': out += '\\'; break; case '"': out += '"'; break; case '\'': out += '\''; break;
        case 'x': break;    /* asm6x drops \x and keeps the digits as letters: "\x41" is "41" */
        default:
            if (e >= '0' && e <= '7') {
                int v = e - '0', k = 1;
                while (i + 1 < raw.size() && k < 3 && raw[i + 1] >= '0' && raw[i + 1] <= '7') { v = v * 8 + (raw[++i] - '0'); k++; }
                out += (char)v;
            } else out += e;
        }
    }
    return out;
}

/* one data item of a width: a constant (truncated with a warning, as asm6x does), or an
   address as a relocation, or a difference of two labels */
void data_item(Unit &u, const std::vector<Token> &t, size_t a, size_t b, int width)
{
    Value v;
    std::string err;
    if (!eval(u, t, a, b, v, err)) { u.error(err); return; }
    unsigned long at = u.here();
    if (v.sym >= 0 || v.secref >= 0) {
        RelKind kind = width == 4 ? R_ABS32 : width == 2 ? R_ABS16 : R_ABS8;
        if (v.operate && v.op != R_NONE) {
            if (v.sub >= 0) { u.error("an exception-table operator cannot be differenced"); return; }
            kind = v.op;
        }
        if (v.sub < 0 && v.operate && v.op == R_NONE) { u.error("a scope needs another scope to differ from"); return; }
        if (width == 1 && v.sub < 0) { u.error("a byte cannot hold an address"); return; }
        if (v.secref >= 0) u.fixup(at, -1, kind, v.v, -1, v.secref, width);
        else u.fixup(at, v.sym, kind, v.v, v.sub, -1, width);
        u.emitData(width, 0);
        return;
    }
    long long lo = width == 4 ? -2147483648LL : width == 2 ? -32768 : -128;
    long long hi = width == 4 ? 4294967295LL : width == 2 ? 65535 : 255;
    if (v.v < lo || v.v > hi) u.warn("value truncated to " + std::to_string(width * 8) + " bits");
    u.emitData(width, (unsigned long long)v.v);
}

}

C6xTarget::C6xTarget() : done(false), pass(0), last_at(0), last_section(-1), units(0)
{
}

void C6xTarget::begin_pass(int n)
{
    pass = n;
    done = false;
    last_at = 0;
    last_section = -1;
    units = 0;
    aliases.clear();
}

/* the end: a label still waiting takes the end of its section; asm6x pads every section
   holding code to the fetch packet, and marks the last data word a code section ends with
   (R_C6000_NOCMP against the section itself) */
void C6xTarget::end_of_file(Unit &u)
{
    u.placeLabel();
    for (size_t i = 0; i < u.sections.size(); i++) {
        Section &s = u.sections[i];
        if (!s.code) continue;
        if (s.dataLast && s.dataEnd >= 4) {
            int was = u.current;
            u.current = (int)i;
            u.fixup(s.dataEnd - 4, -1, R_NOCMP, 0, -1, (int)i);
            u.current = was;
        }
        if (s.hasCode) while (s.bytes.size() % 32) s.bytes.push_back(0);
    }
    done = true;
}

/* a statement: [label[:]] [||] [[!]Rn] mnemonic[.unit] operands, or [label] directive */
void C6xTarget::statement(Unit &u, std::vector<Token> &t)
{
    size_t i = 0;
    if (t.size() >= 2 && t[0].kind == T_NAME && is_punct(t, 1, ':') && t[1].value == 1) {
        std::string label = t[0].text;
        i = 2;
        /* a .set with the name in the label column is a constant, not a place - a .set alias
           of a label stands for the label, and is asm6x's absolute of the label's offset */
        if (i < t.size() && t[i].kind == T_NAME && (upper(t[i].text) == ".SET" || upper(t[i].text) == ".EQU")) {
            Value v;
            std::string err;
            if (!eval(u, t, i + 1, t.size(), v, err)) { u.error(err); return; }
            if (v.sub >= 0 || v.secref >= 0 || v.operate) { u.error(".set takes a constant or a label"); return; }
            if (v.sym >= 0) {
                const Symbol &s = u.symbols[v.sym];
                u.constant(label, (s.defined ? s.value : 0) + v.v, v.sym, v.v);
            } else u.constant(label, v.v);
            return;
        }
        u.pendingLabels.push_back(label);
        if (i >= t.size()) return;
    }
    /* .asg names stand for their text in the operands */
    if (!aliases.empty() && !(t[i].kind == T_NAME && upper(t[i].text) == ".ASG")) {
        std::vector<Token> out(t.begin(), t.begin() + (long)i + 1);
        for (size_t k = i + 1; k < t.size(); k++) {
            std::map<std::string, std::vector<Token> >::const_iterator al = t[k].kind == T_NAME ? aliases.find(t[k].text) : aliases.end();
            if (al == aliases.end()) out.push_back(t[k]);
            else out.insert(out.end(), al->second.begin(), al->second.end());
        }
        t.swap(out);
    }
    if (t[i].kind == T_NAME && t[i].text[0] == '.') {
        if (!directive(u, t, i)) u.error("'" + t[i].text + "' is not a directive this assembler knows");
        return;
    }
    instruction(u, t, i);
}

bool C6xTarget::directive(Unit &u, const std::vector<Token> &t, size_t i)
{
    std::string d = upper(t[i].text);
    std::vector<size_t> cuts;
    size_t n;
    if (!count_items(u, t, i + 1, cuts, n)) return true;
    std::string err;
    #define ITEM(k) cuts[k], cuts[(k) + 1] - 1

    if (d == ".TEXT") { u.placeLabel(); u.section(".text", false); units = 0; return true; }
    if (d == ".DATA") { u.placeLabel(); u.section(".data", true); units = 0; return true; }
    if (d == ".SECT") {
        u.placeLabel();
        if (n < 1 || (t[cuts[0]].kind != T_STR && t[cuts[0]].kind != T_NAME) || cuts[1] - 1 != cuts[0] + 1) { u.error(".sect needs a name"); return true; }
        std::string name = t[cuts[0]].text;
        bool rw = false;
        for (size_t k = 1; k < n; k++) {
            std::string a = upper(t[cuts[k]].text);
            if (a == "RW") rw = true;
            else if (a != "RO") { u.error("'" + t[cuts[k]].text + "' is not a .sect attribute"); return true; }
        }
        int sec = u.section(name, rw);
        if (u.sections[sec].bss) { u.error("'" + name + "' is uninitialised; .sect cannot reopen it"); return true; }
        units = 0;
        return true;
    }
    if (d == ".BSS" || d == ".USECT") {
        /* .bss sym, size[, align]   name .usect "sect", size[, align] - the symbol at the start
           of the space, in .bss or in the named section, which is uninitialised */
        bool usect = d == ".USECT";
        if (n < 1) { u.error(d + " needs a name and a size"); return true; }
        std::string symbol = usect && !u.pendingLabels.empty() ? u.pendingLabels.back() : usect ? std::string() : t[cuts[0]].text;
        if (usect && symbol.empty()) { u.error("a symbol is expected in the label field of .usect"); return true; }
        if (usect && t[cuts[0]].kind != T_STR && t[cuts[0]].kind != T_NAME) { u.error(".usect needs a section name"); return true; }
        if (!usect && t[cuts[0]].kind != T_NAME) { u.error(".bss needs a symbol"); return true; }
        if (n < 2) { u.error(d + " needs a size"); return true; }
        long long size, align = 1;
        if (!eval_const(u, t, ITEM(1), size, err, true)) { u.error(err); return true; }
        if (n > 2 && !eval_const(u, t, ITEM(2), align, err, true)) { u.error(err); return true; }
        if (n > 3) { u.error(d + " takes a name, a size and an alignment"); return true; }
        if (size < 0 || size > 0x7FFFFFFF) { u.error("the size is 0 to 2^31-1"); return true; }
        align = rounded_alignment(u, align);
        std::string name = usect ? t[cuts[0]].text : ".bss";
        int was = u.current;
        int sec = u.section(name, true);
        if (u.sections[sec].code || (!u.sections[sec].bss && !u.sections[sec].bytes.empty())) { u.error("'" + name + "' holds data; it cannot be uninitialised"); u.current = was; return true; }
        u.sections[sec].bss = true;
        u.sections[sec].kind = SEC_NOBITS;
        u.align((int)align);
        if (usect) u.pendingLabels.pop_back();
        u.placeLabel();
        if (u.define(symbol, SYM_OBJECT)) {
            Symbol &s = u.symbols[u.find(symbol)];
            s.size = size;
            s.sized = usect;
        }
        for (long long b = 0; b < size; b++) u.emit8(0);
        u.current = was;
        return true;
    }
    if (d == ".GLOBAL" || d == ".DEF" || d == ".REF" || d == ".WEAK") {
        if (n < 1) { u.error(d + " names a symbol"); return true; }
        for (size_t k = 0; k < n; k++) {
            if (t[cuts[k]].kind != T_NAME || cuts[k + 1] - 1 != cuts[k] + 1) { u.error(d + " names symbols"); return true; }
            int s = u.ref(t[cuts[k]].text);
            Symbol &sym = u.symbols[s];
            if (d == ".REF") { sym.referenced = true; if (sym.bind == B_LOCAL) sym.bind = B_EXTERN; }
            else if (d == ".WEAK") { sym.bind = B_WEAK; sym.referenced = true; }
            else if (d == ".DEF") { if (sym.bind != B_WEAK) sym.bind = B_GLOBAL; sym.mustDefine = true; }
            else if (sym.bind != B_WEAK) sym.bind = B_GLOBAL;   /* .global: written only if defined or used */
        }
        return true;
    }
    if (d == ".ALIGN") {
        long long a = 1;
        if (n > 1) { u.error(".align takes one value"); return true; }
        if (n == 1 && !eval_const(u, t, ITEM(0), a, err, true)) { u.error(err); return true; }
        a = rounded_alignment(u, a);
        u.align((int)a);
        u.placeLabel();
        return true;
    }
    if (d == ".WORD" || d == ".LONG" || d == ".INT" || d == ".ULONG" || d == ".UINT" || d == ".UWORD" ||
        d == ".HALF" || d == ".SHORT" || d == ".UHALF" || d == ".USHORT" ||
        d == ".BYTE" || d == ".CHAR" || d == ".UBYTE" || d == ".UCHAR") {
        int width = (d == ".HALF" || d == ".SHORT" || d == ".UHALF" || d == ".USHORT") ? 2
                  : (d == ".BYTE" || d == ".CHAR" || d == ".UBYTE" || d == ".UCHAR") ? 1 : 4;
        if (n < 1) { u.error(d + " needs a value"); return true; }
        if (width > 1) u.align(width);      /* a word or a half aligns itself, as TI's do */
        u.placeLabel();
        for (size_t k = 0; k < n; k++) {
            /* a string is one item per character, as asm6x takes .half "ab" */
            if (t[cuts[k]].kind == T_STR && cuts[k + 1] - 1 == cuts[k] + 1) {
                for (size_t c = 0; c < t[cuts[k]].text.size(); c++) u.emitData(width, (unsigned char)t[cuts[k]].text[c]);
                continue;
            }
            data_item(u, t, ITEM(k), width);
        }
        return true;
    }
    if (d == ".STRING" || d == ".CSTRING") {
        if (n < 1) { u.error(d + " needs a string"); return true; }
        u.placeLabel();
        for (size_t k = 0; k < n; k++) {
            if (t[cuts[k]].kind == T_STR && cuts[k + 1] - 1 == cuts[k] + 1) {
                std::string s = d == ".CSTRING" ? c_escapes(t[cuts[k]].text) : t[cuts[k]].text;
                for (size_t c = 0; c < s.size(); c++) u.emitData(1, (unsigned char)s[c]);
                if (d == ".CSTRING" && (s.empty() || s[s.size() - 1] != '\0')) u.emitData(1, 0);
            } else data_item(u, t, ITEM(k), 1);
        }
        return true;
    }
    if (d == ".FLOAT" || d == ".DOUBLE") {
        if (n < 1) { u.error(d + " needs a value"); return true; }
        u.align(d == ".FLOAT" ? 4 : 8);
        u.placeLabel();
        for (size_t k = 0; k < n; k++) {
            /* the item's text, as the lexer split it, read back as a real */
            std::string text;
            for (size_t j = cuts[k]; j < cuts[k + 1] - 1; j++) text += t[j].text;
            char *end = 0;
            double x = strtod(text.c_str(), &end);
            if (end == text.c_str() || *end) { u.error("'" + text + "' is not a real"); return true; }
            if (d == ".FLOAT") { float f = (float)x; unsigned long bits = 0; memcpy(&bits, &f, 4); u.emitData(4, bits); }
            else { unsigned long long bits; memcpy(&bits, &x, 8); u.emitData(8, bits); }
        }
        return true;
    }
    if (d == ".SPACE" || d == ".BES") {
        long long b;
        if (n != 1) { u.error(d + " takes a count"); return true; }
        if (!eval_const(u, t, ITEM(0), b, err, true)) { u.error(err); return true; }
        if (b < 0 || b > 0x7FFFFFFF) { u.error("the count is 0 to 2^31-1"); return true; }
        if (d == ".BES") {
            /* the label names the last byte of the space */
            for (long long k = 0; k + 1 < b; k++) u.emitData(1, 0);
            u.placeLabel();
            if (b > 0) u.emitData(1, 0);
        } else { u.placeLabel(); for (long long k = 0; k < b; k++) u.emitData(1, 0); }
        return true;
    }
    if (d == ".SET" || d == ".EQU") { u.error(d + " takes its name from the label field"); return true; }
    if (d == ".ASG") {
        /* .asg text, name: from here the name stands for the text wherever it appears as an
           operand - cl6x writes .asg A15, FP */
        if (n != 2 || t[cuts[1]].kind != T_NAME || cuts[2] - 1 != cuts[1] + 1) { u.error(".asg takes a value and a name"); return true; }
        std::vector<Token> value(t.begin() + (long)cuts[0], t.begin() + (long)(cuts[1] - 1));
        if (value.size() == 1 && value[0].kind == T_STR) {
            /* a quoted value is the text itself, read again */
            std::vector<Token> inner;
            if (!split_line(" " + value[0].text, inner, err)) { u.error(err); return true; }
            value = inner;
        }
        aliases[t[cuts[1]].text] = value;
        return true;
    }
    if (d == ".SYMDEPEND") {
        /* .symdepend "sym"[, "section"]: the section (the current one when unnamed) needs the
           symbol - an R_C6000_NONE relocation at its start, as asm6x records it */
        if (n < 1 || n > 2) { u.error(".symdepend takes a symbol and, optionally, a section"); return true; }
        for (size_t k = 0; k < n; k++)
            if ((t[cuts[k]].kind != T_STR && t[cuts[k]].kind != T_NAME) || cuts[k + 1] - 1 != cuts[k] + 1) { u.error(".symdepend takes names"); return true; }
        int s = u.ref(t[cuts[0]].text);
        u.symbols[s].referenced = true;
        std::string name = n == 2 ? t[cuts[1]].text : u.sections[u.current].name;
        u.depends.push_back(std::make_pair(s, name));   /* the section may be opened later in the file */
        return true;
    }
    if (d == ".END") { done = true; return true; }
    if (d == ".DWPSN") {
        /* .dwpsn file "f",line n,column c,is_stmt,address sym,isa 0 (cl6x -g; cpp11 -g): the position of the
           next instruction of this section, a row of the .debug_line dwarf.cpp builds; `address` opens a sequence */
        DwRow r; r.section = u.current; r.insn = u.sections[(size_t)u.current].insns.size();
        r.line = 0; r.column = 0; r.stmt = false; r.newSeq = false;
        for (size_t k = 0; k < n; k++) {
            size_t a = cuts[k], last = cuts[k + 1] - 2;          /* the item's tokens, before the comma after it */
            if (last < a || t[a].kind != T_NAME) continue;
            std::string key = upper(t[a].text);
            bool one = last == a + 1;
            if (key == "FILE" && one && t[a + 1].kind == T_STR) r.file = t[a + 1].text;
            else if (key == "LINE" && one && t[a + 1].kind == T_NUM) r.line = (int)strtol(t[a + 1].text.c_str(), 0, 0);
            else if (key == "COLUMN" && one && t[a + 1].kind == T_NUM) r.column = (int)strtol(t[a + 1].text.c_str(), 0, 0);
            else if (key == "IS_STMT" && last == a) r.stmt = true;
            else if (key == "ADDRESS" && one) r.newSeq = true;
        }
        if (r.file.empty() || r.line <= 0) { u.error(".dwpsn takes file \"name\" and line n"); return true; }
        u.dwRows.push_back(r);
        return true;
    }
    /* .dwtag / .dwattr / .dwendtag (cl6x -g; c90, cpp11 and shalimar -g on tms6747): the functions and
       their variables. Not yet a .debug_info: kept as their text, a line each, for the .gride.dwtag
       section assembler.cpp writes - the line rebuilt from its tokens, a string quoted again */
    if (d == ".DWTAG" || d == ".DWATTR" || d == ".DWENDTAG") {
        std::string text = t[i].text;
        for (size_t k = i + 1; k < t.size(); k++) {
            const bool word = t[k].kind == T_NAME || t[k].kind == T_NUM;
            const char last = text.empty() ? ' ' : text[text.size() - 1];
            if (word && (isalnum((unsigned char)last) || last == '_')) text += ' ';
            if (t[k].kind == T_STR) text += "\"" + t[k].text + "\"";
            else text += t[k].text;
        }
        if (d == ".DWTAG" || d == ".DWENDTAG") u.dwTags += text + "\n";
        return true;
    }
    if (d == ".CLINK" || d == ".RETAIN" || d == ".NOCMP" || d == ".COMPILER_OPTS" || d == ".FILE" ||
        d == ".LIST" || d == ".NOLIST" || d == ".TITLE" || d == ".OPTION" || d == ".LENGTH" || d == ".WIDTH")
        return true;
    #undef ITEM
    return false;
}

namespace {

/* a memory operand: *R  *+R(n)  *-R(n)  *+R[n]  *-R[n]  *+R[Rn]  *-R[Rn]  *R++  *R--  *++R  *--R,
   the last four with (n), [n] or [Rn] */
bool mem_operand(Unit &u, const std::vector<Token> &t, size_t a, size_t b, Operand &o)
{
    size_t i = a + 1;    /* past the * */
    o.kind = O_MEM; o.minus = false; o.bracket = false; o.mode = 0; o.v = 0; o.offReg = -1;
    if (is_punct(t, i, '+')) i++;
    else if (is_punct(t, i, '-')) { o.minus = true; i++; }
    else if (i < b && t[i].kind == T_PUNCT && t[i].text == "++") { o.mode = 1; i++; }
    else if (i < b && t[i].kind == T_PUNCT && t[i].text == "--") { o.mode = 2; i++; }
    if (i >= b || t[i].kind != T_NAME || !reg_name(t[i].text, o.reg, o.side)) { u.error("a base register is expected in an address"); return false; }
    i++;
    if (i < b && t[i].kind == T_PUNCT && t[i].text == "++") { if (o.mode || o.minus) { u.error("a bad address"); return false; } o.mode = 3; i++; }
    else if (i < b && t[i].kind == T_PUNCT && t[i].text == "--") { if (o.mode || o.minus) { u.error("a bad address"); return false; } o.mode = 4; i++; }
    if (i >= b) {
        if (o.mode) { o.v = 1; o.bracket = true; }
        else if (a + 1 < b && t[a + 1].kind == T_PUNCT && (t[a + 1].text == "+" || t[a + 1].text == "-")) { u.error("the address is missing its offset"); return false; }
        return true;
    }
    char open = t[i].text[0], close = open == '(' ? ')' : ']';
    if (!is_punct(t, i, open) || (open != '(' && open != '[') || !is_punct(t, b - 1, close)) { u.error("a bad address"); return false; }
    o.bracket = open == '[';
    int r, sd;
    if (b - 1 == i + 2 && t[i + 1].kind == T_NAME && reg_name(t[i + 1].text, r, sd)) {
        if (!o.bracket) { u.error("a register offset is written [Rn]"); return false; }
        if (sd != o.side) { u.error("the offset register is on the base's side"); return false; }
        o.offReg = r;
        return true;
    }
    std::string err;
    if (!eval_const(u, t, i + 1, b - 1, o.v, err)) { u.error(err); return false; }
    return true;
}

bool operand(Unit &u, const std::vector<Token> &t, size_t a, size_t b, Operand &o)
{
    if (a >= b) { u.error("an operand is missing"); return false; }
    if (is_punct(t, a, '*')) return mem_operand(u, t, a, b, o);
    if (b - a == 1 && t[a].kind == T_NAME && reg_name(t[a].text, o.reg, o.side)) { o.kind = O_REG; return true; }
    if (b - a == 3 && t[a].kind == T_NAME && is_punct(t, a + 1, ':') && t[a + 2].kind == T_NAME) {
        int hi, hs, lo, ls;
        if (!reg_name(t[a].text, hi, hs) || !reg_name(t[a + 2].text, lo, ls)) { u.error("a bad register pair"); return false; }
        if (hs != ls || hi != lo + 1 || (lo & 1)) { u.error("a register pair is odd:even of one side"); return false; }
        o.kind = O_PAIR; o.reg = lo; o.side = ls;
        return true;
    }
    std::string err;
    if (!eval(u, t, a, b, o.val, err)) { u.error(err); return false; }
    if (o.val.sub >= 0) { u.error("a difference of labels in different sections cannot be an operand"); return false; }
    if (o.val.secref >= 0) { u.error("$ is not an operand"); return false; }
    if (o.val.operate) { u.error("an exception-table operator is not an operand"); return false; }
    if (o.val.sym < 0) { o.kind = O_CST; o.v = o.val.v; return true; }
    o.kind = O_EXPR;
    o.v = o.val.v;
    return true;
}

std::string shape_of(const std::vector<Operand> &o)
{
    std::string shape;
    for (size_t k = 0; k < o.size(); k++)
        shape += o[k].kind == O_REG ? 'r' : o[k].kind == O_PAIR ? 'p' : o[k].kind == O_CST ? 'c' : o[k].kind == O_MEM ? 'm' : 's';
    return shape;
}

bool commutative(const std::string &m)
{
    static const char *const list[] = {
        "ADD", "AND", "OR", "XOR", "CMPEQ", "MPY32", "MPY", "MPYU", "ADDSP", "MPYSP", "CMPEQSP", "ADDDP", "MPYDP", "CMPEQDP",
        "MPY32U", "ADDU", "MAX2", "MIN2", "ADD2", "ADD4", "CMPEQ2", "CMPEQ4", "SADD", "AVG2", "DOTP2", "MPY2", "SMPY", "SMPY2",
        "MPYLI", "MPYHI", "MPYH", "MPYHU", 0 };
    for (int k = 0; list[k]; k++) if (m == list[k]) return true;
    return false;
}

/* the compare of the other sense, for the sources turned about */
std::string turned(const std::string &m)
{
    if (m.compare(0, 5, "CMPLT") == 0) return "CMPGT" + m.substr(5);
    if (m.compare(0, 5, "CMPGT") == 0) return "CMPLT" + m.substr(5);
    return std::string();
}

/* the register, constant and pair forms, from the table. Without a unit named, the rows
   asm6x chose on its own come first, then every row of that same unit (the probes named the
   cross path of a form where its default did not); with one, the rows of that unit. Last, a
   crossed source on an uncrossed row where the mnemonic, shape and unit have no crossed row
   at all: the probes could not name every constant or unary form's cross path, and asm6x
   encodes AND.D1X 3,B1,A2 and ABS.L1X B1,A2 as the plain row with x set. */
bool table_form(const std::string &m, const UnitName &un, const std::vector<Operand> &o, Encoded &e)
{
    if (o.empty() || (o.back().kind != O_REG && o.back().kind != O_PAIR)) return false;
    std::string shape = shape_of(o);
    int dst = (int)o.size() - 1;
    int s = o[dst].side;
    const size_t nForms = sizeof kForms / sizeof *kForms, nMore = sizeof kMoreForms / sizeof *kMoreForms;
    /* the units asm6x takes for this mnemonic and shape when none is named */
    std::string dfltUnits;
    for (size_t r = 0; r < nForms + nMore; r++) {
        const FormRow &f = r < nForms ? kForms[r] : kMoreForms[r - nForms];
        if (f.dflt && strcmp(f.name, m.c_str()) == 0 && strcmp(f.shape, shape.c_str()) == 0 && dfltUnits.find(f.unit) == std::string::npos) dfltUnits += f.unit;
    }
    for (int pass = 0; pass < 3; pass++) {
        if (pass == 1 && un.letter) continue;
        for (size_t r = 0; r < nForms + nMore; r++) {
            const FormRow &f = r < nForms ? kForms[r] : kMoreForms[r - nForms];
            if (strcmp(f.name, m.c_str()) != 0 || strcmp(f.shape, shape.c_str()) != 0) continue;
            if (un.letter ? f.unit != un.letter : (pass == 0 ? !f.dflt : dfltUnits.find(f.unit) == std::string::npos)) continue;
            bool cross = f.cross != 0;
            if (pass == 2) {
                if (cross || (f.form & 0x400)) continue;
                bool any = false;
                for (size_t q = 0; q < nForms + nMore && !any; q++) {
                    const FormRow &g = q < nForms ? kForms[q] : kMoreForms[q - nForms];
                    any = g.cross && g.unit == f.unit && strcmp(g.name, m.c_str()) == 0 && strcmp(g.shape, shape.c_str()) == 0;
                }
                if (any) continue;
                cross = true;
            }
            if (un.letter && !un.loose && un.cross != cross) continue;
            bool ok = true, crossed = false, src1cross = f.map[0] == 6 || f.map[1] == 6;
            for (int k = 0; k < dst && ok; k++) {
                int mp = f.map[k];
                if (pass == 2 && o[k].kind == O_PAIR && o[k].side != s) { ok = false; break; }   /* a long operand never crosses */
                if (mp == 1) { ok = (o[k].side != s) == (cross && !src1cross); crossed = crossed || o[k].side != s; }
                else if (mp == 6) { ok = o[k].side != s; crossed = true; }
                else if (mp == 2 || mp == 5) ok = o[k].side == s;
                else if (mp == 3 || mp == 4) ok = f.cst == 'u' ? (o[k].v >= 0 && o[k].v <= 31) : (o[k].v >= -16 && o[k].v <= 15);
            }
            if (!ok || (pass == 2 && !crossed)) continue;
            unsigned long w = ((unsigned long)(f.form & 0x3FF) << 2) | (f.form & 0x400 ? 1ul << 12 : 0) | (cross ? 1ul << 12 : 0) | ((unsigned long)s << 1);
            for (int k = 0; k <= dst; k++) {
                int mp = f.map[k];
                unsigned long v = o[k].kind == O_CST ? ((unsigned long)o[k].v & 31) : (unsigned long)o[k].reg;
                if (mp == 0) w |= v << 23;
                else if (mp == 1 || mp == 4) w |= v << 18;
                else if (mp == 2 || mp == 3 || mp == 6) w |= v << 13;
                else if (mp == 5) w |= (v + 1) << 18 | v << 13;
            }
            if (m == "NOT") w |= 31ul << 13;    /* XOR with -1, on every unit */
            e.w = w; e.unit = f.unit; e.side = s;
            return true;
        }
    }
    return false;
}

int access_width(const std::string &m)
{
    if (m == "LDB" || m == "LDBU" || m == "STB") return 1;
    if (m == "LDH" || m == "LDHU" || m == "STH") return 2;
    if (m == "LDDW" || m == "STDW" || m == "LDNDW" || m == "STNDW") return 8;
    return 4;
}

/* the loads and stores: bits 2-8 with the mode and y cleared */
struct MemForm { const char *name; unsigned form; };
const MemForm kMemory[] = {
    { "LDB", 0x009 }, { "LDBU", 0x005 }, { "LDH", 0x011 }, { "LDHU", 0x001 }, { "LDW", 0x019 }, { "LDDW", 0x059 },
    { "LDNW", 0x04D }, { "LDNDW", 0x049 },
    { "STB", 0x00D }, { "STH", 0x015 }, { "STW", 0x01D }, { "STDW", 0x051 }, { "STNW", 0x055 }, { "STNDW", 0x05D },
};

unsigned mem_form(const std::string &m)
{
    for (size_t k = 0; k < sizeof kMemory / sizeof *kMemory; k++) if (m == kMemory[k].name) return kMemory[k].form;
    return 0xFFFF;
}

bool encode_memory(Unit &u, const std::string &m, const UnitName &un, const std::vector<Operand> &o, Encoded &e)
{
    bool load = m[0] == 'L';
    if (o.size() != 2 || (load ? o[0].kind != O_MEM : o[1].kind != O_MEM)) { u.error(m + " takes an address and a register" + (load ? std::string(", that way about") : std::string(", the other way about"))); return false; }
    const Operand &mem = load ? o[0] : o[1];
    const Operand &reg = load ? o[1] : o[0];
    unsigned f = mem_form(m);
    int width = access_width(m);
    bool pair = width == 8;
    if (pair ? reg.kind != O_PAIR : reg.kind != O_REG) { u.error(m + (pair ? " moves a register pair" : " moves one register")); return false; }
    if (un.letter && (un.letter != 'D' || (un.cross && !un.loose))) { u.error(m + " is a .D instruction"); return false; }
    if (un.letter && !un.loose && un.side != mem.side) { u.error("the base register is on the other side of the named unit"); return false; }
    if (un.dataSide >= 0 && un.dataSide != reg.side) { u.error("the data register is on the other side of the named unit's T"); return false; }
    unsigned long w;
    if (mem.offReg >= 0) {
        static const unsigned modes[5] = { 5, 13, 12, 15, 14 };
        unsigned mode = mem.mode ? modes[mem.mode] : (mem.minus ? 4 : 5);
        w = ((unsigned long)f << 2) | ((unsigned long)reg.reg << 23) | ((unsigned long)mem.reg << 18) | ((unsigned long)mem.offReg << 13) |
            ((unsigned long)mode << 9) | ((unsigned long)mem.side << 7) | ((unsigned long)reg.side << 1);
    } else {
        long long off = mem.v;
        bool minus = mem.minus;
        if (off < 0) { off = -off; minus = !minus; }       /* asm6x turns *+R(-4) into *-R[1] */
        if (!mem.bracket) {
            if (off % width) { u.warn("the offset is not a multiple of the access; truncated"); off -= off % width; }
            off /= width;
        }
        if (mem.mode && minus) { u.error("a bad address"); return false; }
        if (off > 31 && mem.mode == 0 && !minus && mem.side == 1 && (mem.reg == 14 || mem.reg == 15) && off < 32768 && (f & 0x40) == 0) {
            /* the 15-bit offset from the data or stack pointer: the constant in 8-22, the
               pointer in bit 7, and 11 where the other form has 01 */
            w = ((unsigned long)(f & 0x1C) << 2) | 0xC | ((unsigned long)reg.reg << 23) | ((unsigned long)off << 8) |
                (mem.reg == 15 ? 1ul << 7 : 0) | ((unsigned long)reg.side << 1);
        } else {
            if (off > 31) { u.error("the offset is 0 to 31 units of the access, or up to 32767 from B14 or B15"); return false; }
            static const unsigned modes[5] = { 1, 9, 8, 11, 10 };
            unsigned mode = mem.mode ? modes[mem.mode] : (minus ? 0 : 1);
            w = ((unsigned long)f << 2) | ((unsigned long)reg.reg << 23) | ((unsigned long)mem.reg << 18) | ((unsigned long)off << 13) |
                ((unsigned long)mode << 9) | ((unsigned long)mem.side << 7) | ((unsigned long)reg.side << 1);
        }
    }
    if (m == "LDNDW" || m == "STNDW") w = (w & ~(31ul << 23)) | ((unsigned long)(reg.reg + 1) << 23);
    e.w = w; e.unit = 'D'; e.side = mem.side;
    return true;
}

bool unit_is(Unit &u, const std::string &m, const UnitName &un, char letter, int side, bool cross)
{
    if (!un.letter) return true;
    if (un.letter != letter || un.dataSide >= 0) { u.error(m + std::string(" is a .") + letter + " instruction"); return false; }
    if (un.loose) return true;
    if (side >= 0 && un.side != side) { u.error(m + " is not on the named unit's side"); return false; }
    if (un.cross != cross) { u.error(m + (cross ? " crosses; the unit needs X" : " does not cross; the unit takes no X")); return false; }
    return true;
}

/* MVK, MVKL, MVKH, MVKLH: the 16-bit constant in bits 7-22 of the .S form; MVK of -16..15 is
   asm6x's MVK.L, the L form with the constant in src2 */
bool encode_mvk(Unit &u, const std::string &m, const UnitName &un, const std::vector<Operand> &o, Encoded &e)
{
    if (o.size() != 2 || o[1].kind != O_REG || (o[0].kind != O_CST && o[0].kind != O_EXPR)) { u.error(m + " takes a constant or a symbol, and a register"); return false; }
    int side = o[1].side;
    unsigned long dst = ((unsigned long)o[1].reg << 23) | ((unsigned long)side << 1);
    if (un.letter && !un.loose && un.cross) { u.error(m + " has no cross path"); return false; }
    if (un.letter && !un.loose && un.side != side) { u.error(m + " is not on the named unit's side"); return false; }
    e.side = side; e.unit = 'S';
    if (o[0].kind == O_EXPR) {
        if (m == "MVKLH") { u.error("MVKLH takes a constant"); return false; }
        if (un.letter && un.letter != 'S') { u.error(m + " of a symbol is a .S instruction"); return false; }
        e.w = (m == "MVKH" ? 0x68ul : 0x28ul) | dst;
        e.fixSym = o[0].val.sym; e.fixKind = m == "MVK" ? R_ABS_S16 : m == "MVKL" ? R_ABS_L16 : R_ABS_H16; e.fixAdd = o[0].v;
        return true;
    }
    long long c = o[0].v;
    if (m == "MVK") {
        bool small = c >= -16 && c <= 15;
        if (c < -32768 || c > 32767) { u.warn("the constant is truncated to 16 bits"); c = (long long)(short)(c & 0xFFFF); small = false; }
        if (un.letter == 'L' || (!un.letter && small)) {
            if (c < -16 || c > 15) { u.error("MVK on .L takes -16 to 15"); return false; }
            e.w = 0xA358 | dst | (((unsigned long)c & 31) << 18); e.unit = 'L';
            return true;
        }
        if (un.letter == 'D') {
            /* the C64x+ MVK .D, which asm6x takes when .L and .S are spoken for in a packet */
            if (c < -16 || c > 15) { u.error("MVK on .D takes -16 to 15"); return false; }
            e.w = 0x40 | dst | (((unsigned long)c & 31) << 13); e.unit = 'D';
            return true;
        }
        if (un.letter && un.letter != 'S') { u.error("MVK is a .S or .L instruction"); return false; }
        e.w = 0x28 | dst | (((unsigned long)c & 0xFFFF) << 7);
        return true;
    }
    if (un.letter && un.letter != 'S') { u.error(m + " is a .S instruction"); return false; }
    if (c < -2147483648LL || c > 4294967295LL) u.warn("the constant is truncated to 32 bits");
    unsigned long half = m == "MVKH" ? ((unsigned long)c >> 16) & 0xFFFF : (unsigned long)c & 0xFFFF;
    e.w = (m == "MVKL" ? 0x28ul : 0x68ul) | dst | (half << 7);
    return true;
}

bool encode_branch(Unit &u, const std::string &m, const UnitName &un, const std::vector<Operand> &o, unsigned creg, Encoded &e)
{
    bool nop = m == "BNOP" || m == "RETNOP";
    size_t need = m == "CALLP" ? 2 : 1;
    if (o.size() < need || o.size() > need + (nop ? 1 : 0)) { u.error(m + " takes " + (m == "CALLP" ? "a target and the return register" : nop ? "a target and a count" : "a target")); return false; }
    long long count = 0;
    if (nop && o.size() == 2) {
        if (o[1].kind != O_CST) { u.error(m + "'s count is a constant"); return false; }
        count = o[1].v;
        if (count < 0 || count > 7) { u.error(m + "'s count is 0 to 7"); return false; }
    }
    const Operand &tg = o[0];
    if (tg.kind == O_REG) {
        /* by register: always .S2, the cross path for an A register */
        if (m == "CALLP") { u.error("CALLP takes a label"); return false; }
        if (!unit_is(u, m, un, 'S', 1, tg.side == 0)) return false;
        e.w = (nop ? 0x800362ul : 0x362ul) | ((unsigned long)tg.reg << 18) | (tg.side ? 0 : 1ul << 12) | ((unsigned long)count << 13);
        e.unit = 'S'; e.side = 1;
        return true;
    }
    if (m == "RET" || m == "RETNOP") { u.error(m + " takes a register"); return false; }
    if (tg.kind != O_EXPR) { u.error(m + " takes a label or a register"); return false; }
    int side;
    if (m == "CALLP") {
        if (o[1].kind != O_REG || o[1].reg != 3) { u.error("A3 or B3 is required as the return address"); return false; }
        if (creg) { u.error("CALLP must be unconditional"); return false; }
        side = o[1].side;
        if (!unit_is(u, m, un, 'S', side, false)) return false;
        e.w = 0x10000010ul | ((unsigned long)side << 1);
    } else {
        side = un.letter && !un.loose ? un.side : nop ? 0 : 1;      /* asm6x's defaults: B on .S2, BNOP on .S1 */
        if (!unit_is(u, m, un, 'S', side, false)) return false;
        e.w = (nop ? 0x120ul | ((unsigned long)count << 13) : 0x10ul) | ((unsigned long)side << 1);
    }
    e.unit = 'S'; e.side = side;
    e.fixKind = nop ? R_PCR_S12 : R_PCR_S21;
    e.fixSym = tg.val.sym; e.fixAdd = tg.v;
    return true;
}

bool encode_mvc(Unit &u, const std::vector<Token> &t, const std::vector<size_t> &cuts, size_t n, const UnitName &un, Encoded &e)
{
    /* a control register in or out, on .S2: the register side is B, or crosses in. The
       control register is a name, not a symbol, so it is read before the operands are. */
    static const struct { const char *name; unsigned code; } cregs[] = {
        { "AMR", 0 }, { "CSR", 1 }, { "IFR", 2 }, { "ISR", 2 }, { "ICR", 3 }, { "IER", 4 }, { "ISTP", 5 }, { "IRP", 6 },
        { "NRP", 7 }, { "TSCL", 10 }, { "TSCH", 11 }, { "ILC", 13 }, { "RILC", 14 }, { "REP", 15 }, { "PCE1", 16 },
        { "DNUM", 17 }, { "SSR", 21 }, { "GPLYA", 22 }, { "GPLYB", 23 }, { "GFPGFR", 24 }, { "DIER", 25 }, { "TSR", 26 },
        { "ITSR", 27 }, { "NTSR", 28 }, { "EFR", 29 }, { "ECR", 29 }, { "IERR", 31 } };
    if (n != 2 || cuts[1] - 1 != cuts[0] + 1 || cuts[2] - 1 != cuts[1] + 1 ||
        t[cuts[0]].kind != T_NAME || t[cuts[1]].kind != T_NAME) { u.error("MVC takes a register and a control register"); return false; }
    int reg, side, code = -1;
    bool in = reg_name(t[cuts[0]].text, reg, side);
    std::string cname = upper(t[in ? cuts[1] : cuts[0]].text);
    if (!in && !reg_name(t[cuts[1]].text, reg, side)) { u.error("MVC takes a register and a control register"); return false; }
    for (size_t k = 0; k < sizeof cregs / sizeof *cregs; k++) if (cname == cregs[k].name) code = (int)cregs[k].code;
    if (code < 0) { u.error("'" + cname + "' is not a control register"); return false; }
    if (!unit_is(u, "MVC", un, 'S', 1, in && !side)) return false;
    if (in) e.w = 0x3A2 | ((unsigned long)code << 23) | ((unsigned long)reg << 18) | (side ? 0 : 1ul << 12);
    else {
        if (!side) { u.error("MVC out of a control register lands on the B side"); return false; }
        e.w = 0x3E2 | ((unsigned long)reg << 23) | ((unsigned long)code << 18);
    }
    e.unit = 'S'; e.side = 1;
    return true;
}

/* everything but MVC: the special forms first, then asm6x's rewrites, then the table */
bool encode(Unit &u, std::string m, const UnitName &un, std::vector<Operand> &o, unsigned creg, Encoded &e)
{
    size_t n = o.size();
    #define REG(k) (o[k].kind == O_REG)
    #define PAIR(k) (o[k].kind == O_PAIR)
    #define CST(k) (o[k].kind == O_CST)
    #define SYM(k) (o[k].kind == O_EXPR)

    if (m == "NOP") {
        long long c = 1;
        if (n == 1 && CST(0)) c = o[0].v;
        else if (n != 0) { u.error("NOP takes a count"); return false; }
        if (un.letter) { u.error("NOP takes no unit"); return false; }
        if (c < 1) { u.error("NOP takes 1 to 9"); return false; }
        if (c > 9) { u.warn("NOP takes 1 to 9; 9 taken"); c = 9; }
        e.w = (unsigned long)(c - 1) << 13;
        return true;
    }
    if (m == "SWE" || m == "IDLE") {
        if (n || un.letter) { u.error(m + " takes nothing"); return false; }
        e.w = m == "SWE" ? 0x10000000ul : 0x0001E000ul;
        return true;
    }
    if (m == "B" || m == "RET" || m == "CALLP" || m == "BNOP" || m == "RETNOP") return encode_branch(u, m, un, o, creg, e);
    if (mem_form(m) != 0xFFFF) return encode_memory(u, m, un, o, e);
    if (m == "MVK" || m == "MVKL" || m == "MVKH" || m == "MVKLH") return encode_mvk(u, m, un, o, e);
    if (m == "ADDK") {
        if (n != 2 || !CST(0) || !REG(1)) { u.error("ADDK takes a constant and a register"); return false; }
        if (!unit_is(u, m, un, 'S', o[1].side, false)) return false;
        long long c = o[0].v;
        if (c < -32768 || c > 32767) { u.error("ADDK's constant is 16 bits signed"); return false; }
        e.w = 0x50 | ((unsigned long)o[1].reg << 23) | ((unsigned long)o[1].side << 1) | (((unsigned long)c & 0xFFFF) << 7);
        e.unit = 'S'; e.side = o[1].side;
        return true;
    }
    if (m == "ZERO") {
        if (n != 1 || (!REG(0) && !PAIR(0))) { u.error("ZERO takes a register or a pair"); return false; }
        e.side = o[0].side;
        if (PAIR(0)) {
            if (!unit_is(u, m, un, 'L', e.side, false)) return false;
            e.w = 0x4F8ul | ((unsigned long)o[0].reg << 23) | ((unsigned long)e.side << 1); e.unit = 'L';
        } else if (un.letter == 'S') {
            if (!unit_is(u, m, un, 'S', e.side, false)) return false;
            e.w = 0x28ul | ((unsigned long)o[0].reg << 23) | ((unsigned long)e.side << 1); e.unit = 'S';
        } else {
            if (!unit_is(u, m, un, 'L', e.side, false)) return false;
            e.w = 0xA358ul | ((unsigned long)o[0].reg << 23) | ((unsigned long)e.side << 1); e.unit = 'L';
        }
        return true;
    }
    if ((m == "EXT" || m == "EXTU" || m == "CLR" || m == "SET") && n == 4) {
        if (!REG(0) || !CST(1) || !CST(2) || !REG(3)) { u.error(m + " takes a register, two constants and a register"); return false; }
        if (o[0].side != o[3].side) { u.error(m + "'s source and destination are on one side"); return false; }
        if (!unit_is(u, m, un, 'S', o[3].side, false)) return false;
        if (o[1].v < 0 || o[1].v > 31 || o[2].v < 0 || o[2].v > 31) { u.error(m + "'s constants are 0 to 31"); return false; }
        unsigned long base = m == "EXT" ? 0x48 : m == "EXTU" ? 0x08 : m == "CLR" ? 0xC8 : 0x88;
        e.w = base | ((unsigned long)o[3].reg << 23) | ((unsigned long)o[0].reg << 18) | ((unsigned long)o[1].v << 13) |
              ((unsigned long)o[2].v << 8) | ((unsigned long)o[3].side << 1);
        e.unit = 'S'; e.side = o[3].side;
        return true;
    }
    if ((m == "ADDAB" || m == "ADDAH" || m == "ADDAW") && n == 3 && REG(0) && CST(1) && REG(2) && o[0].side == 1 && (o[0].reg == 14 || o[0].reg == 15)) {
        /* the C64x+ 15-bit address add from the data or stack pointer, which asm6x takes for
           any constant from B14 or B15: the constant in 8-22, the pointer in bit 7, the op
           in 4-6 and 11 in 2-3 */
        long long c = o[1].v;
        if (c < 0 || c > 32767) { u.warn("the constant is truncated to 15 bits"); }
        if (!unit_is(u, m, un, 'D', o[2].side, o[2].side == 0)) return false;
        e.w = 0x10000000ul | ((unsigned long)o[2].reg << 23) | (((unsigned long)c & 0x7FFF) << 8) | (o[0].reg == 15 ? 1ul << 7 : 0) |
              ((unsigned long)(m == "ADDAB" ? 3 : m == "ADDAH" ? 5 : 7) << 4) | 0xC | ((unsigned long)o[2].side << 1);
        e.unit = 'D'; e.side = o[2].side;
        return true;
    }

    /* asm6x's rewrites, read back from it, for a register on the destination's side: ADD of
       -16..-1 keeps the signed .D form (the table's 0x2BC, which dis6x lists as SUB), ADD of
       -31..-17 is SUB of the positive, SUB r,-c is ADD r,c; across the cross path SUB r,c is
       ADD r,-c in the signed form; a compare written register first is the constant-first
       compare of the other sense */
    if (n == 3 && REG(2) && (un.letter == 0 || un.letter == 'D')) {
        int r = CST(0) && REG(1) ? 1 : REG(0) && CST(1) ? 0 : -1;
        if (r >= 0) {
            int c = 1 - r;
            bool same = o[r].side == o[2].side;
            if (m == "ADD" && o[c].v < -16 && o[c].v >= -31 && same) {
                m = "SUB";
                o[c].v = -o[c].v;
                if (r == 1) { Operand x = o[0]; o[0] = o[1]; o[1] = x; }
            } else if (m == "SUB" && r == 0 && o[c].v < 0 && o[c].v >= -31 && same) {
                m = "ADD";
                o[c].v = -o[c].v;
            } else if (m == "SUB" && r == 0 && o[c].v > 0 && o[c].v <= 16 && !same) {
                m = "ADD";
                o[c].v = -o[c].v;
            }
        }
    }
    if (n == 3 && REG(0) && CST(1) && REG(2) && m.compare(0, 3, "CMP") == 0) {
        std::string tm = turned(m);
        Operand x = o[0]; o[0] = o[1]; o[1] = x;
        if (!tm.empty()) m = tm;
    }
    for (int attempt = 0; attempt < 2; attempt++) {
        if (table_form(m, un, o, e)) return true;
        /* no row fits as written: asm6x turns the sources of a commutative operation about
           when the first is the one across, and a compare with its sense */
        if (attempt == 0 && n == 3 && (REG(0) || CST(0) || PAIR(0)) && (REG(1) || CST(1) || PAIR(1)) && (REG(2) || PAIR(2))) {
            std::string tm = turned(m);
            if (commutative(m) || !tm.empty()) {
                Operand x = o[0]; o[0] = o[1]; o[1] = x;
                if (!tm.empty()) m = tm;
                continue;
            }
        }
        break;
    }
    #undef REG
    #undef PAIR
    #undef CST
    #undef SYM
    std::string shape = shape_of(o);
    if (shape.find('s') != std::string::npos) u.error(m + " does not take a symbol; a symbol's address comes by MVKL and MVKH");
    else if (un.letter) u.error(m + " with these operands has no form on the named unit");
    else u.error(m + " with these operands is not an instruction this assembler knows");
    return false;
}

}

namespace {

unsigned unit_bit(const Encoded &e)
{
    if (!e.unit) return 0;
    return 1u << ((e.unit == 'D' ? 0 : e.unit == 'L' ? 2 : e.unit == 'M' ? 4 : 6) + e.side);
}

}

/* another unit for an instruction of a packet, free among `taken`: the units in asm6x's
   order D, S, L, M after the one it has, each tried with only the letter asked for. With
   keepUnit the instruction may stay where it is - it is an earlier one being asked to make
   way, and the unit it holds is in `taken` for the asker's sake. */
bool C6xTarget::retarget(Unit &u, Packet &p, unsigned taken, bool keepUnit)
{
    static const char order[] = "DSLM";
    size_t errs = u.errors.size(), warns = u.warnings.size();
    /* A branch to a label may take .S on either side - asm6x moves `B` to .S1 when .S2 is
       spoken for, as `MVK 32, B0 || B label` needs. Its fixup names the side in no way, so
       one already recorded for an earlier instruction of the packet stays right. */
    if ((p.mnemonic == "B" || p.mnemonic == "BNOP") && p.e.unit == 'S' && p.e.side >= 0 && p.e.fixSym >= 0) {
        UnitName un;
        un.letter = 'S'; un.side = 1 - p.e.side; un.cross = false; un.dataSide = -1; un.loose = false;
        std::vector<Operand> o(p.operands);
        Encoded e;
        e.w = 0; e.unit = 0; e.side = -1; e.fixSym = -1; e.fixKind = R_NONE; e.fixAdd = 0;
        bool ok = encode(u, p.mnemonic, un, o, p.creg, e);
        u.errors.resize(errs);
        u.warnings.resize(warns);
        if (ok && e.fixKind == p.e.fixKind && !(taken & unit_bit(e))) { p.e = e; return true; }
        return false;
    }
    if (p.mnemonic == "MVC" || p.e.fixSym >= 0) return false;
    for (int k = 0; k < 4; k++) {
        if (order[k] == p.e.unit) continue;
        UnitName un;
        un.letter = order[k]; un.side = -1; un.cross = false; un.dataSide = -1; un.loose = true;
        std::vector<Operand> o(p.operands);
        Encoded e;
        e.w = 0; e.unit = 0; e.side = -1; e.fixSym = -1; e.fixKind = R_NONE; e.fixAdd = 0;
        bool ok = encode(u, p.mnemonic, un, o, p.creg, e);
        u.errors.resize(errs);
        u.warnings.resize(warns);
        if (!ok || e.fixSym >= 0 || (taken & unit_bit(e)) || (!keepUnit && e.unit == p.e.unit)) continue;
        p.e = e;
        return true;
    }
    return false;
}

/* one instruction: the predicate and parallel bar first, then the mnemonic, a unit if one is
   named, and the operands */
void C6xTarget::instruction(Unit &u, const std::vector<Token> &t, size_t i)
{
    Section *sec = u.cur();
    if (!sec) return;
    bool par = false;
    unsigned creg = 0, z = 0;
    if (i < t.size() && t[i].kind == T_PUNCT && t[i].text == "||") { par = true; i++; }
    if (is_punct(t, i, '[')) {
        i++;
        bool neg = false;
        if (is_punct(t, i, '!')) { neg = true; i++; }
        int reg, side;
        if (i >= t.size() || t[i].kind != T_NAME || !reg_name(t[i].text, reg, side) || predicate_code(reg, side) < 0 ||
            !is_punct(t, i + 1, ']')) {
            u.error("a predicate is [A0], [A1], [A2], [B0], [B1] or [B2], with ! before it for the zero test");
            return;
        }
        creg = (unsigned)predicate_code(reg, side);
        z = neg ? 1 : 0;
        i += 2;
        if (i < t.size() && t[i].kind == T_PUNCT && t[i].text == "||") { u.error("|| comes before the predicate"); return; }
    }
    if (i >= t.size() || t[i].kind != T_NAME) { u.error("an instruction is expected"); return; }
    std::string m = upper(t[i].text);
    i++;
    UnitName un;
    un.letter = 0; un.side = -1; un.cross = false; un.dataSide = -1; un.loose = false;
    std::string why;
    size_t dot = m.find('.');
    if (dot != std::string::npos) {
        if (!parse_unit(m.substr(dot + 1), un, why)) { u.error(why); return; }
        m = m.substr(0, dot);
    } else if (i < t.size() && t[i].kind == T_NAME && t[i].text[0] == '.') {
        if (!parse_unit(t[i].text.substr(1), un, why)) { u.error(why); return; }
        i++;
    }

    /* the section is code from here; an instruction after data takes the next word, and a
       label waiting from the line before takes that word too */
    if (!sec->code) sec->code = true;
    if (!sec->hasCode) { sec->hasCode = true; if (sec->align < 32) sec->align = 32; }
    u.align(4);
    if (par) {
        if (!u.pendingLabels.empty()) { u.error("|| cannot follow a label"); return; }
        if (last_section != u.current || last_at + 4 != u.here()) { u.error("|| needs an instruction just before it"); return; }
    }
    u.placeLabel();

    std::vector<size_t> cuts;
    size_t n;
    if (!count_items(u, t, i, cuts, n)) return;
    Encoded e;
    e.w = 0; e.unit = 0; e.side = -1; e.fixSym = -1; e.fixKind = R_NONE; e.fixAdd = 0;
    std::vector<Operand> operands;
    if (m == "MVC") {
        if (!encode_mvc(u, t, cuts, n, un, e)) return;
    } else {
        for (size_t k = 0; k < n; k++) {
            Operand x;
            if (!operand(u, t, cuts[k], cuts[k + 1] - 1, x)) return;
            operands.push_back(x);
        }
        std::vector<Operand> o(operands);
        if (!encode(u, m, un, o, creg, e)) return;
    }
    if (creg && (m == "NOP" || m == "SWE" || m == "IDLE")) { u.error(m + " cannot be predicated"); return; }
    /* the unit the word takes, for a packet. When two instructions of one packet want one
       unit, asm6x moves one of them to a unit that is free and has a form for it: the later
       instruction if it can move, else an earlier one that can - read back from it in
       tests/probes and review-probes-2026-09-19/edge/15*-16*. A unit named in the source
       is not moved. */
    unsigned want = unit_bit(e);
    if (par) {
        /* asm6x: "Too many branches to a label in this packet" - a second one is refused before any unit moves. */
        if ((m == "B" || m == "BNOP") && e.fixSym >= 0)
            for (size_t k = 0; k < packet.size(); k++)
                if ((packet[k].mnemonic == "B" || packet[k].mnemonic == "BNOP") && packet[k].e.fixSym >= 0) { u.error("too many branches to a label in this packet"); return; }
        if (units & want) {
            Packet mine;
            mine.mnemonic = m; mine.operands = operands; mine.creg = creg; mine.z = z; mine.at = u.here(); mine.e = e;
            if (!un.letter && retarget(u, mine, units, false)) { e = mine.e; want = unit_bit(e); }
            else {
                bool moved = false;
                for (size_t k = 0; k < packet.size() && !moved; k++) {
                    if (unit_bit(packet[k].e) != want || packet[k].named) continue;
                    unsigned others = units & ~want;
                    for (size_t j = 0; j < packet.size(); j++) if (j != k) others |= unit_bit(packet[j].e);
                    if (!retarget(u, packet[k], others | want, true)) continue;
                    unsigned long pw = (packet[k].e.w | ((unsigned long)packet[k].creg << 29) | ((unsigned long)packet[k].z << 28)) | (sec->bytes[packet[k].at] & 1);
                    for (int b = 0; b < 4; b++) sec->bytes[packet[k].at + (unsigned long)b] = (unsigned char)(pw >> (8 * b));
                    units = others | want | unit_bit(packet[k].e);
                    moved = true;
                }
                if (!moved) { u.error("two instructions of one packet want the same unit, and neither has a form on a free one"); return; }
                units &= ~want;     /* the mover's old unit is this instruction's now */
            }
        }
        sec->bytes[last_at] |= 1;
        units |= want;
    } else { units = want; packet.clear(); }
    unsigned long w = e.w | ((unsigned long)creg << 29) | ((unsigned long)z << 28);
    Packet entry;
    entry.mnemonic = m; entry.operands = operands; entry.creg = creg; entry.z = z; entry.at = u.here(); entry.e = e; entry.named = un.letter != 0;
    packet.push_back(entry);
    last_at = u.here();
    last_section = u.current;
    if (e.fixSym >= 0) u.fixup(last_at, e.fixSym, e.fixKind, e.fixAdd);
    u.emitWord(w);
}
