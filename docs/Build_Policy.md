# Build policy: Release linking of the toolchain's own programs

For every lane working on this repository. It covers the Release configuration of each Visual Studio project that
produces one of our programs: compilers, assemblers, linkers, simulators, the debugger and the IDE. The Debug
configuration does not change. Programs that users compile with the toolchain are not affected, and neither is what
GRIDE's own `link.exe` does with its `/OPT:REF` and `/OPT:ICF` options.

## The rule

A project's Release configuration sets these three properties and keeps its debug information:

| Property | Value | Linker option | What it does |
|---|---|---|---|
| `LinkIncremental` | `false` | `/INCREMENTAL:NO` | Leaves out the padding kept for later relinks |
| `OptimizeReferences` | `true` | `/OPT:REF` | Removes functions and data nothing refers to |
| `EnableCOMDATFolding` | `true` | `/OPT:ICF` | Keeps one copy of byte-identical code |

```xml
<PropertyGroup Condition="'$(Configuration)'=='Release'">
  <LinkIncremental>false</LinkIncremental>
</PropertyGroup>
<ItemDefinitionGroup Condition="'$(Configuration)'=='Release'">
  <Link>
    <GenerateDebugInformation>true</GenerateDebugInformation>
    <OptimizeReferences>true</OptimizeReferences>
    <EnableCOMDATFolding>true</EnableCOMDATFolding>
  </Link>
</ItemDefinitionGroup>
```

The `PropertyGroup` goes after the import of `Microsoft.Cpp.props`.

## Why

With `GenerateDebugInformation` on and these properties unset, the linker works incrementally and keeps unreferenced
code. Measured on `cpp11.exe`, the same objects relinked with the PDB kept:

| Settings | Size |
|---|---:|
| As it was | 7.45 MB |
| `/INCREMENTAL:NO` | 5.92 MB |
| plus `/OPT:REF` | 5.20 MB |
| plus `/OPT:ICF` | 4.72 MB |

Size is the reason to use these settings. It is not a reason to split a program into DLLs: `cpp11.exe` was studied for
that, and the three settings save about four times what a TMS6747 plug-in would move, without adding a runtime
dependency.

## What it costs

- A Release relink is a full link rather than an incremental one.
- `/OPT:ICF` gives identical functions one address. A crash trace or a breakpoint can name a different function from
  the one that ran, and code that compares function addresses would see them as equal.

To read a crash trace exactly, relink that build with `EnableCOMDATFolding` set to `false`.

## How to apply it

1. One project at a time, by the lane that owns it, as part of that lane's own work. No sweep across projects another
   lane has open.
2. Search the project for comparisons of function addresses before turning on `/OPT:ICF`.
3. Rebuild and run that program's full gate. Passing its tests is what shows the settings are safe for it.
4. Record the sizes in the table below in the same commit.
5. Where a project cannot take a setting, leave it off and record the reason in the table.

## A compiler builds other components

`cpp11` builds `grt`, `rts6x` and `shmrt`, and `shalimar`'s runtime depends on it, so its own suite is not its whole
gate. A release of a compiler also runs the harnesses of what it builds, on the libraries the new compiler built:

- `toolchain/Windows/RT/tests`: `completeness.ps1`, `conformance.ps1`, `shalimar/access.ps1` with and without
  `-DebugRuntime`, `threads`, `math99` at `-O2` and `-O0`, `filing`, `debug`, and `probe.ps1` over `exceptions`,
  `names`, `perthread`, `printf`, `signals`, `sort`, `strings` and `wide`.
- The assembly of those libraries compared with the last release's, file by file, with each difference explained.

A change that should not touch generated code, such as these link settings, must leave that assembly byte-identical.

## Status

| Project | Program | Status | Before | After |
|---|---|---|---:|---:|
| `toolchain/Compilers/C++Optimize/ide/cxx1.vcxproj` | `cpp11.exe` | Applied in cpp11 1.61 | 7.45 MB | 4.73 MB |
| All other projects | | Not applied, savings not measured | | |
