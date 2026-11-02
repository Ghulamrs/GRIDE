// badcast.cpp - what a failed dynamic_cast to a reference and a typeid of a null pointer's object throw (cxx\rtti.cpp):
// std::bad_cast and std::bad_typeid as cpp11's <typeinfo> declares them on Windows, in the Microsoft layout, so a
// program catches them by their names, with the Microsoft runtime's words from what().
#include <typeinfo>

namespace {

// what() answers the word after the vftable's pointer: __std_exception_data's what
template <class E>
void say(E &e, const char *words)
{
    *reinterpret_cast<const char **>(reinterpret_cast<char *>(&e) + sizeof(void *)) = words;
}

}  // namespace

extern "C" void __grt_throw_bad_cast()
{
    std::bad_cast e;
    say(e, "Bad dynamic_cast!");
    throw e;
}

extern "C" void __grt_throw_bad_typeid()
{
    std::bad_typeid e;
    say(e, "Attempted a typeid of nullptr pointer!");
    throw e;
}
