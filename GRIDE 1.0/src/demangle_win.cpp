
#include "symbols.h"

#ifdef _WIN32

#include <windows.h>

#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

namespace editor {

namespace {

std::string readable(const std::string& decorated) {
    char plain[1024];

    DWORD flags = UNDNAME_NO_MS_KEYWORDS | UNDNAME_NO_FUNCTION_RETURNS |
                  UNDNAME_NO_ALLOCATION_MODEL | UNDNAME_NO_ALLOCATION_LANGUAGE |
                  UNDNAME_NO_ACCESS_SPECIFIERS | UNDNAME_NO_MEMBER_TYPE;

    DWORD got = UnDecorateSymbolName(decorated.c_str(), plain,
                                     static_cast<DWORD>(sizeof plain), flags);
    if (got == 0) return decorated;
    return std::string(plain, got);
}

}

void installPlatformDemangler() { setDemangler(readable); }

}

#else

namespace editor {

void installPlatformDemangler() {}
}

#endif
