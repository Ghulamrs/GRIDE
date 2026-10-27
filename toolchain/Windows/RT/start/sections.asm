; sections.asm - the markers around the initialiser tables, as the Microsoft runtime's are.
;
; The compilers put a pointer to each translation unit's initialiser in .CRT$XCU (cpp11's dynamic
; initialisers) and C's in .CRT$XIU; GRIDE's link orders the contributions of one output section by
; the name after the $, so XCA comes before every XCU and XCZ after them. start.c walks from the A
; marker to the Z marker. Each marker is a null pointer, and null entries are skipped.

PUBLIC __xi_a
PUBLIC __xi_z
PUBLIC __xc_a
PUBLIC __xc_z

.CRT$XIA SEGMENT READONLY ALIGN(8) 'DATA'
__xi_a DQ 0
.CRT$XIA ENDS

.CRT$XIZ SEGMENT READONLY ALIGN(8) 'DATA'
__xi_z DQ 0
.CRT$XIZ ENDS

.CRT$XCA SEGMENT READONLY ALIGN(8) 'DATA'
__xc_a DQ 0
.CRT$XCA ENDS

.CRT$XCZ SEGMENT READONLY ALIGN(8) 'DATA'
__xc_z DQ 0
.CRT$XCZ ENDS

END
