// shm_c_clock.cpp - clock for Shalimar (docs\Runtime_Design.md 14.3), written by Toolchain.targets'
// ShalimarLibrary from toolchain\Windows\RT\catalogue.json in shc's build: change the catalogue, not this file.
#include "../Borrow.h"

#include <errno.h>
#include <time.h>

extern "C" int32_t shm_c_clock() {
    errno = 0;
    const long long r = static_cast<long long>(clock());
    if (r < 0) shm::failed("clock");
    return shm::toInt("clock", r);
}
