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
| link | Windows\LINK | link.vcxproj, src\ | |

`msbuild\Toolchain.targets` holds the build steps the tool trees did with batch files, as MSBuild tasks:
shalimar's runtimes (shc.vcxproj, target ShalimarRuntimes) and the rts6x libraries (rts6x.vcxproj, target
Rts6xLibraries). No step runs through cmd.exe, so Build Solution in Visual Studio runs the same steps as a
command-line build. Both projects set DisableFastUpToDateCheck, so Visual Studio always lets their targets decide
what is out of date.

The tree's own files: rts6x.vcxproj, ar6x.vcxproj and shc.vcxproj were changed or written for this solution, and
Windows\LINK\src\image.cpp starts two locals at 0, so a Debug build passes /WX (C4701). Five of cpp11's sources -
Compilers\C++Optimize\src\Type.h, Type.cpp, Mangle.cpp, parser\ParserType.cpp, parser\ParserTemplate.cpp - carry the
fix for reference-to-array parameters on the C6747 (`T (&)[N]`, 2026-10-16, as GRIDEd's copy). copy-toolchain.py keeps
these files rather than copying the tool trees' versions again.

Not copied: tests\, docs\, examples\, demo\, review probes, oracle\, tools\ other than RTS6x's ar6x.cpp, *.dat
(release seals), Makefiles, CMakeLists, .xcodeproj / .xcworkspace, the tools' own .sln and build-vs.cmd /
check-vs.cmd / generate.py, RTS6x's build.cmd (its steps are Toolchain.targets now), README / CLAUDE.md of the
tool repositories, .git*, and every build output (build\, x64\, obj\).
