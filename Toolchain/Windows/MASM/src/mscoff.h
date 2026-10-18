#ifndef MSCOFF_H
#define MSCOFF_H

#include "asm.h"

class CoffWriter : public ObjWriter {
public:
    bool write(const Unit &u, const std::string &path, std::string &err);
};

#endif
