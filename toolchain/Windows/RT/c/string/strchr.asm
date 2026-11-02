; strchr.asm - strchr (grtc), in SSE2 (docs\Runtime_Design.md 10.2).
;
; Spec: ISO C 7.21.5.2 - the first c (converted to char) in the string, the terminating NUL itself findable; a
; null pointer when c is not there.
;
; Every load is an aligned 16 bytes, which never crosses a page; in the first block the bytes before the start
; are masked off. A byte x stops the search where min(x, x XOR c) is 0: x is the NUL or x is c. The blocks go one
; at a time to a 64-byte boundary, then four at a time, their minima folded by PMINUB. Where it stops, the byte
; is c or the NUL: c is the answer, the NUL (when c is not NUL) says there is none. Microsoft x64: rcx and edx
; in, rax out; xmm0-xmm5 only.

_TEXT SEGMENT

strchr PROC
    movzx edx, dl
    mov r8d, edx                    ; c as a char
    imul edx, edx, 01010101h
    movd xmm5, edx
    pshufd xmm5, xmm5, 0            ; c in every byte
    pxor xmm0, xmm0
    mov rax, rcx
    and rax, -16                    ; the aligned block holding the first character
    and ecx, 15                     ; the start's place in it
    movdqa xmm1, XMMWORD PTR [rax]
    movdqa xmm2, xmm5
    pxor xmm2, xmm1                 ; 0 where c
    pminub xmm1, xmm2               ; 0 where c or the NUL
    pcmpeqb xmm1, xmm0
    pmovmskb edx, xmm1
    shr edx, cl
    shl edx, cl                     ; the bytes before the start dropped, the rest in place
    test edx, edx
    jnz found
    add rax, 16
single:
    test al, 48                     ; a 64-byte boundary
    jz group
    movdqa xmm1, XMMWORD PTR [rax]
    movdqa xmm2, xmm5
    pxor xmm2, xmm1
    pminub xmm1, xmm2
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
    movdqa xmm3, xmm5
    movdqa xmm4, xmm5
    pxor xmm3, xmm1
    pxor xmm4, xmm2
    pminub xmm1, xmm3
    pminub xmm2, xmm4
    pminub xmm1, xmm2               ; the first 32
    movdqa xmm2, XMMWORD PTR [rax+32]
    movdqa xmm3, XMMWORD PTR [rax+48]
    movdqa xmm4, xmm5
    pxor xmm4, xmm2
    pminub xmm2, xmm4
    movdqa xmm4, xmm5
    pxor xmm4, xmm3
    pminub xmm3, xmm4
    pminub xmm2, xmm3               ; the last 32
    pminub xmm1, xmm2
    add rax, 64
    pcmpeqb xmm1, xmm0
    pmovmskb edx, xmm1
    test edx, edx
    jz group
    ; in one of the four blocks: their masks as one of 64 bits
    movdqa xmm1, xmm5
    movdqa xmm2, xmm5
    movdqa xmm3, xmm5
    movdqa xmm4, xmm5
    pxor xmm1, XMMWORD PTR [rax-64]
    pxor xmm2, XMMWORD PTR [rax-48]
    pxor xmm3, XMMWORD PTR [rax-32]
    pxor xmm4, XMMWORD PTR [rax-16]
    pminub xmm1, XMMWORD PTR [rax-64]
    pminub xmm2, XMMWORD PTR [rax-48]
    pminub xmm3, XMMWORD PTR [rax-32]
    pminub xmm4, XMMWORD PTR [rax-16]
    pcmpeqb xmm1, xmm0
    pcmpeqb xmm2, xmm0
    pcmpeqb xmm3, xmm0
    pcmpeqb xmm4, xmm0
    pmovmskb ecx, xmm1
    pmovmskb edx, xmm2
    pmovmskb r9d, xmm3
    pmovmskb r10d, xmm4
    shl edx, 16
    shl r10d, 16
    or ecx, edx
    or r9d, r10d
    shl r9, 32
    or rcx, r9
    bsf rcx, rcx
    lea rax, [rax+rcx-64]
    cmp BYTE PTR [rax], r8b
    jne none
    ret
found:
    bsf edx, edx
    add rax, rdx
    cmp BYTE PTR [rax], r8b         ; c, or the NUL where c is not
    jne none
    ret
none:
    xor eax, eax
    ret
strchr ENDP

_TEXT ENDS

END
