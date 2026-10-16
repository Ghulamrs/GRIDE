; Spec: ISO C 7.21.6.3 (strlen) and SPRUFE8 3 (LDDW, CMPEQ4, LMBD). The string is read 32 aligned
; bytes at a time - never past the 32-byte block holding its NUL - and CMPEQ4 marks each zero byte.
; A block with one is placed bit by bit; in the first, the bytes before the start are masked off.
	.global	strlen
	.text

; strlen(A4 s) returns the length in A4. A5 and B5 (= A5 + 8) walk the blocks; A7 masks the
; bytes before s in the first, -1 after it (A29 the next block's mask).
strlen:
	CLR	A4, 0, 4, A5
||	MV	A4, A3
||	MVK	-1, A7
||	ZERO	B20
	EXTU	A4, 27, 27, A6
||	ADD	A5, 8, B5
||	ZERO	A20
	SHL	A7, A6, A29
||	MVK	1, A28
len_block:
	LDDW	*A5, A17:A16
||	LDDW	*B5, B17:B16
||	MV	A29, A7
||	MVK	-1, A29
	LDDW	*+A5(16), A19:A18
||	LDDW	*+B5(16), B19:B18
	ADD	A5, 16, A5
||	ADD	B5, 16, B5
	ADD	A5, 16, A5
||	ADD	B5, 16, B5
	NOP	1
	CMPEQ4	A16, A20, A21
||	CMPEQ4	B16, B20, B21
	CMPEQ4	A17, A20, A22
||	CMPEQ4	B17, B20, B22
	CMPEQ4	A18, A20, A23
||	CMPEQ4	B18, B20, B23
||	OR	A21, A22, A25
||	OR	B21, B22, B25
	CMPEQ4	A19, A20, A24
||	CMPEQ4	B19, B20, B24
||	OR	A25, A23, A25
||	OR	B25, B23, B25
	OR	A25, A24, A25
||	OR	B25, B24, B25
	OR	A25, B25, A1
	[!A1]	B	len_block
||	SHL	A22, 4, A22
	SHL	A23, 16, A23
||	SHL	B21, 8, B21
||	OR	A21, A22, A21
	SHL	A24, 20, A24
||	SHL	B22, 12, B22
||	OR	A21, A23, A21
	SHL	B24, 28, A26
||	SHL	B23, 24, B23
||	OR	A21, A24, A21
||	OR	B21, B22, B21
	OR	B21, B23, B21
||	OR	A21, A26, A21
	OR	A21, B21, A21
; a zero byte in this block: its mask, less any bytes before s, picks the first
	AND	A21, A7, A1
	[!A1]	B	len_block
||	NEG	A1, A8
	[A1]	B	B3
||	AND	A1, A8, A8
	LMBD	A28, A8, A9
||	SUB	A5, A3, A2
	SUB	A2, A9, A2
	SUB	A2, 1, A4
	NOP	2
