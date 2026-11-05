#include "Builtin.h"

#include "Ast.h"

#include <cmath>
#include <cstring>
#include <map>

namespace shalimar {
namespace {

// **The symbol each one calls.** The library's rows come from the runtime's catalogue (Library.inc, written by
// the build from toolchain\Windows\RT\catalogue.json, docs\Runtime_Design.md 14.3): a C library name - `sin`, `fabs` -
// shc calls straight, and shm_c_<name> where the signature is not Shalimar's (runtime\wrappers\, from the same
// catalogue). The rest are Shalimar's own and are NOT library calls - max/min propagate NaN, C has no integer max
// or min, abs's int answer traps on INT_MIN: docs/FOREIGN.md.
const Builtin table[] = {
#include "Library.inc"
    {"max",   2, Builtin::Shape::IntOrReal, "shm_fn_max_real", "shm_fn_max_int", nullptr, nullptr, nullptr},
    {"min",   2, Builtin::Shape::IntOrReal, "shm_fn_min_real", "shm_fn_min_int", nullptr, nullptr, nullptr},
    {"len",   1, Builtin::Shape::Length,    nullptr,           nullptr,          nullptr, nullptr, nullptr}
};

const int count = static_cast<int>(sizeof table / sizeof table[0]);
// The rows before Shalimar's own three.
const int libraryCount = count - 3;

}

// Names a person will reasonably try, and the reason each one cannot be borrowed: every entry
// is refused because its C signature needs a type Shalimar does not have, and saying which type
// is the difference between an answer and a refusal (docs/FOREIGN.md). Anything not on it still gets a plain "not a library function this compiler knows".
namespace {
struct Unborrowable { const char *name; const char *why; };
const Unborrowable kUnborrowable[] = {
    {"memset",  "takes a pointer, which Shalimar has no type for"},
    {"memcpy",  "takes a pointer, which Shalimar has no type for"},
    {"strcpy",  "writes through a pointer, which Shalimar has no type for"},
    {"strcat",  "writes through a pointer, which Shalimar has no type for"},
    {"strchr",  "returns a pointer into its argument, which Shalimar has no type for"},
    {"strstr",  "returns a pointer into its argument, which Shalimar has no type for"},
    {"malloc",  "returns a pointer, which Shalimar has no type for"},
    {"free",    "takes a pointer, which Shalimar has no type for"},
    {"fopen",   "returns a pointer, which Shalimar has no type for"},
    {"qsort",   "takes a function pointer, which Shalimar has no type for"},
    {"printf",  "takes a variable number of arguments, which Shalimar has no form for"},
    {"scanf",   "takes a variable number of arguments, which Shalimar has no form for"},
};
}

std::string notBorrowable(const std::string &name) {
    for (std::size_t i = 0; i < sizeof kUnborrowable / sizeof kUnborrowable[0]; ++i)
        if (name == kUnborrowable[i].name) return "'" + name + "' " + kUnborrowable[i].why;
    if (findBuiltin(name) >= 0) return "'" + name + "' is Shalimar's own, not the runtime's";
    return "'" + name + "' is not a library function this compiler knows";
}

int findBuiltin(const std::string &name) {
    for (int i = 0; i < count; ++i) {
        if (name == table[i].name) return i;
    }
    return -1;
}

int findLibrary(const std::string &name) {
    const int which = findBuiltin(name);
    return which < libraryCount ? which : -1;
}

const Builtin &builtin(int index) { return table[index]; }

namespace {
const Type *typeOfLetter(char letter) {
    if (letter == 'r') return Type::realType();
    if (letter == 'i') return Type::intType();
    return Type::arrayOf(Type::charType());
}
}

const Prototype &libraryPrototype(int index) {
    static std::map<int, Prototype> made;
    std::map<int, Prototype>::iterator found = made.find(index);
    if (found != made.end()) return found->second;

    const Builtin &fn = table[index];
    Prototype proto(fn.realSymbol, 0);
    proto.isForeign = true;
    for (const char *letter = fn.outputs; *letter; ++letter) proto.outputs.push_back(typeOfLetter(*letter));
    for (const char *letter = fn.inputs; *letter; ++letter) {
        Param parameter;
        parameter.type = typeOfLetter(*letter);
        proto.inputs.push_back(parameter);
    }
    return made[index] = proto;
}

const char *c6747Symbol(const std::string &cName) {
    for (int i = 0; i < count; ++i) {
        if (table[i].realSymbol && table[i].c6xSymbol && cName == table[i].realSymbol &&
            std::strcmp(table[i].realSymbol, table[i].c6xSymbol) != 0)
            return table[i].c6xSymbol;
    }
    return nullptr;
}
int builtinCount() { return count; }

bool isConstant(const std::string &name) { return name == "pi" || name == "e"; }

double constantValue(const std::string &name) {
    return name == "pi" ? 3.141592653589793 : 2.718281828459045;
}

}
