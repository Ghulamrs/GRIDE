#!/usr/bin/env bash
#
# Copies this tree to the Windows box and builds it there.
#
# That box was rebuilt on 2026-08-25 and everything below is about the machine
# as it is now: reached as `ssh windows`, its ssh shell is cmd.exe, and the
# projects are siblings under C:\Users\GRA\source - RIDE (4.0's own
# directory, so the sealed 3.5 tree in RIDE is left alone), VM6747,
# Compiler-Ci, Compiler-Si, C++Optimize, ASM6x, MASM, LINK, LNK6x, Converter-C2S - which is the shape RIDE.sln assumes when
# it names ..\Compilers\Compiler-Ci\ide\cc1.vcxproj and the rest.
#
# Three rules that each cost an hour before they were written down:
#
#   * A tarball, extracted by Windows' own tar. scp of a directory tree at a
#     time left files behind and nobody noticed; one archive is one thing to
#     check. macOS puts ._ AppleDouble files in the archive unless told not to.
#   * A .cmd file, scp'd over and run by its full path, with no `cmd /c` in
#     front of it. The ssh shell is already cmd, and a command line with quotes
#     in it loses one on the way; a script file has no such problem.
#   * The tree there has no git. What this copies is what is built; a stale
#     file on that side is a stale build with a green suite in front of it.
#
# cxx1 travels with the editor since 3.0. RIDE.sln builds it from
# ..\Compilers\C++Optimize\ide\cxx1.vcxproj (the root copies moved to ide/ on 2026-09-30), written at
# the root of the C++ checkout here; the sources, headers, msvc\compat and
# that project go over together, laid over the tree there - never wiping it,
# since that directory also holds hand-run experiments that are not ours.
#
#   ./tools/to-windows.sh              build the solution and run both suites
#   ./tools/to-windows.sh build        the console editor only, no suites
#   ./tools/to-windows.sh gui          also msbuild the window on its own
#   ./tools/to-windows.sh solution     every program the editor drives, no suites
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

BOX="${ED1_WINDOWS_BOX:-windows}"
ROOT="${ED1_WINDOWS_ROOT:-C:\\Users\\GRA\\source}"
DIR="$ROOT\\RIDE-4.7"
# The three compilers are repositories of their own beside VM6747 since
# 2026-10-12, laid out on the box as they are here; VM6747 is the emulator itself.
VM_ROOT="$ROOT\\TI\\VM6747"
CC1I_DIR="$ROOT\\Compilers\\Compiler-Ci"
CXX1_DIR="$ROOT\\Compilers\\C++Optimize"
SHCI_DIR="$ROOT\\Compilers\\Compiler-Si"
EMU_DIR="$VM_ROOT"
ASM_DIR="$ROOT\\TI\\ASM6x"
MASM_DIR="$ROOT\\Windows\\MASM"
LINK_DIR="$ROOT\\Windows\\LINK"
LNK6X_DIR="$ROOT\\TI\\LNK6x"
C2S_DIR="$ROOT\\Compilers\\Converter-C2S"
SIM_DIR="$ROOT\\TI\\SIM6747"
RTS_DIR="$ROOT\\TI\\RTS6x"
WHAT="${1:-check}"
# A directory of its own: to-windows.sh and to-linux.sh name their archives
# alike, and run at once they overwrote each other's (a truncated tar).
TMP=$(mktemp -d "${TMPDIR:-/tmp}/to-windows.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT

say() { printf '%s\n' "$*"; }

# ---- the editor -----------------------------------------------------------
# Built things go by name and suffix: a Mach-O RIDE.exe or tests/test sent over is "newer" than its
# source there. help/ goes for tests/test.cpp's Help > Contents check. shc's runtime goes as 'shmrt-*'
# and not 'lib', which matched every lib/ and dropped docs/ccs-reference/ccs74/P7misc/lib/util.c.
tar --no-mac-metadata \
    --exclude 'obj' --exclude '*.o' --exclude '*.d' --exclude '*.exe' \
    --exclude 'tests/test' --exclude 'tests/session' --exclude '* 2.*' \
    --exclude 'shmrt-*' --exclude 'x64' --exclude 'DerivedData' \
    -czf "$TMP/ride-src.tgz" \
    src tests winforms help tools docs packaging projects programs \
    Makefile workspace.mk build.bat clean.cmd README.md RIDE.pro \
    RIDE.sln RIDEConsole.vcxproj product.props 2>/dev/null || exit 2

# ---- cxx1 ------------------------------------------------------------------
# The parts its Visual Studio project compiles and includes, and nothing of
# its own build tree.
( cd ../Compilers/Compiler-Ci && tar --no-mac-metadata --exclude '* 2.*' --exclude 'obj' --exclude '*.exe' --exclude 'out-*' \
    -czf "$TMP/c90-src.tgz" src lib msvc tests examples Makefile ide README.md ) || exit 2
( cd ../Compilers/C++Optimize && tar --no-mac-metadata --exclude '* 2.*' --exclude 'obj' --exclude '*.exe' --exclude 'out-*' \
    -czf "$TMP/cxx1-src.tgz" src include lib msvc tests Makefile ide README.md ) || exit 2
( cd ../TI/VM6747 && tar --no-mac-metadata --exclude '* 2.*' --exclude '*.exe' \
    -czf "$TMP/vm6747-src.tgz" src msvc tests Makefile vm6747.vcxproj README.md ) || exit 2
# asm6x: the C6000 assembler, beside this checkout as ../TI/ASM6x, its own repository.
( cd ../TI/ASM6x && tar --no-mac-metadata --exclude '* 2.*' --exclude '*.exe' --exclude 'build' \
    -czf "$TMP/asm6x-src.tgz" src tests Makefile asm6x.vcxproj README.md ) || exit 2
# masm: the x86-64 assembler, beside this checkout as ../Windows/MASM, its own repository.
( cd ../Windows/MASM && tar --no-mac-metadata --exclude '* 2.*' --exclude '*.exe' --exclude 'build' \
    -czf "$TMP/masm-src.tgz" src tests Makefile masm.vcxproj README.md ) || exit 2
# link: the x86-64 linker, beside this checkout as ../Windows/LINK, its own repository.
# Not '*.exe' here: tests/ref holds the images link.exe made, which its bed is
# held to, and they are .exe files. build/ is where its own product would be.
( cd ../Windows/LINK && tar --no-mac-metadata --exclude '* 2.*' --exclude 'build' --exclude 'x64' \
    -czf "$TMP/link-src.tgz" src tests Makefile link.vcxproj README.md ) || exit 2
# lnk6x: the C6000 linker, beside this checkout as ../TI/LNK6x, its own repository;
# tests/ref holds the .out images TI's lnk6x made, which its bed is held to.
( cd ../TI/LNK6x && tar --no-mac-metadata --exclude '* 2.*' --exclude 'build' --exclude 'x64' \
    -czf "$TMP/lnk6x-src.tgz" src tests Makefile lnk6x.vcxproj README.md ) || exit 2
# sim6747: the C6747 simulator (5.0), beside this checkout as ../TI/SIM6747; RIDE.sln builds it.
( cd ../TI/SIM6747 && tar --no-mac-metadata --exclude '* 2.*' --exclude '*.exe' --exclude 'build' --exclude 'oracle/ship' \
    -czf "$TMP/sim6747-src.tgz" src tests msvc sim6747.vcxproj Makefile README.md ) || exit 2
# rts6x: the C6747 runtime (5.1), beside this checkout as ../TI/RTS6x; RIDE.sln builds it with build.cmd.
( cd ../TI/RTS6x && tar --no-mac-metadata --exclude '* 2.*' --exclude 'build' \
    -czf "$TMP/rts6x-src.tgz" src tests tools docs Makefile build.cmd printf6x.members rts6x.vcxproj README.md ) || exit 2
# c2s: the converter, beside this checkout as ../Compilers/Converter-C2S; RIDE.sln builds it, so a fresh root needs it.
( cd ../Compilers/Converter-C2S && tar --no-mac-metadata --exclude '* 2.*' --exclude '*.exe' --exclude 'obj' \
    -czf "$TMP/c2s-src.tgz" src tests c2s.vcxproj Makefile README.md ) || exit 2
# shalimar: what ide/shc.vcxproj compiles - src and the runtime it builds beside the
# binary - and nothing built here; lib/ holds this machine's archives.
( cd ../Compilers/Compiler-Si && tar --no-mac-metadata --exclude '* 2.*' --exclude '*.exe' --exclude 'lib' --exclude 'out-*' \
    -czf "$TMP/shalimar-src.tgz" src runtime tests examples Makefile build.bat ide README.md ) || exit 2

say "copying to $BOX:$ROOT"
# One directory per call: in cmd, `if not exist X mkdir X & if ...` makes the
# second `if` part of the first one's body, so it runs only when X was missing.
for d in "$DIR" "$CC1I_DIR" "$CXX1_DIR" "$SHCI_DIR" "$EMU_DIR" "$ASM_DIR" "$MASM_DIR" "$LINK_DIR" "$LNK6X_DIR" "$C2S_DIR" "$SIM_DIR" "$RTS_DIR"; do
  ssh -n "$BOX" "if not exist \"$d\" mkdir \"$d\"" || exit 2
done
scp -q "$TMP/ride-src.tgz" "$BOX:$DIR\\ride-src.tgz" || exit 2
scp -q "$TMP/c90-src.tgz" "$BOX:$CC1I_DIR\\c90-src.tgz" || exit 2
scp -q "$TMP/cxx1-src.tgz" "$BOX:$CXX1_DIR\\cxx1-src.tgz" || exit 2
scp -q "$TMP/vm6747-src.tgz" "$BOX:$EMU_DIR\\vm6747-src.tgz" || exit 2
scp -q "$TMP/asm6x-src.tgz" "$BOX:$ASM_DIR\\asm6x-src.tgz" || exit 2
scp -q "$TMP/masm-src.tgz" "$BOX:$MASM_DIR\\masm-src.tgz" || exit 2
scp -q "$TMP/link-src.tgz" "$BOX:$LINK_DIR\\link-src.tgz" || exit 2
scp -q "$TMP/lnk6x-src.tgz" "$BOX:$LNK6X_DIR\\lnk6x-src.tgz" || exit 2
scp -q "$TMP/c2s-src.tgz" "$BOX:$C2S_DIR\\c2s-src.tgz" || exit 2
scp -q "$TMP/sim6747-src.tgz" "$BOX:$SIM_DIR\\sim6747-src.tgz" || exit 2
scp -q "$TMP/rts6x-src.tgz" "$BOX:$RTS_DIR\\rts6x-src.tgz" || exit 2
scp -q "$TMP/shalimar-src.tgz" "$BOX:$SHCI_DIR\\shalimar-src.tgz" || exit 2

# ---- the script that does the work there -----------------------------------
# One .cmd, generated here so that what runs is what this file says. The
# compilers are named by full path for the suites - the same four make names
# on Unix - and they are the ones the solution just built into bin (via
# /p:OutDir), beside the editor, which is where the editor would find them on
# its own.
BIN="$DIR\\bin"
{
  printf '@echo off\r\n'
  # tests\ is emptied before each archive lands, as to-linux.sh empties it: a
  # case retired or renamed here would otherwise stay there under its old
  # name and be found by whatever globs the directory - which is how the
  # Linux box reported two probes the Mac no longer has (RIDE 19c73e6).
  # Only tests\: the rest is laid over, since these directories also hold
  # hand-run experiments that are not ours.
  for pair in "$DIR ride" "$CC1I_DIR c90" "$CXX1_DIR cxx1" \
              "$EMU_DIR vm6747" "$ASM_DIR asm6x" "$MASM_DIR masm" "$LINK_DIR link" \
              "$LNK6X_DIR lnk6x" "$SHCI_DIR shalimar" "$C2S_DIR c2s" "$SIM_DIR sim6747" "$RTS_DIR rts6x"; do
    set -- $pair
    printf 'cd /d "%s" || exit /b 2\r\n' "$1"
    printf 'if exist tests rmdir /s /q tests\r\n'
    # RIDE's samples are the repository's alone, so they go too: laid over, a sample removed
    # from the tree stayed here and was staged into the installer (K6747c, 03-10-2026).
    if [ "$2" = ride ]; then
      for d in projects programs examples; do printf 'if exist %s rmdir /s /q %s\r\n' "$d" "$d"; done
    fi
    printf 'tar -xzf %s-src.tgz || exit /b 2\r\n' "$2"
    printf 'del /q %s-src.tgz\r\n' "$2"
  done
  printf 'cd /d "%s"\r\n' "$DIR"
  # A TEMP of this root's own: build.bat keeps a fixed file there, and two roots built at once collided on it.
  printf 'if not exist "%s\\tmp" mkdir "%s\\tmp"\r\nset TEMP=%s\\tmp\r\nset TMP=%s\\tmp\r\n' "$DIR" "$DIR" "$DIR" "$DIR"
  printf 'set CC1=%s\\c90.exe\r\n' "$BIN"
  printf 'set CXX1=%s\\cpp11.exe\r\n' "$BIN"
  printf 'set VM6747=%s\\vm6747.exe\r\n' "$BIN"
  printf 'set SHC=%s\\shalimar.exe\r\n' "$BIN"
  printf 'set C2S=%s\\c2s.exe\r\n' "$BIN"
  # For the CCS end-to-end cases (tests/test.cpp, ccsProjectsBuiltAndRun): the C6000 assembler and
  # linker beside the compilers, and the box's exception-handling runtime, which CCS does not ship.
  printf 'set ASM6X=%s\\asm6x.exe\r\n' "$BIN"
  printf 'set LNK6X=%s\\lnk6x.exe\r\n' "$BIN"
  printf 'set C6747_EHLIB=C:\\ti\\c6747-lib\r\n'
  case "$WHAT" in
    build)
      printf 'call build.bat\r\n' ;;
    gui)
      printf 'call build.bat gui\r\n' ;;
    solution)
      printf 'call build.bat solution\r\n' ;;
    *)
      # The solution first - every program the editor drives, into
      # x64\Release - and then the two suites against exactly those.
      printf 'call build.bat solution\r\n'
      printf 'if errorlevel 1 exit /b 1\r\n'
      printf 'call build.bat check\r\n' ;;
  esac
  printf 'exit /b %%errorlevel%%\r\n'
} > "$TMP/ride-run.cmd"
scp -q "$TMP/ride-run.cmd" "$BOX:$DIR\\ride-run.cmd" || exit 2

say "running $DIR\\ride-run.cmd ($WHAT)"
ssh -n "$BOX" "$DIR\\ride-run.cmd"
