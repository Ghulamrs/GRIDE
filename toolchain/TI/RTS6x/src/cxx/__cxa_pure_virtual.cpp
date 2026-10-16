// Spec: Itanium C++ ABI 3.2.6 - __cxa_pure_virtual fills a pure virtual function's slot; reaching it
// is undefined ([class.abstract]/6), and here it ends the program with a message.

extern "C" void __c6xabi_abort_msg(const char *message);

extern "C" void __cxa_pure_virtual(void)
{
    __c6xabi_abort_msg("pure virtual function called\n");
}
