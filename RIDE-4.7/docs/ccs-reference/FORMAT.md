# CCS project format for a TMS320C6747 (C6000) project, measured

Measured on 2026-09-30 on the Windows box, from projects that **CCS itself created
and built headless**. Nothing here was hand-written except the one
`sourceEntries` exclusion in `ccs74/P7misc`, which is marked below. Every
command line quoted is copied from a real `gmake` log in `logs/`.

| | CCS 7.4 | CCS 5.5 |
| --- | --- | --- |
| install | `C:\ti\ccsv7` | `C:\ti\ccsv5` |
| compiler | `ti-cgt-c6000_8.2.2` | `c6000_7.4.4` |
| create app | `com.ti.ccstudio.apps.createProject` | `com.ti.ccstudio.apps.projectCreate` |
| build app | `com.ti.ccstudio.apps.buildProject` | `com.ti.ccstudio.apps.projectBuild` |
| workspace | `C:\ti\ccsref\ws7` | `C:\ti\ccsref\ws55` |

## What is in this directory

```
ccs74/K6747c      C project, CCS 7.4: main.c, inc/config.h, linked util.c, C6747.cmd
ccs74/K6747cpp    C++ project, CCS 7.4: main.cpp (a class + printf), --exceptions, EH runtime
ccs74/P7misc      probe, CCS 7.4: a source in a subfolder with per-file options,
                  a build variable, a post-build step, and one excluded file
ccs55/K6747c      the same C project, CCS 5.5
ccs55/K6747cpp    the same C++ project, CCS 5.5
src/              the files that were copied or linked into the projects
logs/             create*.cmd / build*.cmd (the exact scripts), their output,
                  help7.txt/help55.txt (usage), lo7.txt/lo55.txt (every build
                  option CCS lists for the C6747), tpl*.txt (templates)
ti-option-definitions/
                  TI's own option-definition XML for the two compiler versions,
                  copied from the CCS plugins, and a TSV of every option in each
                  (id, valueType, flag, default, enum values) - see "Where the
                  option table comes from"
```

Each project directory holds `.project`, `.ccsproject`, `.cproject`,
`.settings/`, its sources, and `Debug/` and `Release/` with the generated
`makefile`, `subdir_rules.mk`, `subdir_vars.mk`, `sources.mk`, `objects.mk` and
`ccsObjs.opt`. Objects, `.out`, maps and dependency files were left behind.
All eight project/config builds succeeded; `P7misc` Debug built after the
exclusion was added (it failed before, on purpose - see below).

## The commands that worked

Create, CCS 7.4 (CCS 5.5 is the same with `projectCreate`, `ccsv5`, `ws55`,
`7.4.4` and `C:/ti/c6747-lib`; see `logs/create55.cmd`):

```
C:\ti\ccsv7\eclipse\eclipsec.exe -noSplash -data C:\ti\ccsref\ws7
  -application com.ti.ccstudio.apps.createProject
  -ccs.name K6747c -ccs.device TMS320C67XX.TMS320C6747 -ccs.cgtVersion 8.2.2
  -ccs.outputFormat ELF -ccs.template com.ti.common.project.core.emptyProjectTemplate
  -ccs.configurations Debug Release
  -ccs.copyFile C:\ti\ccsref\src\main.c -ccs.cmd C:\ti\ccsref\src\C6747.cmd
  -ccs.copyFile C:\ti\ccsref\src\inc\config.h @dir inc
  -ccs.linkFile C:\ti\ccsref\src\util.c
  -ccs.setCompilerOptions "--include_path=${PROJECT_ROOT}/inc"
  -ccs.setCompilerOptions "-O2 --opt_for_speed=5 --define=NDEBUG --define=LEVEL=2 --undefine=OLDAPI --no_compress --symdebug:none" @configurations Release
  -ccs.setLinkerOptions "--heap_size=0x800 --stack_size=0x1000 -i C:/Users/GRA/Documents/VM6747/tilib -l rts6740_elf_eh.lib" @configurations Release
  -ccs.overwrite full
```

`K6747cpp` differs by `-ccs.rts rts6740_elf_eh.lib`, `main.cpp`,
`--exceptions` and `-i <eh lib dir>` in every configuration, and in Release
`-O3 -ms3 --define=LEVEL=3 --define=\"GREETING=hi there\"` and
`--heap_size=0x2000 --stack_size=0x2000 --rom_model`.

Build (each project x each configuration):

```
eclipsec.exe -noSplash -data C:\ti\ccsref\ws7 -application com.ti.ccstudio.apps.buildProject
  -ccs.projects K6747c -ccs.configuration Release -ccs.buildType full
```

Things that did not work, and why:

- `-ccs.configurations "Debug Release"` (quoted) creates **one** configuration
  named `Debug Release`. Unquoted, it is a list.
- `-ccs.cmd <path>` or `-ccs.copyFile <path>` of a file that does not exist:
  `-ccs.copyFile` says `cannot be located`; `-ccs.cmd` says nothing, copies
  nothing and records the name anyway, and the build then links with **no**
  command file (`#10247-D creating output section ... without a SECTIONS
  specification`). The `C6747.cmd` the task named does not exist on
  the box (only `C6747-ddr.cmd` is there), so the repo's `tools/c6747/C6747.cmd`
  was copied to `C:\ti\ccsref\src\` and used. **It is byte-identical to TI's
  own `C:\ti\ccsv7\ccs_base\c6000\include\C6747.cmd`** (and to CCS 5.5's), which
  is what CCS copies in when `-ccs.cmd` is not given.
- Without `-ccs.cmd` CCS copies the device default in first, so a later
  `-ccs.linkFile ...\C6747.cmd` is dropped: `Resource 'C6747.cmd' already exists!`.
- CCS 5.5 `projectCreate` defaults to **COFF** for this device (its
  `c6747.xml` has no `IsElfDefault`); CCS 7.4 defaults to ELF. Always pass
  `-ccs.outputFormat ELF`.

## `.project` - identical in CCS 7.4 and 5.5

A standard Eclipse project description. What a reader needs:

| XPath | meaning |
| --- | --- |
| `/projectDescription/name` | project name (`${ProjName}`) |
| `/projectDescription/natures/nature` | `com.ti.ccstudio.core.ccsNature` marks a CCS project; `org.eclipse.cdt.core.cnature`, `...ccnature` (present even in the C project), `org.eclipse.cdt.managedbuilder.core.managedBuildNature`, `...ScannerConfigNature` |
| `/projectDescription/buildSpec/buildCommand/name` | `org.eclipse.cdt.managedbuilder.core.genmakebuilder` - makefiles are generated |
| `/projectDescription/projects/project` | referenced projects (none here) |
| `/projectDescription/linkedResources/link` | files that live outside the folder: `name` (project-relative path), `type` (`1` file, `2` folder), `location` (absolute, `C:/ti/ccsref/src/util.c` here) or, in hand-made/imported projects, `locationURI` with a path variable (`PROJECT_LOC/...`, `PARENT-1-PROJECT_LOC/...`) - not produced by these runs |

## `.ccsproject` - the creation record

```xml
<projectOptions>
  <ccsVersion value="7.4.0"/>                            <!-- 7.4 only -->
  <deviceVariant value="TMS320C67XX.TMS320C6747"/>
  <deviceFamily value="C6000"/>
  <deviceEndianness value="little"/>
  <codegenToolVersion value="8.2.2"/>                    <!-- 7.4.4 in CCS 5.5 -->
  <isElfFormat value="true"/>
  <linkerCommandFile value="C6747.cmd"/>                 <!-- absent if none was copied -->
  <rts value="libc.a"/>                                  <!-- rts6740_elf_eh.lib in K6747cpp -->
  <createSlaveProjects value=""/>                        <!-- 7.4 only -->
  <templateProperties value="id=com.ti.common.project.core.emptyProjectTemplate,"/>
  <filesToOpen value=""/>                                <!-- 7.4 only -->
</projectOptions>
```

**It records what the wizard was told, not what is built.** The build reads
only `.cproject` and the files in the folder. Measured: the linker command file
used is whatever `*.cmd` sits in the project (it became `CMD_SRCS`), and the
runtime library is the `LIBRARY` option list, whatever `<rts>` says.

## Device, compiler version, output format - where each is said

| question | where | value here |
| --- | --- | --- |
| device | `.ccsproject/deviceVariant`; `.cproject` `OPT_TAGS` `DEVICE_CONFIGURATION_ID=` | `TMS320C67XX.TMS320C6747` (the id `-ccs.device` takes; `logs/dev7.txt`, `logs/dev55.txt` list every device) |
| family / ISA | `.ccsproject/deviceFamily`; the `C6000` in every `buildDefinitions` id | `C6000` |
| core | compiler `SILICON_VERSION` (string) -> `-mv6740` | `6740` = C674x |
| device macro | compiler `DEFINE` list's first entry | `c6747` -> `--define=c6747` |
| endianness | `.ccsproject/deviceEndianness`; `OPT_TAGS DEVICE_ENDIANNESS=` | `little` (big would add `BIG_ENDIAN` -> `--big_endian`) |
| compiler version | `.ccsproject/codegenToolVersion`; `OPT_CODEGEN_VERSION` option; major.minor in every tool id (`C6000_8.2`, `C6000_7.4`) | `8.2.2` / `7.4.4` |
| output format | `.ccsproject/isElfFormat`; `OPT_TAGS OUTPUT_FORMAT=` | `ELF`; with 7.4.x also compiler `ABI` = `...ABI.eabi` -> `--abi=eabi` (8.x has only EABI and stores no ABI) |
| output type | `OPT_TAGS OUTPUT_TYPE=`; toolChain superClass `...exe.DebugToolchain` vs `...library...` | `executable` |
| runtime library | linker `LIBRARY` list (effective); `OPT_TAGS RUNTIME_SUPPORT_LIBRARY=` and `.ccsproject/rts` (record) | `libc.a` or `rts6740_elf_eh.lib` |
| linker command file | a `*.cmd` file in the project tree (effective); `OPT_TAGS LINKER_COMMAND_FILE=` (record, only when copied) | `C6747.cmd` |
| CCS release | `OPT_TAGS CCS_MBS_VERSION=`; `.ccsproject/ccsVersion` | `6.1.3` + `7.4.0` / `5.5.0` |

Where the defaults come from: `ccs_base/common/targetdb/devices/c6747.xml`
in each install -

```xml
<property Value="little" id="Endianness"/>
<property Value="true" id="IsElfDefault"/>          <!-- CCS 7.4 only -->
<property Value="--silicon_version=6740 -D=c6747" id="CompilerBuildOptions"/>
<property Value="--stack_size=0x800 --heap_size=0x800" id="LinkerBuildOptions"/>
<property Value="C6747.cmd" id="LinkerCmd"/>
```

That is why every project here has `-mv6740 --define=c6747` and heap and stack
0x800 until told otherwise.

## `.cproject` - the build model

Skeleton, one `cconfiguration` per build configuration:

```
/cproject/storageModule[@moduleId='org.eclipse.cdt.core.settings']
  /cconfiguration[@id='com.ti.ccstudio.buildDefinitions.C6000.Debug.<n>']
    /storageModule[@moduleId='org.eclipse.cdt.core.settings'][@name='Debug']
        /macros/stringMacro[@name][@type='VALUE_TEXT'][@value]        build variables
        /extensions/extension                                         parsers (ignore)
    /storageModule[@moduleId='cdtBuildSystem']
      /configuration[@name='Debug'][@artifactName='${ProjName}'][@artifactExtension='out']
                    [@parent='com.ti.ccstudio.buildDefinitions.C6000.Debug']
                    [@postbuildStep][@prebuildStep][@cleanCommand='${CG_CLEAN_CMD}']
        /folderInfo[@resourcePath='']                                 the whole project
          /toolChain[@superClass='com.ti.ccstudio.buildDefinitions.C6000_8.2.exe.DebugToolchain']
            /option  OPT_TAGS (stringList), OPT_CODEGEN_VERSION (string)
            /targetPlatform, /builder[@buildPath='${BuildDirectory}'][@parallelBuildOn]
            /tool[@name='C6000 Compiler'][@superClass='...C6000_8.2.exe.compilerDebug']
              /option ...                                              compiler options
              /inputType ...                                           (ignore)
            /tool[@name='C6000 Linker'][@superClass='...C6000_8.2.exe.linkerDebug']
              /option ...                                              linker options
            /tool[@name='C6000 Hex Utility']                           7.4 only, not run
        /fileInfo[@resourcePath='lib/util.c'][@name='util.c']          per-file overrides
          /tool[@superClass=<the config's compiler tool instance id>]/option ...
        /sourceEntries/entry[@kind='sourcePath'][@name=''][@excluding='a.c|dir/b.c']
/cproject/storageModule[@moduleId='cdtBuildSystem']/project[@projectType='com.ti.ccstudio.buildDefinitions.C6000.ProjectType']
```

Configuration and tool ids end in random numbers (`.1370101988`); **match on
`superClass`, not `id`**, and on the tail after `compilerID.` / `linkerID.`
(the prefix is `com.ti.ccstudio.buildDefinitions.C6000_8.2.` in 7.4 and
`...C6000_7.4.` in 5.5). The Release configuration's optimisation option has its
own id, `compilerID.OPT_LEVEL.release`, whose superClass is `compilerID.OPT_LEVEL`
- another reason to match on the tail of `superClass`.

An `option` element is one of:

| valueType | shape | example |
| --- | --- | --- |
| `string` | `@value` | `STACK_SIZE value="0x1000"` |
| `boolean` | `@value="true"` | `NO_COMPRESS value="true"` |
| `enumerated` | `@value` = the enum value's full id | `OPT_LEVEL.release value="...compilerID.OPT_LEVEL.3"` |
| `stringList`, `definedSymbols`, `includePath`, `libPaths`, `libs` | `listOptionValue[@value]` children, in order | `DEFINE` -> `c6747`, `NDEBUG`, `LEVEL=2` |

**Only options that differ from the definition's default are stored.**
Measured: `-O2` on Release stored nothing (Release's default is already level 2);
`--rom_model` stored nothing (it is the default of `INITIALIZATION_MODEL`); an
option CCS's own template sets (`DIAG_WARNING 225`, `DIAG_WRAP off`,
`DISPLAY_ERROR_NUMBER`, `MAP_FILE`, `XML_LINK_INFO`, `OUTPUT_FILE`) is stored
because it differs from the tool definition. So a reader must take the
defaults from the definition, not from the file - the table below, and in full
`ti-option-definitions/options-*.tsv`.

## Every option set here, stored and emitted

Ids are tails after `com.ti.ccstudio.buildDefinitions.C6000_8.2.` (7.4) or
`...C6000_7.4.` (5.5). "Flag" is copied from the generated makefile.

### Compiler

| what | option id | stored as | flag in makefile |
| --- | --- | --- | --- |
| core | `compilerID.SILICON_VERSION` | string `6740` | `-mv6740` |
| ABI (5.5 / 7.4.x only) | `compilerID.ABI` | enum `...ABI.eabi` | `--abi=eabi` |
| opt level | `compilerID.OPT_LEVEL` (Debug, per file), `compilerID.OPT_LEVEL.release` (Release) | enum `...OPT_LEVEL.{off,0,1,2,3}` | `-Ooff`/`-O0`..`-O3`. Defaults: Debug none (no flag), Release `2` |
| speed/size | `compilerID.OPT_FOR_SPEED` | enum `...OPT_FOR_SPEED.{0..5}` | `--opt_for_speed=5` |
| code size | `compilerID.OPT_FOR_SPACE` | enum `...OPT_FOR_SPACE.{0..3}` | `-ms3` |
| define | `compilerID.DEFINE` | definedSymbols list: `c6747`, `NDEBUG`, `LEVEL=2`, and `"GREETING=hi there"` **with the quotes inside the value** | `--define=c6747 --define=NDEBUG --define=LEVEL=2 --define="GREETING=hi there"` - values emitted verbatim |
| undefine | `compilerID.UNDEFINE` | stringList `OLDAPI` | `--undefine=OLDAPI` |
| include path | `compilerID.INCLUDE_PATH` | includePath list, macros unresolved: 7.4 `${PROJECT_ROOT}`, `${PROJECT_ROOT}/inc`, `${CG_TOOL_ROOT}/include`; 5.5 `"${CG_TOOL_ROOT}/include"`, `"${PROJECT_ROOT}/inc"` (quoted, and **no** `${PROJECT_ROOT}` by default) | `--include_path="C:/ti/ccsref/ws7/K6747c/inc"` - resolved to absolute, quoted, in list order |
| debug model | `compilerID.DEBUGGING_MODEL` | enum `...SYMDEBUG__DWARF` (Debug, stored), `...SYMDEBUG__NONE` | `-g`, `--symdebug:none`; Release stores none and emits none |
| no compress | `compilerID.NO_COMPRESS` | boolean | `--no_compress` |
| C++ exceptions | `compilerID.EXCEPTIONS` | boolean | `--exceptions` (on the compile line only, not the link line) |
| diagnostics (template) | `compilerID.DIAG_WARNING` list `225`, `DIAG_WRAP` enum `.off`, `DISPLAY_ERROR_NUMBER` boolean | | `--diag_warning=225 --diag_wrap=off --display_error_number` |
| never stored | added by CCS's command-line generator for dependencies | | `--preproc_with_compile --preproc_dependency="main.d_raw"` (7.4) / `="main.pp"` (5.5) |
| per-file subfolder | implied by the file's folder | | `--obj_directory="lib"` |

Compile line shape (7.4 Release, `logs/build7-K6747c-Release.txt`):

```
"C:/ti/ccsv7/tools/compiler/ti-cgt-c6000_8.2.2/bin/cl6x" -mv6740 -O2 --opt_for_speed=5
  --include_path="C:/ti/ccsref/ws7/K6747c" --include_path="C:/ti/ccsref/ws7/K6747c/inc"
  --include_path="C:/ti/ccsv7/tools/compiler/ti-cgt-c6000_8.2.2/include"
  --define=c6747 --define=NDEBUG --define=LEVEL=2 --undefine=OLDAPI --symdebug:none
  --diag_warning=225 --diag_wrap=off --display_error_number --no_compress
  --preproc_with_compile --preproc_dependency="main.d_raw"  "../main.c"
```

The flag order is the definition file's option order, not the order they were
set in; 5.5 orders them differently (`-mv6740 --abi=eabi -O2 --symdebug:none
--include_path=... --define=... --no_compress --opt_for_speed=5 ...`). The
compiler runs in `<project>/<Config>/`, so a project file is `../main.c` and a
linked file keeps its absolute path.

### Linker (cl6x `-z`)

| what | option id | stored as | flag |
| --- | --- | --- | --- |
| run the linker | `linkerID.Z` | hidden, `value="true"` in the definition, never stored | `-z` |
| heap | `linkerID.HEAP_SIZE` | string `0x800` | `--heap_size=0x800` |
| stack | `linkerID.STACK_SIZE` | string `0x1000` | `--stack_size=0x1000` |
| search path | `linkerID.SEARCH_PATH` | libPaths list: `${CG_TOOL_ROOT}/lib`, user dirs, `${CG_TOOL_ROOT}/include` (5.5: each quoted) | `-i"C:/ti/.../lib" -i"C:/Users/GRA/Documents/VM6747/tilib" -i"C:/ti/.../include"` |
| libraries | `linkerID.LIBRARY` | libs list: `libc.a`, `rts6740_elf_eh.lib` (5.5: quoted) | at the **end** of the line, after objects and the .cmd: `-lrts6740_elf_eh.lib -llibc.a` (7.4), `-l"libc.a" -l"rts6740_elf_eh.lib"` (5.5) |
| map file | `linkerID.MAP_FILE` | string `${ProjName}.map` (5.5: `"${ProjName}.map"`) | `-m"K6747c.map"` |
| output | `linkerID.OUTPUT_FILE` | string `${ProjName}.out` | `-o "K6747c.out"` |
| XML link info | `linkerID.XML_LINK_INFO` | string `${ProjName}_linkInfo.xml` | `--xml_link_info="K6747c_linkInfo.xml"` |
| reread libs, warn sections | `linkerID.REREAD_LIBS`, `WARN_SECTIONS` | defaults `true`, not stored | `--reread_libs --warn_sections` |
| init model | `linkerID.INITIALIZATION_MODEL` | enum `...ROM_MODEL` (default, not stored) / `...RAM_MODEL` | `--rom_model` / `--ram_model` |
| diagnostics | `linkerID.DIAG_WRAP` `.off`, `DISPLAY_ERROR_NUMBER` | | `--diag_wrap=off --display_error_number` |

The link line **repeats the compiler's options** except include paths, the
dependency options and `--exceptions` (measured - `-mv6740 -O2 --opt_for_speed=5
--define=... --undefine=... --symdebug:none --diag_warning=225 ... --no_compress`
all appear ahead of `-z`). Inputs come from `ORDERED_OBJS` in the makefile:
objects (`"./main.obj" "./util.obj"`, a subfolder's as `"./lib/util.obj"`),
then every `CMD_SRCS` file (`"../C6747.cmd"`), then `-l` libraries. Full line,
7.4 Release:

```
"C:/ti/ccsv7/tools/compiler/ti-cgt-c6000_8.2.2/bin/cl6x" -mv6740 -O2 --opt_for_speed=5
  --define=c6747 --define=NDEBUG --define=LEVEL=2 --undefine=OLDAPI --symdebug:none
  --diag_warning=225 --diag_wrap=off --display_error_number --no_compress
  -z -m"K6747c.map" --heap_size=0x800 --stack_size=0x1000
  -i"C:/ti/ccsv7/tools/compiler/ti-cgt-c6000_8.2.2/lib" -i"C:/Users/GRA/Documents/VM6747/tilib"
  -i"C:/ti/ccsv7/tools/compiler/ti-cgt-c6000_8.2.2/include" --reread_libs --diag_wrap=off
  --display_error_number --warn_sections --xml_link_info="K6747c_linkInfo.xml" --rom_model
  -o "K6747c.out" "./main.obj" "./util.obj" "../C6747.cmd"  -lrts6740_elf_eh.lib -llibc.a
```

`libc.a` is not an archive: it is TI's library *index*, from which cl6x picks
an `rts6740*_elf*.lib` by the objects' build attributes.

## Debug versus Release

Both are created from `com.ti.ccstudio.buildDefinitions.C6000.Debug` /
`.Release` (`configuration/@parent`) with toolchains `...exe.DebugToolchain` /
`...exe.ReleaseToolchain`. The differences before anything is set:

| | Debug | Release |
| --- | --- | --- |
| opt level | none (definition default `_none`, no flag) | `2` (the `OPT_LEVEL.release` default) - `-O2` with nothing stored |
| debug model | `SYMDEBUG__DWARF` stored -> `-g` | nothing stored, no flag |
| everything else | the same | the same |

Every other option is per configuration and independent - `P7misc`'s
`-ccs.setLinkerOptions` without `@configurations` was written into both.

## Source files: how the list is made

There is no file list. **Every file under the project folder is a build input,
by extension**, plus every linked resource, minus exclusions, minus the build
directories. Measured in `P7misc`: `extra.c` (containing `#error`) was compiled
the moment it was in the folder, and the build failed; a header in `inc/` is not
an input; `lib/util.c` became `Debug/lib/subdir_rules.mk` and `./lib/util.obj`;
`*.cmd` became `CMD_SRCS` and went on the link line.

Exclusion is CDT's, per configuration:

```xml
<configuration name="Debug" ...>
  ...
  <sourceEntries>
    <entry excluding="extra.c" flags="VALUE_WORKSPACE_PATH|RESOLVED" kind="sourcePath" name=""/>
  </sourceEntries>
</configuration>
```

(`excluding` is `|`-separated project-relative paths.) **This element was added
by hand** to `ccs74/P7misc/.cproject` - projectCreate cannot exclude - and CCS
7.4 accepted it: the next headless build skipped `extra.c`, linked, and left the
file unchanged. `.cproject.as-created` beside it is what CCS wrote.

Extension -> makefile variable (the `*_SRCS` variables `subdir_vars.mk` declares; only `.c`, `.cpp`, `.cmd` were exercised): `.c` `C_SRCS`, `.cpp`
`CPP_SRCS` (also `.cc .cxx .C .c++`), `.asm` `ASM_SRCS`, `.s*` `S*_SRCS`,
`.cmd` `CMD_SRCS`, `.lib/.a` `LIB_SRCS`/`A_SRCS`, `.obj` `OBJ_SRCS`, `.sa`
`SA_SRCS` (linear assembly).

Per-file options (`P7misc`, `-ccs.setCompilerOptions "-O3 --define=PERFILE=1"
@files lib/util.c @configurations Debug`, 7.4 only) are a `fileInfo` holding a
full copy of each overridden option - the DEFINE list repeats `c6747` - so a
file's option is its `fileInfo` option if present, else the folder's:

```
cl6x -mv6740 -O3 ... --define=c6747 --define=PERFILE=1 -g ... --obj_directory="lib" "../lib/util.c"
```

## Macros

| macro | resolved to (measured) | where it appears |
| --- | --- | --- |
| `${ProjName}` | `K6747c` | `artifactName`, `OUTPUT_FILE`, `MAP_FILE`, `XML_LINK_INFO` |
| `${PROJECT_ROOT}` | the project folder, `C:/ti/ccsref/ws7/K6747c` | include paths |
| `${CG_TOOL_ROOT}` | the compiler install for `OPT_CODEGEN_VERSION`: `C:/ti/ccsv7/tools/compiler/ti-cgt-c6000_8.2.2`, `C:/ti/ccsv5/tools/compiler/c6000_7.4.4` (also written as `CG_TOOL_ROOT :=` in the makefile) | include and search paths |
| `${CG_TOOL_CL}` | `"${CG_TOOL_ROOT}/bin/cl6x"` | tool command (definition) |
| `${CG_CLEAN_CMD}` | `DEL /F` | `cleanCommand` |
| `${BuildDirectory}` | `<project>/<Config>` | `builder/@buildPath` |
| `${BuildArtifactFileName}` | `P7misc.out` (the post-build step printed it) | build steps |
| user build variable | `MYLIBDIR` -> `C:/ti/c6747-lib`; `-ccs.defineBuildVariable ... @scope project` wrote a `macros/stringMacro` into **each** configuration | anywhere, e.g. `-i"C:/ti/c6747-lib"` from `${MYLIBDIR}` |

Not produced here but common in imported projects: `${PROJECT_LOC}`,
`${workspace_loc:/Proj/path}`, `${COM_TI_..._INSTALL_DIR}` product paths,
and Eclipse path variables in `.project` `locationURI`. A reader must at least
resolve the first two; `CG_TOOL_ROOT` is where RIDE substitutes its own toolchain.

Build steps: `configuration/@postbuildStep="echo post-build ${BuildArtifactFileName}"`
(written into every configuration without `@configurations`) ran after the link
in `gmake`; `@prebuildStep` is the other.

## The generated makefiles

`<Config>/makefile` sets `CG_TOOL_ROOT`, `ORDERED_OBJS` (the link inputs, in
order), includes `sources.mk`, `subdir_vars.mk` (per folder: `C_SRCS`,
`CPP_SRCS`, `CMD_SRCS`, `OBJS`, deps), `subdir_rules.mk` (one rule and one full
`cl6x` line per source), `objects.mk` (`LIBS := -llibc.a`), and holds the link
rule. `ccsObjs.opt` is the link inputs on one line (`"./main.obj" "./util.obj"
"../C6747.cmd" -llibc.a`). A subfolder gets `<Config>/<dir>/subdir_*.mk`. CCS
runs `gmake -k -j 20 all -O` (7.4) / `gmake -k all` (5.5) from `<Config>/`. The
7.4 makefile also defines a `hex6x` rule, which `all` does not reach.

## CCS 7.4 against CCS 5.5

| | 7.4 | 5.5 |
| --- | --- | --- |
| app names | `createProject`, `buildProject` | `projectCreate`, `projectBuild` |
| default format for C6747 | ELF | COFF |
| option id prefix | `C6000_8.2.` | `C6000_7.4.` |
| `ABI` option | not stored (EABI only) | `ABI.eabi` stored, `--abi=eabi` emitted |
| string/list values | bare: `${ProjName}.out`, `libc.a`, `${CG_TOOL_ROOT}/lib` | wrapped in `"`: `"${ProjName}.out"`, `"libc.a"` |
| default include path | `${PROJECT_ROOT}`, extras, `${CG_TOOL_ROOT}/include` | `${CG_TOOL_ROOT}/include`, extras - no project root |
| where extras go | include and search paths inserted before `${CG_TOOL_ROOT}/include`; a new library **before** `libc.a` | appended at the end, a new library after `libc.a` |
| `.ccsproject` | has `ccsVersion`, `createSlaveProjects`, `filesToOpen` | lacks them |
| `OPT_TAGS CCS_MBS_VERSION` | `6.1.3` | `5.5.0` |
| Hex Utility tool | present (not run) | absent |
| dependency files | `--preproc_dependency="x.d_raw"`, `.d` | `--preproc_dependency="x.pp"` |
| make | `gmake -k -j 20 all -O`; `all` re-invokes make for the `.out` | `gmake -k all`; `all: K6747c.out` |
| per-file options, `-ccs.toolChain`, `-ccs.autoBuild`, hex options | yes | not in its projectCreate |
| `.project` | identical | identical |

## Where the option table comes from

`ti-option-definitions/` holds TI's own definitions, copied from the plugins:

- `v7-C6000_8.2.xml`, `v7-native-C6000_8.2.2.xml` - from
  `C:\ti\ccsv7\eclipse\plugins\com.ti.ccstudio.buildDefinitions.C6000_7.4.0.201711260800\resources\buildDefinitions\`
- `v5-C6000_7.4.xml`, `v5-native-C6000_7.4.4.xml` - from
  `C:\ti\ccsv5\eclipse\plugins\com.ti.ccstudio.buildDefinitions.C6000_5.5.0.201308270800\resources\buildDefinitions\`
- `options-ccs74-C6000_8.2.tsv` (338 options), `options-ccs55-C6000_7.4.tsv`
  (229) - every `<option>`: tool, id, valueType, `command`, `commandFalse`,
  `defaultValue`/`value`, superClass, and each enum value's id suffix and flag
  (the default marked). Made by `logs/opts.py`.

The generic rule, which every row measured above obeys: a boolean emits
`command` when true; a string emits `command` + value (`--heap_size=` + `0x800`;
`-mv` + `6740`); an enumerated emits the chosen `enumeratedOptionValue`'s own
`command`; a list emits `command` + item once per item. The last emits include
and search paths quoted (`--include_path="..."`, `-i"..."`) and defines as
written. An unstored option takes `defaultValue`, or the enum value marked
`isDefault="true"`, or nothing.

## Proposed mapping to our toolchain

cpp11/c90 accepts `-arch tms6747 -O0/-O1/-O2 -g -D -U -I --no_compress -c -S -o`
(read from `src/Driver.cpp`); LNK6x accepts `-mv6740 --abi=eabi <cmd file>
objects -o -m -i -l --heap_size[=] --stack_size[=] --rom_model --ram_model
-e/--entry_point --no_compress` (read from its `src/main.cpp` and usage line).

| CCS option (id tail) | cl6x flag | ours | note |
| --- | --- | --- | --- |
| `SILICON_VERSION` 6740 | `-mv6740` | cpp11 `-arch tms6747`; LNK6x `-mv6740` | any other value: refuse the project |
| `ABI` eabi / format ELF | `--abi=eabi` | implied; LNK6x takes `--abi=eabi` | COFF (`coffabi`, `isElfFormat=false`) unsupported -> refuse |
| `BIG_ENDIAN` | `--big_endian` | unsupported | refuse |
| `OPT_LEVEL` off, 0 | `-Ooff`, `-O0` | `-O0` | |
| `OPT_LEVEL` 1 | `-O1` | `-O1` | |
| `OPT_LEVEL` 2, 3 | `-O2`, `-O3` | `-O2` | cpp11 has no `-O3` |
| `OPT_FOR_SPEED` 0-5 | `--opt_for_speed=n` | unsupported -> RIDE default | |
| `OPT_FOR_SPACE` 0-3 | `-msN` | unsupported -> RIDE default (cpp11 has no size level) | |
| `DEBUGGING_MODEL` DWARF | `-g` | `-g` | skeletal/none/coff -> no `-g` |
| `DEFINE` | `--define=X` | `-DX` (strip the outer `"` CCS keeps inside the value) | `c6747` included |
| `UNDEFINE` | `--undefine=X` | `-UX` | |
| `INCLUDE_PATH` | `--include_path="p"` | `-Ip`, macros resolved | drop `${CG_TOOL_ROOT}/include` -> RIDE's own headers |
| `NO_COMPRESS` | `--no_compress` | `--no_compress` (cpp11 and LNK6x) | |
| `EXCEPTIONS`, `RTTI` | `--exceptions`, `--rtti` | always on in cpp11 -> ignore | runtime must then be the `_eh` library |
| `CPP_DEFAULT` | `--cpp_default` | unsupported -> choose compiler by extension (cpp11 for C++) | |
| `DIAG_*`, `DISPLAY_ERROR_NUMBER`, `PREPROC_*`, `OBJ_DIRECTORY`, `KEEP_ASM` etc. | | ignore | RIDE chooses its own |
| any other compiler option | | unsupported -> RIDE default, listed to the user | |
| `Z` | `-z` | (the linker is LNK6x) | |
| linker command file (`*.cmd` in the tree) | positional | LNK6x first positional argument | TI's C6747.cmd is what LNK6x is measured with |
| `HEAP_SIZE` | `--heap_size=` | LNK6x `--heap_size=` | |
| `STACK_SIZE` | `--stack_size=` | LNK6x `--stack_size=` | |
| `SEARCH_PATH` | `-i"p"` | LNK6x `-ip` | drop `${CG_TOOL_ROOT}/lib`, `/include` -> RIDE's runtime dir |
| `LIBRARY` | `-lname` | LNK6x `-lname` | `libc.a` is an index LNK6x does not read: map it to `rts6740_elf_eh.lib` (cpp11 objects need the `_eh` runtime) |
| `INITIALIZATION_MODEL` | `--rom_model`/`--ram_model` | same | |
| `MAP_FILE` | `-m"x.map"` | LNK6x `-m x.map` | |
| `OUTPUT_FILE` | `-o "x.out"` | LNK6x `-o x.out` | |
| `ENTRY_POINT` | `--entry_point=` | LNK6x `-e`/`--entry_point=` | |
| `REREAD_LIBS`, `WARN_SECTIONS`, `XML_LINK_INFO`, `DIAG_*` | | ignore | |
| compiler options repeated on the link line | | ignore | only the linker tool's options map |
| pre/post-build step | `@prebuildStep`/`@postbuildStep` | run as given, macros resolved | |
