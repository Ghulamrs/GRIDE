; Spec: ISO C 7.21.5.2 (strchr: the first c, the NUL itself findable) and SPRUFE8 3 (LDDW, CMPEQ4,
; LMBD). As strlen.s: 32 aligned bytes a turn, never past the block holding the NUL; here CMPEQ4 marks
; each byte equal to 0 or to c, and the first so marked is c (a pointer to it) or the end (null).
	.global	strchr
	.text

; strchr(A4 s, B4 c) returns A4. A5, B5 (= A5 + 8) walk the blocks; A7 masks bytes before s in the
; first; A20 and B20 are zero, A30 and B30 c in each byte, A27 c alone.
strchr:
	CLR	A4, 0, 4, A5
||	MV	A4, A3
||	MVK	-1, A7
||	EXTU	B4, 24, 24, B30
	EXTU	A4, 27, 27, A6
||	ADD	A5, 8, B5
||	ZERO	A20
||	SHL	B30, 8, B31
||	MV	B30, A27
	SHL	A7, A6, A29
||	OR	B30, B31, B30
||	MVK	1, A28
||	ZERO	B20
	SHL	B30, 16, B31
	OR	B30, B31, B30
	MV	B30, A30
chr_block:
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
	CMPEQ4	A16, A30, A16
||	CMPEQ4	B16, B30, B16
	CMPEQ4	A17, A20, A22
||	CMPEQ4	B17, B20, B22
||	OR	A21, A16, A21
||	OR	B21, B16, B21
	CMPEQ4	A17, A30, A17
||	CMPEQ4	B17, B30, B17
	CMPEQ4	A18, A20, A23
||	CMPEQ4	B18, B20, B23
||	OR	A22, A17, A22
||	OR	B22, B17, B22
	CMPEQ4	A18, A30, A18
||	CMPEQ4	B18, B30, B18
||	OR	A21, A22, A25
||	OR	B21, B22, B25
	CMPEQ4	A19, A20, A24
||	CMPEQ4	B19, B20, B24
||	OR	A23, A18, A23
||	OR	B23, B18, B23
	CMPEQ4	A19, A30, A19
||	CMPEQ4	B19, B30, B19
||	OR	A25, A23, A25
||	OR	B25, B23, B25
	OR	A24, A19, A24
||	OR	B24, B19, B24
	OR	A25, A24, A25
||	OR	B25, B24, B25
	OR	A25, B25, A1
	[!A1]	B	chr_block
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
; a marked byte in this block, past s: the first one is either c or the NUL
	AND	A21, A7, A1
	[!A1]	B	chr_block
||	NEG	A1, A8
	AND	A1, A8, A8
	LMBD	A28, A8, A9
||	SUB	A5, 1, A2
	SUB	A2, A9, A2
	[A1]	LDBU	*A2, A6
	NOP	4
	CMPEQ	A6, A27, A0
||	MV	A2, A4
||	B	B3
	[!A0]	ZERO	A4
	NOP	4
