; Spec: the C$$IO$$ host channel as sim6747 serves it (src/C6xHost.cpp): a request in _CIOBUF_,
; then a call to C$$IO$$, where the host stops the program, answers in the buffer, and resumes.
; __rts6x_cio_trap is the same address under a name C++ can call; CioChannel.h says the layout.
	.global	_CIOBUF_
	.global	C$$IO$$
	.global	__rts6x_cio_trap
_CIOBUF_	.usect	".cio", 1056, 8
	.text
__rts6x_cio_trap:
C$$IO$$:
	B	B3
	NOP	5
