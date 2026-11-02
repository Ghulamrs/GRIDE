"""mathfast.py - writes c\\mathfast.asm, the runtime's fast exp, log, sin, cos and pow (docs\\Runtime_Design.md 10.4):
the code below with its tables worked out here to 80-100 digits. Run it from anywhere: python tools\\mathfast.py"""
import decimal, math, os, struct
from decimal import Decimal as D

decimal.getcontext().prec = 100
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'c', 'mathfast.asm')


def bits(x):
    return struct.unpack('<Q', struct.pack('<d', x))[0]


def frombits(u):
    return struct.unpack('<d', struct.pack('<Q', u))[0]


def dq(x):
    return '0%016XH' % bits(x)


def atan_inv(n):
    x = D(1) / n
    x2 = x * x
    s, t, k = D(0), x, 1
    while abs(t) > D(10) ** -98:
        s += t / k
        t *= -x2
        k += 2
    return s


def atanh_inv(n):
    x = D(1) / n
    x2 = x * x
    s, t, k = D(0), x, 1
    while t > D(10) ** -98:
        s += t / k
        t *= x2
        k += 2
    return s


PI = 16 * atan_inv(5) - 4 * atan_inv(239)
LN2 = 2 * atanh_inv(3)


def exp(x):
    s, t, n = D(1), D(1), 1
    while abs(t) > D(10) ** -98:
        t = t * x / n
        s += t
        n += 1
    return s


def ln(x):
    x = D(x)
    k = 0
    while x > 2:
        x /= 2
        k += 1
    while x < 1:
        x *= 2
        k -= 1
    y = (x - 1) / (x + 1)
    y2 = y * y
    s, t, n = D(0), y, 1
    while abs(t) > D(10) ** -98:
        s += t / n
        t *= y2
        n += 2
    return 2 * s + k * LN2


def top(v, n):
    """v rounded to n significant bits"""
    m, e = math.frexp(float(v))
    return math.ldexp(round(m * 2 ** n), e - n)


def split(v):
    hi = float(v)
    return hi, float(v - D(hi))


data = []
# exp: 2^(j/128) as hi + lo, ln2/128 in a 32-bit high part (n * it exact for |n| < 2^21) and the rest
step = LN2 / 128
stepHi = top(step, 32)
data += ['expScale DQ ' + dq(float(128 / LN2)),
         'expStepHi DQ ' + dq(stepHi),
         'expStepLo DQ ' + dq(float(step - D(stepHi))),
         'expC2 DQ ' + dq(0.5),
         'expC3 DQ ' + dq(float(D(1) / 6)),
         'expC4 DQ ' + dq(float(D(1) / 24)),
         'expC5 DQ ' + dq(float(D(1) / 120))]
rows = [split(exp(LN2 * j / 128)) for j in range(128)]
data.append('expTable DQ %s, %s' % (dq(rows[0][0]), dq(rows[0][1])))
data += ['  DQ %s, %s' % (dq(h), dq(l)) for h, l in rows[1:]]

# log: 128 buckets of z in [OFF, 2 OFF), each an 8-bit reciprocal of its middle (1 for the bucket of 1), so that
# z * invc - 1 is exact, and -ln(invc) as hi + lo; ln2 in a 42-bit high part (k * it exact); log1p's coefficients
OFF = 0x3FE6A09E00000000
bucketOfOne = (0x3FF0000000000000 - OFF) >> 45
rows = []
for i in range(128):
    lo = frombits(OFF + (i << 45))
    hi = frombits(OFF + ((i + 1) << 45))
    if i == bucketOfOne:
        invc = 1.0
    else:
        invc = top(1.0 / ((lo + hi) / 2), 8)
    h, l = split(-ln(D(invc)))
    rows.append((invc, h, l))
ln2Hi = top(LN2, 42)
data += ['logLn2Hi DQ ' + dq(ln2Hi),
         'logLn2Lo DQ ' + dq(float(LN2 - D(ln2Hi)))]
data += ['logP%d DQ %s' % (n, dq(float((-1) ** (n + 1) / D(n)))) for n in range(2, 10)]
data.append('logTable DQ %s, %s, %s, 0' % (dq(rows[0][0]), dq(rows[0][1]), dq(rows[0][2])))
data += ['  DQ %s, %s, %s, 0' % (dq(a), dq(b), dq(c)) for a, b, c in rows[1:]]

# sin and cos: pi/2 in parts of 33, 33 and 53 bits (n * the first two exact for n < 2^20), 2/pi, and the Taylor
# coefficients of sin to r^17 and cos to r^18
half = PI / 2
p1 = top(half, 33)
p2 = top(half - D(p1), 33)
data += ['trigPio2a DQ ' + dq(p1),
         'trigPio2b DQ ' + dq(p2),
         'trigPio2c DQ ' + dq(float(half - D(p1) - D(p2))),
         'trigTwoOverPi DQ ' + dq(float(2 / PI))]
f = D(1)
for k in range(1, 19):
    f *= k
    if k >= 3 and k % 2:
        data.append('sinS%d DQ %s' % (k, dq(float((-1) ** ((k - 1) // 2) / f))))
    if k >= 4 and k % 2 == 0:
        data.append('cosC%d DQ %s' % (k, dq(float((-1) ** (k // 2) / f))))

data += ['one DQ ' + dq(1.0), 'half DQ ' + dq(0.5), 'shifter DQ ' + dq(6755399441055744.0),
         'splitter DQ ' + dq(134217729.0)]

HEADER = r'''; mathfast.asm - exp, log, sin, cos and pow (grtc): the double kernels a program's time goes to, in assembly
; (docs\Runtime_Design.md 10.4). Written by tools\mathfast.py with its tables; do not edit - change the script.
;
; Spec: ISO C 7.12.6.1 exp, 7.12.6.7 log, 7.12.4.6 sin, 7.12.4.5 cos, 7.12.7.4 pow, within an ulp of the true
; value and of the UCRT's (tests\math99, the UCRT probes). Each takes the common case here and leaves everything
; else - a NaN, an infinity, a zero, a subnormal, a result near overflow or underflow, a very large argument of sin
; and cos, pow with x not positive or y outside 2^-60..2^60 - to RTS6x's kernels (c\mathslow.cpp), which give the
; special values and errno. Only xmm0-xmm5 are used, so no register needs saving.
;   exp  x = (128 n + j) ln2/128 + r, the step's product subtracted in two parts so the first is exact; e^r - 1 by
;        its Taylor series to r^5 (|r| <= ln2/256); 2^(j/128) as hi + lo; the exponent added into the bits
;   log  z = x 2^-k in [0.707, 1.414); z invc - 1 exact in two parts (z's top 45 bits times the 8-bit invc, then
;        the rest); ln x = k ln2 + ln(1/invc) + log1p(r), the large terms summed with their errors, log1p by its
;        Taylor series to r^8 (r^9 for pow) in two chains of r^2; k's register cleared before cvtsi2sd, which
;        would otherwise wait for the register's last writer, one call's tail holding up the next
;   sin  x = n pi/2 + r by pi/2 in three parts, r kept with its low part; sin r or cos r by the quadrant, each by
;   cos  its Taylor series in two chains of r^4; cos as 1 - r^2/2 with that subtraction's error kept
;   pow  ln x as a double-double from log's kernel, y ln x as one by Dekker's product, e^ of it by exp's kernel
; Where the processor has the fused multiply-add (__grt_fma, c\memory.asm), each takes it: log's r = z invc - 1 in
; one exact step, the polynomials' steps fused, pow's y ln x error by fma(y, hi, -product). The results are those of
; the other path but in the last bit of rare cases. GRIDE's masm has no FMA mnemonics: they are written as bytes.
'''

CODE = r'''
PUBLIC exp
PUBLIC log
PUBLIC sin
PUBLIC cos
PUBLIC pow
EXTERN __grt_exp_slow:PROC
EXTERN __grt_log_slow:PROC
EXTERN __grt_sin_slow:PROC
EXTERN __grt_cos_slow:PROC
EXTERN __grt_pow_slow:PROC
EXTERN __grt_fma:DWORD

_DATA SEGMENT
;DATA
_DATA ENDS

_TEXT SEGMENT

exp PROC
    movq rax, xmm0
    mov rcx, rax
    btr rcx, 63
    mov rdx, 3C90000000000000h
    sub rcx, rdx
    mov rdx, 03F6200000000000h
    cmp rcx, rdx
    jae expSlow
    cmp DWORD PTR __grt_fma, 0
    jne expFma
;EXPCORE
    ret
expFma:
;EXPFMACORE
    ret
expSlow:
    jmp __grt_exp_slow
exp ENDP

log PROC
    movq rax, xmm0
    mov rcx, rax
    mov rdx, 0010000000000000h
    sub rcx, rdx
    mov rdx, 7FE0000000000000h
    cmp rcx, rdx
    jae logSlow
    cmp DWORD PTR __grt_fma, 0
    jne logFma
;LOGBODY
    addsd xmm4, xmm5
    movapd xmm0, xmm4
    ret
logFma:
;LOGFMABODY
    addsd xmm4, xmm5
    movapd xmm0, xmm4
    ret
logSlow:
    jmp __grt_log_slow
log ENDP

sin PROC
    movq rax, xmm0
    mov rcx, rax
    btr rcx, 63
    mov rdx, 3E50000000000000h
    cmp rcx, rdx
    jb sinTiny
    mov rdx, 412E848000000000h
    cmp rcx, rdx
    jae sinSlow
    xor r8d, r8d
    jmp trigReduce
sinTiny:
    ret
sinSlow:
    jmp __grt_sin_slow
sin ENDP

cos PROC
    movq rax, xmm0
    mov rcx, rax
    btr rcx, 63
    mov rdx, 3E40000000000000h
    cmp rcx, rdx
    jb cosTiny
    mov rdx, 412E848000000000h
    cmp rcx, rdx
    jae cosSlow
    mov r8d, 1
    jmp trigReduce
cosTiny:
    movsd xmm0, QWORD PTR one
    ret
cosSlow:
    jmp __grt_cos_slow
cos ENDP

trigReduce PROC
    movapd xmm1, xmm0
    mulsd xmm1, QWORD PTR trigTwoOverPi
    addsd xmm1, QWORD PTR shifter
    movq rax, xmm1
    add eax, r8d
    subsd xmm1, QWORD PTR shifter
    movapd xmm2, xmm1
    mulsd xmm2, QWORD PTR trigPio2a
    subsd xmm0, xmm2
    movapd xmm3, xmm1
    mulsd xmm3, QWORD PTR trigPio2b
    movapd xmm2, xmm0
    subsd xmm2, xmm3
    subsd xmm0, xmm2
    subsd xmm0, xmm3
    mulsd xmm1, QWORD PTR trigPio2c
    subsd xmm0, xmm1
    movapd xmm1, xmm2
    addsd xmm1, xmm0
    subsd xmm2, xmm1
    addsd xmm2, xmm0
    movapd xmm3, xmm1
    mulsd xmm3, xmm1
    movapd xmm5, xmm3
    mulsd xmm5, xmm3
    cmp DWORD PTR __grt_fma, 0
    jne trigFma
    test eax, 1
    jnz trigCosine
    movsd xmm4, QWORD PTR sinS15
    mulsd xmm4, xmm5
    addsd xmm4, QWORD PTR sinS11
    mulsd xmm4, xmm5
    addsd xmm4, QWORD PTR sinS7
    mulsd xmm4, xmm5
    addsd xmm4, QWORD PTR sinS3
    movsd xmm0, QWORD PTR sinS17
    mulsd xmm0, xmm5
    addsd xmm0, QWORD PTR sinS13
    mulsd xmm0, xmm5
    addsd xmm0, QWORD PTR sinS9
    mulsd xmm0, xmm5
    addsd xmm0, QWORD PTR sinS5
    mulsd xmm0, xmm3
    addsd xmm4, xmm0
    mulsd xmm4, xmm3
    mulsd xmm4, xmm1
    addsd xmm4, xmm2
    addsd xmm1, xmm4
    movapd xmm0, xmm1
    jmp trigSign
trigCosine:
    movsd xmm4, QWORD PTR cosC16
    mulsd xmm4, xmm5
    addsd xmm4, QWORD PTR cosC12
    mulsd xmm4, xmm5
    addsd xmm4, QWORD PTR cosC8
    mulsd xmm4, xmm5
    addsd xmm4, QWORD PTR cosC4
    movsd xmm0, QWORD PTR cosC18
    mulsd xmm0, xmm5
    addsd xmm0, QWORD PTR cosC14
    mulsd xmm0, xmm5
    addsd xmm0, QWORD PTR cosC10
    mulsd xmm0, xmm5
    addsd xmm0, QWORD PTR cosC6
    mulsd xmm0, xmm3
    addsd xmm4, xmm0
    mulsd xmm4, xmm5
    mulsd xmm2, xmm1
    subsd xmm4, xmm2
    movapd xmm5, xmm3
    mulsd xmm5, QWORD PTR half
    movsd xmm0, QWORD PTR one
    subsd xmm0, xmm5
    movsd xmm2, QWORD PTR one
    subsd xmm2, xmm0
    subsd xmm2, xmm5
    addsd xmm2, xmm4
    addsd xmm0, xmm2
    jmp trigSign
trigFma:
    test eax, 1
    jnz trigFmaCosine
    movsd xmm4, QWORD PTR sinS17
    movsd xmm0, QWORD PTR sinS13
    vfmadd213sd xmm4, xmm5, xmm0
    movsd xmm0, QWORD PTR sinS9
    vfmadd213sd xmm4, xmm5, xmm0
    movsd xmm0, QWORD PTR sinS5
    vfmadd213sd xmm4, xmm5, xmm0
    movq rcx, xmm4
    movsd xmm4, QWORD PTR sinS15
    movsd xmm0, QWORD PTR sinS11
    vfmadd213sd xmm4, xmm5, xmm0
    movsd xmm0, QWORD PTR sinS7
    vfmadd213sd xmm4, xmm5, xmm0
    movsd xmm0, QWORD PTR sinS3
    vfmadd213sd xmm4, xmm5, xmm0
    movq xmm0, rcx
    vfmadd231sd xmm4, xmm0, xmm3
    mulsd xmm4, xmm3
    vfmadd213sd xmm4, xmm1, xmm2
    addsd xmm1, xmm4
    movapd xmm0, xmm1
    jmp trigSign
trigFmaCosine:
    movsd xmm4, QWORD PTR cosC18
    movsd xmm0, QWORD PTR cosC14
    vfmadd213sd xmm4, xmm5, xmm0
    movsd xmm0, QWORD PTR cosC10
    vfmadd213sd xmm4, xmm5, xmm0
    movsd xmm0, QWORD PTR cosC6
    vfmadd213sd xmm4, xmm5, xmm0
    movq rcx, xmm4
    movsd xmm4, QWORD PTR cosC16
    movsd xmm0, QWORD PTR cosC12
    vfmadd213sd xmm4, xmm5, xmm0
    movsd xmm0, QWORD PTR cosC8
    vfmadd213sd xmm4, xmm5, xmm0
    movsd xmm0, QWORD PTR cosC4
    vfmadd213sd xmm4, xmm5, xmm0
    movq xmm0, rcx
    vfmadd231sd xmm4, xmm0, xmm3
    mulsd xmm4, xmm5
    mulsd xmm2, xmm1
    subsd xmm4, xmm2
    movapd xmm5, xmm3
    mulsd xmm5, QWORD PTR half
    movsd xmm0, QWORD PTR one
    subsd xmm0, xmm5
    movsd xmm2, QWORD PTR one
    subsd xmm2, xmm0
    subsd xmm2, xmm5
    addsd xmm2, xmm4
    addsd xmm0, xmm2
trigSign:
    test eax, 2
    jz trigDone
    movq rcx, xmm0
    btc rcx, 63
    movq xmm0, rcx
trigDone:
    ret
trigReduce ENDP

pow PROC
    movq r10, xmm0
    movq r11, xmm1
    mov rcx, r10
    mov rdx, 0010000000000000h
    sub rcx, rdx
    mov rdx, 7FE0000000000000h
    cmp rcx, rdx
    jae powSlow
    mov rcx, r11
    btr rcx, 63
    mov rdx, 3C30000000000000h
    sub rcx, rdx
    mov rdx, 0780000000000000h
    cmp rcx, rdx
    jae powSlow
    mov rax, r10
    cmp DWORD PTR __grt_fma, 0
    jne powFma
;POWLOGBODY
    movapd xmm0, xmm4
    addsd xmm0, xmm5
    subsd xmm4, xmm0
    addsd xmm4, xmm5
    movq r9, xmm4
    movq xmm1, r11
    movapd xmm2, xmm1
    mulsd xmm2, xmm0
    movapd xmm3, xmm1
    mulsd xmm3, QWORD PTR splitter
    movapd xmm5, xmm3
    subsd xmm5, xmm1
    subsd xmm3, xmm5
    subsd xmm1, xmm3
    movapd xmm5, xmm0
    mulsd xmm5, QWORD PTR splitter
    movapd xmm4, xmm5
    subsd xmm4, xmm0
    subsd xmm5, xmm4
    subsd xmm0, xmm5
    movapd xmm4, xmm3
    mulsd xmm4, xmm5
    subsd xmm4, xmm2
    mulsd xmm3, xmm0
    addsd xmm4, xmm3
    mulsd xmm5, xmm1
    addsd xmm4, xmm5
    mulsd xmm1, xmm0
    addsd xmm4, xmm1
    movq xmm1, r11
    movq xmm0, r9
    mulsd xmm1, xmm0
    addsd xmm4, xmm1
    jmp powExp
powFma:
;POWLOGFMABODY
    movapd xmm0, xmm4
    addsd xmm0, xmm5
    subsd xmm4, xmm0
    addsd xmm4, xmm5
    movq xmm1, r11
    movapd xmm2, xmm1
    mulsd xmm2, xmm0
    movapd xmm3, xmm1
    vfmsub213sd xmm3, xmm0, xmm2
    vfmadd231sd xmm3, xmm1, xmm4
    movapd xmm4, xmm3
powExp:
    movq rax, xmm2
    btr rax, 63
    mov rdx, 4086200000000000h
    cmp rax, rdx
    jae powSlow
    movapd xmm0, xmm2
    cmp DWORD PTR __grt_fma, 0
    jne powExpFma
;POWEXPCORE
    ret
powExpFma:
;POWEXPFMACORE
    ret
powSlow:
    movq xmm0, r10
    movq xmm1, r11
    jmp __grt_pow_slow
pow ENDP

_TEXT ENDS

END
'''


FMA_OPCODES = {'vfmadd132sd': 0x99, 'vfmadd213sd': 0xA9, 'vfmadd231sd': 0xB9, 'vfmsub213sd': 0xAB,
               'vfnmadd231sd': 0xBD}


def encode(text):
    """the FMA instructions, which GRIDE's masm has no mnemonics for, as their VEX bytes: register forms of xmm0-xmm7
    only (C4, then R X B clear with map 0F38, then W1 with the first source's register inverted and prefix 66)"""
    out = []
    for line in text.split('\n'):
        words = line.replace(',', ' ').split()
        if words and words[0] in FMA_OPCODES:
            dst, src1, src2 = (int(w[3:]) for w in words[1:4])
            assert max(dst, src1, src2) < 8
            data = [0xC4, 0xE2, 0x81 | ((~src1 & 15) << 3), FMA_OPCODES[words[0]], 0xC0 | dst << 3 | src2]
            line = '    DB ' + ', '.join('0%02Xh' % b for b in data) + '    ; ' + line.strip()
        out.append(line)
    return '\n'.join(out)


def exp_core(fma, low):
    """e^x for x in xmm0 (plus, with low, a low part in xmm4) into xmm0: the step's multiple n, r = x - n ln2/128 in
    two parts, e^r - 1 by its series, times 2^(j/128) as hi + lo, the exponent added into the bits"""
    b = '''    movsd xmm1, QWORD PTR expScale
    mulsd xmm1, xmm0
    movsd xmm2, QWORD PTR shifter
    addsd xmm1, xmm2
    movq rax, xmm1
    subsd xmm1, xmm2
'''
    if fma:
        b += '''    movsd xmm3, QWORD PTR expStepHi
    vfnmadd231sd xmm0, xmm1, xmm3
    movsd xmm3, QWORD PTR expStepLo
    vfnmadd231sd xmm0, xmm1, xmm3
'''
    else:
        b += '''    movsd xmm3, QWORD PTR expStepHi
    mulsd xmm3, xmm1
    subsd xmm0, xmm3
    movsd xmm3, QWORD PTR expStepLo
    mulsd xmm3, xmm1
    subsd xmm0, xmm3
'''
    if low:
        b += '''    addsd xmm0, xmm4
'''
    if fma:
        b += '''    movapd xmm1, xmm0
    mulsd xmm1, xmm0
    movsd xmm2, QWORD PTR expC5
    movsd xmm3, QWORD PTR expC4
    vfmadd213sd xmm2, xmm0, xmm3
    movsd xmm3, QWORD PTR expC3
    vfmadd213sd xmm2, xmm0, xmm3
    movsd xmm3, QWORD PTR expC2
    vfmadd213sd xmm2, xmm0, xmm3
    vfmadd213sd xmm2, xmm1, xmm0
'''
    else:
        b += '''    movapd xmm1, xmm0
    mulsd xmm1, xmm0
    movsd xmm2, QWORD PTR expC5
    mulsd xmm2, xmm0
    addsd xmm2, QWORD PTR expC4
    mulsd xmm2, xmm0
    addsd xmm2, QWORD PTR expC3
    mulsd xmm2, xmm0
    addsd xmm2, QWORD PTR expC2
    mulsd xmm2, xmm1
    addsd xmm2, xmm0
'''
    b += '''    mov ecx, eax
    and ecx, 127
    sar eax, 7
    shl rcx, 4
    lea rdx, expTable
    movsd xmm3, QWORD PTR [rdx+rcx]
    movsd xmm4, QWORD PTR [rdx+rcx+8]
'''
    if fma:
        b += '''    vfmadd213sd xmm2, xmm3, xmm4
'''
    else:
        b += '''    mulsd xmm2, xmm3
    addsd xmm2, xmm4
'''
    b += '''    addsd xmm2, xmm3
    movsxd rax, eax
    shl rax, 52
    movq rcx, xmm2
    add rcx, rax
    movq xmm0, rcx'''
    return b


def log_body_fma(ninth):
    """log's kernel with the fused multiply-add: r = z invc - 1 in one exact step, the polynomial's steps fused; ln x
    as hi (xmm4) + lo (xmm5)"""
    b = '''    mov rcx, rax
    mov rdx, 3FE6A09E00000000h
    sub rcx, rdx
    mov r8, rcx
    sar r8, 52
    mov r9, rcx
    shr r9, 45
    and r9d, 127
    mov rcx, r8
    shl rcx, 52
    sub rax, rcx
    movq xmm0, rax
    shl r9, 5
    lea rdx, logTable
    movsd xmm2, QWORD PTR [rdx+r9]
    movsd xmm1, QWORD PTR one
    vfmsub213sd xmm0, xmm2, xmm1
    movapd xmm4, xmm0
    mulsd xmm4, xmm0
    movsd xmm5, QWORD PTR logP8
    movsd xmm1, QWORD PTR logP6
    vfmadd213sd xmm5, xmm4, xmm1
    movsd xmm1, QWORD PTR logP4
    vfmadd213sd xmm5, xmm4, xmm1
    movsd xmm1, QWORD PTR logP2
    vfmadd213sd xmm5, xmm4, xmm1
'''
    if ninth:
        b += '''    movsd xmm2, QWORD PTR logP9
    movsd xmm1, QWORD PTR logP7
    vfmadd213sd xmm2, xmm4, xmm1
'''
    else:
        b += '''    movsd xmm2, QWORD PTR logP7
'''
    b += '''    movsd xmm1, QWORD PTR logP5
    vfmadd213sd xmm2, xmm4, xmm1
    movsd xmm1, QWORD PTR logP3
    vfmadd213sd xmm2, xmm4, xmm1
    vfmadd213sd xmm2, xmm0, xmm5
    mulsd xmm2, xmm4
    xorps xmm3, xmm3
    cvtsi2sd xmm3, r8
    movsd xmm1, QWORD PTR logLn2Hi
    mulsd xmm1, xmm3
    movsd xmm4, QWORD PTR [rdx+r9+8]
    movapd xmm5, xmm1
    addsd xmm5, xmm4
    subsd xmm1, xmm5
    addsd xmm1, xmm4
    addsd xmm2, xmm1
    movapd xmm4, xmm5
    addsd xmm4, xmm0
    subsd xmm5, xmm4
    addsd xmm5, xmm0
    addsd xmm2, xmm5
    mulsd xmm3, QWORD PTR logLn2Lo
    addsd xmm3, QWORD PTR [rdx+r9+16]
    addsd xmm2, xmm3
    movapd xmm5, xmm2'''
    return b


def log_body(ninth):
    """log's kernel from x's bits in rax to ln x as hi (xmm4) + lo (xmm5); with ninth, log1p's r^9 term too"""
    b = '''    mov rcx, rax
    mov rdx, 3FE6A09E00000000h
    sub rcx, rdx
    mov r8, rcx
    sar r8, 52
    mov r9, rcx
    shr r9, 45
    and r9d, 127
    mov rcx, r8
    shl rcx, 52
    sub rax, rcx
    movq xmm0, rax
    and rax, -256
    movq xmm1, rax
    subsd xmm0, xmm1
    shl r9, 5
    lea rdx, logTable
    movsd xmm2, QWORD PTR [rdx+r9]
    mulsd xmm1, xmm2
    subsd xmm1, QWORD PTR one
    mulsd xmm0, xmm2
    movapd xmm3, xmm1
    addsd xmm3, xmm0
    movapd xmm4, xmm3
    mulsd xmm4, xmm3
    movsd xmm5, QWORD PTR logP8
    mulsd xmm5, xmm4
    addsd xmm5, QWORD PTR logP6
    mulsd xmm5, xmm4
    addsd xmm5, QWORD PTR logP4
    mulsd xmm5, xmm4
    addsd xmm5, QWORD PTR logP2
'''
    if ninth:
        b += '''    movsd xmm2, QWORD PTR logP9
    mulsd xmm2, xmm4
    addsd xmm2, QWORD PTR logP7
'''
    else:
        b += '''    movsd xmm2, QWORD PTR logP7
'''
    b += '''    mulsd xmm2, xmm4
    addsd xmm2, QWORD PTR logP5
    mulsd xmm2, xmm4
    addsd xmm2, QWORD PTR logP3
    mulsd xmm2, xmm3
    addsd xmm5, xmm2
    mulsd xmm5, xmm4
    addsd xmm5, xmm0
    xorps xmm2, xmm2
    cvtsi2sd xmm2, r8
    movsd xmm3, QWORD PTR logLn2Hi
    mulsd xmm3, xmm2
    movsd xmm4, QWORD PTR [rdx+r9+8]
    movapd xmm0, xmm3
    addsd xmm0, xmm4
    subsd xmm3, xmm0
    addsd xmm3, xmm4
    addsd xmm5, xmm3
    movapd xmm4, xmm0
    addsd xmm4, xmm1
    subsd xmm0, xmm4
    addsd xmm0, xmm1
    addsd xmm5, xmm0
    mulsd xmm2, QWORD PTR logLn2Lo
    addsd xmm2, QWORD PTR [rdx+r9+16]
    addsd xmm5, xmm2'''
    return b


parts = {';DATA': '\n'.join(data), ';LOGBODY': log_body(False), ';POWLOGBODY': log_body(True),
         ';LOGFMABODY': log_body_fma(False), ';POWLOGFMABODY': log_body_fma(True),
         ';EXPCORE': exp_core(False, False), ';EXPFMACORE': exp_core(True, False),
         ';POWEXPCORE': exp_core(False, True), ';POWEXPFMACORE': exp_core(True, True)}
code = CODE
for mark in sorted(parts, key=len, reverse=True):
    code = code.replace(mark, parts[mark])
text = HEADER + encode(code)
open(OUT, 'w', newline='\n').write(text.lstrip('\n'))
print('wrote', os.path.normpath(OUT))
