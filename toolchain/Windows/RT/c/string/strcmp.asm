; strcmp.asm - strcmp (grtc), in SSE2 (docs\Runtime_Design.md 10.2).
;
; Spec: ISO C 7.21.4.2 - the first differing character, as unsigned char, decides. The value is the Microsoft
; runtime's: -1, 0 or 1.
;
; The two strings cannot both be aligned. a is: its first 16 are loaded unaligned (or compared a character at a
; time where either string's first 16 would run into its next page), then a goes on by aligned loads and b by
; unaligned ones, each of which stays in b's page: r9d counts the steps of 16 before b's would reach into its
; next page. That step goes a character at a time, which stops at the NUL, so b's next page is touched only when
; the string goes on there. A byte of min(a, a == b) is 0 where the two differ or a has the NUL; the steps go four
; at a time, folded by PMINUB, while r9d allows. Microsoft x64: rcx and rdx in, eax out; xmm0-xmm4 only.

_TEXT SEGMENT

strcmp PROC
    pxor xmm0, xmm0
    sub rdx, rcx                    ; b - a: b's character at [rcx+rdx] beside a's at [rcx]
    mov eax, ecx
    and eax, 4095
    cmp eax, 4080
    ja headbytes                    ; a's first 16 would reach into its next page
    lea eax, [rcx+rdx]
    and eax, 4095
    cmp eax, 4080
    ja headbytes                    ; b's
    movdqu xmm1, XMMWORD PTR [rcx]
    movdqu xmm2, XMMWORD PTR [rcx+rdx]
    pcmpeqb xmm2, xmm1              ; FF where the two agree
    pminub xmm2, xmm1               ; 0 where they differ or a's is the NUL
    pcmpeqb xmm2, xmm0
    pmovmskb eax, xmm2
    test eax, eax
    jnz found
    add rcx, 16
    and rcx, -16                    ; a on to its next 16-byte boundary, 1 to 16 characters on, all compared
    jmp aligned
headbytes:
    ; a character at a time to a's next 16-byte boundary
    test cl, 15
    jz aligned
    movzx eax, BYTE PTR [rcx]
    movzx r8d, BYTE PTR [rcx+rdx]
    cmp eax, r8d
    jne differ
    test eax, eax
    jz equal
    inc rcx
    jmp headbytes
aligned:
    ; a 16-aligned; r9d: the steps of 16 b takes before one would reach into its next page
    lea r9d, [rcx+rdx]
    and r9d, 4095
    neg r9d
    add r9d, 4096
    shr r9d, 4
    cmp r9d, 4
    jb few
group:
    movdqa xmm1, XMMWORD PTR [rcx]
    movdqu xmm2, XMMWORD PTR [rcx+rdx]
    pcmpeqb xmm2, xmm1
    pminub xmm2, xmm1
    movdqa xmm3, XMMWORD PTR [rcx+16]
    movdqu xmm4, XMMWORD PTR [rcx+rdx+16]
    pcmpeqb xmm4, xmm3
    pminub xmm4, xmm3
    pminub xmm2, xmm4               ; the first 32
    movdqa xmm1, XMMWORD PTR [rcx+32]
    movdqu xmm3, XMMWORD PTR [rcx+rdx+32]
    pcmpeqb xmm3, xmm1
    pminub xmm3, xmm1
    movdqa xmm1, XMMWORD PTR [rcx+48]
    movdqu xmm4, XMMWORD PTR [rcx+rdx+48]
    pcmpeqb xmm4, xmm1
    pminub xmm4, xmm1
    pminub xmm3, xmm4               ; the last 32
    pminub xmm2, xmm3
    pcmpeqb xmm2, xmm0
    pmovmskb eax, xmm2
    test eax, eax
    jnz groupfound
    add rcx, 64
    sub r9d, 4
    cmp r9d, 4
    jae group
few:
    test r9d, r9d
    jz cross
one:
    movdqa xmm1, XMMWORD PTR [rcx]
    movdqu xmm2, XMMWORD PTR [rcx+rdx]
    pcmpeqb xmm2, xmm1
    pminub xmm2, xmm1
    pcmpeqb xmm2, xmm0
    pmovmskb eax, xmm2
    test eax, eax
    jnz found
    add rcx, 16
    dec r9d
    jnz one
cross:
    ; b's next 16 would reach into its next page, unless b is at a page's start
    lea eax, [rcx+rdx]
    and eax, 4095
    cmp eax, 4080
    jbe aligned
    mov r9d, 16
crossbytes:
    movzx eax, BYTE PTR [rcx]
    movzx r8d, BYTE PTR [rcx+rdx]
    cmp eax, r8d
    jne differ
    test eax, eax
    jz equal
    inc rcx
    dec r9d
    jnz crossbytes
    jmp aligned
groupfound:
    mov r9d, 4                      ; in these four steps, all in b's page
    jmp one
found:
    bsf eax, eax
    add rcx, rax
    movzx eax, BYTE PTR [rcx]
    movzx r8d, BYTE PTR [rcx+rdx]
    cmp eax, r8d
    jne differ
equal:
    xor eax, eax
    ret
differ:
    sbb eax, eax                    ; -1 where a's character is below b's, else 0
    or eax, 1
    ret
strcmp ENDP

_TEXT ENDS

END
