// shm_c_atoll.cpp - atoll for Shalimar (docs\Runtime_Design.md 14.3), written by Toolchain.targets'
// ShalimarLibrary from toolchain\Windows\RT\catalogue.json in shc's build: change the catalogue, not this file.
#include "../Borrow.h"

#include <stdlib.h>

extern "C" int32_t shm_c_atoll(const ShmArray *p0) {
    const shm::CText a0(p0);
    const long long r = static_cast<long long>(atoll(a0));
    return shm::toInt("atoll", r);
}
