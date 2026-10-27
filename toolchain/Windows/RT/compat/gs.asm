; gs.asm - what objects cl compiled with /GS (its default) call, so that a prebuilt library of cl's links
; with GRIDE's runtime (docs\Runtime_Design.md 13.4), in grtcompat.
;
; __security_cookie        the value cl's prologue XORs with rsp and keeps in the frame (the Microsoft
;                          runtime's default; it is not randomised at start-up here)
; __security_check_cookie  rcx the frame's value XORed back: a match returns, anything else is a smashed
;                          stack and ends the program by the fast fail FAST_FAIL_STACK_COOKIE_CHECK_FAILURE (2)
; __GSHandlerCheck         the exception handler of a /GS function: nothing to unwind here, so
;                          ExceptionContinueSearch (1)
; __report_rangecheckfailure  an index out of a checked array's bounds: the fast fail FAST_FAIL_RANGE_CHECK_FAILURE (8)

_DATA SEGMENT
__security_cookie DQ 00002B992DDFA232h
_DATA ENDS

_TEXT SEGMENT

__security_check_cookie PROC
    cmp rcx, QWORD PTR __security_cookie
    jne smashed
    ret
smashed:
    mov ecx, 2
    int 29h
    int 3
__security_check_cookie ENDP

__GSHandlerCheck PROC
    mov eax, 1
    ret
__GSHandlerCheck ENDP

__report_rangecheckfailure PROC
    mov ecx, 8
    int 29h
    int 3
__report_rangecheckfailure ENDP

_TEXT ENDS
PUBLIC __security_cookie
PUBLIC __security_check_cookie
PUBLIC __GSHandlerCheck
PUBLIC __report_rangecheckfailure
END
