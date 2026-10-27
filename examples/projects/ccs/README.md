# Sample CCS 7.4 projects

Three Code Composer Studio 7.4 projects for the TMS320C6747, shipped as CCS
left them: `.project`, `.ccsproject`, `.cproject`, the linker command file and
the sources. GRIDE opens a CCS project folder as it is and writes nothing into
it; they are here to try that on.

| folder     | what it is                                                        |
|------------|-------------------------------------------------------------------|
| `Hello`    | C: CCS 7.4's own Hello World template - `hello.c`, one `printf`   |
| `Sample`   | C++: `Math.cpp` over class templates for a vector, a matrix and a quaternion (`Vector.h`, `Matrix.h`, `Quat.h`), printed with `std::cout` |
| `SampleExt`| C++: `Sample` extended - two 3x3 matrices multiplied, a vector's cross, dot and component products, a matrix times a vector, and a rotation about Z; the same three headers grown with the operators it uses |

## Opening one

They are in `examples\projects\ccs` beside `GRIDE.exe`. White's CCS switch in
`settings.json` ships on, `"ccs": { "enabled": true }`, and White copies them
into `Documents\GRIDE\projects\ccs`, where Project > Open starts: choose a
project's `.project`, or its folder. Black (File > Open > Project / Workspace)
and Grey (File > Open Project) open the same `.project`, read-only. `"root"`
may name a CCS install (`C:/ti/ccsv7`); on a machine without CCS it is left
empty.

GRIDE takes the project's device, sources, include paths, defines and
configurations (Debug and Release) and builds with its own cpp11 or c90,
asm6x and lnk6x for tms6747, running the program on vm6747. Every option of
the project it does not honour is named on the first line of the build, with
GRIDE's default used instead.

## What each prints

| project    | Debug and Release                                                  |
|------------|--------------------------------------------------------------------|
| `Hello`    | `Hello World!`                                                     |
| `Sample`   | `Hello Math!`, then a unit quaternion `1 0 0 0`, a zero vector and a zero 3x3 matrix |
| `SampleExt`| `Sample`'s lines, then `A`, `B`, `C = A * B`, `a`, `b`, `a * b`, `a . b = 32`, `a ^ b`, `A * a`, `Rz(0.5)` and `Rz * a` |

Release builds also report the TI objects made: a `.out` for the real board
needs TI's linker, named under Tools.

`Hello` was made on 03-10-2026 by CCS 7.4 itself (`eclipsec` createProject,
template `com.ti.ccstudio.project.templates.helloWorld`, device C6747, compiler
8.2.2, ELF) and builds in CCS 7.4 with no error in both configurations.

Sample prints through `std::cout`, and GRIDE's C++ library writes it the way
`printf` does. Built by CCS 7.4 itself and run on TI's C6747 simulator, the
same program prints nothing: TI's own `<iostream>` emits no console output
there, though its `printf` does. Its values are CCS 7.4's all the same,
checked through `printf`.

SampleExt is a copy of Sample's project files with its own sources, and
its `.project` names it `SampleExt` rather than `Sample`, so the two can sit
in one CCS workspace.

## SampleExt for Windows

A CCS project is the C6747's alone, so SampleExt is also a GRIDE project of
its own outside this folder: `examples\projects\cpp-sampleext`, the same four
sources, unchanged, with `cpp-sampleext.pro`. Built there with GRIDE's cpp11,
masm and link for Windows (Debug or Release, on GRIDE's runtime or
Microsoft's), it prints SampleExt's lines of the table above, 34 in all,
the same as Microsoft's cl makes of the same sources. A change to the sources
goes into both folders.
