// Spec: ISO C++11 18.6.1 [new.delete] (the replaceable allocation functions: the new handler called
// until one succeeds, a null pointer from the nothrow forms) and 18.6.2.3 (set_new_handler,
// get_new_handler); SPRAB89B 10 (size_t is unsigned int, so operator new is _Znwj).
#ifndef RTS6X_ALLOCATION_H
#define RTS6X_ALLOCATION_H

#include <stddef.h>

namespace std {
struct nothrow_t {};
extern const nothrow_t nothrow;
typedef void (*new_handler)();
new_handler set_new_handler(new_handler handler) throw();
new_handler get_new_handler() throw();
}

namespace rts6x {

class Allocation {
public:
    // size bytes (at least one), the new handler run while there are none; null once there is none.
    static void *tryAllocate(size_t size);
    // The same, or std::bad_alloc thrown.
    static void *allocate(size_t size);

    static std::new_handler handler() { return handler_; }
    static std::new_handler exchange(std::new_handler next);

private:
    // Zero-filled at start: no handler.
    static std::new_handler handler_;
};

}  // namespace rts6x

#endif
