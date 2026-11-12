// fcntl.h - the file-control constants, as much of them as a program here has
// asked for.
//
// Only the two translation modes, because only they are reachable: `_setmode`
// in <io.h> is the one function that takes one. The open flags a POSIX
// <fcntl.h> also carries are not here, since `open` is not declared anywhere
// in this library - <stdio.h> is how a program here opens a file.
//
// The values are Microsoft's own and are what libcmt reads. They are numbers
// rather than an enum so that `#if` can see them, which is what a header of
// constants is for.
#ifndef _CXX1_FCNTL_H
#define _CXX1_FCNTL_H

#define _O_TEXT   0x4000
#define _O_BINARY 0x8000

// _open's flags (docs\Runtime_Design.md 9.4, 2026-11-10), Microsoft's values on both targets: the C6747's runtime
// gives the host its own. _O_TEMPORARY removes the file when it is closed, on Windows.
#define _O_RDONLY    0x0000
#define _O_WRONLY    0x0001
#define _O_RDWR      0x0002
#define _O_APPEND    0x0008
#define _O_RANDOM    0x0010
#define _O_SEQUENTIAL 0x0020
#define _O_TEMPORARY 0x0040
#define _O_NOINHERIT 0x0080
#define _O_CREAT     0x0100
#define _O_TRUNC     0x0200
#define _O_EXCL      0x0400
#define _O_RAW       _O_BINARY
#if defined(__cplusplus) || defined(_POSIX_C_SOURCE) || defined(_POSIX_SOURCE)
#define O_RDONLY _O_RDONLY
#define O_WRONLY _O_WRONLY
#define O_RDWR   _O_RDWR
#define O_APPEND _O_APPEND
#define O_CREAT  _O_CREAT
#define O_TRUNC  _O_TRUNC
#define O_EXCL   _O_EXCL
#define O_TEXT   _O_TEXT
#define O_BINARY _O_BINARY
#define O_RAW    _O_BINARY
#endif

#endif
