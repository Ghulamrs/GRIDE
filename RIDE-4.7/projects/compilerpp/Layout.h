// Layout.h -- PASS 4, the object model.
//
// Where each field sits, how big an object is, and which function a virtual
// call reaches.  Single inheritance makes the rules fit in three sentences:
//
//   * A derived object begins with its base subobject, so a Derived* and its
//     Base* are the same address and an upcast costs nothing.
//   * If a class or any base has a virtual function, the object starts with a
//     pointer to its vtable; a base that already has one shares it.
//   * A vtable is its base's, copied, with overridden slots replaced and new
//     virtuals appended -- so a slot index means the same thing all the way
//     down the chain.
//
// Multiple inheritance would break all three at once.
//
// C++98 only.

#ifndef LAYOUT_H
#define LAYOUT_H

#include <map>
#include <set>
#include <string>
#include <vector>

#include "AST1.h"
#include "Bytecode.h"
#include "Diagnostics.h"

struct FieldLayout {
    std::string name;
    std::string ownerClass;     // the class that declared it
    cc::Type *type;             // not owned
    int offset;
    int size;
};

// One step of building or taking apart an object.  The plan is computed per
// class and is the same for every constructor of it, apart from which body
// runs -- because the ORDER is fixed by the class, never by the constructor.
struct InitStep {
    enum Kind { StepBase, StepVPtr, StepField, StepBody };
    Kind kind;
    std::string name;
    InitStep(Kind k, const std::string &n) : kind(k), name(n) {}
};

struct ClassLayout {
    std::string name;
    int size;                   // bytes, including the vptr and any padding
    int align;
    bool hasVPtr;
    int firstOwnField;          // index into `fields` where this class's own start
    std::vector<FieldLayout> fields;             // base fields first
    std::vector<cxx::MethodDecl*> vtable;        // slot -> final override
    // Base first, then this class's own fields in DECLARATION order, then the
    // constructor body.  Destruction is this list reversed, exactly.
    std::vector<InitStep> constructionPlan;
    std::vector<InitStep> destructionPlan;
    bool hasDtor;                                // this class or any base
    bool hasCtor;                                // this class declares one
    ClassLayout()
        : size(0), align(1), hasVPtr(false), firstOwnField(0),
          hasDtor(false), hasCtor(false) {}
};

class Layout {
public:
    explicit Layout(Diagnostics &d);

    // The machine an object has to fit in.  Defaults to MachineMemory, which
    // is what the VM defaults to; a host that shrinks one must shrink both, or
    // the front end accepts an array the machine will refuse to load.
    void setMemoryLimit(vmword bytes) { memoryLimit = bytes; }

    // Computes a layout for every class, base classes first.
    void computeAll(const std::map<std::string, cxx::ClassDecl*> &classes);

    const ClassLayout *forClass(const std::string &name) const;

    // Byte size of any type; 0 and a diagnostic for something with no size,
    // or for an object too large for the machine to hold.
    int sizeOf(cc::Type *t) const;
    int alignOf(cc::Type *t) const;

    void print() const;

    // Builtin sizes live in the type model (builtinSize in AST.h); only the
    // pointer size is Layout's own, because no builtin type describes one.
    static const int PointerSize = 8;
    static const int IntSize = 4;

private:
    Diagnostics &diag;
    // sizeOf is const and is called for every type in the program, so an
    // oversized array would otherwise be reported once per mention.  One
    // mistake costs one line.
    mutable bool reportedOversize;
    vmword memoryLimit;
    std::map<std::string, ClassLayout> layouts;

    // Needed during the recursion: a class-typed FIELD has to be laid out
    // before its container, and the map is the only way from a name to a
    // declaration.  Without it the answer depended on the alphabetical order
    // computeAll happened to walk.
    const std::map<std::string, cxx::ClassDecl*> *classIndex;
    std::set<std::string> inProgress;           // catches A-contains-B-contains-A

    // Depth-first, so a base -- and any class-typed member -- is always laid
    // out before the class that needs its size.
    void computeFor(cxx::ClassDecl *cd);
    // The class a field's type names, or 0 when the field is not a class.
    cxx::ClassDecl *classDeclOf(cc::Type *t) const;
    static int roundUp(int value, int alignment);

    Layout(const Layout &);
    Layout &operator=(const Layout &);
};

#endif
