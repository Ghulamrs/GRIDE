; tlsdata.asm - the tables of thread_local's dynamic initialisation (grtcxx), which cxx\threadlocal.cpp walks and
; registers: cpp11 cannot place a word in a section of its choosing.
; - __xd_a (.CRT$XDA) and __xd_z (.CRT$XDZ), null, bound each translation unit's initialiser in .CRT$XDU;
; - the TLS callbacks win\tls.asm's directory calls for each thread: __dyn_tls_init (.CRT$XLC) as it starts, after
;   grtwin's __grt_thread_attach (.CRT$XLB), and __grt_tls_end (.CRT$XLD) as it ends;
; - __dyn_tls_on_demand_init in .CRT$XCY, which start.c's walk of .CRT$XC* calls after every static object's
;   initialiser (.CRT$XCU): the first thread's thread_local objects, made before main.

OPTION DOTNAME

PUBLIC __xd_a
PUBLIC __xd_z
EXTERN __dyn_tls_init:PROC
EXTERN __dyn_tls_on_demand_init:PROC
EXTERN __grt_tls_end:PROC

.CRT$XDA SEGMENT READONLY ALIGN(8) 'DATA'
__xd_a DQ 0
.CRT$XDA ENDS

.CRT$XDZ SEGMENT READONLY ALIGN(8) 'DATA'
__xd_z DQ 0
.CRT$XDZ ENDS

.CRT$XLC SEGMENT READONLY ALIGN(8) 'DATA'
  DQ __dyn_tls_init
.CRT$XLC ENDS

.CRT$XLD SEGMENT READONLY ALIGN(8) 'DATA'
  DQ __grt_tls_end
.CRT$XLD ENDS

.CRT$XCY SEGMENT READONLY ALIGN(8) 'DATA'
  DQ __dyn_tls_on_demand_init
.CRT$XCY ENDS

END
