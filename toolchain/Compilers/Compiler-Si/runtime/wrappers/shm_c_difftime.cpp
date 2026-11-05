// shm_c_difftime.cpp - difftime for Shalimar (docs\Runtime_Design.md 14.3), written by Toolchain.targets'
// ShalimarLibrary from toolchain\Windows\RT\catalogue.json in shc's build: change the catalogue, not this file.
#include "../Borrow.h"

#include <time.h>

extern "C" double shm_c_difftime(int32_t p0, int32_t p1) {
    const double r = difftime(static_cast<time_t>(p0), static_cast<time_t>(p1));
    return r;
}
