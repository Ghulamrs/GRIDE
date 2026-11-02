; memcmp.asm - memcmp (grtc), in SSE2 (docs\Runtime_Design.md 10.2).
;
; Spec: ISO C 7.21.4.1 - the first differing byte of the n, as unsigned char, decides. The value is the
; Microsoft runtime's: -1, 0 or 1.
;
; Both areas are the caller's for all n bytes, so loads go anywhere inside them, unaligned: below 16 bytes two
; overlapping loads of 8 or 4 from both ends (the first differing byte found by XOR and BSF), or bytes; from 16
; on, 64 and then 16 at a time, and the last 16 overlapping what is known equal. Microsoft x64: rcx, rdx and r8
; in, eax out; xmm0-xmm5 only.

_TEXT SEGMENT

memcmp PROC
    sub rdx, rcx                    ; b - a: b's byte at [rcx+rdx] beside a's at [rcx]
    cmp r8, 16
    jb small
    cmp r8, 64
    jb sixteen
group:
    movdqu xmm1, XMMWORD PTR [rcx]
    movdqu xmm2, XMMWORD PTR [rcx+rdx]
    movdqu xmm3, XMMWORD PTR [rcx+16]
    movdqu xmm4, XMMWORD PTR [rcx+rdx+16]
    pcmpeqb xmm1, xmm2
    pcmpeqb xmm3, xmm4
    movdqu xmm0, XMMWORD PTR [rcx+32]
    movdqu xmm2, XMMWORD PTR [rcx+rdx+32]
    movdqu xmm4, XMMWORD PTR [rcx+48]
    movdqu xmm5, XMMWORD PTR [rcx+rdx+48]
    pcmpeqb xmm0, xmm2
    pcmpeqb xmm4, xmm5
    pand xmm1, xmm3
    pand xmm0, xmm4
    pand xmm1, xmm0
    pmovmskb eax, xmm1
    cmp eax, 0FFFFh
    jne sixteen                     ; a difference in these 64: found 16 at a time
    add rcx, 64
    sub r8, 64
    cmp r8, 64
    jae group
sixteen:
    cmp r8, 16
    jb last
    movdqu xmm1, XMMWORD PTR [rcx]
    movdqu xmm2, XMMWORD PTR [rcx+rdx]
    pcmpeqb xmm1, xmm2
    pmovmskb eax, xmm1
    xor eax, 0FFFFh                 ; the differing bytes
    jnz differ
    add rcx, 16
    sub r8, 16
    jmp sixteen
last:
    ; 0 to 15 left, at least 16 compared: the last 16, overlapping bytes known equal
    test r8, r8
    jz equal
    lea rcx, [rcx+r8-16]
    movdqu xmm1, XMMWORD PTR [rcx]
    movdqu xmm2, XMMWORD PTR [rcx+rdx]
    pcmpeqb xmm1, xmm2
    pmovmskb eax, xmm1
    xor eax, 0FFFFh
    jnz differ
equal:
    xor eax, eax
    ret
differ:
    bsf eax, eax
    add rcx, rax
    movzx eax, BYTE PTR [rcx]
    movzx r8d, BYTE PTR [rcx+rdx]
    cmp eax, r8d
sign:
    sbb eax, eax                    ; -1 where a's byte is below b's, else 0
    or eax, 1
    ret
small:
    cmp r8, 8
    jb small8
    mov rax, QWORD PTR [rcx]
    mov r9, QWORD PTR [rcx+rdx]
    cmp rax, r9
    jne words
    lea rcx, [rcx+r8-8]
    mov rax, QWORD PTR [rcx]
    mov r9, QWORD PTR [rcx+rdx]
    cmp rax, r9
    jne words
    xor eax, eax
    ret
small8:
    cmp r8, 4
    jb small4
    mov eax, DWORD PTR [rcx]
    mov r9d, DWORD PTR [rcx+rdx]
    cmp eax, r9d
    jne words
    lea rcx, [rcx+r8-4]
    mov eax, DWORD PTR [rcx]
    mov r9d, DWORD PTR [rcx+rdx]
    cmp eax, r9d
    jne words
    xor eax, eax
    ret
words:
    ; rax and r9 the words loaded from rcx: the lowest differing byte is the first
    xor rax, r9
    bsf rax, rax
    shr eax, 3
    add rcx, rax
    movzx eax, BYTE PTR [rcx]
    movzx r8d, BYTE PTR [rcx+rdx]
    cmp eax, r8d
    jmp sign
small4:
    test r8, r8
    jz equal
bytes:
    movzx eax, BYTE PTR [rcx]
    movzx r9d, BYTE PTR [rcx+rdx]
    cmp eax, r9d
    jne sign
    inc rcx
    dec r8
    jnz bytes
    xor eax, eax
    ret
memcmp ENDP

_TEXT ENDS

END
