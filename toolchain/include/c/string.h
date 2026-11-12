// string.h - the char * functions, and the memory ones beside them.
//
// The same bargain as <stdio.h>: prototypes only, checked against glibc by the
// suite building every case twice. See that file for why this is not a copy of
// the real header.
//
// const on a parameter is written as C writes it, and is worth one honest note.
// In this compiler const is a property of the declared object rather than part
// of its type, so "const char *" makes neither the pointer nor what it points
// at read-only here. The qualifier is accepted and carried; it simply does not
// yet reach through a pointer. Writing these prototypes any other way would
// misdescribe the functions to a reader for the sake of matching what the type
// model currently checks.
#ifndef _CC1_STRING_H
#define _CC1_STRING_H

// Everything here is the C library, so it is named the way C names it.
// Without this the compiler would give every declaration below a C++
// linkage name and ask the linker for a symbol libc has never had.
#ifdef __cplusplus
extern "C" {
#endif


#include <stddef.h>

size_t strlen(const char *);

char *strcpy(char *, const char *);
char *strncpy(char *, const char *, size_t);
char *strcat(char *, const char *);
char *strncat(char *, const char *, size_t);

int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t);
// The locale's collating order, and the transformation that makes strcmp follow it.
int strcoll(const char *, const char *);
size_t strxfrm(char *, const char *, size_t);

void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);
// memmove is memcpy for the case memcpy refuses to define: a source and a
// destination that overlap. Sliding a run of bytes along inside one buffer is
// the ordinary reason to want it, and doing that with memcpy is undefined even
// where it happens to work.
void *memmove(void *, const void *, size_t);
int memcmp(const void *, const void *, size_t);
void *memchr(const void *, int, size_t);

// Searching. The three that return a pointer into their argument return null
// when they find nothing, which is why NULL is in <stddef.h> and reachable from
// here.
char *strchr(const char *, int);
char *strrchr(const char *, int);
char *strstr(const char *, const char *);
char *strpbrk(const char *, const char *);
size_t strspn(const char *, const char *);
size_t strcspn(const char *, const char *);

// strtok keeps state between calls, which makes it the one function here that
// is not reentrant. It is in C90 and so it is declared; a program that wants
// two scans at once needs something else.
char *strtok(char *, const char *);

char *strerror(int);

// **Microsoft's string names** (docs\Runtime_Design.md 9.4, the policy of 2026-11-10): both runtimes define them, as
// the UCRT does. _strdup's copy is malloc's to free; _stricmp and _strnicmp compare as the "C" locale lowers ASCII;
// _strlwr and _strrev work in place and answer their argument. Their old spellings, behind POSIX's guard as fileno's
// is, are grtcompat's on Windows (oldnames.lib's on Microsoft's runtime) and rts6x.lib's on the C6747.
char *_strdup(const char *);
int _stricmp(const char *, const char *);
int _strnicmp(const char *, const char *, size_t);
char *_strlwr(char *);
char *_strrev(char *);
#if defined(__cplusplus) || defined(_POSIX_C_SOURCE) || defined(_POSIX_SOURCE)
char *strdup(const char *);
int stricmp(const char *, const char *);
int strnicmp(const char *, const char *, size_t);
char *strlwr(char *);
char *strrev(char *);
#endif


#ifdef __cplusplus
}  // extern "C"
#endif
#endif
