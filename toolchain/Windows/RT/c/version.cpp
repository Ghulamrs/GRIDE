// grt's own name and number, so that the library says which run-time it is, as RTS6x's __rts6x_version does.
// The string has the shape the toolchain's programs answer --version in - `name x.y - what it is` - and an IDE's
// About reads it out of grtc.lib and grtcd.lib. Nothing names the symbol, so no program carries it.
//
// 1.0 is the first number grt has had (2026-11-12): written for GRIDE from 2026-10-23 and never numbered. Its C
// library is RTS6x's sources, but its start-up, its Windows layer and its C++ support are its own, so it is
// numbered on its own and not with RTS6x.
//
// C++ and not C: c90 refuses a file that defines no function, and this one defines none.
extern "C" const char __grt_version[] = "grt 1.0 - GRIDE's Windows x64 run-time";
