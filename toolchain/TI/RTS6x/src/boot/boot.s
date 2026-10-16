; Spec: SPRAB89B 3 (B15 the stack pointer, 8-byte aligned, *B15 a free word; B14 the data page pointer;
; A4 the first argument) and sim6747's C$$EXIT stop, A4 the status. _c_int00 sets the two pointers
; and enters Startup; __rts6x_halt, which is C$$EXIT, is where every way of ending arrives.
; __rts6x_bounds: the tables Startup walks, as weak references - lnk6x, as TI's linker, defines
; them only when the image has the section, and a missing one is then 0, an empty table.
	.global	_c_int00
	.global	__rts6x_halt
	.global	C$$EXIT
	.ref	__rts6x_start
	.ref	__TI_STACK_END
	.ref	__TI_STATIC_BASE
	.global	__rts6x_bounds
	.weak	__TI_CINIT_Base
	.weak	__TI_CINIT_Limit
	.weak	__TI_Handler_Table_Base
	.weak	__TI_INITARRAY_Base
	.weak	__TI_INITARRAY_Limit
	.sect	".const"
	.align	4
__rts6x_bounds:
	.word	__TI_CINIT_Base
	.word	__TI_CINIT_Limit
	.word	__TI_Handler_Table_Base
	.word	__TI_INITARRAY_Base
	.word	__TI_INITARRAY_Limit
	.text
_c_int00:
	MVKL	__TI_STACK_END, B15
	MVKH	__TI_STACK_END, B15
	SUB	B15, 8, B15
	AND	-8, B15, B15
	MVKL	__TI_STATIC_BASE, B14
	MVKH	__TI_STATIC_BASE, B14
	MVKL	__rts6x_start, A3
	MVKH	__rts6x_start, A3
	B	A3
	NOP	5
__rts6x_halt:
C$$EXIT:
	B	C$$EXIT
	NOP	5
