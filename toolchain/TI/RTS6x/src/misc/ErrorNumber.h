// Spec: SPRAB89B 9.5 - errno is *__c6xabi_errno_addr(); ISO C 7.5/3 - zero at program start.
// One int for the program, the C6747 running one thread.
#ifndef RTS6X_ERROR_NUMBER_H
#define RTS6X_ERROR_NUMBER_H

#if defined(_WIN32)
// GRIDE's runtime for Windows (toolchain\Windows\RT): one errno for each thread, *_errno() as the Microsoft
// runtime's, kept in thread-local storage by its Windows layer (win\errno.cpp).
extern "C" int *_errno(void);
#endif

namespace rts6x {

class ErrorNumber {
public:
#if defined(_WIN32)
    static int *address() { return _errno(); }
    static void set(int code) { *_errno() = code; }
#else
    static int *address() { return &value_; }
    static void set(int code) { value_ = code; }

private:
    static int value_;
#endif
};

}  // namespace rts6x

#endif
