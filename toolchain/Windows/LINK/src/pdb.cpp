/*  pdb.cpp - the program database /DEBUG writes beside the image.
 *
 *  A PDB is an MSF file: blocks of 4096 bytes, and a directory saying which blocks each
 *  stream is made of. LLVM's PDB documentation (llvm.org/docs/PDB) and lld's writer are
 *  the map this follows; llvm-pdbutil and dbghelp are what it was held against. Streams:
 *
 *    0   the previous directory - empty
 *    1   PDB info: version, signature, age and GUID - the GUID and age the image's RSDS
 *        record names, which is how a debugger knows the PDB is the image's - and the named
 *        streams (/names)
 *    2   TPI, the types, and 4 IPI, the ids: every object's .debug$T merged, each record once
 *    3   DBI: a module per object (and per archive member) with the stream of its symbols
 *        and line tables, the section contributions, the section map, each module's source
 *        files, and the optional debug streams - the image's section headers
 *    5.. each module's stream; the globals (S_PROCREF per procedure) and the publics
 *        (S_PUB32 per external, with the address map), the record stream both index, the
 *        section headers, /names - the file names the line tables use
 *
 *  A module's CodeView is its objects' .debug$S, relocated - SECREL and SECTION become the
 *  output section's number and the offset in it - and laid out as a module stream: the
 *  symbols (the scopes' parent and end offsets filled in), the line tables as they were,
 *  and the file table with its names moved into /names. A .debug$S that goes with a COMDAT
 *  the link dropped is dropped with it; a record or a line table that names something not
 *  in the image is left out.
 */
#include "link.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace {

typedef std::vector<u8> Bytes;

void put8(Bytes &b, u32 v) { b.push_back((u8)v); }
void put16(Bytes &b, u32 v) { put8(b, v & 0xFF); put8(b, (v >> 8) & 0xFF); }
void put32(Bytes &b, u32 v) { put16(b, v & 0xFFFF); put16(b, v >> 16); }
void text(Bytes &b, const std::string &s) { b.insert(b.end(), s.begin(), s.end()); put8(b, 0); }
void pad4(Bytes &b) { while (b.size() % 4) put8(b, 0); }
void append(Bytes &b, const Bytes &x) { b.insert(b.end(), x.begin(), x.end()); }

/* the two hashes the PDB's tables are keyed by (LLVM's hashStringV1, the reference's
   Hasher::lhashPbCb) - a reader finds a name with the same function, so they are exact */
u32 hash_v1(const std::string &s)
{
    u32 r = 0;
    const size_t n = s.size();
    const u8 *p = (const u8 *)s.data();
    size_t i = 0;
    for (; i + 4 <= n; i += 4) r ^= rd32(p + i);
    if (n - i >= 2) { r ^= rd16(p + i); i += 2; }
    if (n - i == 1) r ^= p[i];
    r |= 0x20202020u;
    r ^= r >> 11;
    return r ^ (r >> 16);
}

/* ------------------------------------------------------------------ MSF */

const u32 kBlock = 4096;

/*  The container. Blocks 1 and 2 hold the two free-block maps, and so does every 4096th
 *  block after them (4096k + 1, 4096k + 2): a stream's blocks step round those. */
struct Msf {
    std::vector<Bytes> streams;

    u32 add() { streams.push_back(Bytes()); return (u32)streams.size() - 1; }
    u32 add(const Bytes &b) { streams.push_back(b); return (u32)streams.size() - 1; }

    static bool fpm(u32 block) { u32 r = block % kBlock; return r == 1 || r == 2; }

    bool write(const std::string &path, std::string &err) const
    {
        u32 next = 3;
        std::vector<std::vector<u32> > blocks(streams.size());
        for (size_t i = 0; i < streams.size(); i++)
            for (u32 n = 0; n * kBlock < streams[i].size(); n++) {
                while (fpm(next)) next++;
                blocks[i].push_back(next++);
            }
        Bytes dir;
        put32(dir, (u32)streams.size());
        for (size_t i = 0; i < streams.size(); i++) put32(dir, (u32)streams[i].size());
        for (size_t i = 0; i < streams.size(); i++)
            for (size_t k = 0; k < blocks[i].size(); k++) put32(dir, blocks[i][k]);
        std::vector<u32> dirblocks;
        for (u32 n = 0; n * kBlock < dir.size(); n++) {
            while (fpm(next)) next++;
            dirblocks.push_back(next++);
        }
        if (dirblocks.size() * 4 > kBlock) { err = "the PDB's directory does not fit its block map"; return false; }
        while (fpm(next)) next++;
        const u32 mapblock = next++;
        const u32 nblocks = next;

        std::vector<u8> f((size_t)nblocks * kBlock, 0);
        static const char magic[] = "Microsoft C/C++ MSF 7.00\r\n\x1a" "DS\0\0";
        memcpy(&f[0], magic, 32);
        wr32(&f[32], kBlock);
        wr32(&f[36], 1);                      /* the free-block map in use */
        wr32(&f[40], nblocks);
        wr32(&f[44], (u32)dir.size());
        wr32(&f[48], 0);
        wr32(&f[52], mapblock);
        for (size_t i = 0; i < streams.size(); i++)
            for (size_t k = 0; k < blocks[i].size(); k++) {
                size_t from = k * kBlock, n = std::min((size_t)kBlock, streams[i].size() - from);
                memcpy(&f[(size_t)blocks[i][k] * kBlock], &streams[i][from], n);
            }
        for (size_t k = 0; k < dirblocks.size(); k++) {
            size_t from = k * kBlock, n = std::min((size_t)kBlock, dir.size() - from);
            memcpy(&f[(size_t)dirblocks[k] * kBlock], &dir[from], n);
            wr32(&f[(size_t)mapblock * kBlock + k * 4], dirblocks[k]);
        }
        /*  The map, a bit per block, set when free: every block is in use, and the bits past
         *  the last block say free. It runs across the map blocks 4096k + 1 (and the same into
         *  the second map, 4096k + 2). */
        for (u32 k = 0; k * kBlock < nblocks; k++)
            for (u32 which = 1; which <= 2; which++) {
                const u32 b = k * kBlock + which;
                if (b >= nblocks) break;
                for (u32 i = 0; i < kBlock; i++) {
                    const u64 first = ((u64)k * kBlock + i) * 8;     /* the block this byte's bit 0 is */
                    u8 v = 0xFF;
                    for (u32 bit = 0; bit < 8; bit++)
                        if (first + bit < nblocks) v = (u8)(v & ~(1u << bit));
                    f[(size_t)b * kBlock + i] = v;
                }
            }
        FILE *out = fopen(path.c_str(), "wb");
        if (!out) { err = path + ": cannot create"; return false; }
        if (fwrite(&f[0], 1, f.size(), out) != f.size()) { fclose(out); err = path + ": short write"; return false; }
        fclose(out);
        return true;
    }
};

/* -------------------------------------------------------------- tables */

/*  /names: the file names the line tables point at by offset, and a hash of them for a
 *  reader that looks one up by name. Buckets grow as the reference grows them. */
struct Names {
    Bytes buf;
    std::map<std::string, u32> at;
    Names() { put8(buf, 0); }
    u32 add(const std::string &s)
    {
        if (s.empty()) return 0;
        std::map<std::string, u32>::const_iterator it = at.find(s);
        if (it != at.end()) return it->second;
        u32 off = (u32)buf.size();
        text(buf, s);
        at[s] = off;
        return off;
    }
    Bytes stream() const
    {
        u32 buckets = 1, count = 0;
        for (size_t i = 0; i < at.size(); i++) {
            count++;
            if (buckets * 3 / 4 < count) buckets = buckets * 3 / 2 + 1;
        }
        std::vector<u32> table(buckets, 0);
        for (std::map<std::string, u32>::const_iterator it = at.begin(); it != at.end(); ++it) {
            u32 h = hash_v1(it->first);
            for (u32 k = 0; k < buckets; k++) {
                u32 slot = (h + k) % buckets;
                if (table[slot]) continue;
                table[slot] = it->second;
                break;
            }
        }
        Bytes b;
        put32(b, 0xEFFEEFFEu);
        put32(b, 1);                          /* hash version 1 */
        put32(b, (u32)buf.size());
        append(b, buf);
        put32(b, buckets);
        for (size_t i = 0; i < table.size(); i++) put32(b, table[i]);
        put32(b, (u32)at.size());
        return b;
    }
};

/*  The globals' and the publics' hash: 4096 buckets by hash_v1 of the name, each bucket's
 *  records by name - shorter first, then without regard to case - as lld orders them. */
struct Gsi {
    std::vector<std::pair<std::string, u32> > recs;    /* name, offset in the record stream */

    static bool before(const std::pair<std::string, u32> &a, const std::pair<std::string, u32> &b)
    {
        if (a.first.size() != b.first.size()) return a.first.size() < b.first.size();
        for (size_t i = 0; i < a.first.size(); i++) {
            int x = (unsigned char)a.first[i], y = (unsigned char)b.first[i];
            if (x < 128 && y < 128) { if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; }
            if (x != y) return x < y;
        }
        return a.second < b.second;
    }
    Bytes stream() const
    {
        const u32 nb = 4096;
        std::vector<std::vector<std::pair<std::string, u32> > > bucket(nb);
        for (size_t i = 0; i < recs.size(); i++) bucket[hash_v1(recs[i].first) % nb].push_back(recs[i]);
        Bytes hr, bitmap((size_t)((nb + 1 + 31) / 32) * 4, 0), offs;
        u32 n = 0;
        for (u32 b = 0; b < nb; b++) {
            if (bucket[b].empty()) continue;
            std::sort(bucket[b].begin(), bucket[b].end(), before);
            bitmap[b / 8] = (u8)(bitmap[b / 8] | (1u << (b % 8)));
            put32(offs, n * 12);              /* the reference's in-memory record size */
            for (size_t k = 0; k < bucket[b].size(); k++, n++) {
                put32(hr, bucket[b][k].second + 1);
                put32(hr, 1);
            }
        }
        Bytes s;
        put32(s, 0xFFFFFFFFu);
        put32(s, 0xEFFE0000u + 19990810u);
        put32(s, (u32)hr.size());
        put32(s, (u32)(bitmap.size() + offs.size()));
        append(s, hr);
        append(s, bitmap);
        append(s, offs);
        return s;
    }
};

/* a symbol record: its length (the kind and the body, padded to four), its kind, its body */
void record(Bytes &out, u16 kind, const Bytes &body)
{
    Bytes r = body;
    pad4(r);
    put16(out, (u32)r.size() + 2);
    put16(out, kind);
    append(out, r);
}

/* ------------------------------------------------------------ types */

/*  An object's .debug$T is its types and ids in one sequence, numbered from 0x1000 in order.
 *  The PDB keeps types in TPI and ids (LF_FUNC_ID and the other 0x16xx kinds) in IPI, each
 *  numbered from 0x1000 again; so every record is copied with each index it holds rewritten to
 *  where the record it names went, and a record already there - the same bytes after the
 *  rewriting, as two objects' `int *` are - is not added twice. */

enum {
    LF_VTSHAPE = 0x000A, LF_LABEL = 0x000E, LF_MODIFIER = 0x1001, LF_POINTER = 0x1002, LF_PROCEDURE = 0x1008,
    LF_MFUNCTION = 0x1009, LF_ARGLIST = 0x1201, LF_FIELDLIST = 0x1203, LF_BITFIELD = 0x1205,
    LF_METHODLIST = 0x1206, LF_BCLASS = 0x1400, LF_VBCLASS = 0x1401, LF_IVBCLASS = 0x1402, LF_INDEX = 0x1404,
    LF_VFUNCTAB = 0x1409, LF_ENUMERATE = 0x1502, LF_ARRAY = 0x1503, LF_CLASS = 0x1504, LF_STRUCTURE = 0x1505,
    LF_UNION = 0x1506, LF_ENUM = 0x1507, LF_PRECOMP = 0x1509, LF_ALIAS = 0x150A, LF_MEMBER = 0x150D,
    LF_STMEMBER = 0x150E, LF_METHOD = 0x150F, LF_NESTTYPE = 0x1510, LF_ONEMETHOD = 0x1511,
    LF_TYPESERVER2 = 0x1515, LF_INTERFACE = 0x1519, LF_VFTABLE = 0x151D, LF_FUNC_ID = 0x1601,
    LF_MFUNC_ID = 0x1602, LF_BUILDINFO = 0x1603, LF_SUBSTR_LIST = 0x1604, LF_STRING_ID = 0x1605,
    LF_UDT_SRC_LINE = 0x1606, LF_UDT_MOD_SRC_LINE = 0x1607
};

bool is_id(u16 kind) { return kind >= LF_FUNC_ID && kind <= LF_UDT_MOD_SRC_LINE; }

/* a numeric leaf's length in bytes: the value itself below 0x8000, else its kind and the value */
size_t numeric_size(const u8 *p, size_t n)
{
    if (n < 2) return 0;
    const u16 v = rd16(p);
    if (v < 0x8000) return 2;
    switch (v) {
    case 0x8000: return 3;
    case 0x8001: case 0x8002: return 4;
    case 0x8003: case 0x8004: case 0x8005: return 6;
    case 0x8006: case 0x8009: case 0x800A: case 0x800C: case 0x801A: return 10;
    case 0x8007: return 12;
    case 0x800B: return 8;
    case 0x8008: case 0x800D: case 0x8017: case 0x8018: case 0x8019: return 18;
    case 0x800E: return 22;
    case 0x800F: return 34;
    case 0x8010: return n >= 4 ? 4 + rd16(p + 2) : 0;
    default: return 0;
    }
}

size_t name_size(const u8 *p, size_t n)
{
    size_t k = 0;
    while (k < n && p[k]) k++;
    return k < n ? k + 1 : 0;
}

/*  Where the type and id indices of one record are: offsets in its data (the bytes after the
 *  kind). False for a kind this linker cannot take apart, which leaves the object's types out. */
bool type_refs(u16 kind, const u8 *d, size_t n, std::vector<u32> &at)
{
    at.clear();
    switch (kind) {
    case LF_VTSHAPE: case LF_LABEL: return true;
    case LF_MODIFIER: case LF_BITFIELD: case LF_ALIAS: case LF_STRING_ID: at.push_back(0); return true;
    case LF_POINTER: {
        at.push_back(0);
        if (n >= 8) { u32 mode = (rd32(d + 4) >> 5) & 7; if (mode == 2 || mode == 3) at.push_back(8); }
        return true;
    }
    case LF_PROCEDURE: at.push_back(0); at.push_back(8); return true;
    case LF_MFUNCTION: at.push_back(0); at.push_back(4); at.push_back(8); at.push_back(16); return true;
    case LF_ARGLIST: case LF_SUBSTR_LIST: {
        if (n < 4) return false;
        const u32 count = rd32(d);
        for (u32 i = 0; i < count; i++) at.push_back(4 + 4 * i);
        return 4 + 4 * (size_t)count <= n;
    }
    case LF_BUILDINFO: {
        if (n < 2) return false;
        const u32 count = rd16(d);
        for (u32 i = 0; i < count; i++) at.push_back(2 + 4 * i);
        return 2 + 4 * (size_t)count <= n;
    }
    case LF_ARRAY: case LF_FUNC_ID: case LF_MFUNC_ID: case LF_UDT_SRC_LINE: case LF_VFTABLE:
        at.push_back(0); at.push_back(4); return true;
    case LF_CLASS: case LF_STRUCTURE: case LF_INTERFACE: at.push_back(4); at.push_back(8); at.push_back(12); return true;
    case LF_UNION: at.push_back(4); return true;
    case LF_ENUM: at.push_back(4); at.push_back(8); return true;
    case LF_UDT_MOD_SRC_LINE: at.push_back(0); return true;
    case LF_METHODLIST: {
        for (size_t k = 0; k + 8 <= n; ) {
            const u32 prop = (rd16(d + k) >> 2) & 7;
            at.push_back((u32)k + 4);
            k += 8 + ((prop == 4 || prop == 6) ? 4 : 0);
        }
        return true;
    }
    case LF_FIELDLIST:
        for (size_t k = 0; k < n; ) {
            if (d[k] >= 0xF0) { k++; continue; }           /* LF_PAD */
            if (k + 2 > n) return false;
            const u16 leaf = rd16(d + k);
            const u8 *m = d + k + 2;
            const size_t left = n - k - 2;
            size_t len = 0, s = 0;
            switch (leaf) {
            case LF_MEMBER:
                at.push_back((u32)k + 4);
                s = numeric_size(m + 6, left > 6 ? left - 6 : 0);
                len = s ? 6 + s + name_size(m + 6 + s, left - 6 - s) : 0;
                if (len == 6 + s) len = 0;
                break;
            case LF_STMEMBER: case LF_NESTTYPE:
                at.push_back((u32)k + 4);
                len = left > 6 ? 6 + name_size(m + 6, left - 6) : 0;
                if (len == 6) len = 0;
                break;
            case LF_ENUMERATE:
                s = numeric_size(m + 2, left > 2 ? left - 2 : 0);
                len = s ? 2 + s + name_size(m + 2 + s, left - 2 - s) : 0;
                if (len == 2 + s) len = 0;
                break;
            case LF_BCLASS:
                at.push_back((u32)k + 4);
                s = numeric_size(m + 6, left > 6 ? left - 6 : 0);
                len = s ? 6 + s : 0;
                break;
            case LF_VBCLASS: case LF_IVBCLASS: {
                at.push_back((u32)k + 4); at.push_back((u32)k + 8);
                s = numeric_size(m + 10, left > 10 ? left - 10 : 0);
                size_t s2 = s ? numeric_size(m + 10 + s, left - 10 - s) : 0;
                len = s && s2 ? 10 + s + s2 : 0;
                break;
            }
            case LF_VFUNCTAB: case LF_INDEX:
                at.push_back((u32)k + 4);
                len = 6;
                break;
            case LF_ONEMETHOD: {
                at.push_back((u32)k + 4);
                const u32 prop = left >= 2 ? (rd16(m) >> 2) & 7 : 0;
                const size_t fixed = 6 + ((prop == 4 || prop == 6) ? 4 : 0);
                len = left > fixed ? fixed + name_size(m + fixed, left - fixed) : 0;
                if (len == fixed) len = 0;
                break;
            }
            case LF_METHOD:
                at.push_back((u32)k + 4);
                len = left > 6 ? 6 + name_size(m + 6, left - 6) : 0;
                if (len == 6) len = 0;
                break;
            default:
                return false;
            }
            if (len == 0 || len > left) return false;
            k += 2 + len;
        }
        return true;
    default:
        return false;
    }
}

/* CRC-32 as the reference hashes a record by its bytes (LLVM's JamCRC from 0) */
u32 jam_crc(const u8 *p, size_t n)
{
    static u32 table[256];
    static bool made = false;
    if (!made) {
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        made = true;
    }
    u32 crc = 0;
    for (size_t i = 0; i < n; i++) crc = (crc >> 8) ^ table[(crc ^ p[i]) & 0xFF];
    return crc;
}

/*  A record's hash, as a reader computes it to find a type by name: a structure, class, union
 *  or enum that is not a forward reference by its name (or its unique name), every other record
 *  by its bytes. A debugger resolves a forward reference - every pointer to a struct names one -
 *  by looking the definition's name up through these. */
u32 type_hash(const Bytes &rec)
{
    const u16 kind = rd16(&rec[2]);
    const u8 *d = rec.data() + 4;
    const size_t n = rec.size() - 4;
    if (kind == LF_CLASS || kind == LF_STRUCTURE || kind == LF_INTERFACE || kind == LF_UNION || kind == LF_ENUM) {
        const u16 prop = n >= 4 ? rd16(d + 2) : 0;
        size_t at = kind == LF_UNION ? 8 : kind == LF_ENUM ? 12 : 16;
        if (kind != LF_ENUM) at += numeric_size(d + at, n > at ? n - at : 0);
        if (at < n) {
            const size_t nl = name_size(d + at, n - at);
            const std::string name = nl ? std::string((const char *)d + at, nl - 1) : std::string();
            const std::string unique = nl && at + nl < n && name_size(d + at + nl, n - at - nl)
                                       ? std::string((const char *)d + at + nl) : std::string();
            const bool forward = (prop & 0x80) != 0, scoped = (prop & 0x100) != 0, has_unique = (prop & 0x200) != 0;
            const bool anon = name == "<unnamed-tag>" || name == "__unnamed" ||
                              (name.size() >= 15 && name.compare(name.size() - 15, 15, "::<unnamed-tag>") == 0) ||
                              (name.size() >= 11 && name.compare(name.size() - 11, 11, "::__unnamed") == 0);
            if (!forward && !scoped && !anon) return hash_v1(name);
            if (!forward && has_unique && !anon) return hash_v1(unique);
        }
    } else if (kind == LF_UDT_SRC_LINE || kind == LF_UDT_MOD_SRC_LINE) {
        return hash_v1(std::string((const char *)d, 4));
    }
    return jam_crc(&rec[0], rec.size());
}

/* TPI or IPI: the records, each once, and the index each has */
struct TypeTable {
    std::vector<Bytes> recs;
    std::map<Bytes, u32> index;

    u32 add(const Bytes &rec)
    {
        std::map<Bytes, u32>::const_iterator it = index.find(rec);
        if (it != index.end()) return it->second;
        const u32 ti = 0x1000 + (u32)recs.size();
        recs.push_back(rec);
        index[rec] = ti;
        return ti;
    }
    const Bytes *at(u32 ti) const { return ti >= 0x1000 && ti - 0x1000 < recs.size() ? &recs[ti - 0x1000] : 0; }

    /* the stream, its header naming `hash` as the stream of its hashes */
    Bytes stream(u32 hash) const
    {
        Bytes body;
        for (size_t i = 0; i < recs.size(); i++) append(body, recs[i]);
        u32 hashes = (u32)recs.size() * 4, offsets = 0;
        for (size_t i = 0, bytes = 0; i < recs.size(); bytes += recs[i].size(), i++)
            if (i == 0 || (bytes + recs[i].size()) / 8192 > bytes / 8192) offsets += 8;
        Bytes b;
        put32(b, 20040203);
        put32(b, 56);
        put32(b, 0x1000);
        put32(b, 0x1000 + (u32)recs.size());
        put32(b, (u32)body.size());
        put16(b, hash);
        put16(b, 0xFFFF);
        put32(b, 4);
        put32(b, 0x3FFFF);
        put32(b, 0); put32(b, hashes);            /* the hash values, then the index offsets, no adjusters */
        put32(b, hashes); put32(b, offsets);
        put32(b, hashes + offsets); put32(b, 0);
        append(b, body);
        return b;
    }
    Bytes hash_stream() const
    {
        Bytes b;
        for (size_t i = 0; i < recs.size(); i++) put32(b, type_hash(recs[i]) % 0x3FFFF);
        for (size_t i = 0, bytes = 0; i < recs.size(); bytes += recs[i].size(), i++)
            if (i == 0 || (bytes + recs[i].size()) / 8192 > bytes / 8192) { put32(b, 0x1000 + (u32)i); put32(b, (u32)bytes); }
        return b;
    }
};

/* ---------------------------------------------------------- symbols */

enum {
    S_END = 0x0006, S_FRAMEPROC = 0x1012, S_OBJNAME = 0x1101, S_THUNK32 = 0x1102, S_BLOCK32 = 0x1103,
    S_LABEL32 = 0x1105, S_REGISTER = 0x1106, S_CONSTANT = 0x1107, S_UDT = 0x1108, S_BPREL32 = 0x110B,
    S_LDATA32 = 0x110C, S_GDATA32 = 0x110D, S_PUB32 = 0x110E, S_LPROC32 = 0x110F, S_GPROC32 = 0x1110,
    S_REGREL32 = 0x1111, S_LTHREAD32 = 0x1112, S_GTHREAD32 = 0x1113, S_PROCREF = 0x1125,
    S_LPROCREF = 0x1127, S_SEPCODE = 0x1132, S_SECTION = 0x1136, S_COMPILE2 = 0x1116,
    S_FRAMECOOKIE = 0x113A, S_COMPILE3 = 0x113C, S_ENVBLOCK = 0x113D, S_LOCAL = 0x113E,
    S_DEFRANGE_REGISTER = 0x1141, S_DEFRANGE_FRAMEPOINTER_REL = 0x1142, S_DEFRANGE_SUBFIELD_REGISTER = 0x1143,
    S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE = 0x1144, S_DEFRANGE_REGISTER_REL = 0x1145,
    S_LPROC32_ID = 0x1146, S_GPROC32_ID = 0x1147, S_BUILDINFO = 0x114C, S_INLINESITE = 0x114D,
    S_INLINESITE_END = 0x114E, S_PROC_ID_END = 0x114F, S_FILESTATIC = 0x1153, S_CALLSITEINFO = 0x1139,
    S_HEAPALLOCSITE = 0x115E, S_UNAMESPACE = 0x1124, S_TRAMPOLINE = 0x112C, S_ANNOTATION = 0x1019
};

enum { DEBUG_S_SYMBOLS = 0xF1, DEBUG_S_LINES = 0xF2, DEBUG_S_STRINGTABLE = 0xF3, DEBUG_S_FILECHKSMS = 0xF4 };

/*  Where a type or id index sits in a symbol's body, for the kinds that carry one: it is
 *  rewritten to the merged TPI's (or IPI's) index. A record kind not in this list, and not
 *  in `plain`, is not carried at all. */
int type_field(u16 kind)
{
    switch (kind) {
    case S_GPROC32: case S_LPROC32: case S_GPROC32_ID: case S_LPROC32_ID: return 24;
    case S_REGREL32: case S_BPREL32: return 4;
    case S_LDATA32: case S_GDATA32: case S_LTHREAD32: case S_GTHREAD32: case S_UDT: case S_CONSTANT:
    case S_LOCAL: case S_REGISTER: case S_FILESTATIC: case S_BUILDINFO: return 0;
    case S_CALLSITEINFO: case S_HEAPALLOCSITE: return 8;
    default: return -1;
    }
}

bool plain(u16 kind)
{
    switch (kind) {
    case S_END: case S_FRAMEPROC: case S_OBJNAME: case S_THUNK32: case S_BLOCK32: case S_LABEL32:
    case S_COMPILE2: case S_COMPILE3: case S_ENVBLOCK: case S_FRAMECOOKIE: case S_SEPCODE:
    case S_DEFRANGE_REGISTER: case S_DEFRANGE_FRAMEPOINTER_REL: case S_DEFRANGE_SUBFIELD_REGISTER:
    case S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE: case S_DEFRANGE_REGISTER_REL: case S_PROC_ID_END:
    case S_UNAMESPACE: case S_TRAMPOLINE: case S_ANNOTATION: case S_SECTION:
        return true;
    default:
        return false;
    }
}

bool opens_scope(u16 kind)
{
    return kind == S_GPROC32 || kind == S_LPROC32 || kind == S_GPROC32_ID || kind == S_LPROC32_ID ||
           kind == S_BLOCK32 || kind == S_THUNK32 || kind == S_SEPCODE || kind == S_INLINESITE;
}

bool closes_scope(u16 kind) { return kind == S_END || kind == S_PROC_ID_END || kind == S_INLINESITE_END; }

/* the name a record ends in, for the kinds the globals and publics are made of */
std::string record_name(u16 kind, const u8 *body, size_t len)
{
    size_t at = 0;
    switch (kind) {
    case S_GPROC32: case S_LPROC32: at = 35; break;
    case S_GDATA32: case S_LDATA32: case S_GTHREAD32: case S_LTHREAD32: at = 10; break;
    case S_UDT: at = 4; break;
    default: return std::string();
    }
    if (at >= len) return std::string();
    size_t n = 0;
    while (at + n < len && body[at + n]) n++;
    return std::string((const char *)body + at, n);
}

struct DbiModule {
    std::string module, object;    /* ModuleName and ObjFileName */
    Bytes syms;                    /* the stream's symbols, from the signature on */
    Bytes c13;                     /* its line tables and file table */
    std::vector<std::string> files;
    int stream;
    /* its first contribution, the one the module record carries */
    u16 sc_sec; u32 sc_off, sc_size, sc_flags;
    DbiModule() : stream(-1), sc_sec(0xFFFF), sc_off(0), sc_size(0), sc_flags(0) {}
};

struct Global {
    u16 kind;
    std::string name;
    Bytes body;
};

/*  One link's PDB, built from the link's modules. */
struct Builder {
    Link &lk;
    Names names;
    std::vector<DbiModule> mods;
    std::vector<int> dbi_of;              /* the DBI module of each of the link's modules */
    std::vector<Global> globals;
    std::set<std::string> udts;
    TypeTable tpi, ipi;
    std::vector<u32> tmap;                /* the module being read: its type indices' new ones */

    explicit Builder(Link &l) : lk(l) {}

    /* an index of the module's own, as the merged TPI or IPI numbers it; 0 when it has none */
    u32 remap(u32 v) const
    {
        if (v < 0x1000) return v;
        return v - 0x1000 < tmap.size() ? tmap[v - 0x1000] : 0;
    }

    /*  The module's .debug$T into TPI and IPI, its indices' new ones into tmap. An object whose
     *  types are in a type server or a precompiled header's object (LF_TYPESERVER2, LF_PRECOMP),
     *  or hold a kind this linker cannot take apart, keeps none: its indices become T_NOTYPE. */
    void merge_types(const Module &m)
    {
        tmap.clear();
        std::vector<u32> refs;
        for (size_t si = 0; si < m.secs.size(); si++) {
            const Contrib &c = m.secs[si];
            if (c.name != ".debug$T" || c.data.size() < 4 || rd32(&c.data[0]) != 4) continue;
            const u8 *p = &c.data[0];
            const size_t n = c.data.size();
            for (size_t at = 4; at + 4 <= n; ) {
                const u32 len = rd16(p + at);
                if (len < 2 || at + 2 + len > n) break;
                const u16 kind = rd16(p + at + 2);
                Bytes rec(p + at, p + at + 2 + len);
                if (kind == LF_TYPESERVER2 || kind == LF_PRECOMP || !type_refs(kind, rec.data() + 4, rec.size() - 4, refs)) {
                    tmap.clear();
                    return;
                }
                for (size_t r = 0; r < refs.size(); r++)
                    if (refs[r] + 8 <= rec.size()) wr32(&rec[4 + refs[r]], remap(rd32(&rec[4 + refs[r]])));
                while (rec.size() % 4) rec.push_back((u8)(0xF0 | (4 - rec.size() % 4)));
                wr16(&rec[0], (u16)(rec.size() - 2));
                tmap.push_back((is_id(kind) ? ipi : tpi).add(rec));
                at += 2 + len;
            }
        }
    }

    /* a symbol's type or id field rewritten; an _ID procedure's id becomes its function's type */
    void retype(u16 kind, u8 *field)
    {
        u32 v = remap(rd32(field));
        if (kind == S_GPROC32_ID || kind == S_LPROC32_ID) {
            const Bytes *id = ipi.at(v);
            const u16 k = id ? rd16(&(*id)[2]) : 0;
            v = (k == LF_FUNC_ID || k == LF_MFUNC_ID) && id->size() >= 12 ? rd32(&(*id)[8]) : 0;
        }
        wr32(field, v);
    }

    /* where a .debug$S relocation's symbol is in the image: its output section (1-based) and
       the offset in it; false when it is in nothing placed */
    bool address(int mi, u32 si, u16 &seg, u32 &off, u32 &rva)
    {
        const Module &m = lk.mods[(size_t)mi];
        if (si >= m.syms.size()) return false;
        const Symbol &s = m.syms[si];
        int dm = mi, ds = (int)si;
        if (s.section == 0 || (s.section > 0 && (size_t)s.section <= m.secs.size() && m.secs[(size_t)s.section - 1].dropped)) {
            if (s.section > 0 && s.storage != SYM_EXTERNAL) return false;
            std::map<std::string, std::pair<int, int> >::const_iterator it = lk.resolved.find(s.name);
            if (it == lk.resolved.end()) return false;
            dm = it->second.first;
            ds = it->second.second;
        }
        const Symbol &t = lk.mods[(size_t)dm].syms[(size_t)ds];
        if (t.section <= 0 || (size_t)t.section > lk.mods[(size_t)dm].secs.size()) return false;
        int fm = dm, fs = t.section - 1;
        lk.fold_rep(fm, fs);
        const Contrib &c = lk.mods[(size_t)fm].secs[(size_t)fs];
        if (c.dropped || c.out < 0) return false;
        seg = (u16)(c.out + 1);
        rva = c.rva + t.value;
        off = rva - lk.outs[(size_t)c.out].rva;
        return true;
    }

    /* one .debug$S relocated: SECREL and SECTION written in place; `dead` holds the
       offsets of the fields whose symbol is in nothing placed */
    void relocate(int mi, const Contrib &c, Bytes &d, std::set<u32> &dead)
    {
        for (size_t r = 0; r < c.relocs.size(); r++) {
            const Reloc &rl = c.relocs[r];
            u16 seg = 0; u32 off = 0, rva = 0;
            const bool ok = address(mi, rl.sym, seg, off, rva);
            if (rl.type == REL_SECREL && rl.offset + 4 <= d.size()) {
                if (ok) wr32(&d[rl.offset], rd32(&d[rl.offset]) + off); else dead.insert(rl.offset);
            } else if (rl.type == REL_SECTION && rl.offset + 2 <= d.size()) {
                if (ok) wr16(&d[rl.offset], seg); else { wr16(&d[rl.offset], 0); dead.insert(rl.offset); }
            } else if (rl.type == REL_ADDR32NB && rl.offset + 4 <= d.size()) {
                if (ok) wr32(&d[rl.offset], rd32(&d[rl.offset]) + rva); else dead.insert(rl.offset);
            } else if (rl.offset < d.size()) {
                dead.insert(rl.offset);
            }
        }
    }

    static bool dead_in(const std::set<u32> &dead, u32 from, u32 to)
    {
        std::set<u32>::const_iterator it = dead.lower_bound(from);
        return it != dead.end() && *it < to;
    }

    /*  The symbols of one DEBUG_S_SYMBOLS subsection into the module's stream. The scope
     *  records' parent and end offsets are the stream's; an _ID procedure becomes a plain
     *  one, its end S_END, as link.exe writes them. A scope whose record names nothing
     *  placed is left out whole. */
    void symbols(DbiModule &dm, int dbi, const u8 *p, u32 base, u32 len, const std::set<u32> &dead,
                 std::vector<size_t> &scopes, int &skipping)
    {
        u32 at = 0;
        while (at + 4 <= len) {
            const u32 reclen = rd16(p + at);
            if (reclen < 2 || at + 2 + reclen > len) break;
            const u16 kind = rd16(p + at + 2);
            const u8 *body = p + at + 4;
            const u32 blen = reclen - 2;
            const u32 here = at;
            at += 2 + reclen;
            if (skipping > 0) {
                if (opens_scope(kind)) skipping++;
                else if (closes_scope(kind)) skipping--;
                continue;
            }
            const bool dead_here = dead_in(dead, base + here, base + at);
            const int tf = type_field(kind);
            if (dead_here || (tf < 0 && !plain(kind))) {
                if (opens_scope(kind)) skipping = 1;
                continue;
            }
            if (kind == S_INLINESITE_END) continue;      /* their sites are left out (no ids) */
            u16 k = kind == S_GPROC32_ID ? (u16)S_GPROC32 : kind == S_LPROC32_ID ? (u16)S_LPROC32
                  : kind == S_PROC_ID_END ? (u16)S_END : kind;
            /* a data symbol at file scope and a UDT go to the globals; a global variable only there */
            if (scopes.empty() && (k == S_GDATA32 || k == S_GTHREAD32 || k == S_LDATA32 || k == S_LTHREAD32 || k == S_UDT)) {
                Global g;
                g.kind = k;
                g.body.assign(body, body + blen);
                if (tf >= 0 && (u32)tf + 4 <= blen) retype(kind, &g.body[(size_t)tf]);
                g.name = record_name(k, g.body.data(), g.body.size());
                if (k != S_UDT || udts.insert(g.name).second) globals.push_back(g);
                if (k == S_GDATA32 || k == S_GTHREAD32 || k == S_UDT) continue;
            }
            /* a record of an object need not end on four bytes; one of a PDB must, so it is padded */
            const size_t start = dm.syms.size();
            const u32 padded = (blen + 2 + 2 + 3) / 4 * 4 - 2;
            put16(dm.syms, padded);
            put16(dm.syms, k);
            dm.syms.insert(dm.syms.end(), body, body + blen);
            dm.syms.resize(start + 2 + padded, 0);
            u8 *b = dm.syms.data() + start + 4;
            if (tf >= 0 && (u32)tf + 4 <= blen) retype(kind, b + tf);
            if (opens_scope(k)) {
                wr32(b, scopes.empty() ? 0 : (u32)scopes.back());
                wr32(b + 4, 0);
                if (k != S_BLOCK32 && k != S_SEPCODE && k != S_INLINESITE) wr32(b + 8, 0);
                if (k == S_GPROC32 || k == S_LPROC32) {
                    Global g;
                    g.kind = k == S_GPROC32 ? (u16)S_PROCREF : (u16)S_LPROCREF;
                    g.name = record_name(k, b, blen);
                    put32(g.body, 0);
                    put32(g.body, (u32)start);
                    put16(g.body, (u32)dbi + 1);
                    text(g.body, g.name);
                    if (scopes.empty()) globals.push_back(g);
                }
                scopes.push_back(start);
            } else if (closes_scope(k) && !scopes.empty()) {
                wr32(&dm.syms[scopes.back() + 8], (u32)start);
                scopes.pop_back();
            }
        }
    }

    /*  One module: every .debug$S of it that stays, relocated and taken apart. */
    void module(size_t mi, DbiModule &dm, int dbi)
    {
        const Module &m = lk.mods[mi];
        merge_types(m);
        put32(dm.syms, 4);                       /* CV_SIGNATURE_C13 */
        std::vector<size_t> scopes;
        int skipping = 0;
        Bytes checksums;
        std::vector<Bytes> lines;
        std::map<u32, std::string> strings;      /* the object's string table, by offset */
        bool have_files = false;
        for (size_t si = 0; si < m.secs.size(); si++) {
            const Contrib &c = m.secs[si];
            if (c.name != ".debug$S" || c.data.size() < 4 || rd32(&c.data[0]) != 4) continue;
            if (c.select == COMDAT_ASSOCIATIVE && c.assoc > 0 && (size_t)c.assoc <= m.secs.size()) {
                const Contrib &parent = m.secs[(size_t)c.assoc - 1];
                if (parent.dropped || parent.out < 0 || parent.fold_mod >= 0) continue;
            }
            Bytes d = c.data;
            std::set<u32> dead;
            relocate((int)mi, c, d, dead);
            for (u32 at = 4; at + 8 <= d.size(); ) {
                const u32 kind = rd32(&d[at]) & 0x7FFFFFFFu, len = rd32(&d[at + 4]);
                const u32 data = at + 8;
                if (data + len > d.size()) break;
                if (kind == DEBUG_S_SYMBOLS) {
                    symbols(dm, dbi, d.data() + data, data, len, dead, scopes, skipping);
                } else if (kind == DEBUG_S_LINES) {
                    if (!dead_in(dead, data, data + len) && len >= 12) {
                        Bytes l;
                        put32(l, DEBUG_S_LINES);
                        put32(l, len);
                        l.insert(l.end(), d.begin() + data, d.begin() + data + len);
                        pad4(l);
                        lines.push_back(l);
                    }
                } else if (kind == DEBUG_S_STRINGTABLE) {
                    for (u32 k = 0; k < len; ) {
                        u32 n = 0;
                        while (k + n < len && d[data + k + n]) n++;
                        strings[k] = std::string((const char *)&d[data + k], n);
                        k += n + 1;
                    }
                } else if (kind == DEBUG_S_FILECHKSMS && !have_files) {
                    checksums.assign(d.begin() + data, d.begin() + data + len);
                    have_files = true;
                }
                at = (data + len + 3) & ~3u;
            }
        }
        /* the file table, its names now /names offsets; the module's source files in order */
        if (have_files) {
            for (size_t at = 0; at + 6 <= checksums.size(); ) {
                const std::string name = strings[rd32(&checksums[at])];
                wr32(&checksums[at], names.add(name));
                dm.files.push_back(name);
                at = (at + 6 + checksums[at + 4] + 3) & ~(size_t)3;
            }
        }
        for (size_t i = 0; i < lines.size(); i++) append(dm.c13, lines[i]);
        if (have_files) {
            put32(dm.c13, DEBUG_S_FILECHKSMS);
            put32(dm.c13, (u32)checksums.size());
            append(dm.c13, checksums);
            pad4(dm.c13);
        }
    }

    /* the linker's own module: its name, and a record per output section */
    void linker_module(DbiModule &dm)
    {
        put32(dm.syms, 4);
        Bytes b;
        put32(b, 0);
        text(b, "* Linker *");
        record(dm.syms, S_OBJNAME, b);
        b.clear();
        put32(b, 7);                             /* CV_CFL_LINK */
        put16(b, 0xD0);
        for (int k = 0; k < 8; k++) put16(b, k == 0 || k == 4 ? 14 : 0);
        text(b, "GRIDE link");
        record(dm.syms, S_COMPILE3, b);
        for (size_t i = 0; i < lk.outs.size(); i++) {
            const OutSection &o = lk.outs[i];
            b.clear();
            put16(b, (u32)i + 1);
            put8(b, 12);                         /* the alignment, as a power of two: 4096 */
            put8(b, 0);
            put32(b, o.rva);
            put32(b, o.virt_size);
            put32(b, o.flags);
            text(b, o.name);
            record(dm.syms, S_SECTION, b);
        }
    }

    bool build(Msf &msf, std::string &err);
};

bool Builder::build(Msf &msf, std::string &err)
{
    (void)err;
    /* streams 0 to 4, filled in at the end */
    msf.add(); msf.add(); msf.add(); msf.add(); msf.add();

    /*  The modules: every object and archive member, in the link's order, and one for the
     *  linker's own contributions last, as link.exe lists them. */
    dbi_of.assign(lk.mods.size(), -1);
    int linker = -1;
    for (size_t mi = 0; mi < lk.mods.size(); mi++) {
        const Module &m = lk.mods[mi];
        const bool own = !m.name.empty() && m.name[0] == '*';
        if (own) continue;
        DbiModule dm;
        size_t paren = m.name.rfind('(');
        if (paren != std::string::npos && m.name[m.name.size() - 1] == ')') {
            dm.module = m.name.substr(paren + 1, m.name.size() - paren - 2);
            dm.object = m.name.substr(0, paren);
        } else {
            dm.module = dm.object = m.name;
        }
        dbi_of[mi] = (int)mods.size();
        mods.push_back(dm);
        module(mi, mods.back(), dbi_of[mi]);
    }
    linker = (int)mods.size();
    {
        DbiModule dm;
        dm.module = "* Linker *";
        mods.push_back(dm);
        linker_module(mods.back());
    }
    for (size_t mi = 0; mi < lk.mods.size(); mi++) if (dbi_of[mi] < 0) dbi_of[mi] = linker;

    /*  Section contributions, by section and offset; a module's first is its own record's. */
    struct SC { u16 sec; u32 off, size, flags; u16 mod; bool operator<(const SC &o) const { return sec != o.sec ? sec < o.sec : off < o.off; } };
    std::vector<SC> scs;
    for (size_t i = 0; i < lk.all.size(); i++) {
        const Contrib *c = lk.all[i];
        if (c->size == 0 || c->out < 0) continue;
        SC s;
        s.sec = (u16)(c->out + 1);
        s.off = c->rva - lk.outs[(size_t)c->out].rva;
        s.size = c->size;
        s.flags = c->flags;
        s.mod = (u16)dbi_of[(size_t)c->module];
        scs.push_back(s);
    }
    std::stable_sort(scs.begin(), scs.end());
    for (size_t i = 0; i < scs.size(); i++) {
        DbiModule &dm = mods[scs[i].mod];
        const bool code = (scs[i].flags & SCN_CNT_CODE) != 0;
        if (dm.sc_sec == 0xFFFF || (code && !(dm.sc_flags & SCN_CNT_CODE))) {
            dm.sc_sec = scs[i].sec; dm.sc_off = scs[i].off; dm.sc_size = scs[i].size; dm.sc_flags = scs[i].flags;
        }
    }

    /* each module's stream: the symbols, no C11 lines, the C13 lines, no global references */
    for (size_t i = 0; i < mods.size(); i++) {
        DbiModule &dm = mods[i];
        Bytes s = dm.syms;
        append(s, dm.c13);
        put32(s, 0);
        dm.stream = (int)msf.add(s);
    }

    /*  The record stream: the publics - every external the image holds, flagged code and
     *  function as link.exe flags them - then the globals; and the two hashes over it. */
    Bytes records;
    Gsi pub, glob;
    struct Addr { u16 seg; u32 off; std::string name; u32 at;
                  bool operator<(const Addr &o) const { return seg != o.seg ? seg < o.seg : off != o.off ? off < o.off : name < o.name; } };
    std::vector<Addr> addrs;
    for (std::map<std::string, std::pair<int, int> >::const_iterator it = lk.resolved.begin(); it != lk.resolved.end(); ++it) {
        const Module &m = lk.mods[(size_t)it->second.first];
        const Symbol &s = m.syms[(size_t)it->second.second];
        if (s.section <= 0 || (size_t)s.section > m.secs.size()) continue;
        u16 seg = 0; u32 off = 0, rva = 0;
        if (!address(it->second.first, (u32)it->second.second, seg, off, rva)) continue;
        const bool code = (lk.outs[(size_t)seg - 1].flags & SCN_CNT_CODE) != 0;
        Bytes b;
        put32(b, (code ? 1u : 0u) | ((s.type & 0xF0) == 0x20 ? 2u : 0u));
        put32(b, off);
        put16(b, seg);
        text(b, it->first);
        Addr a;
        a.seg = seg; a.off = off; a.name = it->first; a.at = (u32)records.size();
        addrs.push_back(a);
        pub.recs.push_back(std::make_pair(it->first, (u32)records.size()));
        record(records, S_PUB32, b);
    }
    for (size_t i = 0; i < globals.size(); i++) {
        glob.recs.push_back(std::make_pair(globals[i].name, (u32)records.size()));
        record(records, globals[i].kind, globals[i].body);
    }
    std::sort(addrs.begin(), addrs.end());
    const u32 records_stream = msf.add(records);
    const u32 globals_stream = msf.add(glob.stream());
    {
        Bytes gsi = pub.stream();
        Bytes p;
        put32(p, (u32)gsi.size());
        put32(p, (u32)addrs.size() * 4);
        put32(p, 0); put32(p, 0);                /* no thunks */
        put16(p, 0); put16(p, 0);
        put32(p, 0); put32(p, 0);
        append(p, gsi);
        for (size_t i = 0; i < addrs.size(); i++) put32(p, addrs[i].at);
        msf.add(p);
    }
    const u32 publics_stream = (u32)msf.streams.size() - 1;
    const u32 headers_stream = msf.add(lk.section_headers);
    const u32 names_stream = msf.add(names.stream());

    /* 1: the PDB info, its one named stream /names, and the feature saying the IPI is there */
    {
        Bytes b;
        put32(b, 20000404);
        put32(b, lk.opt.timestamp);
        put32(b, 1);
        b.insert(b.end(), lk.pdb_guid, lk.pdb_guid + 16);
        const std::string nm = "/names";
        put32(b, (u32)nm.size() + 1);
        text(b, nm);
        const u32 capacity = 8;
        put32(b, 1);                             /* entries */
        put32(b, capacity);
        put32(b, 1);                             /* the present buckets' bit vector: one word */
        put32(b, 1u << ((hash_v1(nm) & 0xFFFF) % capacity));
        put32(b, 0);                             /* no deleted buckets */
        put32(b, 0);                             /* the key: /names at offset 0 of the buffer */
        put32(b, names_stream);
        put32(b, 0);                             /* as lld writes it, before the features */
        put32(b, 20140508);                      /* VC140: the IPI stream is present */
        msf.streams[1] = b;
    }

    /* 2 and 4: TPI and IPI, each with the stream of its hashes */
    msf.streams[2] = tpi.stream(msf.add(tpi.hash_stream()));
    msf.streams[4] = ipi.stream(msf.add(ipi.hash_stream()));

    /* 3: DBI */
    {
        Bytes modinfo;
        for (size_t i = 0; i < mods.size(); i++) {
            const DbiModule &dm = mods[i];
            put32(modinfo, 0);
            put16(modinfo, dm.sc_sec); put16(modinfo, 0);
            put32(modinfo, dm.sc_off); put32(modinfo, dm.sc_size); put32(modinfo, dm.sc_flags);
            put16(modinfo, (u32)i); put16(modinfo, 0);
            put32(modinfo, 0); put32(modinfo, 0);
            put16(modinfo, 0);                   /* flags */
            put16(modinfo, (u32)dm.stream);
            put32(modinfo, (u32)dm.syms.size());
            put32(modinfo, 0);                   /* C11 lines */
            put32(modinfo, (u32)dm.c13.size());
            put16(modinfo, (u32)dm.files.size());
            put16(modinfo, 0);
            put32(modinfo, 0);
            put32(modinfo, 0); put32(modinfo, 0);
            text(modinfo, dm.module);
            text(modinfo, dm.object);
            pad4(modinfo);
        }
        Bytes contribs;
        put32(contribs, 0xEFFE0000u + 19970605u);
        for (size_t i = 0; i < scs.size(); i++) {
            put16(contribs, scs[i].sec); put16(contribs, 0);
            put32(contribs, scs[i].off); put32(contribs, scs[i].size); put32(contribs, scs[i].flags);
            put16(contribs, scs[i].mod); put16(contribs, 0);
            put32(contribs, 0); put32(contribs, 0);
        }
        /* the section map: an entry per output section, and one for absolute addresses */
        Bytes secmap;
        put16(secmap, (u32)lk.outs.size() + 1);
        put16(secmap, (u32)lk.outs.size() + 1);
        for (size_t i = 0; i < lk.outs.size(); i++) {
            const u32 f = lk.outs[i].flags;
            u32 flags = 0x0108;                  /* a selector, a 32-bit address */
            if (f & SCN_MEM_READ) flags |= 1;
            if (f & SCN_MEM_WRITE) flags |= 2;
            if (f & SCN_MEM_EXECUTE) flags |= 4;
            put16(secmap, flags); put16(secmap, 0); put16(secmap, 0);
            put16(secmap, (u32)i + 1);
            put16(secmap, 0xFFFF); put16(secmap, 0xFFFF);
            put32(secmap, 0); put32(secmap, lk.outs[i].virt_size);
        }
        put16(secmap, 0x0208); put16(secmap, 0); put16(secmap, 0);
        put16(secmap, (u32)lk.outs.size() + 1);
        put16(secmap, 0xFFFF); put16(secmap, 0xFFFF);
        put32(secmap, 0); put32(secmap, 0xFFFFFFFFu);
        /* the source files of each module, their names once each */
        Bytes files, buffer;
        std::map<std::string, u32> named;
        u32 total = 0;
        for (size_t i = 0; i < mods.size(); i++) total += (u32)mods[i].files.size();
        put16(files, (u32)mods.size());
        put16(files, total & 0xFFFF);
        for (size_t i = 0, first = 0; i < mods.size(); first += mods[i].files.size(), i++) put16(files, (u32)first & 0xFFFF);
        for (size_t i = 0; i < mods.size(); i++) put16(files, (u32)mods[i].files.size());
        for (size_t i = 0; i < mods.size(); i++)
            for (size_t k = 0; k < mods[i].files.size(); k++) {
                const std::string &f = mods[i].files[k];
                std::map<std::string, u32>::const_iterator it = named.find(f);
                if (it == named.end()) { it = named.insert(std::make_pair(f, (u32)buffer.size())).first; text(buffer, f); }
                put32(files, it->second);
            }
        append(files, buffer);
        pad4(files);
        Bytes ec = Names().stream();
        Bytes dbg;
        for (int k = 0; k < 11; k++) put16(dbg, k == 5 ? headers_stream : 0xFFFF);

        Bytes b;
        put32(b, 0xFFFFFFFFu);
        put32(b, 19990903);
        put32(b, 1);
        put16(b, globals_stream);
        put16(b, 0x8000 | (14 << 8) | 11);       /* the new build-number format, 14.11 */
        put16(b, publics_stream);
        put16(b, 0);
        put16(b, records_stream);
        put16(b, 0);
        put32(b, (u32)modinfo.size());
        put32(b, (u32)contribs.size());
        put32(b, (u32)secmap.size());
        put32(b, (u32)files.size());
        put32(b, 0);                             /* no type server map */
        put32(b, 0);
        put32(b, (u32)dbg.size());
        put32(b, (u32)ec.size());
        put16(b, 0);                             /* flags */
        put16(b, 0x8664);
        put32(b, 0);
        append(b, modinfo);
        append(b, contribs);
        append(b, secmap);
        append(b, files);
        append(b, ec);
        append(b, dbg);
        msf.streams[3] = b;
    }
    return true;
}

} /* namespace */

bool Link::write_pdb()
{
    if (!opt.debug) return true;
    Msf msf;
    Builder b(*this);
    if (!b.build(msf, err)) return false;
    return msf.write(pdb_path, err);
}
