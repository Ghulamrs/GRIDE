// Spec: Itanium C++ ABI 3.3.2 (__cxa_guard_acquire, _release, _abort) as SPRAB89B 10 fixes the
// guard for the C6000: a 32-bit word whose first byte, nonzero, says the object is built. The
// C6747 runs one thread, so the second byte only marks an initialisation that is in progress.
#ifndef RTS6X_GUARD_H
#define RTS6X_GUARD_H

namespace rts6x {

class Guard {
public:
    explicit Guard(int *word) : bytes_(reinterpret_cast<unsigned char *>(word)) {}

    // 1 if the caller is to build the object, 0 if it is built; a recursive initialisation, which
    // [stmt.dcl]/4 leaves undefined, ends the program.
    int acquire();
    void release() { bytes_[0] = 1; bytes_[1] = 0; }
    void abandon() { bytes_[1] = 0; }

private:
    unsigned char *bytes_;
};

}  // namespace rts6x

#endif
