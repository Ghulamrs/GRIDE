; Spec: ISO C 7.21.2.2 (memmove: as if through a temporary array) and SPRUFE8 3 (LDNDW/STNDW).
; A destination below the source, or apart from it, goes to memcpy.s, which reads every byte before
; a store could reach it; above it and overlapping, this copies from the end, block by block.
	.global	memmove
	.ref	memcpy
	.text

; memmove(A4 to, B4 from, A6 n) returns A4. Backwards: the first 32 bytes (each offset at most
; n - 8) read first, then 32-byte blocks from the end down, then the first bytes stored.
memmove:
	SUB	A4, B4, A0
||	CMPGTU	8, A6, A1
||	MV	A4, A3
	CMPLTU	A0, A6, A0
||	MVK	16, A9
||	SUB	A6, 8, A17
	[A1]	ZERO	A0
||	MVK	24, A16
||	MVK	8, A8
	[!A0]	B	memcpy
||	SHRU	A6, 5, A2
	CMPGT	A8, A17, A1
||	MVK	32, A7
	[A1]	MV	A17, A8
||	CMPGT	A9, A17, A1
	[A1]	MV	A17, A9
||	CMPGT	A16, A17, A1
	[A1]	MV	A17, A16
	ADD	B4, A8, B6
	ADD	B4, A9, B7
	ADD	B4, A16, B8
	ADD	B4, A6, B5
	LDNDW	*B4, A21:A20
||	SUB	B5, A7, B5
	LDNDW	*B6, A23:A22
||	ADD	A3, A6, A3
	LDNDW	*B7, A25:A24
||	SUB	A3, A7, A3
	LDNDW	*B8, A27:A26
||	[!A2]	B	mov_head
	NOP	5
mov_loop:
	LDNDW	*B5, B17:B16
||	SUB	A2, 1, A2
	LDNDW	*+B5(8), B19:B18
	LDNDW	*+B5(16), B21:B20
	LDNDW	*+B5(24), B23:B22
||	[A2]	B	mov_loop
||	SUB	B5, A7, B5
	NOP
	STNDW	B17:B16, *A3
	STNDW	B19:B18, *+A3(8)
	STNDW	B21:B20, *+A3(16)
	STNDW	B23:B22, *+A3(24)
||	SUB	A3, A7, A3
mov_head:
	ADD	A4, A8, A5
	STNDW	A21:A20, *A4
||	ADD	A4, A9, A18
	STNDW	A23:A22, *A5
||	ADD	A4, A16, A19
	STNDW	A25:A24, *A18
||	B	B3
	STNDW	A27:A26, *A19
	NOP	4
