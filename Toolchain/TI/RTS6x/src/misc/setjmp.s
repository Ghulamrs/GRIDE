; Spec: ISO C 7.13 (setjmp returns 0, then longjmp's value - 1 for 0) and SPRAB89B 3.1 (A10-A15,
; B10-B14 and B15 survive a call; B3 the return address). jmp_buf words: A10-A15 at 0-20, B10-B14 at
; 24-40, B15 at 44, B3 at 48. env arrives in A4 and is copied to B5, so each file stores its own.
	.global	setjmp
	.global	longjmp
	.text
setjmp:
	MV	A4, B5
	STW	A10, *+A4(0)
	STW	A11, *+A4(4)
	STW	A12, *+A4(8)
	STW	A13, *+A4(12)
	STW	A14, *+A4(16)
	STW	A15, *+A4(20)
	STW	B10, *+B5(24)
	STW	B11, *+B5(28)
	STW	B12, *+B5(32)
	STW	B13, *+B5(36)
	STW	B14, *+B5(40)
	STW	B15, *+B5(44)
	STW	B3, *+B5(48)
	ZERO	A4
	B	B3
	NOP	5

; longjmp(env in A4, value in B4): every saved register back, then a return from setjmp with value.
longjmp:
	MV	A4, B5
	LDW	*+A4(0), A10
	LDW	*+A4(4), A11
	LDW	*+A4(8), A12
	LDW	*+A4(12), A13
	LDW	*+A4(16), A14
	LDW	*+A4(20), A15
	LDW	*+B5(24), B10
	LDW	*+B5(28), B11
	LDW	*+B5(32), B12
	LDW	*+B5(36), B13
	LDW	*+B5(40), B14
	LDW	*+B5(44), B15
	LDW	*+B5(48), B3
	MV	B4, B0
	NOP	3
	[!B0]	MVK	1, B4
	MV	B4, A4
	B	B3
	NOP	5
