; strrchr.asm - strrchr (grtc), in SSE2 (docs\Runtime_Design.md 10.2).
;
; Spec: ISO C 7.21.5.5 - the last c (converted to char) in the string, the terminating NUL itself findable; a
; null pointer when c is not there.
;
; Every load is an aligned 16 bytes, which never crosses a page; in the first block the bytes before the start
; are masked off. The scan keeps the last block seen holding c and its mask of c's (r10, r11). In the block
; with the NUL, only the c's up to and including the NUL count: mask AND (z XOR (z - 1)), z the NUL's mask; the
; highest of them, if any, is the answer, else the highest in the kept block. The blocks go one at a time to a
; 64-byte boundary, then four at a time: min(x, x XOR c) folded by PMINUB says whether a group holds c or the
; NUL at all, and only such a group has its masks taken, as 64 bits. Microsoft x64: rcx and edx in, rax out;
; xmm0-xmm5 only.

_TEXT SEGMENT

strrchr PROC
    movzx edx, dl
    imul edx, edx, 01010101h
    movd xmm5, edx
    pshufd xmm5, xmm5, 0            ; c in every byte
    pxor xmm0, xmm0
    xor r10d, r10d                  ; the last block seen holding c (none yet) ...
    xor r11d, r11d                  ; ... and its c's
    mov rax, rcx
    and rax, -16                    ; the aligned block holding the first character
    and ecx, 15                     ; the start's place in it
    movdqa xmm1, XMMWORD PTR [rax]
    movdqa xmm2, xmm1
    pcmpeqb xmm1, xmm0
    pcmpeqb xmm2, xmm5
    pmovmskb edx, xmm1              ; the NULs
    pmovmskb r9d, xmm2              ; the c's
    shr edx, cl
    shl edx, cl                     ; the bytes before the start dropped, the rest in place
    shr r9d, cl
    shl r9d, cl
    test edx, edx
    jnz last
    test r9d, r9d
    cmovnz r10, rax
    cmovnz r11d, r9d
    add rax, 16
single:
    test al, 48                     ; a 64-byte boundary
    jz group
    movdqa xmm1, XMMWORD PTR [rax]
    movdqa xmm2, xmm1
    pcmpeqb xmm1, xmm0
    pcmpeqb xmm2, xmm5
    pmovmskb edx, xmm1
    pmovmskb r9d, xmm2
    test edx, edx
    jnz last
    test r9d, r9d
    cmovnz r10, rax
    cmovnz r11d, r9d
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
    pminub xmm1, xmm2
    movdqa xmm2, XMMWORD PTR [rax+32]
    movdqa xmm3, XMMWORD PTR [rax+48]
    movdqa xmm4, xmm5
    pxor xmm4, xmm2
    pminub xmm2, xmm4
    movdqa xmm4, xmm5
    pxor xmm4, xmm3
    pminub xmm3, xmm4
    pminub xmm2, xmm3
    pminub xmm1, xmm2
    add rax, 64
    pcmpeqb xmm1, xmm0
    pmovmskb edx, xmm1
    test edx, edx
    jz group
    ; the group holds c or the NUL: its NULs (rcx) and c's (r9) as 64 bits
    sub rax, 64
    movdqa xmm1, xmm0
    movdqa xmm2, xmm0
    movdqa xmm3, xmm0
    movdqa xmm4, xmm0
    pcmpeqb xmm1, XMMWORD PTR [rax]
    pcmpeqb xmm2, XMMWORD PTR [rax+16]
    pcmpeqb xmm3, XMMWORD PTR [rax+32]
    pcmpeqb xmm4, XMMWORD PTR [rax+48]
    pmovmskb ecx, xmm1
    pmovmskb edx, xmm2
    pmovmskb r8d, xmm3
    pmovmskb r9d, xmm4
    shl edx, 16
    shl r9d, 16
    or ecx, edx
    or r8d, r9d
    shl r8, 32
    or rcx, r8
    movdqa xmm1, xmm5
    movdqa xmm2, xmm5
    movdqa xmm3, xmm5
    movdqa xmm4, xmm5
    pcmpeqb xmm1, XMMWORD PTR [rax]
    pcmpeqb xmm2, XMMWORD PTR [rax+16]
    pcmpeqb xmm3, XMMWORD PTR [rax+32]
    pcmpeqb xmm4, XMMWORD PTR [rax+48]
    pmovmskb r9d, xmm1
    pmovmskb edx, xmm2
    shl edx, 16
    or r9d, edx
    pmovmskb r8d, xmm3
    pmovmskb edx, xmm4
    shl edx, 16
    or r8d, edx
    shl r8, 32
    or r9, r8
    mov rdx, rcx
    test rcx, rcx
    jnz last
    mov r10, rax                    ; c's only: kept, and on
    mov r11, r9
    add rax, 64
    jmp group
last:
    ; rax the block (or group) with the first NUL, rdx its NULs, r9 its c's
    lea rcx, [rdx-1]
    xor rdx, rcx                    ; every byte up to and including the first NUL
    and r9, rdx
    jz earlier
    bsr r9, r9
    add rax, r9
    ret
earlier:
    test r11, r11
    jz none
    bsr r11, r11
    lea rax, [r10+r11]
    ret
none:
    xor eax, eax
    ret
strrchr ENDP

_TEXT ENDS

END
