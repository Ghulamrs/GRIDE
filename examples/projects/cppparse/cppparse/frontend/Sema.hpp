// Sema.hpp - version 6 stage 3: semantic analysis of the cxx:: syntax tree.
//
// Types are interned canonical objects; symbols are the named entities;
// scopes hold the symbols of a declarative region. Sema walks a translation
// unit, declares everything, resolves names, computes the type of every
// expression with the C++98 implicit conversion rules, chooses overloads,
// evaluates constant expressions, lays classes out, instantiates templates
// on the syntax tree, and reports S-coded diagnostics. The tree is
// annotated in place (Expr::semaType/semaSym, Declarator::semaSym,
// Decl::semaSym) for the IR generator.
//
// Strict C++98.
#ifndef CPPPARSE_SEMA_HPP
#define CPPPARSE_SEMA_HPP

#include "CxxAst.hpp"
#include "Target.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace cppparse {
namespace sema {

struct Symbol;
struct Scope;


struct Type {
    enum Kind {
        Void, Bool, Char, SChar, UChar, WChar, Short, UShort, Int, UInt, Long, ULong, LongLong, ULongLong,
        Float, Double, LongDouble, NullPtr /* the constant 0 as a pointer */,
        Pointer, Reference, Array, Function, Class, Enum, MemberPointer, Dependent, Error
    };
    Kind                     kind;
    bool                     isConst, isVolatile;
    const Type*              inner;        // pointee / referent / element / return type / member pointer target
    long                     arraySize;    // Array: -1 when unknown
    std::vector<const Type*> params;       // Function
    bool                     variadic;     // Function
    bool                     constMethod;  // Function: member function with const this
    Symbol*                  decl;         // Class / Enum: the symbol; Dependent: the template parameter
    const Type*              memberClass;  // MemberPointer

    Type() : kind(Error), isConst(false), isVolatile(false), inner(0), arraySize(-1), variadic(false), constMethod(false), decl(0), memberClass(0) {}

    bool isIntegral()   const { return (kind >= Bool && kind <= ULongLong) || kind == Enum; }
    bool isFloating()   const { return kind == Float || kind == Double || kind == LongDouble; }
    bool isArithmetic() const { return isIntegral() || isFloating(); }
    bool isPointer()    const { return kind == Pointer || kind == NullPtr; }
    bool isScalar()     const { return isArithmetic() || isPointer() || kind == MemberPointer || kind == Dependent; }
    bool isClass()      const { return kind == Class; }
    bool isReference()  const { return kind == Reference; }
    bool isVoid()       const { return kind == Void; }
    bool isUnsigned()   const { return kind == Bool || kind == UChar || kind == UShort || kind == UInt || kind == ULong || kind == ULongLong; }   // char is signed, as with cl
    bool isDependent()  const;
    std::string key()  const;   // interning key
    std::string text() const;   // "const int*"
};

class TypeTable {
public:
    TypeTable();
    ~TypeTable();
    const Type* basic(Type::Kind k) const { return m_basic[k]; }
    const Type* qualified(const Type* t, bool isConst, bool isVolatile);   // adds qualifiers (never removes)
    const Type* unqualified(const Type* t);
    const Type* pointer(const Type* to);
    const Type* reference(const Type* to);
    const Type* array(const Type* of, long size);
    const Type* function(const Type* ret, const std::vector<const Type*>& params, bool variadic, bool constMethod);
    const Type* memberPointer(const Type* cls, const Type* target);
    const Type* classType(Symbol* cls);
    const Type* enumType(Symbol* en);
    const Type* dependent(Symbol* param);
    const Type* nullPtr() const { return m_basic[Type::NullPtr]; }
    const Type* error() const { return m_basic[Type::Error]; }
    // decay: array -> pointer, function -> pointer, reference -> referent
    const Type* decay(const Type* t);
    const Type* unref(const Type* t) const { return t && t->kind == Type::Reference ? t->inner : t; }
    int  sizeOf(const Type* t) const;
    int  alignOf(const Type* t) const;
    static void memberPointerModel(const Symbol* cls, bool& multiple, bool& virt, int depth);
    // cl's value of &C::m, m a non-static member of C, as a pointer to member of C (measured on cl's objects): a data
    // member's offset (in the virtual model counted from the subobject that holds C's vbptr, where its vbtable's
    // entry 0 leads); a function's this adjustment (from that subobject too in the virtual model: X : A0, B1 with
    // B1's vbptr shared gives -16); for a virtual function whose slot is in a vftable of a virtual base, the byte offset
    // of that base's entry in C's vbtable (vbindex, else 0), the adjustment then counting from the base, and in the
    // unspecified model the vbptr's offset with it (vbptr, else 0)
    static void memberPointerValue(const Symbol* m, int& offset, int& adjust, int& vbptr, int& vbindex);
    static int  vbptrOrigin(const Symbol* cls);   // where C's vbptr's subobject starts (0 without virtual bases)
    // t is, or holds in an element, base or member (not through a reference), a pointer to member
    static bool holdsMemberPointer(const Type* t, int depth = 0);
    int  rank(const Type* t) const;        // integer conversion rank
    const Type* promoted(const Type* t);   // integral / floating promotion
    const Type* arithmeticResult(const Type* a, const Type* b);   // usual arithmetic conversions
    void setTarget(const Target& target);  // the sizes of long, wchar_t, long double and pointers
    const Type* sizeType() const { return m_basic[m_sizeIsLong ? Type::ULong : Type::ULongLong]; }     // size_t
    const Type* ptrdiffType() const { return m_basic[m_sizeIsLong ? Type::Long : Type::LongLong]; }    // ptrdiff_t
private:
    const Type* intern(const Type& t);
    const Type*                    m_basic[Type::Error + 1];
    std::map<std::string, Type*>   m_types;
    int                            m_pointerSize, m_longSize, m_wcharSize, m_longDoubleSize, m_maxAlign;
    bool                           m_sizeIsLong;
};


struct BaseClass {
    Symbol*     cls;
    cxx::Access access;
    bool        isVirtual;
    int         offset;
    BaseClass() : cls(0), access(cxx::AccessPublic), isVirtual(false), offset(0) {}
};

// one vfptr of a polymorphic class and the table it points to (MSVC: one vftable per polymorphic base subobject
// that does not share the vfptr at offset 0). fns[s] is the final overrider in slot s; thunks[s] is how far the
// entry moves this down (from the vfptr's subobject to where the overrider expects it): non-zero means the slot
// holds an adjustor thunk ("sub rcx, n; jmp f")
struct VfTable {
    int                  offset;          // of the vfptr in the class
    Symbol*              introducedBy;    // the class whose own vfptr this is (names the table: ??_7D@@6BPB@@@)
    std::vector<Symbol*> fns;
    std::vector<int>     thunks;
    // cl's covariant returns: an overrider whose return class holds the overridden function's return class at a
    // non-zero offset gets a slot of its own, and every slot it overrides holds a return-adjusting thunk that calls
    // fns[s] and moves the result by returnAdjust[s] (-1: through a virtual base, not generated); returnOf[s] is the
    // slot's original function, whose return type the thunk returns (0: a plain slot)
    std::vector<Symbol*> returnOf;
    std::vector<int>     returnAdjust;
    // a vfptr inside a virtual base of the class (vbase; offset is where that base lies in a complete object of the
    // class, its vfptr is found through the vbtable): overriders from outside the base are reached through thunks
    // that move this by thunks[s], first subtracting the vtordisp field before the base when the class has one
    // (vtordisp: cl's $4 thunks; the field holds how far the base moved from where this class's layout has it)
    Symbol*              vbase;
    bool                 vtordisp;
    // per slot of such a table with a vtordisp: the virtual base of the class that is the overrider's own class, when
    // that is not the table's base (its distance from the base varies: cl's vtordispex thunk reads its vbtable entry,
    // $R4); 0 for the other slots (an overrider in a non-virtual base of another virtual base gets the $4 thunk of
    // the static layout, as cl gives it)
    std::vector<Symbol*> vtordispexVbase;
    Symbol*              vtordispex;      // such an overrider whose thunk --compile6 cannot build (no vbtable entry for
                                          // its base); 0 when the table has none
    // per slot of a table inside a virtual base: the class that appended the slot (cl puts a covariant override's slot
    // of its own at the end of the virtual base's table that holds the overridden function: CV : virtual VB { Y* get(); }
    // over VB::get returning X*), 0 for the base's own slots. The appending class's function in it is reached without
    // the vtordisp (cl: ?get@CV@@UEAAPEAUY@@XZ itself, or a plain adjustor in a bigger class), any other through it
    std::vector<Symbol*> slotClass;
    VfTable() : offset(0), introducedBy(0), vbase(0), vtordisp(false), vtordispex(0) {}
};

// one vbptr of a class with virtual bases and its vbtable (MSVC: ??_8D@@7B@): entries[0] is the offset of the
// vbptr from the start of the subobject that owns it (negated), entries[k] the offset of the virtual base
// order[k - 1] from the vbptr, in a complete object of the class
struct VbTable {
    int                  offset;          // of the vbptr in the class
    Symbol*              introducedBy;    // the class whose own vbptr this is
    std::vector<Symbol*> order;           // the virtual bases indexed by the table (index k + 1)
    std::vector<int>     entries;
    std::vector<Symbol*> path;            // the bases that name the table apart (cl's ??_8VX@@7BVL@@@)
    Symbol*              nextToMangle;    // the base appended to path when two tables would share a name
    VbTable() : offset(0), introducedBy(0), nextToMangle(0) {}
};

struct Symbol {
    enum Kind { Namespace, Class, Enum, Typedef, Variable, Function, Enumerator, TemplateParam, ClassTemplate, FunctionTemplate, Label };
    Kind          kind;
    std::string   name;            // "Circle", "operator+", "~Shape", "Circle<int>" for instantiations
    Scope*        scope;           // the scope the symbol is declared in
    const Type*   type;            // Typedef: the aliased type; Variable: its type; Function: the function type; Class/Enum: the class/enum type
    cxx::Loc      loc;
    cxx::Access   access;
    // namespaces, classes: the member scope
    Scope*        members;
    // classes
    std::vector<BaseClass>   bases;
    bool          isDefinition, isStruct, isUnion, isPolymorphic, isAbstract, layoutDone;
    int           size, align;
    // MSVC's empty-base rules: the size without the byte an empty class is given (0 for an empty class), and
    // whether the class starts / ends with a zero-sized subobject (see Sema::layoutClass)
    int           nvSize;
    bool          leadsWithZeroSized, endsWithZeroSized;
    std::vector<Symbol*>     fields;      // non-static data members in order (offset in Symbol::offset)
    std::vector<Symbol*>     unnamedBitFields;   // ": n" members, not named and not initialised: laid out before fields[index]
    std::vector<Symbol*>     vtable;      // virtual functions in slot order (including inherited)
    std::vector<VfTable>     vftables;    // every vfptr of the class: [0] the one at offset 0 (fns == vtable), then the secondary ones
    int           vptrOffset;             // -1 when no vptr
    // MSVC's virtual bases (Sema::layoutClass): every virtual base, direct or inherited, in cl's order (depth
    // first, a base's own virtual bases before it), each placed once after the non-virtual part (nvSize) at
    // vbaseOffsets[i] of a complete object; vbptrOffset is the vbptr whose table indexes them all (own or shared
    // with the first non-virtual base that has one), -1 without virtual bases; vbtables: every vbptr
    std::vector<Symbol*>     vbases;
    std::vector<int>         vbaseOffsets;
    std::vector<VbTable>     vbtables;
    int           vbptrOffset;
    // per vbases[i]: cl's vtordisp, an int just before the base (at vbaseOffsets[i] - 4) that a class with a
    // user-declared constructor or destructor and an override of one of the base's virtual functions gets (and
    // every class derived from one that has it)
    std::vector<bool>        vtordisp;
    cxx::ClassDecl* classAst;
    // functions
    std::vector<Symbol*>     params;
    bool          isVirtual, isPureVirtual, isStatic, isExtern, isInline, isExplicit, isConstructor, isDestructor, isConversion, isOperator, isFriendDecl, hasBody, isExternC;
    cxx::Decl*    definition;             // the function definition (or 0)
    cxx::Decl*    declAst;                // the declaration this symbol came from
    Scope*        body;                   // function scope (parameters) when defined
    int           vtableSlot;             // in the vftable at thisAdjust
    int           thisAdjust;             // virtual function: MSVC's this adjustor, the offset in its class of the vfptr
                                          // whose table first holds it; callers pass this + thisAdjust (0 for destructors)
    int           vfptrOffset;            // virtual function: the vfptr (in its class) whose table holds it at vtableSlot;
                                          // equal to thisAdjust except for a destructor reached through a secondary table
    Symbol*       vfptrVbase;             // virtual function: the virtual base of its class whose vftable first holds it (its
                                          // vfptr is found through the vbtable; vfptrOffset is the base's place in the class's
                                          // layout, which this takes on entry); 0 when the vfptr is at a fixed offset
    Symbol*       overrides;
    Symbol*       ownerClass;             // member functions / static members / fields
    // variables, enumerators, fields
    int           offset;                 // field offset within the class
    int           index;                  // parameter index / enumerator order
    bool          isMember, isParam, isLocal, isMutable;
    cxx::Expr*    init;
    Symbol*       initCtor;               // variable of class type: the constructor chosen for its initialisation
    bool          hasConstValue;
    long long     constValue;             // the value in its type: the low 32 bits sign-extended for a type of 4 bytes or less, all 64 bits of a 64-bit type
    int           declAlign;              // __declspec(align(N)) on a variable or field (0: none)
    int           bitWidth, bitOffset;    // bit-field: its width (-1: not a bit-field) and first bit in the unit at offset
    // a member of an anonymous union or struct, seen as a member of the enclosing class: the unnamed member
    // holding the anonymous object and the member of it this symbol names (offset = anonHost's + anonOf's)
    Symbol*       anonHost;
    Symbol*       anonOf;
    // templates
    cxx::Decl*    templateAst;            // the template declaration (Decl::Template)
    std::vector<Symbol*>     templateParams;
    std::map<std::string, Symbol*> instantiations;   // argument text -> instantiated class / function
    Symbol*       instantiatedFrom;
    std::vector<const Type*> templateArgs;           // for an instantiation: the type arguments (0 for non-type)
    std::vector<long long>   templateIntArgs;
    std::vector<Symbol*>     templateSymArgs;        // for an instantiation: the member a pointer-to-member argument names (0 for the others; templateIntArgs holds a data member's offset)
    Symbol*       memberArg;              // a non-type template parameter bound in an instantiation to the pointer to member &C::m: m
    bool          isDependentContext;     // declared inside a template: not analysed until instantiated
    bool          isImplicitInstantiation; // class: made from a template by instantiateClassTemplate (not an explicit specialisation)
    bool          isLazyInstantiation;    // function analysed tentatively (Sema::isLazyMember): generated only when used
    bool          instantiationFailed;    // such a function whose body has errors: its diagnostics are held back and
                                          // it is never generated; a use of it is an error (14.7.1)
    bool          isExplicitInstantiation; // function named by an explicit instantiation definition (14.7.2): analysed and
                                          // generated as any function, so that the unit emits its COMDAT for the others
    std::vector<Symbol*>     partialSpecs;        // class template: its partial specialisations (ClassTemplate symbols)
    std::vector<cxx::TemplateArg> patternArgs;    // partial specialisation: the argument pattern
    bool          isImplicit;             // compiler-generated constructor / destructor / assignment
    int           requiredParams;         // function: parameters without default arguments
    bool          hasUserCtor, hasCopyCtor, hasDtor, hasCopyAssign, hasDefaultCtor;   // class: user-declared special members
    // class: the size of a pointer to member of it was needed while it was incomplete (TypeTable::sizeOf); cl
    // decides the inheritance model there and keeps the unspecified one (data 12, functions 24) once it is defined
    bool          memberPointerUnspecified;
    int           defOrder;               // class: the order its definition began in (0: inside a function body)
    // access control (clause 11): the friends of a class (functions and classes; the friend declarations of a
    // template's definition are repeated in each instantiation); friendsUnknown when a friend declaration could
    // not be resolved (a template, a function of another scope): then nothing of the class is checked
    std::vector<Symbol*>     friends;
    std::vector<std::string> friendNames;   // "friend class List;" before List is declared: matched by name
    bool          friendsUnknown;
    bool          isUsed;                 // variable, parameter: referred to by an expression (C4100, C4101, C4189)
    bool          isNoreturn;             // function: __declspec(noreturn)
    std::string   deletedWhy;             // implicit copy assignment that cannot be generated (12.8/12): why
    std::string   mangled;                // filled by code generation

    Symbol();
    std::string qualifiedName() const;   // "geom::Circle::area"
    // constructors of "Buffer<int, 4>" are named "Buffer", like those of the template
    std::string ctorName() const { return instantiatedFrom ? instantiatedFrom->name : name; }
    bool isType() const { return kind == Class || kind == Enum || kind == Typedef || kind == TemplateParam; }
};

struct Scope {
    enum Kind { Global, Namespace, Class, Function, Block, TemplateParams, Instantiation };
    Kind                                       kind;
    Scope*                                     parent;
    Symbol*                                    owner;       // namespace / class / function symbol
    std::map<std::string, std::vector<Symbol*> > names;     // overload sets
    std::vector<Symbol*>                       order;       // declaration order
    std::vector<Scope*>                        usingDirectives;
    int                                        blockIndex;  // Block: the n-th block scope of its function (0: the body), for the names of local classes
    Scope() : kind(Block), parent(0), owner(0), blockIndex(0) {}
    void add(Symbol* s) { names[s->name].push_back(s); order.push_back(s); }
    const std::vector<Symbol*>* find(const std::string& name) const { std::map<std::string, std::vector<Symbol*> >::const_iterator it = names.find(name); return it == names.end() ? 0 : &it->second; }
    bool isNamespaceLike() const { return kind == Global || kind == Namespace; }
};

// the value of an integer literal's digits modulo 2^64 (SemaExpr.cpp); *overflow when it needs more bits
unsigned long long integerLiteralValue(const std::string& text, bool* overflow);

// folded integer constants (Expr::semaValue, Symbol::constValue, Symbol::templateIntArgs) hold their value in their
// type, modulo 2^N: all 64 bits of a type of 8 bytes, the low 32 bits sign-extended for one of 4 bytes or less (an
// unsigned int 0xFFFFFFFF is -1, as when semaValue was a 32-bit long), the value of a char or short.
// foldedConstant: v as a value of type t; convertedConstant: a value of type from converted to type to (4.7)
long long foldedConstant(const TypeTable& types, long long v, const Type* t);
long long convertedConstant(const TypeTable& types, long long v, const Type* from, const Type* to);

struct Diag {
    enum Severity { Error, Warning, Note };
    Severity    severity;
    std::string code;        // "S100"
    cxx::Loc    loc;
    std::string message;
    Diag() : severity(Error) {}
};

struct SemaStats {
    int symbols, classes, functionsDefined, variables, expressionsTyped, namesResolved, namesUnresolved, callsResolved, callsAmbiguous, classInstantiations, functionInstantiations;
    SemaStats() : symbols(0), classes(0), functionsDefined(0), variables(0), expressionsTyped(0), namesResolved(0), namesUnresolved(0), callsResolved(0), callsAmbiguous(0), classInstantiations(0), functionInstantiations(0) {}
};

// the result of analysing one translation unit
struct Program {
    Scope*               global;
    std::vector<Symbol*> symbols;        // every symbol, in creation order (owned)
    std::vector<Scope*>  scopes;         // every scope (owned)
    std::map<Symbol*, int> blockCounts;  // function -> its block scopes so far (Scope::blockIndex)
    Program();
    ~Program();
    Symbol* newSymbol(Symbol::Kind k, const std::string& name, Scope* in);
    Scope*  newScope(Scope::Kind k, Scope* parent, Symbol* owner);
};

// an implicit conversion sequence, ranked as in C++98 13.3.3.1
struct Conversion {
    enum Rank { Exact = 0, Promotion = 1, Standard = 2, UserDefined = 3, Ellipsis = 4, None = 5 };
    Rank        rank;
    Symbol*     userFunction;    // converting constructor or conversion operator
    const Type* target;
    bool        derivedToBase;   // a pointer/reference conversion to a base class (better than the other standard conversions, 13.3.3.2/4)
    bool        qualification;   // only cv-qualification added
    bool        pointerToVoid;   // an object pointer converted to a (cv) void* (4.10/2), not a null pointer constant
    Rank        second;          // UserDefined: the rank of the standard conversion after the user conversion (13.3.3.2/3)
    Symbol*     function;        // the name of an overloaded function or a function template: the one of the target's type (13.4)
    Conversion() : rank(None), userFunction(0), target(0), derivedToBase(false), qualification(false), pointerToVoid(false), second(Exact), function(0) {}
};

class Sema {
public:
    Sema(TypeTable& types, Program& program);
    void useArena(cxx::CxxArena& arena) { m_externalArena = &arena; }   // where nodes added to the tree are allocated
    ~Sema();
    void analyze(cxx::TranslationUnit& tu, const std::vector<std::string>& files);
    const std::vector<Diag>& diagnostics() const { return m_diags; }
    const SemaStats& stats() const { return m_stats; }
    int errorCount() const;
    std::string dumpSymbols() const;     // text dump of the global scope for tests
    // the diagnostics held back for a member with instantiationFailed (0 when there are none)
    const std::vector<Diag>* heldDiagnostics(Symbol* fn) const;

    // used by later stages
    Symbol* lookupQualified(const cxx::QualifiedName& n, Scope* from, bool typesOnly);
    const Type* typeOf(const cxx::DeclSpec& spec, Scope* in);
    const Type* applyDeclarator(const Type* base, const cxx::Declarator& d, Scope* in, std::vector<Symbol*>* params);

private:
    // declarations
    void   declareAll(const std::vector<cxx::Decl*>& decls, Scope* in, cxx::Access access);
    void   declare(cxx::Decl* d, Scope* in, cxx::Access access);
    void   declareSimple(cxx::Decl* d, Scope* in, cxx::Access access);
    Symbol* declareFunction(cxx::Decl* d, cxx::Declarator* dcl, const cxx::DeclSpec& spec, Scope* in, cxx::Access access, const Type* ft, const std::vector<Symbol*>& params);
    Symbol* declareClass(cxx::ClassDecl* c, Scope* in, cxx::Access access, Symbol* existing);
    void   declareAnonymousMember(Symbol* anon, Scope* in, cxx::Access access);
    Symbol* declareEnum(cxx::EnumDecl* e, Scope* in, cxx::Access access);
    void   declareTemplate(cxx::Decl* d, Scope* in, cxx::Access access);
    void   declareTemplateMemberDefinition(Symbol* tmpl, Symbol* cls, Symbol* mt);
    void   declareExplicitInstantiation(cxx::Decl* d, Scope* in);
    void   explicitlyInstantiate(Symbol* fn);
    void   explicitlyInstantiateMembers(Symbol* cls, int depth);
    Symbol* classTemplateOfQualifier(const cxx::QualifiedName& n, Scope* in);
    void   completeClass(Symbol* cls);          // layout, vtable, implicit members
    void   layoutClass(Symbol* cls);
    Symbol* findClassSymbolForQualifier(const cxx::QualifiedName& n, Scope* from);
    Scope* scopeForQualifier(const cxx::QualifiedName& n, Scope* from);
    Symbol* injectedTemplate(Symbol* s, const cxx::NameComponent& c) const;   // the template an injected name with arguments names
    Symbol* explicitSpecialization(Symbol* primary, const std::string& key, const std::vector<const Type*>& types, const std::vector<long long>& ints, Scope* in);

    // bodies
    // order: where the body stands among the class definitions of the unit (Symbol::defOrder), for cl's
    // member-pointer models (memberPointerSizeUse)
    struct PendingBody { Symbol* fn; cxx::Decl* def; Scope* scope; int order; };
    void   queueBody(PendingBody pb);
    void   memberPointerSizeUse(const Type* t);
    void   analyzeBodies();
    // implicit instantiation (C++98 14.7.1): a non-virtual member function of a class template instantiation,
    // and a function template specialisation, is analysed tentatively; errors in its body count only when it
    // is reachable from code that is not itself tentative
    bool   isLazyMember(const Symbol* fn) const;
    void   noteUse(Symbol* used);           // m_currentFunction (0 at namespace scope) refers to used
    // access control (clause 11) and the other checks of the 2026-10-15 review's M12
    bool   accessible(Symbol* member, Scope* from);
    void   accessCheck(Symbol* member, const cxx::Loc& loc, Scope* from);
    bool   isFriendOf(Symbol* x, Symbol* cls);
    void   abstractCheck(const Type* t, const cxx::Loc& loc);
    bool   assignmentDeleted(Symbol* cls);
    void   narrowingWarning(cxx::Expr* from, const Type* to, const char* what);
    // the warnings cl gives at /W4 that need the whole body (functionWarnings runs after analyzeFunctionBody)
    void   functionWarnings(Symbol* fn, cxx::Decl* def, size_t diagMark);
    bool   mayFallThrough(cxx::Stmt* s);
    bool   hasJump(cxx::Stmt* s, bool wantContinue);
    bool   hasDefault(cxx::Stmt* s);
    bool   generatesCode(cxx::Stmt* s);
    bool   isNoreturnCall(cxx::Expr* e);
    void   unreachableCheck(cxx::Stmt* s);
    void   uninitStmt(cxx::Stmt* s, std::set<Symbol*>& assigned);
    void   uninitDecl(cxx::Decl* d, std::set<Symbol*>& assigned);
    void   uninitExpr(cxx::Expr* e, std::set<Symbol*>& assigned, bool write);
    void   reportUsedFailedMembers();       // releases the held diagnostics of every member reachable from real code
    void   analyzeFunctionBody(Symbol* fn, cxx::Decl* def, Scope* enclosing);
    void   stmt(cxx::Stmt* s, Scope* in, Symbol* fn);
    void   localDecl(cxx::Decl* d, Scope* in, bool isHandler = false);

    // lookup
    Symbol*  lookupUnqualified(const std::string& name, Scope* from, bool typesOnly, std::vector<Symbol*>* all);
    bool     lookupInScope(Scope* s, const std::string& name, bool typesOnly, std::vector<Symbol*>& out, std::set<Scope*>& seen);
    bool     lookupMember(Symbol* cls, const std::string& name, std::vector<Symbol*>& out, int depth = 0);
    bool     memberLookupAmbiguous(Symbol* cls, const std::string& name);   // 10.2/2 (cl C2385)
    bool     ambiguousMemberName(const cxx::QualifiedName& n, Symbol* s, Scope* in, const cxx::Loc& loc);   // reports S144
    Symbol*  resolveName(const cxx::QualifiedName& n, Scope* from, bool typesOnly, std::vector<Symbol*>* overloads);
    // argument-dependent lookup (3.4.2): the namespaces associated with a type, and the functions named name
    // declared in the namespaces associated with the arguments' types (in-class friend definitions among them)
    void     associatedNamespaces(const Type* t, std::vector<Scope*>& out, int depth);
    void     adlCandidates(const std::string& name, const std::vector<cxx::Expr*>& args, size_t firstArg, std::vector<Symbol*>& cands);

    // types
    const Type* namedType(const cxx::QualifiedName& n, Scope* in, const std::string& elaborated, const cxx::Loc& loc);
    const Type* instantiateClassTemplate(Symbol* tmpl, const std::vector<cxx::TemplateArg>& args, Scope* from, const cxx::Loc& loc);
    // params: the template's parameters, whose types the value arguments are converted to (0: as written)
    // syms: the members pointer-to-member arguments name (&C::m; 0 for the other arguments)
    bool   templateArgText(const std::vector<cxx::TemplateArg>& args, Scope* from, std::vector<const Type*>& types, std::vector<long long>& ints, std::string& text, const std::vector<Symbol*>* params = 0, std::vector<Symbol*>* syms = 0);
    Symbol* memberPointerArg(cxx::Expr* x, const Symbol* param);   // &C::m as a template argument (14.3.2/1), 0 when it is not one
    void   bindMemberArg(Symbol* b, Symbol* m);
    static bool anyMemberArg(const std::vector<Symbol*>& syms);
    bool   derivesFrom(Symbol* derived, Symbol* base, int* offset, int depth = 0);
    int    baseSubobjects(Symbol* derived, Symbol* base);   // subobjects of base in derived (C2594 / C2385 when > 1)

    // expressions
    const Type* expr(cxx::Expr* e, Scope* in);
    const Type* exprName(cxx::Expr* e, Scope* in);
    const Type* exprMember(cxx::Expr* e, Scope* in);
    const Type* exprCall(cxx::Expr* e, Scope* in);
    const Type* exprBinary(cxx::Expr* e, Scope* in);
    cxx::Expr*  boolContext(cxx::Expr* e, Scope* in);
    cxx::Expr*  converted(cxx::Expr* e, const Conversion& c);
    void        promoteVariadic(std::vector<cxx::Expr*>& args, size_t firstArg, const Type* ft);
    const Type* exprUnary(cxx::Expr* e, Scope* in);
    const Type* exprAssign(cxx::Expr* e, Scope* in);
    const Type* exprNew(cxx::Expr* e, Scope* in);
    const Type* exprCast(cxx::Expr* e, Scope* in, const Type* to, bool checked);
    bool        dynamicCastCheck(cxx::Expr* e, const Type* to);
    Symbol*     stdClass(const char* name);       // std::type_info, std::bad_cast when declared
    bool        floatingConstant(cxx::Expr* e, double& v, bool toFloating, int depth);
    const Type* valueType(cxx::Expr* e);          // lvalue-to-rvalue: the type after decay, without top-level cv
    // brace initialisation of aggregates: elided braces become explicit InitList nodes and every element
    // initializer is converted to its element type
    bool        isAggregate(const Type* t);
    long        braceCount(const Type* t, const std::vector<cxx::Expr*>& items);   // elements of an array of unknown bound
    cxx::Expr*  braceInit(const Type* t, std::vector<cxx::Expr*>& items, size_t& pos, Scope* in, const cxx::Loc& loc, long* count);
    cxx::Expr*  braceElement(const Type* t, std::vector<cxx::Expr*>& items, size_t& pos, Scope* in);
    bool        literalForCharArray(const Type* u, cxx::Expr* it);
    void        braceDeclInit(Symbol* var, cxx::Initializer& init, Scope* in);
    const Type* completedArray(const Type* t, const cxx::Initializer& init);
    bool   completeForSizeof(const Type* t, Scope* in, const cxx::Loc& loc);   // false, with S113, for an incomplete type
    bool   constantValue(cxx::Expr* e, long long& out);
    long long fitConstant(long long v, const Type* t) const { return foldedConstant(m_types, v, t); }
    long long constantIn(long long v, const Type* from, const Type* to) const { return convertedConstant(m_types, v, from, to); }
    long long exprConstantIn(const cxx::Expr* e, const Type* to) const { return convertedConstant(m_types, e->semaValue, e->semaType, to); }
    void      foldBinary(cxx::Expr* e, const std::string& op, const Type* l, const Type* r, const Type* rt);
    // the type a non-type template parameter is bound with in an instantiation: its declared integral type, else int
    const Type* valueParamType(const Symbol* param) const { return param && param->type && param->type->isIntegral() ? m_types.unqualified(param->type) : m_types.basic(Type::Int); }
    Symbol* classOperator(const std::string& op, cxx::Expr* e, const std::vector<cxx::Expr*>& operands, Scope* in, const Type*& result);
    // explicitArgs: the template arguments written on the call (f<int>(x)), bound before deduction
    Symbol* resolveOverload(const std::vector<Symbol*>& cands, const std::vector<cxx::Expr*>& args, size_t firstArg, cxx::Expr* thisExpr, Scope* in, const cxx::Loc& loc, const std::string& what, bool report, const std::vector<cxx::TemplateArg>* explicitArgs = 0);
    Conversion convert(cxx::Expr* from, const Type* to, Scope* in);
    Conversion convertType(const Type* from, bool lvalue, bool nullConstant, const Type* to, Scope* in, bool allowUser);
    Symbol* instantiateFunctionTemplate(Symbol* tmpl, const std::vector<cxx::Expr*>& args, size_t firstArg, Scope* in, const cxx::Loc& loc, const std::vector<cxx::TemplateArg>* explicitArgs = 0);
    Symbol* instantiateBoundFunctionTemplate(Symbol* tmpl, std::map<std::string, const Type*>& bind, std::map<std::string, long long>& ints);
    bool   matchFunctionTemplate(Symbol* tmpl, cxx::Declarator* dcl, const Type* ft, Scope* in, std::map<std::string, const Type*>& bind, std::map<std::string, long long>& ints);
    bool   atLeastAsSpecialized(Symbol* a, Symbol* b);   // 14.5.5.2: b's parameters deduce from a's with a's parameters unique types
    static bool typeHasError(const Type* t);
    void   declareFunctionSpecialization(cxx::Decl* d, cxx::Decl* inner, cxx::Declarator* dcl, Scope* in, cxx::Access access);
    bool   deduce(const cxx::Param* p, const Type* argType, Scope* tscope, std::map<std::string, const Type*>& bindings, std::map<std::string, long long>& ints);
    const Type* resultOfCall(Symbol* fn);

    // diagnostics
    void error(const cxx::Loc& loc, const char* code, const std::string& msg);
    void warning(const cxx::Loc& loc, const char* code, const std::string& msg);
    std::string where(const cxx::Loc& loc) const;

    TypeTable&                 m_types;
    Program&                   m_prog;
    std::vector<Diag>          m_diags;
    SemaStats                  m_stats;
    std::vector<std::string>   m_files;
    std::vector<PendingBody>   m_pending;
    Symbol*                    m_currentFunction;
    Symbol*                    m_currentClass;
    int                        m_loopDepth, m_switchDepth, m_depth;
    std::vector<Symbol*>       m_locals;          // the scalar locals of the body being analysed (functionWarnings)
    std::set<Symbol*>          m_uninitCandidates;
    bool                       m_hasGoto;         // the body has a goto: no flow warnings
    int                        m_classOrder;      // class definitions begun so far in the declaration pass
    int                        m_bodyOrder;       // the order of the body being analysed (-1: declaration pass)
    int                        m_definingDepth;   // class definitions open
    std::set<std::string>      m_reportedUnknown;
    std::set<std::string>      m_fixedTemplateParams;   // the template parameters given explicitly on the call being deduced (not deduced, converted)
    std::map<std::string, Symbol*> m_boundMembers;      // the pointer-to-member arguments given explicitly to the function template being instantiated, by parameter
    bool                       m_strictDeduce;          // partial ordering: a const in the parameter needs a const in the argument
    int                        m_instantiationDepth;    // class template instantiations nested (cl stops at 1000: C2999)
    bool                       m_instantiationOverflow; // the limit was reported once
    std::map<Symbol*, std::set<Symbol*> > m_uses;          // function (0: namespace scope) -> functions it refers to
    std::set<Symbol*>                     m_lazyMembers;   // members analysed tentatively
    std::map<Symbol*, std::vector<Diag> > m_held;          // their held-back diagnostics
    // nodes sema adds to the tree (instantiation copies, conversion wrappers) live in the translation
    // unit's arena when one is given, so they outlive this object; m_cloneArena is the fallback
    cxx::CxxArena*                m_externalArena;
    cxx::CxxArena                 m_cloneArena;
    cxx::CxxArena&                nodeArena() { return m_externalArena ? *m_externalArena : m_cloneArena; }
    std::vector<cxx::Decl*>       m_ownedDecls;        // rewritten copies of out-of-line template member definitions
    std::vector<cxx::Declarator*> m_ownedDeclarators;
};

} // namespace sema
} // namespace cppparse

#endif
