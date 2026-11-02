; strncmp.asm - strncmp (grtc), in SSE2 (docs\Runtime_Design.md 10.2).
;
; Spec: ISO C 7.21.4.4 - at most n characters compared, as unsigned char, up to the first NUL. The value is the
; Microsoft runtime's: -1, 0 or 1.
;
; strcmp's walk (strcmp.asm says it): a by aligned loads, b by unaligned ones kept inside b's page by the step
; counter r9d, a character at a time where a page is crossed. r8 counts the characters left; a block with r8 at 16
; or fewer counts only its first r8 bytes, and the groups of four run only while more than 64 are left. Microsoft
; x64: rcx, rdx and r8 in, eax out; xmm0-xmm4 only.

_TEXT SEGMENT

strncmp PROC
    test r8, r8
    jz equal
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
    cmp r8, 16
    jbe limited
    test eax, eax
    jnz found
    lea r9, [rcx+16]
    and r9, -16                     ; a's next 16-byte boundary, 1 to 16 characters on, all compared
    sub r9, rcx
    add rcx, r9
    sub r8, r9
    jmp aligned
headbytes:
    ; a character at a time to a's next 16-byte boundary
    test cl, 15
    jz aligned
    movzx eax, BYTE PTR [rcx]
    movzx r9d, BYTE PTR [rcx+rdx]
    cmp eax, r9d
    jne differ
    test eax, eax
    jz equal
    inc rcx
    dec r8
    jz equal
    jmp headbytes
aligned:
    ; a 16-aligned, r8 at least 1; r9d: the steps of 16 b takes before one would reach into its next page
    lea r9d, [rcx+rdx]
    and r9d, 4095
    neg r9d
    add r9d, 4096
    shr r9d, 4
group:
    cmp r9d, 4
    jb one
    cmp r8, 64
    jbe one
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
    sub r8, 64
    sub r9d, 4
    jmp group
groupfound:
    mov r9d, 4                      ; in these four steps, all in b's page
one:
    test r9d, r9d
    jz cross
    movdqa xmm1, XMMWORD PTR [rcx]
    movdqu xmm2, XMMWORD PTR [rcx+rdx]
    pcmpeqb xmm2, xmm1
    pminub xmm2, xmm1
    pcmpeqb xmm2, xmm0
    pmovmskb eax, xmm2
    cmp r8, 16
    jbe limited
    test eax, eax
    jnz found
    add rcx, 16
    sub r8, 16
    dec r9d
    jmp group
cross:
    ; b's next 16 would reach into its next page, unless b is at a page's start
    lea eax, [rcx+rdx]
    and eax, 4095
    cmp eax, 4080
    jbe aligned
    mov r9d, 16
crossbytes:
    movzx eax, BYTE PTR [rcx]
    movzx r10d, BYTE PTR [rcx+rdx]
    cmp eax, r10d
    jne differ
    test eax, eax
    jz equal
    inc rcx
    dec r8
    jz equal
    dec r9d
    jnz crossbytes
    jmp aligned
limited:
    ; eax: where a block from rcx stops, of which only the first r8 (1 to 16) bytes count
    mov r10, rcx
    mov ecx, r8d
    mov r9d, 1
    shl r9d, cl
    dec r9d
    mov rcx, r10
    and eax, r9d
    jz equal
found:
    bsf eax, eax
    add rcx, rax
    movzx eax, BYTE PTR [rcx]
    movzx r9d, BYTE PTR [rcx+rdx]
    cmp eax, r9d
    jne differ
equal:
    xor eax, eax
    ret
differ:
    sbb eax, eax                    ; -1 where a's character is below b's, else 0
    or eax, 1
    ret
strncmp ENDP

_TEXT ENDS

END
