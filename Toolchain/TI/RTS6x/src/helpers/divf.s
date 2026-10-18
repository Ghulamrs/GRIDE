; Spec: SPRAB89B 8.2 Table 8-4 (__c6xabi_divf: x in A4 / y in B4, the quotient in A4) and IEEE 754
; 7.2-7.3 for normal operands and a normal result; everything else is handed, untouched, to
; FloatArithmetic::divideBinary32. The method is divd.s's in binary32: docs/DIVISION.md.
	.global	__c6xabi_divf
	.global	_ZN5rts6x15FloatArithmetic14divideBinary32Eff
	.text
__c6xabi_divf:
; ex A0, ey B0; the significands n A3 and d B6; a = 1.fx in A9, b = 1.fy in B9 and A7.
	EXTU	.S1	A4, 1, 24, A0
||	EXTU	.S2	B4, 1, 24, B0
	CLR	.S1	A4, 23, 31, A3
||	CLR	.S2	B4, 23, 31, B6
||	ADD	.L1	-1, A0, A16
||	ADD	.L2	-1, B0, B16
||	SUB	.D1X	A0, B0, A22
	SET	.S1	A3, 23, 29, A9
||	SET	.S2	B6, 23, 29, B9
	SET	.S1	A3, 23, 23, A3
||	SET	.S2	B6, 23, 23, B6
||	MV	.L1X	B9, A7
; r0 = 1/b to 8 bits by RCPSP, on both sides; Q >= 1 (A19) is n >= d; f = ex - ey + 126 + (Q >= 1).
	RCPSP	.S1	A7, A7
||	RCPSP	.S2	B9, B23
||	CMPLTU	.L1X	A3, B6, A19
	MVK	.S1	254, A17
||	MVK	.S2	254, B17
||	XOR	.L1	1, A19, A19
	CMPLTU	.L1	A16, A17, A16
||	CMPLTU	.L2	B16, B17, B16
||	ADDK	.S1	126, A22
	ADD	.L1	A22, A19, A22
||	AND	.S1X	A16, B16, A16
	ADD	.L1	-1, A22, A23
	CMPLTU	.L1	A23, A17, A23
	AND	.L1	A16, A23, A1
; Not both normal, or a result that is not: the general path. Else t = 0: b r0 and Q0 = a r0.
	[!A1]	B	.S2	_ZN5rts6x15FloatArithmetic14divideBinary32Eff
||	[A1]	MPYSP	.M2	B9, B23, B25
||	[A1]	MPYSP	.M1	A9, A7, A17
	MVKL	.S1	0x40000000, A31
||	MVKL	.S2	0x3F800000, B21
||	XOR	.D1X	A4, B4, A25
	MVKH	.S1	0x40000000, A31
||	MVKH	.S2	0x3F800000, B21
||	ADD	.L1	-1, A22, A24
	MVKL	.S1	0x3F800000, A29
	SHL	.S1	A24, 23, A24
; t = 4: e = 1 - b r0 (B27) and F1 = 2 - b r0 (A18).
	SUBSP	.L2	B21, B25, B27
||	SUBSP	.L1X	A31, B25, A18
||	MVKH	.S1	0x3F800000, A29
; hiBase A24 = sign | (f - 1) << 23; want A19 = 126 + (Q >= 1); top A26 = n << (24 - (Q >= 1)).
	CLR	.S1	A25, 0, 30, A25
	OR	.L1	A24, A25, A24
||	MVK	.S1	24, A27
	SUB	.L1	A27, A19, A27
||	ADDK	.S1	126, A19
; t = 8: Q1 = Q0 F1 = a r0 (1 + e), and e^2; t = 12: F2 = 1 + e^2; t = 16: Q2 = (a/b)(1 - e^4).
	MPYSP	.M1	A17, A18, A17
||	MPYSP	.M2	B27, B27, B29
||	SHL	.S1	A3, A27, A26
	NOP	3
	ADDSP	.L1X	A29, B29, A18
	NOP	3
	MPYSP	.M1	A17, A18, A17
	NOP	3
; The guess m (A21): Q2's significand, or the end of the range when Q2 lies across 1 from Q.
	EXTU	.S1	A17, 1, 24, A20
||	MV	.L1X	B6, A28
	CLR	.S1	A17, 23, 31, A21
||	CMPLTU	.L1	A20, A19, A1
	SET	.S1	A21, 23, 23, A21
||	CMPGTU	.L1	A20, A19, A2
	[A1]	ZERO	.L1	A21
	[A1]	SET	.S1	A21, 23, 23, A21
	[A2]	MVK	.S1	-1, A21
	[A2]	CLR	.S1	A21, 24, 31, A21
; R = n 2^s - m d modulo 2^32, which is all of it (A16).
	MPY32	.M1	A21, A28, A20
	NOP	3
	SUB	.L1	A26, A20, A16
; While R < 0: m - 1, R + d; while R >= d: m + 1, R - d.
divf_down:
	CMPGT	.L1	0, A16, A1
	[A1]	B	.S2	divf_down
||	[A1]	ADD	.L1	A16, A28, A16
||	[A1]	ADD	.S1	-1, A21, A21
	NOP	5
divf_up:
	CMPLT	.L1	A16, A28, A1
	[!A1]	B	.S2	divf_up
||	[!A1]	SUB	.L1	A16, A28, A16
||	[!A1]	ADD	.S1	1, A21, A21
	NOP	5
; Round to nearest: up when 2R > d, or 2R = d and m is odd; a carry to 2^24 moves into the field.
	ADD	.L1	A16, A16, A22
	CMPGTU	.L1	A22, A28, A1
||	AND	.S1	1, A21, A23
	CMPEQ	.L1	A22, A28, A2
	AND	.L1	A2, A23, A2
	OR	.L1	A1, A2, A1
	B	.S2	B3
||	ADD	.L1	A21, A1, A21
	ADD	.L1	A24, A21, A4
	NOP	4
