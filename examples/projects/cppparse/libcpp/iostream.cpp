// iostream.cpp - cppparse's C++98 library: the standard stream objects, the global allocation functions and the streams'
// calls of the num facets for char and wchar_t.
// Compiled by cppparse (--compile6 -I ../include) and linked with every program that uses <iostream>.
#include <iostream>
#include <new>
#include <cstdlib>

// the global allocation functions (18.4.1): a new-expression calls them by their MSVC names (??2@YAPEAX_K@Z and
// so on), so a program that links this unit gets these instead of the C runtime's, whose failure is reported
// through MSVC's exception handling, which no cppparse frame can catch; these throw std::bad_alloc through
// cppparse's runtime. A program may still define its own (18.4.1.1/2) in a unit of its own; there is no new_handler.
void* operator new(size_t n)
{
    void* p = malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](size_t n)
{
    void* p = malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) { free(p); }
void operator delete[](void* p) { free(p); }

namespace std {

// the buffers over the C runtime's FILEs, then the streams over them (27.3): constructed in this order before main,
// destroyed after it in the reverse order, so the streams go before their buffers
static filebuf  __stdin_buf(stdin);
static filebuf  __stdout_buf(stdout);
static filebuf  __stderr_buf(stderr);
static wfilebuf __wstdin_buf(stdin);
static wfilebuf __wstdout_buf(stdout);
static wfilebuf __wstderr_buf(stderr);

istream  cin(&__stdin_buf);
ostream  cout(&__stdout_buf);
ostream  cerr(&__stderr_buf);
ostream  clog(&__stderr_buf);
wistream wcin(&__wstdin_buf);
wostream wcout(&__wstdout_buf);
wostream wcerr(&__wstderr_buf);
wostream wclog(&__wstderr_buf);

// cin and cerr are tied to cout, wcin and wcerr to wcout; cerr and wcerr flush after each output (27.3.1, 27.3.2)
struct __init_streams {
    __init_streams()
    {
        cin.tie(&cout);
        cerr.tie(&cout);
        cerr.setf(ios_base::unitbuf);
        wcin.tie(&wcout);
        wcerr.tie(&wcout);
        wcerr.setf(ios_base::unitbuf);
    }
};
static __init_streams __init_streams_now;

// the streams' calls of the num facets for char and wchar_t (declared in <ostream> and <istream>): the facets' code is
// compiled here once, not in every unit that writes or reads a number
bool __num_put_out(basic_ostream<char, char_traits<char> >& os, const locale::facet* f, char fill, bool v) { return __num_put_call(os, f, fill, v); }
bool __num_put_out(basic_ostream<char, char_traits<char> >& os, const locale::facet* f, char fill, long v) { return __num_put_call(os, f, fill, v); }
bool __num_put_out(basic_ostream<char, char_traits<char> >& os, const locale::facet* f, char fill, unsigned long v) { return __num_put_call(os, f, fill, v); }
bool __num_put_out(basic_ostream<char, char_traits<char> >& os, const locale::facet* f, char fill, long long v) { return __num_put_call(os, f, fill, v); }
bool __num_put_out(basic_ostream<char, char_traits<char> >& os, const locale::facet* f, char fill, unsigned long long v) { return __num_put_call(os, f, fill, v); }
bool __num_put_out(basic_ostream<char, char_traits<char> >& os, const locale::facet* f, char fill, double v) { return __num_put_call(os, f, fill, v); }
bool __num_put_out(basic_ostream<char, char_traits<char> >& os, const locale::facet* f, char fill, long double v) { return __num_put_call(os, f, fill, v); }
bool __num_put_out(basic_ostream<char, char_traits<char> >& os, const locale::facet* f, char fill, const void* v) { return __num_put_call(os, f, fill, v); }
bool __num_put_out(basic_ostream<wchar_t, char_traits<wchar_t> >& os, const locale::facet* f, wchar_t fill, bool v) { return __num_put_call(os, f, fill, v); }
bool __num_put_out(basic_ostream<wchar_t, char_traits<wchar_t> >& os, const locale::facet* f, wchar_t fill, long v) { return __num_put_call(os, f, fill, v); }
bool __num_put_out(basic_ostream<wchar_t, char_traits<wchar_t> >& os, const locale::facet* f, wchar_t fill, unsigned long v) { return __num_put_call(os, f, fill, v); }
bool __num_put_out(basic_ostream<wchar_t, char_traits<wchar_t> >& os, const locale::facet* f, wchar_t fill, long long v) { return __num_put_call(os, f, fill, v); }
bool __num_put_out(basic_ostream<wchar_t, char_traits<wchar_t> >& os, const locale::facet* f, wchar_t fill, unsigned long long v) { return __num_put_call(os, f, fill, v); }
bool __num_put_out(basic_ostream<wchar_t, char_traits<wchar_t> >& os, const locale::facet* f, wchar_t fill, double v) { return __num_put_call(os, f, fill, v); }
bool __num_put_out(basic_ostream<wchar_t, char_traits<wchar_t> >& os, const locale::facet* f, wchar_t fill, long double v) { return __num_put_call(os, f, fill, v); }
bool __num_put_out(basic_ostream<wchar_t, char_traits<wchar_t> >& os, const locale::facet* f, wchar_t fill, const void* v) { return __num_put_call(os, f, fill, v); }
void __num_get_in(basic_istream<char, char_traits<char> >& is, ios_base::iostate& err, bool& v) { __num_get_call(is, err, v); }
void __num_get_in(basic_istream<char, char_traits<char> >& is, ios_base::iostate& err, long& v) { __num_get_call(is, err, v); }
void __num_get_in(basic_istream<char, char_traits<char> >& is, ios_base::iostate& err, unsigned short& v) { __num_get_call(is, err, v); }
void __num_get_in(basic_istream<char, char_traits<char> >& is, ios_base::iostate& err, unsigned int& v) { __num_get_call(is, err, v); }
void __num_get_in(basic_istream<char, char_traits<char> >& is, ios_base::iostate& err, unsigned long& v) { __num_get_call(is, err, v); }
void __num_get_in(basic_istream<char, char_traits<char> >& is, ios_base::iostate& err, long long& v) { __num_get_call(is, err, v); }
void __num_get_in(basic_istream<char, char_traits<char> >& is, ios_base::iostate& err, unsigned long long& v) { __num_get_call(is, err, v); }
void __num_get_in(basic_istream<char, char_traits<char> >& is, ios_base::iostate& err, float& v) { __num_get_call(is, err, v); }
void __num_get_in(basic_istream<char, char_traits<char> >& is, ios_base::iostate& err, double& v) { __num_get_call(is, err, v); }
void __num_get_in(basic_istream<char, char_traits<char> >& is, ios_base::iostate& err, long double& v) { __num_get_call(is, err, v); }
void __num_get_in(basic_istream<char, char_traits<char> >& is, ios_base::iostate& err, void*& v) { __num_get_call(is, err, v); }
void __num_get_in(basic_istream<wchar_t, char_traits<wchar_t> >& is, ios_base::iostate& err, bool& v) { __num_get_call(is, err, v); }
void __num_get_in(basic_istream<wchar_t, char_traits<wchar_t> >& is, ios_base::iostate& err, long& v) { __num_get_call(is, err, v); }
void __num_get_in(basic_istream<wchar_t, char_traits<wchar_t> >& is, ios_base::iostate& err, unsigned short& v) { __num_get_call(is, err, v); }
void __num_get_in(basic_istream<wchar_t, char_traits<wchar_t> >& is, ios_base::iostate& err, unsigned int& v) { __num_get_call(is, err, v); }
void __num_get_in(basic_istream<wchar_t, char_traits<wchar_t> >& is, ios_base::iostate& err, unsigned long& v) { __num_get_call(is, err, v); }
void __num_get_in(basic_istream<wchar_t, char_traits<wchar_t> >& is, ios_base::iostate& err, long long& v) { __num_get_call(is, err, v); }
void __num_get_in(basic_istream<wchar_t, char_traits<wchar_t> >& is, ios_base::iostate& err, unsigned long long& v) { __num_get_call(is, err, v); }
void __num_get_in(basic_istream<wchar_t, char_traits<wchar_t> >& is, ios_base::iostate& err, float& v) { __num_get_call(is, err, v); }
void __num_get_in(basic_istream<wchar_t, char_traits<wchar_t> >& is, ios_base::iostate& err, double& v) { __num_get_call(is, err, v); }
void __num_get_in(basic_istream<wchar_t, char_traits<wchar_t> >& is, ios_base::iostate& err, long double& v) { __num_get_call(is, err, v); }
void __num_get_in(basic_istream<wchar_t, char_traits<wchar_t> >& is, ios_base::iostate& err, void*& v) { __num_get_call(is, err, v); }

}
