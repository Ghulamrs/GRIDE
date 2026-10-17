// AST.cpp
//
// C++98 only.

#include "AST.h"

namespace cc {

// --- operator spelling ---

const char *binaryOpText(BinaryOp op) {
    switch (op) {
    case BIN_Add:    return "+";
    case BIN_Sub:    return "-";
    case BIN_Mul:    return "*";
    case BIN_Div:    return "/";
    case BIN_Mod:    return "%";
    case BIN_Assign: return "=";
    case BIN_AddAssign: return "+=";
    case BIN_SubAssign: return "-=";
    case BIN_MulAssign: return "*=";
    case BIN_DivAssign: return "/=";
    case BIN_ModAssign: return "%=";
    case BIN_EQ:     return "==";
    case BIN_NE:     return "!=";
    case BIN_LT:     return "<";
    case BIN_GT:     return ">";
    case BIN_LE:     return "<=";
    case BIN_GE:     return ">=";
    case BIN_LAnd:   return "&&";
    case BIN_LOr:    return "||";
    case BIN_Shl:    return "<<";
    case BIN_Shr:    return ">>";
    }
    return "?";
}

bool binaryOpIsComparison(BinaryOp op) {
    return op == BIN_EQ || op == BIN_NE || op == BIN_LT
        || op == BIN_GT || op == BIN_LE || op == BIN_GE;
}

bool binaryOpIsLogical(BinaryOp op) {
    return op == BIN_LAnd || op == BIN_LOr;
}

bool binaryOpIsAssignment(BinaryOp op) {
    return op == BIN_Assign || op == BIN_AddAssign || op == BIN_SubAssign
        || op == BIN_MulAssign || op == BIN_DivAssign || op == BIN_ModAssign;
}

BinaryOp binaryOpUnderlying(BinaryOp op) {
    switch (op) {
    case BIN_AddAssign: return BIN_Add;
    case BIN_SubAssign: return BIN_Sub;
    case BIN_MulAssign: return BIN_Mul;
    case BIN_DivAssign: return BIN_Div;
    case BIN_ModAssign: return BIN_Mod;
    default:            return op;
    }
}

const char *unaryOpText(UnaryOp op) {
    switch (op) {
    case UN_Neg:    return "-";
    case UN_Not:    return "!";
    case UN_Deref:  return "*";
    case UN_AddrOf: return "&";
    case UN_PreInc:  return "++ (prefix)";
    case UN_PreDec:  return "-- (prefix)";
    case UN_PostInc: return "++ (postfix)";
    case UN_PostDec: return "-- (postfix)";
    }
    return "?";
}

bool unaryOpIsIncDec(UnaryOp op) {
    return op == UN_PreInc || op == UN_PreDec
        || op == UN_PostInc || op == UN_PostDec;
}

// --- builtin types ---

const char *builtinName(BuiltinKind k) {
    switch (k) {
    case BK_Void:   return "void";
    case BK_Char:   return "char";
    case BK_SChar:  return "signed char";
    case BK_UChar:  return "unsigned char";
    case BK_Short:  return "short";
    case BK_UShort: return "unsigned short";
    case BK_Int:    return "int";
    case BK_UInt:   return "unsigned int";
    case BK_Long:   return "long";
    case BK_ULong:  return "unsigned long";
    case BK_Float:  return "float";
    case BK_Double: return "double";
    }
    return "?";
}

int builtinSize(BuiltinKind k) {
    switch (k) {
    case BK_Void:   return 0;
    case BK_Char:
    case BK_SChar:
    case BK_UChar:  return 1;
    case BK_Short:
    case BK_UShort: return 2;
    case BK_Int:
    case BK_UInt:
    case BK_Float:  return 4;
    case BK_Long:
    case BK_ULong:
    case BK_Double: return 8;
    }
    return 0;
}

int builtinRank(BuiltinKind k) {
    switch (k) {
    case BK_Void:   return -1;
    case BK_Char:
    case BK_SChar:
    case BK_UChar:  return 0;
    case BK_Short:
    case BK_UShort: return 1;
    case BK_Int:
    case BK_UInt:   return 2;
    case BK_Long:
    case BK_ULong:  return 3;
    case BK_Float:  return 4;
    case BK_Double: return 5;
    }
    return -1;
}

bool builtinIsFloating(BuiltinKind k) {
    return k == BK_Float || k == BK_Double;
}

bool builtinIsInteger(BuiltinKind k) {
    return k != BK_Void && !builtinIsFloating(k);
}

bool builtinIsArithmetic(BuiltinKind k) {
    return k != BK_Void;
}

// Plain `char` is signed in this implementation, as it is on x86 and arm64.
bool builtinIsSigned(BuiltinKind k) {
    switch (k) {
    case BK_UChar:
    case BK_UShort:
    case BK_UInt:
    case BK_ULong:  return false;
    default:        return true;
    }
}

// --- Types ---

void BuiltinType::print(int indent) {
    printIndent(indent);
    std::cout << name() << std::endl;
}

void ArrayType::print(int indent) {
    printIndent(indent);
    std::cout << "array[" << count << "] of ";
    element->print(0);
}

void PointerType::print(int indent) {
    printIndent(indent);
    std::cout << "pointer to ";
    base->print(0);
}

// --- Expressions ---

void NumberExpr::print(int indent) {
    printIndent(indent);
    std::cout << value;
    if (kind != BK_Int) std::cout << " : " << builtinName(kind);
    std::cout << std::endl;
}

void FloatExpr::print(int indent) {
    printIndent(indent);
    std::cout << value << " : " << builtinName(kind) << std::endl;
}

void StringExpr::print(int indent) {
    printIndent(indent);
    std::cout << "\"" << value << "\"" << std::endl;
}

void IdentExpr::print(int indent) {
    printIndent(indent);
    std::cout << name << std::endl;
}

void UnaryExpr::print(int indent) {
    printIndent(indent);
    std::cout << "Unary " << unaryOpText(op) << std::endl;
    if (operand) operand->print(indent + 1);
}

IndexExpr::~IndexExpr() {
    delete base;
    delete index;
}

void IndexExpr::print(int indent) {
    printIndent(indent);
    std::cout << "Index" << std::endl;
    if (base) base->print(indent + 1);
    if (index) index->print(indent + 1);
}

BinaryExpr::~BinaryExpr() {
    delete lhs;
    delete rhs;
}

void BinaryExpr::print(int indent) {
    printIndent(indent);
    std::cout << "Binary " << binaryOpText(op) << std::endl;
    if (lhs) lhs->print(indent + 1);
    if (rhs) rhs->print(indent + 1);
}

CallExpr::~CallExpr() {
    delete callee;
    for (std::size_t i = 0; i < args.size(); ++i) delete args[i];
}

void CallExpr::print(int indent) {
    printIndent(indent);
    std::cout << "Call" << std::endl;
    if (callee) callee->print(indent + 1);
    for (std::size_t i = 0; i < args.size(); ++i) {
        printIndent(indent + 1);
        std::cout << "arg " << i << ":" << std::endl;
        args[i]->print(indent + 2);
    }
}

// --- Declarations ---

VarDecl::~VarDecl() {
    delete type;
    delete init;
    for (std::size_t i = 0; i < ctorArgs.size(); ++i) delete ctorArgs[i];
}

void VarDecl::print(int indent) {
    printIndent(indent);
    std::cout << "Var " << name << " : ";
    if (type) type->print(0);
    else std::cout << "<none>" << std::endl;
    if (init) {
        printIndent(indent + 1);
        std::cout << "init:" << std::endl;
        init->print(indent + 2);
    }
    if (hasCtorArgs) {
        printIndent(indent + 1);
        std::cout << "construct with " << ctorArgs.size() << " argument(s):" << std::endl;
        for (std::size_t i = 0; i < ctorArgs.size(); ++i) ctorArgs[i]->print(indent + 2);
    }
}

Function::~Function() {
    delete retType;
    for (std::size_t i = 0; i < params.size(); ++i) delete params[i];
    delete body;
}

// Split out so cxx::MethodDecl can change the first line and reuse the rest.
void Function::printSignature(int indent) {
    printIndent(indent);
    std::cout << "Function " << name << " returns ";
    if (retType) retType->print(0);
    else std::cout << "<none>" << std::endl;
}

void Function::printBodyPrefix(int) {
}

void Function::print(int indent) {
    printSignature(indent);
    for (std::size_t i = 0; i < params.size(); ++i) {
        printIndent(indent + 1);
        std::cout << "param:" << std::endl;
        params[i]->print(indent + 2);
    }
    printBodyPrefix(indent + 1);        // virtual: a constructor's init list
    if (body) body->print(indent + 1);
}

// --- Statements ---

CompoundStmt::~CompoundStmt() {
    for (std::size_t i = 0; i < body.size(); ++i) delete body[i];
}

void CompoundStmt::print(int indent) {
    printIndent(indent);
    std::cout << "Block" << std::endl;
    for (std::size_t i = 0; i < body.size(); ++i) body[i]->print(indent + 1);
    for (std::size_t i = 0; i < destroyAtExit.size(); ++i) {
        printIndent(indent + 1);
        std::cout << "[on exit: destroy " << destroyAtExit[i]->name << "]" << std::endl;
    }
}

void DeclStmt::print(int indent) {
    if (var) var->print(indent);
}

void ExprStmt::print(int indent) {
    printIndent(indent);
    std::cout << "ExprStmt" << std::endl;
    if (expr) expr->print(indent + 1);
}

void ReturnStmt::print(int indent) {
    printIndent(indent);
    std::cout << "Return" << std::endl;
    if (expr) expr->print(indent + 1);
}

IfStmt::~IfStmt() {
    delete cond;
    delete thenBranch;
    delete elseBranch;
}

void IfStmt::print(int indent) {
    printIndent(indent);
    std::cout << "If" << std::endl;
    if (cond) cond->print(indent + 1);
    printIndent(indent);
    std::cout << "then:" << std::endl;
    if (thenBranch) thenBranch->print(indent + 1);
    if (elseBranch) {
        printIndent(indent);
        std::cout << "else:" << std::endl;
        elseBranch->print(indent + 1);
    }
}

CastExpr::~CastExpr() {
    delete type;
    delete expr;
}

void CastExpr::print(int indent) {
    printIndent(indent);
    std::cout << "Cast to ";
    if (type) type->print(0);
    else std::cout << "<none>" << std::endl;
    if (expr) expr->print(indent + 1);
}

DoWhileStmt::~DoWhileStmt() {
    delete body;
    delete cond;
}

void DoWhileStmt::print(int indent) {
    printIndent(indent);
    std::cout << "DoWhile" << std::endl;
    if (body) body->print(indent + 1);
    if (cond) cond->print(indent + 1);
}

void CaseStmt::print(int indent) {
    printIndent(indent);
    if (isDefault) std::cout << "default:" << std::endl;
    else           std::cout << "case " << value << ":" << std::endl;
}

SwitchStmt::~SwitchStmt() {
    delete cond;
    delete body;
}

void SwitchStmt::print(int indent) {
    printIndent(indent);
    std::cout << "Switch" << std::endl;
    if (cond) cond->print(indent + 1);
    if (body) body->print(indent + 1);
}

WhileStmt::~WhileStmt() {
    delete cond;
    delete body;
}

void WhileStmt::print(int indent) {
    printIndent(indent);
    std::cout << "While" << std::endl;
    if (cond) cond->print(indent + 1);
    if (body) body->print(indent + 1);
}

ForStmt::~ForStmt() {
    delete init;
    delete cond;
    delete step;
    delete body;
}

void ForStmt::print(int indent) {
    printIndent(indent);
    std::cout << "For" << std::endl;
    if (init) init->print(indent + 1);
    if (cond) cond->print(indent + 1);
    if (step) step->print(indent + 1);
    if (body) body->print(indent + 1);
}

void BreakStmt::print(int indent) {
    printIndent(indent);
    std::cout << "Break" << std::endl;
}

void ContinueStmt::print(int indent) {
    printIndent(indent);
    std::cout << "Continue" << std::endl;
}

} // namespace cc
