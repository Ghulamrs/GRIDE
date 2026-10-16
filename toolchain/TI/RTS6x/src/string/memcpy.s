; Spec: ISO C 7.21.2.1 (memcpy) and SPRUFE8 3 (LDNDW/STNDW, LDNW/STNW: doubleword and word access
; at any byte address). Every byte is read before the store that could reach it, so a forward
; overlap (destination below source) is safe too and memmove sends that case here.
	.global	memcpy
	.text

; memcpy(A4 to, B4 from, A6 n) returns A4. n >= 8: the last 32 bytes (each offset kept at 0 or
; more) are read first, then 32-byte blocks from the start, then the last bytes stored. A block's
; loads come before its stores; one non-aligned access per packet, as SPRUFE8 requires.
memcpy:
	CMPGTU	8, A6, A1
||	MV	A4, A3
||	MV	B4, B5
||	MVK	32, A7
	[A1]	B	cpy_small
||	SHRU	A6, 5, A2
	SUB	A6, A7, A8
||	SUB	A6, 8, A17
	ADD	A8, 8, A9
||	ADD	A8, 16, A16
	CMPGT	0, A8, A0
	[A0]	ZERO	A8
||	CMPGT	0, A9, A0
	[A0]	ZERO	A9
||	CMPGT	0, A16, A0
	[A0]	ZERO	A16
||	ADD	B5, A8, B6
	ADD	B5, A9, B7
	ADD	B5, A16, B8
	ADD	B5, A17, B9
	LDNDW	*B6, A21:A20
	LDNDW	*B7, A23:A22
	LDNDW	*B8, A25:A24
	LDNDW	*B9, A27:A26
||	[!A2]	B	cpy_tail
	NOP	5
cpy_loop:
	LDNDW	*B5, B17:B16
||	SUB	A2, 1, A2
	LDNDW	*+B5(8), B19:B18
	LDNDW	*+B5(16), B21:B20
	LDNDW	*+B5(24), B23:B22
||	[A2]	B	cpy_loop
||	ADD	B5, A7, B5
	NOP
	STNDW	B17:B16, *A3
	STNDW	B19:B18, *+A3(8)
	STNDW	B21:B20, *+A3(16)
	STNDW	B23:B22, *+A3(24)
||	ADD	A3, A7, A3
cpy_tail:
	ADD	A4, A8, A3
||	ADD	A4, A9, A5
	STNDW	A21:A20, *A3
||	ADD	A4, A16, A18
	STNDW	A23:A22, *A5
||	ADD	A4, A17, A19
	STNDW	A25:A24, *A18
||	B	B3
	STNDW	A27:A26, *A19
	NOP	4

; n < 8: 4 to 7 bytes are the first four and the last four; 1 to 3, bytes 0, n - 1 and n / 2.
cpy_small:
	CMPGTU	4, A6, A1
||	SUB	A6, 4, A7
||	MV	A6, B0
	[A1]	B	cpy_tiny
||	SUB	A6, 1, A8
||	SHRU	A6, 1, A9
	NOP	5
	LDNW	*B5, B16
||	ADD	B5, A7, B6
	LDNW	*B6, B17
||	ADD	A3, A7, A5
	NOP	4
	STNW	B16, *A3
||	B	B3
	STNW	B17, *A5
	NOP	4
cpy_tiny:
	[B0]	LDBU	*B5, B16
||	ADD	B5, A8, B6
||	ADD	A3, A8, A5
	[B0]	LDBU	*B6, B17
||	ADD	B5, A9, B7
||	ADD	A3, A9, A16
	[B0]	LDBU	*B7, B18
||	B	B3
	NOP	2
	[B0]	STB	B16, *A3
	[B0]	STB	B17, *A5
	[B0]	STB	B18, *A16
