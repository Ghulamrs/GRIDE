// Spec: ISO C 7.19.1 (FILE), 7.13 (jmp_buf), 7.23.1 (struct tm), 7.11.2 (lconv); SPRAB89B 9.18 (_ftable).
// RTS6x's own layouts of the types its C library shares with the compilers' headers. Each value
// here is checked against cpp11's and c90's headers by tests/layout, so the two cannot drift apart.
#ifndef RTS6X_H
#define RTS6X_H

#define RTS6X_FILE_SIZE        24   // struct _IO_FILE: fd, buf, pos, bufend, buff_stop, flags
#define RTS6X_FILE_FD           0
#define RTS6X_FILE_BUF          4
#define RTS6X_FILE_POS          8
#define RTS6X_FILE_BUFEND      12
#define RTS6X_FILE_BUFF_STOP   16
#define RTS6X_FILE_FLAGS       20
#define RTS6X_FTABLE_COUNT     20   // _ftable[]: stdin, stdout, stderr first (SPRAB89B 9.18)
#define RTS6X_JMP_BUF_SIZE    100
#define RTS6X_TM_SIZE          44
#define RTS6X_LCONV_SIZE       48
#define RTS6X_TIME_T_SIZE       4
#define RTS6X_CLOCK_T_SIZE      4

#endif
