// Spec: ISO C++11 18.8.3 (terminate, set_terminate: abort the default), D.11 (unexpected,
// set_unexpected: terminate the default) and 18.8.4 (uncaught_exception).
#ifndef RTS6X_HANDLERS_H
#define RTS6X_HANDLERS_H

namespace std {
typedef void (*terminate_handler)();
typedef void (*unexpected_handler)();
terminate_handler set_terminate(terminate_handler handler) throw();
terminate_handler get_terminate() throw();
void terminate();
unexpected_handler set_unexpected(unexpected_handler handler) throw();
unexpected_handler get_unexpected() throw();
void unexpected();
bool uncaught_exception() throw();
}

namespace rts6x {

class Handlers {
public:
    static std::terminate_handler exchangeTerminate(std::terminate_handler next);
    static std::unexpected_handler exchangeUnexpected(std::unexpected_handler next);
    static std::terminate_handler terminating() { return terminate_; }
    static std::unexpected_handler unexpecting() { return unexpected_; }
    // The handler, then the end of the program as abort ends it, should the handler return.
    static void terminate();
    static void unexpected();

private:
    // Zero-filled at start: the defaults.
    static std::terminate_handler terminate_;
    static std::unexpected_handler unexpected_;
};

}  // namespace rts6x

#endif
