; halt.asm - the process's end (grtwin).
;
; __rts6x_halt(status): ExitProcess(status). RTS6x's Termination calls it once exit's list has run, and
;   abort calls it with 0xC0000409, the fast fail's own exit code, which is taken as the sign to fail fast.
; __grt_fail_fast(): what a Release program built with the Microsoft runtime does for abort, a failed assert,
;   an uncaught exception and a pure virtual call: the fast fail (int 29h, ecx FAST_FAIL_FATAL_APP_EXIT, 7),
;   which Windows ends the process with, exit code 0xC0000409, no handler run.

EXTERN __imp_ExitProcess:QWORD

_TEXT SEGMENT

__rts6x_halt PROC FRAME
    sub rsp, 40
    .ALLOCSTACK 40
    .ENDPROLOG
    cmp ecx, 0C0000409h
    je fail
    call QWORD PTR __imp_ExitProcess
fail:
    mov ecx, 7
    int 29h
    int 3
__rts6x_halt ENDP

__grt_fail_fast PROC
    mov ecx, 7
    int 29h
    int 3
__grt_fail_fast ENDP

_TEXT ENDS
PUBLIC __rts6x_halt
PUBLIC __grt_fail_fast
END
