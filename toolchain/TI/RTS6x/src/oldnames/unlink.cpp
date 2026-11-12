// Spec: Microsoft C runtime's old name unlink for _unlink (oldnames.lib), here for the C6747.

#define _POSIX_SOURCE 1
#include <io.h>

extern "C" int unlink(const char *path)
{
    return _unlink(path);
}
