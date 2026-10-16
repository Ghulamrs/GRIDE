; Spec: SPRAB89B 8.2 Table 8-4 (__c6xabi_divd: x in A5:A4 / y in B5:B4, the quotient in A5:A4) and
; IEEE 754 7.2-7.3 for normal operands and a normal result; everything else is handed, untouched, to
; FloatArithmetic::divideBinary64. How the quotient is guessed and made exact: docs/DIVISION.md.
	.global	__c6xabi_divd
	.global	_ZN5rts6x15FloatArithmetic14divideBinary64Edd
	.text
__c6xabi_divd:
; ex A0, ey B0; fractions' high words, nh A3 and dh B6; a = 1.fx in A9:A8, b = 1.fy in B9:B8, A7:A6.
	EXTU	.S1	A5, 1, 21, A0
||	EXTU	.S2	B5, 1, 21, B0
||	MV	.L1	A4, A8
||	MV	.L2	B4, B8
	CLR	.S1	A5, 20, 31, A3
||	CLR	.S2	B5, 20, 31, B6
||	ADD	.L1	-1, A0, A16
||	ADD	.L2	-1, B0, B16
||	SUB	.D1X	A0, B0, A22
	SET	.S1	A3, 20, 29, A9
||	SET	.S2	B6, 20, 29, B9
||	MV	.L1X	B4, A6
	SET	.S1	A3, 20, 20, A3
||	SET	.S2	B6, 20, 20, B6
||	MV	.L1X	B9, A7
; r0 = 1/b to 8 bits by RCPDP, on both sides; Q >= 1 (A19) is sx >= sy; f = ex - ey + 1022 + (Q >= 1).
	RCPDP	.S2	B9:B8, B23:B22
||	RCPDP	.S1	A7:A6, A7:A6
||	CMPGTU	.L1X	A3, B6, A19
	CMPEQ	.L1X	A3, B6, A20
||	MVK	.S1	2046, A17
||	MVK	.S2	2046, B17
	CMPLTU	.L1X	A4, B4, A21
||	ADDK	.S1	1022, A22
||	CMPLTU	.L2	B16, B17, B16
	XOR	.L1	1, A21, A21
	CMPLTU	.L1	A16, A17, A16
	AND	.L1	A20, A21, A20
	OR	.L1	A19, A20, A19
	ADD	.L1	A22, A19, A22
||	AND	.S1X	A16, B16, A16
	ADD	.L1	-1, A22, A23
	CMPLTU	.L1	A23, A17, A23
	AND	.L1	A16, A23, A1
; Not both normal, or a result that is not: the general path. Else t = 0: b r0 and Q0 = a r0.
	[!A1]	B	.S2	_ZN5rts6x15FloatArithmetic14divideBinary64Edd
||	[A1]	MPYDP	.M2	B9:B8, B23:B22, B25:B24
||	[A1]	MPYDP	.M1	A9:A8, A7:A6, A17:A16
	MVKL	.S1	0x3FF00000, A29
||	MVKL	.S2	0x3FF00000, B21
||	ZERO	.L1	A28
||	ZERO	.L2	B20
	MVKH	.S1	0x3FF00000, A29
||	MVKH	.S2	0x3FF00000, B21
||	ZERO	.L1	A30
||	XOR	.D1X	A5, B5, A25
	MVKL	.S1	0x40000000, A31
||	ADD	.L1	-1, A22, A24
	MVKH	.S1	0x40000000, A31
	SHL	.S1	A24, 20, A24
	CLR	.S1	A25, 0, 30, A25
; hiBase A24 = sign | (f - 1) << 20; want A25 = 1022 + (Q >= 1); top A26 = xl << (21 - (Q >= 1)).
	OR	.L1	A24, A25, A24
||	MVK	.S1	21, A27
	SUB	.L1	A27, A19, A27
||	ADDK	.S1	1022, A19
	SHL	.S1	A4, A27, A26
||	MV	.L1	A19, A25
; t = 10: e = 1 - b r0 (B27:B26) and F1 = 2 - b r0 (A19:A18).
	SUBDP	.L2	B21:B20, B25:B24, B27:B26
||	SUBDP	.S1X	A31:A30, B25:B24, A19:A18
	NOP	6
; t = 17: Q1 = Q0 F1 = a r0 (1 + e), and e^2.
	MPYDP	.M1	A17:A16, A19:A18, A17:A16
||	MPYDP	.M2	B27:B26, B27:B26, B29:B28
	NOP	9
; t = 27: F2 = 1 + e^2, and e^4; t = 34: Q2 = Q1 F2; t = 37: F3 = 1 + e^4.
	ADDDP	.S1X	A29:A28, B29:B28, A19:A18
||	MPYDP	.M2	B29:B28, B29:B28, B31:B30
	NOP	6
	MPYDP	.M1	A17:A16, A19:A18, A17:A16
	NOP	2
	ADDDP	.S1X	A29:A28, B31:B30, A21:A20
	NOP	6
; t = 44: Q3 = Q2 F3 = a r0 (1 - e^8) / (b r0) = (a/b)(1 - e^8), to a few ulps, in A17:A16.
	MPYDP	.M1	A17:A16, A21:A20, A17:A16
	NOP	9
; The guess m, A21:A16: Q3's significand, or the end of the range when Q3 lies across 1 from Q.
	EXTU	.S1	A17, 1, 21, A20
||	MV	.L1X	B6, A29
	CLR	.S1	A17, 20, 31, A21
||	MV	.L1X	B4, A28
	SET	.S1	A21, 20, 20, A21
||	CMPLTU	.L1	A20, A25, A1
	CMPGTU	.L1	A20, A25, A2
	[A1]	ZERO	.L1	A16
||	[A1]	MVK	.S1	0, A21
	[A1]	SET	.S1	A21, 20, 20, A21
	[A2]	MVK	.S1	-1, A16
	[A2]	MVK	.S1	-1, A21
	[A2]	CLR	.S1	A21, 21, 31, A21
; R = sx 2^s - m sy modulo 2^64 (A19:A18): top - mh dl - ml dh - hi(ml dl), and -lo(ml dl).
	MPY32U	.M1	A16, A28, A23:A22
	MPY32	.M1	A21, A28, A20
	MPY32	.M1	A16, A29, A27
	NOP	3
	SUB	.L1	A26, A23, A19
||	NEG	.S1	A22, A18
	SUB	.L1	A19, A20, A19
	SUB	.L1	A19, A27, A19
	CMPLTU	.L1	0, A22, A0
	SUB	.L1	A19, A0, A19
; While R < 0: m - 1, R + d.
divd_down:
	CMPGT	.L1	0, A19, A1
	[A1]	B	.S2	divd_down
||	[A1]	ADD	.L1	A18, A28, A18
||	[A1]	ADD	.D1	A19, A29, A19
||	[A1]	ADD	.S1	-1, A16, A16
	[A1]	CMPLTU	.L1	A18, A28, A0
	[A1]	ADD	.L1	A19, A0, A19
	[A1]	CMPEQ	.L1	-1, A16, A0
	[A1]	SUB	.L1	A21, A0, A21
	NOP	1
; While R >= d: m + 1, R - d.
divd_up:
	CMPGTU	.L1	A19, A29, A1
	CMPEQ	.L1	A19, A29, A2
	CMPLTU	.L1	A18, A28, A0
	XOR	.L1	1, A0, A20
	AND	.L1	A2, A20, A2
	OR	.L1	A1, A2, A1
	[A1]	B	.S2	divd_up
||	[A1]	SUB	.L1	A18, A28, A18
||	[A1]	SUB	.D1	A19, A29, A19
||	[A1]	ADD	.S1	1, A16, A16
	[A1]	SUB	.L1	A19, A0, A19
	[A1]	CMPEQ	.L1	0, A16, A0
	[A1]	ADD	.L1	A21, A0, A21
	NOP	2
; Round to nearest: up when 2R > d, or 2R = d and m is odd (ties to even).
	SHL	.S1	A19, 1, A20
||	ADD	.L1	A18, A18, A22
	SHRU	.S1	A18, 31, A23
	OR	.L1	A20, A23, A20
	CMPGTU	.L1	A20, A29, A1
	CMPEQ	.L1	A20, A29, A2
	CMPGTU	.L1	A22, A28, A0
	CMPEQ	.L1	A22, A28, A23
||	AND	.S1	1, A16, A27
	AND	.L1	A23, A27, A23
	OR	.L1	A0, A23, A0
	AND	.L1	A2, A0, A2
	OR	.L1	A1, A2, A1
	ADD	.L1	A16, A1, A16
	CMPLTU	.L1	A16, A1, A0
	ADD	.L1	A21, A0, A21
; A carry out of the significand (2^53) moves into the field; from 2046 that is infinity.
	B	.S2	B3
||	ADD	.L1	A24, A21, A5
||	MV	.D1	A16, A4
	NOP	5
