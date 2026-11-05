// shm_c_perror.cpp - perror for Shalimar (docs\Runtime_Design.md 14.3), written by Toolchain.targets'
// ShalimarLibrary from toolchain\Windows\RT\catalogue.json in shc's build: change the catalogue, not this file.
#include "../Borrow.h"

#include <stdio.h>

extern "C" void shm_c_perror(const ShmArray *p0) {
    const shm::CText a0(p0);
    perror(a0);
}
