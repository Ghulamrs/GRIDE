// Spec: SPRAB89B 9.5 - errno is *__c6xabi_errno_addr(); ISO C 7.5/3 - zero at program start.
// One int for the program, the C6747 running one thread.
#ifndef RTS6X_ERROR_NUMBER_H
#define RTS6X_ERROR_NUMBER_H

namespace rts6x {

class ErrorNumber {
public:
    static int *address() { return &value_; }
    static void set(int code) { value_ = code; }

private:
    static int value_;
};

}  // namespace rts6x

#endif
