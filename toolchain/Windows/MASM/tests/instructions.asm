; instructions.asm - every form ml64 takes of the instructions GRIDE's Windows runtime needs (and the
; SSE ops beside them in masm's table), each checked byte for byte and relocation for relocation
; against ml64 by check.py. Written by gen_instructions.py, which says which operands ml64 takes.

EXTERN gb:BYTE
EXTERN gw:WORD
EXTERN gd:DWORD
EXTERN gq:QWORD
EXTERN gx:XMMWORD
EXTERN gf:REAL4
EXTERN gr8:REAL8
EXTERN gp:PROC

.DATA
lq DQ 0, 0, 0, 0
lb DB 0

.CODE

sse PROC
; pcmpeqb
  pcmpeqb xmm0, xmm15
  pcmpeqb xmm1, xmm9
  pcmpeqb xmm7, xmm8
  pcmpeqb xmm8, xmm7
  pcmpeqb xmm0, [rax]
  pcmpeqb xmm1, [rcx+8]
  pcmpeqb xmm7, [rdx+1000h]
  pcmpeqb xmm8, [rsp]
  pcmpeqb xmm9, [rsp+16]
  pcmpeqb xmm15, [rbp]
  pcmpeqb xmm0, [r13]
  pcmpeqb xmm1, [r12]
  pcmpeqb xmm7, [r12+40h]
  pcmpeqb xmm8, [rax+rbx*4]
  pcmpeqb xmm9, [r8+r9*8+16]
  pcmpeqb xmm15, [r15+r14*2-128]
  pcmpeqb xmm0, [rsi+r10+200h]
  pcmpeqb xmm1, [rdi*8+16]
  pcmpeqb xmm7, [rbx+rbp]
  pcmpeqb xmm8, [r11+rsp]
  pcmpeqb xmm9, [rax-8]
  pcmpeqb xmm15, XMMWORD PTR [r9+r10*4+7Fh]
  pcmpeqb xmm0, OWORD PTR [r9+r10*4+7Fh]
  pcmpeqb xmm1, BYTE PTR [r9+r10*4+7Fh]
  pcmpeqb xmm7, XMMWORD PTR [rax]
  pcmpeqb xmm8, OWORD PTR [rax]
  pcmpeqb xmm9, BYTE PTR [rax]
  pcmpeqb xmm15, gx
  pcmpeqb xmm0, gx+32
  pcmpeqb xmm1, OWORD PTR gq
  pcmpeqb xmm7, XMMWORD PTR gq+8
  pcmpeqb xmm8, OWORD PTR lq
  pcmpeqb xmm9, OWORD PTR lq+16
  pcmpeqb xmm15, gx[rcx]
  pcmpeqb xmm0, OWORD PTR lq[rax*8]
  pcmpeqb xmm1, [gx+16]
  pcmpeqb xmm7, gb
  pcmpeqb xmm8, gb+4
; pcmpeqw
  pcmpeqw xmm0, xmm15
  pcmpeqw xmm1, xmm9
  pcmpeqw xmm7, xmm8
  pcmpeqw xmm8, xmm7
  pcmpeqw xmm9, [rax]
  pcmpeqw xmm15, [rcx+8]
  pcmpeqw xmm0, [rdx+1000h]
  pcmpeqw xmm1, [rsp]
  pcmpeqw xmm7, [rsp+16]
  pcmpeqw xmm8, [rbp]
  pcmpeqw xmm9, [r13]
  pcmpeqw xmm15, [r12]
  pcmpeqw xmm0, [r12+40h]
  pcmpeqw xmm1, [rax+rbx*4]
  pcmpeqw xmm7, [r8+r9*8+16]
  pcmpeqw xmm8, [r15+r14*2-128]
  pcmpeqw xmm9, [rsi+r10+200h]
  pcmpeqw xmm15, [rdi*8+16]
  pcmpeqw xmm0, [rbx+rbp]
  pcmpeqw xmm1, [r11+rsp]
  pcmpeqw xmm7, [rax-8]
  pcmpeqw xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  pcmpeqw xmm9, OWORD PTR [r9+r10*4+7Fh]
  pcmpeqw xmm15, WORD PTR [r9+r10*4+7Fh]
  pcmpeqw xmm0, XMMWORD PTR [rax]
  pcmpeqw xmm1, OWORD PTR [rax]
  pcmpeqw xmm7, WORD PTR [rax]
  pcmpeqw xmm8, gx
  pcmpeqw xmm9, gx+32
  pcmpeqw xmm15, OWORD PTR gq
  pcmpeqw xmm0, XMMWORD PTR gq+8
  pcmpeqw xmm1, OWORD PTR lq
  pcmpeqw xmm7, OWORD PTR lq+16
  pcmpeqw xmm8, gx[rcx]
  pcmpeqw xmm9, OWORD PTR lq[rax*8]
  pcmpeqw xmm15, [gx+16]
  pcmpeqw xmm0, gw
  pcmpeqw xmm1, gw+4
; pcmpeqd
  pcmpeqd xmm0, xmm15
  pcmpeqd xmm1, xmm9
  pcmpeqd xmm7, xmm8
  pcmpeqd xmm8, xmm7
  pcmpeqd xmm7, [rax]
  pcmpeqd xmm8, [rcx+8]
  pcmpeqd xmm9, [rdx+1000h]
  pcmpeqd xmm15, [rsp]
  pcmpeqd xmm0, [rsp+16]
  pcmpeqd xmm1, [rbp]
  pcmpeqd xmm7, [r13]
  pcmpeqd xmm8, [r12]
  pcmpeqd xmm9, [r12+40h]
  pcmpeqd xmm15, [rax+rbx*4]
  pcmpeqd xmm0, [r8+r9*8+16]
  pcmpeqd xmm1, [r15+r14*2-128]
  pcmpeqd xmm7, [rsi+r10+200h]
  pcmpeqd xmm8, [rdi*8+16]
  pcmpeqd xmm9, [rbx+rbp]
  pcmpeqd xmm15, [r11+rsp]
  pcmpeqd xmm0, [rax-8]
  pcmpeqd xmm1, XMMWORD PTR [r9+r10*4+7Fh]
  pcmpeqd xmm7, OWORD PTR [r9+r10*4+7Fh]
  pcmpeqd xmm8, DWORD PTR [r9+r10*4+7Fh]
  pcmpeqd xmm9, XMMWORD PTR [rax]
  pcmpeqd xmm15, OWORD PTR [rax]
  pcmpeqd xmm0, DWORD PTR [rax]
  pcmpeqd xmm1, REAL4 PTR [rcx]
  pcmpeqd xmm7, gf
  pcmpeqd xmm8, gx
  pcmpeqd xmm9, gx+32
  pcmpeqd xmm15, OWORD PTR gq
  pcmpeqd xmm0, XMMWORD PTR gq+8
  pcmpeqd xmm1, OWORD PTR lq
  pcmpeqd xmm7, OWORD PTR lq+16
  pcmpeqd xmm8, gx[rcx]
  pcmpeqd xmm9, OWORD PTR lq[rax*8]
  pcmpeqd xmm15, [gx+16]
  pcmpeqd xmm0, gd
  pcmpeqd xmm1, gd+4
; pminub
  pminub xmm0, xmm15
  pminub xmm1, xmm9
  pminub xmm7, xmm8
  pminub xmm8, xmm7
  pminub xmm7, [rax]
  pminub xmm8, [rcx+8]
  pminub xmm9, [rdx+1000h]
  pminub xmm15, [rsp]
  pminub xmm0, [rsp+16]
  pminub xmm1, [rbp]
  pminub xmm7, [r13]
  pminub xmm8, [r12]
  pminub xmm9, [r12+40h]
  pminub xmm15, [rax+rbx*4]
  pminub xmm0, [r8+r9*8+16]
  pminub xmm1, [r15+r14*2-128]
  pminub xmm7, [rsi+r10+200h]
  pminub xmm8, [rdi*8+16]
  pminub xmm9, [rbx+rbp]
  pminub xmm15, [r11+rsp]
  pminub xmm0, [rax-8]
  pminub xmm1, XMMWORD PTR [r9+r10*4+7Fh]
  pminub xmm7, OWORD PTR [r9+r10*4+7Fh]
  pminub xmm8, BYTE PTR [r9+r10*4+7Fh]
  pminub xmm9, XMMWORD PTR [rax]
  pminub xmm15, OWORD PTR [rax]
  pminub xmm0, BYTE PTR [rax]
  pminub xmm1, gx
  pminub xmm7, gx+32
  pminub xmm8, OWORD PTR gq
  pminub xmm9, XMMWORD PTR gq+8
  pminub xmm15, OWORD PTR lq
  pminub xmm0, OWORD PTR lq+16
  pminub xmm1, gx[rcx]
  pminub xmm7, OWORD PTR lq[rax*8]
  pminub xmm8, [gx+16]
  pminub xmm9, gb
  pminub xmm15, gb+4
; pmaxub
  pmaxub xmm0, xmm15
  pmaxub xmm1, xmm9
  pmaxub xmm7, xmm8
  pmaxub xmm8, xmm7
  pmaxub xmm0, [rax]
  pmaxub xmm1, [rcx+8]
  pmaxub xmm7, [rdx+1000h]
  pmaxub xmm8, [rsp]
  pmaxub xmm9, [rsp+16]
  pmaxub xmm15, [rbp]
  pmaxub xmm0, [r13]
  pmaxub xmm1, [r12]
  pmaxub xmm7, [r12+40h]
  pmaxub xmm8, [rax+rbx*4]
  pmaxub xmm9, [r8+r9*8+16]
  pmaxub xmm15, [r15+r14*2-128]
  pmaxub xmm0, [rsi+r10+200h]
  pmaxub xmm1, [rdi*8+16]
  pmaxub xmm7, [rbx+rbp]
  pmaxub xmm8, [r11+rsp]
  pmaxub xmm9, [rax-8]
  pmaxub xmm15, XMMWORD PTR [r9+r10*4+7Fh]
  pmaxub xmm0, OWORD PTR [r9+r10*4+7Fh]
  pmaxub xmm1, BYTE PTR [r9+r10*4+7Fh]
  pmaxub xmm7, XMMWORD PTR [rax]
  pmaxub xmm8, OWORD PTR [rax]
  pmaxub xmm9, BYTE PTR [rax]
  pmaxub xmm15, gx
  pmaxub xmm0, gx+32
  pmaxub xmm1, OWORD PTR gq
  pmaxub xmm7, XMMWORD PTR gq+8
  pmaxub xmm8, OWORD PTR lq
  pmaxub xmm9, OWORD PTR lq+16
  pmaxub xmm15, gx[rcx]
  pmaxub xmm0, OWORD PTR lq[rax*8]
  pmaxub xmm1, [gx+16]
  pmaxub xmm7, gb
  pmaxub xmm8, gb+4
; psubb
  psubb xmm0, xmm15
  psubb xmm1, xmm9
  psubb xmm7, xmm8
  psubb xmm8, xmm7
  psubb xmm9, [rax]
  psubb xmm15, [rcx+8]
  psubb xmm0, [rdx+1000h]
  psubb xmm1, [rsp]
  psubb xmm7, [rsp+16]
  psubb xmm8, [rbp]
  psubb xmm9, [r13]
  psubb xmm15, [r12]
  psubb xmm0, [r12+40h]
  psubb xmm1, [rax+rbx*4]
  psubb xmm7, [r8+r9*8+16]
  psubb xmm8, [r15+r14*2-128]
  psubb xmm9, [rsi+r10+200h]
  psubb xmm15, [rdi*8+16]
  psubb xmm0, [rbx+rbp]
  psubb xmm1, [r11+rsp]
  psubb xmm7, [rax-8]
  psubb xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  psubb xmm9, OWORD PTR [r9+r10*4+7Fh]
  psubb xmm15, BYTE PTR [r9+r10*4+7Fh]
  psubb xmm0, XMMWORD PTR [rax]
  psubb xmm1, OWORD PTR [rax]
  psubb xmm7, BYTE PTR [rax]
  psubb xmm8, gx
  psubb xmm9, gx+32
  psubb xmm15, OWORD PTR gq
  psubb xmm0, XMMWORD PTR gq+8
  psubb xmm1, OWORD PTR lq
  psubb xmm7, OWORD PTR lq+16
  psubb xmm8, gx[rcx]
  psubb xmm9, OWORD PTR lq[rax*8]
  psubb xmm15, [gx+16]
  psubb xmm0, gb
  psubb xmm1, gb+4
; pandn
  pandn xmm0, xmm15
  pandn xmm1, xmm9
  pandn xmm7, xmm8
  pandn xmm8, xmm7
  pandn xmm7, [rax]
  pandn xmm8, [rcx+8]
  pandn xmm9, [rdx+1000h]
  pandn xmm15, [rsp]
  pandn xmm0, [rsp+16]
  pandn xmm1, [rbp]
  pandn xmm7, [r13]
  pandn xmm8, [r12]
  pandn xmm9, [r12+40h]
  pandn xmm15, [rax+rbx*4]
  pandn xmm0, [r8+r9*8+16]
  pandn xmm1, [r15+r14*2-128]
  pandn xmm7, [rsi+r10+200h]
  pandn xmm8, [rdi*8+16]
  pandn xmm9, [rbx+rbp]
  pandn xmm15, [r11+rsp]
  pandn xmm0, [rax-8]
  pandn xmm1, XMMWORD PTR [r9+r10*4+7Fh]
  pandn xmm7, OWORD PTR [r9+r10*4+7Fh]
  pandn xmm8, XMMWORD PTR [rax]
  pandn xmm9, OWORD PTR [rax]
  pandn xmm15, gx
  pandn xmm0, gx+32
  pandn xmm1, OWORD PTR gq
  pandn xmm7, XMMWORD PTR gq+8
  pandn xmm8, OWORD PTR lq
  pandn xmm9, OWORD PTR lq+16
  pandn xmm15, gx[rcx]
  pandn xmm0, OWORD PTR lq[rax*8]
  pandn xmm1, [gx+16]
; punpcklbw
  punpcklbw xmm0, xmm15
  punpcklbw xmm1, xmm9
  punpcklbw xmm7, xmm8
  punpcklbw xmm8, xmm7
  punpcklbw xmm7, [rax]
  punpcklbw xmm8, [rcx+8]
  punpcklbw xmm9, [rdx+1000h]
  punpcklbw xmm15, [rsp]
  punpcklbw xmm0, [rsp+16]
  punpcklbw xmm1, [rbp]
  punpcklbw xmm7, [r13]
  punpcklbw xmm8, [r12]
  punpcklbw xmm9, [r12+40h]
  punpcklbw xmm15, [rax+rbx*4]
  punpcklbw xmm0, [r8+r9*8+16]
  punpcklbw xmm1, [r15+r14*2-128]
  punpcklbw xmm7, [rsi+r10+200h]
  punpcklbw xmm8, [rdi*8+16]
  punpcklbw xmm9, [rbx+rbp]
  punpcklbw xmm15, [r11+rsp]
  punpcklbw xmm0, [rax-8]
  punpcklbw xmm1, XMMWORD PTR [r9+r10*4+7Fh]
  punpcklbw xmm7, OWORD PTR [r9+r10*4+7Fh]
  punpcklbw xmm8, BYTE PTR [r9+r10*4+7Fh]
  punpcklbw xmm9, XMMWORD PTR [rax]
  punpcklbw xmm15, OWORD PTR [rax]
  punpcklbw xmm0, BYTE PTR [rax]
  punpcklbw xmm1, gx
  punpcklbw xmm7, gx+32
  punpcklbw xmm8, OWORD PTR gq
  punpcklbw xmm9, XMMWORD PTR gq+8
  punpcklbw xmm15, OWORD PTR lq
  punpcklbw xmm0, OWORD PTR lq+16
  punpcklbw xmm1, gx[rcx]
  punpcklbw xmm7, OWORD PTR lq[rax*8]
  punpcklbw xmm8, [gx+16]
  punpcklbw xmm9, gb
  punpcklbw xmm15, gb+4
; punpcklwd
  punpcklwd xmm0, xmm15
  punpcklwd xmm1, xmm9
  punpcklwd xmm7, xmm8
  punpcklwd xmm8, xmm7
  punpcklwd xmm0, [rax]
  punpcklwd xmm1, [rcx+8]
  punpcklwd xmm7, [rdx+1000h]
  punpcklwd xmm8, [rsp]
  punpcklwd xmm9, [rsp+16]
  punpcklwd xmm15, [rbp]
  punpcklwd xmm0, [r13]
  punpcklwd xmm1, [r12]
  punpcklwd xmm7, [r12+40h]
  punpcklwd xmm8, [rax+rbx*4]
  punpcklwd xmm9, [r8+r9*8+16]
  punpcklwd xmm15, [r15+r14*2-128]
  punpcklwd xmm0, [rsi+r10+200h]
  punpcklwd xmm1, [rdi*8+16]
  punpcklwd xmm7, [rbx+rbp]
  punpcklwd xmm8, [r11+rsp]
  punpcklwd xmm9, [rax-8]
  punpcklwd xmm15, XMMWORD PTR [r9+r10*4+7Fh]
  punpcklwd xmm0, OWORD PTR [r9+r10*4+7Fh]
  punpcklwd xmm1, WORD PTR [r9+r10*4+7Fh]
  punpcklwd xmm7, XMMWORD PTR [rax]
  punpcklwd xmm8, OWORD PTR [rax]
  punpcklwd xmm9, WORD PTR [rax]
  punpcklwd xmm15, gx
  punpcklwd xmm0, gx+32
  punpcklwd xmm1, OWORD PTR gq
  punpcklwd xmm7, XMMWORD PTR gq+8
  punpcklwd xmm8, OWORD PTR lq
  punpcklwd xmm9, OWORD PTR lq+16
  punpcklwd xmm15, gx[rcx]
  punpcklwd xmm0, OWORD PTR lq[rax*8]
  punpcklwd xmm1, [gx+16]
  punpcklwd xmm7, gw
  punpcklwd xmm8, gw+4
; punpckldq
  punpckldq xmm0, xmm15
  punpckldq xmm1, xmm9
  punpckldq xmm7, xmm8
  punpckldq xmm8, xmm7
  punpckldq xmm9, [rax]
  punpckldq xmm15, [rcx+8]
  punpckldq xmm0, [rdx+1000h]
  punpckldq xmm1, [rsp]
  punpckldq xmm7, [rsp+16]
  punpckldq xmm8, [rbp]
  punpckldq xmm9, [r13]
  punpckldq xmm15, [r12]
  punpckldq xmm0, [r12+40h]
  punpckldq xmm1, [rax+rbx*4]
  punpckldq xmm7, [r8+r9*8+16]
  punpckldq xmm8, [r15+r14*2-128]
  punpckldq xmm9, [rsi+r10+200h]
  punpckldq xmm15, [rdi*8+16]
  punpckldq xmm0, [rbx+rbp]
  punpckldq xmm1, [r11+rsp]
  punpckldq xmm7, [rax-8]
  punpckldq xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  punpckldq xmm9, OWORD PTR [r9+r10*4+7Fh]
  punpckldq xmm15, DWORD PTR [r9+r10*4+7Fh]
  punpckldq xmm0, XMMWORD PTR [rax]
  punpckldq xmm1, OWORD PTR [rax]
  punpckldq xmm7, DWORD PTR [rax]
  punpckldq xmm8, REAL4 PTR [rcx]
  punpckldq xmm9, gf
  punpckldq xmm15, gx
  punpckldq xmm0, gx+32
  punpckldq xmm1, OWORD PTR gq
  punpckldq xmm7, XMMWORD PTR gq+8
  punpckldq xmm8, OWORD PTR lq
  punpckldq xmm9, OWORD PTR lq+16
  punpckldq xmm15, gx[rcx]
  punpckldq xmm0, OWORD PTR lq[rax*8]
  punpckldq xmm1, [gx+16]
  punpckldq xmm7, gd
  punpckldq xmm8, gd+4
; punpcklqdq
  punpcklqdq xmm0, xmm15
  punpcklqdq xmm1, xmm9
  punpcklqdq xmm7, xmm8
  punpcklqdq xmm8, xmm7
  punpcklqdq xmm9, [rax]
  punpcklqdq xmm15, [rcx+8]
  punpcklqdq xmm0, [rdx+1000h]
  punpcklqdq xmm1, [rsp]
  punpcklqdq xmm7, [rsp+16]
  punpcklqdq xmm8, [rbp]
  punpcklqdq xmm9, [r13]
  punpcklqdq xmm15, [r12]
  punpcklqdq xmm0, [r12+40h]
  punpcklqdq xmm1, [rax+rbx*4]
  punpcklqdq xmm7, [r8+r9*8+16]
  punpcklqdq xmm8, [r15+r14*2-128]
  punpcklqdq xmm9, [rsi+r10+200h]
  punpcklqdq xmm15, [rdi*8+16]
  punpcklqdq xmm0, [rbx+rbp]
  punpcklqdq xmm1, [r11+rsp]
  punpcklqdq xmm7, [rax-8]
  punpcklqdq xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  punpcklqdq xmm9, OWORD PTR [r9+r10*4+7Fh]
  punpcklqdq xmm15, QWORD PTR [r9+r10*4+7Fh]
  punpcklqdq xmm0, XMMWORD PTR [rax]
  punpcklqdq xmm1, OWORD PTR [rax]
  punpcklqdq xmm7, QWORD PTR [rax]
  punpcklqdq xmm8, REAL8 PTR [rcx]
  punpcklqdq xmm9, gr8
  punpcklqdq xmm15, gx
  punpcklqdq xmm0, gx+32
  punpcklqdq xmm1, OWORD PTR gq
  punpcklqdq xmm7, XMMWORD PTR gq+8
  punpcklqdq xmm8, OWORD PTR lq
  punpcklqdq xmm9, OWORD PTR lq+16
  punpcklqdq xmm15, gx[rcx]
  punpcklqdq xmm0, OWORD PTR lq[rax*8]
  punpcklqdq xmm1, [gx+16]
  punpcklqdq xmm7, gq
  punpcklqdq xmm8, gq+4
; movups
  movups xmm0, xmm15
  movups xmm1, xmm9
  movups xmm7, xmm8
  movups xmm8, xmm7
  movups xmm9, [rax]
  movups xmm15, [rcx+8]
  movups xmm0, [rdx+1000h]
  movups xmm1, [rsp]
  movups xmm7, [rsp+16]
  movups xmm8, [rbp]
  movups xmm9, [r13]
  movups xmm15, [r12]
  movups xmm0, [r12+40h]
  movups xmm1, [rax+rbx*4]
  movups xmm7, [r8+r9*8+16]
  movups xmm8, [r15+r14*2-128]
  movups xmm9, [rsi+r10+200h]
  movups xmm15, [rdi*8+16]
  movups xmm0, [rbx+rbp]
  movups xmm1, [r11+rsp]
  movups xmm7, [rax-8]
  movups xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  movups xmm9, OWORD PTR [r9+r10*4+7Fh]
  movups xmm15, DWORD PTR [r9+r10*4+7Fh]
  movups xmm0, XMMWORD PTR [rax]
  movups xmm1, OWORD PTR [rax]
  movups xmm7, DWORD PTR [rax]
  movups xmm8, REAL4 PTR [rcx]
  movups xmm9, gf
  movups xmm15, gx
  movups xmm0, gx+32
  movups xmm1, OWORD PTR gq
  movups xmm7, XMMWORD PTR gq+8
  movups xmm8, OWORD PTR lq
  movups xmm9, OWORD PTR lq+16
  movups xmm15, gx[rcx]
  movups xmm0, OWORD PTR lq[rax*8]
  movups xmm1, [gx+16]
  movups xmm7, gd
  movups xmm8, gd+4
; movaps
  movaps xmm0, xmm15
  movaps xmm1, xmm9
  movaps xmm7, xmm8
  movaps xmm8, xmm7
  movaps xmm9, [rax]
  movaps xmm15, [rcx+8]
  movaps xmm0, [rdx+1000h]
  movaps xmm1, [rsp]
  movaps xmm7, [rsp+16]
  movaps xmm8, [rbp]
  movaps xmm9, [r13]
  movaps xmm15, [r12]
  movaps xmm0, [r12+40h]
  movaps xmm1, [rax+rbx*4]
  movaps xmm7, [r8+r9*8+16]
  movaps xmm8, [r15+r14*2-128]
  movaps xmm9, [rsi+r10+200h]
  movaps xmm15, [rdi*8+16]
  movaps xmm0, [rbx+rbp]
  movaps xmm1, [r11+rsp]
  movaps xmm7, [rax-8]
  movaps xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  movaps xmm9, OWORD PTR [r9+r10*4+7Fh]
  movaps xmm15, DWORD PTR [r9+r10*4+7Fh]
  movaps xmm0, XMMWORD PTR [rax]
  movaps xmm1, OWORD PTR [rax]
  movaps xmm7, DWORD PTR [rax]
  movaps xmm8, REAL4 PTR [rcx]
  movaps xmm9, gf
  movaps xmm15, gx
  movaps xmm0, gx+32
  movaps xmm1, OWORD PTR gq
  movaps xmm7, XMMWORD PTR gq+8
  movaps xmm8, OWORD PTR lq
  movaps xmm9, OWORD PTR lq+16
  movaps xmm15, gx[rcx]
  movaps xmm0, OWORD PTR lq[rax*8]
  movaps xmm1, [gx+16]
  movaps xmm7, gd
  movaps xmm8, gd+4
; xorps
  xorps xmm0, xmm15
  xorps xmm1, xmm9
  xorps xmm7, xmm8
  xorps xmm8, xmm7
  xorps xmm9, [rax]
  xorps xmm15, [rcx+8]
  xorps xmm0, [rdx+1000h]
  xorps xmm1, [rsp]
  xorps xmm7, [rsp+16]
  xorps xmm8, [rbp]
  xorps xmm9, [r13]
  xorps xmm15, [r12]
  xorps xmm0, [r12+40h]
  xorps xmm1, [rax+rbx*4]
  xorps xmm7, [r8+r9*8+16]
  xorps xmm8, [r15+r14*2-128]
  xorps xmm9, [rsi+r10+200h]
  xorps xmm15, [rdi*8+16]
  xorps xmm0, [rbx+rbp]
  xorps xmm1, [r11+rsp]
  xorps xmm7, [rax-8]
  xorps xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  xorps xmm9, OWORD PTR [r9+r10*4+7Fh]
  xorps xmm15, DWORD PTR [r9+r10*4+7Fh]
  xorps xmm0, XMMWORD PTR [rax]
  xorps xmm1, OWORD PTR [rax]
  xorps xmm7, DWORD PTR [rax]
  xorps xmm8, REAL4 PTR [rcx]
  xorps xmm9, gf
  xorps xmm15, gx
  xorps xmm0, gx+32
  xorps xmm1, OWORD PTR gq
  xorps xmm7, XMMWORD PTR gq+8
  xorps xmm8, OWORD PTR lq
  xorps xmm9, OWORD PTR lq+16
  xorps xmm15, gx[rcx]
  xorps xmm0, OWORD PTR lq[rax*8]
  xorps xmm1, [gx+16]
  xorps xmm7, gd
  xorps xmm8, gd+4
; andps
  andps xmm0, xmm15
  andps xmm1, xmm9
  andps xmm7, xmm8
  andps xmm8, xmm7
  andps xmm9, [rax]
  andps xmm15, [rcx+8]
  andps xmm0, [rdx+1000h]
  andps xmm1, [rsp]
  andps xmm7, [rsp+16]
  andps xmm8, [rbp]
  andps xmm9, [r13]
  andps xmm15, [r12]
  andps xmm0, [r12+40h]
  andps xmm1, [rax+rbx*4]
  andps xmm7, [r8+r9*8+16]
  andps xmm8, [r15+r14*2-128]
  andps xmm9, [rsi+r10+200h]
  andps xmm15, [rdi*8+16]
  andps xmm0, [rbx+rbp]
  andps xmm1, [r11+rsp]
  andps xmm7, [rax-8]
  andps xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  andps xmm9, OWORD PTR [r9+r10*4+7Fh]
  andps xmm15, DWORD PTR [r9+r10*4+7Fh]
  andps xmm0, XMMWORD PTR [rax]
  andps xmm1, OWORD PTR [rax]
  andps xmm7, DWORD PTR [rax]
  andps xmm8, REAL4 PTR [rcx]
  andps xmm9, gf
  andps xmm15, gx
  andps xmm0, gx+32
  andps xmm1, OWORD PTR gq
  andps xmm7, XMMWORD PTR gq+8
  andps xmm8, OWORD PTR lq
  andps xmm9, OWORD PTR lq+16
  andps xmm15, gx[rcx]
  andps xmm0, OWORD PTR lq[rax*8]
  andps xmm1, [gx+16]
  andps xmm7, gd
  andps xmm8, gd+4
; andnps
  andnps xmm0, xmm15
  andnps xmm1, xmm9
  andnps xmm7, xmm8
  andnps xmm8, xmm7
  andnps xmm9, [rax]
  andnps xmm15, [rcx+8]
  andnps xmm0, [rdx+1000h]
  andnps xmm1, [rsp]
  andnps xmm7, [rsp+16]
  andnps xmm8, [rbp]
  andnps xmm9, [r13]
  andnps xmm15, [r12]
  andnps xmm0, [r12+40h]
  andnps xmm1, [rax+rbx*4]
  andnps xmm7, [r8+r9*8+16]
  andnps xmm8, [r15+r14*2-128]
  andnps xmm9, [rsi+r10+200h]
  andnps xmm15, [rdi*8+16]
  andnps xmm0, [rbx+rbp]
  andnps xmm1, [r11+rsp]
  andnps xmm7, [rax-8]
  andnps xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  andnps xmm9, OWORD PTR [r9+r10*4+7Fh]
  andnps xmm15, DWORD PTR [r9+r10*4+7Fh]
  andnps xmm0, XMMWORD PTR [rax]
  andnps xmm1, OWORD PTR [rax]
  andnps xmm7, DWORD PTR [rax]
  andnps xmm8, REAL4 PTR [rcx]
  andnps xmm9, gf
  andnps xmm15, gx
  andnps xmm0, gx+32
  andnps xmm1, OWORD PTR gq
  andnps xmm7, XMMWORD PTR gq+8
  andnps xmm8, OWORD PTR lq
  andnps xmm9, OWORD PTR lq+16
  andnps xmm15, gx[rcx]
  andnps xmm0, OWORD PTR lq[rax*8]
  andnps xmm1, [gx+16]
  andnps xmm7, gd
  andnps xmm8, gd+4
; orps
  orps xmm0, xmm15
  orps xmm1, xmm9
  orps xmm7, xmm8
  orps xmm8, xmm7
  orps xmm9, [rax]
  orps xmm15, [rcx+8]
  orps xmm0, [rdx+1000h]
  orps xmm1, [rsp]
  orps xmm7, [rsp+16]
  orps xmm8, [rbp]
  orps xmm9, [r13]
  orps xmm15, [r12]
  orps xmm0, [r12+40h]
  orps xmm1, [rax+rbx*4]
  orps xmm7, [r8+r9*8+16]
  orps xmm8, [r15+r14*2-128]
  orps xmm9, [rsi+r10+200h]
  orps xmm15, [rdi*8+16]
  orps xmm0, [rbx+rbp]
  orps xmm1, [r11+rsp]
  orps xmm7, [rax-8]
  orps xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  orps xmm9, OWORD PTR [r9+r10*4+7Fh]
  orps xmm15, DWORD PTR [r9+r10*4+7Fh]
  orps xmm0, XMMWORD PTR [rax]
  orps xmm1, OWORD PTR [rax]
  orps xmm7, DWORD PTR [rax]
  orps xmm8, REAL4 PTR [rcx]
  orps xmm9, gf
  orps xmm15, gx
  orps xmm0, gx+32
  orps xmm1, OWORD PTR gq
  orps xmm7, XMMWORD PTR gq+8
  orps xmm8, OWORD PTR lq
  orps xmm9, OWORD PTR lq+16
  orps xmm15, gx[rcx]
  orps xmm0, OWORD PTR lq[rax*8]
  orps xmm1, [gx+16]
  orps xmm7, gd
  orps xmm8, gd+4
; movdqa
  movdqa xmm0, xmm15
  movdqa xmm1, xmm9
  movdqa xmm7, xmm8
  movdqa xmm8, xmm7
  movdqa xmm9, [rax]
  movdqa xmm15, [rcx+8]
  movdqa xmm0, [rdx+1000h]
  movdqa xmm1, [rsp]
  movdqa xmm7, [rsp+16]
  movdqa xmm8, [rbp]
  movdqa xmm9, [r13]
  movdqa xmm15, [r12]
  movdqa xmm0, [r12+40h]
  movdqa xmm1, [rax+rbx*4]
  movdqa xmm7, [r8+r9*8+16]
  movdqa xmm8, [r15+r14*2-128]
  movdqa xmm9, [rsi+r10+200h]
  movdqa xmm15, [rdi*8+16]
  movdqa xmm0, [rbx+rbp]
  movdqa xmm1, [r11+rsp]
  movdqa xmm7, [rax-8]
  movdqa xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  movdqa xmm9, OWORD PTR [r9+r10*4+7Fh]
  movdqa xmm15, XMMWORD PTR [rax]
  movdqa xmm0, OWORD PTR [rax]
  movdqa xmm1, gx
  movdqa xmm7, gx+32
  movdqa xmm8, OWORD PTR gq
  movdqa xmm9, XMMWORD PTR gq+8
  movdqa xmm15, OWORD PTR lq
  movdqa xmm0, OWORD PTR lq+16
  movdqa xmm1, gx[rcx]
  movdqa xmm7, OWORD PTR lq[rax*8]
  movdqa xmm8, [gx+16]
; movdqu
  movdqu xmm0, xmm15
  movdqu xmm1, xmm9
  movdqu xmm7, xmm8
  movdqu xmm8, xmm7
  movdqu xmm9, [rax]
  movdqu xmm15, [rcx+8]
  movdqu xmm0, [rdx+1000h]
  movdqu xmm1, [rsp]
  movdqu xmm7, [rsp+16]
  movdqu xmm8, [rbp]
  movdqu xmm9, [r13]
  movdqu xmm15, [r12]
  movdqu xmm0, [r12+40h]
  movdqu xmm1, [rax+rbx*4]
  movdqu xmm7, [r8+r9*8+16]
  movdqu xmm8, [r15+r14*2-128]
  movdqu xmm9, [rsi+r10+200h]
  movdqu xmm15, [rdi*8+16]
  movdqu xmm0, [rbx+rbp]
  movdqu xmm1, [r11+rsp]
  movdqu xmm7, [rax-8]
  movdqu xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  movdqu xmm9, OWORD PTR [r9+r10*4+7Fh]
  movdqu xmm15, XMMWORD PTR [rax]
  movdqu xmm0, OWORD PTR [rax]
  movdqu xmm1, gx
  movdqu xmm7, gx+32
  movdqu xmm8, OWORD PTR gq
  movdqu xmm9, XMMWORD PTR gq+8
  movdqu xmm15, OWORD PTR lq
  movdqu xmm0, OWORD PTR lq+16
  movdqu xmm1, gx[rcx]
  movdqu xmm7, OWORD PTR lq[rax*8]
  movdqu xmm8, [gx+16]
; pxor
  pxor xmm0, xmm15
  pxor xmm1, xmm9
  pxor xmm7, xmm8
  pxor xmm8, xmm7
  pxor xmm9, [rax]
  pxor xmm15, [rcx+8]
  pxor xmm0, [rdx+1000h]
  pxor xmm1, [rsp]
  pxor xmm7, [rsp+16]
  pxor xmm8, [rbp]
  pxor xmm9, [r13]
  pxor xmm15, [r12]
  pxor xmm0, [r12+40h]
  pxor xmm1, [rax+rbx*4]
  pxor xmm7, [r8+r9*8+16]
  pxor xmm8, [r15+r14*2-128]
  pxor xmm9, [rsi+r10+200h]
  pxor xmm15, [rdi*8+16]
  pxor xmm0, [rbx+rbp]
  pxor xmm1, [r11+rsp]
  pxor xmm7, [rax-8]
  pxor xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  pxor xmm9, OWORD PTR [r9+r10*4+7Fh]
  pxor xmm15, XMMWORD PTR [rax]
  pxor xmm0, OWORD PTR [rax]
  pxor xmm1, gx
  pxor xmm7, gx+32
  pxor xmm8, OWORD PTR gq
  pxor xmm9, XMMWORD PTR gq+8
  pxor xmm15, OWORD PTR lq
  pxor xmm0, OWORD PTR lq+16
  pxor xmm1, gx[rcx]
  pxor xmm7, OWORD PTR lq[rax*8]
  pxor xmm8, [gx+16]
; pand
  pand xmm0, xmm15
  pand xmm1, xmm9
  pand xmm7, xmm8
  pand xmm8, xmm7
  pand xmm9, [rax]
  pand xmm15, [rcx+8]
  pand xmm0, [rdx+1000h]
  pand xmm1, [rsp]
  pand xmm7, [rsp+16]
  pand xmm8, [rbp]
  pand xmm9, [r13]
  pand xmm15, [r12]
  pand xmm0, [r12+40h]
  pand xmm1, [rax+rbx*4]
  pand xmm7, [r8+r9*8+16]
  pand xmm8, [r15+r14*2-128]
  pand xmm9, [rsi+r10+200h]
  pand xmm15, [rdi*8+16]
  pand xmm0, [rbx+rbp]
  pand xmm1, [r11+rsp]
  pand xmm7, [rax-8]
  pand xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  pand xmm9, OWORD PTR [r9+r10*4+7Fh]
  pand xmm15, XMMWORD PTR [rax]
  pand xmm0, OWORD PTR [rax]
  pand xmm1, gx
  pand xmm7, gx+32
  pand xmm8, OWORD PTR gq
  pand xmm9, XMMWORD PTR gq+8
  pand xmm15, OWORD PTR lq
  pand xmm0, OWORD PTR lq+16
  pand xmm1, gx[rcx]
  pand xmm7, OWORD PTR lq[rax*8]
  pand xmm8, [gx+16]
; por
  por xmm0, xmm15
  por xmm1, xmm9
  por xmm7, xmm8
  por xmm8, xmm7
  por xmm9, [rax]
  por xmm15, [rcx+8]
  por xmm0, [rdx+1000h]
  por xmm1, [rsp]
  por xmm7, [rsp+16]
  por xmm8, [rbp]
  por xmm9, [r13]
  por xmm15, [r12]
  por xmm0, [r12+40h]
  por xmm1, [rax+rbx*4]
  por xmm7, [r8+r9*8+16]
  por xmm8, [r15+r14*2-128]
  por xmm9, [rsi+r10+200h]
  por xmm15, [rdi*8+16]
  por xmm0, [rbx+rbp]
  por xmm1, [r11+rsp]
  por xmm7, [rax-8]
  por xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  por xmm9, OWORD PTR [r9+r10*4+7Fh]
  por xmm15, XMMWORD PTR [rax]
  por xmm0, OWORD PTR [rax]
  por xmm1, gx
  por xmm7, gx+32
  por xmm8, OWORD PTR gq
  por xmm9, XMMWORD PTR gq+8
  por xmm15, OWORD PTR lq
  por xmm0, OWORD PTR lq+16
  por xmm1, gx[rcx]
  por xmm7, OWORD PTR lq[rax*8]
  por xmm8, [gx+16]
; xorpd
  xorpd xmm0, xmm15
  xorpd xmm1, xmm9
  xorpd xmm7, xmm8
  xorpd xmm8, xmm7
  xorpd xmm9, [rax]
  xorpd xmm15, [rcx+8]
  xorpd xmm0, [rdx+1000h]
  xorpd xmm1, [rsp]
  xorpd xmm7, [rsp+16]
  xorpd xmm8, [rbp]
  xorpd xmm9, [r13]
  xorpd xmm15, [r12]
  xorpd xmm0, [r12+40h]
  xorpd xmm1, [rax+rbx*4]
  xorpd xmm7, [r8+r9*8+16]
  xorpd xmm8, [r15+r14*2-128]
  xorpd xmm9, [rsi+r10+200h]
  xorpd xmm15, [rdi*8+16]
  xorpd xmm0, [rbx+rbp]
  xorpd xmm1, [r11+rsp]
  xorpd xmm7, [rax-8]
  xorpd xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  xorpd xmm9, OWORD PTR [r9+r10*4+7Fh]
  xorpd xmm15, QWORD PTR [r9+r10*4+7Fh]
  xorpd xmm0, XMMWORD PTR [rax]
  xorpd xmm1, OWORD PTR [rax]
  xorpd xmm7, QWORD PTR [rax]
  xorpd xmm8, REAL8 PTR [rcx]
  xorpd xmm9, gr8
  xorpd xmm15, gx
  xorpd xmm0, gx+32
  xorpd xmm1, OWORD PTR gq
  xorpd xmm7, XMMWORD PTR gq+8
  xorpd xmm8, OWORD PTR lq
  xorpd xmm9, OWORD PTR lq+16
  xorpd xmm15, gx[rcx]
  xorpd xmm0, OWORD PTR lq[rax*8]
  xorpd xmm1, [gx+16]
  xorpd xmm7, gq
  xorpd xmm8, gq+4
; andpd
  andpd xmm0, xmm15
  andpd xmm1, xmm9
  andpd xmm7, xmm8
  andpd xmm8, xmm7
  andpd xmm9, [rax]
  andpd xmm15, [rcx+8]
  andpd xmm0, [rdx+1000h]
  andpd xmm1, [rsp]
  andpd xmm7, [rsp+16]
  andpd xmm8, [rbp]
  andpd xmm9, [r13]
  andpd xmm15, [r12]
  andpd xmm0, [r12+40h]
  andpd xmm1, [rax+rbx*4]
  andpd xmm7, [r8+r9*8+16]
  andpd xmm8, [r15+r14*2-128]
  andpd xmm9, [rsi+r10+200h]
  andpd xmm15, [rdi*8+16]
  andpd xmm0, [rbx+rbp]
  andpd xmm1, [r11+rsp]
  andpd xmm7, [rax-8]
  andpd xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  andpd xmm9, OWORD PTR [r9+r10*4+7Fh]
  andpd xmm15, QWORD PTR [r9+r10*4+7Fh]
  andpd xmm0, XMMWORD PTR [rax]
  andpd xmm1, OWORD PTR [rax]
  andpd xmm7, QWORD PTR [rax]
  andpd xmm8, REAL8 PTR [rcx]
  andpd xmm9, gr8
  andpd xmm15, gx
  andpd xmm0, gx+32
  andpd xmm1, OWORD PTR gq
  andpd xmm7, XMMWORD PTR gq+8
  andpd xmm8, OWORD PTR lq
  andpd xmm9, OWORD PTR lq+16
  andpd xmm15, gx[rcx]
  andpd xmm0, OWORD PTR lq[rax*8]
  andpd xmm1, [gx+16]
  andpd xmm7, gq
  andpd xmm8, gq+4
; andnpd
  andnpd xmm0, xmm15
  andnpd xmm1, xmm9
  andnpd xmm7, xmm8
  andnpd xmm8, xmm7
  andnpd xmm9, [rax]
  andnpd xmm15, [rcx+8]
  andnpd xmm0, [rdx+1000h]
  andnpd xmm1, [rsp]
  andnpd xmm7, [rsp+16]
  andnpd xmm8, [rbp]
  andnpd xmm9, [r13]
  andnpd xmm15, [r12]
  andnpd xmm0, [r12+40h]
  andnpd xmm1, [rax+rbx*4]
  andnpd xmm7, [r8+r9*8+16]
  andnpd xmm8, [r15+r14*2-128]
  andnpd xmm9, [rsi+r10+200h]
  andnpd xmm15, [rdi*8+16]
  andnpd xmm0, [rbx+rbp]
  andnpd xmm1, [r11+rsp]
  andnpd xmm7, [rax-8]
  andnpd xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  andnpd xmm9, OWORD PTR [r9+r10*4+7Fh]
  andnpd xmm15, QWORD PTR [r9+r10*4+7Fh]
  andnpd xmm0, XMMWORD PTR [rax]
  andnpd xmm1, OWORD PTR [rax]
  andnpd xmm7, QWORD PTR [rax]
  andnpd xmm8, REAL8 PTR [rcx]
  andnpd xmm9, gr8
  andnpd xmm15, gx
  andnpd xmm0, gx+32
  andnpd xmm1, OWORD PTR gq
  andnpd xmm7, XMMWORD PTR gq+8
  andnpd xmm8, OWORD PTR lq
  andnpd xmm9, OWORD PTR lq+16
  andnpd xmm15, gx[rcx]
  andnpd xmm0, OWORD PTR lq[rax*8]
  andnpd xmm1, [gx+16]
  andnpd xmm7, gq
  andnpd xmm8, gq+4
; orpd
  orpd xmm0, xmm15
  orpd xmm1, xmm9
  orpd xmm7, xmm8
  orpd xmm8, xmm7
  orpd xmm9, [rax]
  orpd xmm15, [rcx+8]
  orpd xmm0, [rdx+1000h]
  orpd xmm1, [rsp]
  orpd xmm7, [rsp+16]
  orpd xmm8, [rbp]
  orpd xmm9, [r13]
  orpd xmm15, [r12]
  orpd xmm0, [r12+40h]
  orpd xmm1, [rax+rbx*4]
  orpd xmm7, [r8+r9*8+16]
  orpd xmm8, [r15+r14*2-128]
  orpd xmm9, [rsi+r10+200h]
  orpd xmm15, [rdi*8+16]
  orpd xmm0, [rbx+rbp]
  orpd xmm1, [r11+rsp]
  orpd xmm7, [rax-8]
  orpd xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  orpd xmm9, OWORD PTR [r9+r10*4+7Fh]
  orpd xmm15, QWORD PTR [r9+r10*4+7Fh]
  orpd xmm0, XMMWORD PTR [rax]
  orpd xmm1, OWORD PTR [rax]
  orpd xmm7, QWORD PTR [rax]
  orpd xmm8, REAL8 PTR [rcx]
  orpd xmm9, gr8
  orpd xmm15, gx
  orpd xmm0, gx+32
  orpd xmm1, OWORD PTR gq
  orpd xmm7, XMMWORD PTR gq+8
  orpd xmm8, OWORD PTR lq
  orpd xmm9, OWORD PTR lq+16
  orpd xmm15, gx[rcx]
  orpd xmm0, OWORD PTR lq[rax*8]
  orpd xmm1, [gx+16]
  orpd xmm7, gq
  orpd xmm8, gq+4
; movapd
  movapd xmm0, xmm15
  movapd xmm1, xmm9
  movapd xmm7, xmm8
  movapd xmm8, xmm7
  movapd xmm9, [rax]
  movapd xmm15, [rcx+8]
  movapd xmm0, [rdx+1000h]
  movapd xmm1, [rsp]
  movapd xmm7, [rsp+16]
  movapd xmm8, [rbp]
  movapd xmm9, [r13]
  movapd xmm15, [r12]
  movapd xmm0, [r12+40h]
  movapd xmm1, [rax+rbx*4]
  movapd xmm7, [r8+r9*8+16]
  movapd xmm8, [r15+r14*2-128]
  movapd xmm9, [rsi+r10+200h]
  movapd xmm15, [rdi*8+16]
  movapd xmm0, [rbx+rbp]
  movapd xmm1, [r11+rsp]
  movapd xmm7, [rax-8]
  movapd xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  movapd xmm9, OWORD PTR [r9+r10*4+7Fh]
  movapd xmm15, QWORD PTR [r9+r10*4+7Fh]
  movapd xmm0, XMMWORD PTR [rax]
  movapd xmm1, OWORD PTR [rax]
  movapd xmm7, QWORD PTR [rax]
  movapd xmm8, REAL8 PTR [rcx]
  movapd xmm9, gr8
  movapd xmm15, gx
  movapd xmm0, gx+32
  movapd xmm1, OWORD PTR gq
  movapd xmm7, XMMWORD PTR gq+8
  movapd xmm8, OWORD PTR lq
  movapd xmm9, OWORD PTR lq+16
  movapd xmm15, gx[rcx]
  movapd xmm0, OWORD PTR lq[rax*8]
  movapd xmm1, [gx+16]
  movapd xmm7, gq
  movapd xmm8, gq+4
; movupd
  movupd xmm0, xmm15
  movupd xmm1, xmm9
  movupd xmm7, xmm8
  movupd xmm8, xmm7
  movupd xmm9, [rax]
  movupd xmm15, [rcx+8]
  movupd xmm0, [rdx+1000h]
  movupd xmm1, [rsp]
  movupd xmm7, [rsp+16]
  movupd xmm8, [rbp]
  movupd xmm9, [r13]
  movupd xmm15, [r12]
  movupd xmm0, [r12+40h]
  movupd xmm1, [rax+rbx*4]
  movupd xmm7, [r8+r9*8+16]
  movupd xmm8, [r15+r14*2-128]
  movupd xmm9, [rsi+r10+200h]
  movupd xmm15, [rdi*8+16]
  movupd xmm0, [rbx+rbp]
  movupd xmm1, [r11+rsp]
  movupd xmm7, [rax-8]
  movupd xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  movupd xmm9, OWORD PTR [r9+r10*4+7Fh]
  movupd xmm15, QWORD PTR [r9+r10*4+7Fh]
  movupd xmm0, XMMWORD PTR [rax]
  movupd xmm1, OWORD PTR [rax]
  movupd xmm7, QWORD PTR [rax]
  movupd xmm8, REAL8 PTR [rcx]
  movupd xmm9, gr8
  movupd xmm15, gx
  movupd xmm0, gx+32
  movupd xmm1, OWORD PTR gq
  movupd xmm7, XMMWORD PTR gq+8
  movupd xmm8, OWORD PTR lq
  movupd xmm9, OWORD PTR lq+16
  movupd xmm15, gx[rcx]
  movupd xmm0, OWORD PTR lq[rax*8]
  movupd xmm1, [gx+16]
  movupd xmm7, gq
  movupd xmm8, gq+4
; paddq
  paddq xmm0, xmm15
  paddq xmm1, xmm9
  paddq xmm7, xmm8
  paddq xmm8, xmm7
  paddq xmm9, [rax]
  paddq xmm15, [rcx+8]
  paddq xmm0, [rdx+1000h]
  paddq xmm1, [rsp]
  paddq xmm7, [rsp+16]
  paddq xmm8, [rbp]
  paddq xmm9, [r13]
  paddq xmm15, [r12]
  paddq xmm0, [r12+40h]
  paddq xmm1, [rax+rbx*4]
  paddq xmm7, [r8+r9*8+16]
  paddq xmm8, [r15+r14*2-128]
  paddq xmm9, [rsi+r10+200h]
  paddq xmm15, [rdi*8+16]
  paddq xmm0, [rbx+rbp]
  paddq xmm1, [r11+rsp]
  paddq xmm7, [rax-8]
  paddq xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  paddq xmm9, OWORD PTR [r9+r10*4+7Fh]
  paddq xmm15, QWORD PTR [r9+r10*4+7Fh]
  paddq xmm0, XMMWORD PTR [rax]
  paddq xmm1, OWORD PTR [rax]
  paddq xmm7, QWORD PTR [rax]
  paddq xmm8, REAL8 PTR [rcx]
  paddq xmm9, gr8
  paddq xmm15, gx
  paddq xmm0, gx+32
  paddq xmm1, OWORD PTR gq
  paddq xmm7, XMMWORD PTR gq+8
  paddq xmm8, OWORD PTR lq
  paddq xmm9, OWORD PTR lq+16
  paddq xmm15, gx[rcx]
  paddq xmm0, OWORD PTR lq[rax*8]
  paddq xmm1, [gx+16]
  paddq xmm7, gq
  paddq xmm8, gq+4
; psubq
  psubq xmm0, xmm15
  psubq xmm1, xmm9
  psubq xmm7, xmm8
  psubq xmm8, xmm7
  psubq xmm9, [rax]
  psubq xmm15, [rcx+8]
  psubq xmm0, [rdx+1000h]
  psubq xmm1, [rsp]
  psubq xmm7, [rsp+16]
  psubq xmm8, [rbp]
  psubq xmm9, [r13]
  psubq xmm15, [r12]
  psubq xmm0, [r12+40h]
  psubq xmm1, [rax+rbx*4]
  psubq xmm7, [r8+r9*8+16]
  psubq xmm8, [r15+r14*2-128]
  psubq xmm9, [rsi+r10+200h]
  psubq xmm15, [rdi*8+16]
  psubq xmm0, [rbx+rbp]
  psubq xmm1, [r11+rsp]
  psubq xmm7, [rax-8]
  psubq xmm8, XMMWORD PTR [r9+r10*4+7Fh]
  psubq xmm9, OWORD PTR [r9+r10*4+7Fh]
  psubq xmm15, QWORD PTR [r9+r10*4+7Fh]
  psubq xmm0, XMMWORD PTR [rax]
  psubq xmm1, OWORD PTR [rax]
  psubq xmm7, QWORD PTR [rax]
  psubq xmm8, REAL8 PTR [rcx]
  psubq xmm9, gr8
  psubq xmm15, gx
  psubq xmm0, gx+32
  psubq xmm1, OWORD PTR gq
  psubq xmm7, XMMWORD PTR gq+8
  psubq xmm8, OWORD PTR lq
  psubq xmm9, OWORD PTR lq+16
  psubq xmm15, gx[rcx]
  psubq xmm0, OWORD PTR lq[rax*8]
  psubq xmm1, [gx+16]
  psubq xmm7, gq
  psubq xmm8, gq+4
sse ENDP

stores PROC
; movups
  movups [rax], xmm9
  movups [rcx+8], xmm15
  movups [rdx+1000h], xmm0
  movups [rsp], xmm1
  movups [rsp+16], xmm7
  movups [rbp], xmm8
  movups [r13], xmm9
  movups [r12], xmm15
  movups [r12+40h], xmm0
  movups [rax+rbx*4], xmm1
  movups [r8+r9*8+16], xmm7
  movups [r15+r14*2-128], xmm8
  movups [rsi+r10+200h], xmm9
  movups [rdi*8+16], xmm15
  movups [rbx+rbp], xmm0
  movups [r11+rsp], xmm1
  movups [rax-8], xmm7
  movups XMMWORD PTR [r9+r10*4+7Fh], xmm8
  movups OWORD PTR [r9+r10*4+7Fh], xmm9
  movups DWORD PTR [r9+r10*4+7Fh], xmm15
  movups XMMWORD PTR [rax], xmm0
  movups OWORD PTR [rax], xmm1
  movups DWORD PTR [rax], xmm7
  movups REAL4 PTR [rcx], xmm8
  movups gf, xmm9
  movups gx, xmm15
  movups gx+32, xmm0
  movups OWORD PTR gq, xmm1
  movups XMMWORD PTR gq+8, xmm7
  movups OWORD PTR lq, xmm8
  movups OWORD PTR lq+16, xmm9
  movups gx[rcx], xmm15
  movups OWORD PTR lq[rax*8], xmm0
  movups [gx+16], xmm1
  movups gd, xmm7
  movups gd+4, xmm8
; movaps
  movaps [rax], xmm9
  movaps [rcx+8], xmm15
  movaps [rdx+1000h], xmm0
  movaps [rsp], xmm1
  movaps [rsp+16], xmm7
  movaps [rbp], xmm8
  movaps [r13], xmm9
  movaps [r12], xmm15
  movaps [r12+40h], xmm0
  movaps [rax+rbx*4], xmm1
  movaps [r8+r9*8+16], xmm7
  movaps [r15+r14*2-128], xmm8
  movaps [rsi+r10+200h], xmm9
  movaps [rdi*8+16], xmm15
  movaps [rbx+rbp], xmm0
  movaps [r11+rsp], xmm1
  movaps [rax-8], xmm7
  movaps XMMWORD PTR [r9+r10*4+7Fh], xmm8
  movaps OWORD PTR [r9+r10*4+7Fh], xmm9
  movaps DWORD PTR [r9+r10*4+7Fh], xmm15
  movaps XMMWORD PTR [rax], xmm0
  movaps OWORD PTR [rax], xmm1
  movaps DWORD PTR [rax], xmm7
  movaps REAL4 PTR [rcx], xmm8
  movaps gf, xmm9
  movaps gx, xmm15
  movaps gx+32, xmm0
  movaps OWORD PTR gq, xmm1
  movaps XMMWORD PTR gq+8, xmm7
  movaps OWORD PTR lq, xmm8
  movaps OWORD PTR lq+16, xmm9
  movaps gx[rcx], xmm15
  movaps OWORD PTR lq[rax*8], xmm0
  movaps [gx+16], xmm1
  movaps gd, xmm7
  movaps gd+4, xmm8
; movdqa
  movdqa [rax], xmm9
  movdqa [rcx+8], xmm15
  movdqa [rdx+1000h], xmm0
  movdqa [rsp], xmm1
  movdqa [rsp+16], xmm7
  movdqa [rbp], xmm8
  movdqa [r13], xmm9
  movdqa [r12], xmm15
  movdqa [r12+40h], xmm0
  movdqa [rax+rbx*4], xmm1
  movdqa [r8+r9*8+16], xmm7
  movdqa [r15+r14*2-128], xmm8
  movdqa [rsi+r10+200h], xmm9
  movdqa [rdi*8+16], xmm15
  movdqa [rbx+rbp], xmm0
  movdqa [r11+rsp], xmm1
  movdqa [rax-8], xmm7
  movdqa XMMWORD PTR [r9+r10*4+7Fh], xmm8
  movdqa OWORD PTR [r9+r10*4+7Fh], xmm9
  movdqa XMMWORD PTR [rax], xmm15
  movdqa OWORD PTR [rax], xmm0
  movdqa gx, xmm1
  movdqa gx+32, xmm7
  movdqa OWORD PTR gq, xmm8
  movdqa XMMWORD PTR gq+8, xmm9
  movdqa OWORD PTR lq, xmm15
  movdqa OWORD PTR lq+16, xmm0
  movdqa gx[rcx], xmm1
  movdqa OWORD PTR lq[rax*8], xmm7
  movdqa [gx+16], xmm8
; movdqu
  movdqu [rax], xmm9
  movdqu [rcx+8], xmm15
  movdqu [rdx+1000h], xmm0
  movdqu [rsp], xmm1
  movdqu [rsp+16], xmm7
  movdqu [rbp], xmm8
  movdqu [r13], xmm9
  movdqu [r12], xmm15
  movdqu [r12+40h], xmm0
  movdqu [rax+rbx*4], xmm1
  movdqu [r8+r9*8+16], xmm7
  movdqu [r15+r14*2-128], xmm8
  movdqu [rsi+r10+200h], xmm9
  movdqu [rdi*8+16], xmm15
  movdqu [rbx+rbp], xmm0
  movdqu [r11+rsp], xmm1
  movdqu [rax-8], xmm7
  movdqu XMMWORD PTR [r9+r10*4+7Fh], xmm8
  movdqu OWORD PTR [r9+r10*4+7Fh], xmm9
  movdqu XMMWORD PTR [rax], xmm15
  movdqu OWORD PTR [rax], xmm0
  movdqu gx, xmm1
  movdqu gx+32, xmm7
  movdqu OWORD PTR gq, xmm8
  movdqu XMMWORD PTR gq+8, xmm9
  movdqu OWORD PTR lq, xmm15
  movdqu OWORD PTR lq+16, xmm0
  movdqu gx[rcx], xmm1
  movdqu OWORD PTR lq[rax*8], xmm7
  movdqu [gx+16], xmm8
; movntdq
  movntdq [rax], xmm9
  movntdq [rcx+8], xmm15
  movntdq [rdx+1000h], xmm0
  movntdq [rsp], xmm1
  movntdq [rsp+16], xmm7
  movntdq [rbp], xmm8
  movntdq [r13], xmm9
  movntdq [r12], xmm15
  movntdq [r12+40h], xmm0
  movntdq [rax+rbx*4], xmm1
  movntdq [r8+r9*8+16], xmm7
  movntdq [r15+r14*2-128], xmm8
  movntdq [rsi+r10+200h], xmm9
  movntdq [rdi*8+16], xmm15
  movntdq [rbx+rbp], xmm0
  movntdq [r11+rsp], xmm1
  movntdq [rax-8], xmm7
  movntdq XMMWORD PTR [r9+r10*4+7Fh], xmm8
  movntdq OWORD PTR [r9+r10*4+7Fh], xmm9
  movntdq XMMWORD PTR [rax], xmm15
  movntdq OWORD PTR [rax], xmm0
  movntdq gx, xmm1
  movntdq gx+32, xmm7
  movntdq OWORD PTR gq, xmm8
  movntdq XMMWORD PTR gq+8, xmm9
  movntdq OWORD PTR lq, xmm15
  movntdq OWORD PTR lq+16, xmm0
  movntdq gx[rcx], xmm1
  movntdq OWORD PTR lq[rax*8], xmm7
  movntdq [gx+16], xmm8
; movapd
  movapd [rax], xmm9
  movapd [rcx+8], xmm15
  movapd [rdx+1000h], xmm0
  movapd [rsp], xmm1
  movapd [rsp+16], xmm7
  movapd [rbp], xmm8
  movapd [r13], xmm9
  movapd [r12], xmm15
  movapd [r12+40h], xmm0
  movapd [rax+rbx*4], xmm1
  movapd [r8+r9*8+16], xmm7
  movapd [r15+r14*2-128], xmm8
  movapd [rsi+r10+200h], xmm9
  movapd [rdi*8+16], xmm15
  movapd [rbx+rbp], xmm0
  movapd [r11+rsp], xmm1
  movapd [rax-8], xmm7
  movapd XMMWORD PTR [r9+r10*4+7Fh], xmm8
  movapd OWORD PTR [r9+r10*4+7Fh], xmm9
  movapd QWORD PTR [r9+r10*4+7Fh], xmm15
  movapd XMMWORD PTR [rax], xmm0
  movapd OWORD PTR [rax], xmm1
  movapd QWORD PTR [rax], xmm7
  movapd REAL8 PTR [rcx], xmm8
  movapd gr8, xmm9
  movapd gx, xmm15
  movapd gx+32, xmm0
  movapd OWORD PTR gq, xmm1
  movapd XMMWORD PTR gq+8, xmm7
  movapd OWORD PTR lq, xmm8
  movapd OWORD PTR lq+16, xmm9
  movapd gx[rcx], xmm15
  movapd OWORD PTR lq[rax*8], xmm0
  movapd [gx+16], xmm1
  movapd gq, xmm7
  movapd gq+4, xmm8
; movupd
  movupd [rax], xmm9
  movupd [rcx+8], xmm15
  movupd [rdx+1000h], xmm0
  movupd [rsp], xmm1
  movupd [rsp+16], xmm7
  movupd [rbp], xmm8
  movupd [r13], xmm9
  movupd [r12], xmm15
  movupd [r12+40h], xmm0
  movupd [rax+rbx*4], xmm1
  movupd [r8+r9*8+16], xmm7
  movupd [r15+r14*2-128], xmm8
  movupd [rsi+r10+200h], xmm9
  movupd [rdi*8+16], xmm15
  movupd [rbx+rbp], xmm0
  movupd [r11+rsp], xmm1
  movupd [rax-8], xmm7
  movupd XMMWORD PTR [r9+r10*4+7Fh], xmm8
  movupd OWORD PTR [r9+r10*4+7Fh], xmm9
  movupd QWORD PTR [r9+r10*4+7Fh], xmm15
  movupd XMMWORD PTR [rax], xmm0
  movupd OWORD PTR [rax], xmm1
  movupd QWORD PTR [rax], xmm7
  movupd REAL8 PTR [rcx], xmm8
  movupd gr8, xmm9
  movupd gx, xmm15
  movupd gx+32, xmm0
  movupd OWORD PTR gq, xmm1
  movupd XMMWORD PTR gq+8, xmm7
  movupd OWORD PTR lq, xmm8
  movupd OWORD PTR lq+16, xmm9
  movupd gx[rcx], xmm15
  movupd OWORD PTR lq[rax*8], xmm0
  movupd [gx+16], xmm1
  movupd gq, xmm7
  movupd gq+4, xmm8
stores ENDP

imm8 PROC
; pshufd
  pshufd xmm0, xmm7, 0
  pshufd xmm1, xmm8, 1
  pshufd xmm7, xmm9, 1Bh
  pshufd xmm8, xmm15, 0FFh
  pshufd xmm9, xmm0, 255
  pshufd xmm15, xmm1, -1
  pshufd xmm0, xmm7, -128
  pshufd xmm1, xmm8, 127
  pshufd xmm7, xmm9, 3 + 4
  pshufd xmm9, [rax], 240
  pshufd xmm15, [rcx+8], 241
  pshufd xmm0, [rdx+1000h], 242
  pshufd xmm1, [rsp], 243
  pshufd xmm7, [rsp+16], 244
  pshufd xmm8, [rbp], 245
  pshufd xmm9, [r13], 246
  pshufd xmm15, [r12], 247
  pshufd xmm0, [r12+40h], 248
  pshufd xmm1, [rax+rbx*4], 249
  pshufd xmm7, [r8+r9*8+16], 250
  pshufd xmm8, [r15+r14*2-128], 251
  pshufd xmm9, [rsi+r10+200h], 252
  pshufd xmm15, [rdi*8+16], 253
  pshufd xmm0, [rbx+rbp], 254
  pshufd xmm1, [r11+rsp], 255
  pshufd xmm7, [rax-8], 0
  pshufd xmm8, XMMWORD PTR [r9+r10*4+7Fh], 1
  pshufd xmm9, OWORD PTR [r9+r10*4+7Fh], 2
  pshufd xmm15, DWORD PTR [r9+r10*4+7Fh], 3
  pshufd xmm0, XMMWORD PTR [rax], 4
  pshufd xmm1, OWORD PTR [rax], 5
  pshufd xmm7, DWORD PTR [rax], 6
  pshufd xmm8, REAL4 PTR [rcx], 7
  pshufd xmm9, gf, 8
  pshufd xmm15, gx, 9
  pshufd xmm0, gx+32, 10
  pshufd xmm1, OWORD PTR gq, 11
  pshufd xmm7, XMMWORD PTR gq+8, 12
  pshufd xmm8, OWORD PTR lq, 13
  pshufd xmm9, OWORD PTR lq+16, 14
  pshufd xmm15, gx[rcx], 15
  pshufd xmm0, OWORD PTR lq[rax*8], 16
  pshufd xmm1, [gx+16], 17
  pshufd xmm7, gd, 18
  pshufd xmm8, gd+4, 19
; roundsd
  roundsd xmm0, xmm7, 0
  roundsd xmm1, xmm8, 1
  roundsd xmm7, xmm9, 1Bh
  roundsd xmm8, xmm15, 0FFh
  roundsd xmm9, xmm0, 255
  roundsd xmm15, xmm1, -1
  roundsd xmm0, xmm7, -128
  roundsd xmm1, xmm8, 127
  roundsd xmm7, xmm9, 3 + 4
  roundsd xmm9, [rax], 20
  roundsd xmm15, [rcx+8], 21
  roundsd xmm0, [rdx+1000h], 22
  roundsd xmm1, [rsp], 23
  roundsd xmm7, [rsp+16], 24
  roundsd xmm8, [rbp], 25
  roundsd xmm9, [r13], 26
  roundsd xmm15, [r12], 27
  roundsd xmm0, [r12+40h], 28
  roundsd xmm1, [rax+rbx*4], 29
  roundsd xmm7, [r8+r9*8+16], 30
  roundsd xmm8, [r15+r14*2-128], 31
  roundsd xmm9, [rsi+r10+200h], 32
  roundsd xmm15, [rdi*8+16], 33
  roundsd xmm0, [rbx+rbp], 34
  roundsd xmm1, [r11+rsp], 35
  roundsd xmm7, [rax-8], 36
  roundsd xmm8, XMMWORD PTR [r9+r10*4+7Fh], 37
  roundsd xmm9, OWORD PTR [r9+r10*4+7Fh], 38
  roundsd xmm15, QWORD PTR [r9+r10*4+7Fh], 39
  roundsd xmm0, XMMWORD PTR [rax], 40
  roundsd xmm1, OWORD PTR [rax], 41
  roundsd xmm7, QWORD PTR [rax], 42
  roundsd xmm8, REAL8 PTR [rcx], 43
  roundsd xmm9, gr8, 44
  roundsd xmm15, gx, 45
  roundsd xmm0, gx+32, 46
  roundsd xmm1, OWORD PTR gq, 47
  roundsd xmm7, XMMWORD PTR gq+8, 48
  roundsd xmm8, OWORD PTR lq, 49
  roundsd xmm9, OWORD PTR lq+16, 50
  roundsd xmm15, gx[rcx], 51
  roundsd xmm0, OWORD PTR lq[rax*8], 52
  roundsd xmm1, [gx+16], 53
  roundsd xmm7, gq, 54
  roundsd xmm8, gq+4, 55
; roundss
  roundss xmm0, xmm7, 0
  roundss xmm1, xmm8, 1
  roundss xmm7, xmm9, 1Bh
  roundss xmm8, xmm15, 0FFh
  roundss xmm9, xmm0, 255
  roundss xmm15, xmm1, -1
  roundss xmm0, xmm7, -128
  roundss xmm1, xmm8, 127
  roundss xmm7, xmm9, 3 + 4
  roundss xmm9, [rax], 56
  roundss xmm15, [rcx+8], 57
  roundss xmm0, [rdx+1000h], 58
  roundss xmm1, [rsp], 59
  roundss xmm7, [rsp+16], 60
  roundss xmm8, [rbp], 61
  roundss xmm9, [r13], 62
  roundss xmm15, [r12], 63
  roundss xmm0, [r12+40h], 64
  roundss xmm1, [rax+rbx*4], 65
  roundss xmm7, [r8+r9*8+16], 66
  roundss xmm8, [r15+r14*2-128], 67
  roundss xmm9, [rsi+r10+200h], 68
  roundss xmm15, [rdi*8+16], 69
  roundss xmm0, [rbx+rbp], 70
  roundss xmm1, [r11+rsp], 71
  roundss xmm7, [rax-8], 72
  roundss xmm8, XMMWORD PTR [r9+r10*4+7Fh], 73
  roundss xmm9, OWORD PTR [r9+r10*4+7Fh], 74
  roundss xmm15, DWORD PTR [r9+r10*4+7Fh], 75
  roundss xmm0, XMMWORD PTR [rax], 76
  roundss xmm1, OWORD PTR [rax], 77
  roundss xmm7, DWORD PTR [rax], 78
  roundss xmm8, REAL4 PTR [rcx], 79
  roundss xmm9, gf, 80
  roundss xmm15, gx, 81
  roundss xmm0, gx+32, 82
  roundss xmm1, OWORD PTR gq, 83
  roundss xmm7, XMMWORD PTR gq+8, 84
  roundss xmm8, OWORD PTR lq, 85
  roundss xmm9, OWORD PTR lq+16, 86
  roundss xmm15, gx[rcx], 87
  roundss xmm0, OWORD PTR lq[rax*8], 88
  roundss xmm1, [gx+16], 89
  roundss xmm7, gd, 90
  roundss xmm8, gd+4, 91
; pslldq
  pslldq xmm0, 0
  pslldq xmm1, 1
  pslldq xmm7, 4
  pslldq xmm8, 8
  pslldq xmm9, 15
  pslldq xmm15, 16
  pslldq xmm0, 0FFh
  pslldq xmm1, -1
  pslldq xmm7, -128
; psrldq
  psrldq xmm0, 0
  psrldq xmm1, 1
  psrldq xmm7, 4
  psrldq xmm8, 8
  psrldq xmm9, 15
  psrldq xmm15, 16
  psrldq xmm0, 0FFh
  psrldq xmm1, -1
  psrldq xmm7, -128
imm8 ENDP

moves PROC
; pmovmskb
  pmovmskb eax, xmm0
  pmovmskb eax, xmm9
  pmovmskb eax, xmm15
  pmovmskb ecx, xmm0
  pmovmskb ecx, xmm9
  pmovmskb ecx, xmm15
  pmovmskb r10d, xmm0
  pmovmskb r10d, xmm9
  pmovmskb r10d, xmm15
  pmovmskb r15d, xmm0
  pmovmskb r15d, xmm9
  pmovmskb r15d, xmm15
  pmovmskb rax, xmm0
  pmovmskb rax, xmm9
  pmovmskb rax, xmm15
  pmovmskb rdx, xmm0
  pmovmskb rdx, xmm9
  pmovmskb rdx, xmm15
  pmovmskb r9, xmm0
  pmovmskb r9, xmm9
  pmovmskb r9, xmm15
  pmovmskb r15, xmm0
  pmovmskb r15, xmm9
  pmovmskb r15, xmm15
; movd and movq with general registers
  movd xmm0, eax
  movd eax, xmm0
  movd xmm0, esp
  movd esp, xmm0
  movd xmm0, r8d
  movd r8d, xmm0
  movd xmm0, r15d
  movd r15d, xmm0
  movd xmm0, rax
  movd rax, xmm0
  movq xmm0, rax
  movq rax, xmm0
  movd xmm0, rbp
  movd rbp, xmm0
  movq xmm0, rbp
  movq rbp, xmm0
  movd xmm0, r9
  movd r9, xmm0
  movq xmm0, r9
  movq r9, xmm0
  movd xmm0, r15
  movd r15, xmm0
  movq xmm0, r15
  movq r15, xmm0
  movd xmm6, eax
  movd eax, xmm6
  movd xmm6, esp
  movd esp, xmm6
  movd xmm6, r8d
  movd r8d, xmm6
  movd xmm6, r15d
  movd r15d, xmm6
  movd xmm6, rax
  movd rax, xmm6
  movq xmm6, rax
  movq rax, xmm6
  movd xmm6, rbp
  movd rbp, xmm6
  movq xmm6, rbp
  movq rbp, xmm6
  movd xmm6, r9
  movd r9, xmm6
  movq xmm6, r9
  movq r9, xmm6
  movd xmm6, r15
  movd r15, xmm6
  movq xmm6, r15
  movq r15, xmm6
  movd xmm8, eax
  movd eax, xmm8
  movd xmm8, esp
  movd esp, xmm8
  movd xmm8, r8d
  movd r8d, xmm8
  movd xmm8, r15d
  movd r15d, xmm8
  movd xmm8, rax
  movd rax, xmm8
  movq xmm8, rax
  movq rax, xmm8
  movd xmm8, rbp
  movd rbp, xmm8
  movq xmm8, rbp
  movq rbp, xmm8
  movd xmm8, r9
  movd r9, xmm8
  movq xmm8, r9
  movq r9, xmm8
  movd xmm8, r15
  movd r15, xmm8
  movq xmm8, r15
  movq r15, xmm8
  movd xmm15, eax
  movd eax, xmm15
  movd xmm15, esp
  movd esp, xmm15
  movd xmm15, r8d
  movd r8d, xmm15
  movd xmm15, r15d
  movd r15d, xmm15
  movd xmm15, rax
  movd rax, xmm15
  movq xmm15, rax
  movq rax, xmm15
  movd xmm15, rbp
  movd rbp, xmm15
  movq xmm15, rbp
  movq rbp, xmm15
  movd xmm15, r9
  movd r9, xmm15
  movq xmm15, r9
  movq r9, xmm15
  movd xmm15, r15
  movd r15, xmm15
  movq xmm15, r15
  movq r15, xmm15
; movq between xmm registers
  movq xmm0, xmm1
  movq xmm7, xmm8
  movq xmm15, xmm15
  movq xmm9, xmm2
; movd and movq with memory
  movd xmm0, DWORD PTR [rax]
  movd DWORD PTR [rax], xmm0
  movd xmm0, QWORD PTR [rax]
  movd QWORD PTR [rax], xmm0
  movq xmm0, QWORD PTR [rax]
  movq QWORD PTR [rax], xmm0
  movd xmm1, DWORD PTR [rcx+8]
  movd DWORD PTR [rcx+8], xmm1
  movd xmm1, QWORD PTR [rcx+8]
  movd QWORD PTR [rcx+8], xmm1
  movq xmm1, QWORD PTR [rcx+8]
  movq QWORD PTR [rcx+8], xmm1
  movd xmm7, DWORD PTR [rdx+1000h]
  movd DWORD PTR [rdx+1000h], xmm7
  movd xmm7, QWORD PTR [rdx+1000h]
  movd QWORD PTR [rdx+1000h], xmm7
  movq xmm7, QWORD PTR [rdx+1000h]
  movq QWORD PTR [rdx+1000h], xmm7
  movd xmm8, DWORD PTR [rsp]
  movd DWORD PTR [rsp], xmm8
  movd xmm8, QWORD PTR [rsp]
  movd QWORD PTR [rsp], xmm8
  movq xmm8, QWORD PTR [rsp]
  movq QWORD PTR [rsp], xmm8
  movd xmm9, DWORD PTR [rsp+16]
  movd DWORD PTR [rsp+16], xmm9
  movd xmm9, QWORD PTR [rsp+16]
  movd QWORD PTR [rsp+16], xmm9
  movq xmm9, QWORD PTR [rsp+16]
  movq QWORD PTR [rsp+16], xmm9
  movd xmm15, DWORD PTR [rbp]
  movd DWORD PTR [rbp], xmm15
  movd xmm15, QWORD PTR [rbp]
  movd QWORD PTR [rbp], xmm15
  movq xmm15, QWORD PTR [rbp]
  movq QWORD PTR [rbp], xmm15
  movd xmm0, DWORD PTR [r13]
  movd DWORD PTR [r13], xmm0
  movd xmm0, QWORD PTR [r13]
  movd QWORD PTR [r13], xmm0
  movq xmm0, QWORD PTR [r13]
  movq QWORD PTR [r13], xmm0
  movd xmm1, DWORD PTR [r12]
  movd DWORD PTR [r12], xmm1
  movd xmm1, QWORD PTR [r12]
  movd QWORD PTR [r12], xmm1
  movq xmm1, QWORD PTR [r12]
  movq QWORD PTR [r12], xmm1
  movd xmm7, DWORD PTR [r12+40h]
  movd DWORD PTR [r12+40h], xmm7
  movd xmm7, QWORD PTR [r12+40h]
  movd QWORD PTR [r12+40h], xmm7
  movq xmm7, QWORD PTR [r12+40h]
  movq QWORD PTR [r12+40h], xmm7
  movd xmm8, DWORD PTR [rax+rbx*4]
  movd DWORD PTR [rax+rbx*4], xmm8
  movd xmm8, QWORD PTR [rax+rbx*4]
  movd QWORD PTR [rax+rbx*4], xmm8
  movq xmm8, QWORD PTR [rax+rbx*4]
  movq QWORD PTR [rax+rbx*4], xmm8
  movd xmm9, DWORD PTR [r8+r9*8+16]
  movd DWORD PTR [r8+r9*8+16], xmm9
  movd xmm9, QWORD PTR [r8+r9*8+16]
  movd QWORD PTR [r8+r9*8+16], xmm9
  movq xmm9, QWORD PTR [r8+r9*8+16]
  movq QWORD PTR [r8+r9*8+16], xmm9
  movd xmm15, DWORD PTR [r15+r14*2-128]
  movd DWORD PTR [r15+r14*2-128], xmm15
  movd xmm15, QWORD PTR [r15+r14*2-128]
  movd QWORD PTR [r15+r14*2-128], xmm15
  movq xmm15, QWORD PTR [r15+r14*2-128]
  movq QWORD PTR [r15+r14*2-128], xmm15
  movd xmm0, DWORD PTR [rsi+r10+200h]
  movd DWORD PTR [rsi+r10+200h], xmm0
  movd xmm0, QWORD PTR [rsi+r10+200h]
  movd QWORD PTR [rsi+r10+200h], xmm0
  movq xmm0, QWORD PTR [rsi+r10+200h]
  movq QWORD PTR [rsi+r10+200h], xmm0
  movd xmm1, DWORD PTR [rdi*8+16]
  movd DWORD PTR [rdi*8+16], xmm1
  movd xmm1, QWORD PTR [rdi*8+16]
  movd QWORD PTR [rdi*8+16], xmm1
  movq xmm1, QWORD PTR [rdi*8+16]
  movq QWORD PTR [rdi*8+16], xmm1
  movd xmm7, DWORD PTR [rbx+rbp]
  movd DWORD PTR [rbx+rbp], xmm7
  movd xmm7, QWORD PTR [rbx+rbp]
  movd QWORD PTR [rbx+rbp], xmm7
  movq xmm7, QWORD PTR [rbx+rbp]
  movq QWORD PTR [rbx+rbp], xmm7
  movd xmm8, DWORD PTR [r11+rsp]
  movd DWORD PTR [r11+rsp], xmm8
  movd xmm8, QWORD PTR [r11+rsp]
  movd QWORD PTR [r11+rsp], xmm8
  movq xmm8, QWORD PTR [r11+rsp]
  movq QWORD PTR [r11+rsp], xmm8
  movd xmm9, DWORD PTR [rax-8]
  movd DWORD PTR [rax-8], xmm9
  movd xmm9, QWORD PTR [rax-8]
  movd QWORD PTR [rax-8], xmm9
  movq xmm9, QWORD PTR [rax-8]
  movq QWORD PTR [rax-8], xmm9
  movd xmm15, DWORD PTR [r9+r10*4+7Fh]
  movd DWORD PTR [r9+r10*4+7Fh], xmm15
  movd xmm15, QWORD PTR [r9+r10*4+7Fh]
  movd QWORD PTR [r9+r10*4+7Fh], xmm15
  movq xmm15, QWORD PTR [r9+r10*4+7Fh]
  movq QWORD PTR [r9+r10*4+7Fh], xmm15
  movd xmm3, gd
  movd gd, xmm12
  movd xmm3, gd+4
  movd gd+4, xmm12
  movd xmm3, gf
  movd gf, xmm12
  movd xmm3, DWORD PTR gq
  movd DWORD PTR gq, xmm12
  movd xmm3, DWORD PTR lq+8
  movd DWORD PTR lq+8, xmm12
  movd xmm3, REAL4 PTR [rax]
  movd REAL4 PTR [rax], xmm12
  movd xmm3, gd[rcx*4]
  movd gd[rcx*4], xmm12
  movd xmm3, gq
  movd gq, xmm12
  movq xmm3, gq
  movq gq, xmm12
  movd xmm3, gq+8
  movd gq+8, xmm12
  movq xmm3, gq+8
  movq gq+8, xmm12
  movd xmm3, gr8
  movd gr8, xmm12
  movq xmm3, gr8
  movq gr8, xmm12
  movd xmm3, QWORD PTR gx
  movd QWORD PTR gx, xmm12
  movq xmm3, QWORD PTR gx
  movq QWORD PTR gx, xmm12
  movd xmm3, QWORD PTR gx+8
  movd QWORD PTR gx+8, xmm12
  movq xmm3, QWORD PTR gx+8
  movq QWORD PTR gx+8, xmm12
  movd xmm3, lq
  movd lq, xmm12
  movq xmm3, lq
  movq lq, xmm12
  movd xmm3, lq+8
  movd lq+8, xmm12
  movq xmm3, lq+8
  movq lq+8, xmm12
  movd xmm3, REAL8 PTR [rax]
  movd REAL8 PTR [rax], xmm12
  movq xmm3, REAL8 PTR [rax]
  movq REAL8 PTR [rax], xmm12
  movd xmm3, gq[rcx*8]
  movd gq[rcx*8], xmm12
  movq xmm3, gq[rcx*8]
  movq gq[rcx*8], xmm12
  movd xmm3, QWORD PTR lq[rdx]
  movd QWORD PTR lq[rdx], xmm12
  movq xmm3, QWORD PTR lq[rdx]
  movq QWORD PTR lq[rdx], xmm12
; movnti
  movnti [rax], eax
  movnti DWORD PTR [rax], eax
  movnti [rax], rax
  movnti QWORD PTR [rax], rax
  movnti [rcx+8], ecx
  movnti DWORD PTR [rcx+8], ecx
  movnti [rcx+8], rcx
  movnti QWORD PTR [rcx+8], rcx
  movnti [rdx+1000h], r8d
  movnti DWORD PTR [rdx+1000h], r8d
  movnti [rdx+1000h], r8
  movnti QWORD PTR [rdx+1000h], r8
  movnti [rsp], r15d
  movnti DWORD PTR [rsp], r15d
  movnti [rsp], r15
  movnti QWORD PTR [rsp], r15
  movnti [rsp+16], esi
  movnti DWORD PTR [rsp+16], esi
  movnti [rsp+16], rsi
  movnti QWORD PTR [rsp+16], rsi
  movnti [rbp], eax
  movnti DWORD PTR [rbp], eax
  movnti [rbp], rax
  movnti QWORD PTR [rbp], rax
  movnti [r13], ecx
  movnti DWORD PTR [r13], ecx
  movnti [r13], rcx
  movnti QWORD PTR [r13], rcx
  movnti [r12], r8d
  movnti DWORD PTR [r12], r8d
  movnti [r12], r8
  movnti QWORD PTR [r12], r8
  movnti [r12+40h], r15d
  movnti DWORD PTR [r12+40h], r15d
  movnti [r12+40h], r15
  movnti QWORD PTR [r12+40h], r15
  movnti [rax+rbx*4], esi
  movnti DWORD PTR [rax+rbx*4], esi
  movnti [rax+rbx*4], rsi
  movnti QWORD PTR [rax+rbx*4], rsi
  movnti [r8+r9*8+16], eax
  movnti DWORD PTR [r8+r9*8+16], eax
  movnti [r8+r9*8+16], rax
  movnti QWORD PTR [r8+r9*8+16], rax
  movnti [r15+r14*2-128], ecx
  movnti DWORD PTR [r15+r14*2-128], ecx
  movnti [r15+r14*2-128], rcx
  movnti QWORD PTR [r15+r14*2-128], rcx
  movnti [rsi+r10+200h], r8d
  movnti DWORD PTR [rsi+r10+200h], r8d
  movnti [rsi+r10+200h], r8
  movnti QWORD PTR [rsi+r10+200h], r8
  movnti [rdi*8+16], r15d
  movnti DWORD PTR [rdi*8+16], r15d
  movnti [rdi*8+16], r15
  movnti QWORD PTR [rdi*8+16], r15
  movnti [rbx+rbp], esi
  movnti DWORD PTR [rbx+rbp], esi
  movnti [rbx+rbp], rsi
  movnti QWORD PTR [rbx+rbp], rsi
  movnti [r11+rsp], eax
  movnti DWORD PTR [r11+rsp], eax
  movnti [r11+rsp], rax
  movnti QWORD PTR [r11+rsp], rax
  movnti [rax-8], ecx
  movnti DWORD PTR [rax-8], ecx
  movnti [rax-8], rcx
  movnti QWORD PTR [rax-8], rcx
  movnti gd, r11d
  movnti gf, r11d
  movnti DWORD PTR gq+4, r11d
  movnti gq, r11
  movnti gr8, r11
  movnti lq+8, r11
  movnti QWORD PTR gx, r11
  movnti gq[rax*8], r11
moves ENDP

control PROC
; prefetchnta
  prefetchnta [rax]
  prefetchnta BYTE PTR [rcx+8]
  prefetchnta WORD PTR [rdx+1000h]
  prefetchnta DWORD PTR [rsp]
  prefetchnta QWORD PTR [rsp+16]
  prefetchnta XMMWORD PTR [rbp]
  prefetchnta OWORD PTR [r13]
  prefetchnta REAL4 PTR [r12]
  prefetchnta REAL8 PTR [r12+40h]
  prefetchnta [rax+rbx*4]
  prefetchnta BYTE PTR [r8+r9*8+16]
  prefetchnta WORD PTR [r15+r14*2-128]
  prefetchnta DWORD PTR [rsi+r10+200h]
  prefetchnta QWORD PTR [rdi*8+16]
  prefetchnta XMMWORD PTR [rbx+rbp]
  prefetchnta OWORD PTR [r11+rsp]
  prefetchnta REAL4 PTR [rax-8]
  prefetchnta gb
  prefetchnta gw
  prefetchnta gd
  prefetchnta gq
  prefetchnta gx
  prefetchnta gf
  prefetchnta gr8
  prefetchnta lq
  prefetchnta lb+1
  prefetchnta gx[rax]
  prefetchnta BYTE PTR gp
; prefetcht0
  prefetcht0 [rax]
  prefetcht0 BYTE PTR [rcx+8]
  prefetcht0 QWORD PTR [rdx+1000h]
  prefetcht0 [rsp]
  prefetcht0 BYTE PTR [rsp+16]
  prefetcht0 QWORD PTR [rbp]
  prefetcht0 [r13]
  prefetcht0 BYTE PTR [r12]
  prefetcht0 QWORD PTR [r12+40h]
  prefetcht0 [rax+rbx*4]
  prefetcht0 BYTE PTR [r8+r9*8+16]
  prefetcht0 QWORD PTR [r15+r14*2-128]
  prefetcht0 [rsi+r10+200h]
  prefetcht0 BYTE PTR [rdi*8+16]
  prefetcht0 QWORD PTR [rbx+rbp]
  prefetcht0 [r11+rsp]
  prefetcht0 BYTE PTR [rax-8]
  prefetcht0 gb
  prefetcht0 gq+64
  prefetcht0 lq
; prefetcht1
  prefetcht1 [rax]
  prefetcht1 BYTE PTR [rcx+8]
  prefetcht1 [rdx+1000h]
  prefetcht1 BYTE PTR [rsp]
  prefetcht1 [rsp+16]
  prefetcht1 BYTE PTR [rbp]
  prefetcht1 [r13]
  prefetcht1 BYTE PTR [r12]
  prefetcht1 [r12+40h]
  prefetcht1 BYTE PTR [rax+rbx*4]
  prefetcht1 [r8+r9*8+16]
  prefetcht1 BYTE PTR [r15+r14*2-128]
  prefetcht1 [rsi+r10+200h]
  prefetcht1 BYTE PTR [rdi*8+16]
  prefetcht1 [rbx+rbp]
  prefetcht1 BYTE PTR [r11+rsp]
  prefetcht1 [rax-8]
  prefetcht1 gx
; prefetcht2
  prefetcht2 [rax]
  prefetcht2 BYTE PTR [rcx+8]
  prefetcht2 [rdx+1000h]
  prefetcht2 BYTE PTR [rsp]
  prefetcht2 [rsp+16]
  prefetcht2 BYTE PTR [rbp]
  prefetcht2 [r13]
  prefetcht2 BYTE PTR [r12]
  prefetcht2 [r12+40h]
  prefetcht2 BYTE PTR [rax+rbx*4]
  prefetcht2 [r8+r9*8+16]
  prefetcht2 BYTE PTR [r15+r14*2-128]
  prefetcht2 [rsi+r10+200h]
  prefetcht2 BYTE PTR [rdi*8+16]
  prefetcht2 [rbx+rbp]
  prefetcht2 BYTE PTR [r11+rsp]
  prefetcht2 [rax-8]
  prefetcht2 gd
; stmxcsr
  stmxcsr [rax]
  stmxcsr DWORD PTR [rcx+8]
  stmxcsr REAL4 PTR [rdx+1000h]
  stmxcsr [rsp]
  stmxcsr DWORD PTR [rsp+16]
  stmxcsr REAL4 PTR [rbp]
  stmxcsr [r13]
  stmxcsr DWORD PTR [r12]
  stmxcsr REAL4 PTR [r12+40h]
  stmxcsr [rax+rbx*4]
  stmxcsr DWORD PTR [r8+r9*8+16]
  stmxcsr REAL4 PTR [r15+r14*2-128]
  stmxcsr [rsi+r10+200h]
  stmxcsr DWORD PTR [rdi*8+16]
  stmxcsr REAL4 PTR [rbx+rbp]
  stmxcsr [r11+rsp]
  stmxcsr DWORD PTR [rax-8]
  stmxcsr gd
  stmxcsr gf
  stmxcsr DWORD PTR gq
  stmxcsr DWORD PTR lq+4
  stmxcsr gd[rcx*4]
; ldmxcsr
  ldmxcsr [rax]
  ldmxcsr DWORD PTR [rcx+8]
  ldmxcsr REAL4 PTR [rdx+1000h]
  ldmxcsr [rsp]
  ldmxcsr DWORD PTR [rsp+16]
  ldmxcsr REAL4 PTR [rbp]
  ldmxcsr [r13]
  ldmxcsr DWORD PTR [r12]
  ldmxcsr REAL4 PTR [r12+40h]
  ldmxcsr [rax+rbx*4]
  ldmxcsr DWORD PTR [r8+r9*8+16]
  ldmxcsr REAL4 PTR [r15+r14*2-128]
  ldmxcsr [rsi+r10+200h]
  ldmxcsr DWORD PTR [rdi*8+16]
  ldmxcsr REAL4 PTR [rbx+rbp]
  ldmxcsr [r11+rsp]
  ldmxcsr DWORD PTR [rax-8]
  ldmxcsr gd
  ldmxcsr gf
  ldmxcsr DWORD PTR gx+12
  ldmxcsr DWORD PTR lq
; fnstcw
  fnstcw [rax]
  fnstcw WORD PTR [rcx+8]
  fnstcw [rdx+1000h]
  fnstcw WORD PTR [rsp]
  fnstcw [rsp+16]
  fnstcw WORD PTR [rbp]
  fnstcw [r13]
  fnstcw WORD PTR [r12]
  fnstcw [r12+40h]
  fnstcw WORD PTR [rax+rbx*4]
  fnstcw [r8+r9*8+16]
  fnstcw WORD PTR [r15+r14*2-128]
  fnstcw [rsi+r10+200h]
  fnstcw WORD PTR [rdi*8+16]
  fnstcw [rbx+rbp]
  fnstcw WORD PTR [r11+rsp]
  fnstcw [rax-8]
  fnstcw gw
  fnstcw WORD PTR gq+2
  fnstcw WORD PTR lq+6
  fnstcw gw[rcx*2]
; fldcw
  fldcw [rax]
  fldcw WORD PTR [rcx+8]
  fldcw [rdx+1000h]
  fldcw WORD PTR [rsp]
  fldcw [rsp+16]
  fldcw WORD PTR [rbp]
  fldcw [r13]
  fldcw WORD PTR [r12]
  fldcw [r12+40h]
  fldcw WORD PTR [rax+rbx*4]
  fldcw [r8+r9*8+16]
  fldcw WORD PTR [r15+r14*2-128]
  fldcw [rsi+r10+200h]
  fldcw WORD PTR [rdi*8+16]
  fldcw [rbx+rbp]
  fldcw WORD PTR [r11+rsp]
  fldcw [rax-8]
  fldcw gw
  fldcw WORD PTR gd
  fldcw WORD PTR lq
; no operands
  fnclex
  fninit
  fwait
  wait
  cpuid
  xgetbv
  pause
  sfence
  lfence
  mfence
  std
  cld
; the string instructions the runtime uses, with REP
  rep movsb
  rep stosb
  rep movsw
  rep stosw
  rep movsd
  rep stosd
  rep movsq
  rep stosq
  movsb
  stosb
  movsq
  stosq
  repe cmpsb
  repne scasb
  repz cmpsq
  repnz scasb
control ENDP

bits PROC
; bsf
  bsf ax, cx
  bsf r9w, r15w
  bsf eax, ecx
  bsf r8d, esp
  bsf ebp, r13d
  bsf rax, rcx
  bsf r15, r8
  bsf rsp, rbp
  bsf r12, r13
  bsf ax, [rax]
  bsf ax, WORD PTR [rax]
  bsf eax, [rcx+8]
  bsf eax, DWORD PTR [rcx+8]
  bsf rax, [rdx+1000h]
  bsf rax, QWORD PTR [rdx+1000h]
  bsf r10w, [rsp]
  bsf r10w, WORD PTR [rsp]
  bsf r11d, [rsp+16]
  bsf r11d, DWORD PTR [rsp+16]
  bsf r12, [rbp]
  bsf r12, QWORD PTR [rbp]
  bsf ax, [r13]
  bsf ax, WORD PTR [r13]
  bsf eax, [r12]
  bsf eax, DWORD PTR [r12]
  bsf rax, [r12+40h]
  bsf rax, QWORD PTR [r12+40h]
  bsf r10w, [rax+rbx*4]
  bsf r10w, WORD PTR [rax+rbx*4]
  bsf r11d, [r8+r9*8+16]
  bsf r11d, DWORD PTR [r8+r9*8+16]
  bsf r12, [r15+r14*2-128]
  bsf r12, QWORD PTR [r15+r14*2-128]
  bsf ax, [rsi+r10+200h]
  bsf ax, WORD PTR [rsi+r10+200h]
  bsf eax, [rdi*8+16]
  bsf eax, DWORD PTR [rdi*8+16]
  bsf rax, [rbx+rbp]
  bsf rax, QWORD PTR [rbx+rbp]
  bsf r10w, [r11+rsp]
  bsf r10w, WORD PTR [r11+rsp]
  bsf r11d, [rax-8]
  bsf r11d, DWORD PTR [rax-8]
  bsf cx, gw
  bsf ecx, gd
  bsf ecx, gf
  bsf rcx, gq
  bsf rcx, gr8
  bsf r9, lq+8
  bsf rdx, QWORD PTR gx
  bsf edx, DWORD PTR lq+4
  bsf r8, gq[rax*8]
; bsr
  bsr ax, cx
  bsr r9w, r15w
  bsr eax, ecx
  bsr r8d, esp
  bsr ebp, r13d
  bsr rax, rcx
  bsr r15, r8
  bsr rsp, rbp
  bsr r12, r13
  bsr ax, [rax]
  bsr ax, WORD PTR [rax]
  bsr eax, [rcx+8]
  bsr eax, DWORD PTR [rcx+8]
  bsr rax, [rdx+1000h]
  bsr rax, QWORD PTR [rdx+1000h]
  bsr r10w, [rsp]
  bsr r10w, WORD PTR [rsp]
  bsr r11d, [rsp+16]
  bsr r11d, DWORD PTR [rsp+16]
  bsr r12, [rbp]
  bsr r12, QWORD PTR [rbp]
  bsr ax, [r13]
  bsr ax, WORD PTR [r13]
  bsr eax, [r12]
  bsr eax, DWORD PTR [r12]
  bsr rax, [r12+40h]
  bsr rax, QWORD PTR [r12+40h]
  bsr r10w, [rax+rbx*4]
  bsr r10w, WORD PTR [rax+rbx*4]
  bsr r11d, [r8+r9*8+16]
  bsr r11d, DWORD PTR [r8+r9*8+16]
  bsr r12, [r15+r14*2-128]
  bsr r12, QWORD PTR [r15+r14*2-128]
  bsr ax, [rsi+r10+200h]
  bsr ax, WORD PTR [rsi+r10+200h]
  bsr eax, [rdi*8+16]
  bsr eax, DWORD PTR [rdi*8+16]
  bsr rax, [rbx+rbp]
  bsr rax, QWORD PTR [rbx+rbp]
  bsr r10w, [r11+rsp]
  bsr r10w, WORD PTR [r11+rsp]
  bsr r11d, [rax-8]
  bsr r11d, DWORD PTR [rax-8]
  bsr cx, gw
  bsr ecx, gd
  bsr ecx, gf
  bsr rcx, gq
  bsr rcx, gr8
  bsr r9, lq+8
  bsr rdx, QWORD PTR gx
  bsr edx, DWORD PTR lq+4
  bsr r8, gq[rax*8]
; tzcnt
  tzcnt ax, cx
  tzcnt r9w, r15w
  tzcnt eax, ecx
  tzcnt r8d, esp
  tzcnt ebp, r13d
  tzcnt rax, rcx
  tzcnt r15, r8
  tzcnt rsp, rbp
  tzcnt r12, r13
  tzcnt ax, [rax]
  tzcnt ax, WORD PTR [rax]
  tzcnt eax, [rcx+8]
  tzcnt eax, DWORD PTR [rcx+8]
  tzcnt rax, [rdx+1000h]
  tzcnt rax, QWORD PTR [rdx+1000h]
  tzcnt r10w, [rsp]
  tzcnt r10w, WORD PTR [rsp]
  tzcnt r11d, [rsp+16]
  tzcnt r11d, DWORD PTR [rsp+16]
  tzcnt r12, [rbp]
  tzcnt r12, QWORD PTR [rbp]
  tzcnt ax, [r13]
  tzcnt ax, WORD PTR [r13]
  tzcnt eax, [r12]
  tzcnt eax, DWORD PTR [r12]
  tzcnt rax, [r12+40h]
  tzcnt rax, QWORD PTR [r12+40h]
  tzcnt r10w, [rax+rbx*4]
  tzcnt r10w, WORD PTR [rax+rbx*4]
  tzcnt r11d, [r8+r9*8+16]
  tzcnt r11d, DWORD PTR [r8+r9*8+16]
  tzcnt r12, [r15+r14*2-128]
  tzcnt r12, QWORD PTR [r15+r14*2-128]
  tzcnt ax, [rsi+r10+200h]
  tzcnt ax, WORD PTR [rsi+r10+200h]
  tzcnt eax, [rdi*8+16]
  tzcnt eax, DWORD PTR [rdi*8+16]
  tzcnt rax, [rbx+rbp]
  tzcnt rax, QWORD PTR [rbx+rbp]
  tzcnt r10w, [r11+rsp]
  tzcnt r10w, WORD PTR [r11+rsp]
  tzcnt r11d, [rax-8]
  tzcnt r11d, DWORD PTR [rax-8]
  tzcnt cx, gw
  tzcnt ecx, gd
  tzcnt ecx, gf
  tzcnt rcx, gq
  tzcnt rcx, gr8
  tzcnt r9, lq+8
  tzcnt rdx, QWORD PTR gx
  tzcnt edx, DWORD PTR lq+4
  tzcnt r8, gq[rax*8]
; lzcnt
  lzcnt ax, cx
  lzcnt r9w, r15w
  lzcnt eax, ecx
  lzcnt r8d, esp
  lzcnt ebp, r13d
  lzcnt rax, rcx
  lzcnt r15, r8
  lzcnt rsp, rbp
  lzcnt r12, r13
  lzcnt ax, [rax]
  lzcnt ax, WORD PTR [rax]
  lzcnt eax, [rcx+8]
  lzcnt eax, DWORD PTR [rcx+8]
  lzcnt rax, [rdx+1000h]
  lzcnt rax, QWORD PTR [rdx+1000h]
  lzcnt r10w, [rsp]
  lzcnt r10w, WORD PTR [rsp]
  lzcnt r11d, [rsp+16]
  lzcnt r11d, DWORD PTR [rsp+16]
  lzcnt r12, [rbp]
  lzcnt r12, QWORD PTR [rbp]
  lzcnt ax, [r13]
  lzcnt ax, WORD PTR [r13]
  lzcnt eax, [r12]
  lzcnt eax, DWORD PTR [r12]
  lzcnt rax, [r12+40h]
  lzcnt rax, QWORD PTR [r12+40h]
  lzcnt r10w, [rax+rbx*4]
  lzcnt r10w, WORD PTR [rax+rbx*4]
  lzcnt r11d, [r8+r9*8+16]
  lzcnt r11d, DWORD PTR [r8+r9*8+16]
  lzcnt r12, [r15+r14*2-128]
  lzcnt r12, QWORD PTR [r15+r14*2-128]
  lzcnt ax, [rsi+r10+200h]
  lzcnt ax, WORD PTR [rsi+r10+200h]
  lzcnt eax, [rdi*8+16]
  lzcnt eax, DWORD PTR [rdi*8+16]
  lzcnt rax, [rbx+rbp]
  lzcnt rax, QWORD PTR [rbx+rbp]
  lzcnt r10w, [r11+rsp]
  lzcnt r10w, WORD PTR [r11+rsp]
  lzcnt r11d, [rax-8]
  lzcnt r11d, DWORD PTR [rax-8]
  lzcnt cx, gw
  lzcnt ecx, gd
  lzcnt ecx, gf
  lzcnt rcx, gq
  lzcnt rcx, gr8
  lzcnt r9, lq+8
  lzcnt rdx, QWORD PTR gx
  lzcnt edx, DWORD PTR lq+4
  lzcnt r8, gq[rax*8]
; popcnt
  popcnt ax, cx
  popcnt r9w, r15w
  popcnt eax, ecx
  popcnt r8d, esp
  popcnt ebp, r13d
  popcnt rax, rcx
  popcnt r15, r8
  popcnt rsp, rbp
  popcnt r12, r13
  popcnt ax, [rax]
  popcnt ax, WORD PTR [rax]
  popcnt eax, [rcx+8]
  popcnt eax, DWORD PTR [rcx+8]
  popcnt rax, [rdx+1000h]
  popcnt rax, QWORD PTR [rdx+1000h]
  popcnt r10w, [rsp]
  popcnt r10w, WORD PTR [rsp]
  popcnt r11d, [rsp+16]
  popcnt r11d, DWORD PTR [rsp+16]
  popcnt r12, [rbp]
  popcnt r12, QWORD PTR [rbp]
  popcnt ax, [r13]
  popcnt ax, WORD PTR [r13]
  popcnt eax, [r12]
  popcnt eax, DWORD PTR [r12]
  popcnt rax, [r12+40h]
  popcnt rax, QWORD PTR [r12+40h]
  popcnt r10w, [rax+rbx*4]
  popcnt r10w, WORD PTR [rax+rbx*4]
  popcnt r11d, [r8+r9*8+16]
  popcnt r11d, DWORD PTR [r8+r9*8+16]
  popcnt r12, [r15+r14*2-128]
  popcnt r12, QWORD PTR [r15+r14*2-128]
  popcnt ax, [rsi+r10+200h]
  popcnt ax, WORD PTR [rsi+r10+200h]
  popcnt eax, [rdi*8+16]
  popcnt eax, DWORD PTR [rdi*8+16]
  popcnt rax, [rbx+rbp]
  popcnt rax, QWORD PTR [rbx+rbp]
  popcnt r10w, [r11+rsp]
  popcnt r10w, WORD PTR [r11+rsp]
  popcnt r11d, [rax-8]
  popcnt r11d, DWORD PTR [rax-8]
  popcnt cx, gw
  popcnt ecx, gd
  popcnt ecx, gf
  popcnt rcx, gq
  popcnt rcx, gr8
  popcnt r9, lq+8
  popcnt rdx, QWORD PTR gx
  popcnt edx, DWORD PTR lq+4
  popcnt r8, gq[rax*8]
bits ENDP

END
