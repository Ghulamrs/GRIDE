; entryguard.asm - the program's entry called under a language handler (grtstart): the Microsoft runtime's start-up
; filter, through which an access violation, an illegal instruction or a floating-point exception in the program's
; first thread reaches its SIGSEGV, SIGILL or SIGFPE handler (c\signals.cpp). __grt_call_entry(entry, a, b, c, d) is
; entry(a, b, c, d), whichever of the four entries that is; its unwind data, written by hand (GRIDE's masm takes no
; PROC FRAME:handler), name __grt_main_handler (start.c) for the search for a handler. A nop follows the call so that
; its return address is not on the epilog.

OPTION DOTNAME
OPTION NOSCOPED

PUBLIC __grt_call_entry
EXTERN __grt_main_handler:PROC

.CODE
__grt_call_entry PROC
$entry_begin:
    sub rsp, 40
$entry_prolog:
    mov rax, rcx
    mov rcx, rdx
    mov rdx, r8
    mov r8, r9
    mov r9, QWORD PTR [rsp+80]
    call rax
    nop
    add rsp, 40
    ret
$entry_end:
__grt_call_entry ENDP

.pdata SEGMENT READONLY ALIGN(4) 'DATA'
  DD imagerel $entry_begin
  DD imagerel $entry_end
  DD imagerel $entry_unwind
.pdata ENDS

; version 1 with an exception handler (09H); the prolog's size; one code; no frame register; at the prolog's end
; UWOP_ALLOC_SMALL of 40 bytes (042H); a pad to an even count; the handler
.xdata SEGMENT READONLY ALIGN(8) 'DATA'
$entry_unwind DB 09H
  DB $entry_prolog-$entry_begin
  DB 1
  DB 0
  DB $entry_prolog-$entry_begin
  DB 042H
  DW 0
  DD imagerel __grt_main_handler
  DD 0
.xdata ENDS

END
