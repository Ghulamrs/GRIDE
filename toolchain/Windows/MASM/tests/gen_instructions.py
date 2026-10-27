"""gen_instructions.py - writes instructions.asm beside it: every form ml64 takes of the instructions GRIDE's
Windows runtime needs, register and memory, low and high registers, every address mode, RIP-relative and
absolute-indexed operands on extern and local data."""
import os, sys

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'instructions.asm')
L = []
def emit(s): L.append('  ' + s)
def note(s): L.append('; ' + s)

XL = ['xmm0', 'xmm1', 'xmm7', 'xmm8', 'xmm9', 'xmm15']
# address modes: plain base, disp8, disp32, rsp (SIB), rbp/r13 (mod 01), r12 (SIB), index with every scale,
# REX.X and REX.B, no base, negative displacement
ADDR = ['[rax]', '[rcx+8]', '[rdx+1000h]', '[rsp]', '[rsp+16]', '[rbp]', '[r13]', '[r12]', '[r12+40h]',
        '[rax+rbx*4]', '[r8+r9*8+16]', '[r15+r14*2-128]', '[rsi+r10+200h]', '[rdi*8+16]', '[rbx+rbp]',
        '[r11+rsp]', '[rax-8]']
def xmm(i): return XL[i % len(XL)]

ELEM = {1: 'BYTE', 2: 'WORD', 4: 'DWORD', 8: 'QWORD'}
ELABEL = {1: 'gb', 2: 'gw', 4: 'gd', 8: 'gq'}

# name and element width: ml64 takes memory of that width besides 128 bits (0: 128 bits only)
SSE = [('pcmpeqb', 1), ('pcmpeqw', 2), ('pcmpeqd', 4), ('pminub', 1), ('pmaxub', 1), ('psubb', 1), ('pandn', 0),
       ('punpcklbw', 1), ('punpcklwd', 2), ('punpckldq', 4), ('punpcklqdq', 8),
       ('movups', 4), ('movaps', 4), ('xorps', 4), ('andps', 4), ('andnps', 4), ('orps', 4),
       ('movdqa', 0), ('movdqu', 0), ('pxor', 0), ('pand', 0), ('por', 0),
       ('xorpd', 8), ('andpd', 8), ('andnpd', 8), ('orpd', 8), ('movapd', 8), ('movupd', 8), ('paddq', 8), ('psubq', 8)]
STORE = ['movups', 'movaps', 'movdqa', 'movdqu', 'movntdq', 'movapd', 'movupd']

def mem_forms(elem, k):
    """the memory operands a 128-bit op takes: unsized, XMMWORD, OWORD, its element; RIP and indexed"""
    f = []
    for j, a in enumerate(ADDR):
        f.append(a)
    for a in ['[r9+r10*4+7Fh]', '[rax]']:
        f.append('XMMWORD PTR ' + a)
        f.append('OWORD PTR ' + a)
        if elem:
            f.append(ELEM[elem] + ' PTR ' + a)
    if elem == 4: f.append('REAL4 PTR [rcx]'); f.append('gf')
    if elem == 8: f.append('REAL8 PTR [rcx]'); f.append('gr8')
    f += ['gx', 'gx+32', 'OWORD PTR gq', 'XMMWORD PTR gq+8', 'OWORD PTR lq', 'OWORD PTR lq+16', 'gx[rcx]',
          'OWORD PTR lq[rax*8]', '[gx+16]']
    if elem:
        f.append(ELABEL[elem]); f.append(ELABEL[elem] + '+4')
    return f

L += ['; instructions.asm - every form ml64 takes of the instructions GRIDE\'s Windows runtime needs (and the',
      '; SSE ops beside them in masm\'s table), each checked byte for byte and relocation for relocation',
      '; against ml64 by check.py. Written by gen_instructions.py, which says which operands ml64 takes.',
      '', 'EXTERN gb:BYTE', 'EXTERN gw:WORD', 'EXTERN gd:DWORD', 'EXTERN gq:QWORD', 'EXTERN gx:XMMWORD',
      'EXTERN gf:REAL4', 'EXTERN gr8:REAL8', 'EXTERN gp:PROC', '',
      '.DATA', 'lq DQ 0, 0, 0, 0', 'lb DB 0', '', '.CODE', '']

L.append('sse PROC')
k = 0
for op, elem in SSE:
    note(op)
    for i in range(4):
        emit('%s %s, %s' % (op, xmm(i), xmm(5 - i)))
    for m in mem_forms(elem, k):
        emit('%s %s, %s' % (op, xmm(k), m)); k += 1
L.append('sse ENDP\n')

L.append('stores PROC')
for op in STORE:
    note(op)
    elem = dict(SSE).get(op, 0)
    for m in mem_forms(elem, k):
        emit('%s %s, %s' % (op, m, xmm(k))); k += 1
L.append('stores ENDP\n')

L.append('imm8 PROC')
for op, elem in [('pshufd', 4), ('roundsd', 8), ('roundss', 4)]:
    note(op)
    for i, imm in enumerate(['0', '1', '1Bh', '0FFh', '255', '-1', '-128', '127', '3 + 4']):
        emit('%s %s, %s, %s' % (op, xmm(i), xmm(i + 2), imm))
    for m in mem_forms(elem, k):
        emit('%s %s, %s, %d' % (op, xmm(k), m, k % 256)); k += 1
for op in ['pslldq', 'psrldq']:
    note(op)
    for i, imm in enumerate(['0', '1', '4', '8', '15', '16', '0FFh', '-1', '-128']):
        emit('%s %s, %s' % (op, XL[i % len(XL)], imm))
L.append('imm8 ENDP\n')

L.append('moves PROC')
note('pmovmskb')
for r in ['eax', 'ecx', 'r10d', 'r15d', 'rax', 'rdx', 'r9', 'r15']:
    for x in ['xmm0', 'xmm9', 'xmm15']:
        emit('pmovmskb %s, %s' % (r, x))
note('movd and movq with general registers')
for x in ['xmm0', 'xmm6', 'xmm8', 'xmm15']:
    for r in ['eax', 'esp', 'r8d', 'r15d']:
        emit('movd %s, %s' % (x, r)); emit('movd %s, %s' % (r, x))
    for r in ['rax', 'rbp', 'r9', 'r15']:
        emit('movd %s, %s' % (x, r)); emit('movd %s, %s' % (r, x))
        emit('movq %s, %s' % (x, r)); emit('movq %s, %s' % (r, x))
note('movq between xmm registers')
for a, b in [('xmm0', 'xmm1'), ('xmm7', 'xmm8'), ('xmm15', 'xmm15'), ('xmm9', 'xmm2')]:
    emit('movq %s, %s' % (a, b))
note('movd and movq with memory')
for j, a in enumerate(ADDR + ['[r9+r10*4+7Fh]']):
    x = xmm(j)
    emit('movd %s, DWORD PTR %s' % (x, a)); emit('movd DWORD PTR %s, %s' % (a, x))
    emit('movd %s, QWORD PTR %s' % (x, a)); emit('movd QWORD PTR %s, %s' % (a, x))
    emit('movq %s, QWORD PTR %s' % (x, a)); emit('movq QWORD PTR %s, %s' % (a, x))
for m in ['gd', 'gd+4', 'gf', 'DWORD PTR gq', 'DWORD PTR lq+8', 'REAL4 PTR [rax]', 'gd[rcx*4]']:
    emit('movd xmm3, %s' % m); emit('movd %s, xmm12' % m)
for m in ['gq', 'gq+8', 'gr8', 'QWORD PTR gx', 'QWORD PTR gx+8', 'lq', 'lq+8', 'REAL8 PTR [rax]', 'gq[rcx*8]', 'QWORD PTR lq[rdx]']:
    emit('movd xmm3, %s' % m); emit('movd %s, xmm12' % m)
    emit('movq xmm3, %s' % m); emit('movq %s, xmm12' % m)
note('movnti')
for j, a in enumerate(ADDR):
    r32 = ['eax', 'ecx', 'r8d', 'r15d', 'esi'][j % 5]
    r64 = ['rax', 'rcx', 'r8', 'r15', 'rsi'][j % 5]
    emit('movnti %s, %s' % (a, r32)); emit('movnti DWORD PTR %s, %s' % (a, r32))
    emit('movnti %s, %s' % (a, r64)); emit('movnti QWORD PTR %s, %s' % (a, r64))
for m in ['gd', 'gf', 'DWORD PTR gq+4']:
    emit('movnti %s, r11d' % m)
for m in ['gq', 'gr8', 'lq+8', 'QWORD PTR gx', 'gq[rax*8]']:
    emit('movnti %s, r11' % m)
L.append('moves ENDP\n')

L.append('control PROC')
for op, sizes in [('prefetchnta', ['', 'BYTE', 'WORD', 'DWORD', 'QWORD', 'XMMWORD', 'OWORD', 'REAL4', 'REAL8']),
                  ('prefetcht0', ['', 'BYTE', 'QWORD']), ('prefetcht1', ['', 'BYTE']), ('prefetcht2', ['', 'BYTE']),
                  ('stmxcsr', ['', 'DWORD', 'REAL4']), ('ldmxcsr', ['', 'DWORD', 'REAL4']),
                  ('fnstcw', ['', 'WORD']), ('fldcw', ['', 'WORD'])]:
    note(op)
    for j, a in enumerate(ADDR):
        s = sizes[j % len(sizes)]
        emit('%s %s%s' % (op, (s + ' PTR ') if s else '', a))
    labels = {'prefetchnta': ['gb', 'gw', 'gd', 'gq', 'gx', 'gf', 'gr8', 'lq', 'lb+1', 'gx[rax]', 'BYTE PTR gp'],
              'prefetcht0': ['gb', 'gq+64', 'lq'], 'prefetcht1': ['gx'], 'prefetcht2': ['gd'],
              'stmxcsr': ['gd', 'gf', 'DWORD PTR gq', 'DWORD PTR lq+4', 'gd[rcx*4]'],
              'ldmxcsr': ['gd', 'gf', 'DWORD PTR gx+12', 'DWORD PTR lq'],
              'fnstcw': ['gw', 'WORD PTR gq+2', 'WORD PTR lq+6', 'gw[rcx*2]'],
              'fldcw': ['gw', 'WORD PTR gd', 'WORD PTR lq']}[op]
    for m in labels:
        emit('%s %s' % (op, m))
note('no operands')
for op in ['fnclex', 'fninit', 'fwait', 'wait', 'cpuid', 'xgetbv', 'pause', 'sfence', 'lfence', 'mfence', 'std', 'cld']:
    emit(op)
note('the string instructions the runtime uses, with REP')
for op in ['rep movsb', 'rep stosb', 'rep movsw', 'rep stosw', 'rep movsd', 'rep stosd', 'rep movsq', 'rep stosq',
           'movsb', 'stosb', 'movsq', 'stosq', 'repe cmpsb', 'repne scasb', 'repz cmpsq', 'repnz scasb']:
    emit(op)
L.append('control ENDP\n')

L.append('bits PROC')
for op in ['bsf', 'bsr', 'tzcnt', 'lzcnt', 'popcnt']:
    note(op)
    for d, s in [('ax', 'cx'), ('r9w', 'r15w'), ('eax', 'ecx'), ('r8d', 'esp'), ('ebp', 'r13d'), ('rax', 'rcx'),
                 ('r15', 'r8'), ('rsp', 'rbp'), ('r12', 'r13')]:
        emit('%s %s, %s' % (op, d, s))
    for j, a in enumerate(ADDR):
        d = ['ax', 'eax', 'rax', 'r10w', 'r11d', 'r12'][j % 6]
        sz = {'ax': 'WORD', 'r10w': 'WORD', 'eax': 'DWORD', 'r11d': 'DWORD', 'rax': 'QWORD', 'r12': 'QWORD'}[d]
        emit('%s %s, %s' % (op, d, a)); emit('%s %s, %s PTR %s' % (op, d, sz, a))
    for d, m in [('cx', 'gw'), ('ecx', 'gd'), ('ecx', 'gf'), ('rcx', 'gq'), ('rcx', 'gr8'), ('r9', 'lq+8'),
                 ('rdx', 'QWORD PTR gx'), ('edx', 'DWORD PTR lq+4'), ('r8', 'gq[rax*8]')]:
        emit('%s %s, %s' % (op, d, m))
L.append('bits ENDP\n')
L.append('END')

open(out, 'w', newline='\r\n').write('\n'.join(L) + '\n')
print('%d instruction lines' % sum(1 for l in L if l.startswith('  ')))
