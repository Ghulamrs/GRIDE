; chkstk.asm - __chkstk, which a function whose frame passes a page calls before it moves rsp.
;
; Spec: the x64 calling convention - rax holds the bytes the caller will take; every page between
; the stack pointer and that far below it is touched in order, top down, so the guard page under
; the committed stack is met one page at a time and Windows grows the stack. Only r10, r11 and the
; flags may change: rax and the stack pointer come back as they were.

PUBLIC __chkstk

.CODE
__chkstk PROC
  mov r10, rsp
  add r10, 8                 ; the stack pointer the caller had before its call
  mov r11, rax
  cmp r11, 1000h
  jb $done
$probe:
  sub r10, 1000h
  cmp BYTE PTR [r10], 0      ; touch the page (a read; the flags are free to change)
  sub r11, 1000h
  cmp r11, 1000h
  jae $probe
$done:
  ret
__chkstk ENDP

END
