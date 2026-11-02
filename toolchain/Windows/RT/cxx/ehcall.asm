; ehcall.asm - the calls C++ exception handling makes that must be guarded (grtcxx), as vcruntime guards them with
; __try: a catch block, a destructor run while unwinding, a copy constructor building a catch's object.
;
; Each helper has unwind data of its own, written here by hand (GRIDE's masm takes no PROC FRAME:handler): one
; UWOP_ALLOC_SMALL for its 40-byte frame - the callee's 32 bytes of home and 8 for a pointer - and a language
; handler in cxx\exceptions.cpp that the dispatcher calls for every exception passing through the call:
; - __grt_call_catch(funclet, parentFrame, guard): the catch funclet called with rdx the parent's frame, as cpp11's
;   funclets take it; its answer, the address to continue at, in rax. guard (at [rsp+32]) is what the handler
;   __grt_catch_guard reads: a rethrow out of the catch is unwound to $catch_rethrow and raised again from here,
;   and any other way out of the catch ends it as vcruntime's __finally would.
; - __grt_call_or_terminate(function, a, b): function(a, b); a C++ exception out of it calls terminate
;   (__grt_terminate_guard): a destructor that throws while the stack unwinds, or a thrown object's destructor.
; - __grt_call_or_abort(function, a, b, c): function(a, b, c); any exception out of it is unwound to $abort_here,
;   which calls abort (__grt_abort_guard): a copy constructor building a catch's object, a terminate handler.
; Each call is followed by a nop: a return address on an epilog (add rsp; ret) would tell Windows' unwinder that
; the helper is already leaving, and it would not call the helper's handler.
; __grt_call_catch keeps rbp itself (UWOP_PUSH_NONVOL), as vcruntime's helper does, rather than trust the funclet to
; give it back: cpp11's funclets reserve only the 32 bytes of home, so a call with a fifth argument stores it over the
; rbp the funclet saved, and the funclet returns with rbp lost.
; .CRT$XIC holds __grt_eh_start, which sets the filter that sends an uncaught C++ exception to terminate.

OPTION DOTNAME
OPTION NOSCOPED

PUBLIC __grt_call_catch
PUBLIC __grt_call_or_terminate
PUBLIC __grt_call_or_abort
EXTERN __grt_catch_guard:PROC
EXTERN __grt_terminate_guard:PROC
EXTERN __grt_abort_guard:PROC
EXTERN __grt_rethrow_caught:PROC
EXTERN __grt_eh_start:PROC
EXTERN abort:PROC

.CODE
__grt_call_catch PROC
$catch_begin:
    push rbp
$catch_push:
    sub rsp, 48
$catch_prolog:
    mov QWORD PTR [rsp+32], r8
    mov rax, rcx
    call rax
    nop
    add rsp, 48
    pop rbp
    ret
$catch_rethrow:
    mov rcx, QWORD PTR [rsp+32]
    call __grt_rethrow_caught
    int 3
$catch_end:
__grt_call_catch ENDP

__grt_call_or_terminate PROC
$terminate_begin:
    sub rsp, 40
$terminate_prolog:
    mov rax, rcx
    mov rcx, rdx
    mov rdx, r8
    call rax
    nop
    add rsp, 40
    ret
$terminate_end:
__grt_call_or_terminate ENDP

__grt_call_or_abort PROC
$abort_begin:
    sub rsp, 40
$abort_prolog:
    mov rax, rcx
    mov rcx, rdx
    mov rdx, r8
    mov r8, r9
    call rax
    nop
    add rsp, 40
    ret
$abort_here:
    call abort
    int 3
$abort_end:
__grt_call_or_abort ENDP

.pdata SEGMENT READONLY ALIGN(4) 'DATA'
  DD imagerel $catch_begin
  DD imagerel $catch_end
  DD imagerel $catch_unwind
  DD imagerel $terminate_begin
  DD imagerel $terminate_end
  DD imagerel $terminate_unwind
  DD imagerel $abort_begin
  DD imagerel $abort_end
  DD imagerel $abort_unwind
.pdata ENDS

; version 1 with an exception and an unwind handler (019H); the prolog's size; the codes; no frame register; at the
; prolog's end UWOP_ALLOC_SMALL of 40 bytes (042H) - __grt_call_catch's two: UWOP_ALLOC_SMALL of 48 (052H), then
; after the push UWOP_PUSH_NONVOL rbp (050H) - a pad to an even count; the handler; its data
.xdata SEGMENT READONLY ALIGN(8) 'DATA'
$catch_unwind DB 019H
  DB $catch_prolog-$catch_begin
  DB 2
  DB 0
  DB $catch_prolog-$catch_begin
  DB 052H
  DB $catch_push-$catch_begin
  DB 050H
  DD imagerel __grt_catch_guard
  DD imagerel $catch_rethrow
$terminate_unwind DB 019H
  DB $terminate_prolog-$terminate_begin
  DB 1
  DB 0
  DB $terminate_prolog-$terminate_begin
  DB 042H
  DW 0
  DD imagerel __grt_terminate_guard
  DD 0
$abort_unwind DB 019H
  DB $abort_prolog-$abort_begin
  DB 1
  DB 0
  DB $abort_prolog-$abort_begin
  DB 042H
  DW 0
  DD imagerel __grt_abort_guard
  DD imagerel $abort_here
.xdata ENDS

.CRT$XIC SEGMENT READONLY ALIGN(8) 'DATA'
  DQ __grt_eh_start
.CRT$XIC ENDS

END
