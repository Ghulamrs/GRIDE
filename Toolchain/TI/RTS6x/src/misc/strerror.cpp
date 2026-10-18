// Spec: ISO C 7.21.6.2 - strerror: a message for an error number. The numbers are the ones the
// compilers' <errno.h> gives the C6000; any other answers "Unknown error".

#include <string.h>
#include <errno.h>

extern "C" char *strerror(int code)
{
    switch (code) {
    case 0: return (char *)"No error";
    case EPERM: return (char *)"Operation not permitted";
    case ENOENT: return (char *)"No such file or directory";
    case EINTR: return (char *)"Interrupted system call";
    case EIO: return (char *)"Input/output error";
    case EBADF: return (char *)"Bad file descriptor";
    case ENOMEM: return (char *)"Cannot allocate memory";
    case EACCES: return (char *)"Permission denied";
    case EEXIST: return (char *)"File exists";
    case EINVAL: return (char *)"Invalid argument";
    case ENOSPC: return (char *)"No space left on device";
    case EDOM: return (char *)"Numerical argument out of domain";
    case ERANGE: return (char *)"Numerical result out of range";
    case EILSEQ: return (char *)"Illegal byte sequence";
    default: return (char *)"Unknown error";
    }
}
