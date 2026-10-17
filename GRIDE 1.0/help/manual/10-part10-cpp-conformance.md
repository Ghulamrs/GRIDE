# Part X — C++11 in cpp11, feature by feature

`cpp11` accepts C++11 minus a documented list (Part II chapter 8). This part goes
through the language a feature at a time, each with a one-line probe you can
compile, and says **accepted** or **refused** — and for a refusal, what to do
instead. The authoritative, source-derived inventory is the C++ compiler's own
`docs/EXCLUSIONS.md`; this is the readable companion. When in doubt about a single
construct, compile the probe: a refusal reads more broadly than it fires.

--------------------------------------------------------------------------------
## 47. Types, declarations, expressions

- **`auto` for a variable** — accepted. `auto x = 3; auto y = f();`
- **`decltype`** — accepted in the ordinary forms.
- **`nullptr` and `std::nullptr_t`** — accepted.
- **`enum class` and an enum base** — accepted. `enum class C : unsigned { A };`
- **`alignas` / `alignof`** — accepted, on a class, a member and an object.
- **`static_assert`** — accepted. `static_assert(sizeof(int) == 4, "");`
- **`constexpr`** — accepted in the C++11 forms the compiler implements; the
  C++14 relaxations are refused *naming the version*.
- **A trailing return type** `auto f(int) -> int` — **refused** (documented
  exclusion); write the ordinary `int f(int)`.
- **`volatile`** — parsed and then **dropped**: it does not change codegen. Do
  not use it for memory-mapped I/O.

--------------------------------------------------------------------------------
## 48. Classes, inheritance, special members

- **Data and function members, access control, `this`, nested classes,
  `friend`** — accepted.
- **Constructors** (default, parameterised, copy; move where the shape is
  simple), **member initialiser lists**, **destructors** — accepted.
- **Single and multiple inheritance; virtual functions; pure-virtual and abstract
  classes** — accepted. Dispatch is through the vtable.
- **Virtual (shared) base classes** — accepted on the three Itanium targets, with
  the real Itanium machinery (secondary vtables, the VTT, `_ZTv` virtual thunks,
  construction vtables, C2/D2 constructors taking a VTT). On `x86_64-windows` a
  polymorphic virtual base is refused **by name** (`.notarget`).
- **`= default` / `= delete`** — accepted in the forms the compiler implements;
  where a defaulted/deleted member would link-but-never-be-callable it is refused
  at the declaration, by name.

--------------------------------------------------------------------------------
## 49. Templates

- **Class and function templates** — accepted.
- **Explicit specialisation** — accepted: `template <> struct Box<int> {…};`
- **Partial specialisation** — accepted.
- **Non-type template parameters** — accepted.
- **A member *function* template** — accepted.
- **A member *class* template** — **refused**; lift it to a namespace-scope
  template.
- **`template <>` where a template parameter list is expected** — refused (but an
  explicit specialisation `template <> struct …` compiles; the refusal is
  narrower than it reads).

--------------------------------------------------------------------------------
## 50. Exceptions

- **`throw` / `try` / `catch`** — accepted; unwinding runs destructors.
- **catch by value** (copying from `__cxa_begin_catch`), **by reference**, a
  **thrown pointer**, **rethrow** — accepted, on the three Itanium targets.
- **`noexcept` / `throw()`** — accepted; an exception escaping one calls
  `std::terminate`.
- **A destructor that throws while an exception unwinds** — terminates, as the
  standard requires, on the Itanium targets.
- Some exception shapes are refused on `x86_64-windows` (the Microsoft ABI path
  is narrower); they are refused by name with a `.notarget`.

--------------------------------------------------------------------------------
## 51. RTTI

- **`typeid`, static and dynamic; `type_info`** — accepted.
- **`dynamic_cast` to a pointer** along a single-base chain, including
  **`dynamic_cast<void*>`** (the most-derived-object form) — accepted, and it
  answers null on a failed cast.
- **`dynamic_cast` to a reference** — **refused** (it has no null to return); use
  the pointer form and test for null.
- **`dynamic_cast` naming a class with more than one base** — **refused** (it
  needs `__vmi_class_type_info`, not emitted).

--------------------------------------------------------------------------------
## 52. new / delete, lambdas, and the library

- **`operator new` / `operator delete`** — the replaceable global forms,
  class-specific forms, and placement new — all accepted and dispatched
  correctly (Part IX chapter 40).
- **`new[]` / `delete[]`** with an array cookie — accepted; arrays of a class
  with a destructor (local, `new[]`/`delete[]`, static storage) run their
  destructors.
- **Lambdas** — accepted.
- **The library** in `include/`: `<cstddef>`, `<cstdlib>`, `<cstring>`,
  `<cmath>`, `<cctype>` (C headers wrapped into `std`); `<string>` (a class);
  `<utility>`, `<vector>`, `<map>`, `<set>`, `<algorithm>` (a vector is a growing
  array, a map a sorted vector of pairs, a set a sorted vector, iterators are
  pointers); and the streams `<iostream>`/`<ostream>`/`<istream>`/`<sstream>`/
  `<fstream>`/`<ios>`/`<cstdio>` with `std::cout`/`cin`/`cerr` as file-scope
  objects. It is a working subset, not a fully conforming standard library.

--------------------------------------------------------------------------------
## 53. The exclusions, gathered

If a program fails, check it against this and the compiler's `docs/EXCLUSIONS.md`;
every refusal is by name:

- a trailing return type; a member class template;
- `dynamic_cast` to a reference, or naming a class with more than one base;
- objects that would run code before `main` in the dynamic-initialisation forms
  the compiler does not do;
- C++14 and C++17 syntax (refused *naming the version*);
- eight keywords the parser has no rule for yet (refused as "not supported yet");
- `volatile` semantics (accepted then dropped);
- a small set of `x86_64-windows`-only refusals (polymorphic virtual base, some
  exception shapes).

The working rule: **cpp11 is a large, usable C++11 subset; it never miscompiles a
construct it does not support — it refuses it, by name, with a line.** A program
that compiles has stepped on none of the above.
