; fabs.asm - fabs (grtc): the sign bit cleared.
;
; Spec: ISO C 7.12.7.2 - fabs(x) is |x|: the sign bit cleared, NaNs and infinities included. RTS6x's
; math\fabs.cpp says the same; this is the two instructions it should be. fabs is also the x87 instruction's
; name, which ml64 reserves: OPTION NOKEYWORD frees it, as GRIDE's masm and ml64 both take it.

OPTION NOKEYWORD:<fabs>
PUBLIC fabs

.CODE
fabs PROC
  movq rax, xmm0
  btr rax, 63
  movq xmm0, rax
  ret
fabs ENDP

END
