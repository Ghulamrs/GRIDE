// shm_c_llrint.cpp - llrint for Shalimar (docs\Runtime_Design.md 14.3), written by Toolchain.targets'
// ShalimarLibrary from toolchain\Windows\RT\catalogue.json in shc's build: change the catalogue, not this file.
#include "../Borrow.h"

#include <math.h>

extern "C" int32_t shm_c_llrint(double p0) {
    const long long r = static_cast<long long>(llrint(p0));
    return shm::toInt("llrint", r);
}
