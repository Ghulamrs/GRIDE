# cpp11: what it does not yet do of ISO C++11

The banner says what cpp11 claims; this file says where the claim stops. Each item names the clause it
falls short of. The conformance suite (`tests/conformance`) and the library census (`tests/conformance/library`)
measure the rest.

## Library

- **"C" is the only named locale** ([locale.cons]): `locale("C")`, `locale("POSIX")` and `locale("")` are it,
  any other name throws `runtime_error`, and so do the `_byname` facets'. A program's own facets are used as
  the standard's are; the "C" locale's are made the first time one is asked for.
- **A wide file stream converts by the C library's `wcrtomb` and `mbrtowc`** in the "C" locale
  ([filebuf]/5's codecvt is the locale's, and there is none).
- **`atomic_flag` is not lock-free** ([atomics.flag]/2): cpp11 emits no interlocked instruction, so every
  atomic operation takes one lock shared by the program. Each is indivisible and sequentially consistent;
  none is lock-free, and `is_lock_free()` and the `ATOMIC_*_LOCK_FREE` macros say so.
- **`<cmath>`'s `long double` overloads compute in `double`**, which is `long double` on Windows and the
  C6747 and narrower than it on x86-64 Linux.
- **The search functions of `<cstring>` and `<cwchar>` are C's** (`char *strchr(const char *, int)`), not
  [c.strings]/4's const-correct pairs.
- **`<cfenv>` and `<cuchar>`** are not provided for the C6747, whose runtime has neither; `<cuchar>` is not
  provided on Darwin, whose C library has no char16_t conversions.

## Language

- **A template name out of reach inside a template's own definition** is not refused, since a template's text is
  read when it is instantiated ([temp.res]); outside a template it is.
- **Itanium's `Tn` mangling prefix** for a function template whose parameter type depends on another is not
  produced (clang 17's mangling; [temp.over.link] is met, the names differ from clang's).
