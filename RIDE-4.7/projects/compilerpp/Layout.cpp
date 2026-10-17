// Layout.cpp
//
// C++98 only.  See Layout.h for the three rules single inheritance buys.

#include "Layout.h"

#include <cstddef>
#include <iostream>
#include <sstream>

Layout::Layout(Diagnostics &d)
    : diag(d), reportedOversize(false), memoryLimit(MachineMemory), classIndex(0) {}

int Layout::roundUp(int value, int alignment) {
    if (alignment <= 1) return value;
    const int rem = value % alignment;
    return rem ? value + (alignment - rem) : value;
}

const ClassLayout *Layout::forClass(const std::string &name) const {
    std::map<std::string, ClassLayout>::const_iterator it = layouts.find(name);
    return (it == layouts.end()) ? 0 : &it->second;
}

int Layout::sizeOf(cc::Type *t) const {
    if (!t) return 0;
    // A reference is a pointer at runtime; that is the whole of its lowering.
    cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(t);
    if (rt) return PointerSize;
    if (dynamic_cast<cc::PointerType*>(t)) return PointerSize;
    // An array is its elements, laid end to end -- computed in the machine's
    // own word, not in int.  `int a[700000000];` overflowed this multiply,
    // and a wrapped size is not a diagnostic: the array was laid out at some
    // wrong size, the program compiled, it RAN, and it reported success. An
    // accepted program that means nothing is the worst answer available.
    //
    // The size is also what the whole object will occupy, so this is where it
    // can be compared against the memory the machine actually has, while there
    // is still a declaration to point at.  The VM checks the same thing when
    // it loads an image, but by then CodeGen has already materialised the
    // bytes in the host: `int a[300000000];` reached a gigabyte of host memory
    // before anything objected, which on a phone is not an error message.
    if (cc::ArrayType *at = dynamic_cast<cc::ArrayType*>(t)) {
        const vmword elem  = sizeOf(at->element);
        const vmword count = static_cast<vmword>(at->count);
        const vmword bytes = count * elem;
        if (elem > 0 && (count > memoryLimit / elem || bytes > memoryLimit)) {
            if (!reportedOversize) {
                reportedOversize = true;
                std::ostringstream ss;
                ss << "an array of " << count << " x " << elem
                   << " bytes does not fit in this machine's " << (memoryLimit / 1024 / 1024)
                   << "MB of memory";
                diag.error(at->line, at->col, ss.str());
            }
            return 0;
        }
        return static_cast<int>(bytes);
    }
    // bool is the C++ layer's, so the C layer's size table does not name it.
    if (dynamic_cast<cxx::BoolType*>(t)) return 1;
    // The type model owns every builtin's size; Layout does not restate them.
    cc::BuiltinType *bt = dynamic_cast<cc::BuiltinType*>(t);
    if (bt) return cc::builtinSize(bt->kind);
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(t);
    if (ct) {
        const ClassLayout *cl = forClass(ct->className);
        return cl ? cl->size : 0;
    }
    return 0;
}

int Layout::alignOf(cc::Type *t) const {
    // An array aligns like one element, however many it holds.
    if (cc::ArrayType *at = dynamic_cast<cc::ArrayType*>(t)) return alignOf(at->element);
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(t);
    if (ct) {
        const ClassLayout *cl = forClass(ct->className);
        return cl ? cl->align : 1;
    }
    const int s = sizeOf(t);
    return s > 0 ? s : 1;                       // scalars align to their size
}

void Layout::computeAll(const std::map<std::string, cxx::ClassDecl*> &classes) {
    classIndex = &classes;
    std::map<std::string, cxx::ClassDecl*>::const_iterator it;
    for (it = classes.begin(); it != classes.end(); ++it) {
        computeFor(it->second);
    }
    classIndex = 0;
}

// A field's class, if it has one.  An array of objects counts: its element
// needs a size before the array does.
cxx::ClassDecl *Layout::classDeclOf(cc::Type *t) const {
    while (cc::ArrayType *at = dynamic_cast<cc::ArrayType*>(t)) t = at->element;
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(t);
    if (!ct || !classIndex) return 0;
    std::map<std::string, cxx::ClassDecl*>::const_iterator it = classIndex->find(ct->className);
    return (it == classIndex->end()) ? 0 : it->second;
}

void Layout::computeFor(cxx::ClassDecl *cd) {
    if (!cd) return;
    if (layouts.find(cd->name) != layouts.end()) return;    // already done

    // A class cannot contain itself, directly or through a chain of members --
    // that has no finite size.  Caught here because this is where the chain is
    // walked.
    if (inProgress.find(cd->name) != inProgress.end()) {
        diag.error(cd->line, cd->col,
                   "class '" + cd->name + "' cannot contain itself");
        return;
    }
    inProgress.insert(cd->name);

    // A base must be laid out first: the derived layout starts as a copy of it.
    // The semantic pass has already broken any cycle, so this recursion ends.
    if (cd->base) computeFor(cd->base);

    // So must any class a field is made of -- otherwise its size depends on
    // whether its name happened to sort before this one.
    for (std::size_t i = 0; i < cd->members.size(); ++i) {
        cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[i]);
        if (fd) computeFor(classDeclOf(fd->type));
    }
    const ClassLayout *baseLayout = cd->base ? forClass(cd->base->name) : 0;

    ClassLayout cl;
    cl.name = cd->name;

    // Needed if this class declares a virtual, or its base already had one.
    bool declaresVirtual = false;
    for (std::size_t i = 0; i < cd->members.size(); ++i) {
        cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(cd->members[i]);
        if (md && md->isVirtual) { declaresVirtual = true; break; }
    }
    cl.hasVPtr = declaresVirtual || (baseLayout && baseLayout->hasVPtr);

    int offset = 0;
    cl.align = 1;

    if (baseLayout) {
        // The base subobject sits at offset 0, so its fields keep their offsets.
        cl.fields = baseLayout->fields;
        offset = baseLayout->size;
        cl.align = baseLayout->align;
        // That would need the vptr in front and the base pushed down.
        // Rejecting it keeps every upcast a no-op.
        if (cl.hasVPtr && !baseLayout->hasVPtr) {
            diag.error(cd->line, cd->col,
                       "class '" + cd->name + "' adds a virtual function but base '"
                       + cd->base->name + "' has none");
            cl.hasVPtr = false;
        }
    } else if (cl.hasVPtr) {
        offset = PointerSize;                   // the vptr occupies offset 0
        cl.align = PointerSize;
    }

    cl.firstOwnField = static_cast<int>(cl.fields.size());

    for (std::size_t i = 0; i < cd->members.size(); ++i) {
        cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[i]);
        if (!fd) continue;
        const int fsize = sizeOf(fd->type);
        const int falign = alignOf(fd->type);
        if (fsize == 0) {
            // Unless sizeOf has already said why, in which case this is the
            // same mistake a second time.  One mistake costs one line.
            if (!reportedOversize) {
                diag.error(fd->line, fd->col,
                           "field '" + fd->name + "' has no size in class '" + cd->name + "'");
            }
            continue;
        }
        offset = roundUp(offset, falign);
        FieldLayout f;
        f.name = fd->name;
        f.ownerClass = cd->name;
        f.type = fd->type;
        f.offset = offset;
        f.size = fsize;
        cl.fields.push_back(f);
        offset += fsize;
        if (falign > cl.align) cl.align = falign;
    }

    // Rounded up so an array of them stays aligned.
    cl.size = roundUp(offset, cl.align);
    if (cl.size == 0) cl.size = 1;              // an empty class still occupies a byte

    // --- construction and destruction order -------------------------------
    // Base first, then this class's own fields in DECLARATION order -- not the
    // order an initialiser list is written in, hence that warning -- then the
    // constructor body, which can now rely on everything being in place.
    cl.hasCtor = !cd->ctors.empty();
    if (cd->base) cl.constructionPlan.push_back(InitStep(InitStep::StepBase, cd->base->name));
    // AFTER the base and BEFORE this class's fields: that is what makes a
    // virtual call inside a constructor reach THIS class's override.
    if (cl.hasVPtr) cl.constructionPlan.push_back(InitStep(InitStep::StepVPtr, cd->name));
    for (std::size_t i = 0; i < cd->members.size(); ++i) {
        cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[i]);
        if (fd) cl.constructionPlan.push_back(InitStep(InitStep::StepField, fd->name));
    }
    // Without a constructor there is nothing to run after the fields.
    if (cl.hasCtor || cd->dtor) cl.constructionPlan.push_back(InitStep(InitStep::StepBody, cd->name));

    // Exact reverse: body first (it may still need the members), then members
    // backwards, then the base -- which outlives the derived part.
    for (std::size_t i = cl.constructionPlan.size(); i > 0; --i) {
        cl.destructionPlan.push_back(cl.constructionPlan[i - 1]);
    }

    cl.hasDtor = (cd->dtor != 0) || (baseLayout && baseLayout->hasDtor);

    // --- the vtable -------------------------------------------------------
    // Start from the base's, so a slot means the same thing down the chain.
    // An override REPLACES its slot; a new virtual APPENDS one.
    if (baseLayout) cl.vtable = baseLayout->vtable;
    if (cl.hasVPtr) {
        for (std::size_t i = 0; i < cd->members.size(); ++i) {
            cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(cd->members[i]);
            if (!md || !md->isVirtual) continue;
            bool replaced = false;
            if (md->overrides) {
                for (std::size_t s = 0; s < cl.vtable.size(); ++s) {
                    if (cl.vtable[s] == md->overrides) { cl.vtable[s] = md; replaced = true; break; }
                }
            }
            if (!replaced) cl.vtable.push_back(md);
        }
    }

    layouts[cd->name] = cl;
    inProgress.erase(cd->name);
}

void Layout::print() const {
    std::map<std::string, ClassLayout>::const_iterator it;
    for (it = layouts.begin(); it != layouts.end(); ++it) {
        const ClassLayout &cl = it->second;
        std::cout << "class " << cl.name
                  << "  size=" << cl.size
                  << " align=" << cl.align
                  << (cl.hasVPtr ? "  [has vptr]" : "")
                  << std::endl;
        if (cl.hasVPtr) {
            std::cout << "     +0  __vptr (" << PointerSize << " bytes)" << std::endl;
        }
        for (std::size_t i = 0; i < cl.fields.size(); ++i) {
            const FieldLayout &f = cl.fields[i];
            std::cout << "    +" << f.offset << "  " << f.name
                      << " (" << f.size << " bytes)";
            if (f.ownerClass != cl.name) std::cout << "  inherited from " << f.ownerClass;
            std::cout << std::endl;
        }
        for (std::size_t s = 0; s < cl.vtable.size(); ++s) {
            cxx::MethodDecl *m = cl.vtable[s];
            std::cout << "    vtable[" << s << "] = " << m->ownerClass << "::" << m->name << std::endl;
        }
        // Only worth showing when the order can matter.
        if (cl.hasCtor || cl.hasDtor || cl.hasVPtr) {
            std::cout << "    construct:";
            for (std::size_t i = 0; i < cl.constructionPlan.size(); ++i) {
                const InitStep &st = cl.constructionPlan[i];
                std::cout << " " << (i ? "-> " : "");
                switch (st.kind) {
                case InitStep::StepBase:  std::cout << "base " << st.name; break;
                case InitStep::StepVPtr:  std::cout << "set vptr"; break;
                case InitStep::StepField: std::cout << "field " << st.name; break;
                case InitStep::StepBody:  std::cout << st.name << "() body"; break;
                }
            }
            std::cout << std::endl;
            std::cout << "    destroy:  ";
            for (std::size_t i = 0; i < cl.destructionPlan.size(); ++i) {
                const InitStep &st = cl.destructionPlan[i];
                std::cout << " " << (i ? "-> " : "");
                switch (st.kind) {
                case InitStep::StepBase:  std::cout << "base " << st.name; break;
                case InitStep::StepVPtr:  std::cout << "reset vptr"; break;
                case InitStep::StepField: std::cout << "field " << st.name; break;
                case InitStep::StepBody:  std::cout << "~" << st.name << "() body"; break;
                }
            }
            std::cout << std::endl;
        }
        std::cout << std::endl;
    }
}
