; strlen.asm - strlen (grtc), in SSE2 (docs\Runtime_Design.md 10.2).
;
; Spec: ISO C 7.21.6.3 - the number of characters before the terminating NUL.
;
; Every load is an aligned 16 bytes, which never crosses a page, so nothing is read from a page the string does
; not reach; in the first block the bytes before the start are masked off. The blocks go one at a time to a
; 64-byte boundary, then four at a time, folded by PMINUB: a byte of the minimum is 0 only where one of the four
; has a 0. Microsoft x64: rcx in, rax out; xmm0-xmm4 only.

_TEXT SEGMENT

strlen PROC
    mov r9, rcx                     ; the start
    mov rax, rcx
    and rax, -16                    ; the aligned block holding the first character
    and ecx, 15                     ; the start's place in it
    pxor xmm0, xmm0
    movdqa xmm1, XMMWORD PTR [rax]
    pcmpeqb xmm1, xmm0
    pmovmskb edx, xmm1
    shr edx, cl                     ; the bytes before the start dropped: bit 0 is the first character
    test edx, edx
    jnz head
    add rax, 16
single:
    test al, 48                     ; a 64-byte boundary
    jz group
    movdqa xmm1, XMMWORD PTR [rax]
    pcmpeqb xmm1, xmm0
    pmovmskb edx, xmm1
    test edx, edx
    jnz found
    add rax, 16
    jmp single
    ALIGN 16
group:
    movdqa xmm1, XMMWORD PTR [rax]
    movdqa xmm2, XMMWORD PTR [rax+16]
    movdqa xmm3, XMMWORD PTR [rax+32]
    movdqa xmm4, XMMWORD PTR [rax+48]
    pminub xmm1, xmm2
    pminub xmm3, xmm4
    pminub xmm1, xmm3
    add rax, 64
    pcmpeqb xmm1, xmm0
    pmovmskb edx, xmm1
    test edx, edx
    jz group
    ; the NUL is in one of the four blocks: their masks as one of 64 bits (xmm2 and xmm4 still hold the second
    ; block and the fourth)
    movdqa xmm1, XMMWORD PTR [rax-64]
    movdqa xmm3, XMMWORD PTR [rax-32]
    pcmpeqb xmm1, xmm0
    pcmpeqb xmm2, xmm0
    pcmpeqb xmm3, xmm0
    pcmpeqb xmm4, xmm0
    pmovmskb ecx, xmm1
    pmovmskb edx, xmm2
    pmovmskb r8d, xmm3
    pmovmskb r10d, xmm4
    shl edx, 16
    shl r10d, 16
    or ecx, edx
    or r8d, r10d
    shl r8, 32
    or rcx, r8
    bsf rcx, rcx
    lea rax, [rax+rcx-64]
    sub rax, r9
    ret
found:
    bsf edx, edx
    add rax, rdx
    sub rax, r9
    ret
head:
    bsf eax, edx
    ret
strlen ENDP

_TEXT ENDS

END
