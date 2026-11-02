; tls.asm - the TLS directory Windows' loader reads (grtwin), laid out as the Microsoft runtime's tlssup.obj lays
; it out. GRIDE's link points the image's TLS directory at _tls_used. It is here, in the lowest layer, because
; errno's thread_local (win\errno.cpp) and grtcxx's read _tls_index; mainCRTStartup links it into every program.
; - _tls_start (.tls) and _tls_end (.tls$ZZZ) bound the template the loader copies for each thread: every object's
;   .tls$ lies between them, and GRIDE's link measures a SECTIONREL into it from _tls_start's section.
; - _tls_index is the slot the loader gives the image in each thread's array (gs:[58h]).
; - The callbacks are the words between __xl_a (.CRT$XLA) and __xl_z (.CRT$XLZ), both null; the loader calls
;   each from the word after __xl_a up to the null. .CRT$XLB is __grt_thread_attach (win\thread.c), the backstop
;   of docs\Runtime_Design.md 9.6 for a thread not started by the runtime's CreateThread; grtcxx's thread_local
;   callbacks are .CRT$XLC and .CRT$XLD (cxx\tlsdata.asm).

OPTION DOTNAME

PUBLIC _tls_used
PUBLIC _tls_index
PUBLIC _tls_start
PUBLIC _tls_end
PUBLIC __xl_a
PUBLIC __xl_z
EXTERN __grt_thread_attach:PROC

.DATA
_tls_index DD 0

.tls SEGMENT ALIGN(16) 'DATA'
_tls_start DB 0
.tls ENDS

.tls$ZZZ SEGMENT ALIGN(1) 'DATA'
_tls_end DB 0
.tls$ZZZ ENDS

.CRT$XLA SEGMENT READONLY ALIGN(8) 'DATA'
__xl_a DQ 0
.CRT$XLA ENDS

.CRT$XLB SEGMENT READONLY ALIGN(8) 'DATA'
  DQ __grt_thread_attach
.CRT$XLB ENDS

.CRT$XLZ SEGMENT READONLY ALIGN(8) 'DATA'
__xl_z DQ 0
.CRT$XLZ ENDS

; IMAGE_TLS_DIRECTORY64: the template's start and end, the index's address, the callbacks' address, no zero
; fill, no characteristics
.rdata$T SEGMENT READONLY ALIGN(8) 'DATA'
_tls_used DQ _tls_start
  DQ _tls_end
  DQ _tls_index
  DQ __xl_a+8
  DD 0
  DD 0
.rdata$T ENDS

END
