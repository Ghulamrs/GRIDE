#!/bin/sh
# Remove everything the four projects build, and nothing they are made of.
#
#   ./clean.sh          clean
#   ./clean.sh -n       say what would go, remove nothing
#
# Why this exists rather than four `make clean`s: a build leaves things in
# places no single Makefile owns - the shared build directory, the suites'
# tests/out-* scratch, the binaries a workspace build put somewhere else - and
# porting a tree means tarring it, where every one of those is dead weight.
#
# **The rule that makes it safe to run anywhere: nothing git tracks is ever
# removed.** That is not caution for its own sake. `Compiler-C/lib` is sixteen
# tracked header files, and `Compiler-C/build` is a tracked *file*, while in
# Compiler-S `lib/` is the runtime archives and is output. The same two names
# mean opposite things one directory apart, so a list of names cannot be
# trusted and the repository is asked instead. A project with no git checkout -
# the Windows box has none - falls back to its .gitignore, and anything not
# matched there is left alone and reported.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)

DRY=0
[ "${1:-}" = "-n" ] && DRY=1

removed=0
kept=0

# Is this path something the repository holds? Answers "yes" for anything git
# tracks, and - when there is no git here - for anything .gitignore does NOT
# cover, which is the same answer erring the same way.
tracked() {
    repo=$1 path=$2
    rel=${path#"$repo"/}
    if [ -e "$repo/.git" ]; then
        git -C "$repo" ls-files --error-unmatch "$rel" >/dev/null 2>&1 && return 0
        # A directory holding tracked files counts as tracked too.
        [ -n "$(git -C "$repo" ls-files -- "$rel" 2>/dev/null)" ] && return 0
        return 1
    fi
    if [ -f "$repo/.gitignore" ]; then
        # No git: keep anything the ignore file does not claim.
        base=$(basename "$rel")
        grep -qE "^/?($rel|$base)/?$" "$repo/.gitignore" && return 1
        return 0
    fi
    return 0
}

drop() {
    repo=$1 path=$2
    [ -e "$path" ] || return 0
    if tracked "$repo" "$path"; then
        echo "  KEPT     ${path#"$root"/}   (the repository holds this)"
        kept=$((kept + 1))
        return 0
    fi
    size=$(du -sh "$path" 2>/dev/null | cut -f1)
    if [ "$DRY" = 1 ]; then
        echo "  would go ${path#"$root"/}   ($size)"
    else
        rm -rf "$path"
        echo "  removed  ${path#"$root"/}   ($size)"
    fi
    removed=$((removed + 1))
}

echo "cleaning under $root"
[ "$DRY" = 1 ] && echo "(dry run - nothing will be removed)"
echo

# The shared object root the four Makefiles default to. One directory, so this
# is one removal rather than four.
if [ -e "$root/build" ]; then
    echo "the shared build directory:"
    drop "$root" "$root/build"
    echo
fi

# The projects RIDE 4.7 builds, named the way workspace.mk names them and overridable the same way, so a
# machine that keeps them elsewhere is reached by naming them: a cleaner that silently cleaned nothing because
# it looked for a name that machine does not use would be worse than no cleaner.
CC1_DIR=${CC1_DIR:-$root/Compilers/Compiler-Ci}
CXX1_DIR=${CXX1_DIR:-$root/Compilers/C++Optimize}
SHC_DIR=${SHC_DIR:-$root/Compilers/Compiler-Si}
VM_DIR=${VM_DIR:-$root/TI/VM6747}
C2S_DIR=${C2S_DIR:-$root/Compilers/Converter-C2S}
ASM_DIR=${ASM_DIR:-$root/TI/ASM6x}
MASM_DIR=${MASM_DIR:-$root/Windows/MASM}
LINK_DIR=${LINK_DIR:-$root/Windows/LINK}
LNK6X_DIR=${LNK6X_DIR:-$root/TI/LNK6x}
ED_DIR=${ED_DIR:-$here}

for repo in "$CC1_DIR" "$CXX1_DIR" "$SHC_DIR" "$VM_DIR" "$C2S_DIR" "$ASM_DIR" "$MASM_DIR" "$LINK_DIR" "$LNK6X_DIR" "$ED_DIR"; do
    if [ ! -d "$repo" ]; then
        echo "  NOT FOUND $repo   (name it with CC1_DIR, CXX1_DIR, SHC_DIR, VM_DIR, C2S_DIR, ASM_DIR, MASM_DIR, LINK_DIR or LNK6X_DIR)"
        continue
    fi
    echo "$(basename "$repo"):"

    # Objects, wherever this project has historically put them.
    drop "$repo" "$repo/obj"

    # The binaries. Named rather than globbed, because a glob over *.exe in a
    # directory that also holds sources is how a clean script becomes a bug.
    for exe in c90.exe cpp11.exe shalimar.exe c2s.exe vm6747.exe asm6x.exe masm.exe link.exe lnk6x.exe \
               RIDE.exe RIDEConsole.exe cc1.exe cxx1.exe shc.exe; do
        drop "$repo" "$repo/$exe"
    done
    drop "$repo" "$repo/bin"
    drop "$repo" "$repo/build"     # ASM6x, LNK6x, LINK and MASM build here; a tracked one is kept

    # Compiler-S's runtime archives. In Compiler-C this same name is source,
    # which `tracked` is what stops.
    drop "$repo" "$repo/lib"

    # What the suites leave. Compiler-C has eight of these.
    for out in "$repo"/tests/out "$repo"/tests/out-*; do
        # cpp11's emit golden is a recorded before-and-after, kept through a clean on purpose.
        case "$out" in *out-emit.golden) continue ;; esac
        drop "$repo" "$out"
    done

    # Built test harnesses.
    for t in tests/test tests/session tests/test.exe tests/session.exe; do
        drop "$repo" "$repo/$t"
    done

    # Xcode's per-user state, which is never worth carrying and is not source.
    for x in "$repo"/*.xcodeproj/xcuserdata "$repo"/*.xcodeproj/project.xcworkspace; do
        drop "$repo" "$x"
    done

    # Debug symbol bundles.
    for d in "$repo"/*.dSYM; do
        drop "$repo" "$d"
    done

    # **macOS "keep both" duplicates.** A copy onto a read-only file can leave
    # `Ast 2.cpp` beside `Ast.cpp`, and those are not merely clutter: a
    # Makefile using $(wildcard src/*.cpp) picks one up, make splits the name
    # on the space, and the build tries to link a program called `Ast`. That
    # happened here, in Converter-C2S/src/s/vendor, and cost a confusing
    # afternoon of linker errors about a missing `_main`.
    #
    # Only ever untracked ones - a real file with a space in its name would be
    # tracked, and this leaves it alone.
    # find, not a glob: `**` is not globstar in plain sh, and the ones that
    # caused the trouble were three directories down.
    find "$repo" -name '* [0-9].*' -not -path '*/.git/*' 2>/dev/null |
    while IFS= read -r dup; do
        [ -e "$dup" ] || continue
        if [ -e "$repo/.git" ] &&
           git -C "$repo" ls-files --error-unmatch "${dup#"$repo"/}" >/dev/null 2>&1; then
            echo "  KEPT     ${dup#"$root"/}   (the repository holds this)"
            continue
        fi
        size=$(du -sh "$dup" 2>/dev/null | cut -f1)
        if [ "$DRY" = 1 ]; then echo "  would go ${dup#"$root"/}   ($size)"
        else rm -rf "$dup"; echo "  removed  ${dup#"$root"/}   ($size)"; fi
    done

    # **Objects lying beside the sources.** This is the mess the object
    # directory was moved out of the checkout to stop making, but older builds
    # left plenty behind - 39 of them on the Linux box, in src/ and runtime/
    # next to the .cpp they came from - and no `obj` removal reaches those.
    #
    # Swept rather than listed, which needs justifying because everything else
    # here is named: the repository is asked first whether it tracks ANY object
    # file, and only if the answer is none does the sweep run. No repository
    # here tracks one, so the sweep can only take build output; if one ever
    # did, this prints and does nothing.
    if [ -e "$repo/.git" ] && [ -n "$(git -C "$repo" ls-files '*.o' '*.d' '*.obj' 2>/dev/null)" ]; then
        echo "  SKIPPED  loose objects in $(basename "$repo")   (it tracks some)"
    else
        n=$(find "$repo" -name '*.o' -o -name '*.d' -o -name '*.obj' 2>/dev/null | wc -l | tr -d ' ')
        if [ "$n" -gt 0 ]; then
            if [ "$DRY" = 1 ]; then
                echo "  would go $n loose object files beside sources"
            else
                find "$repo" \( -name '*.o' -o -name '*.d' -o -name '*.obj' \) -delete 2>/dev/null
                echo "  removed  $n loose object files beside sources"
            fi
            removed=$((removed + 1))
        fi
    fi

    echo
done

# The converter copies Compiler-S's front end in to build its RIDE engine.
[ -d "$C2S_DIR" ] && drop "$C2S_DIR" "$C2S_DIR/rstudio/c2sr/src/core"

echo
if [ "$DRY" = 1 ]; then
    echo "$removed would be removed, $kept kept as source."
else
    echo "$removed removed, $kept kept as source."
fi
