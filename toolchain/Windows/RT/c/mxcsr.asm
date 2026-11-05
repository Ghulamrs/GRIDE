; mxcsr.asm - the SSE control and status register for c\fenv.cpp (grtc): __grt_mxcsr() reads MXCSR, __grt_set_mxcsr(v)
; writes it. Both are leaves that use the caller's home space; GRIDE's masm has no mnemonics for the two, so they
; are written as their bytes.

PUBLIC __grt_mxcsr
PUBLIC __grt_set_mxcsr

_TEXT SEGMENT

__grt_mxcsr PROC
    DB 0Fh, 0AEh, 05Ch, 024h, 008h      ; stmxcsr DWORD PTR [rsp+8]
    mov eax, DWORD PTR [rsp+8]
    ret
__grt_mxcsr ENDP

__grt_set_mxcsr PROC
    mov DWORD PTR [rsp+8], ecx
    DB 0Fh, 0AEh, 054h, 024h, 008h      ; ldmxcsr DWORD PTR [rsp+8]
    ret
__grt_set_mxcsr ENDP

_TEXT ENDS

END
