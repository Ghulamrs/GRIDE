; Spec: SPRAB89B 8.2 Table 8-6 and its text - __c6xabi_divremi: x / y in A4 and x % y in A5, as C has
; them; 8.3 Table 8-9 - it may change A1 A2 A4 A5 A6 B0 B1 B2 B4 B30 B31 and nothing else. The
; magnitudes divided as divremu does (SPRUFE8's SUBC); the quotient negated when one sign was set, the remainder when x's.
	.global	__c6xabi_divremi
	.text
__c6xabi_divremi:
	CMPGT	.L1	0, A4, A2
||	CMPGT	.L2	0, B4, B1
||	MVKL	.S2	divremi_last, B30
	[A2]	NEG	.L1	A4, A4
||	[B1]	NEG	.L2	B4, B4
||	XOR	.D1X	A2, B1, A6
||	MVKH	.S2	divremi_last, B30
; |x| < 2^31 but for INT_MIN: h its top bit (A1, A4 and B2), A5 m = |x| >> h (|x| even, so nothing lost).
	SHRU	.S1	A4, 31, A1
||	LMBD	.L2	1, B4, B0
	SHRU	.S1	A4, A1, A5
||	MV	.L2X	A1, B2
||	SHL	.S2	B4, B0, B31
||	MV	.D1	A1, A4
; B1 lz(m); A1 m < |y|, the small path; else k = lz(|y|) - lz(m), D = |y| << k and k+1 SUBCs.
	LMBD	.L2X	1, A5, B1
||	SUBAW	.D2	B30, B0, B30
||	CMPLTU	.L1X	A5, B4, A1
	ADDAW	.D2	B30, B1, B30
||	SUB	.L2	B0, B1, B0
||	SHRU	.S2	B31, B1, B31
; B0 s = k+1; B1 |y|; A1 the quotient's sign; A6 s + h; B4 dh = (|y| + h) >> h.
	[!A1]	B	.S2	B30
||	[A1]	B	.S1	divremi_small
||	ADD	.L2	B0, 1, B0
||	MV	.D2	B4, B1
	ADD	.L2	B4, B2, B4
||	MV	.D1	A6, A1
||	ADD	.L1X	A4, B0, A6
	SHRU	.S2	B4, B2, B4
	NOP	3
; Thirty-two SUBCs, the last k+1 of them run: m becomes r1 << s | q1, r1 = m % D and q1 = m / D.
; .align keeps asm6x from compressing this section, so each SUBC is one word and the entry is exact.
	.align	32
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
	SUBC	.L1X	A5, B31, A5
divremi_last:
	SUBC	.L1X	A5, B31, A5
||	B	.S2	B3
; |x| = 2^h m; c = r1 >= dh says r1 << h is |y| or more: |x| / |y| = (q1 << h) + c, and |x| % |y| is
; r1 << h, less |y| when c. Then the signs.
	SHRU	.S2X	A5, B0, B30
||	SHL	.S1	A5, A4, A5
	SHL	.S1X	B30, A6, A6
||	SHL	.S2	B30, B2, B31
||	CMPLTU	.L2	B30, B4, B0
	SUB	.L1	A5, A6, A4
||	[!B0]	SUB	.D2	B31, B1, B31
	[!B0]	ADD	.L1	A4, 1, A4
||	[A2]	NEG	.L2	B31, B31
	[A1]	NEG	.L1	A4, A4
||	MV	.S1X	B31, A5
; m < |y|: the quotient's magnitude is 1 when |x| >= |y|, else 0, and the remainder |x| or |x| - |y|.
divremi_small:
	B	.S2	B3
||	SHL	.S1	A5, A4, A5
	CMPGTU	.L2X	B1, A5, B0
||	MVK	.S1	1, A4
	[B0]	ZERO	.L1	A4
||	[!B0]	SUB	.S1X	A5, B1, A5
	[A1]	NEG	.L1	A4, A4
||	[A2]	NEG	.S1	A5, A5
	NOP	2
