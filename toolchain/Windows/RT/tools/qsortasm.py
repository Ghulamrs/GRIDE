"""python tools\qsortasm.py c\qsortword.asm, from toolchain\Windows\RT: writes c\qsortword.asm, qsort.cpp's algorithm for elements of 4 and 8 bytes, the two
routines alike but for the width, its shift and the swap's registers."""
import sys

HEAD = """; qsortword.asm - qsort for elements of 4 and 8 bytes (grtc), called by c\\qsort.cpp:
; __grt_qsort4(base, count, compare) and __grt_qsort8. The algorithm is qsort.cpp's step by step - and so the
; Microsoft runtime's, which decides the order of elements that compare equal: a part of 8 or fewer elements sorted by
; selection, a larger one split around the median of three, the smaller side next, the larger on a stack of 62 parts.
; The C++ kept its pointers in memory across every call of the comparison; here they live in the registers the
; Microsoft x64 convention keeps across calls:
;   rbx lo, rsi hi, rdi mid (the selection's greatest), r12 up (the selection's next), r13 down,
;   r14 the comparison, r15 the stack's depth; the stack of parts above the comparison's home space.
; A comparison that throws unwinds through the frame by its unwind data. Written by qsortasm.py.

OPTION DOTNAME
OPTION NOSCOPED

PUBLIC __grt_qsort4
PUBLIC __grt_qsort8

_TEXT SEGMENT
"""

BODY = """
__grt_qsort{W} PROC FRAME
    push rbx
    .PUSHREG rbx
    push rsi
    .PUSHREG rsi
    push rdi
    .PUSHREG rdi
    push r12
    .PUSHREG r12
    push r13
    .PUSHREG r13
    push r14
    .PUSHREG r14
    push r15
    .PUSHREG r15
    sub rsp, 1024                   ; home space 32, the stack's lo parts at 32 and hi parts at 528
    .ALLOCSTACK 1024
    .ENDPROLOG
    mov r14, r8
    xor r15d, r15d
    mov rbx, rcx
    lea rsi, [rdx-1]
    shl rsi, {S}
    add rsi, rbx                    ; hi = lo + (count - 1) * width
$q{W}_part:
    mov rax, rsi
    sub rax, rbx
    cmp rax, {SEVEN}
    ja $q{W}_split
$q{W}_select:                       ; 8 or fewer: the greatest, the first of equal ones, swapped to the end
    cmp rsi, rbx
    jbe $q{W}_pop
    mov rdi, rbx
    lea r12, [rbx+{W}]
$q{W}_scan:
    cmp r12, rsi
    ja $q{W}_last
    mov rcx, r12
    mov rdx, rdi
    call r14
    test eax, eax
    jle $q{W}_scanned
    mov rdi, r12
$q{W}_scanned:
    add r12, {W}
    jmp $q{W}_scan
$q{W}_last:
{SWAP_rdi_rsi}
    sub rsi, {W}
    jmp $q{W}_select
$q{W}_split:                        ; mid = lo + (size / 2) * width, size = (hi - lo) / width + 1
    shr rax, {S}
    inc rax
    shr rax, 1
    shl rax, {S}
    lea rdi, [rbx+rax]
    mov rcx, rbx
    mov rdx, rdi
    call r14
    test eax, eax
    jle $q{W}_m1
{SWAP_rbx_rdi}
$q{W}_m1:
    mov rcx, rbx
    mov rdx, rsi
    call r14
    test eax, eax
    jle $q{W}_m2
{SWAP_rbx_rsi}
$q{W}_m2:
    mov rcx, rdi
    mov rdx, rsi
    call r14
    test eax, eax
    jle $q{W}_m3
{SWAP_rdi_rsi}
$q{W}_m3:
    mov r12, rbx
    mov r13, rsi
$q{W}_loop:
    cmp rdi, r12                    ; mid > up: up on while below mid and not above it
    jbe $q{W}_past
$q{W}_before:
    add r12, {W}
    cmp r12, rdi
    jae $q{W}_past
    mov rcx, r12
    mov rdx, rdi
    call r14
    test eax, eax
    jle $q{W}_before
$q{W}_past:
    cmp rdi, r12                    ; mid <= up: up on while within hi and not above mid
    ja $q{W}_down
$q{W}_after:
    add r12, {W}
    cmp r12, rsi
    ja $q{W}_down
    mov rcx, r12
    mov rdx, rdi
    call r14
    test eax, eax
    jle $q{W}_after
$q{W}_down:                         ; down back while above mid and above it
    sub r13, {W}
    cmp r13, rdi
    jbe $q{W}_met
    mov rcx, r13
    mov rdx, rdi
    call r14
    test eax, eax
    jg $q{W}_down
$q{W}_met:
    cmp r13, r12
    jb $q{W}_apart
{SWAP_r12_r13}
    cmp rdi, r13
    jne $q{W}_loop
    mov rdi, r12
    jmp $q{W}_loop
$q{W}_apart:
    add r13, {W}
    cmp rdi, r13                    ; mid < down: down back over the elements equal to mid
    jae $q{W}_low
$q{W}_equal:
    sub r13, {W}
    cmp r13, rdi
    jbe $q{W}_low
    mov rcx, r13
    mov rdx, rdi
    call r14
    test eax, eax
    jz $q{W}_equal
$q{W}_low:
    cmp rdi, r13                    ; mid >= down: down back while above lo and equal to mid
    jb $q{W}_sides
$q{W}_lower:
    sub r13, {W}
    cmp r13, rbx
    jbe $q{W}_sides
    mov rcx, r13
    mov rdx, rdi
    call r14
    test eax, eax
    jz $q{W}_lower
$q{W}_sides:                        ; down - lo >= hi - up, as signed differences: the left side waits
    mov rax, r13
    sub rax, rbx
    mov rcx, rsi
    sub rcx, r12
    cmp rax, rcx
    jl $q{W}_right
    cmp rbx, r13
    jae $q{W}_leftnone
    mov QWORD PTR [rsp+r15*8+32], rbx
    mov QWORD PTR [rsp+r15*8+528], r13
    inc r15
$q{W}_leftnone:
    cmp r12, rsi
    jae $q{W}_pop
    mov rbx, r12
    jmp $q{W}_part
$q{W}_right:
    cmp r12, rsi
    jae $q{W}_rightnone
    mov QWORD PTR [rsp+r15*8+32], r12
    mov QWORD PTR [rsp+r15*8+528], rsi
    inc r15
$q{W}_rightnone:
    cmp rbx, r13
    jae $q{W}_pop
    mov rsi, r13
    jmp $q{W}_part
$q{W}_pop:
    dec r15
    js $q{W}_done
    mov rbx, QWORD PTR [rsp+r15*8+32]
    mov rsi, QWORD PTR [rsp+r15*8+528]
    jmp $q{W}_part
$q{W}_done:
    add rsp, 1024
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret
__grt_qsort{W} ENDP
"""


def swap(w, a, b):
    size, ra, rd = ("DWORD", "eax", "edx") if w == 4 else ("QWORD", "rax", "rdx")
    return "\n".join([
        f"    mov {ra}, {size} PTR [{a}]",
        f"    mov {rd}, {size} PTR [{b}]",
        f"    mov {size} PTR [{a}], {rd}",
        f"    mov {size} PTR [{b}], {ra}",
    ])


out = [HEAD]
for w, s in ((4, 2), (8, 3)):
    text = BODY
    for a, b in (("rdi", "rsi"), ("rbx", "rdi"), ("rbx", "rsi"), ("r12", "r13")):
        text = text.replace("{SWAP_%s_%s}" % (a, b), swap(w, a, b))
    out.append(text.replace("{W}", str(w)).replace("{S}", str(s)).replace("{SEVEN}", str(7 * w)))
out.append("\n_TEXT ENDS\n\nEND\n")
with open(sys.argv[1], "w", newline="\r\n") as f:
    f.write("".join(out).lstrip("\n").replace("\r\n", "\n"))
