// direct.h - cppparse's declarations of the MSVC directory functions its own sources use.
#ifndef CPPPARSE_DIRECT_H
#define CPPPARSE_DIRECT_H

#ifndef _WIN32
#error "direct.h declares MSVC runtime functions: only for a 64-bit Windows target"
#endif

extern "C" {
    char* _getcwd(char* buffer, int size);
    int   _mkdir(const char* path);
    int   _rmdir(const char* path);
    int   _chdir(const char* path);
}

#endif
