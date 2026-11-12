; fill.asm - __grt_fill(p, value, n): n bytes of value (grtwin), the Debug heap's fill of a block it makes or frees.
; rep stosb: one instruction for any size, fast where the processor has ERMSB, and never the byte loop c90 writes at
; -O0, which made a Debug program's large allocations ten times slower than on Microsoft's debug heap. grtwin cannot
; call grtc's memset: it is the layer below it.

_TEXT SEGMENT

__grt_fill PROC FRAME
    push rdi
    .PUSHREG rdi
    .ENDPROLOG
    mov rdi, rcx
    mov eax, edx
    mov rcx, r8
    rep stosb
    pop rdi
    ret
__grt_fill ENDP

_TEXT ENDS
PUBLIC __grt_fill
END
