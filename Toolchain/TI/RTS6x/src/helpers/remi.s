; Spec: SPRAB89B 8.2 Table 8-6 (__c6xabi_remi: int32 x % y, the sign of x, ISO C 6.5.5/6) and 8.3
; Table 8-9 - it may change A1 A2 A4 A5 A6 B0 B1 B2 B4 B30 B31 and nothing else. The magnitudes'
; remainder as remu finds it (SPRUFE8's SUBC), negated when x was negative (A2).
	.global	__c6xabi_remi
	.text
__c6xabi_remi:
	CMPGT	.L1	0, A4, A2
||	CMPGT	.L2	0, B4, B1
||	MVKL	.S2	remi_last, B30
	[A2]	NEG	.L1	A4, A4
||	[B1]	NEG	.L2	B4, B4
||	MVKH	.S2	remi_last, B30
; |x| < 2^31 but for INT_MIN: A1 and A4 h its top bit; A5 m = |x| >> h (|x| even, nothing lost);
; B0 lz(|y|); B2 big = |x| >> 7 >= |y| (k >= 6 below), A6 |x| >> 7 on the way, B1 |y| - 1 before it.
	CMPGT	.L1	0, A4, A1
||	LMBD	.L2	1, B4, B0
||	SHRU	.S1	A4, 7, A6
||	SUB	.D2	B4, 1, B1
	SHRU	.S1	A4, A1, A5
||	CMPLTU	.L2X	B1, A6, B2
||	MV	.L1	A1, A4
; B1 min(lz(m), lz(|y|)) = lz(m | |y|), so k = lz(|y|) - B1 >= 0 even when m < |y|: D = |y| << k.
	OR	.L1X	A5, B4, A1
||	SUBAW	.D2	B30, B0, B30
||	SHL	.S2	B4, B0, B31
	LMBD	.L2X	1, A1, B1
	ADDAW	.D2	B30, B1, B30
||	SUB	.L2	B0, B1, B0
||	SHRU	.S2	B31, B1, B31
; Big: six SUBCs here, with the second branch; else the k+1 from B30.
	[!B2]	B	.S2	B30
||	ADD	.D2	B30, 24, B30
||	ADD	.L2	B0, 1, B0
; B0 s = k+1; B1 h; B30 dh = (|y| + h) >> h.
	[B2]	B	.S2	B30
||	[B2]	SUBC	.L1X	A5, B31, A5
||	MV	.L2X	A4, B1
	[B2]	SUBC	.L1X	A5, B31, A5
||	ADD	.D2	B4, B1, B30
	[B2]	SUBC	.L1X	A5, B31, A5
||	SHRU	.S2	B30, B1, B30
	[B2]	SUBC	.L1X	A5, B31, A5
	[B2]	SUBC	.L1X	A5, B31, A5
	[B2]	SUBC	.L1X	A5, B31, A5
; Thirty-two SUBCs, the last k+1 (or k-5) of them run: m becomes r1 << s | q1, r1 = m % D, q1 = m / D.
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
remi_last:
	SUBC	.L1X	A5, B31, A5
||	B	.S2	B3
; |x| = 2^h m: |x| % |y| is r1 << h, less |y| once when r1 >= dh; then x's sign.
	SHRU	.S2X	A5, B0, B31
	SHL	.S2	B31, B1, B2
||	CMPLTU	.L2	B31, B30, B0
	[!B0]	SUB	.D2	B2, B4, B2
	[A2]	NEG	.L2	B2, B2
	MV	.L1X	B2, A4
