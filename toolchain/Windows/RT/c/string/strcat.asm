; strcat.asm - strcat (grtc) (docs\Runtime_Design.md 10.2).
;
; Spec: ISO C 7.21.3.1 - src and its NUL copied over dst's NUL; dst answered. dst's end by strlen, then strcpy.
; Microsoft x64: rcx and rdx in, rax out.

EXTERN strlen:PROC
EXTERN strcpy:PROC

_TEXT SEGMENT

strcat PROC FRAME
    push rbx
    .PUSHREG rbx
    push rsi
    .PUSHREG rsi
    sub rsp, 40
    .ALLOCSTACK 40
    .ENDPROLOG
    mov rbx, rcx                    ; dst
    mov rsi, rdx                    ; src
    call strlen
    lea rcx, [rbx+rax]
    mov rdx, rsi
    call strcpy
    mov rax, rbx
    add rsp, 40
    pop rsi
    pop rbx
    ret
strcat ENDP

_TEXT ENDS

END
