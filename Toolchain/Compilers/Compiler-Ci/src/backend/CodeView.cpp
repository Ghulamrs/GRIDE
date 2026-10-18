#include "CodeView.h"

#include <map>

namespace {

// Symbol record kinds (cvinfo.h) and the subsection that carries them.
const int kSObjName = 0x1101, kSCompile3 = 0x113c, kSFrameProc = 0x1012;
const int kSGProc32 = 0x1110, kSLProc32 = 0x110f, kSEnd = 0x0006, kSBlock32 = 0x1103;
const int kSRegRel32 = 0x1111, kSLData32 = 0x110c, kSGData32 = 0x110d, kSUdt = 0x1108;
const int kDebugSSymbols = 0xf1, kCvSignatureC13 = 4, kMachineAmd64 = 0xd0, kRegRbp = 334;

// S_FRAMEPROC flags: locals and parameters both off RBP (encoded 2, bits 14-15 and 16-17).
const int kFrameFlags = (2 << 14) | (2 << 16);

// Type record kinds, numeric leaves, and the built-in indices that need no record.
const unsigned kLfPointer = 0x1002, kLfProcedure = 0x1008, kLfArgList = 0x1201;
const unsigned kLfFieldList = 0x1203, kLfBitField = 0x1205, kLfArray = 0x1503;
const unsigned kLfStructure = 0x1505, kLfUnion = 0x1506, kLfMember = 0x150d;
const unsigned kLfEnum = 0x1507, kLfEnumerate = 0x1502;
const unsigned kLfLong = 0x8003, kLfULong = 0x8004, kLfUQuad = 0x800a;
const unsigned kTVoid = 0x03, kTInt4 = 0x74, kTUQuad = 0x23, kT64Pointer = 0x0600, kFirstType = 0x1000;

// A 64-bit pointer (CV_PTR_64, 0x0c) of size 8; a declaration standing for a definition elsewhere.
const unsigned kPointer64 = 0x0c | (8 << 13), kForwardRef = 0x80, kPublic = 3;

void line(std::string &o, const std::string &text) { o += text; o += '\n'; }

void num(std::string &o, const char *dir, long long v) {
    o += dir;
    o += ' ';
    o += std::to_string(v);
    o += '\n';
}

void name(std::string &o, const std::string &v) {
    o += "  .asciz \"";
    for (char c : v) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    o += "\"\n";
}

// The index CodeView has built in for a basic type, or 0 for one that needs a record.
unsigned basicIndex(const Type *t) {
    switch (t->kind()) {
    case Kind::Void:       return kTVoid;
    case Kind::Char:       return 0x70;
    case Kind::SChar:      return 0x10;
    case Kind::UChar:      return 0x20;
    case Kind::Short:      return 0x11;
    case Kind::UShort:     return 0x21;
    case Kind::Int:        return 0x74;
    case Kind::UInt:       return 0x75;
    case Kind::Long:       return 0x12;
    case Kind::ULong:      return 0x22;
    case Kind::LongLong:   return 0x13;
    case Kind::ULongLong:  return 0x23;
    case Kind::Float:      return 0x40;
    case Kind::Double:     return 0x41;
    case Kind::LongDouble: return 0x41;
    default:               return 0;
    }
}

// **One type record as bytes**: its length (not counting the length field), its
// kind and its fields, padded to four with LF_PAD bytes (0xF3 0xF2 0xF1).
class Record {
public:
    explicit Record(unsigned kind) { u16(0); u16(kind); }

    void u8(unsigned v) { b_.push_back(static_cast<unsigned char>(v)); }
    void u16(unsigned v) { u8(v & 0xff); u8((v >> 8) & 0xff); }
    void u32(unsigned v) { u16(v & 0xffff); u16(v >> 16); }
    void text(const std::string &s) {
        for (char c : s) u8(static_cast<unsigned char>(c));
        u8(0);
    }
    // A numeric leaf: the value itself below 0x8000, else a leaf kind and the value.
    void numeric(unsigned long long v) {
        if (v < 0x8000) { u16(static_cast<unsigned>(v)); return; }
        if (v <= 0xffffffffULL) { u16(kLfULong); u32(static_cast<unsigned>(v)); return; }
        u16(kLfUQuad);
        u32(static_cast<unsigned>(v & 0xffffffffULL));
        u32(static_cast<unsigned>(v >> 32));
    }
    // A signed one: a negative value as LF_LONG, which holds any an enum constant can have.
    void numeric(long long v) {
        if (v >= 0) { numeric(static_cast<unsigned long long>(v)); return; }
        u16(kLfLong);
        u32(static_cast<unsigned>(v));
    }
    void pad() {
        while (b_.size() % 4 != 0) u8(0xf0 | (4 - b_.size() % 4));
    }
    const std::vector<unsigned char> &finish() {
        pad();
        b_[0] = static_cast<unsigned char>((b_.size() - 2) & 0xff);
        b_[1] = static_cast<unsigned char>((b_.size() - 2) >> 8);
        return b_;
    }

private:
    std::vector<unsigned char> b_;
};

// **The type records, each written after the ones it names**, as link.exe requires.
// A tagged struct or union is declared first (a forward reference) and every pointer
// names that, so a struct that points at itself needs no record before its own.
class Types {
public:
    Types(const Target &target, const std::vector<EnumType> &enums)
        : target_(target), enums_(enums), enumIndices_(enums.size(), 0) {}

    // A declaration's type: the enum it named in place of int, where it named one.
    unsigned declared(const Type *t, int enumType) {
        if (enumType < 0 || static_cast<std::size_t>(enumType) >= enums_.size() || t->kind() != Kind::Int)
            return index(t);
        return enumIndex(enumType);
    }

    unsigned index(const Type *t) {
        if (t == nullptr) return kTVoid;
        if (unsigned basic = basicIndex(t)) return basic;
        std::map<const Type *, unsigned>::iterator it = done_.find(t);
        if (it != done_.end()) return it->second;
        unsigned n = 0;
        if (t->isPointer()) n = pointer(t->pointee());
        else if (t->isArray()) n = array(t);
        else if (t->isFunction()) n = function(t);
        else if (t->isStructOrUnion()) n = aggregate(t);
        done_[t] = n;
        return n;
    }

    unsigned procedure(unsigned returns, std::vector<unsigned> args, bool variadic) {
        if (variadic) args.push_back(0);
        Record list(kLfArgList);
        list.u32(static_cast<unsigned>(args.size()));
        for (unsigned a : args) list.u32(a);
        const unsigned listIndex = add(list);
        Record r(kLfProcedure);
        r.u32(returns);
        r.u8(0);
        r.u8(0);
        r.u16(static_cast<unsigned>(args.size()));
        r.u32(listIndex);
        return add(r);
    }

    // The tags defined here, for S_UDT.
    const std::vector<std::pair<std::string, unsigned> > &tags() const { return tags_; }

    void write(std::string &o) const {
        if (records_.empty()) return;
        line(o, "  .section .debug$T,\"dr\"");
        line(o, "  .p2align 2");
        num(o, "  .long", kCvSignatureC13);
        for (const std::vector<unsigned char> &r : records_) {
            for (std::size_t i = 0; i < r.size(); i += 16) {
                o += "  .byte ";
                for (std::size_t k = i; k < r.size() && k < i + 16; k++) {
                    if (k != i) o += ", ";
                    o += std::to_string(static_cast<unsigned>(r[k]));
                }
                o += '\n';
            }
        }
    }

private:
    const Target &target_;
    const std::vector<EnumType> &enums_;
    std::vector<unsigned> enumIndices_;
    std::map<const Type *, unsigned> done_, forward_;
    std::vector<std::vector<unsigned char> > records_;
    std::vector<std::pair<std::string, unsigned> > tags_;

    unsigned add(Record &r) {
        records_.push_back(r.finish());
        return kFirstType + static_cast<unsigned>(records_.size()) - 1;
    }

    static std::string tagOf(const Type *t) {
        return t->tag().empty() ? std::string("<unnamed-tag>") : t->tag();
    }

    unsigned enumIndex(int n) {
        if (enumIndices_[n] != 0) return enumIndices_[n];
        const EnumType &e = enums_[n];
        Record fields(kLfFieldList);
        for (const std::pair<std::string, long long> &v : e.values) {
            fields.u16(kLfEnumerate);
            fields.u16(kPublic);
            fields.numeric(v.second);
            fields.text(v.first);
            fields.pad();
        }
        const unsigned fieldList = add(fields);
        Record r(kLfEnum);
        r.u16(static_cast<unsigned>(e.values.size()));
        r.u16(0);
        r.u32(kTInt4);
        r.u32(fieldList);
        r.text(e.tag.empty() ? std::string("<unnamed-tag>") : e.tag);
        enumIndices_[n] = add(r);
        if (!e.tag.empty()) tags_.push_back(std::make_pair(e.tag, enumIndices_[n]));
        return enumIndices_[n];
    }

    unsigned function(const Type *t) {
        std::vector<unsigned> args;
        for (const Type *p : t->params()) args.push_back(index(p));
        return procedure(index(t->returns()), args, t->isVariadicFn());
    }

    unsigned pointer(const Type *to) {
        if (unsigned basic = basicIndex(to)) return kT64Pointer | basic;
        Record r(kLfPointer);
        r.u32(to->isStructOrUnion() && !to->tag().empty() ? forward(to) : index(to));
        r.u32(kPointer64);
        return add(r);
    }

    unsigned array(const Type *t) {
        const unsigned element = index(t->pointee());
        Record r(kLfArray);
        r.u32(element);
        r.u32(kTUQuad);
        r.numeric(t->length() >= 0 ? static_cast<unsigned long long>(t->size(target_)) : 0);
        r.text("");
        return add(r);
    }

    unsigned forward(const Type *t) {
        std::map<const Type *, unsigned>::iterator it = forward_.find(t);
        if (it != forward_.end()) return it->second;
        const bool isUnion = t->kind() == Kind::Union;
        Record r(isUnion ? kLfUnion : kLfStructure);
        r.u16(0);
        r.u16(kForwardRef);
        r.u32(0);
        if (!isUnion) { r.u32(0); r.u32(0); }
        r.numeric(0ULL);
        r.text(tagOf(t));
        const unsigned n = add(r);
        forward_[t] = n;
        return n;
    }

    // A bit-field's member names its storage unit's offset; the record holds the bit within it.
    unsigned bitField(const Member &m) {
        const unsigned type = index(m.type);
        const int unitBits = m.type->size(target_) * 8;
        const int bits = m.offset * 8 + m.bitOffset;
        Record r(kLfBitField);
        r.u32(type);
        r.u8(static_cast<unsigned>(m.width));
        r.u8(static_cast<unsigned>(bits % unitBits));
        return add(r);
    }

    unsigned aggregate(const Type *t) {
        if (!t->tag().empty()) forward(t);
        if (!t->isComplete()) return forward(t);
        const std::vector<Member> &ms = t->members();
        std::vector<unsigned> types;
        std::vector<int> offsets;
        for (const Member &m : ms) {
            if (m.name.empty()) { types.push_back(0); offsets.push_back(0); continue; }
            const int unit = m.isBitField() ? m.type->size(target_) : 1;
            types.push_back(m.isBitField() ? bitField(m) : declared(m.type, m.enumType));
            offsets.push_back(m.isBitField() ? (m.offset * 8 + m.bitOffset) / (unit * 8) * unit
                                             : m.offset);
        }
        Record fields(kLfFieldList);
        unsigned count = 0;
        for (std::size_t i = 0; i < ms.size(); i++) {
            if (ms[i].name.empty()) continue;
            fields.u16(kLfMember);
            fields.u16(kPublic);
            fields.u32(types[i]);
            fields.numeric(static_cast<unsigned long long>(offsets[i]));
            fields.text(ms[i].name);
            fields.pad();
            count++;
        }
        const unsigned fieldList = add(fields);
        const bool isUnion = t->kind() == Kind::Union;
        Record r(isUnion ? kLfUnion : kLfStructure);
        r.u16(count);
        r.u16(0);
        r.u32(fieldList);
        if (!isUnion) { r.u32(0); r.u32(0); }
        r.numeric(static_cast<unsigned long long>(t->size(target_)));
        r.text(tagOf(t));
        const unsigned n = add(r);
        if (!t->tag().empty()) tags_.push_back(std::make_pair(t->tag(), n));
        return n;
    }
};

// **One symbol record, measured by its own labels.** A symbol record is its length
// (not counting the length field), its kind, its fields, and padding to four.
class Records {
public:
    explicit Records(std::string &o) : o_(o) {}

    void begin(int kind) {
        const std::string n = std::to_string(++count_);
        end_ = ".Lcv.r" + n + ".e";
        const std::string b = ".Lcv.r" + n + ".b";
        line(o_, "  .short " + end_ + "-" + b);
        line(o_, b + ":");
        num(o_, "  .short", kind);
    }
    void end() {
        line(o_, "  .p2align 2");
        line(o_, end_ + ":");
    }

    // A DEBUG_S_SYMBOLS subsection around the records written between the two.
    void openSubsection() {
        const std::string n = std::to_string(++count_);
        subEnd_ = ".Lcv.s" + n + ".e";
        const std::string b = ".Lcv.s" + n + ".b";
        num(o_, "  .long", kDebugSSymbols);
        line(o_, "  .long " + subEnd_ + "-" + b);
        line(o_, b + ":");
    }
    void closeSubsection() {
        line(o_, subEnd_ + ":");
        line(o_, "  .p2align 2");
    }

    std::string &out() { return o_; }

private:
    std::string &o_;
    std::string end_;
    std::string subEnd_;
    int count_ = 0;
};

void writeCompileUnit(Records &r, const std::string &objectName) {
    std::string &o = r.out();
    r.openSubsection();
    r.begin(kSObjName);
    num(o, "  .long", 0);
    name(o, objectName);
    r.end();

    // Language 0 is CV_CFL_C, which decides how cdb prints a name and evaluates an expression.
    r.begin(kSCompile3);
    num(o, "  .long", 0);
    num(o, "  .short", kMachineAmd64);
    for (int i = 0; i < 8; i++) num(o, "  .short", i % 4 == 0 ? 1 : 0);
    name(o, "c90");
    r.end();
    r.closeSubsection();
}

// S_LDATA32 and S_GDATA32 have one shape: the type, the address as section and offset, the name.
void writeData(Records &r, int kind, unsigned type, const std::string &symbol, const std::string &n) {
    std::string &o = r.out();
    r.begin(kind);
    num(o, "  .long", type);
    line(o, "  .secrel32 " + symbol);
    line(o, "  .secidx " + symbol);
    name(o, n);
    r.end();
}

void writeLocal(Records &r, const Local &l, Types &types) {
    if (!l.staticName.empty()) {
        writeData(r, kSLData32, types.declared(l.type, l.enumType), l.staticName, l.name);
        return;
    }
    std::string &o = r.out();
    r.begin(kSRegRel32);
    num(o, "  .long", -static_cast<long long>(l.offset));
    num(o, "  .long", types.declared(l.type, l.enumType));
    num(o, "  .short", kRegRbp);
    name(o, l.name);
    r.end();
}

bool declaresAnything(const DwarfFunction &f, int scope) {
    for (const Local &l : *f.locals)
        if (l.scope == scope) return true;
    for (std::size_t b = 1; b < f.blocks.size(); b++)
        if (f.blocks[b].parent == scope && declaresAnything(f, static_cast<int>(b))) return true;
    return false;
}

// **Parameters first and in order, then locals**: dbghelp takes the first records,
// as many as the procedure has arguments, for the parameters.
void writeScope(Records &r, const DwarfFunction &f, int scope, Types &types) {
    for (int pass = 0; pass < 2; pass++)
        for (const Local &l : *f.locals)
            if (l.scope == scope && l.isParam == (pass == 0)) writeLocal(r, l, types);

    for (std::size_t b = 1; b < f.blocks.size(); b++) {
        const DwarfBlock &block = f.blocks[b];
        const int id = static_cast<int>(b);
        if (block.parent != scope || !declaresAnything(f, id)) continue;
        if (block.begin.empty() || block.end.empty()) {
            writeScope(r, f, id, types);
            continue;
        }
        std::string &o = r.out();
        r.begin(kSBlock32);
        line(o, "  .long 0, 0");
        line(o, "  .long " + block.end + "-" + block.begin);
        line(o, "  .secrel32 " + block.begin);
        line(o, "  .secidx " + block.begin);
        name(o, "");
        r.end();
        writeScope(r, f, id, types);
        r.begin(kSEnd);
        r.end();
    }
}

void writeFunction(Records &r, const DwarfFunction &f, int id, Types &types) {
    std::vector<unsigned> params;
    for (const Local &l : *f.locals)
        if (l.isParam) params.push_back(types.declared(l.type, l.enumType));
    const unsigned type = types.procedure(types.index(f.returns), params, f.variadic);

    std::string &o = r.out();
    r.openSubsection();
    r.begin(f.external ? kSGProc32 : kSLProc32);
    line(o, "  .long 0, 0, 0");
    line(o, "  .long " + f.codeEnd + "-" + f.symbol);
    line(o, "  .long " + f.prologEnd + "-" + f.symbol);
    line(o, "  .long " + f.codeEnd + "-" + f.symbol);
    num(o, "  .long", type);
    line(o, "  .secrel32 " + f.symbol);
    line(o, "  .secidx " + f.symbol);
    num(o, "  .byte", 0);
    name(o, f.name);
    r.end();

    r.begin(kSFrameProc);
    num(o, "  .long", f.frameSize);
    line(o, "  .long 0, 0, 0, 0");
    num(o, "  .short", 0);
    num(o, "  .long", kFrameFlags);
    r.end();

    writeScope(r, f, 0, types);

    r.begin(kSEnd);
    r.end();
    r.closeSubsection();
    line(o, "  .cv_linetable " + std::to_string(id) + ", " + f.symbol + ", " + f.codeEnd);
}

}

std::string codeViewPath(const std::string &compDir, const std::string &name) {
    const bool absolute = (name.size() > 1 && name[1] == ':') ||
                          (!name.empty() && (name[0] == '/' || name[0] == '\\'));
    std::string full = absolute || compDir.empty() ? name : compDir + "\\" + name;
    std::string out;
    for (char c : full) {
        if (c == '/') c = '\\';
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out;
}

void writeCodeView(std::string &out, const Target &target, const std::string &objectName,
                   const std::vector<DwarfFunction> &fns,
                   const std::vector<DwarfGlobal> &globals,
                   const std::vector<EnumType> &enums) {
    if (fns.empty()) return;

    Types types(target, enums);
    Records r(out);
    line(out, "  .section .debug$S,\"dr\"");
    line(out, "  .p2align 2");
    num(out, "  .long", kCvSignatureC13);
    writeCompileUnit(r, objectName);
    for (std::size_t i = 0; i < fns.size(); i++) writeFunction(r, fns[i], static_cast<int>(i), types);

    // The globals' types first, so the tags they define are known for S_UDT.
    std::vector<unsigned> globalTypes;
    for (const DwarfGlobal &g : globals) globalTypes.push_back(types.declared(g.type, g.enumType));
    const bool any = !globals.empty() || !types.tags().empty();
    if (any) r.openSubsection();
    for (std::size_t i = 0; i < globals.size(); i++)
        writeData(r, globals[i].external ? kSGData32 : kSLData32, globalTypes[i],
                  globals[i].symbol, globals[i].name);
    for (const std::pair<std::string, unsigned> &tag : types.tags()) {
        r.begin(kSUdt);
        num(out, "  .long", tag.second);
        name(out, tag.first);
        r.end();
    }
    if (any) r.closeSubsection();
    line(out, "  .cv_filechecksums");
    line(out, "  .cv_stringtable");

    types.write(out);
}
