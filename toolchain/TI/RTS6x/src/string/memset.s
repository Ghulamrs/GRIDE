; Spec: ISO C 7.21.6.1 (memset), SPRUFE8 3 (STDW; STNDW/STNW at any address). The byte spread over
; a doubleword: the first and last 8 bytes stored non-aligned, the doublewords between aligned, 64
; bytes a turn then 32, 16, 8 as the count's bits say; B8 is A3 + 8, two stores to a packet.
	.global	memset
	.text

; memset(A4 to, B4 value, A6 n) returns A4.
memset:
	EXTU	B4, 24, 24, B5
||	CMPGTU	8, A6, A1
||	MV	A4, A3
	SHL	B5, 8, B6
||	ADD	A4, A6, A8
||	MV	A6, B0
||	[A1]	B	set_small
	OR	B5, B6, B5
||	ADD	A4, 8, A9
	SHL	B5, 16, B6
||	SUB	A8, 8, A5
||	AND	-8, A9, A9
	OR	B5, B6, B5
||	AND	-8, A8, A8
||	MVK	64, A7
	MV	B5, B4
||	MV	B5, A16
||	SUB	A8, A9, A2
	MV	B5, A17
||	SHRU	A2, 3, A2

; n >= 8: A9 the first aligned doubleword, A2 how many there are to the last whole one
	STNDW	A17:A16, *A3
||	SHRU	A2, 3, B1
||	AND	7, A2, A2
	STNDW	A17:A16, *A5
||	[!B1]	B	set_rest
||	MV	A9, A3
||	ADD	A9, 8, B8
||	MV	B4, B5
	SUB	B1, 1, B1
||	MVK	64, B7
	NOP	4
set_loop:
	STDW	A17:A16, *A3
||	STDW	B5:B4, *B8
||	[B1]	B	set_loop
||	[B1]	ADD	-1, B1, B1
	STDW	A17:A16, *+A3(16)
||	STDW	B5:B4, *+B8(16)
	STDW	A17:A16, *+A3(32)
||	STDW	B5:B4, *+B8(32)
	STDW	A17:A16, *+A3(48)
||	STDW	B5:B4, *+B8(48)
||	ADD	A3, A7, A3
||	ADD	B8, B7, B8
	NOP	2
; 0 to 7 doublewords left: four, two and one as the bits of A2 say
set_rest:
	AND	4, A2, B0
||	AND	2, A2, A1
||	AND	1, A2, A0
	[B0]	STDW	A17:A16, *A3
||	[B0]	STDW	B5:B4, *B8
||	SHL	B0, 3, B6
	[B0]	STDW	A17:A16, *+A3(16)
||	[B0]	STDW	B5:B4, *+B8(16)
||	ADD	A3, B6, A3
||	ADD	B8, B6, B8
||	SHL	A1, 3, A6
	[A1]	STDW	A17:A16, *A3
||	[A1]	STDW	B5:B4, *B8
||	ADD	A3, A6, A3
||	B	B3
	[A0]	STDW	A17:A16, *A3
	NOP	4

; n < 8: 4 to 7 bytes are two words, the first four and the last four; 1 to 3, bytes 0, n - 1
; and n / 2, none at all for 0.
set_small:
	CMPGTU	4, A6, A1
||	SUB	A6, 1, A18
||	SHRU	A6, 1, A19
	[A1]	B	set_tiny
	NOP	5
	STNW	B5, *A3
||	ADD	A3, A6, A8
||	B	B3
	SUB	A8, 4, A8
	STNW	B5, *A8
	NOP	3
set_tiny:
	[B0]	STB	B5, *A3
||	ADD	A3, A18, A8
||	ADD	A3, A19, A20
||	B	B3
	[B0]	STB	B5, *A8
	[B0]	STB	B5, *A20
	NOP	3
