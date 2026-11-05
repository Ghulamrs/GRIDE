// errno.h - the last error number.
//
// errno is required to be a modifiable lvalue and is *not* required to be a
// variable, which is the whole difficulty. Every one of these platforms makes
// it thread-local by hiding a function behind a macro, and each hides a
// different one:
//
//     glibc     (*__errno_location())
//     macOS     (*__error())
//     UCRT      (*_errno())
//
// A header declaring 'extern int errno;' would link on none of them. So the
// function is declared and the macro dereferences it, which is what the
// platform's own header does.
//
// Only EDOM and ERANGE are C90's; everything else a program tests for is
// POSIX. The two that C90 requires agree across all three targets at 33 and 34
// - measured rather than assumed - and the rest are given their real values
// per platform, because a program comparing errno to ENOENT wants the number
// its own C library will set.
#ifndef _CC1_ERRNO_H
#define _CC1_ERRNO_H

// Everything here is the C library, so it is named the way C names it.
// Without this the compiler would give every declaration below a C++
// linkage name and ask the linker for a symbol libc has never had.
#ifdef __cplusplus
extern "C" {
#endif


#ifdef _WIN32
int *_errno(void);
#define errno (*_errno())
#elif defined(__APPLE__)
int *__error(void);
#define errno (*__error())
#elif defined(__TMS320C6X__)
/* TI's spelling under the EABI, and the VM6747 emulator answers to it. */
int *__c6xabi_errno_addr(void);
#define errno (*__c6xabi_errno_addr())
#else
/* glibc's spelling, and the VM6747 emulator answers to the same name. */
int *__errno_location(void);
#define errno (*__errno_location())
#endif

// C90's two, and the same number everywhere.
#define EDOM    33
#define ERANGE  34

// EILSEQ is C95 and genuinely different on each platform, which is why it is
// worth guarding rather than picking one: 84 under glibc, 92 on macOS, 42
// under the UCRT.
#ifdef _WIN32
#define EILSEQ  42
#elif defined(__APPLE__)
#define EILSEQ  92
#else
#define EILSEQ  84
#endif

// [errno]/1: the rest of POSIX's names C++11 lists, each with its own C library's number - the
// low ones agree, the networking ones are numbered three ways.
#ifdef _WIN32
#define E2BIG           7
#define EACCES          13
#define EADDRINUSE      100
#define EADDRNOTAVAIL   101
#define EAFNOSUPPORT    102
#define EAGAIN          11
#define EALREADY        103
#define EBADF           9
#define EBADMSG         104
#define EBUSY           16
#define ECANCELED       105
#define ECHILD          10
#define ECONNABORTED    106
#define ECONNREFUSED    107
#define ECONNRESET      108
#define EDEADLK         36
#define EDESTADDRREQ    109
#define EEXIST          17
#define EFAULT          14
#define EFBIG           27
#define EHOSTUNREACH    110
#define EIDRM           111
#define EINPROGRESS     112
#define EINTR           4
#define EINVAL          22
#define EIO             5
#define EISCONN         113
#define EISDIR          21
#define ELOOP           114
#define EMFILE          24
#define EMLINK          31
#define EMSGSIZE        115
#define ENAMETOOLONG    38
#define ENETDOWN        116
#define ENETRESET       117
#define ENETUNREACH     118
#define ENFILE          23
#define ENOBUFS         119
#define ENODATA         120
#define ENODEV          19
#define ENOENT          2
#define ENOEXEC         8
#define ENOLCK          39
#define ENOLINK         121
#define ENOMEM          12
#define ENOMSG          122
#define ENOPROTOOPT     123
#define ENOSPC          28
#define ENOSR           124
#define ENOSTR          125
#define ENOSYS          40
#define ENOTCONN        126
#define ENOTDIR         20
#define ENOTEMPTY       41
#define ENOTRECOVERABLE 127
#define ENOTSOCK        128
#define ENOTSUP         129
#define ENOTTY          25
#define ENXIO           6
#define EOPNOTSUPP      130
#define EOVERFLOW       132
#define EOWNERDEAD      133
#define EPERM           1
#define EPIPE           32
#define EPROTO          134
#define EPROTONOSUPPORT 135
#define EPROTOTYPE      136
#define EROFS           30
#define ESPIPE          29
#define ESRCH           3
#define ETIME           137
#define ETIMEDOUT       138
#define ETXTBSY         139
#define EWOULDBLOCK     140
#define EXDEV           18
#elif defined(__APPLE__)
#define E2BIG           7
#define EACCES          13
#define EADDRINUSE      48
#define EADDRNOTAVAIL   49
#define EAFNOSUPPORT    47
#define EAGAIN          35
#define EALREADY        37
#define EBADF           9
#define EBADMSG         94
#define EBUSY           16
#define ECANCELED       89
#define ECHILD          10
#define ECONNABORTED    53
#define ECONNREFUSED    61
#define ECONNRESET      54
#define EDEADLK         11
#define EDESTADDRREQ    39
#define EEXIST          17
#define EFAULT          14
#define EFBIG           27
#define EHOSTUNREACH    65
#define EIDRM           90
#define EINPROGRESS     36
#define EINTR           4
#define EINVAL          22
#define EIO             5
#define EISCONN         56
#define EISDIR          21
#define ELOOP           62
#define EMFILE          24
#define EMLINK          31
#define EMSGSIZE        40
#define ENAMETOOLONG    63
#define ENETDOWN        50
#define ENETRESET       52
#define ENETUNREACH     51
#define ENFILE          23
#define ENOBUFS         55
#define ENODATA         96
#define ENODEV          19
#define ENOENT          2
#define ENOEXEC         8
#define ENOLCK          77
#define ENOLINK         97
#define ENOMEM          12
#define ENOMSG          91
#define ENOPROTOOPT     42
#define ENOSPC          28
#define ENOSR           98
#define ENOSTR          99
#define ENOSYS          78
#define ENOTCONN        57
#define ENOTDIR         20
#define ENOTEMPTY       66
#define ENOTRECOVERABLE 104
#define ENOTSOCK        38
#define ENOTSUP         45
#define ENOTTY          25
#define ENXIO           6
#define EOPNOTSUPP      102
#define EOVERFLOW       84
#define EOWNERDEAD      105
#define EPERM           1
#define EPIPE           32
#define EPROTO          100
#define EPROTONOSUPPORT 43
#define EPROTOTYPE      41
#define EROFS           30
#define ESPIPE          29
#define ESRCH           3
#define ETIME           101
#define ETIMEDOUT       60
#define ETXTBSY         26
#define EWOULDBLOCK     35
#define EXDEV           18
#else
#define E2BIG           7
#define EACCES          13
#define EADDRINUSE      98
#define EADDRNOTAVAIL   99
#define EAFNOSUPPORT    97
#define EAGAIN          11
#define EALREADY        114
#define EBADF           9
#define EBADMSG         74
#define EBUSY           16
#define ECANCELED       125
#define ECHILD          10
#define ECONNABORTED    103
#define ECONNREFUSED    111
#define ECONNRESET      104
#define EDEADLK         35
#define EDESTADDRREQ    89
#define EEXIST          17
#define EFAULT          14
#define EFBIG           27
#define EHOSTUNREACH    113
#define EIDRM           43
#define EINPROGRESS     115
#define EINTR           4
#define EINVAL          22
#define EIO             5
#define EISCONN         106
#define EISDIR          21
#define ELOOP           40
#define EMFILE          24
#define EMLINK          31
#define EMSGSIZE        90
#define ENAMETOOLONG    36
#define ENETDOWN        100
#define ENETRESET       102
#define ENETUNREACH     101
#define ENFILE          23
#define ENOBUFS         105
#define ENODATA         61
#define ENODEV          19
#define ENOENT          2
#define ENOEXEC         8
#define ENOLCK          37
#define ENOLINK         67
#define ENOMEM          12
#define ENOMSG          42
#define ENOPROTOOPT     92
#define ENOSPC          28
#define ENOSR           63
#define ENOSTR          60
#define ENOSYS          38
#define ENOTCONN        107
#define ENOTDIR         20
#define ENOTEMPTY       39
#define ENOTRECOVERABLE 131
#define ENOTSOCK        88
#define ENOTSUP         95
#define ENOTTY          25
#define ENXIO           6
#define EOPNOTSUPP      95
#define EOVERFLOW       75
#define EOWNERDEAD      130
#define EPERM           1
#define EPIPE           32
#define EPROTO          71
#define EPROTONOSUPPORT 93
#define EPROTOTYPE      91
#define EROFS           30
#define ESPIPE          29
#define ESRCH           3
#define ETIME           62
#define ETIMEDOUT       110
#define ETXTBSY         26
#define EWOULDBLOCK     11
#define EXDEV           18
#endif


#ifdef __cplusplus
}  // extern "C"
#endif
#endif
