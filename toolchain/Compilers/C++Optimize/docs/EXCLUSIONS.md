# cpp11: what it does not yet do of ISO C++11

The banner says what cpp11 claims; this file says where the claim stops. Each item names the clause it
falls short of. The conformance suite (`tests/conformance`) and the library census (`tests/conformance/library`)
measure the rest.

What is implementation-defined - the locale names, the character sets, the freestanding C6747's headers,
the limits - is in IMPLEMENTATION-DEFINED.md, not here.

## Library

- **On the C6747, whose runtime is RTS6x**, three of what [compliance]/3 asks of a freestanding implementation
  are missing: `<cstdlib>`'s `_Exit`, `quick_exit` and `at_quick_exit` (RTS6x has none, a program naming them
  does not link); `<exception>`'s `exception_ptr`, `current_exception` and `rethrow_exception` (RTS6x's
  exception objects carry no reference count); and `<typeinfo>`'s `bad_cast` and `bad_typeid` are not thrown,
  since RTS6x has no `__cxa_bad_cast` or `__cxa_bad_typeid` - a failed `dynamic_cast<T &>` does not link, and
  `typeid(*p)` with a null `p` reads address 0 instead ([expr.typeid]/2).
- **On the C6747, a `throw(T...)` list that is violated does not reach `std::unexpected`** and a `noexcept`
  that is violated does not reach the program's `terminate` handler ([except.unexpected], [except.terminate]):
  RTS6x's personality routine ends the program itself. On x86_64-windows both are met; x86_64-linux and
  arm64-darwin have not been run.
- **On the C6747, an exception leaving a handler does not destroy the handler's remaining locals** before the
  catch ends (eh05, eh09 of the suite; [except.handle]/16): measured on the simulator, not yet isolated.
- **A pointer caught by reference on the Itanium targets refers to a copy** of the pointer the runtime hands
  back, not to the exception object ([except.handle]/17): `catch (int *&p) { p = 0; throw; }` rethrows the
  original. The Microsoft runtime hands over the object itself.
- **On the C6747 a `thread_local` object has static storage** (IMPLEMENTATION-DEFINED.md), so its address is
  taken as a template argument there, which [temp.arg.nontype]/1 does not allow (r1n10 of the suite).

## Language

- **On x86_64-linux, a `long double` literal a `double` cannot hold exactly is refused** ([lex.fcon]): `0.1L`,
  `LDBL_MAX`. The compiler carries a floating constant in a double, so that every build machine reads the same
  number, and x87's 64-bit significand holds more than that.
- **A template name out of reach inside a template's own definition** is not refused, since a template's text is
  read when it is instantiated ([temp.res]); outside a template it is.
- **Itanium's `Tn` mangling prefix** for a function template whose parameter type depends on another is not
  produced (clang 17's mangling; [temp.over.link] is met, the names differ from clang's).
