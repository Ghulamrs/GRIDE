#!/bin/sh
# The release build, Mac and Linux: every program compiled from a fresh git checkout, the installer last.
# git is the source of record, so nothing of a working tree reaches a release; one run, one set of build times.
#
#   packaging/release.sh [version]    -> <RELEASE_DIR>/<stamp>/RIDE-<ver>/dist/ and RELEASE.txt beside it
#
# What is final is each repository's default branch on GitHub (main or master), never a side branch.
# The three compilers are repositories of their own beside VM6747 since 2026-10-12, so each is taken
# at its own head like the rest; there is no submodule pin left to agree with (D16).
# RELEASE_DIR (default ~/ride-release, outside iCloud's ~/Documents), JOBS (8 on a Mac, 2 on Linux).
set -eu
# GRIDE1: this is RIDE's own release, cloned from its repositories on GitHub (RIDE-4.7 among them) and sealed
# by MASTER.SEAL, which GRIDE 1.0 does not carry. It does not apply inside GRIDE1; GRIDE1 ships through
# installer/windows. RIDE_RELEASE_OUTSIDE_GRIDE1=1 runs it all the same.
[ "${RIDE_RELEASE_OUTSIDE_GRIDE1:-}" = 1 ] || { echo "release.sh: RIDE's own release - it does not apply inside GRIDE1"; exit 2; }

VER=${1:-5.17}
BASE=${RELEASE_DIR:-$HOME/ride-release}
STAMP=$(TZ=Asia/Karachi date +%Y%m%d-%H%M%S)
W=$BASE/$STAMP
HOST=$(uname -s)
if [ "$HOST" = Darwin ]; then JOBS=${JOBS:-8}; else JOBS=${JOBS:-2}; fi
# cpp11 stamps its build with __DATE__/__TIME__ and calls it PST (Pakistan); the Linux box's clock is UTC.
export TZ=Asia/Karachi
say() { printf '%s\n' "$*"; }

# directory  repository - the siblings RIDE.sln and workspace.mk expect.
REPOS="RIDE-$VER:RIDE-4.7
TI/VM6747:VM6747
Compilers/Compiler-Ci:Compiler-Ci
Compilers/Compiler-Si:Compiler-Si
Compilers/C++Optimize:Compiler-Cpp-Optimize
TI/ASM6x:ASM6x
TI/LNK6x:LNK6X
Windows/LINK:LINK
Windows/MASM:MASM
Compilers/Converter-C2S:Converter-C2S
TI/SIM6747:SIM6747
TI/RTS6x:RTS6x"

mkdir -p "$W"
say "RIDE $VER release in $W"
say "[1/3] Fresh checkouts from github.com/Ghulamrs"
# https needs no key, and every repository is public.
GIT="git -c url.https://github.com/.insteadOf=git@github.com: -c advice.detachedHead=false"
branch() { git -C "$1" rev-parse --abbrev-ref HEAD; }
echo "$REPOS" | while IFS=: read -r dir repo; do
    $GIT clone -q "https://github.com/Ghulamrs/$repo.git" "$W/$dir"
    say "  $dir  $(git -C "$W/$dir" rev-parse --short HEAD)  $(branch "$W/$dir")"
done

{
    say "RIDE $VER, built $(date '+%d-%m-%Y %H:%M:%S') PKT on $(hostname) ($HOST) from fresh checkouts"
    echo "$REPOS" | while IFS=: read -r dir repo; do
        say "$dir  $(git -C "$W/$dir" rev-parse HEAD)  $(branch "$W/$dir")  github.com/Ghulamrs/$repo"
    done
} > "$W/RELEASE.txt"

# The sources as sealed, before a line is compiled: a release of code that does not match its seal is refused (T1).
say "[1b] The seals - verify_seals.py, MASTER.SEAL down to every project"
python3 "$W/RIDE-$VER/verify_seals.py" > "$W/SEALS.txt" 2>&1 || {
    tail -30 "$W/SEALS.txt"; say "release.sh: the sources do not match their seals - reseal (tools/seal write, then RIDE's tools/master-seal write), commit, push, and run again"; exit 1; }
say "  $(tail -1 "$W/SEALS.txt")"

say "[2/3] Every program, then the installer - workspace.mk's all"
R=$W/RIDE-$VER
if [ "$HOST" = Darwin ]; then
    # Into dist/mac/bin for macOS 12, which build-pkg.sh then finds already built in this same run.
    MACOSX_DEPLOYMENT_TARGET=12.0 make -C "$R" -f workspace.mk -j"$JOBS" VER="$VER" BINDIR="$R/dist/mac/bin"
    PRODUCT=$R/dist/RIDE-$VER-macos.pkg
else
    make -C "$R" -f workspace.mk -j"$JOBS" VER="$VER"
    PRODUCT=$R/dist/RIDE-$VER-linux-x86_64.run
fi
[ -f "$PRODUCT" ] || { say "release.sh: no $PRODUCT"; exit 1; }
# workspace.mk's installer ran `confirm`, which stops on a MISSING one of the six libraries (S2).
# What was packaged, by SHA-256, in a second table of MASTER.SEAL beside RELEASE.txt (D13).
if [ "$HOST" = Darwin ]; then SHIPPED=$R/dist/mac/bin; else SHIPPED=$R/bin; fi
python3 "$R/tools/master-seal" artefacts "$SHIPPED" --release "RIDE $VER, $(basename "$PRODUCT"), built $(date '+%d-%m-%Y %H:%M:%S') PKT on $(hostname)"
cp "$R/MASTER.SEAL" "$W/MASTER.SEAL"

say "[3/3] Done"
say "  installer : $PRODUCT"
say "  record    : $W/RELEASE.txt"
say "  artefacts : $W/MASTER.SEAL"
say "installer $PRODUCT  $(date -r "$PRODUCT" '+%d-%m-%Y %H:%M:%S') PKT" >> "$W/RELEASE.txt"
