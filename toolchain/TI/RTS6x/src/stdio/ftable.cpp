// Spec: ISO C 7.19.1 (stdin, stdout, stderr); SPRAB89B 9.18 (they are &_ftable[0], [1], [2]).
// The table of FILE, with the three standard streams first on the host's descriptors 0, 1 and 2.
// Data alone - a translation unit with no function, which cpp11 takes since 2026-10-06.

#include "Stream.h"

FILE _ftable[RTS6X_FTABLE_COUNT] = {
    { 0, 0, 0, 0, 0, rts6x::Stream::Open | rts6x::Stream::Readable },
    { 1, 0, 0, 0, 0, rts6x::Stream::Open | rts6x::Stream::Writable },
    { 2, 0, 0, 0, 0, rts6x::Stream::Open | rts6x::Stream::Writable },
};
