#!/bin/bash
# GRIDEx2's macOS installer: one .pkg with RIDE's macOS editor and the shared toolchain, modelled on
# RIDE's packaging/macos/build-pkg.sh and GRIDE-Installer's packaging/macos/build-pkg.sh.
#
#   /Applications/<APP_NAME>          RIDE's AppKit editor (macos/macos/Window.xcodeproj), with the tools
#                                     beside it in Contents/MacOS and their headers and runtime in Resources
#   /usr/local/gride-<ver>/           the toolchain as the Windows install lays it out: bin (bin/lib the
#                                     runtime libraries) include lib help examples docs
#   /usr/local/bin/<tool>-<ver>       links into /usr/local/gride-<ver>/bin, and the plain names pointed at
#   /usr/local/bin/<tool>             this version - all but link, which macOS has as /usr/bin/link
#
# Two choices in the installer: "Editor" (the app) and "Toolchain" (/usr/local). GRIDEd ("Black") and
# GRIDE 2.0 ("Grey") are .NET Framework WinForms programs, Windows only: they are not in this package.
#
#   installer/macos/build-pkg.sh [version] [--tools-bin DIR] [--app-only] [--out DIR]
#                                [--sign-app IDENTITY] [--sign-pkg IDENTITY] [-j N]
#                                                            -> <out>/GRIDE-<ver>-macos.pkg
#
#   --tools-bin DIR   the tools already built for macOS (c90.exe ... and lib/), instead of building them
#   --app-only        no toolchain: an editor-only package (the editor then finds no compilers)
#   --sign-app ID     codesign identity for the app and the tools (default: ad hoc, "-")
#   --sign-pkg ID     a "Developer ID Installer" identity for productbuild (default: unsigned)
#
# Everything is built in a temporary copy laid out as RIDE's workspace expects - RIDE/ (GRIDEx2/macos)
# beside Compilers/ TI/ Windows/ (GRIDEx2/toolchain) - so the projects' ../ paths resolve and nothing
# is written into the working copy. Staged outside ~/Documents: files written there carry provenance
# attributes, which pkgbuild records as ._ entries.
#
# Written on Windows and NOT run: see installer/macos/README.md for what is untested.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)                 # GRIDEx2
VER=1.1.0
TOOLS_BIN=
APP_ONLY=0
OUT=$ROOT/dist
SIGN_APP=-
SIGN_PKG=
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
while [ $# -gt 0 ]; do
    case $1 in
    --tools-bin) TOOLS_BIN=$(cd "$2" && pwd); shift 2 ;;
    --app-only) APP_ONLY=1; shift ;;
    --out) OUT=$2; shift 2 ;;
    --sign-app) SIGN_APP=$2; shift 2 ;;
    --sign-pkg) SIGN_PKG=$2; shift 2 ;;
    -j) JOBS=$2; shift 2 ;;
    -h|--help) sed -n '2,28p' "$0"; exit 0 ;;
    -*) echo "build-pkg.sh: unknown option $1" >&2; exit 2 ;;
    *) VER=$1; shift ;;
    esac
done

say() { printf '%s\n' "$*"; }
die() { printf 'build-pkg.sh: %s\n' "$*" >&2; exit 1; }
[ "$(uname -s)" = Darwin ] || die "xcodebuild, pkgbuild and productbuild are macOS's - run this on a Mac"
for t in xcodebuild pkgbuild productbuild codesign ditto rsync make plutil; do
    command -v "$t" >/dev/null 2>&1 || die "no $t - install Xcode and its command-line tools (xcode-select --install)"
done

COMPACT=$(echo "$VER" | tr -d .)
PRODUCT=GRIDE
APP_NAME=${APP_NAME:-GRIDE Editor $VER.app}
RDIR=gride-$VER
PKGID=com.ghulamrs.gride$COMPACT
TOOLS="c90 cpp11 shalimar c2s vm6747 sim6747 asm6x masm link lnk6x"
RTS_LIBS="rts6x.lib rts6xd.lib printf6x.lib printf6xd.lib shmrt6x.lib shmrt6xd.lib"
TC=$ROOT/toolchain
CPP=Compilers/C++Optimize
CC1=Compilers/Compiler-Ci

OUT=$(mkdir -p "$OUT" && cd "$OUT" && pwd)
MAC=$OUT/mac
rm -rf "$MAC"; mkdir -p "$MAC"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/gride-work.XXXXXX")
XC=$(mktemp -d "${TMPDIR:-/tmp}/gride-xcode.XXXXXX")
STAGE=$(mktemp -d "${TMPDIR:-/tmp}/gride-pkg.XXXXXX")
trap 'rm -rf "$WORK" "$XC" "$STAGE"' EXIT
LOG=$MAC/build.log; : > "$LOG"

# ---- what the macOS toolchain build needs, and GRIDEx2's toolchain/ does not carry -------------------
# RIDE builds its tools for the Mac with make (workspace.mk calls each tool tree's own Makefile), not with
# the tool .xcodeproj files; toolchain/MANIFEST.md says those Makefiles were left out of the copy.
MAKEFILES="Compilers/Compiler-Ci/Makefile Compilers/C++Optimize/Makefile Compilers/Compiler-Si/Makefile
Compilers/Converter-C2S/Makefile TI/VM6747/Makefile TI/ASM6x/Makefile TI/LNK6x/Makefile TI/SIM6747/Makefile
TI/RTS6x/Makefile Windows/MASM/Makefile Windows/LINK/Makefile TI/RTS6x/tools/provenance"
# The console editor (Editor.xcodeproj) depends on these ten projects by relative path.
XCODEPROJS="Compilers/Compiler-Ci/ide/cc1.xcodeproj Compilers/C++Optimize/ide/cxx1.xcodeproj
Compilers/Compiler-Si/ide/shc.xcodeproj Compilers/Converter-C2S/c2s.xcodeproj TI/VM6747/vm6747.xcodeproj
TI/ASM6x/asm6x.xcodeproj TI/LNK6x/lnk6x.xcodeproj TI/SIM6747/sim6747.xcodeproj Windows/MASM/masm.xcodeproj
Windows/LINK/link.xcodeproj"
missing_from() { for f in $1; do [ -e "$TC/$f" ] || printf '  toolchain/%s\n' "$f"; done; }

say "[0/6] The work tree: RIDE/ beside Compilers/ TI/ Windows/, in $WORK"
rsync -a --exclude .DS_Store --exclude xcuserdata --exclude DerivedData "$ROOT/macos/" "$WORK/RIDE/"
for d in Compilers TI Windows; do
    rsync -a --exclude .DS_Store --exclude 'build/' --exclude 'x64/' --exclude 'obj/' "$TC/$d/" "$WORK/$d/"
done

# ---- a. the editor app, with xcodebuild ------------------------------------------------------------------
say "[1/6] The editor app (macos/Window.xcodeproj, scheme RIDE, Release)"
xcodebuild -quiet -project "$WORK/RIDE/macos/Window.xcodeproj" -scheme RIDE -configuration Release \
    -derivedDataPath "$XC" build >>"$LOG" 2>&1 || { tail -40 "$LOG"; die "the app did not build (log: $LOG)"; }
APPSRC=$XC/Build/Products/Release/RIDE.app
[ -d "$APPSRC" ] || die "xcodebuild reported success but left no $APPSRC"

# The console editor: Editor.xcodeproj, when the tool projects it depends on are there.
CONSOLE=
if [ -z "$(missing_from "$XCODEPROJS")" ]; then
    say "      and the console editor (Editor.xcodeproj, target RIDE.exe)"
    xcodebuild -quiet -project "$WORK/RIDE/Editor.xcodeproj" -target RIDE.exe -configuration Release \
        SYMROOT="$XC/editor" OBJROOT="$XC/editor" build >>"$LOG" 2>&1 \
        || { tail -40 "$LOG"; die "the console editor did not build (log: $LOG)"; }
    CONSOLE=$WORK/RIDE/RIDE.exe     # its own build phase puts it in SRCROOT
else
    say "      no console editor: Editor.xcodeproj depends on tool projects GRIDEx2 lacks:"
    missing_from "$XCODEPROJS"
fi

# ---- b. the toolchain, for macOS 12, the way RIDE's workspace.mk builds it ---------------------------
B=$MAC/bin
if [ "$APP_ONLY" = 1 ]; then
    say "[2/6] The toolchain: left out (--app-only)"
elif [ -n "$TOOLS_BIN" ]; then
    say "[2/6] The toolchain: taken from $TOOLS_BIN"
    mkdir -p "$B"; ditto "$TOOLS_BIN" "$B"
else
    say "[2/6] The toolchain, for macOS 12, into $B"
    MISSING=$(missing_from "$MAKEFILES")
    if [ -n "$MISSING" ]; then
        say "  GRIDEx2's toolchain/ has no macOS build files. Missing:"
        printf '%s\n' "$MISSING"
        say "  (RIDE's own copies are in the tool repositories, e.g. My Documents\\GRIDEdworking\\<tree>\\Makefile.)"
        die "add them to toolchain/, or pass --tools-bin DIR (tools built elsewhere) or --app-only"
    fi
    W=$WORK
    mk() { MACOSX_DEPLOYMENT_TARGET=12.0 make -s -j"$JOBS" "$@" >>"$LOG" 2>&1 || { tail -40 "$LOG"; die "make $* failed (log: $LOG)"; }; }
    # The recipes of RIDE's workspace.mk, in its order: shalimar after cpp11, rts6x after cpp11 and asm6x.
    mk -C "$W/$CC1" BINDIR="$B" OBJDIR="$B/obj/cc1"
    mk -C "$W/$CPP" BINDIR="$B" OBJDIR="$B/obj/cxx1"
    mk -C "$W/Compilers/Compiler-Si" BINDIR="$B" BUILD="$B/obj/shc" all tms6747 CXX1="$B/cpp11.exe"
    mk -C "$W/Compilers/Converter-C2S" BINDIR="$B" OBJDIR="$B/obj/c2s"
    mk -C "$W/TI/VM6747" BINDIR="$B" OBJDIR="$B/obj/vm6747"
    mk -C "$W/TI/ASM6x" BINDIR="$B" OBJDIR="$B/obj/asm6x"
    mk -C "$W/Windows/MASM" BINDIR="$B" OBJDIR="$B/obj/masm"
    mk -C "$W/Windows/LINK" BINDIR="$B" OBJDIR="$B/obj/link"
    mk -C "$W/TI/LNK6x" BINDIR="$B" OBJDIR="$B/obj/lnk6x"
    mk -C "$W/TI/SIM6747" BINDIR="$B" OBJDIR="$B/obj/sim6747" TARGET="$B/sim6747.exe"
    mk -C "$W/TI/RTS6x" CPP11="$B/cpp11.exe" ASM6X="$B/asm6x.exe" OBJDIR="$B/obj/rts6x" BINDIR="$B/obj/rts6x-bin"
    mkdir -p "$B/lib/rts6x-tms6747"
    for l in rts6x.lib printf6x.lib rts6xd.lib printf6xd.lib; do cp "$B/obj/rts6x-bin/$l" "$B/lib/rts6x-tms6747/"; done
    for v in shmrt6x:shmrt-tms6747 shmrt6xd:shmrt-tms6747-debug; do
        lib=${v%%:*}; dir=${v#*:}
        rm -rf "$B/obj/$lib"; mkdir -p "$B/obj/$lib"
        "$B/asm6x.exe" "$B/lib/$dir/*.s" -o "$B/obj/$lib/" >>"$LOG" 2>&1 || die "asm6x on $dir failed (log: $LOG)"
        rm -f "$B/lib/rts6x-tms6747/$lib.lib"
        "$B/obj/rts6x-bin/ar6x.exe" -r "$B/lib/rts6x-tms6747/$lib.lib" "$B/obj/$lib"/*.obj >>"$LOG" 2>&1 || die "ar6x $lib failed"
    done
    rm -rf "$B/obj"
fi
if [ "$APP_ONLY" = 0 ]; then
    # RIDE's `confirm`: nothing the editor drives may be missing.
    for t in $TOOLS; do [ -f "$B/$t.exe" ] || die "no $B/$t.exe"; done
    for l in $RTS_LIBS; do [ -f "$B/lib/rts6x-tms6747/$l" ] || die "no $B/lib/rts6x-tms6747/$l"; done
fi

# ---- c. staging, as RIDE's macOS package lays itself out ----------------------------------------------
say "[3/6] Staging"
SA=$STAGE/editor; ST=$STAGE/toolchain
mkdir -p "$SA/Applications" "$ST/usr/local/$RDIR" "$ST/usr/local/bin"
ditto "$APPSRC" "$SA/Applications/$APP_NAME"
APP=$SA/Applications/$APP_NAME/Contents
R=$ST/usr/local/$RDIR

if [ "$APP_ONLY" = 0 ]; then
    # The app: the tools beside it; cpp11's headers (C++ and the C ones they wrap) in Resources/include;
    # Resources/lib holding c90's headers beside the runtime libraries, as RIDE's app carries them.
    for t in $TOOLS; do cp -p "$B/$t.exe" "$APP/MacOS/"; done
    rm -rf "$APP/Resources/include" "$APP/Resources/lib" "$APP/MacOS/include" "$APP/MacOS/lib"
    ditto "$WORK/$CPP/include" "$APP/Resources/include"
    cp -p "$WORK/$CPP"/lib/*.h "$APP/Resources/include/"
    ditto "$B/lib" "$APP/Resources/lib"
    cp -p "$WORK/$CC1"/lib/*.h "$APP/Resources/lib/"
    ln -s ../Resources/include "$APP/MacOS/include"
    ln -s ../Resources/lib "$APP/MacOS/lib"

    # The toolchain tree.
    mkdir -p "$R/bin"
    for t in $TOOLS; do cp -p "$B/$t.exe" "$R/bin/"; done
    if [ -n "$CONSOLE" ] && [ -f "$CONSOLE" ]; then cp -p "$CONSOLE" "$R/bin/RIDE.exe"; fi
    ditto "$B/lib" "$R/bin/lib"
    ditto "$WORK/$CPP/include" "$R/include"
    cp -p "$WORK/$CPP"/lib/*.h "$R/include/"
    ditto "$WORK/$CC1/lib" "$R/lib"
    ditto "$ROOT/macos/help" "$R/help"
    # GRIDEx3's one examples folder, the one the Windows Setup installs: projects/ (cppparse among them) and programs/.
    mkdir -p "$R/examples"
    rsync -a --exclude .DS_Store --exclude 'x64/' --exclude 'obj/' --exclude 'bin/' --exclude '.vs/' \
        --exclude 'sandbox/' --exclude '.gride/' --exclude '*.user' "$ROOT/examples/" "$R/examples/"
    mkdir -p "$R/docs"
    for f in "$ROOT"/docs/*_Reference.html "$ROOT/docs/TOOLCHAIN-FORMAT.md" \
             "$ROOT/toolchain/MANIFEST.md" "$HERE/README.md"; do
        if [ -f "$f" ]; then cp -p "$f" "$R/docs/"; fi
    done
    cp -p "$ROOT/README.md" "$R/docs/GRIDEx2-README.md"
    for f in "$R"/bin/*.exe; do codesign --force --sign "$SIGN_APP" "$f"; done
    for t in $TOOLS; do
        [ "$t" = link ] && continue
        ln -s "../$RDIR/bin/$t.exe" "$ST/usr/local/bin/$t-$VER"
        ln -s "../$RDIR/bin/$t.exe" "$ST/usr/local/bin/$t"
    done
    if [ -f "$R/bin/RIDE.exe" ]; then
        ln -s "../$RDIR/bin/RIDE.exe" "$ST/usr/local/bin/ride-$VER"
        ln -s "../$RDIR/bin/RIDE.exe" "$ST/usr/local/bin/ride"
    fi
fi
# Copies made under ~/Documents carry provenance and Finder attributes codesign refuses.
xattr -cr "$STAGE"
codesign --force --deep --sign "$SIGN_APP" "$SA/Applications/$APP_NAME"
codesign --verify --deep "$SA/Applications/$APP_NAME"
# RIDE's release record, after every signature: written into the toolchain's bin, not into the app.
if [ -f "$R/bin/RIDE.exe" ]; then "$R/bin/RIDE.exe" --release-record "$R/bin" "$APP/MacOS"; fi

say "[4/6] Checking the staged compilers find their own headers"
if [ "$APP_ONLY" = 1 ]; then
    say "  skipped (--app-only)"
else
    PROBE=$MAC/probe; rm -rf "$PROBE"; mkdir -p "$PROBE"
    printf '#include <stdio.h>\nint main(void) { printf("c %%d\\n", 6 * 7); return 0; }\n' > "$PROBE/p.c"
    printf '#include <string>\n#include <cstdio>\nint main() { std::string s("cpp"); std::printf("%%s %%d\\n", s.c_str(), 6 * 7); return 0; }\n' > "$PROBE/p.cpp"
    printf 'fun <> = main() {\n  ? 42\n}\n' > "$PROBE/p.shl"
    check() {  # the program, its source, what it must print
        "$1" "$PROBE/$2" -o "$PROBE/a.out" >/dev/null 2>"$PROBE/err" || { say "  $1 $2: refused"; cat "$PROBE/err"; exit 1; }
        got=$("$PROBE/a.out" | tr -d ' ')
        [ "$got" = "$3" ] || { say "  $1 $2: printed '$got', wanted '$3'"; exit 1; }
        say "  ok  $(basename "$1") $2"
    }
    for where in "$APP/MacOS" "$R/bin"; do
        check "$where/c90.exe" p.c "c42"
        check "$where/cpp11.exe" p.cpp "cpp42"
        check "$where/shalimar.exe" p.shl "42"
    done
    rm -rf "$PROBE"
fi

# ---- d. the packages: two components and one product --------------------------------------------------
say "[5/6] The packages"
xattr -cr "$STAGE"
SCRIPTS=$MAC/scripts
# The samples are the package's own, so an upgrade replaces them whole: an installer lays files over an
# earlier install and never removes. The user's own copies are not touched.
mkdir -p "$SCRIPTS/editor" "$SCRIPTS/toolchain"
cat > "$SCRIPTS/editor/preinstall" <<EOS
#!/bin/sh
for d in projects programs; do rm -rf "/Applications/$APP_NAME/Contents/Resources/\$d"; done
exit 0
EOS
cat > "$SCRIPTS/toolchain/preinstall" <<EOS
#!/bin/sh
rm -rf "/usr/local/$RDIR/examples"
exit 0
EOS
chmod 755 "$SCRIPTS/editor/preinstall" "$SCRIPTS/toolchain/preinstall"

COMP=$MAC/editor-component.plist
pkgbuild --analyze --root "$SA" "$COMP" >/dev/null
# Installed where it says, not wherever another bundle with RIDE's bundle identifier happens to be found.
n=0
while plutil -extract "$n.BundleIsRelocatable" raw "$COMP" >/dev/null 2>&1; do
    plutil -replace "$n.BundleIsRelocatable" -bool NO "$COMP"; n=$((n + 1))
done
pkgbuild --root "$SA" --component-plist "$COMP" --identifier "$PKGID.editor" --version "$VER" \
    --install-location / --scripts "$SCRIPTS/editor" "$MAC/editor.pkg" >/dev/null
CHOICES='        <line choice="editor"/>'
REFS="    <pkg-ref id=\"$PKGID.editor\" version=\"$VER\" onConclusion=\"none\">editor.pkg</pkg-ref>"
if [ "$APP_ONLY" = 0 ]; then
    pkgbuild --root "$ST" --identifier "$PKGID.toolchain" --version "$VER" \
        --install-location / --scripts "$SCRIPTS/toolchain" "$MAC/toolchain.pkg" >/dev/null
    CHOICES="$CHOICES
        <line choice=\"toolchain\"/>"
    REFS="$REFS
    <pkg-ref id=\"$PKGID.toolchain\" version=\"$VER\" onConclusion=\"none\">toolchain.pkg</pkg-ref>"
fi

DIST=$MAC/distribution.xml
cat > "$DIST" <<EOX
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$PRODUCT $VER - editor and toolchain</title>
    <options customize="allow" require-scripts="false" rootVolumeOnly="true" hostArchitectures="$(uname -m)"/>
    <domains enable_localSystem="true" enable_currentUserHome="false" enable_anywhere="false"/>
    <volume-check>
        <allowed-os-versions><os-version min="12.0"/></allowed-os-versions>
    </volume-check>
    <choices-outline>
$CHOICES
    </choices-outline>
    <choice id="editor" title="Editor" visible="true" start_selected="true"
            description="RIDE's macOS editor, in /Applications/$APP_NAME, with the compilers and runtime inside it.">
        <pkg-ref id="$PKGID.editor"/>
    </choice>
    <choice id="toolchain" title="Toolchain" visible="true" start_selected="true"
            description="c90, cpp11, shalimar, c2s, asm6x, lnk6x, rts6x, sim6747, vm6747, masm and link for the command line, in /usr/local/$RDIR, with help, examples and docs; commands in /usr/local/bin.">
        <pkg-ref id="$PKGID.toolchain"/>
    </choice>
$REFS
</installer-gui-script>
EOX
if [ "$APP_ONLY" = 1 ]; then
    # No toolchain component: drop its choice, which would name a package that is not there.
    awk '/<choice id="toolchain"/ { skip = 1 } !skip { print } skip && /<\/choice>/ { skip = 0 }' "$DIST" > "$DIST.tmp"
    mv "$DIST.tmp" "$DIST"
fi
PRODUCT_PKG=$OUT/$PRODUCT-$VER-macos.pkg
rm -f "$PRODUCT_PKG"
productbuild --distribution "$DIST" --package-path "$MAC" ${SIGN_PKG:+--sign "$SIGN_PKG"} "$PRODUCT_PKG" >/dev/null
if lsbom -s "$(pkgutil --bom "$MAC/editor.pkg" | head -1)" | grep -q '/\._'; then
    say "  note: the payload carries ._ entries (com.apple.provenance); harmless, but a build run from"
    say "  your own Terminal has none."
fi

say "[6/6] Done: $PRODUCT_PKG ($(du -h "$PRODUCT_PKG" | cut -f1)); build log $LOG"
