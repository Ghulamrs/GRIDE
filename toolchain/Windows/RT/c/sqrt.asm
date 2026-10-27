; sqrt.asm - sqrt (grtc), the processor's own correctly rounded square root (docs\Runtime_Design.md 10.4).
;
; Spec: ISO C 7.12.7.5 and F.9.4.5, as the Microsoft runtime gives them: sqrt(x) for x < 0 is the default NaN
; (x86's, printed -nan(ind)) with errno EDOM (33); sqrt(-0) is -0, sqrt(+inf) is +inf, and a NaN comes back as
; it went in, with errno untouched. sqrtsd gives every one of those values; only the EDOM needs a call.

EXTERN _errno:PROC

_TEXT SEGMENT

sqrt PROC FRAME
    sub rsp, 40
    .ALLOCSTACK 40
    .ENDPROLOG
    movsd xmm1, xmm0
    sqrtsd xmm0, xmm0
    pxor xmm2, xmm2
    ucomisd xmm2, xmm1          ; 0 > x: above, and never for a NaN (unordered sets ZF, PF and CF)
    ja negative
    add rsp, 40
    ret
negative:
    movsd QWORD PTR [rsp+32], xmm0
    call _errno
    mov DWORD PTR [rax], 33
    movsd xmm0, QWORD PTR [rsp+32]
    add rsp, 40
    ret
sqrt ENDP

_TEXT ENDS
PUBLIC sqrt
END
