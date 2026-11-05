#ifndef CXX1_VERSION_H
#define CXX1_VERSION_H

// **The release, in one place.**
//
// 1.24 (2026-10-21) is 1.23 - mended against clang on macOS - with what 1.10 found against cl on
// Windows: masm and link started without cmd, CPP11_DECLINES empty or 0 as unset, `volatile` said as a
// warning at its line, deduction inside a template-id matching the argument as it is, the UCRT's
// headers (time_t of eight bytes, _time64, _findfirst, <sys/stat.h>, <windows.h>, <direct.h>), the
// string members 1.23 lacked, <algorithm>'s last functions inside std, and the headers copied
// beside cpp11.exe.
//
// 1.25 (2026-10-21): a braced list inside the braces that call a constructor initialises the
// parameter it reaches - `const CMatrix<3, 3> X = { {1.0, 2.0, 3.0}, ... }` makes each row the
// CVector<3> of CMatrix(CVector<3>, CVector<3>, CVector<3>), as cl and clang++ -std=c++11 do - and a
// braced call argument goes to the overloads whose parameter such a list can initialise.
//
// 1.26 (2026-10-21): the first five fixes of the 1.25 review - a temporary bound to `T &&`, `auto &&`
// or `const T &` is the call's own object and lives to the reference's scope's end, a temporary a
// reference holds a member of is destroyed there too, a static local whose initialiser throws is
// built again on the next call, the implicit move assignment exists and a written copy assignment
// suppresses the implicit move constructor, and a 256 MB stack with the parser's nesting bounded by
// name where 80 parentheses or a 40-deep template recursion used to overflow it.
//
// 1.27 (2026-10-21): the 1.25 review's two remaining items. Breadth - overload ranking by cv and by
// the nearer base, a user-defined conversion's second standard conversion, deduction keeping a
// reference's const and seeing through `T (&)[N]`, `const T &&`, ADL on an enumeration's operators,
// a nested class reading the outer class's later and private members, a local class's members by
// the written name, `decltype` of a comparison, of a string literal and of `c ? x : cx`, `PB::v`
// and `Base<T>::v` through `this`, `a[arr]`, `return {4, 5}`, `S{"s", 1}` for an aggregate holding a
// std::string, `int a[]{...}`, `const int &r{9}`, `throw` as an expression, `__func__`, a UTF-8 BOM,
// `(1, 3)` as a constant, `-1 > 0u` in `#if`, `_Pragma`, a function-like macro's name at the end of
// an expansion rescanned with the tokens after it, pointers to member functions in arrays and against
// null, `&Outer::K`, `const int Outer::K;` holding its value, a lambda reading the enclosing
// function's constants, a member of an rvalue being an xvalue, and a function template's signature
// read in its own namespace. Library - vector::at and array::at throw out_of_range on every target,
// vector moves where the standard moves and takes a move-only element type, std::swap moves, a map's
// element is pair<const K, V> - its insert takes the pair<K, V> make_pair gives, and a pair<K, V> is
// made from it - std::declval.
//
// 1.28 (2026-10-22): the 1.25 review's wrong-code set. A static local of an inline, in-class or
// template function is one object for the program - in a COMDAT under cl's name, `?n@?1??f@@YAHXZ@4HA`
// with its `?$TSS0@...` guard, and under Itanium's `_ZZ1fvE1n` with `_ZGV`, so a cpp11 unit and a cl
// unit share it; `int *` and `const int *` thrown in one unit write their `_CTA2PEAH` once; a member
// template called with explicit arguments, `s.m<int>(1)`, keeps its definition in the unit; and three
// ill-formed programs are refused: `static_cast` between pointers to unrelated objects, an `explicit
// operator bool` in copy-initialisation, and `--` on a bool.
//
// 1.29 (2026-10-22): -g through GRIDE's own masm, the PDB line's cpp11 part. In the MASM spelling
// -g is no longer refused: the lines go out as the `.FILE n, "path"` and `.LINE line, n` directives
// GRIDE's masm reads, by the `.cv_loc` rules, and the CodeView the gnu spelling writes for clang -
// S_GPROC32 with the display name, parameters and locals, blocks, globals, the types - is spelled
// for masm as `.debug$S` and `.debug$T` SEGMENTs, a mergeable function's ASSOCIATIVE with its
// COMDAT; the driver assembles with the masm beside this program, and link /DEBUG makes the PDB.
// For the debugger that reads it: every name is whole, with its namespaces, as cl's PDB spells it -
// `std::basic_string<char>::size`, `geo::Box<int>::count`, `geo::Pair::Pair` - a template specialization
// with its arguments, `biggest<int>`, and a handler funclet is a procedure of its own with its lines,
// cl's `` `f'::`1'::catch$0 ``, so a breakpoint in a catch block lands on its line and the stack names
// the frame. Without -g the output is unchanged.
//
// 1.30 (2026-10-23): three fixes GRIDE's Windows runtime needs. `p - n` with n an integer narrower
// than the pointer - an `unsigned`, and `unsigned long` on Windows - widens n before it negates it:
// on every 64-bit target p - n was p + (2^32 - n) (RTS6x's `text_ + sizeof text_ - length_`); the
// C6000's output is unchanged. In the MASM spelling a C name that is a MASM keyword - `fabs`, C90's
// `div`, `length` - is the symbol's own name, given up with `OPTION NOKEYWORD:<...>` as ml64 has it,
// where a definition had been renamed `$fabs`; in such a file a `div` instruction is written as its
// bytes. And -g with a dynamic initialiser names the `$LNend$` and `$LNprolog$` labels its code
// defines, so GRIDE's masm takes the file.
//
// 1.31 (2026-10-28): Phase 1 of the road to strict C++11 (the 1.30 verification, Fable 5.1), measured
// by tests\conformance: the silent wrong code, the crashes and the four most dangerous accepted
// programs. A mem-initialiser's temporaries die at the end of it, the base's and the delegated
// constructor's too; `d.L::v` reaches a virtual base's member through L's own subobject; `throw x`
// moves from a local, a parameter, an xvalue or a prvalue where [class.copy]/31-32 allow it; `new
// T[n]` with n below zero or past a size_t throws std::bad_array_new_length; `decltype(cs.m)` is the
// member's declared type, a reference member's included; an enumeration is spelt by its own name
// in a specialization's key, so `k<E>` is not `k<int>` and `Box<E>` not `Box<int>`; a member
// initialiser in a class with no constructor reads `this` of the object being built; partial
// specializations are ordered with their packs, `A<T, U, Ts...>` against `A<T, T, Ts...>` no longer
// crashing; the register allocator demotes every node it cannot colour once a function has taken
// sixteen rounds, so a 3000-term expression compiles at -O2 in seconds, the colouring otherwise as
// it was. Refused now: a static_cast casting const away, an assignment through a copy assignment
// [class.copy]/23 deletes, a conversion to an ambiguous base, an `explicit` conversion function used
// by a built-in operator. CPP11_PASS_TIMES prints each optimiser pass's time.
//
// 1.32 (2026-10-28): W13, and the first of Phase 2's C++98 holes. On x86_64-windows every vbtable
// pointer of the complete object is set - those inside its virtual bases too - a shared one's table
// begins with its base's order, and two subobjects of one class get tables of their own; a virtual
// base with virtual bases of its own no longer crashes. Compiled now: a pure virtual destructor; a
// parameter of function type; `char s[] = {"ab"}`; `static_cast<B &>(d)`; a pointer to member, and
// `bool b = p`, where a bool is wanted; a local class of a member function reading its class's
// private members; `: m{v}` and `: B{v}` mem-initialisers, without narrowing; an exception
// specification on a pointer to function, held to [except.spec]/5 and refused in a typedef; a
// function template's default argument naming its parameters, a class template's member's too.
//
// 1.33 (2026-10-29): Phase 2's template parameters and member function types. A non-type template
// parameter may be a pointer, a reference, a pointer to member or std::nullptr_t, its type built on an
// earlier parameter's or not - `template <int *P>`, `template <void (&F)()>`, `template <int S::*PM>`,
// `template <class T, T *P>` - and its argument `&gv`, an array or a function named bare, `&S::m`, `&S::f`
// or a null pointer of its type, every use of the parameter rebuilt in the function that reads it; both
// ABIs name the specialization as cl and clang do, `??$deref@$1?gv@@3HA@@YAHXZ`, `_Z5derefIXadL_Z2gvEEEiv`.
// A floating or class type for one is refused where it is written. `int (S::*)(int) const` is a type of
// its own, `P8S@@EBAHH@Z` and `M1SKFiiE`: `&S::f` of a const f has it, a non-const pointer refuses it, and
// it is the only kind a const object is called through; an alias declaration reads one. `0` and `nullptr`
// reach a pointer to member as a call's argument.
//
// 1.34 (2026-10-29): `volatile` in the type system. A qualifier beside const on every type, `this`
// included: `volatile int *` is not `int *` and neither converts to the other's pointee without a cast,
// a reference to a non-volatile type binds no volatile object, `f() volatile` is a member of its own,
// overloads and deduction keep the qualifier (`const T &` given a `const volatile int` deduces T as
// `volatile int`), a `const volatile` object is no constant, and the class a compiler writes the copy
// constructor of is not copied from a volatile one. cl's and clang's names - `?g@@3HC`, `PECH`,
// `?h@S@@QECAHXZ`, `PVKi`, `_ZNVK1S1kEv` - and <type_traits>' is_volatile, remove_volatile, add_cv; a
// file that writes it is still compiled without -O, the optimiser's passes not yet asking it. And
// R5, R8, R11, R15, R22, R26 of the plan's Phase 2: the surrogate call through a conversion to a
// function pointer, default arguments added over declarations, `using N::f; using M::f;` as one
// overload set - `auto p = std::strlen;` with it - `M C::*` deduced, `T &&` given a string literal,
// `f(std::string &&)` chosen for `f("lit")`, `auto &rf = fn;`, a braced list bound to a reference to
// an array, an aggregate's reference members, and `alignas(T)` in a class template's head. Merged in
// with them: the preprocessor as translation phases 1-4 over pp-tokens, with hide sets and `#if` on
// intmax_t (Phase 3); on x86_64-windows a handler's own try, cleanups and rethrow as funclets inside
// its funclet, function-try-blocks, `dynamic_cast<T &>`, and two or more polymorphic bases with a table
// each, cl's adjustors and RTTI (Phase 6); and the library's C names, `abs` in its argument's type, and
// the manipulators on either stream (Phase 7a). With the last: C++ overloads beside the one function of a
// name with C linkage ([dcl.link]/6), so <stdlib.h>'s `abs(int)` keeps C's linkage and the TI and
// Windows runtimes that define it build again, and a redeclaration that changes C++ linkage to C is
// refused ([dcl.link]/5).
//
// 1.35 (2026-10-29): Phase 2's R6, R16, R20, R28. A conversion function template is deduced from the
// type a conversion asks for ([temp.deduct.conv]) - `template <class T> operator T *()` at `double *dp =
// s;` - and loses a tie to a function that is not one; a constexpr function template is made where a
// constant needs it (`Arr<sq(3)>`, `int a[sq(3)]`); `noexcept(e)` on a template is folded with its
// arguments; `new T[n]{a, b}` builds the listed elements where they live and destroys them if a later
// one throws, `new auto(e)` and `new T[n][3]` beside it. A template has no C linkage ([temp]/4),
// `extern "C++"` inside `extern "C"` is C++, `operator new` returns `void *`. Merged in with them: on
// x86_64-windows a polymorphic virtual base, its overriders entered at the base, and cl's vtordisp
// with its thunks (Phase 6); and the streams' iostate with a sentry, the unformatted and positioning
// members, and <string> to C++11 - char_traits, u16string, u32string, hash, every to_string (Phase 7a).
//
// 1.36 (2026-10-29): Phase 2's R7, R10, R12, R27. A member function or static data member of one
// specialization written out, `template <> int S<int>::f()` and `template <> int S<int>::s = 2;`, in
// place of the template's; explicit specializations of a deleted function template, whose own
// specializations are deleted; `template <class U> friend class H;` and friend function templates, and
// member alias templates, `S::M<int>`; a placement `new` whose constructor throws frees through the
// matching placement `operator delete` with the same arguments, `delete` takes the usual one beside it,
// and `::new` and `::delete` pass a class's own over. Merged in with them: on x86_64-windows thread_local,
// a noexcept function that ends the program when an exception leaves it, and `throw(T...)` honoured
// through unexpected() (Phase 6).
//
// 1.37 (2026-10-29): Phase 2's R21, its ranking half. A braced argument among overloads that take
// different types there is made for the one it converts to best ([over.ics.list], [over.ics.rank]/3):
// an initializer_list<X> before anything else, then the worse of its elements' conversions - `f({1})`
// takes f(initializer_list<int>) over f(int), `k({"s"})` takes `const char *` over std::string - and
// two that take it equally well are refused by name.
//
// 1.38 (2026-10-29): what Phase 2's R-list left open. `::g` and `::f()` are looked up in the global
// namespace alone ([basic.lookup.qual]/4); a C function declared in a namespace is reached through it
// ([dcl.link]/6); a literal class where an integral constant is wanted goes through its conversion
// function ([expr.const]/5), `P()` of a class with no constructor folds to zeroes, and `constexpr
// explicit` is read in either order; an overloaded name in a braced list is the one its element's type
// names ([over.over]/1).
//
// 1.39 (2026-10-29): Phase 2's R21, its other half. An initializer_list of class objects made inside an
// expression - `count({T(1), T(2)})` - has its elements as temporaries of the full expression, built
// in place and destroyed at its end, last first, or as far as they were built when one throws; an
// element made from a prvalue of its own class or through a converting constructor is constructed
// where it lives, the copy cl and clang leave out, in a declaration too.
//
// 1.40 (2026-10-29): a file that writes `volatile` keeps its -O on x86_64: each function that reads or
// writes a volatile lvalue is written past the optimizer and walks no callee in place, nor is it walked in
// another, and the rest of the file is optimized as before - the C6000 and arm64 still compile such a file
// without -O. Itanium names carry clang 17's `Tn<type>` before a non-type argument whose parameter's type
// is another parameter's, `_Z1fIiTnT_Li3EEiv`.
//
// 1.41 (2026-10-29): Phase 2's R7, its member templates ([temp.expl.spec]/16). `template <> template <>
// int S<int>::g(char)` is S<int>'s member template's specialization at char, found by every later use
// that deduces it, and `template <> template <class U> int S<char>::g(U)` defines S<char>'s anew; a
// member class of one specialization is still refused.
//
// 1.42 (2026-10-29): deduction from a braced list ([temp.deduct.call]/1, CWG 1591): `il({1, 2})` deduces
// initializer_list<T>'s T from the elements, `cnt({1, 2, 3})` a `T (&&)[N]`'s T and N, and any other
// parameter takes the list as a non-deduced context; the list is then made for the specialization's
// parameter, an array reference's as a temporary of the call.
//
// 1.43 (2026-10-30): Phase 4 begins. The type-trait built-ins clang and cl share - __is_class, __is_enum,
// __is_pod, __is_trivially_copyable, __is_constructible(T, Args...), __is_convertible_to, __is_assignable,
// __has_virtual_destructor and the rest, and __underlying_type(E) - which <type_traits> is written over.
// Names in a template are bound where it is written ([temp.res]/10): an unqualified call no longer sees an
// overload declared after the template, argument-dependent lookup still does, and a base that depends on a
// template parameter is left out of unqualified lookup ([temp.dep]/3). Phase 7a's <algorithm>, <numeric>
// and <limits>.
//
// 1.44 (2026-10-30): expressions over a template's parameters in a function template's signature - a
// `sizeof(T)` array bound, a qualified call, `std::declval<T>()`, a type trait - read as patterns and
// spelled as clang spells them; a function template only declared is a candidate; trailing template
// arguments a call does not write are deduced; explicit instantiation; a string literal bound to a reference
// to an array where it lies. Phase 5's first four batches (classes, initialisation, statements, unions,
// loops, friends, required diagnostics) and Phase 7a's <memory>, <utility> and <exception>.
//
// 1.45 (2026-10-30): member function templates defined outside their class, or only declared (the sizeof
// idiom), chosen among every member template of the name with substitution failure; partial specializations
// on an array's bound and on function types, a pack among the parameters, and a function type as a template
// argument (what std::function is written over); the [temp.mem] and [temp.names]/4 rules on member templates.
// Phase 5's batches 5-6 (access, constant expressions, template declarations) and Phase 7a's containers and
// <type_traits> over the built-ins.
//
// 1.46 (2026-10-30): member class templates (allocator::rebind) and member alias templates of class
// templates, named bare, qualified, through a base and after 'template'; a member class of a class
// template specialization declared and defined only where needed ([temp.inst]/1), so an explicit
// specialization of it may follow the class's first use. Phase 5's range-for over a temporary's member
// and Phase 7a's get<I> for pair and array, result_of and <type_traits> on 1.45's patterns.
//
// 1.47 (2026-11-02): packs expanded in base lists, mem-initializer lists, lambda captures, alignas, a call
// in a trailing return type and a template's fixed parameters; a function type's trailing pack deduced
// ([temp.deduct.type]/10); Tup<H, T...> in a partial specialization's arguments; member templates of a
// variadic class template; a function template named without a call ([over.over]/2); static auto members.
//
// 1.48 (2026-11-02): the wrong code the library found - a throwing constructor in placement new freed the
// storage, assignment through an empty base overwrote a member, a template in any namespace beat the named
// non-template - with argument-dependent lookup through bases and template arguments, implicit members'
// noexcept, typename-qualified non-type parameters, unnamed classes as template arguments; Phase 5's access
// to private member types and enumerators, friend-only functions, constexpr over arrays and addresses.
//
// 1.49 (2026-11-02): templates the program never instantiates are read once with stand-in arguments, their
// own errors said ([temp.res]/8-10) - Phase 4 finished; the library's compiler items: operator templates only
// where lookup reaches them, explicit and converting constructor templates, bodies of unchosen candidates
// left unsaid, ::operator new by name, initializer-list constructors with defaults, braced assignments.
//
// 1.50 (2026-11-02): Phase 7b begins - <functional> (function, bind, mem_fn, hash), <tuple>, <iterator> and
// <string>'s iterator ranges, with what they met: deduction from pointers to member functions, assignment to
// class prvalues, specializations only where deduced, copy-initialisation without explicit constructors,
// member templates ranked, pointers to members through template-ids, and class-to-scalar casts refused.
// 1.51 (2026-11-03): Phase 7b's containers - deque, list, forward_list, stack, queue, the unordered maps and
// sets, bitset, vector<bool> - with vector and basic_string<charT, traits, Allocator> going through
// allocator_traits, tuple's uses-allocator constructors and pair's piecewise one; and what they met: wrong
// code where `return os << s.str();` returned a copy's address and `B(std::move(d))` moved a copy, a trait
// asked inside a function clearing that function's slots, ADL through specializations, member templates'
// namespaces and partial ordering with packs, `void()`, and C++11's converting constructors with defaults.
// 1.52 (2026-11-03): Phase 7b finished - list, forward_list, deque and the unordered containers take their
// storage from the allocator, rebound, and build elements through allocator_traits; map, multimap, set and
// multiset keep an allocator and emplace in place, map::operator[] building its element piecewise; and what
// they met: template template parameters passed on and matched with packs, empty pack expansions, a
// template's own parameters hiding a template of their name, member defaults read in their class, trait
// trials leaving functions marked used, and packs lost across a nested inline body.
// 1.53 (2026-11-03): Phase 7c part 1 - <ratio>, <chrono>, <system_error> (ios_base::failure one), <cerrno>
// with all of [errno]'s names, <typeindex>, <complex>, <valarray>, <random> with the standard's engine
// values, <cmath>'s float, long double and integer overloads; and what they met: wrong code where `: m()`
// skipped a default constructor taking defaults, block typedefs that were global, short-circuit constants,
// constexpr bodies with typedefs, decltype as a nested name, and bound parameters in member templates.
// 1.54 (2026-11-03): Phase 7e - <atomic>, <mutex>, <condition_variable>, <thread> and <future> over the
// operating system's lock, condition variable and thread (atomics through one lock: the compiler emits no
// interlocked instruction) - and Phase 7d: tms6747 is a freestanding implementation, __STDC_HOSTED__ 0; the
// implicit object parameter ranked by its added qualifiers; nested #include to Annex B's 256.
// 1.55 (2026-11-03): the iostreams as the standard's templates - basic_streambuf, basic_ios, basic_istream,
// basic_ostream and the string and file streams over it, wide streams included, std::endl and its kin
// templates, one cout per program. Compiler: an overloaded name right of << and >> resolved by the member
// operator's parameter; a typedef of a specialization declared before its template's definition; a pointer
// to a specialization put off while its base is made; earlier declarations' defaults under their own names;
// a written template argument matched exactly in deduction; a base of a virtual base reached through the
// table (wrong code); template-id vbtable names.
// 1.56 (2026-11-03): <strstream> ([depr.str.strstreams]); a body compiled in the middle of another function -
// a constructor template made for an initialiser - keeps its entry-zeroed flags and its statements'
// temporaries in its own frame (wrong code: a std::function made in a variadic template wrote a parameter).
// 1.57 (2026-11-03): <locale> and <codecvt> - the locale with every standard facet, a stream's numbers through
// its num_put and num_get, imbue and getloc, wstring_convert and wbuffer_convert, codecvt_utf8/utf16; the
// time and money manipulators through their facets. Compiler: operator member templates defined outside a
// class; a protected base destructor in an implicit one; a default template argument read in its own
// namespace; a conditional's same-typed arms keep their type; escapes in wide literals are code units;
// <stdlib.h>'s MB_CUR_MAX.
// 1.58 (2026-11-04): <regex> - the six grammars on a backtracking machine, leftmost-longest for POSIX's, with
// regex_traits, sub_match, match_results, regex_match, regex_search, regex_replace and both iterators. Compiler:
// k::name in an expression is k's, not a class template of the namespace around it; a function template's
// default arguments are read in its namespace; a member's definition no longer reads its table entry after
// the body has grown the table (a crash).
// 1.59 (2026-11-04): a destructor that throws at the end of its scope runs once and the objects still alive are
// destroyed; a destructor with no specification is non-throwing, [except.spec]/14; a throw in a noexcept function
// calls std::terminate, not abort; noexcept(pf()) reads a pointer's own noexcept; throw of a prvalue builds the
// exception object in place. Language: pointer to member to bool and the boolean conversion's rank, friend T,
// function-type parameters, decltype of a function, T(4); as a temporary, lists of initializer_lists of two,
// overloaded function arguments decided by the other arguments, the lvalue/rvalue reference tie-break of
// [temp.deduct.partial]/9, packs of template template parameters, inherited constructor templates.
// 1.60 (2026-11-04): <scoped_allocator>. A throwing destructor leaves its object's other members, bases and array
// elements to be destroyed; a return in a destructor goes on to them; an initialiser's temporaries and object are
// destroyed under a throw. Friends defined in a namespace's class template are defined; noexcept on friends, data
// members and parameters; d.base::f() through a member typedef and a class's own private base by name; a trait no
// longer asks for a constructor template's body; tag dispatch rules a candidate out before its body; a reference
// binds a derived object with no constructor asked; an empty base is not copied over what follows it on the
// Microsoft layout. Census complete.
// 1.61 (2026-11-04): one name, one template per namespace - class templates, alias templates and function templates
// of a name in several namespaces, a program's own `vector` beside the library's. Template names are looked up:
// `vector<int>` with nothing bringing it here and `k::Box<int>` for a Box k has not are refused, two using-directives
// finding two templates are ambiguous; using-declarations of class templates; `::X<int>` and `::g(1)`. Release links
// by docs/Build_Policy.md: 7.45 MB to 4.73 MB.
#define CXX1_VERSION      "1.61"
#define CXX1_SEAL_DATE    "04-11-2026"
#define CXX1_SEAL_FILE    "cxx1-1.61.dat"

// The line every run prints first; what it claims is docs/EXCLUSIONS.md's to measure (review of 2026-10-08).
#define CXX1_BANNER "©2026 G. R. Akhtar - C++03 with C++11 extensions"

#endif
