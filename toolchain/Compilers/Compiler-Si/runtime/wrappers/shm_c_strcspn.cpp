// shm_c_strcspn.cpp - strcspn for Shalimar (docs\Runtime_Design.md 14.3), written by Toolchain.targets'
// ShalimarLibrary from toolchain\Windows\RT\catalogue.json in shc's build: change the catalogue, not this file.
#include "../Borrow.h"

#include <string.h>

extern "C" int32_t shm_c_strcspn(const ShmArray *p0, const ShmArray *p1) {
    const shm::CText a0(p0);
    const shm::CText a1(p1);
    const long long r = static_cast<long long>(strcspn(a0, a1));
    return shm::toInt("strcspn", r);
}
