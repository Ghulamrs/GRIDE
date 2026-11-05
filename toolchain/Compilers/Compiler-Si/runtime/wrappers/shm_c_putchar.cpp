// shm_c_putchar.cpp - putchar for Shalimar (docs\Runtime_Design.md 14.3), written by Toolchain.targets'
// ShalimarLibrary from toolchain\Windows\RT\catalogue.json in shc's build: change the catalogue, not this file.
#include "../Borrow.h"

#include <errno.h>
#include <stdio.h>

extern "C" int32_t shm_c_putchar(int32_t p0) {
    errno = 0;
    const int r = putchar(p0);
    if (r < 0) shm::failed("putchar");
    return r;
}
