#!/bin/sh
# The macOS installer: one .pkg holding the window and the console.
#
#   /Applications/RIDE <ver>.app   the AppKit window, with every tool it
#                               drives and their headers and runtime inside it
#   /usr/local/ride-<ver>/      the console editor and the same tools, laid
#                               out as the Windows install is: bin include
#                               lib help projects programs (projects/ccs: CCS samples)
#   /usr/local/bin/<tool>-<ver> symbolic links into /usr/local/ride-<ver>/bin,
#   /usr/local/bin/<tool>       and the plain names pointed at this version as
#                               the newest - all but link, which macOS already
#                               has as /usr/bin/link
#
# Its own package identifier, app name and directory: 4.7 installs beside
# 4.5 and 4.51 (com.ghulamrs.ride and ride451, their RIDE apps,
# /usr/local/ride and ride-4.51) and touches none of it but the plain
# command names, which now point here.
#
#   packaging/macos/build-pkg.sh [version]        -> dist/RIDE-<ver>-macos.pkg
#
# The tools are built for macOS 12, the window's own deployment target, into
# dist/mac/bin: the ones in bin/ carry the building Mac's version as their
# minimum and would refuse to start on anything older. Both halves find their
# headers the way a relocated install must - beside the program or one
# directory up - so nothing depends on where this tree was.
#
# Without a Developer ID the package cannot be signed or notarized: another
# Mac asks the user to open it from Finder's context menu the first time.
set -eu

VER=${1:-5.17}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
CPP=${CPP:-$ROOT/../toolchain/Compilers/C++Optimize}
CC=${CC:-$ROOT/../toolchain/Compilers/Compiler-Ci}
OUT=$ROOT/dist
MAC=$OUT/mac
# Staged outside ~/Documents: files written there carry provenance attributes,
# which pkgbuild records as ._ entries and the install puts back on every file.
STAGE=$(mktemp -d "${TMPDIR:-/tmp}/ride-pkg.XXXXXX")
XC=
trap 'rm -rf "$STAGE" ${XC:+"$XC"}' EXIT
say() { printf '%s\n' "$*"; }

say "[1/6] The tools, for macOS 12, into $MAC/bin"
MACOSX_DEPLOYMENT_TARGET=12.0 make -s -C "$ROOT" -f workspace.mk bin BINDIR="$MAC/bin" NOINSTALLER=1 -j8 >/dev/null

say "[2/6] RIDE.app, Release"
# Built outside ~/Documents: iCloud Drive marks a bundle there with Finder info at
# any moment, and Xcode's own CodeSign then refuses the app as carrying detritus.
XC=$(mktemp -d "${TMPDIR:-/tmp}/ride-xcode.XXXXXX")
xcodebuild -quiet -project "$ROOT/macos/Window.xcodeproj" -scheme RIDE -configuration Release \
    -derivedDataPath "$XC" build
APPSRC=$XC/Build/Products/Release/RIDE.app
APPNAME="RIDE $VER.app"
RDIR=ride-$VER
PKGID=com.ghulamrs.ride$(echo "$VER" | tr -d .)

say "[3/6] Staging"
rm -rf "$STAGE"
mkdir -p "$STAGE/Applications" "$STAGE/usr/local/$RDIR" "$STAGE/usr/local/bin"
ditto "$APPSRC" "$STAGE/Applications/$APPNAME"
APP=$STAGE/Applications/$APPNAME/Contents
TOOLS="c90 cpp11 shalimar c2s vm6747 sim6747 asm6x masm link lnk6x"

# The window: the macOS-12 tools beside it, and what they read in Resources -
# cpp11's headers (C++ and the C ones they wrap, one directory, as installed on
# Windows), and lib holding c90's headers beside the Shalimar runtime, since
# c90 and shalimar both look in lib/ beside themselves.
for t in $TOOLS; do cp -p "$MAC/bin/$t.exe" "$APP/MacOS/"; done
rm -rf "$APP/Resources/include" "$APP/Resources/lib" "$APP/MacOS/include" "$APP/MacOS/lib"
ditto "$CPP/include" "$APP/Resources/include"
cp -p "$CPP"/lib/*.h "$APP/Resources/include/"
ditto "$MAC/bin/lib" "$APP/Resources/lib"
cp -p "$CC"/lib/*.h "$APP/Resources/lib/"
ditto "$ROOT/docs/ccs-reference/ti-option-definitions" "$APP/Resources/lib/ccs"   # lib/ccs beside the window
ln -s ../Resources/include "$APP/MacOS/include"
ln -s ../Resources/lib "$APP/MacOS/lib"
# Copies made under ~/Documents carry provenance and Finder attributes codesign refuses.
xattr -cr "$STAGE"
codesign --force --deep --sign - "$STAGE/Applications/$APPNAME"
codesign --verify --deep "$STAGE/Applications/$APPNAME"

# The console, as the Windows install lays itself out.
R=$STAGE/usr/local/$RDIR
mkdir -p "$R/bin"
cp -p "$MAC/bin/RIDE.exe" "$R/bin/"
for t in $TOOLS; do cp -p "$MAC/bin/$t.exe" "$R/bin/"; done
ditto "$MAC/bin/lib" "$R/bin/lib"
ditto "$CPP/include" "$R/include"
cp -p "$CPP"/lib/*.h "$R/include/"
ditto "$CC/lib" "$R/lib"
ditto "$ROOT/help" "$R/help"
ditto "$ROOT/projects" "$R/projects"
ditto "$ROOT/programs" "$R/programs"
# TI's option definitions, read beside the program as on Windows and Linux.
ditto "$ROOT/docs/ccs-reference" "$R/docs/ccs-reference"
for f in "$R"/bin/*.exe; do codesign --force --sign - "$f"; done
# The release record, after every signature: the console's programs, and the window's own executable, whose
# bundle a file added now would break - so the window reads it here, in /usr/local/ride-<ver>/bin.
"$R/bin/RIDE.exe" --release-record "$R/bin" "$APP/MacOS"
for n in ride:RIDE $TOOLS; do
    name=${n%%:*}; file=${n#*:}
    [ "$name" = link ] && continue
    ln -s "../$RDIR/bin/$file.exe" "$STAGE/usr/local/bin/$name-$VER"
    ln -s "../$RDIR/bin/$file.exe" "$STAGE/usr/local/bin/$name"
done

say "[4/6] Checking the staged compilers find their own headers"
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

say "[5/6] The package"
xattr -cr "$STAGE"
COMP=$MAC/component.plist
pkgbuild --analyze --root "$STAGE" "$COMP" >/dev/null
# Installed where it says, not wherever another RIDE.app happens to be found.
python3 - "$COMP" <<'PY'
import plistlib, sys
p = sys.argv[1]
with open(p, 'rb') as f: bundles = plistlib.load(f)
for b in bundles: b['BundleIsRelocatable'] = False
with open(p, 'wb') as f: plistlib.dump(bundles, f)
print('  %d bundle(s), none relocatable: %s' % (len(bundles), ', '.join(b['RootRelativeBundlePath'] for b in bundles)))
PY
# The samples are the package's own, so an upgrade replaces them whole: an installer lays files
# over an earlier install and never removes, which left 4.7's examples/ behind on 03-10-2026.
# The user's copies in ~/Documents/RIDE are not touched.
SCRIPTS=$MAC/scripts; rm -rf "$SCRIPTS"; mkdir -p "$SCRIPTS"
cat > "$SCRIPTS/preinstall" <<EOS
#!/bin/sh
for d in examples projects programs; do
    rm -rf "/usr/local/$RDIR/\$d" "/Applications/$APPNAME/Contents/Resources/\$d"
done
exit 0
EOS
chmod 755 "$SCRIPTS/preinstall"
pkgbuild --root "$STAGE" --component-plist "$COMP" --identifier "$PKGID" \
    --version "$VER" --install-location / --scripts "$SCRIPTS" "$MAC/RIDE-component.pkg" >/dev/null
productbuild --package "$MAC/RIDE-component.pkg" "$OUT/RIDE-$VER-macos.pkg" >/dev/null
if lsbom -s "$(pkgutil --bom "$MAC/RIDE-component.pkg" | head -1)" | grep -q '/\._'; then
    say "  note: the payload carries ._ entries - com.apple.provenance, which macOS puts on"
    say "  every file an agent's shell writes and will not let it remove. Harmless, but a"
    say "  build run from your own Terminal has none."
fi

say "[6/6] Done: $OUT/RIDE-$VER-macos.pkg ($(du -h "$OUT/RIDE-$VER-macos.pkg" | cut -f1))"
