// shm_c_labs.cpp - labs for Shalimar (docs\Runtime_Design.md 14.3), written by Toolchain.targets'
// ShalimarLibrary from toolchain\Windows\RT\catalogue.json in shc's build: change the catalogue, not this file.
#include "../Borrow.h"

#include <stdlib.h>

extern "C" int32_t shm_c_labs(int32_t p0) {
    const long long r = static_cast<long long>(labs(static_cast<long>(p0)));
    return shm::toInt("labs", r);
}
