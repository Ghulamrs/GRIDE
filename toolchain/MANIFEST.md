# toolchain\ - what is here and why (minimal copies)

The toolchain GRIDE runs, built by GRIDE.sln beside GRIDE.exe in `x64\<Configuration>\`. Taken from
D:\GRA\My Documents\GRIDEdworking (read only) by `copy-toolchain.py <IDE root>`. Compilers\, TI\ and Windows\
are siblings because their projects name each other by relative path. Each tool tree keeps its own layout, and
the project files stay inside the tool trees.

| tool | tree | what is here | why |
|---|---|---|---|
| c90 | Compilers\Compiler-Ci | ide\cc1.vcxproj (+filters), src\, msvc\, lib\ | msvc\compat is an include directory; lib\ is c90's headers, compiled in as CC1_INCLUDE_DIR ($(ProjectDir)..\lib) |
| cpp11 | Compilers\C++Optimize | ide\cxx1.vcxproj (+filters), src\, msvc\, include\, lib\ | msvc\compat; lib\ and include\ are its headers (CXX1_INCLUDE_DIR, CXX1_CXX_INCLUDE_DIR) |
| shalimar | Compilers\Compiler-Si | ide\shc.vcxproj (+filters), src\, runtime\ | runtime\ is an include directory and the Shalimar runtime's source |
| c2s | Compilers\Converter-C2S | c2s.vcxproj, src\ | |
| asm6x | TI\ASM6x | asm6x.vcxproj, src\ | |
| lnk6x | TI\LNK6x | lnk6x.vcxproj, src\ | |
| rts6x | TI\RTS6x | rts6x.vcxproj, src\, printf6x.members | the C6747 runtime's sources and printf6x.lib's member list |
| ar6x | TI\RTS6x\tools\ar6x | ar6x.vcxproj, ar6x.cpp | the archiver rts6x packs its libraries with; a build tool, built into its own build\ folder |
| sim6747 | TI\SIM6747 | sim6747.vcxproj, src\ | |
| vm6747 | TI\VM6747 | vm6747.vcxproj, src\ | |
| masm | Windows\MASM | masm.vcxproj, src\ | |
| link | Windows\LINK | link.vcxproj, src\, tests\ | src\librarian.cpp is link /LIB (archives, import libraries from a .def); tests\ is this tree's own, not a copy |
| grt | Windows\RT | grt.vcxproj, rts6x-c.sources, rts6x-cxx.sources, kernel32.def, catalogue.json, start\, compat\, cxx\, c\, win\, internal\, tests\, bench\ | GRIDE's own runtime for Windows, GRIDE's (not copied; docs\Runtime_Design.md): RTS6x's C library (the files rts6x-c.sources names) and the runtime's own layers, built by c90, cpp11, masm and link into five layered libraries - lib\grtstart.lib, grtcompat.lib, grtcxx.lib, grtc.lib, grtwin.lib - with lib\kernel32.lib and the Shalimar runtime over them; catalogue.json is its public C ABI |

`msbuild\Toolchain.targets` holds the build steps the tool trees did with batch files, as MSBuild tasks:
shalimar's runtimes (shc.vcxproj, target ShalimarRuntimes), the rts6x libraries (rts6x.vcxproj, target
Rts6xLibraries) and grt's (grt.vcxproj, target GrtLibraries: WindowsLibrary, LibraryClosed). No step runs through
cmd.exe, so Build Solution in Visual Studio runs the same steps as a command-line build. The three projects set
DisableFastUpToDateCheck, so Visual Studio always lets their targets decide what is out of date.

Every program of the toolchain is built with the static VC++ runtime (/MT, /MTd): only GRIDE.exe, which is C++/CLI,
needs the VC++ runtime DLLs Setup puts beside it. RTS6x's sources carry `#if defined(_WIN32)` branches for grt (the
FILE, abort's status, the Microsoft runtime's spellings of NaN, %p and %a, and two fixes - log's order of evaluation,
pow(-1, y) for a huge y); the C6747 build compiles the other branch, as before.

The tree's own files: rts6x.vcxproj, ar6x.vcxproj and shc.vcxproj were changed or written for this solution, and
Windows\LINK\src\image.cpp starts two locals at 0, so a Debug build passes /WX (C4701). Five of cpp11's sources -
Compilers\C++Optimize\src\Type.h, Type.cpp, Mangle.cpp, parser\ParserType.cpp, parser\ParserTemplate.cpp - carry the
fix for reference-to-array parameters on the C6747 (`T (&)[N]`, 2026-10-16, as GRIDEd's copy). copy-toolchain.py keeps
these files rather than copying the tool trees' versions again.

cpp11's `tests\conformance\` is this tree's own, not a copy: the conformance suite and its frozen baseline
(2026-10-28, its README.md), the measure of the road to strict C++11. copy-toolchain.py only copies, so it
leaves the folder alone.

Not copied: the tool trees' tests\, docs\, examples\, demo\, review probes, oracle\, tools\ other than RTS6x's ar6x.cpp, *.dat
(release seals), Makefiles, CMakeLists, .xcodeproj / .xcworkspace, the tools' own .sln and build-vs.cmd /
check-vs.cmd / generate.py, RTS6x's build.cmd (its steps are Toolchain.targets now), README / CLAUDE.md of the
tool repositories, .git*, and every build output (build\, x64\, obj\).

## Versions (2026-11-12)

Every program answers `--version` in one shape: its name, its number and what it is in four words on the first line,
the copyright on the second (cpp11 adds its seal date and build time). Every runtime library holds a line of the same
shape, which nothing in a program names. Help > About of the three editors (`common\AboutBox.h`) shows these lines as
they are answered, so the words are written once, in the tool.

| component | number | where the number is written | the line |
|---|---|---|---|
| cpp11 | 1.66 | Compilers\C++Optimize\src\Version.h | `cpp11 1.66 - C++03 with C++11 extensions` |
| c90 | 1.3 | Compilers\Compiler-Ci\src\Driver.cpp, `cc1Version` | `c90 1.3 - ISO C 90 compiler` |
| shalimar, shmrt | 1.3 | Compilers\Compiler-Si\runtime\ShalimarVersion.h | `shalimar 1.3 - the Shalimar language compiler`, `shmrt 1.3 - the Shalimar run-time library` (Runtime.cpp, `shm_runtime_version`) |
| c2s | 1.2 | Compilers\Converter-C2S\src\main.cpp | `c2s 1.2 - C and Shalimar converter` |
| masm | 1.0 | Windows\MASM\src\main.cpp, `MASM_VERSION` | `masm 1.0 - x64 assembler, MASM syntax` |
| link | 1.0 | Windows\LINK\src\main.cpp, `LINK_VERSION` | `link 1.0 - x64 linker and librarian` |
| gride-dbg | 1.1 | Windows\DBG\src\main.cpp, `GRIDE_DBG_VERSION` | `gride-dbg 1.1 - GRIDE's Windows x64 debugger` |
| grt | 1.0 | Windows\RT\c\version.cpp, `__grt_version` (in grtc.lib, grtcd.lib) | `grt 1.0 - GRIDE's Windows x64 run-time` |
| asm6x | 1.4 | TI\ASM6x\src\main.cpp | `asm6x 1.4 - TMS320C6000 assembler, TI ELF` |
| lnk6x | 1.2 | TI\LNK6x\src\main.cpp | `lnk6x 1.2 - TMS320C6000 linker, TI ELF` |
| sim6747 | 1.3 | TI\SIM6747\src\main.cpp | `sim6747 1.3 - TMS320C6747 simulator, runs .out` |
| vm6747 | 1.1 | TI\VM6747\src\main.cpp | `vm6747 1.1 - TMS320C6000 emulator, runs assembly` |
| RTS6x, printf6x | 1.2 | TI\RTS6x\src\boot\version.s, `__rts6x_version` and `__printf6x_version` (a member of both libraries) | `RTS6x 1.2 - TMS320C6747 run-time support library`, `printf6x 1.2 - RTS6x's formatted output library` |
| ar6x (a build tool, not installed) | 1.0 | TI\RTS6x\tools\ar6x\ar6x.cpp | `ar6x 1.0 - TMS320C6000 archiver, TI libraries` |

How a number is given: a part built from another's sources is numbered with it and moves with it - shmrt with
shalimar, printf6x with RTS6x - and a part with a source tree of its own has a number of its own. masm, link and grt
came without one (masm, link and the Shalimar runtime were copied in on 2026-10-16 with no `--version`; grt was
written here from 2026-10-23) and start at 1.0. The banner a compiler prints before a compile is unchanged.
