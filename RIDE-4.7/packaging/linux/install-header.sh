#!/bin/sh
# RIDE @VER@ for Linux x86-64: the editor, the three compilers and the tools they
# drive, in one self-extracting file. The same file for Ubuntu 22.04 and later,
# Debian 12, RHEL/Rocky/Alma 9 and Amazon Linux 2023: the programs need glibc
# 2.34 and libstdc++ from GCC 11 or newer, and nothing else of the distribution.
#
#   sudo sh RIDE-@VER@-linux-x86_64.run                    into /opt/ride-@VER@, commands in /usr/local/bin
#   sh RIDE-@VER@-linux-x86_64.run --prefix ~/ride         no root: commands in ~/.local/bin
#   sh RIDE-@VER@-linux-x86_64.run --help
#
# /opt/ride-@VER@/uninstall.sh takes it all away again. Its own directory, so it
# installs beside an earlier RIDE (4.5 is /opt/ride) and leaves that one alone.
set -eu

VER=@VER@
PREFIX=/opt/ride-$VER
LINKDIR=
LINKS=1
PLAIN=1
USERPREFIX=0
say() { printf '%s\n' "$*"; }
die() { printf 'RIDE installer: %s\n' "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case $1 in
    --prefix) [ $# -ge 2 ] || die "--prefix needs a directory"; PREFIX=$2; USERPREFIX=1; shift 2 ;;
    --prefix=*) PREFIX=${1#--prefix=}; USERPREFIX=1; shift ;;
    --bindir) [ $# -ge 2 ] || die "--bindir needs a directory"; LINKDIR=$2; shift 2 ;;
    --bindir=*) LINKDIR=${1#--bindir=}; shift ;;
    --no-links) LINKS=0; shift ;;
    --no-plain-links) PLAIN=0; shift ;;
    -h|--help)
        say "RIDE $VER installer for Linux x86-64"
        say "  --prefix DIR   install into DIR (default /opt/ride-$VER, which needs root)"
        say "  --bindir DIR   put the command links there (default /usr/local/bin as root,"
        say "                 ~/.local/bin otherwise)"
        say "  --no-links     make no command links"
        say "  --no-plain-links  make only the versioned ones (cpp11-$VER), not cpp11 and the rest"
        exit 0 ;;
    *) die "unknown option '$1' - see --help" ;;
    esac
done

# ---- the machine ---------------------------------------------------------------
[ "$(uname -s)" = Linux ] || die "this installer is for Linux"
[ "$(uname -m)" = x86_64 ] || die "this installer is for x86-64, and this machine is $(uname -m)"
glibc=$(getconf GNU_LIBC_VERSION 2>/dev/null | awk '{print $2}')
[ -n "$glibc" ] || glibc=$(ldd --version 2>&1 | head -1 | grep -o '[0-9][0-9]*\.[0-9][0-9]*$' || true)
if [ -n "$glibc" ]; then
    major=${glibc%%.*}; minor=${glibc#*.}; minor=${minor%%.*}
    if [ "$major" -lt 2 ] || { [ "$major" -eq 2 ] && [ "$minor" -lt 34 ]; }; then
        die "glibc $glibc is too old - RIDE needs 2.34 (Ubuntu 22.04, Debian 12, RHEL 9, Amazon Linux 2023 or later)"
    fi
fi

# ---- where it goes -------------------------------------------------------------
if [ "$(id -u)" != 0 ] && [ "$USERPREFIX" = 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        say "Installing into $PREFIX needs root - asking sudo."
        exec sudo sh "$0" "$@"
    fi
    die "run as root, or give --prefix in your home, e.g. --prefix \$HOME/ride"
fi
if [ -z "$LINKDIR" ]; then
    if [ "$(id -u)" = 0 ]; then LINKDIR=/usr/local/bin; else LINKDIR=$HOME/.local/bin; fi
fi
case $PREFIX in /*) ;; *) PREFIX=$(pwd)/$PREFIX ;; esac
[ "$PREFIX" != / ] || die "--prefix / would be the whole system"

if [ -e "$PREFIX" ]; then
    [ -f "$PREFIX/.ride-installed" ] || die "$PREFIX exists and is not a RIDE installation - choose another --prefix"
    say "Replacing the RIDE $(cat "$PREFIX/.ride-installed") in $PREFIX"
    [ -x "$PREFIX/uninstall.sh" ] && sh "$PREFIX/uninstall.sh" --quiet
fi
mkdir -p "$PREFIX" || die "cannot create $PREFIX"

# ---- the files -----------------------------------------------------------------
line=$(awk '/^__RIDE_PAYLOAD__$/ { print NR + 1; exit }' "$0")
tail -n +"$line" "$0" | tar -xzf - -C "$PREFIX" || die "the archive in this file is damaged"
say "$VER" > "$PREFIX/.ride-installed"

MADE=""
if [ "$LINKS" = 1 ]; then
    mkdir -p "$LINKDIR"
    # link is left out: coreutils already has /usr/bin/link, and PATH order would decide.
    for t in ride:RIDE c90 cpp11 shalimar c2s vm6747 sim6747 asm6x masm lnk6x; do
        name=${t%%:*}; file=${t#*:}; [ "$file" = "$t" ] && file=$t
        for dest in "$LINKDIR/$name-$VER" "$LINKDIR/$name"; do
            [ "$PLAIN" = 0 ] && [ "$dest" = "$LINKDIR/$name" ] && continue
            if [ -e "$dest" ] && [ ! -L "$dest" ]; then say "  kept $dest - it is not ours"; continue; fi
            ln -sf "$PREFIX/bin/$file.exe" "$dest"
            MADE="$MADE $dest"
        done
    done
    [ "$PLAIN" = 1 ] && say "  the plain command names (ride, cpp11, ...) now point at RIDE $VER - --no-plain-links keeps them where they were"
fi

cat > "$PREFIX/uninstall.sh" <<EOF
#!/bin/sh
# Removes RIDE $VER from $PREFIX and the command links it made - a link only
# while it still points into $PREFIX, so a later RIDE's plain names survive.
for l in$MADE; do
    [ -L "\$l" ] || continue
    case "\$(readlink "\$l")" in "$PREFIX"/*) rm -f "\$l" ;; esac
done
rm -rf "$PREFIX"
[ "\${1:-}" = --quiet ] || echo "RIDE removed from $PREFIX"
EOF
chmod 755 "$PREFIX/uninstall.sh"

# ---- the samples, for whoever is installing ------------------------------------
# ~/Documents/RIDE/projects and .../programs are where the editor looks first;
# missing or empty, each is filled from the install's own - never over a file.
owner=${SUDO_USER:-$(id -un)}
home=$(getent passwd "$owner" 2>/dev/null | cut -d: -f6)
[ -n "$home" ] || home=$HOME
for leaf in projects programs; do
    dest=$home/Documents/RIDE/$leaf
    if [ -d "$PREFIX/$leaf" ] && { [ ! -d "$dest" ] || [ -z "$(ls -A "$dest" 2>/dev/null)" ]; }; then
        mkdir -p "$dest" && cp -R "$PREFIX/$leaf/." "$dest/"
        [ "$(id -u)" = 0 ] && chown -R "$owner" "$home/Documents/RIDE" 2>/dev/null || true
        say "  samples in $dest"
    fi
done
# The CCS samples reach a projects folder that already has others, each one it lacks.
if [ -d "$PREFIX/projects/ccs" ]; then
    mkdir -p "$home/Documents/RIDE/projects/ccs"
    for e in "$PREFIX/projects/ccs"/*; do
        [ -e "$home/Documents/RIDE/projects/ccs/${e##*/}" ] || cp -R "$e" "$home/Documents/RIDE/projects/ccs/"
    done
    [ "$(id -u)" = 0 ] && chown -R "$owner" "$home/Documents/RIDE" 2>/dev/null || true
fi

# ---- does it work --------------------------------------------------------------
say "RIDE $VER is in $PREFIX"
[ -n "$MADE" ] && say "  commands in $LINKDIR: ride c90 cpp11 shalimar c2s vm6747 sim6747 asm6x masm lnk6x, each also as <name>-$VER"
case ":$PATH:" in *":$LINKDIR:"*) ;; *) [ -n "$MADE" ] && say "  add $LINKDIR to your PATH to use them by name" ;; esac

if command -v cc >/dev/null 2>&1 && command -v c++ >/dev/null 2>&1; then
    probe=$(mktemp -d)
    printf '#include <cstdio>\nint main() { std::printf("%%d\\n", 6 * 7); return 0; }\n' > "$probe/p.cpp"
    if "$PREFIX/bin/cpp11.exe" "$probe/p.cpp" -o "$probe/p" >/dev/null 2>&1 && [ "$("$probe/p")" = 42 ]; then
        say "  checked: cpp11 compiled and ran a program"
    else
        say "  warning: cpp11 could not build a test program - see '$PREFIX/bin/cpp11.exe --help'"
    fi
    rm -rf "$probe"
else
    say "  The compilers write assembly and hand it to the system's assembler and linker,"
    say "  which this machine does not have yet. Install them with:"
    if command -v apt-get >/dev/null 2>&1; then say "    sudo apt-get install g++"
    elif command -v dnf >/dev/null 2>&1; then say "    sudo dnf install gcc-c++"
    elif command -v yum >/dev/null 2>&1; then say "    sudo yum install gcc-c++"
    else say "    your distribution's gcc and g++"; fi
fi
say "  remove it with: $PREFIX/uninstall.sh"
exit 0
__RIDE_PAYLOAD__
