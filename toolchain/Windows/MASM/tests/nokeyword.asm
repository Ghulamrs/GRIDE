; nokeyword.asm - OPTION NOKEYWORD:<names>: the words listed stop being reserved for the rest of the file,
; so a C function can be named like an x87 instruction (fabs), an instruction (div, lea), a register, an
; operator or a directive, and the object's symbol is that name. Checked against ml64 by check.py.
;
; The list is in angle brackets, its names apart by spaces or commas, in any case; there may be several
; OPTION NOKEYWORD lines. Options before NOKEYWORD on its line count; ml64 ignores the rest of the line
; after the list, so PROC:PRIVATE below has no effect and the PROCs are public. A word listed is a name
; from then on, never the instruction or directive it was: this file cannot write `lea` or `offset`.

OPTION NOKEYWORD:<fabs div>
OPTION NOKEYWORD:<FSqrt>
OPTION NOKEYWORD:<lea,str , sete>
OPTION DOTNAME, NOKEYWORD:<eax xmm7 offset low not near title page org comm segment includelib>
OPTION NOKEYWORD:<push>, PROC:PRIVATE

PUBLIC fabs, fsqrt, title
EXTERN div:PROC, lea:PROC
EXTERN str:QWORD
EXTERNDEF sete:PROC
EXTERN eax:PROC
EXTERN xmm7:QWORD
EXTERN offset:PROC, low:QWORD, not:DWORD
EXTERN push:PROC

.DATA
tab DQ fabs, div, fsqrt, eax, lea, push
page DQ 0
comm DD 1

.CODE
fabs PROC
  call div
  call lea
  call sete
  call eax
  call offset
  call push
  mov rcx, str
  mov rdx, QWORD PTR str+8
  movq xmm0, xmm7
  mov ecx, not
  mov rax, page
  mov edx, comm
  add rcx, low
  jmp near
near:
  ret
fabs ENDP

fsqrt PROC
  jmp fabs
fsqrt ENDP

title PROC
  call fsqrt
  call QWORD PTR tab+8
  ret
title ENDP

segment PROC
  call org
  ret
segment ENDP

org PROC
  call segment
  call includelib
  ret
org ENDP

includelib PROC
  ret
includelib ENDP

END
