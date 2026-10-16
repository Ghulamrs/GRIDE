; Spec: SPRAB89B 8.2 Table 8-6 (__c6xabi_remu: uint32 x % y, x in A4, y in B4, the remainder in A4)
; and 8.3 Table 8-9 - it may change A1 A4 A5 A7 B0 B1 B2 B4 B30 B31 and nothing else; SPRUFE8's
; SUBC, LMBD and delay slots. SUBC divides m = x >> h (h x's top bit, so no bit is shifted out).
	.global	__c6xabi_remu
	.text
__c6xabi_remu:
; A1 h; A5 m; A7 nh = x & h, the bit m drops; B0 lz(y); B30 the address of the last SUBC;
; B2 big = x >> 7 >= y (m >= 64y: k >= 6 below), A7 x >> 7 on the way, B1 y - 1 before it.
	CMPGT	.L1	0, A4, A1
||	LMBD	.L2	1, B4, B0
||	MVKL	.S2	remu_last, B30
||	SHRU	.S1	A4, 7, A7
||	SUB	.D2	B4, 1, B1
	SHRU	.S1	A4, A1, A5
||	MVKH	.S2	remu_last, B30
||	AND	.D1	A4, A1, A7
||	CMPLTU	.L2X	B1, A7, B2
||	MV	.L1	A1, A4
; B1 min(lz(m), lz(y)) = lz(m | y), so k = lz(y) - B1 >= 0 even when m < y: D = y << k, k+1 SUBCs.
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
; B0 s = k+1; B4 y - nh; B1 h.
	[B2]	B	.S2	B30
||	[B2]	SUBC	.L1X	A5, B31, A5
||	SUB	.D2X	B4, A7, B4
	[B2]	SUBC	.L1X	A5, B31, A5
||	MV	.L2X	A4, B1
	[B2]	SUBC	.L1X	A5, B31, A5
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
remu_last:
	SUBC	.L1X	A5, B31, A5
||	B	.S2	B3
; x = 2^h m + nh: x % y is r2 = r1 << h | nh, less y once when r2 >= y, that is r1 << h >= y - nh.
	SHRU	.S2X	A5, B0, B31
	SHL	.S2	B31, B1, B2
	OR	.S2X	B2, A7, B31
||	SUB	.D2	B2, B4, B30
||	CMPLTU	.L2	B2, B4, B0
	[!B0]	MV	.S2	B30, B31
	MV	.L1X	B31, A4
