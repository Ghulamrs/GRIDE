# Sample CCS 7.4 projects

Three Code Composer Studio 7.4 projects for the TMS320C6747, shipped as CCS
left them: `.project`, `.ccsproject`, `.cproject`, the linker command file and
the sources. RIDE opens a CCS project folder as it is and writes nothing into
it; they are here to try that on.

| folder     | what it is                                                        |
|------------|-------------------------------------------------------------------|
| `Hello`    | C: CCS 7.4's own Hello World template - `hello.c`, one `printf`   |
| `Sample`   | C++: `Math.cpp` over class templates for a vector, a matrix and a quaternion (`Vector.h`, `Matrix.h`, `Quat.h`), printed with `std::cout` |
| `SampleExt`| C++: `Sample` extended - two 3x3 matrices multiplied, a vector's cross, dot and component products, a matrix times a vector, and a rotation about Z; the same three headers grown with the operators it uses |

## Opening one

The CCS switch in `settings.json` ships on, `"ccs": { "enabled": true }`, and
these three are copied into `Documents/RIDE/projects/ccs` - where
`Project > Open...` starts. On Windows choose a project's `.project`; on macOS
its folder. `"root"` may name a CCS install (`C:/ti/ccsv7`); on a machine
without CCS it is left empty. On the console, give the folder:

    ride Hello --run
    ride SampleExt --config release --run

RIDE takes the project's device, sources, include paths, defines and
configurations (Debug and Release) and builds with its own cpp11 or c90,
asm6x and lnk6x for tms6747, running the program on vm6747. Every option of
the project it does not honour is named in the console on the first line of
the build, with RIDE's default used instead.

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

Sample prints through `std::cout`, and RIDE's C++ library writes it the way
`printf` does. Built by CCS 7.4 itself and run on TI's C6747 simulator, the
same program prints nothing: TI's own `<iostream>` emits no console output
there, though its `printf` does. Its values are CCS 7.4's all the same,
checked through `printf`.

SampleExt is a copy of Sample's project files with its own sources, and
its `.project` names it `SampleExt` rather than `Sample`, so the two can sit
in one CCS workspace.

The CCS projects RIDE's reader was measured against - K6747c, K6747cpp and
P7misc, with their CCS 5.5 twins - are kept unchanged in `docs/ccs-reference`.
