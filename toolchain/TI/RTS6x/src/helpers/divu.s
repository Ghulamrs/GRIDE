; Spec: SPRAB89B 8.2 Table 8-6 (__c6xabi_divu: uint32 x / y, x in A4, y in B4, the quotient in A4)
; and 8.3 Table 8-9 - it may change A0 A1 A2 A4 A6 B0 B1 B2 B4 B30 B31 and nothing else; SPRUFE8's
; SUBC, LMBD and delay slots. SUBC divides m = x >> h (h x's top bit, so no bit is shifted out).
	.global	__c6xabi_divu
	.text
__c6xabi_divu:
; A1 h; A4 m; A6 nh = x & h, the bit m drops; B0 lz(y); B30 the address of the last SUBC;
; B1 big = x >> 7 >= y (m >= 64y: k >= 6 below), A2 x >> 7 on the way, B1 y - 1 before it.
	CMPGT	.L1	0, A4, A1
||	LMBD	.L2	1, B4, B0
||	MVKL	.S2	divu_last, B30
||	SHRU	.S1	A4, 7, A2
||	SUB	.D2	B4, 1, B1
	SHRU	.S1	A4, A1, A4
||	MVKH	.S2	divu_last, B30
||	AND	.D1	A4, A1, A6
||	CMPLTU	.L2X	B1, A2, B1
; B2 lz(m); A0 m < y, the quotient a bit at most; else k = lz(y) - lz(m), D = y << k, k+1 SUBCs.
	LMBD	.L2X	1, A4, B2
||	SUBAW	.D2	B30, B0, B30
||	CMPLTU	.L1X	A4, B4, A0
||	SHL	.S2	B4, B0, B31
	ADDAW	.D2	B30, B2, B30
||	SHRU	.S2	B31, B2, B31
||	OR	.L1X	A0, B1, A2
||	SUB	.L2	B0, B2, B0
; Small: one branch; big: six SUBCs here, with the second branch; else the k+1 from B30.
	[A0]	B	.S1	divu_small
||	[!A2]	B	.S2	B30
||	ADD	.D2	B30, 24, B30
||	ADD	.D1X	B0, 1, A0
||	SUB	.L2X	B4, A6, B4
; A0 s = k+1; B4 y - nh; A6 s + h; B0 h; B2 dh = (y - nh + h) >> h.
	[B1]	B	.S2	B30
||	[B1]	SUBC	.L1X	A4, B31, A4
||	ADD	.D1	A0, A1, A6
||	MV	.L2X	A1, B0
	[B1]	SUBC	.L1X	A4, B31, A4
||	ADD	.D2	B4, B0, B2
	[B1]	SUBC	.L1X	A4, B31, A4
||	SHRU	.S2	B2, B0, B2
	[B1]	SUBC	.L1X	A4, B31, A4
	[B1]	SUBC	.L1X	A4, B31, A4
	[B1]	SUBC	.L1X	A4, B31, A4
; Thirty-two SUBCs, the last k+1 (or k-5) of them run: m becomes r1 << s | q1, r1 = m % D, q1 = m / D.
; .align keeps asm6x from compressing this section, so each SUBC is one word and the entry is exact.
	.align	32
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
	SUBC	.L1X	A4, B31, A4
divu_last:
	SUBC	.L1X	A4, B31, A4
||	B	.S2	B3
; x = 2^h m + nh, so x / y = (q1 << h) + 1 when r1 << h | nh >= y, that is r1 >= dh, else q1 << h.
	SHRU	.S1	A4, A0, A2
	SHL	.S1	A2, A6, A6
||	SHL	.S2X	A4, B0, B30
||	CMPLTU	.L1X	A2, B2, A1
	SUB	.S1X	B30, A6, A0
||	XOR	.L1	1, A1, A1
	ADD	.L1	A0, A1, A4
	NOP
; m < y: the quotient is 1 when x >= y, else 0; x >= y is m << h >= y - nh.
divu_small:
	B	.S2	B3
||	SHL	.S1	A4, A1, A4
	CMPLTU	.L1X	A4, B4, A1
	XOR	.L1	1, A1, A4
	NOP	3
