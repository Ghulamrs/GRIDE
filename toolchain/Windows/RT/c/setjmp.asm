; setjmp.asm - _setjmp and longjmp, in the Microsoft runtime's x64 jump buffer.
;
; Spec: ISO C 7.13 and the layout the compilers' <setjmp.h> sizes jmp_buf for (256 bytes): the frame
; word, rbx, rsp, rbp, rsi, rdi, r12-r15, rip, mxcsr, the x87 control word, xmm6-xmm15. _setjmp takes
; the buffer and a frame (the header passes 0, cl's hidden second argument); longjmp restores the
; registers and returns from _setjmp again with its value, 1 for 0. A frame of 0 means no unwind: C's
; longjmp, no destructor run. stmxcsr, ldmxcsr, fnstcw, fldcw and fnclex are written as bytes - GRIDE's
; masm does not spell them - with the buffer in rcx: [rcx+88] is mxcsr, [rcx+92] the control word.

PUBLIC _setjmp
PUBLIC longjmp

.CODE
_setjmp PROC
  mov QWORD PTR [rcx], rdx
  mov QWORD PTR [rcx+8], rbx
  lea rax, [rsp+8]
  mov QWORD PTR [rcx+16], rax
  mov QWORD PTR [rcx+24], rbp
  mov QWORD PTR [rcx+32], rsi
  mov QWORD PTR [rcx+40], rdi
  mov QWORD PTR [rcx+48], r12
  mov QWORD PTR [rcx+56], r13
  mov QWORD PTR [rcx+64], r14
  mov QWORD PTR [rcx+72], r15
  mov rax, QWORD PTR [rsp]
  mov QWORD PTR [rcx+80], rax
  DB 0Fh, 0AEh, 59h, 58h          ; stmxcsr DWORD PTR [rcx+88]
  DB 0D9h, 79h, 5Ch               ; fnstcw WORD PTR [rcx+92]
  movdqu XMMWORD PTR [rcx+96], xmm6
  movdqu XMMWORD PTR [rcx+112], xmm7
  movdqu XMMWORD PTR [rcx+128], xmm8
  movdqu XMMWORD PTR [rcx+144], xmm9
  movdqu XMMWORD PTR [rcx+160], xmm10
  movdqu XMMWORD PTR [rcx+176], xmm11
  movdqu XMMWORD PTR [rcx+192], xmm12
  movdqu XMMWORD PTR [rcx+208], xmm13
  movdqu XMMWORD PTR [rcx+224], xmm14
  movdqu XMMWORD PTR [rcx+240], xmm15
  xor eax, eax
  ret
_setjmp ENDP

longjmp PROC
  mov eax, edx
  cmp eax, 0
  jne $value
  mov eax, 1
$value:
  mov rbx, QWORD PTR [rcx+8]
  mov rbp, QWORD PTR [rcx+24]
  mov rsi, QWORD PTR [rcx+32]
  mov rdi, QWORD PTR [rcx+40]
  mov r12, QWORD PTR [rcx+48]
  mov r13, QWORD PTR [rcx+56]
  mov r14, QWORD PTR [rcx+64]
  mov r15, QWORD PTR [rcx+72]
  DB 0Fh, 0AEh, 51h, 58h          ; ldmxcsr DWORD PTR [rcx+88]
  DB 0DBh, 0E2h                   ; fnclex
  DB 0D9h, 69h, 5Ch               ; fldcw WORD PTR [rcx+92]
  movdqu xmm6, XMMWORD PTR [rcx+96]
  movdqu xmm7, XMMWORD PTR [rcx+112]
  movdqu xmm8, XMMWORD PTR [rcx+128]
  movdqu xmm9, XMMWORD PTR [rcx+144]
  movdqu xmm10, XMMWORD PTR [rcx+160]
  movdqu xmm11, XMMWORD PTR [rcx+176]
  movdqu xmm12, XMMWORD PTR [rcx+192]
  movdqu xmm13, XMMWORD PTR [rcx+208]
  movdqu xmm14, XMMWORD PTR [rcx+224]
  movdqu xmm15, XMMWORD PTR [rcx+240]
  mov rdx, QWORD PTR [rcx+80]
  mov rsp, QWORD PTR [rcx+16]
  jmp rdx
longjmp ENDP

END
