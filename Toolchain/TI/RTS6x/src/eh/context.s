; Spec: SPRAB89B 3.1 (A10-A15, B10-B15 survive a call; B3 the return address; arguments A4, B4, A6,
; B6) and 11.5. The entry points that begin an unwind store the caller's registers into a context
; on their own stack at B15+8 - A10-A15, B10-B15, the return address - and pass it to the C++ that unwinds.
	.global	__cxa_throw
	.global	__cxa_rethrow
	.global	__cxa_end_cleanup
	.global	__rts6x_unwind_install
	.ref	__rts6x_throw
	.ref	__rts6x_rethrow
	.ref	__rts6x_end_cleanup
	.text


; __cxa_throw(object A4, type B4, destructor A6): rts6x's __rts6x_throw with the context fourth, in B6.
__cxa_throw:
	MV	B15, B5
	ADDK	-64, B15
	ADD	B15, 8, B7
	MV	B7, A8
	STW	A10, *+A8(0)
	STW	A11, *+A8(4)
	STW	A12, *+A8(8)
	STW	A13, *+A8(12)
	STW	A14, *+A8(16)
	STW	A15, *+A8(20)
	STW	B10, *+B7(24)
	STW	B11, *+B7(28)
	STW	B12, *+B7(32)
	STW	B13, *+B7(36)
	STW	B14, *+B7(40)
	STW	B5, *+B7(44)
	STW	B3, *+B7(48)
	MV	B7, B6
	CALLP	__rts6x_throw, B3
	NOP	5

; __cxa_rethrow(): __rts6x_rethrow(context).
__cxa_rethrow:
	MV	B15, B5
	ADDK	-64, B15
	ADD	B15, 8, B7
	MV	B7, A8
	STW	A10, *+A8(0)
	STW	A11, *+A8(4)
	STW	A12, *+A8(8)
	STW	A13, *+A8(12)
	STW	A14, *+A8(16)
	STW	A15, *+A8(20)
	STW	B10, *+B7(24)
	STW	B11, *+B7(28)
	STW	B12, *+B7(32)
	STW	B13, *+B7(36)
	STW	B14, *+B7(40)
	STW	B5, *+B7(44)
	STW	B3, *+B7(48)
	MV	A8, A4
	CALLP	__rts6x_rethrow, B3
	NOP	5

; __cxa_end_cleanup(): a cleanup pad's end; __rts6x_end_cleanup(context) goes on unwinding.
__cxa_end_cleanup:
	MV	B15, B5
	ADDK	-64, B15
	ADD	B15, 8, B7
	MV	B7, A8
	STW	A10, *+A8(0)
	STW	A11, *+A8(4)
	STW	A12, *+A8(8)
	STW	A13, *+A8(12)
	STW	A14, *+A8(16)
	STW	A15, *+A8(20)
	STW	B10, *+B7(24)
	STW	B11, *+B7(28)
	STW	B12, *+B7(32)
	STW	B13, *+B7(36)
	STW	B14, *+B7(40)
	STW	B5, *+B7(44)
	STW	B3, *+B7(48)
	MV	A8, A4
	CALLP	__rts6x_end_cleanup, B3
	NOP	5

; __rts6x_unwind_install(context A4, exception B4, target A6): the registers back, then a branch to
; the target with A4 the exception; nothing returns here.
__rts6x_unwind_install:
	MV	A4, B5
	MV	A6, B6
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
	NOP	4
	MV	B4, A4
	B	B6
	NOP	5
