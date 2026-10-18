// Spec: Itanium C++ ABI 3.2.6 - __cxa_deleted_virtual fills a deleted virtual function's slot.

extern "C" void __c6xabi_abort_msg(const char *message);

extern "C" void __cxa_deleted_virtual(void)
{
    __c6xabi_abort_msg("deleted virtual function called\n");
}
