; Spec: SPRAB89B 8.2 Table 8-6 and its text - __c6xabi_divremu: x / y in A4 and x % y in A5, unsigned;
; 8.3 Table 8-9 - it may change A0 A1 A2 A4 A6 B0 B1 B2 B4 B30 B31, and A5, which the text returns in;
; SPRUFE8's SUBC, LMBD and delay slots. SUBC divides m = x >> h (h x's top bit: no bit shifted out).
	.global	__c6xabi_divremu
	.text
__c6xabi_divremu:
; A1 h, A0 m, A5 nh = x & h (the bit m dropped), B0 lz(y), B30 the address of the last SUBC.
	SHRU	.S1	A4, 31, A1
||	LMBD	.L2	1, B4, B0
||	MVKL	.S2	divremu_last, B30
	SHRU	.S1	A4, A1, A0
||	MVKH	.S2	divremu_last, B30
||	AND	.D1	A4, A1, A5
; B2 lz(m); A2 m < y, the quotient no more than one bit; else k = lz(y) - lz(m) and k+1 SUBCs to run.
	LMBD	.L2X	1, A0, B2
||	SUBAW	.D2	B30, B0, B30
||	CMPLTU	.L1X	A0, B4, A2
	ADDAW	.D2	B30, B2, B30
||	SUB	.S2	B0, B2, B0
	[!A2]	B	.S2	B30
||	[A2]	B	.S1	divremu_small
; B31 D = y << k; A4 s = k+1; B4 y - nh; A6 s + h; B0 h; B1 dh = (y - nh + h) >> h.
	SHL	.S2	B4, B0, B31
||	ADD	.D1X	B0, 1, A4
||	SUB	.L2X	B4, A5, B4
	ADD	.L1	A4, A1, A6
||	MV	.L2X	A1, B0
	ADD	.D2	B4, B0, B1
	SHRU	.S2	B1, B0, B1
	NOP
; Thirty-two SUBCs, the last k+1 of them run: m becomes r1 << s | q1, r1 = m % D and q1 = m / D.
; .align keeps asm6x from compressing this section, so each SUBC is one word and the entry is exact.
	.align	32
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
	SUBC	.L1X	A0, B31, A0
divremu_last:
	SUBC	.L1X	A0, B31, A0
||	B	.S2	B3
; x = 2^h m + nh; c = r1 >= dh says r2 = r1 << h | nh is y or more: x / y = (q1 << h) + c and
; x % y = r2, less y when c.
	SHRU	.S1	A0, A4, A2
	SHL	.S1	A2, A6, A6
||	SHL	.S2X	A0, B0, B30
||	CMPLTU	.L1X	A2, B1, A1
	SUB	.S1X	B30, A6, A0
||	XOR	.L1	1, A1, A1
||	SHL	.S2X	A2, B0, B31
	ADD	.L1	A0, A1, A4
||	OR	.D1X	A5, B31, A5
||	SUB	.D2	B31, B4, B31
	[A1]	MV	.S1X	B31, A5
; m < y: the quotient is 1 when x >= y, else 0, and the remainder x or x - y; x is m << h | nh.
divremu_small:
	B	.S2	B3
||	SHL	.S1	A0, A1, A0
	CMPLTU	.L1X	A0, B4, A1
||	OR	.D1	A0, A5, A2
	XOR	.L1	1, A1, A4
||	SUB	.S1X	A0, B4, A6
	[!A1]	MV	.S1	A6, A2
	MV	.L1	A2, A5
	NOP
