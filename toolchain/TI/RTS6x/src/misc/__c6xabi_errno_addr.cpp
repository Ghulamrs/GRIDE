// Spec: SPRAB89B 9.5 - __c6xabi_errno_addr, the address errno names.

#include "ErrorNumber.h"

extern "C" int *__c6xabi_errno_addr(void)
{
    return rts6x::ErrorNumber::address();
}
