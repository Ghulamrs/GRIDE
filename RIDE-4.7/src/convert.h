#ifndef EDITOR_CONVERT_H
#define EDITOR_CONVERT_H

#include <string>

#include "compile.h"
#include "syntax.h"

namespace editor {

bool convertsFrom(Language lang, bool* toShalimar);

struct Conversion {
    bool ran = false;
    bool ok = false;
    int status = 0;
    std::string produced;
    std::string output;
};

std::string findConverter();

std::string convertedName(const std::string& sourcePath, bool toShalimar);

Conversion convert(const std::string& converter, const std::string& sourcePath,
                   const std::string& outputPath, bool toShalimar,
                   LineSink sink = 0, void* context = 0);

}

#endif
