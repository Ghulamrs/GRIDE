// sys/stat.h - cppparse's declarations of the MSVC stat functions (the 64-bit time, 32-bit size variant
// that the UCRT's "stat" names stand for).
#ifndef CPPPARSE_SYS_STAT_H
#define CPPPARSE_SYS_STAT_H

#ifndef _WIN32
#error "sys/stat.h declares the MSVC stat functions: only for a 64-bit Windows target"
#endif

#include <sys/types.h>

struct _stat64i32 {
    _dev_t         st_dev;
    _ino_t         st_ino;
    unsigned short st_mode;
    short          st_nlink;
    short          st_uid;
    short          st_gid;
    _dev_t         st_rdev;
    _off_t         st_size;
    time_t         st_atime;
    time_t         st_mtime;
    time_t         st_ctime;
};
struct stat : public _stat64i32 {};
struct _stat : public _stat64i32 {};

extern "C" {
    int _stat64i32(const char* path, struct _stat64i32* out);
    int _fstat64i32(int fd, struct _stat64i32* out);
}

inline int stat(const char* path, struct stat* out) { return _stat64i32(path, out); }
inline int _stat(const char* path, struct _stat* out) { return _stat64i32(path, out); }

#define _S_IFMT  0xF000
#define _S_IFDIR 0x4000
#define _S_IFREG 0x8000
#define S_IFMT   _S_IFMT
#define S_IFDIR  _S_IFDIR
#define S_IFREG  _S_IFREG

#endif
