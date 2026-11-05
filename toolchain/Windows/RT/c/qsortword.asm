; qsortword.asm - qsort for elements of 4 and 8 bytes (grtc), called by c\qsort.cpp:
; __grt_qsort4(base, count, compare) and __grt_qsort8. The algorithm is qsort.cpp's step by step - and so the
; Microsoft runtime's, which decides the order of elements that compare equal: a part of 8 or fewer elements sorted by
; selection, a larger one split around the median of three, the smaller side next, the larger on a stack of 62 parts.
; The C++ kept its pointers in memory across every call of the comparison; here they live in the registers the
; Microsoft x64 convention keeps across calls:
;   rbx lo, rsi hi, rdi mid (the selection's greatest), r12 up (the selection's next), r13 down,
;   r14 the comparison, r15 the stack's depth; the stack of parts above the comparison's home space.
; A comparison that throws unwinds through the frame by its unwind data. Written by qsortasm.py.

OPTION DOTNAME
OPTION NOSCOPED

PUBLIC __grt_qsort4
PUBLIC __grt_qsort8

_TEXT SEGMENT

__grt_qsort4 PROC FRAME
    push rbx
    .PUSHREG rbx
    push rsi
    .PUSHREG rsi
    push rdi
    .PUSHREG rdi
    push r12
    .PUSHREG r12
    push r13
    .PUSHREG r13
    push r14
    .PUSHREG r14
    push r15
    .PUSHREG r15
    sub rsp, 1024                   ; home space 32, the stack's lo parts at 32 and hi parts at 528
    .ALLOCSTACK 1024
    .ENDPROLOG
    mov r14, r8
    xor r15d, r15d
    mov rbx, rcx
    lea rsi, [rdx-1]
    shl rsi, 2
    add rsi, rbx                    ; hi = lo + (count - 1) * width
$q4_part:
    mov rax, rsi
    sub rax, rbx
    cmp rax, 28
    ja $q4_split
$q4_select:                       ; 8 or fewer: the greatest, the first of equal ones, swapped to the end
    cmp rsi, rbx
    jbe $q4_pop
    mov rdi, rbx
    lea r12, [rbx+4]
$q4_scan:
    cmp r12, rsi
    ja $q4_last
    mov rcx, r12
    mov rdx, rdi
    call r14
    test eax, eax
    jle $q4_scanned
    mov rdi, r12
$q4_scanned:
    add r12, 4
    jmp $q4_scan
$q4_last:
    mov eax, DWORD PTR [rdi]
    mov edx, DWORD PTR [rsi]
    mov DWORD PTR [rdi], edx
    mov DWORD PTR [rsi], eax
    sub rsi, 4
    jmp $q4_select
$q4_split:                        ; mid = lo + (size / 2) * width, size = (hi - lo) / width + 1
    shr rax, 2
    inc rax
    shr rax, 1
    shl rax, 2
    lea rdi, [rbx+rax]
    mov rcx, rbx
    mov rdx, rdi
    call r14
    test eax, eax
    jle $q4_m1
    mov eax, DWORD PTR [rbx]
    mov edx, DWORD PTR [rdi]
    mov DWORD PTR [rbx], edx
    mov DWORD PTR [rdi], eax
$q4_m1:
    mov rcx, rbx
    mov rdx, rsi
    call r14
    test eax, eax
    jle $q4_m2
    mov eax, DWORD PTR [rbx]
    mov edx, DWORD PTR [rsi]
    mov DWORD PTR [rbx], edx
    mov DWORD PTR [rsi], eax
$q4_m2:
    mov rcx, rdi
    mov rdx, rsi
    call r14
    test eax, eax
    jle $q4_m3
    mov eax, DWORD PTR [rdi]
    mov edx, DWORD PTR [rsi]
    mov DWORD PTR [rdi], edx
    mov DWORD PTR [rsi], eax
$q4_m3:
    mov r12, rbx
    mov r13, rsi
$q4_loop:
    cmp rdi, r12                    ; mid > up: up on while below mid and not above it
    jbe $q4_past
$q4_before:
    add r12, 4
    cmp r12, rdi
    jae $q4_past
    mov rcx, r12
    mov rdx, rdi
    call r14
    test eax, eax
    jle $q4_before
$q4_past:
    cmp rdi, r12                    ; mid <= up: up on while within hi and not above mid
    ja $q4_down
$q4_after:
    add r12, 4
    cmp r12, rsi
    ja $q4_down
    mov rcx, r12
    mov rdx, rdi
    call r14
    test eax, eax
    jle $q4_after
$q4_down:                         ; down back while above mid and above it
    sub r13, 4
    cmp r13, rdi
    jbe $q4_met
    mov rcx, r13
    mov rdx, rdi
    call r14
    test eax, eax
    jg $q4_down
$q4_met:
    cmp r13, r12
    jb $q4_apart
    mov eax, DWORD PTR [r12]
    mov edx, DWORD PTR [r13]
    mov DWORD PTR [r12], edx
    mov DWORD PTR [r13], eax
    cmp rdi, r13
    jne $q4_loop
    mov rdi, r12
    jmp $q4_loop
$q4_apart:
    add r13, 4
    cmp rdi, r13                    ; mid < down: down back over the elements equal to mid
    jae $q4_low
$q4_equal:
    sub r13, 4
    cmp r13, rdi
    jbe $q4_low
    mov rcx, r13
    mov rdx, rdi
    call r14
    test eax, eax
    jz $q4_equal
$q4_low:
    cmp rdi, r13                    ; mid >= down: down back while above lo and equal to mid
    jb $q4_sides
$q4_lower:
    sub r13, 4
    cmp r13, rbx
    jbe $q4_sides
    mov rcx, r13
    mov rdx, rdi
    call r14
    test eax, eax
    jz $q4_lower
$q4_sides:                        ; down - lo >= hi - up, as signed differences: the left side waits
    mov rax, r13
    sub rax, rbx
    mov rcx, rsi
    sub rcx, r12
    cmp rax, rcx
    jl $q4_right
    cmp rbx, r13
    jae $q4_leftnone
    mov QWORD PTR [rsp+r15*8+32], rbx
    mov QWORD PTR [rsp+r15*8+528], r13
    inc r15
$q4_leftnone:
    cmp r12, rsi
    jae $q4_pop
    mov rbx, r12
    jmp $q4_part
$q4_right:
    cmp r12, rsi
    jae $q4_rightnone
    mov QWORD PTR [rsp+r15*8+32], r12
    mov QWORD PTR [rsp+r15*8+528], rsi
    inc r15
$q4_rightnone:
    cmp rbx, r13
    jae $q4_pop
    mov rsi, r13
    jmp $q4_part
$q4_pop:
    dec r15
    js $q4_done
    mov rbx, QWORD PTR [rsp+r15*8+32]
    mov rsi, QWORD PTR [rsp+r15*8+528]
    jmp $q4_part
$q4_done:
    add rsp, 1024
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret
__grt_qsort4 ENDP

__grt_qsort8 PROC FRAME
    push rbx
    .PUSHREG rbx
    push rsi
    .PUSHREG rsi
    push rdi
    .PUSHREG rdi
    push r12
    .PUSHREG r12
    push r13
    .PUSHREG r13
    push r14
    .PUSHREG r14
    push r15
    .PUSHREG r15
    sub rsp, 1024                   ; home space 32, the stack's lo parts at 32 and hi parts at 528
    .ALLOCSTACK 1024
    .ENDPROLOG
    mov r14, r8
    xor r15d, r15d
    mov rbx, rcx
    lea rsi, [rdx-1]
    shl rsi, 3
    add rsi, rbx                    ; hi = lo + (count - 1) * width
$q8_part:
    mov rax, rsi
    sub rax, rbx
    cmp rax, 56
    ja $q8_split
$q8_select:                       ; 8 or fewer: the greatest, the first of equal ones, swapped to the end
    cmp rsi, rbx
    jbe $q8_pop
    mov rdi, rbx
    lea r12, [rbx+8]
$q8_scan:
    cmp r12, rsi
    ja $q8_last
    mov rcx, r12
    mov rdx, rdi
    call r14
    test eax, eax
    jle $q8_scanned
    mov rdi, r12
$q8_scanned:
    add r12, 8
    jmp $q8_scan
$q8_last:
    mov rax, QWORD PTR [rdi]
    mov rdx, QWORD PTR [rsi]
    mov QWORD PTR [rdi], rdx
    mov QWORD PTR [rsi], rax
    sub rsi, 8
    jmp $q8_select
$q8_split:                        ; mid = lo + (size / 2) * width, size = (hi - lo) / width + 1
    shr rax, 3
    inc rax
    shr rax, 1
    shl rax, 3
    lea rdi, [rbx+rax]
    mov rcx, rbx
    mov rdx, rdi
    call r14
    test eax, eax
    jle $q8_m1
    mov rax, QWORD PTR [rbx]
    mov rdx, QWORD PTR [rdi]
    mov QWORD PTR [rbx], rdx
    mov QWORD PTR [rdi], rax
$q8_m1:
    mov rcx, rbx
    mov rdx, rsi
    call r14
    test eax, eax
    jle $q8_m2
    mov rax, QWORD PTR [rbx]
    mov rdx, QWORD PTR [rsi]
    mov QWORD PTR [rbx], rdx
    mov QWORD PTR [rsi], rax
$q8_m2:
    mov rcx, rdi
    mov rdx, rsi
    call r14
    test eax, eax
    jle $q8_m3
    mov rax, QWORD PTR [rdi]
    mov rdx, QWORD PTR [rsi]
    mov QWORD PTR [rdi], rdx
    mov QWORD PTR [rsi], rax
$q8_m3:
    mov r12, rbx
    mov r13, rsi
$q8_loop:
    cmp rdi, r12                    ; mid > up: up on while below mid and not above it
    jbe $q8_past
$q8_before:
    add r12, 8
    cmp r12, rdi
    jae $q8_past
    mov rcx, r12
    mov rdx, rdi
    call r14
    test eax, eax
    jle $q8_before
$q8_past:
    cmp rdi, r12                    ; mid <= up: up on while within hi and not above mid
    ja $q8_down
$q8_after:
    add r12, 8
    cmp r12, rsi
    ja $q8_down
    mov rcx, r12
    mov rdx, rdi
    call r14
    test eax, eax
    jle $q8_after
$q8_down:                         ; down back while above mid and above it
    sub r13, 8
    cmp r13, rdi
    jbe $q8_met
    mov rcx, r13
    mov rdx, rdi
    call r14
    test eax, eax
    jg $q8_down
$q8_met:
    cmp r13, r12
    jb $q8_apart
    mov rax, QWORD PTR [r12]
    mov rdx, QWORD PTR [r13]
    mov QWORD PTR [r12], rdx
    mov QWORD PTR [r13], rax
    cmp rdi, r13
    jne $q8_loop
    mov rdi, r12
    jmp $q8_loop
$q8_apart:
    add r13, 8
    cmp rdi, r13                    ; mid < down: down back over the elements equal to mid
    jae $q8_low
$q8_equal:
    sub r13, 8
    cmp r13, rdi
    jbe $q8_low
    mov rcx, r13
    mov rdx, rdi
    call r14
    test eax, eax
    jz $q8_equal
$q8_low:
    cmp rdi, r13                    ; mid >= down: down back while above lo and equal to mid
    jb $q8_sides
$q8_lower:
    sub r13, 8
    cmp r13, rbx
    jbe $q8_sides
    mov rcx, r13
    mov rdx, rdi
    call r14
    test eax, eax
    jz $q8_lower
$q8_sides:                        ; down - lo >= hi - up, as signed differences: the left side waits
    mov rax, r13
    sub rax, rbx
    mov rcx, rsi
    sub rcx, r12
    cmp rax, rcx
    jl $q8_right
    cmp rbx, r13
    jae $q8_leftnone
    mov QWORD PTR [rsp+r15*8+32], rbx
    mov QWORD PTR [rsp+r15*8+528], r13
    inc r15
$q8_leftnone:
    cmp r12, rsi
    jae $q8_pop
    mov rbx, r12
    jmp $q8_part
$q8_right:
    cmp r12, rsi
    jae $q8_rightnone
    mov QWORD PTR [rsp+r15*8+32], r12
    mov QWORD PTR [rsp+r15*8+528], rsi
    inc r15
$q8_rightnone:
    cmp rbx, r13
    jae $q8_pop
    mov rsi, r13
    jmp $q8_part
$q8_pop:
    dec r15
    js $q8_done
    mov rbx, QWORD PTR [rsp+r15*8+32]
    mov rsi, QWORD PTR [rsp+r15*8+528]
    jmp $q8_part
$q8_done:
    add rsp, 1024
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret
__grt_qsort8 ENDP

_TEXT ENDS

END
