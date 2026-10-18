; Spec: ISO C 7.21.4.2 (strcmp: the sign of the first difference, as unsigned char) and SPRUFE8 3
; (LDDW, CMPEQ4, LMBD). Two strings at the same offset in their doublewords are compared 8 aligned
; bytes a turn, never past the doubleword holding a NUL; any others a byte at a time.
	.global	strcmp
	.text

; strcmp(A4 a, B4 b) returns A4. A5, B5 walk the doublewords; a byte stops the compare when it
; differs or is a's NUL (CMPEQ4 marks), the first doubleword's bytes before a masked off by A7.
strcmp:
	XOR	A4, B4, A0
||	CLR	A4, 0, 2, A5
||	CLR	B4, 0, 2, B5
||	MVK	-1, A7
	AND	7, A0, A0
||	EXTU	A4, 29, 29, A6
||	ZERO	A20
||	ZERO	B20
	[A0]	B	cmp_bytes
||	SHL	A7, A6, A29
||	MVK	1, A28
	NOP	5
cmp_dw:
	LDDW	*A5, A17:A16
||	LDDW	*B5, B17:B16
||	MV	A29, A7
||	MVK	-1, A29
	ADD	A5, 8, A5
||	ADD	B5, 8, B5
	NOP	3
	CMPEQ4	A16, B16, A21
||	CMPEQ4	B17, A17, B21
	CMPEQ4	A16, A20, A22
||	CMPEQ4	A17, B20, B22
||	XOR	15, A21, A21
||	XOR	15, B21, B21
	OR	A21, A22, A21
||	OR	B21, B22, B21
	OR	A21, B21, A1
||	SHL	B21, 4, B21
	[!A1]	B	cmp_dw
||	OR	A21, B21, A21
	AND	A21, A7, A2
	NEG	A2, A8
	AND	A2, A8, A8
	LMBD	A28, A8, A6
	SUB	A5, A6, A9
||	SUB	B5, A6, B9
; a stop in this doubleword past a's start: the first such byte decides
	[A2]	LDBU	*+A9(23), A16
||	[A2]	LDBU	*+B9(23), B16
||	[!A2]	B	cmp_dw
	[A2]	B	B3
	NOP	3
	SUB	A16, B16, A4
	NOP	1

; different offsets: a byte at a time, until one differs or a ends
cmp_bytes:
	LDBU	*A4++, A16
||	LDBU	*B4++, B16
	NOP	4
	SUB	A16, B16, A2
||	CMPEQ	0, A16, A1
	OR	A2, A1, A0
	[!A0]	B	cmp_bytes
	NOP	5
	MV	A2, A4
||	B	B3
	NOP	5
