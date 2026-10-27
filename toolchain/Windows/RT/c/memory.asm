; memory.asm - memcpy, memmove and memset (grtc), tiered by size (docs\Runtime_Design.md 10.2), and the
; processor's features they read, found once at start-up (__grt_memory_start, called by mainCRTStartup).
;
; Spec: ISO C 7.21.2.1 memcpy, 7.21.2.2 memmove (the areas may overlap), 7.21.6.1 memset; each answers its
; first argument. Microsoft x64: rcx, rdx, r8 in, rax out; rdi and rsi are the caller's, so the rep forms
; live in their own procedures with unwind data and are reached by a jump.
;
;   n <= 16          scalar moves from both ends, which overlap: every load before any store
;   16 < n <= 64     unaligned SSE2 moves from both ends, every load before any store
;   64 < n <= 256    the areas apart: groups of four unaligned moves from both ends, no loop
;   larger, or near  memcpy, and memmove with the areas apart or the destination below: forward, by 64 bytes,
;                    the destination 16-aligned; from 32 KB rep movsb where the processor has fast
;                    strings (ERMSB). memmove with the destination inside the source, and a copy whose destination lies just above the
;                    source modulo 4 KB (4K aliasing): backward, by 64 bytes.
; memset the same way: the byte spread over a register and an xmm, no loop to 256 bytes, rep stosb from 32 KB with ERMSB.
; A memcpy whose areas overlap is undefined in C; this one copies as memmove does.

_DATA SEGMENT
__grt_ermsb DD 0                ; non-zero: rep movsb and rep stosb are fast (CPUID 7.0:EBX bit 9)
__grt_avx DD 0                  ; non-zero: 256-bit AVX moves, the processor's (CPUID 1:ECX bit 28) and Windows'
                                ; (OSXSAVE, bit 27, and XGETBV 0 saving the XMM and YMM state, bits 1 and 2)
_DATA ENDS

_TEXT SEGMENT

; ---- the processor's features, once
__grt_memory_start PROC FRAME
    push rbx
    .PUSHREG rbx
    .ENDPROLOG
    xor eax, eax
    cpuid
    mov r8d, eax                    ; the highest leaf
    mov eax, 1
    cpuid
    and ecx, 18000000h
    cmp ecx, 18000000h
    jne noavx
    xor ecx, ecx
    xgetbv
    and eax, 6
    cmp eax, 6
    jne noavx
    mov DWORD PTR __grt_avx, 1
noavx:
    cmp r8d, 7
    jb nofeatures
    mov eax, 7
    xor ecx, ecx
    cpuid
    shr ebx, 9
    and ebx, 1
    mov DWORD PTR __grt_ermsb, ebx
nofeatures:
    pop rbx
    ret
__grt_memory_start ENDP

; ---- memcpy and memmove: one body
memcpy PROC
    ; up to 16 bytes here, without the jump; the rest is memmove's body
    mov rax, rcx
    cmp r8, 16
    ja memmove
    cmp r8, 8
    jb cpyunder8
    mov r9, QWORD PTR [rdx]
    mov r10, QWORD PTR [rdx+r8-8]
    mov QWORD PTR [rcx], r9
    mov QWORD PTR [rcx+r8-8], r10
    ret
cpyunder8:
    cmp r8, 4
    jb cpyunder4
    mov r9d, DWORD PTR [rdx]
    mov r10d, DWORD PTR [rdx+r8-4]
    mov DWORD PTR [rcx], r9d
    mov DWORD PTR [rcx+r8-4], r10d
    ret
cpyunder4:
    test r8, r8
    je cpydone
    movzx r9d, BYTE PTR [rdx]
    movzx r10d, BYTE PTR [rdx+r8-1]
    cmp r8, 2
    jbe cpysmall2
    movzx r11d, BYTE PTR [rdx+1]
    mov BYTE PTR [rcx+1], r11b
cpysmall2:
    mov BYTE PTR [rcx], r9b
    mov BYTE PTR [rcx+r8-1], r10b
cpydone:
    ret
memcpy ENDP

memmove PROC
    mov rax, rcx
    cmp r8, 16
    ja over16
    cmp r8, 8
    jb under8
    mov r9, QWORD PTR [rdx]
    mov r10, QWORD PTR [rdx+r8-8]
    mov QWORD PTR [rcx], r9
    mov QWORD PTR [rcx+r8-8], r10
    ret
under8:
    cmp r8, 4
    jb under4
    mov r9d, DWORD PTR [rdx]
    mov r10d, DWORD PTR [rdx+r8-4]
    mov DWORD PTR [rcx], r9d
    mov DWORD PTR [rcx+r8-4], r10d
    ret
under4:
    test r8, r8
    je done
    movzx r9d, BYTE PTR [rdx]
    movzx r10d, BYTE PTR [rdx+r8-1]
    cmp r8, 2
    jbe small2
    movzx r11d, BYTE PTR [rdx+1]
    mov BYTE PTR [rcx+1], r11b
small2:
    mov BYTE PTR [rcx], r9b
    mov BYTE PTR [rcx+r8-1], r10b
done:
    ret
over16:
    cmp r8, 32
    ja over32
    movdqu xmm0, XMMWORD PTR [rdx]
    movdqu xmm1, XMMWORD PTR [rdx+r8-16]
    movdqu XMMWORD PTR [rcx], xmm0
    movdqu XMMWORD PTR [rcx+r8-16], xmm1
    ret
over32:
    cmp r8, 64
    ja over64
    movdqu xmm0, XMMWORD PTR [rdx]
    movdqu xmm1, XMMWORD PTR [rdx+16]
    movdqu xmm2, XMMWORD PTR [rdx+r8-32]
    movdqu xmm3, XMMWORD PTR [rdx+r8-16]
    movdqu XMMWORD PTR [rcx], xmm0
    movdqu XMMWORD PTR [rcx+16], xmm1
    movdqu XMMWORD PTR [rcx+r8-32], xmm2
    movdqu XMMWORD PTR [rcx+r8-16], xmm3
    ret
over64:
    ; 64 < n <= 256, the areas apart: groups of four moves, each group's loads before its stores (xmm0-xmm5
    ; only: xmm6-xmm15 are the caller's). Overlapping areas go to the loops below.
    cmp r8, 256
    ja large
    mov r9, rcx
    sub r9, rdx
    cmp r9, r8
    jb large
    mov r9, rdx
    sub r9, rcx
    cmp r9, r8
    jb large
    movdqu xmm0, XMMWORD PTR [rdx]
    movdqu xmm1, XMMWORD PTR [rdx+16]
    movdqu xmm2, XMMWORD PTR [rdx+32]
    movdqu xmm3, XMMWORD PTR [rdx+48]
    movdqu XMMWORD PTR [rcx], xmm0
    movdqu XMMWORD PTR [rcx+16], xmm1
    movdqu XMMWORD PTR [rcx+32], xmm2
    movdqu XMMWORD PTR [rcx+48], xmm3
    cmp r8, 128
    jbe last64
    movdqu xmm0, XMMWORD PTR [rdx+64]
    movdqu xmm1, XMMWORD PTR [rdx+80]
    movdqu xmm2, XMMWORD PTR [rdx+96]
    movdqu xmm3, XMMWORD PTR [rdx+112]
    movdqu XMMWORD PTR [rcx+64], xmm0
    movdqu XMMWORD PTR [rcx+80], xmm1
    movdqu XMMWORD PTR [rcx+96], xmm2
    movdqu XMMWORD PTR [rcx+112], xmm3
    movdqu xmm0, XMMWORD PTR [rdx+r8-128]
    movdqu xmm1, XMMWORD PTR [rdx+r8-112]
    movdqu xmm2, XMMWORD PTR [rdx+r8-96]
    movdqu xmm3, XMMWORD PTR [rdx+r8-80]
    movdqu XMMWORD PTR [rcx+r8-128], xmm0
    movdqu XMMWORD PTR [rcx+r8-112], xmm1
    movdqu XMMWORD PTR [rcx+r8-96], xmm2
    movdqu XMMWORD PTR [rcx+r8-80], xmm3
last64:
    movdqu xmm0, XMMWORD PTR [rdx+r8-64]
    movdqu xmm1, XMMWORD PTR [rdx+r8-48]
    movdqu xmm2, XMMWORD PTR [rdx+r8-32]
    movdqu xmm3, XMMWORD PTR [rdx+r8-16]
    movdqu XMMWORD PTR [rcx+r8-64], xmm0
    movdqu XMMWORD PTR [rcx+r8-48], xmm1
    movdqu XMMWORD PTR [rcx+r8-32], xmm2
    movdqu XMMWORD PTR [rcx+r8-16], xmm3
    ret
large:
    ; the destination inside the source (0 < dst - src < n): backward
    mov r9, rcx
    sub r9, rdx
    cmp r9, r8
    jb backward
    ; forward: the last 16 bytes are loaded first and stored last, so the loops stop short of the end
    movdqu xmm5, XMMWORD PTR [rdx+r8-16]
    lea r10, [rcx+r8-16]            ; where they go
    ; the source inside the destination (0 < src - dst < n): 16 at a time, each loaded before it is stored
    mov r9, rdx
    sub r9, rcx
    cmp r9, r8
    jb near16
    cmp r8, 32768
    jb apart
    cmp DWORD PTR __grt_ermsb, 0
    jne __grt_copy_strings
apart:
    ; a destination just above the source modulo 4 KB (by less than 256) would have each forward step's loads
    ; wait on the stores just made (4K aliasing): the areas are apart, so go backward instead
    mov r9, rcx
    sub r9, rdx
    and r9, 4095
    cmp r9, 256
    jb backward
aligned:
    cmp DWORD PTR __grt_avx, 0
    je alignedsse
    ; AVX: the head 32 bytes unaligned, then 128 and 32 at a time to a 32-aligned destination, then what is left
    ; by SSE2 and the kept tail. VEX instructions as bytes (GRIDE's masm has no VEX), each checked against ml64.
    DB 0C5h, 0FEh, 06Fh, 002h                       ; vmovdqu ymm0, YMMWORD PTR [rdx]
    DB 0C5h, 0FEh, 07Fh, 001h                       ; vmovdqu YMMWORD PTR [rcx], ymm0
    mov r11, rcx
    and r11, 31
    neg r11
    add r11, 32                     ; 1..32 bytes to the next 32-byte boundary
    add rcx, r11
    add rdx, r11
    sub r8, r11
avx128:
    cmp r8, 128
    jb avx32
    DB 0C5h, 0FEh, 06Fh, 002h                       ; vmovdqu ymm0, YMMWORD PTR [rdx]
    DB 0C5h, 0FEh, 06Fh, 04Ah, 020h                 ; vmovdqu ymm1, YMMWORD PTR [rdx+32]
    DB 0C5h, 0FEh, 06Fh, 052h, 040h                 ; vmovdqu ymm2, YMMWORD PTR [rdx+64]
    DB 0C5h, 0FEh, 06Fh, 05Ah, 060h                 ; vmovdqu ymm3, YMMWORD PTR [rdx+96]
    DB 0C5h, 0FDh, 07Fh, 001h                       ; vmovdqa YMMWORD PTR [rcx], ymm0
    DB 0C5h, 0FDh, 07Fh, 049h, 020h                 ; vmovdqa YMMWORD PTR [rcx+32], ymm1
    DB 0C5h, 0FDh, 07Fh, 051h, 040h                 ; vmovdqa YMMWORD PTR [rcx+64], ymm2
    DB 0C5h, 0FDh, 07Fh, 059h, 060h                 ; vmovdqa YMMWORD PTR [rcx+96], ymm3
    sub rcx, -128
    sub rdx, -128
    add r8, -128
    jmp avx128
avx32:
    cmp r8, 32
    jb avxdone
    DB 0C5h, 0FEh, 06Fh, 002h                       ; vmovdqu ymm0, YMMWORD PTR [rdx]
    DB 0C5h, 0FDh, 07Fh, 001h                       ; vmovdqa YMMWORD PTR [rcx], ymm0
    add rcx, 32
    add rdx, 32
    sub r8, 32
    jmp avx32
avxdone:
    DB 0C5h, 0F8h, 077h                             ; vzeroupper
    cmp r8, 16
    jbe tail
    movdqu xmm0, XMMWORD PTR [rdx]
    movdqu XMMWORD PTR [rcx], xmm0
    jmp tail
alignedsse:
    ; apart: the head stored unaligned, then 64 at a time to a 16-aligned destination
    movdqu xmm0, XMMWORD PTR [rdx]
    movdqu XMMWORD PTR [rcx], xmm0
    mov r11, rcx
    and r11, 15
    neg r11
    add r11, 16                     ; 1..16 bytes to the next 16-byte boundary
    add rcx, r11
    add rdx, r11
    sub r8, r11
loop64:
    cmp r8, 64
    jb loop16
    movdqu xmm0, XMMWORD PTR [rdx]
    movdqu xmm1, XMMWORD PTR [rdx+16]
    movdqu xmm2, XMMWORD PTR [rdx+32]
    movdqu xmm3, XMMWORD PTR [rdx+48]
    movdqa XMMWORD PTR [rcx], xmm0
    movdqa XMMWORD PTR [rcx+16], xmm1
    movdqa XMMWORD PTR [rcx+32], xmm2
    movdqa XMMWORD PTR [rcx+48], xmm3
    add rcx, 64
    add rdx, 64
    sub r8, 64
    jmp loop64
loop16:
    cmp r8, 16
    jb tail
    movdqu xmm0, XMMWORD PTR [rdx]
    movdqa XMMWORD PTR [rcx], xmm0
    add rcx, 16
    add rdx, 16
    sub r8, 16
    jmp loop16
near16:
    cmp r8, 16
    jbe tail
    movdqu xmm0, XMMWORD PTR [rdx]
    movdqu XMMWORD PTR [rcx], xmm0
    add rcx, 16
    add rdx, 16
    sub r8, 16
    jmp near16
tail:
    movdqu XMMWORD PTR [r10], xmm5
    ret
backward:
    ; the first 16 bytes kept first; then from the end down, 64 and then 16 at a time, each group's loads before
    ; its stores, while what is left lies above the first 16; then the last piece and the kept head. With the
    ; destination inside the source, a group's stores only reach source bytes already loaded.
    movdqu xmm5, XMMWORD PTR [rdx]
    cmp DWORD PTR __grt_avx, 0
    je back64
    cmp r8, 160
    jb back64
    ; AVX: 128 at a time from the ends down while all of a group lies above the first 16, then the SSE2 loops
    ; for the rest, from the starts again
    mov r9, rcx
    add rcx, r8
    add rdx, r8
    lea r10, [r9+144]
avxback:
    cmp rcx, r10
    jb avxbackend
    DB 0C5h, 0FEh, 06Fh, 042h, 0E0h                 ; vmovdqu ymm0, YMMWORD PTR [rdx-32]
    DB 0C5h, 0FEh, 06Fh, 04Ah, 0C0h                 ; vmovdqu ymm1, YMMWORD PTR [rdx-64]
    DB 0C5h, 0FEh, 06Fh, 052h, 0A0h                 ; vmovdqu ymm2, YMMWORD PTR [rdx-96]
    DB 0C5h, 0FEh, 06Fh, 05Ah, 080h                 ; vmovdqu ymm3, YMMWORD PTR [rdx-128]
    DB 0C5h, 0FEh, 07Fh, 041h, 0E0h                 ; vmovdqu YMMWORD PTR [rcx-32], ymm0
    DB 0C5h, 0FEh, 07Fh, 049h, 0C0h                 ; vmovdqu YMMWORD PTR [rcx-64], ymm1
    DB 0C5h, 0FEh, 07Fh, 051h, 0A0h                 ; vmovdqu YMMWORD PTR [rcx-96], ymm2
    DB 0C5h, 0FEh, 07Fh, 059h, 080h                 ; vmovdqu YMMWORD PTR [rcx-128], ymm3
    add rcx, -128
    add rdx, -128
    jmp avxback
avxbackend:
    DB 0C5h, 0F8h, 077h                             ; vzeroupper
    mov r8, rcx
    sub r8, r9
    mov rcx, r9
    sub rdx, r8
back64:
    cmp r8, 80
    jb back16
    movdqu xmm0, XMMWORD PTR [rdx+r8-16]
    movdqu xmm1, XMMWORD PTR [rdx+r8-32]
    movdqu xmm2, XMMWORD PTR [rdx+r8-48]
    movdqu xmm3, XMMWORD PTR [rdx+r8-64]
    movdqu XMMWORD PTR [rcx+r8-16], xmm0
    movdqu XMMWORD PTR [rcx+r8-32], xmm1
    movdqu XMMWORD PTR [rcx+r8-48], xmm2
    movdqu XMMWORD PTR [rcx+r8-64], xmm3
    sub r8, 64
    jmp back64
back16:
    cmp r8, 32
    jb backlast
    movdqu xmm0, XMMWORD PTR [rdx+r8-16]
    movdqu XMMWORD PTR [rcx+r8-16], xmm0
    sub r8, 16
    jmp back16
backlast:
    cmp r8, 16
    jbe backhead
    movdqu xmm0, XMMWORD PTR [rdx+r8-16]
    movdqu XMMWORD PTR [rcx+r8-16], xmm0
backhead:
    movdqu XMMWORD PTR [rcx], xmm5
    ret
memmove ENDP

; rep movsb for a large forward copy (rax already the destination)
__grt_copy_strings PROC FRAME
    push rdi
    .PUSHREG rdi
    push rsi
    .PUSHREG rsi
    .ENDPROLOG
    mov rdi, rcx
    mov rsi, rdx
    mov rcx, r8
    rep movsb
    pop rsi
    pop rdi
    ret
__grt_copy_strings ENDP

; ---- memset
memset PROC
    mov rax, rcx
    movzx edx, dl
    mov r9, 0101010101010101h
    imul rdx, r9                    ; the byte in every byte of rdx
    cmp r8, 16
    ja set16
    cmp r8, 8
    jb set8
    mov QWORD PTR [rcx], rdx
    mov QWORD PTR [rcx+r8-8], rdx
    ret
set8:
    cmp r8, 4
    jb set4
    mov DWORD PTR [rcx], edx
    mov DWORD PTR [rcx+r8-4], edx
    ret
set4:
    test r8, r8
    je setdone
    mov BYTE PTR [rcx], dl
    mov BYTE PTR [rcx+r8-1], dl
    cmp r8, 2
    jbe setdone
    mov BYTE PTR [rcx+1], dl
setdone:
    ret
set16:
    movq xmm0, rdx
    punpcklqdq xmm0, xmm0
    cmp r8, 32
    ja set32
    movdqu XMMWORD PTR [rcx], xmm0
    movdqu XMMWORD PTR [rcx+r8-16], xmm0
    ret
set32:
    cmp r8, 64
    ja set64
    movdqu XMMWORD PTR [rcx], xmm0
    movdqu XMMWORD PTR [rcx+16], xmm0
    movdqu XMMWORD PTR [rcx+r8-32], xmm0
    movdqu XMMWORD PTR [rcx+r8-16], xmm0
    ret
set64:
    cmp r8, 128
    ja set128
    movdqu XMMWORD PTR [rcx], xmm0
    movdqu XMMWORD PTR [rcx+16], xmm0
    movdqu XMMWORD PTR [rcx+32], xmm0
    movdqu XMMWORD PTR [rcx+48], xmm0
    movdqu XMMWORD PTR [rcx+r8-64], xmm0
    movdqu XMMWORD PTR [rcx+r8-48], xmm0
    movdqu XMMWORD PTR [rcx+r8-32], xmm0
    movdqu XMMWORD PTR [rcx+r8-16], xmm0
    ret
set128:
    cmp r8, 256
    ja set256
    movdqu XMMWORD PTR [rcx], xmm0
    movdqu XMMWORD PTR [rcx+16], xmm0
    movdqu XMMWORD PTR [rcx+32], xmm0
    movdqu XMMWORD PTR [rcx+48], xmm0
    movdqu XMMWORD PTR [rcx+64], xmm0
    movdqu XMMWORD PTR [rcx+80], xmm0
    movdqu XMMWORD PTR [rcx+96], xmm0
    movdqu XMMWORD PTR [rcx+112], xmm0
    movdqu XMMWORD PTR [rcx+r8-128], xmm0
    movdqu XMMWORD PTR [rcx+r8-112], xmm0
    movdqu XMMWORD PTR [rcx+r8-96], xmm0
    movdqu XMMWORD PTR [rcx+r8-80], xmm0
    movdqu XMMWORD PTR [rcx+r8-64], xmm0
    movdqu XMMWORD PTR [rcx+r8-48], xmm0
    movdqu XMMWORD PTR [rcx+r8-32], xmm0
    movdqu XMMWORD PTR [rcx+r8-16], xmm0
    ret
set256:
    cmp r8, 32768
    jb setavx
    cmp DWORD PTR __grt_ermsb, 0
    jne __grt_set_strings
setavx:
    cmp DWORD PTR __grt_avx, 0
    je setloop
    ; AVX: the head 32 and the last 128 unaligned, then 128 at a time to a 32-aligned destination up to them
    lea rdx, [rcx+r8]                               ; the end
    DB 0C4h, 0E3h, 07Dh, 018h, 0C0h, 001h           ; vinsertf128 ymm0, ymm0, xmm0, 1
    DB 0C5h, 0FEh, 07Fh, 001h                       ; vmovdqu YMMWORD PTR [rcx], ymm0
    DB 0C5h, 0FEh, 07Fh, 042h, 0E0h                 ; vmovdqu YMMWORD PTR [rdx-32], ymm0
    DB 0C5h, 0FEh, 07Fh, 042h, 0C0h                 ; vmovdqu YMMWORD PTR [rdx-64], ymm0
    DB 0C5h, 0FEh, 07Fh, 042h, 0A0h                 ; vmovdqu YMMWORD PTR [rdx-96], ymm0
    DB 0C5h, 0FEh, 07Fh, 042h, 080h                 ; vmovdqu YMMWORD PTR [rdx-128], ymm0
    lea r9, [rdx-128]
    add rcx, 32
    and rcx, -32
setavx128:
    cmp rcx, r9
    jae setavxend
    DB 0C5h, 0FDh, 07Fh, 001h                       ; vmovdqa YMMWORD PTR [rcx], ymm0
    DB 0C5h, 0FDh, 07Fh, 041h, 020h                 ; vmovdqa YMMWORD PTR [rcx+32], ymm0
    DB 0C5h, 0FDh, 07Fh, 041h, 040h                 ; vmovdqa YMMWORD PTR [rcx+64], ymm0
    DB 0C5h, 0FDh, 07Fh, 041h, 060h                 ; vmovdqa YMMWORD PTR [rcx+96], ymm0
    sub rcx, -128
    jmp setavx128
setavxend:
    DB 0C5h, 0F8h, 077h                             ; vzeroupper
    ret
setloop:
    ; head and the last 64 bytes unaligned, then 64 at a time to a 16-aligned destination up to them
    movdqu XMMWORD PTR [rcx], xmm0
    movdqu XMMWORD PTR [rcx+r8-16], xmm0
    movdqu XMMWORD PTR [rcx+r8-32], xmm0
    movdqu XMMWORD PTR [rcx+r8-48], xmm0
    movdqu XMMWORD PTR [rcx+r8-64], xmm0
    lea r9, [rcx+r8-64]             ; the loop stops before the tail stored above
    add rcx, 16
    and rcx, -16
setloop64:
    cmp rcx, r9
    jae setend
    movdqa XMMWORD PTR [rcx], xmm0
    movdqa XMMWORD PTR [rcx+16], xmm0
    movdqa XMMWORD PTR [rcx+32], xmm0
    movdqa XMMWORD PTR [rcx+48], xmm0
    add rcx, 64
    jmp setloop64
setend:
    ret
memset ENDP

; rep stosb for a large memset (rax already the destination, dl the byte)
__grt_set_strings PROC FRAME
    push rdi
    .PUSHREG rdi
    .ENDPROLOG
    mov rdi, rcx
    mov rcx, r8
    mov r9, rax
    mov eax, edx
    rep stosb
    mov rax, r9
    pop rdi
    ret
__grt_set_strings ENDP

_TEXT ENDS
PUBLIC memcpy
PUBLIC memmove
PUBLIC memset
PUBLIC __grt_memory_start
PUBLIC __grt_ermsb
PUBLIC __grt_avx
END
