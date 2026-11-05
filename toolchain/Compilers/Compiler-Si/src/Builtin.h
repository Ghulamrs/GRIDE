
#pragma once

#include "Type.h"

#include <string>

namespace shalimar {

struct Builtin {

    enum class Shape {
        Real,
        IntOrReal,
        Length,
        // Called as a `uses` declaration is: its outputs and inputs are the row's, its symbol takes them as they are.
        Typed
    };

    const char *name;
    int arity;
    Shape shape;
    const char *realSymbol;
    const char *intSymbol;
    // The C6747's name for realSymbol, where its runtime spells it otherwise (TI's __c6xabi_trunc); null where the
    // C6747's runtime has none.
    const char *c6xSymbol;
    // A Typed row's outputs and inputs, a letter each: r real, i int, t text.
    const char *outputs;
    const char *inputs;
};

struct Prototype;

int findBuiltin(const std::string &name);
// The same among the runtime's functions alone, Shalimar's own left out: what `c.name` reaches.
int findLibrary(const std::string &name);
// What to say of a name that cannot be borrowed.
std::string notBorrowable(const std::string &name);
const Builtin &builtin(int index);
int builtinCount();
// A Typed row as the prototype the checker and the code generator call it through.
const Prototype &libraryPrototype(int index);
// The C6747's spelling of a library function a built-in calls, or null when it is the C name itself.
const char *c6747Symbol(const std::string &cName);

bool isConstant(const std::string &name);
double constantValue(const std::string &name);

}
