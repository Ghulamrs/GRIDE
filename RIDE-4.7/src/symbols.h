#ifndef EDITOR_SYMBOLS_H
#define EDITOR_SYMBOLS_H

#include <cstddef>
#include <string>
#include <vector>

namespace editor {

const unsigned char SymbolFunction = 0;
const unsigned char SymbolExported = 1;
const unsigned char SymbolExternal = 2;
const unsigned char SymbolText     = 3;

struct Symbol {
    unsigned char kind = SymbolFunction;
    std::string name;
    std::string detail;
    size_t line = 0;
};

std::vector<Symbol> symbolsIn(const std::vector<std::string>& assembly);

typedef std::string (*Demangler)(const std::string& decorated);
void setDemangler(Demangler how);

void installPlatformDemangler();

std::vector<std::string> describe(const std::vector<Symbol>& symbols);

}

#endif
