// AST1.cpp
//
// C++98 only.  See AST1.h for the inheritance layout.

#include "AST1.h"

namespace cxx {

const char *accessText(Access a) {
    switch (a) {
    case ACC_Public:    return "public";
    case ACC_Private:   return "private";
    case ACC_Protected: return "protected";
    }
    return "?";
}

// --- Types added by C++ ---

void ReferenceType::print(int indent) {
    printIndent(indent);
    std::cout << "reference to ";
    base->print(0);
}

void BoolType::print(int indent) {
    printIndent(indent);
    std::cout << "bool" << std::endl;
}

void ClassType::print(int indent) {
    printIndent(indent);
    std::cout << "class " << className << std::endl;
}

// --- Declarations ---

void FieldDecl::print(int indent) {
    printIndent(indent);
    std::cout << "Field " << accessText(access) << " " << name << " : ";
    if (type) type->print(0);
    else std::cout << "<none>" << std::endl;
}

MethodDecl::~MethodDecl() {
    for (std::size_t i = 0; i < memberInits.size(); ++i) {
        for (std::size_t j = 0; j < memberInits[i].args.size(); ++j) {
            delete memberInits[i].args[j];
        }
    }
}

// Everything but this first line comes from cc::Function::print().
void MethodDecl::printSignature(int indent) {
    printIndent(indent);
    const char *what = isConstructor ? "Constructor" : (isDestructor ? "Destructor" : "Method");
    std::cout << what << " " << accessText(access) << " ";
    if (isVirtual) std::cout << "virtual ";
    if (overrides) std::cout << "overriding ";
    if (!ownerClass.empty()) std::cout << ownerClass << "::";
    std::cout << name;
    if (isConstructor || isDestructor) {
        std::cout << std::endl;
    } else {
        std::cout << " returns ";
        if (retType) retType->print(0);
        else std::cout << "<none>" << std::endl;
    }
}

void MethodDecl::printBodyPrefix(int indent) {
    for (std::size_t i = 0; i < memberInits.size(); ++i) {
        printIndent(indent);
        std::cout << (memberInits[i].isBase ? "init base " : "init member ")
                  << memberInits[i].name << std::endl;
        for (std::size_t j = 0; j < memberInits[i].args.size(); ++j) {
            memberInits[i].args[j]->print(indent + 1);
        }
    }
}

ClassDecl::~ClassDecl() {
    for (std::size_t i = 0; i < members.size(); ++i) delete members[i];
    // Only the prototypes with no definition elsewhere belong to this class.
    for (std::size_t i = 0; i < friendProtos.size(); ++i) delete friendProtos[i];
}

void ClassDecl::print(int indent) {
    printIndent(indent);
    std::cout << "Class " << name;
    if (!baseName.empty()) std::cout << " : " << accessText(baseAccess) << " " << baseName;
    std::cout << std::endl;
    for (std::size_t i = 0; i < members.size(); ++i) members[i]->print(indent + 1);
}

// --- Qualified name ---

void QualifiedName::print(int indent) {
    printIndent(indent);
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) std::cout << "::";
        std::cout << parts[i];
    }
    std::cout << std::endl;
}

// --- Expressions added by C++ ---

void MemberAccessExpr::print(int indent) {
    printIndent(indent);
    std::cout << "Member " << (isArrow ? "->" : ".") << member << std::endl;
    if (base) base->print(indent + 1);
}

void ThisExpr::print(int indent) {
    printIndent(indent);
    std::cout << "this" << std::endl;
}

void BoolExpr::print(int indent) {
    printIndent(indent);
    std::cout << (value ? "true" : "false") << std::endl;
}

TempExpr::~TempExpr() {
    delete type;
    for (std::size_t i = 0; i < args.size(); ++i) delete args[i];
}

void TempExpr::print(int indent) {
    printIndent(indent);
    std::cout << "Temporary ";
    if (type) type->print(0);
    else std::cout << "<none>" << std::endl;
    for (std::size_t i = 0; i < args.size(); ++i) args[i]->print(indent + 1);
}

NewExpr::~NewExpr() {
    delete allocType;
    delete count;
    for (std::size_t i = 0; i < args.size(); ++i) delete args[i];
}

void NewExpr::print(int indent) {
    printIndent(indent);
    std::cout << (count ? "New[] " : "New ");
    if (allocType) allocType->print(0);
    else std::cout << "<none>" << std::endl;
    if (count) count->print(indent + 1);
    for (std::size_t i = 0; i < args.size(); ++i) args[i]->print(indent + 1);
}

void DeleteExpr::print(int indent) {
    printIndent(indent);
    std::cout << (isArray ? "Delete[]" : "Delete") << std::endl;
    if (operand) operand->print(indent + 1);
}

} // namespace cxx
