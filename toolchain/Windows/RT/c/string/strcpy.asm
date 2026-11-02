; strcpy.asm - strcpy (grtc) (docs\Runtime_Design.md 10.2).
;
; Spec: ISO C 7.21.2.3 - the string and its NUL copied to dst; dst answered. The length is strlen's (SSE2), and
; the copy memcpy's (memory.asm), each the fastest the runtime has. Microsoft x64: rcx and rdx in, rax out.

EXTERN strlen:PROC
EXTERN memcpy:PROC

_TEXT SEGMENT

strcpy PROC FRAME
    push rbx
    .PUSHREG rbx
    push rsi
    .PUSHREG rsi
    sub rsp, 40
    .ALLOCSTACK 40
    .ENDPROLOG
    mov rbx, rcx                    ; dst
    mov rsi, rdx                    ; src
    mov rcx, rdx
    call strlen
    lea r8, [rax+1]
    mov rdx, rsi
    mov rcx, rbx
    call memcpy                     ; answers dst
    add rsp, 40
    pop rsi
    pop rbx
    ret
strcpy ENDP

_TEXT ENDS

END
