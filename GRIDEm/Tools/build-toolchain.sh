#!/bin/bash
# =============================================================================
#  build-toolchain.sh - builds the eleven tools GRIDEm drives, for this machine,
#  into GRIDEm's own build folder (reference section 6). Nothing is written
#  inside GRIDE1 or the tool repositories:
#    - the sources are GRIDE1's toolchain/ (the copy GRIDEd and GRIDE 2.0 build), cpp11
#      included - its copy there (1.61) is newer than any repository on this Mac -
#      copied into $OUT/src with the same Compilers/ TI/ Windows/ include/ layout;
#    - GRIDE1's copy leaves out the tools' Makefiles, so each comes from the tool's
#      own repository under Developer/Claude (C++Optimize/, Compilers/, TI/, Windows/);
#    - the programs land in $OUT/bin, the headers in $OUT/include and the runtimes in
#      $OUT/lib, laid out as GRIDE's installation is (GRIDE1/docs/Runtime_Design.md 5.3):
#        include/c  include/c++  include/win
#        lib/tms6747/   rts6x[d].lib printf6x[d].lib shmrt6x[d].lib shmrt/ shmrt-debug/
#        lib/shmrt-arm64-darwin[-debug].a   shalimar's host runtime, under the name it looks for
#  `make app` then copies bin/* into GRIDEm.app/Contents/MacOS, include/ and lib/ into
#  Contents/Resources, and links both beside the programs, where c90, cpp11 and shalimar
#  look for them first.
#
#    Tools/build-toolchain.sh            all tools
#    Tools/build-toolchain.sh cpp11 c90  just these (cpp11 c90 shalimar c2s asm6x lnk6x
#                                        sim6747 vm6747 masm link rts6x)
#  Environment: GRIDE1 (default: the parent folder when GRIDEm sits inside GRIDE1, else
#  ../GRIDE1), TOOL_REPOS (default: the folder GRIDE1 is in), OUT (default build/toolchain),
#  JOBS (default the processor count).
# =============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
# GRIDEm inside GRIDE1 (GRIDE1/GRIDEm): GRIDE1 is the parent; beside it (../GRIDE1) as before
if [ -d "$HERE/../toolchain" ] || [ -d "$HERE/../Toolchain" ]; then
  GRIDE1="${GRIDE1:-$HERE/..}"
  TOOL_REPOS="${TOOL_REPOS:-$HERE/../..}"
else
  GRIDE1="${GRIDE1:-$HERE/../GRIDE1}"
  TOOL_REPOS="${TOOL_REPOS:-$HERE/..}"
fi
OUT="${OUT:-$HERE/build/toolchain}"
SRC="$OUT/src"
BIN="$OUT/bin"
LIB="$OUT/lib"
INC="$OUT/include"
JOBS="${JOBS:-$( (sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4) )}"

say() { printf '\n== %s\n' "$*"; }
die() { printf 'build-toolchain: %s\n' "$*" >&2; exit 1; }

# GRIDE1's copy of the toolchain: Toolchain/ (toolchain/ before GRIDEx3)
TC_SRC="$GRIDE1/Toolchain"
[ -d "$TC_SRC" ] || TC_SRC="$GRIDE1/toolchain"
[ -d "$TC_SRC" ] || die "no toolchain at $GRIDE1/Toolchain (set GRIDE1)"
[ -d "$TC_SRC/include/c" ] || die "no shared C headers at $TC_SRC/include/c - a toolchain copy from before the include/ layout?"
mkdir -p "$SRC" "$BIN" "$LIB" "$INC"

# tool word -> tree under toolchain/ (and under TOOL_REPOS)
tree_of() {
  case "$1" in
    c90) echo Compilers/Compiler-Ci ;;      cpp11) echo Compilers/C++Optimize ;;
    shalimar) echo Compilers/Compiler-Si ;; c2s) echo Compilers/Converter-C2S ;;
    asm6x) echo TI/ASM6x ;;  lnk6x) echo TI/LNK6x ;;  rts6x) echo TI/RTS6x ;;
    sim6747) echo TI/SIM6747 ;;  vm6747) echo TI/VM6747 ;;
    masm) echo Windows/MASM ;;  link) echo Windows/LINK ;;
    *) die "unknown tool '$1'" ;;
  esac
}

# where a tool's own repository is: C++Optimize sits in Developer/Claude itself (moved there from
# Compilers/), the others under Compilers/, TI/ and Windows/. Only its Makefile is taken from it.
repo_of() {
  local t; t="$(tree_of "$1")"
  if [ "$1" = cpp11 ] && [ -f "$TOOL_REPOS/C++Optimize/Makefile" ]; then echo "$TOOL_REPOS/C++Optimize"
  else echo "$TOOL_REPOS/$t"; fi
}

# the sources of one tree, refreshed from GRIDE1, with the repository's Makefile beside them
stage() {
  local t; t="$(tree_of "$1")"
  local from="$TC_SRC/$t" to="$SRC/$t" repo; repo="$(repo_of "$1")"
  [ -d "$from" ] || die "$1: no sources at $from"
  [ -f "$repo/Makefile" ] || die "$1: no Makefile at $repo/Makefile (set TOOL_REPOS)"
  mkdir -p "$to"
  # copy (not rsync --delete: nothing is removed from a build folder by this script); a tree's
  # tests/ (cpp11's is three thousand files) is for its own `make test` and is left behind
  (cd "$from" && tar cf - --exclude='*.vcxproj*' --exclude='x64' --exclude='build' --exclude='tests' .) | (cd "$to" && tar xf -)
  cp "$repo/Makefile" "$to/Makefile"
  # GRIDE1's copies are held to cl's warnings (Level4), where a trailing field left to its default
  # is not one; the repositories' Makefiles hold the host build to clang's -Wextra -Werror, under
  # which that is -Wmissing-field-initializers. Relaxed in the staged copy of every Makefile,
  # whichever of its variables carries the host flags; the repositories are not touched.
  printf '\n# appended by GRIDEm/Tools/build-toolchain.sh\nCXXFLAGS += -Wno-missing-field-initializers\nHOSTFLAGS += -Wno-missing-field-initializers\n' >> "$to/Makefile"
  # what a Makefile's `all` needs and GRIDE1's minimal copy left out
  # (a folder GRIDE1 kept part of - RTS6x's tools/ holds only ar6x there - gets the missing files)
  for extra in tools workspace.mk; do
    [ -e "$repo/$extra" ] || continue
    if [ ! -e "$to/$extra" ]; then cp -R "$repo/$extra" "$to/$extra"
    elif [ -d "$repo/$extra" ]; then cp -R -n "$repo/$extra/." "$to/$extra/" 2>/dev/null || true
    fi
  done
}

# the C headers c90 and cpp11 share (include/c, with sys/) and the Windows and POSIX ones (include/win):
# one copy under $SRC, which the compilers' Makefiles bake in as their last resort
stage_include() {
  mkdir -p "$SRC/include"
  (cd "$TC_SRC/include" && tar cf - .) | (cd "$SRC/include" && tar xf -)
}

build_simple() {   # word  -> $BIN/<exe>
  local w="$1" t; t="$(tree_of "$w")"
  say "$w"
  # vm6747's Runtime.cpp includes RTS6x's shortest-double file by its place in the tree
  # (../../RTS6x/src/stdio), so RTS6x's sources are staged before the emulator is built
  [ "$w" = vm6747 ] && [ ! -d "$SRC/TI/RTS6x/src" ] && stage rts6x
  stage "$w"
  make -C "$SRC/$t" -j"$JOBS" BINDIR="$BIN" all
}

# c90 and cpp11: their Makefiles bake in the headers of the tree they are built in - lib/ for the
# C headers, which GRIDE1's copy no longer carries. The shared include/c and include/win stand in
# (INCDIR, and the win directory the Makefiles do not know, appended), cpp11's own include/ for
# the C++ headers. The compilers look beside themselves first (Runtime_Design.md 5.3), so the
# baked paths matter only to a program run out of this build folder.
build_compiler() {
  local w="$1" t; t="$(tree_of "$w")"
  say "$w"
  stage_include
  stage "$w"
  local win="$SRC/include/win" vars=(INCDIR="$SRC/include/c")
  if [ "$w" = cpp11 ]; then
    # C++Optimize's Makefile names its program cxx3; GRIDEm's toolchain.json knows it as cpp11
    vars+=(PROGRAM=cpp11 CXXINCDIR="$SRC/$t/include")
    printf '\n# appended by GRIDEm/Tools/build-toolchain.sh: the Windows and POSIX headers\nCXXFLAGS += -DCXX1_WIN_INCLUDE_DIR=%s\n' "'\"$win\"'" >> "$SRC/$t/Makefile"
    # GRIDE1's copy is C++14 (its cxx1.vcxproj: stdcpp14) where the repository's Makefile, written
    # for the C3++ line, says C++11
    printf 'CXXFLAGS := $(subst -std=c++11,-std=c++14,$(CXXFLAGS))\n' >> "$SRC/$t/Makefile"
  else
    printf '\n# appended by GRIDEm/Tools/build-toolchain.sh: the Windows and POSIX headers\nCXXFLAGS += -DCC1_WIN_INCLUDE_DIR=%s\n' "'\"$win\"'" >> "$SRC/$t/Makefile"
  fi
  make -C "$SRC/$t" -j"$JOBS" BINDIR="$BIN" "${vars[@]}" all
}

build_shalimar() {
  local t; t="$(tree_of shalimar)"
  say "shalimar (compiler, host runtimes, C6747 runtime assembly)"
  stage shalimar
  # The runtime's shortest-double printer moved to RTS6x (docs/Runtime_Design.md 14.4): GRIDE1's copy
  # has no runtime/Shortest.cpp, and shc.vcxproj compiles RTS6x's file into the runtime in its place
  # (ShmShortest). The repository's Makefile still lists runtime/Shortest.cpp, so that file stands in.
  local tree="$SRC/$t"
  if [ ! -f "$tree/runtime/Shortest.cpp" ]; then
    local shortest="$TC_SRC/TI/RTS6x/src/stdio/_gride_shortest_double.cpp"
    [ -f "$shortest" ] || die "shalimar: no runtime/Shortest.cpp and no $shortest"
    cp "$shortest" "$tree/runtime/Shortest.cpp"
  fi
  # The repository's Makefile also predates the runtime's Borrow.cpp and wrappers/ (shc.vcxproj's
  # ShmRuntime items: Failure Numbers Array Console Runtime Borrow wrappers\*), so the staged copy's
  # host runtime list takes them. The staged Makefile alone is changed, never the repository's.
  sed 's#^  runtime/Runtime.cpp$#  runtime/Runtime.cpp runtime/Borrow.cpp $(wildcard runtime/wrappers/*.cpp)#' \
      "$tree/Makefile" > "$tree/Makefile.staged" && mv "$tree/Makefile.staged" "$tree/Makefile"
  grep -q 'runtime/Borrow.cpp' "$tree/Makefile" || die "shalimar: the Makefile's RUNTIME_SOURCES list is not where this script expects it"
  make -C "$tree" -j"$JOBS" BINDIR="$BIN" BUILD="$OUT/obj/Compiler-S" all
  [ -x "$BIN/cpp11.exe" ] || die "shalimar's C6747 runtime needs cpp11 first"
  # The C6747 runtime is assembly cpp11 writes, one flat folder per configuration as shc.vcxproj
  # writes it (lib\tms6747\shmrt, shmrt-debug with Debug.cpp): the ShmRuntime items above, without
  # the shortest-double file, which rts6x.lib carries on that target. Not the Makefile's tms6747
  # target, whose list is the host one and whose folders are the layout before stage 3. The two
  # folders are made whole again first, as the vcxproj does: rts6x packs every .s in them.
  local rt="$LIB/tms6747/shmrt" rtd="$LIB/tms6747/shmrt-debug" f names="Failure Numbers Array Console Runtime Borrow"
  for f in "$tree"/runtime/wrappers/*.cpp; do names="$names wrappers/$(basename "${f%.cpp}")"; done
  rm -rf "$rt" "$rtd"; mkdir -p "$rt" "$rtd"
  for f in $names; do
    "$BIN/cpp11.exe" -S -arch tms6747 -nologo -O2 "$tree/runtime/$f.cpp" -o "$rt/$(basename "$f").s"
    "$BIN/cpp11.exe" -S -arch tms6747 -nologo -O0 -DSHM_DEBUG=1 "$tree/runtime/$f.cpp" -o "$rtd/$(basename "$f").s"
  done
  "$BIN/cpp11.exe" -S -arch tms6747 -nologo -O0 -DSHM_DEBUG=1 "$tree/runtime/Debug.cpp" -o "$rtd/Debug.s"
  echo "shalimar: $(ls "$rt" | wc -l | tr -d ' ') C6747 runtime files in $rt, $(ls "$rtd" | wc -l | tr -d ' ') in $rtd"
}

build_rts6x() {
  local t; t="$(tree_of rts6x)"
  say "rts6x (rts6x, printf6x and the C6747 Shalimar runtime libraries)"
  stage rts6x
  [ -x "$BIN/cpp11.exe" ] && [ -x "$BIN/asm6x.exe" ] || die "rts6x needs cpp11 and asm6x first"
  local rdir="$LIB/tms6747"
  mkdir -p "$rdir"
  make -C "$SRC/$t" -j"$JOBS" BINDIR="$rdir" OBJDIR="$OUT/obj/RTS6x/obj" \
       CPP11="$BIN/cpp11.exe" ASM6X="$BIN/asm6x.exe" all
  # shmrt6x.lib / shmrt6xd.lib: Toolchain.targets' PackObjects - the assembly shalimar's build wrote,
  # through asm6x, packed by ar6x
  local ar6x="$rdir/ar6x.exe"
  [ -x "$ar6x" ] || die "rts6x's build left no ar6x at $ar6x"
  local pair
  for pair in "shmrt:shmrt6x.lib" "shmrt-debug:shmrt6xd.lib"; do
    local asmdir="$rdir/${pair%%:*}" libname="${pair##*:}"
    local objdir="$OUT/obj/RTS6x/${libname%.lib}"
    [ -d "$asmdir" ] || die "no Shalimar C6747 runtime assembly at $asmdir (build shalimar first)"
    mkdir -p "$objdir"
    "$BIN/asm6x.exe" "$asmdir/*.s" -o "$objdir"
    ls "$objdir"/*.obj | sort > "$objdir.list"
    "$ar6x" -r "$rdir/$libname" @"$objdir.list"
    echo "rts6x: $rdir/$libname ($(wc -l < "$objdir.list" | tr -d ' ') members)"
  done
  # ar6x is a build tool, not a runtime: out of the library folder the app ships
  mkdir -p "$OUT/obj/RTS6x" && mv -f "$ar6x" "$OUT/obj/RTS6x/ar6x.exe"
  rm -rf "$rdir/ar6x.exe.dSYM"
}

# shalimar writes its host runtimes into $BIN/lib (shmrt-arm64-darwin[-debug].a), the name it
# looks for in lib/ beside itself; GRIDEm ships lib/ in Resources, linked beside the programs
collect_lib() {
  [ -d "$BIN/lib" ] || return 0
  local a
  for a in "$BIN/lib"/*.a; do [ -f "$a" ] && cp -p "$a" "$LIB/"; done
}

# the headers the app ships, as the installation lays them out: include/c and include/win from
# GRIDE1's shared set, include/c++ from cpp11's own
collect_include() {
  [ -d "$SRC/include/c" ] || return 0
  mkdir -p "$INC/c" "$INC/win"
  (cd "$SRC/include/c" && tar cf - .) | (cd "$INC/c" && tar xf -)
  (cd "$SRC/include/win" && tar cf - .) | (cd "$INC/win" && tar xf -)
  local cxx="$SRC/$(tree_of cpp11)/include"
  if [ -d "$cxx" ]; then
    mkdir -p "$INC/c++"
    (cd "$cxx" && tar cf - .) | (cd "$INC/c++" && tar xf -)
  fi
}

ALL="cpp11 c90 shalimar c2s asm6x lnk6x sim6747 vm6747 masm link rts6x"
WANT="${*:-$ALL}"
for w in $ALL; do
  case " $WANT " in *" $w "*) ;; *) continue ;; esac
  case "$w" in
    cpp11|c90) build_compiler "$w" ;;
    shalimar)  build_shalimar ;;
    rts6x)     build_rts6x ;;
    *)         build_simple "$w" ;;
  esac
done
collect_lib
collect_include

say "done"
ls -l "$BIN" | sed -n '1,40p'
echo "headers in $INC:"; ls "$INC"
echo "runtimes in $LIB:"; ls "$LIB"; [ -d "$LIB/tms6747" ] && ls "$LIB/tms6747"
