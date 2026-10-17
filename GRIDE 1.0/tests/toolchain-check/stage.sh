#!/bin/sh
# Gathers the nine programs into progs/: seven of RIDE's samples and two
# cpp11 cases with recorded output. Run from this directory, then relay
# progs/ with win.sh (Windows box, C:\meas0923\rc) or lin.sh (Linux box, ~/rc).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
RIDE=$HERE/../..; CASES=${CASES:-$RIDE/../toolchain/Compilers/C++Optimize/tests/cases}
rm -rf "$HERE/progs"; mkdir -p "$HERE/progs"
for f in c-demo/greet.c c-demo/main.c c-example/hello.c c-example/counter.c c-example/counter.h; do
    cp "$RIDE/projects/$f" "$HERE/progs/"
done
for f in projectile.c gcd.shl primes.shl rotmat.shl smart.cpp; do
    cp "$RIDE/programs/$f" "$HERE/progs/"
done
for n in class lambda; do cp "$CASES/$n.cpp" "$CASES/$n.expected" "$HERE/progs/"; done
cp "$HERE/list.txt" "$HERE/progs/"
