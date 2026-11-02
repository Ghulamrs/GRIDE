; fma.asm - fma (grtc): x * y + z rounded once (ISO C99 7.12.13.1, F.9.10.1), as the Microsoft runtime gives it: the
; processor's fused multiply-add where it has one (__grt_fma, found by __grt_memory_start in c\memory.asm), else
; the exact sum of c\fmasoft.cpp. GRIDE's masm has no FMA mnemonics, so the instruction is written as its bytes.

EXTERN __grt_fma:DWORD
EXTERN __grt_fma_soft:PROC

PUBLIC fma

_TEXT SEGMENT

fma PROC
    cmp DWORD PTR __grt_fma, 0
    je soft
    DB 0C4h, 0E2h, 0F9h, 0B9h, 0D1h     ; vfmadd231sd xmm2, xmm0, xmm1: xmm2 = xmm0 * xmm1 + xmm2
    movapd xmm0, xmm2
    ret
soft:
    jmp __grt_fma_soft
fma ENDP

_TEXT ENDS

END
