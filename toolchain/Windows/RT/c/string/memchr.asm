; memchr.asm - memchr (grtc), in SSE2 (docs\Runtime_Design.md 10.2).
;
; Spec: ISO C 7.21.5.1 - the first of the n bytes equal to c (converted to unsigned char), or a null pointer.
;
; Every load is an aligned 16 bytes, which never crosses a page; in the first block the bytes before the start
; are masked off, and a find at or past the end (start + n, kept to the top of the address space when n runs past
; it) is no find. The blocks go one at a time to a 64-byte boundary, then four at a time while a group starts
; before the end: a 64-aligned group lies in one page, the page of a byte that is the caller's. Microsoft x64:
; rcx, edx and r8 in, rax out; xmm0-xmm5 only.

_TEXT SEGMENT

memchr PROC
    test r8, r8
    jz none
    movzx edx, dl
    imul edx, edx, 01010101h
    movd xmm5, edx
    pshufd xmm5, xmm5, 0            ; c in every byte
    mov r9, -1
    add r8, rcx                     ; the end
    cmovc r8, r9                    ; past the top of the address space: the top
    mov rax, rcx
    and rax, -16                    ; the aligned block holding the first byte
    and ecx, 15                     ; the start's place in it
    movdqa xmm1, xmm5
    pcmpeqb xmm1, XMMWORD PTR [rax]
    pmovmskb edx, xmm1
    shr edx, cl
    shl edx, cl                     ; the bytes before the start dropped, the rest in place
    test edx, edx
    jnz found
    add rax, 16
single:
    cmp rax, r8
    jae none
    test al, 48                     ; a 64-byte boundary
    jz group
    movdqa xmm1, xmm5
    pcmpeqb xmm1, XMMWORD PTR [rax]
    pmovmskb edx, xmm1
    test edx, edx
    jnz found
    add rax, 16
    jmp single
    ALIGN 16
group:
    movdqa xmm1, xmm5
    movdqa xmm2, xmm5
    movdqa xmm3, xmm5
    movdqa xmm4, xmm5
    pcmpeqb xmm1, XMMWORD PTR [rax]
    pcmpeqb xmm2, XMMWORD PTR [rax+16]
    pcmpeqb xmm3, XMMWORD PTR [rax+32]
    pcmpeqb xmm4, XMMWORD PTR [rax+48]
    movdqa xmm0, xmm1
    por xmm0, xmm2
    por xmm0, xmm3
    por xmm0, xmm4
    pmovmskb edx, xmm0
    test edx, edx
    jnz groupfound
    add rax, 64
    cmp rax, r8
    jb group
none:
    xor eax, eax
    ret
groupfound:
    ; their masks as one of 64 bits
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
    add rax, rcx
    cmp rax, r8
    jae none
    ret
found:
    bsf edx, edx
    add rax, rdx
    cmp rax, r8
    jae none
    ret
memchr ENDP

_TEXT ENDS

END
