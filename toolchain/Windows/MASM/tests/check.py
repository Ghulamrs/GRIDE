r"""check.py - GRIDE's masm against Microsoft's ml64, byte for byte.

Each test .asm is assembled by both. Every section of the two objects must hold the same bytes and the
same relocations (offset, type and the symbol's name), and the external symbols must agree (name,
section, value). On a difference the instruction is named from ml64's listing.

usage: python check.py [--masm masm.exe] [--ml64 ml64.exe] [file.asm ...]
  the default files are every .asm beside this script; the default masm is GRIDEd\x64\Release\masm.exe of
  this repository, the default ml64 the one named by the ML64 environment variable or found under Visual
  Studio. ml64 is used for checking only.
"""
import glob, os, re, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..', '..', '..'))
IGNORED = {'.debug$S', '.debug$T'}      # ml64's own CodeView (its version, the object's path) is not compared
RELTYPE = {1: 'ADDR64', 2: 'ADDR32', 3: 'ADDR32NB', 4: 'REL32', 5: 'REL32_1', 6: 'REL32_2', 7: 'REL32_3',
           8: 'REL32_4', 9: 'REL32_5', 10: 'SECTION', 11: 'SECREL'}


def find_ml64():
    if os.environ.get('ML64'):
        return os.environ['ML64']
    pats = [r'D:\Microsoft Visual Studio*\VC\Tools\MSVC\*\bin\Hostx64\x64\ml64.exe',
            r'C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\ml64.exe',
            r'C:\Program Files (x86)\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\ml64.exe']
    for p in pats:
        found = sorted(glob.glob(p))
        if found:
            return found[-1]
    return None


def coff(path):
    """the sections (name -> bytes, relocations) and the external symbols of a COFF object"""
    data = open(path, 'rb').read()
    _, nsec, _, symptr, nsym, opt, _ = struct.unpack('<HHIIIHH', data[:20])
    strtab = data[symptr + 18 * nsym:]

    def name_of(raw):
        if raw[:4] == b'\0\0\0\0':
            off = struct.unpack('<I', raw[4:8])[0]
            return strtab[off:strtab.index(b'\0', off)].decode('latin-1')
        return raw.rstrip(b'\0').decode('latin-1')

    secnames = []
    raw = []
    for i in range(nsec):
        h = data[20 + opt + 40 * i: 20 + opt + 40 * (i + 1)]
        n = h[:8].rstrip(b'\0').decode('latin-1')
        if n.startswith('/'):
            off = int(n[1:])
            n = strtab[off:strtab.index(b'\0', off)].decode('latin-1')
        size, ptr, rptr, _, nrel = struct.unpack('<IIIIH', h[16:34])
        chars = struct.unpack('<I', h[36:40])[0]
        secnames.append(n)
        raw.append((n, size, ptr, rptr, nrel, chars))
    syms = {}
    i = 0
    while i < nsym:
        e = data[symptr + 18 * i: symptr + 18 * i + 18]
        value, sec, _, cls, naux = struct.unpack('<IhHBB', e[8:18])
        syms[i] = (name_of(e[:8]), value, sec, cls)
        i += 1 + naux
    sections = {}
    for (n, size, ptr, rptr, nrel, chars) in raw:
        body = data[ptr:ptr + size] if ptr else b'\0' * size
        rel = []
        for k in range(nrel):
            off, si, typ = struct.unpack('<IIH', data[rptr + 10 * k: rptr + 10 * k + 10])
            rel.append((off, RELTYPE.get(typ, str(typ)), syms[si][0]))
        key = n
        while key in sections:          # COMDATs share a name
            key += "'"
        sections[key] = (body, rel, chars & 0x00F000E0)   # alignment and contents flags
    ext = sorted((nm, secnames[sec - 1] if sec > 0 else 'UNDEF', value) for (nm, value, sec, cls) in syms.values() if cls == 2)
    return sections, ext


def listing(path):
    """ml64's listing: (code offset, source line) for each line that made code"""
    out = []
    code = False
    for l in open(path, encoding='latin-1'):
        if re.search(r'\.CODE\b|\bPROC\b', l, re.I):
            code = True
        if re.search(r'\.DATA\b', l, re.I):
            code = False
        m = re.match(r' ([0-9A-F]{8})  [0-9A-F]', l)
        if m and code:
            out.append((int(m.group(1), 16), l.split('\t')[-1].strip()))
    return out


def where(lst, off):
    best = None
    for o, src in lst:
        if o <= off:
            best = (o, src)
    return '%08X  %s' % best if best else '?'


def check(asm, masm, ml64, tmp):
    base = os.path.splitext(os.path.basename(asm))[0]
    a = os.path.join(tmp, base + '-ml64.obj')
    b = os.path.join(tmp, base + '-masm.obj')
    lst = os.path.join(tmp, base + '.lst')
    r = subprocess.run([ml64, '/nologo', '/c', '/Fl' + lst, '/Fo', a, asm], capture_output=True, text=True)
    if r.returncode != 0:
        return ['ml64 refused it:'] + [l for l in r.stdout.splitlines() if 'error' in l][:10]
    r = subprocess.run([masm, '-t', 'x64', asm, '-o', b], capture_output=True, text=True)
    if r.returncode != 0:
        return ['masm refused it:'] + (r.stdout + r.stderr).splitlines()[:20]
    sa, ea = coff(a)
    sb, eb = coff(b)
    lst = listing(lst)
    bad = []
    for n in sorted(set(sa) | set(sb)):
        if n.rstrip("'") in IGNORED:
            continue
        if n not in sa or n not in sb:
            bad.append('section %s only in %s' % (n, 'ml64' if n in sa else 'masm'))
            continue
        (ba, ra, fa), (bb, rb, fb) = sa[n], sb[n]
        if fa != fb:
            bad.append('section %s: flags %08X (ml64) %08X (masm)' % (n, fa, fb))
        if ba != bb:
            k = next((i for i in range(min(len(ba), len(bb))) if ba[i] != bb[i]), min(len(ba), len(bb)))
            bad.append('section %s: bytes differ at %X (ml64 %d bytes, masm %d)%s' % (
                n, k, len(ba), len(bb), ('; ' + where(lst, k)) if n.startswith('.text') else ''))
            bad.append('    ml64 %s' % ba[max(0, k - 4):k + 12].hex(' '))
            bad.append('    masm %s' % bb[max(0, k - 4):k + 12].hex(' '))
        if ra != rb:
            for x, y in zip(ra + [None] * len(rb), rb + [None] * len(ra)):
                if x != y:
                    bad.append('section %s: relocation ml64 %s, masm %s%s' % (
                        n, x, y, ('; ' + where(lst, (x or y)[0])) if n.startswith('.text') else ''))
                    break
    if ea != eb:
        bad.append('external symbols differ:')
        bad.append('    ml64 %s' % [e for e in ea if e not in eb])
        bad.append('    masm %s' % [e for e in eb if e not in ea])
    counts[0] += sum(len(sa[n][0]) for n in sa if n.rstrip("'") not in IGNORED)
    counts[1] += sum(len(sa[n][1]) for n in sa if n.rstrip("'") not in IGNORED)
    counts[2] += len(ea)
    return bad


counts = [0, 0, 0]      # bytes, relocations and external symbols compared


def forms(asm):
    """the instruction lines: indented, not a comment or a directive"""
    return sum(1 for l in open(asm, encoding='latin-1')
               if re.match(r'\s+[A-Za-z]', l) and not re.match(r'\s+\.', l))


def main():
    args = sys.argv[1:]
    masm = os.path.join(REPO, 'GRIDEd', 'x64', 'Release', 'masm.exe')
    ml64 = None
    files = []
    while args:
        a = args.pop(0)
        if a == '--masm': masm = args.pop(0)
        elif a == '--ml64': ml64 = args.pop(0)
        else: files.append(a)
    ml64 = ml64 or find_ml64()
    if not ml64 or not os.path.exists(ml64):
        sys.exit('ml64.exe not found: name it with --ml64 or the ML64 environment variable')
    if not os.path.exists(masm):
        sys.exit('masm.exe not found: name it with --masm')
    files = files or sorted(glob.glob(os.path.join(HERE, '*.asm')))
    tmp = tempfile.mkdtemp(prefix='masm-check-')
    failed = 0
    total = 0
    try:
        for f in files:
            n = forms(f)
            before = list(counts)
            bad = check(f, masm, ml64, tmp)
            total += n
            print('%-20s %5d forms  %s  (%d bytes, %d relocations, %d external symbols)' % (
                os.path.basename(f), n, 'identical' if not bad else 'DIFFERENT',
                counts[0] - before[0], counts[1] - before[1], counts[2] - before[2]))
            for l in bad:
                print('    ' + l)
            failed += bool(bad)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print('%d files, %d forms: %s' % (len(files), total, 'all identical to ml64' if not failed else '%d different' % failed))
    sys.exit(1 if failed else 0)


main()
