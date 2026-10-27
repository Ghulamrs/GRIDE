// sys/types.h - cppparse's declarations of the MSVC system types.
#ifndef CPPPARSE_SYS_TYPES_H
#define CPPPARSE_SYS_TYPES_H

#ifndef _WIN32
#error "sys/types.h declares the MSVC system types: only for a 64-bit Windows target"
#endif

#include <cstddef>
#include <ctime>

typedef unsigned int   _dev_t;
typedef unsigned short _ino_t;
typedef unsigned short _mode_t;
typedef long           _off_t;
typedef int            _pid_t;
typedef _dev_t  dev_t;
typedef _ino_t  ino_t;
typedef _mode_t mode_t;
typedef _off_t  off_t;
typedef _pid_t  pid_t;

#endif
