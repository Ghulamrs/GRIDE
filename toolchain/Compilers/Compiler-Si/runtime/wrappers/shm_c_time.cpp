// shm_c_time.cpp - time for Shalimar (docs\Runtime_Design.md 14.3), written by Toolchain.targets'
// ShalimarLibrary from toolchain\Windows\RT\catalogue.json in shc's build: change the catalogue, not this file.
#include "../Borrow.h"

#include <errno.h>
#include <time.h>

extern "C" int32_t shm_c_time() {
    errno = 0;
    const long long r = static_cast<long long>(time(0));
    if (r < 0) shm::failed("time");
    return shm::toInt("time", r);
}
