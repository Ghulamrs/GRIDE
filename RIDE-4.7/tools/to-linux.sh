#!/usr/bin/env bash
#
# Copies this tree and what it drives to the Linux box, builds the workspace
# there and runs every suite.
#
# Three reasons it is worth doing, and only the first is the obvious one.
#
# It is the third machine, and the editor claims to be one editor on all three.
# It is built with real g++ rather than Apple's clang, which is the only thing
# that can say whether the sources are ISO C++14 - Apple's libc++ hands you
# C++17 names under -std=c++14, so a C++17-ism compiles clean on a Mac and
# passes the host suite. And it is where a C++ group goes to g++ rather than to
# clang++ or cl, which is a routing this editor now makes and nowhere else can
# check.
#
# A tarball rather than that box's own clones, which is a deliberate choice
# and not an oversight. A clone can only ever have what has been pushed, and
# this script exists to check what is in the working tree *before* it is
# committed.
#
# 3.5: the compilers are the VM6747 line, and they travel with the editor.
# Before this the script handed the suites ~/build-ws/cc1.exe, a three-target
# cc1 from 2026-08-26 that refuses `-arch tms6747` - so the session suite's
# emulated-target case would have failed there, not skipped, and the emulator
# had never been compiled by g++ at all. Now the repositories are laid out on
# the box exactly as they are here - Compiler-Ci, Compiler-Si, C++Optimize and
# VM6747 beside the editor (the compilers out of VM6747 since 2026-10-12),
# which is the one assumption workspace.mk makes, and it builds them the way it
# builds them on the Mac. The converter goes the same way, since ~/converter
# there is a clone and behind.
#
# Laid over rather than wiped: the box is a t3.nano and every object it can
# keep between runs is minutes it does not spend again. What is excluded by
# name is what was built here - a Mach-O object or binary that travels over is
# "newer" than its source there and reads as a broken toolchain, which is how
# the first run of this script failed. The editor's own directory is emptied
# of everything but obj/ for the same reason it always was: it is where the
# binaries land, and a stale one beside a fresh one is a directory nobody can
# read.
#
# Everything runs inside a memory cgroup, as Compiler-C's ./build does, because
# this box has 419 MB and an uncapped build once took it down to a hypervisor
# power cycle. A capped process dies alone.
#
#   ./tools/to-linux.sh              build the workspace and run every suite
#   ./tools/to-linux.sh build        build and confirm only
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

KEY="${ED1_LINUX_KEY:-$HOME/Documents/Claude/myMorningWalk.pem}"
BOX="${ED1_LINUX_BOX:-ec2-user@52.202.164.123}"
# Its own directory, ~/ride-4.5, well away from ~/RIDE, that box's clone,
# and from ~/ride (the 3.5 and 4.0 relay): this one is this script's to empty.
DIR="${ED1_LINUX_DIR:-ride-4.7}"
WHAT="${1:-check}"
# A directory of its own: to-windows.sh and to-linux.sh name their archives
# alike, and run at once they overwrote each other's (a truncated tar).
TMP=$(mktemp -d "${TMPDIR:-/tmp}/to-linux.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT
SSH=(ssh -n -i "$KEY" "$BOX")

say() { printf '%s\n' "$*"; }

# ---- the editor -----------------------------------------------------------
# help/ goes for tests/test.cpp's Help > Contents check, and tools/ for `make check`'s --check of the
# projects. shc's runtime goes as 'shmrt-*' and not 'lib', which matched every lib/ at any depth and
# dropped docs/ccs-reference/ccs74/P7misc/lib/util.c.
tar --no-mac-metadata \
    --exclude 'obj' --exclude '*.o' --exclude '*.d' --exclude '*.exe' \
    --exclude 'tests/test' --exclude 'tests/session' --exclude '* 2.*' \
    --exclude 'shmrt-*' --exclude 'x64' --exclude 'DerivedData' --exclude 'bin' \
    -czf "$TMP/ride-src.tgz" \
    src tests winforms help tools docs packaging projects programs Makefile workspace.mk README.md 2>/dev/null || exit 2

# ---- what it drives ---------------------------------------------------------
# Each repository's sources, tests and Makefile, and nothing built here.
# c90's lib/ is its C headers and travels; shalimar's lib/ is this machine's
# runtime archives and does not. cpp11's suites want tools/ (the mangling
# oracle, the comment-line policy) and c90's the same.
pack() {  # pack <name> <directory> <what...>
    local name=$1 dir=$2; shift 2
    ( cd "$dir" && tar --no-mac-metadata --exclude '* 2.*' --exclude 'obj' \
        --exclude '*.exe' --exclude 'out-*' --exclude '*.o' --exclude '*.d' \
        --exclude 'DerivedData' --exclude 'build' \
        -czf "$TMP/$name-src.tgz" "$@" ) || exit 2
}
pack c90   ../toolchain/Compilers/Compiler-Ci   src lib tools tests examples Makefile README.md
pack cpp11  ../toolchain/Compilers/C++Optimize src include lib tools tests ide Makefile README.md
pack shalimar   ../toolchain/Compilers/Compiler-Si   src runtime tests examples Makefile README.md
pack vm6747 ../toolchain/TI/VM6747      src tests Makefile README.md
# asm6x's run.sh rechecks the review's probes against the oracle's recorded objects
# (2026-09-19): the three directories it reads travel, without the review's own outputs.
pack asm6x  ../toolchain/TI/ASM6x                src tests Makefile README.md \
    review-probes-2026-09-19/recheck.sh review-probes-2026-09-19/known.txt \
    review-probes-2026-09-19/edge review-probes-2026-09-19/linkcheck review-probes-2026-09-19/fresh \
    review-probes-2026-09-19/labeldiff
# masm: the x86-64 assembler, ml64's objects checked in under tests/enc.
pack masm   ../toolchain/Windows/MASM                 src tests Makefile README.md
# link: the x86-64 linker, link.exe's images checked in under tests/ref - and
# they are .exe files, which pack() drops, so this one is packed by hand.
( cd ../toolchain/Windows/LINK && tar --no-mac-metadata --exclude '* 2.*' --exclude 'build' \
    -czf "$TMP/link-src.tgz" src tests Makefile README.md ) || exit 2
# lnk6x: the C6000 linker, TI's lnk6x's .out images checked in under tests/ref.
pack lnk6x  ../toolchain/TI/LNK6x                src tests Makefile README.md
pack c2s    ../toolchain/Compilers/Converter-C2S        src tests Makefile README.md

# Where they land: the shape workspace.mk assumes, the compilers, VM6747
# and ../toolchain/Compilers/Converter-C2S beside the editor's directory. A function and not an
# array, because the Mac's bash is 3.2 and has no associative arrays.
there() {
    case "$1" in
        c90)   echo 'Compilers/Compiler-Ci' ;;
        cpp11)  echo 'Compilers/C++Optimize' ;;
        shalimar)   echo 'Compilers/Compiler-Si' ;;
        vm6747) echo 'TI/VM6747' ;;
        asm6x)  echo 'TI/ASM6x' ;;
        masm)   echo 'Windows/MASM' ;;
        link)   echo 'Windows/LINK' ;;
        lnk6x)  echo 'TI/LNK6x' ;;
        c2s)    echo 'Compilers/Converter-C2S' ;;
    esac
}
NAMES="c90 cpp11 shalimar vm6747 asm6x masm link lnk6x c2s"

say "copying to $BOX"
dirs=""; for name in $NAMES; do dirs="$dirs ~/$(there $name)"; done
"${SSH[@]}" "mkdir -p ~/$DIR $dirs && cd ~/$DIR && find . -mindepth 1 -maxdepth 1 ! -name obj -exec rm -rf {} +" || exit 2
scp -q -i "$KEY" "$TMP/ride-src.tgz" "$BOX:~/$DIR/" || exit 2
for name in $NAMES; do
    scp -q -i "$KEY" "$TMP/$name-src.tgz" "$BOX:~/$(there $name)/" || exit 2
done

# ---- the script that does the work there -----------------------------------
# One script, generated here so that what runs is what this file says. The
# AppleDouble files are deleted after every extraction because a stray ._foo
# is an untracked file to git and a source to a glob. The build's output goes
# to a log and is shown with the compile lines taken out, so a failure is
# read from make's own status and not from what a pipe let through.
{
    printf '#!/bin/sh\nset -u\n'
    # tests/ is emptied before the archive lands: a case retired here would
    # otherwise stay there and fail as "compiled, and should not have". The
    # review's probe directory goes the same way, for the same reason: a
    # probe renamed here (899673b renamed two) stayed on the box under its
    # old name, with no recorded object beside it, and read as a disagreement.
    printf 'unpack() { cd "$1" && rm -rf tests review-probes-* && tar xzf "$2" && rm -f "$2" && find . -name "._*" -delete || exit 2; }\n'
    printf 'unpack ~/%s ride-src.tgz\n' "$DIR"
    for name in $NAMES; do
        printf 'unpack ~/%s %s-src.tgz\n' "$(there $name)" "$name"
    done
    printf 'cd ~/%s\n' "$DIR"
    printf 'WHAT=%s\n' "$WHAT"
    cat <<'REMOTE'
# The cap. --user works on this box; the fallback is the one Compiler-C's
# ./build makes, and a plain make is said out loud rather than pretended.
if systemd-run --user --scope -q -p MemoryMax=300M true 2>/dev/null; then
    CAPPED="systemd-run --user --scope -q -p MemoryMax=300M"
elif sudo -n true 2>/dev/null; then
    CAPPED="sudo systemd-run --scope -q -p MemoryMax=300M --uid=$(id -u) --gid=$(id -g)"
else
    echo "warning: no cgroup cap available - building UNCAPPED" >&2; CAPPED=""
fi
run() {  # run <log> <make arguments...>
    log=$1; shift
    $CAPPED make -f workspace.mk C2S_DIR="$HOME/Converter-C2S" "$@" > "$log" 2>&1
    rc=$?
    grep -vE '^(clang|g)\+\+ |^ar |^/usr/bin/make|^make\[[0-9]+\]: (Entering|Leaving|Nothing)' "$log"
    return $rc
}
run build.log || { echo "the workspace build failed - see $PWD/build.log"; exit 2; }
[ -x ./bin/RIDE.exe ] || { echo "no bin/RIDE.exe was built"; exit 2; }
if [ "$WHAT" = build ]; then echo "built the workspace"; exit 0; fi
run check.log check
REMOTE
} > "$TMP/ride-run.sh"
scp -q -i "$KEY" "$TMP/ride-run.sh" "$BOX:~/$DIR/ride-run.sh" || exit 2

say "running ~/$DIR/ride-run.sh ($WHAT)"
"${SSH[@]}" "sh ~/$DIR/ride-run.sh"
