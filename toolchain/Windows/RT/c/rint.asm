; rint.asm - rint and nearbyint (grtc): x rounded to an integer in the rounding mode in force (docs\Runtime_Design.md
; 10.4), in assembly so that no compiler folds the sum that rounds it.
;
; Spec: ISO C99 7.12.9.4 rint and 7.12.9.3 nearbyint, F.9.6.3 and F.9.6.4: |x| >= 2^52 is already an integer (and an
; infinity is returned, a NaN made quiet by x + x); below that, x + 2^52 with x's sign rounds away its fraction,
; and subtracting it back is exact. The result keeps x's sign, so rint(-0.25) is -0. nearbyint gives the same
; value and puts back the processor's exception flags, so it raises no inexact.

PUBLIC rint
PUBLIC nearbyint

_TEXT SEGMENT

rint PROC
    movq rax, xmm0
    mov rdx, rax
    btr rdx, 63                     ; |x|
    mov rcx, 4330000000000000h      ; 2^52
    cmp rdx, rcx
    jae large
    mov r8, rax
    shr r8, 63
    shl r8, 63                      ; x's sign
    or rcx, r8                      ; 2^52 with x's sign
    movq xmm1, rcx
    addsd xmm0, xmm1
    subsd xmm0, xmm1
    movq rax, xmm0
    or rax, r8
    movq xmm0, rax
    ret
large:
    mov rcx, 7FF0000000000000h
    cmp rdx, rcx
    jbe done                        ; an integer already, or an infinity
    addsd xmm0, xmm0                ; a NaN, made quiet
done:
    ret
rint ENDP

nearbyint PROC FRAME
    sub rsp, 8
    .ALLOCSTACK 8
    .ENDPROLOG
    stmxcsr DWORD PTR [rsp]
    call rint
    ldmxcsr DWORD PTR [rsp]
    add rsp, 8
    ret
nearbyint ENDP

_TEXT ENDS

END
