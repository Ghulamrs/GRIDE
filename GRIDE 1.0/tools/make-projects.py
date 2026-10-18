#!/usr/bin/env python3
"""Writes the project files that build ed1, cc1 and shc together.

    python3 tools/make-projects.py            write them
    python3 tools/make-projects.py --check    say whether they are current

Three machines, three shapes, one idea: open one thing and get all four
programs, with the editor built after the three it drives.

    macOS    RIDE.xcworkspace          RIDE.exe, c90.exe, cpp11.exe, vm6747.exe, asm6x.exe, masm.exe, link.exe, lnk6x.exe, shalimar.exe, c2s.exe
    Windows  RIDE.sln                  RIDEConsole, RIDEGui, c90, cpp11, vm6747, asm6x, masm, link, lnk6x, shalimar, c2s
    Linux    workspace.mk                 make -f workspace.mk

Was make-xcodeproj.py while Xcode was all it wrote.

Three command line tools, built by clang++, from three separate repositories:

    RIDE  this editor         RIDE/Editor.xcodeproj
    cc1      the C compiler      ../Toolchain/Compilers/Compiler-Ci/ide/cc1.xcodeproj    (its own)
    cxx1     the C++ compiler    ../Toolchain/Compilers/C++Optimize/ide/cxx1.xcodeproj (its own)
    shc      the Shalimar one    ../Toolchain/Compilers/Compiler-Si/ide/shc.xcodeproj    (its own)

Since 2026-10-12 the compilers are repositories of their own beside this one -
../Toolchain/Compilers/Compiler-Ci, ../Toolchain/Compilers/C++Optimize and ../Toolchain/Compilers/Compiler-Si, building c90, cpp11 and
shalimar - and the emulator is ../Toolchain/TI/VM6747. The three originals stay sealed and are not opened by
anything written here.
    c2s      the converter       ../Toolchain/Compilers/Converter-C2S/c2s.xcodeproj

cxx1 joined in 3.0; since 3.5 it is cpp11, built from Compilers/C++Optimize
on every machine. Its IDE projects are its own, in its ide/, and the
solution and the workspace name them there.

c2s is the odd one of the four: the editor runs it but does not compile with
it. It converts C89 to Shalimar and back, and the editor's Language menu has
two items that put it over the open file. It is in the group for the same
reason the compilers are - the editor looks for what it drives beside itself,
so all four have to be built into one place.

and RIDE.xcworkspace, which opens all four at once so that a change to a
compiler and the change to the editor that goes with it are one build and one
issue list.

**Each project is generated from that repository's own Makefile.** A hand-kept
project drifts: someone adds a file to the Makefile, forgets the project, and
Xcode quietly builds yesterday's program - which is not an error, just a
smaller program, so nothing says so. That has now happened twice here. Once
when sources were added and this was not re-run, and once when this script
stopped reading a Makefile variable that had been added to it. --check is the
answer to both: it rebuilds every project in memory and compares.

**Three projects are checked rather than written.** winforms/RIDEGui.vcxproj is
kept by hand, because what is in it besides the source list cannot be derived
from a Makefile: it compiles one file managed and every other file native. cc1's
and cxx1's are their own, in each compiler's ide/, written by its ide/generate.py
and asked with --check (2026-09-30; RIDE wrote copies into the compilers until
then). The window's source list is compared against the Makefile all the same - that is the part
that drifts, and the window's had drifted the whole time nobody was checking
it. --check also insists the Makefile is made of the variables named here and
no others, so adding one and forgetting this script fails loudly.

The identifiers are derived from the file names and the product, so
regenerating an unchanged project produces a byte-identical file and the
comparison is exact.

A project is written into the repository it belongs to, so its paths are
relative to that repository and mean something inside it. The workspace lives
here and reaches the other three with ../ - which is the one thing in this
that assumes all four are checked out side by side.
"""

import hashlib
import os
import subprocess
import re
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIBLINGS = os.path.dirname(HERE)
# In GRIDE1 the tools are not siblings of this checkout: they are GRIDE1's shared toolchain, ../toolchain.
SIBLINGS = os.path.join(SIBLINGS, "toolchain")
# 3.5: the C and C++ compilers are the VM6747 line - c90 and cpp11, three
# host targets and the TMS320C6747 - and vm6747, the emulator, is built with
# them. Each its own repository beside this one (VM6747 is the emulator).
CC1_REPO = "Compilers/Compiler-Ci"
CXX1_REPO = "Compilers/C++Optimize"
VM_REPO = "TI/VM6747"
# The C6000 assembler, its own repository beside this one: asm6x turns the
# tms6747 assembly into TI objects, and lnk6x - CCS's, where Tools names it -
# links them into a real .out. It travels with the editor as vm6747 does.
ASM_REPO = "TI/ASM6x"
# The x86-64 assembler, its own repository beside this one too: masm reads
# the MASM the three compilers write for x86_64-windows and writes the COFF
# that link.exe takes, in place of ml64 - which is what settings.json names
# it as, in an installation. Docked since 4.0.
MASM_REPO = "Windows/MASM"
# The x86-64 linker, its own repository beside this one as well: link reads
# the COFF masm (and ml64, and cl) write and produces the PE32+ image, held to
# link.exe byte for byte on its probe bed. Docked since 4.0; settings.json
# names it as the linker only when it can take the editor's whole link line.
LINK_REPO = "Windows/LINK"
# And the C6000 linker, LINK's sibling as ASM6x is MASM's: lnk6x reads the TI
# ELF asm6x (and cl6x) write, places it as a linker command file says and
# writes the .out TI's lnk6x would, held to it byte for byte on its probe bed.
# Docked since 4.0; named as the tms6747 linker under Tools, not by default.
LNK6X_REPO = "TI/LNK6x"
SHC_REPO = "Compilers/Compiler-Si"
# The C6747 simulator, its own repository beside this one since 5.0: SIM6747 runs
# a linked .out the way TI's simulator does - machine code, TI's boot and rts - where
# vm6747 runs assembly. Its product is sim6747.exe so the two can sit side by side.
SIM_REPO = "TI/SIM6747"
# RTS6x, the project's own C6747 runtime (5.1): its rts6x.lib is what a TI program links against
# unless Tools names TI's compiler directory. Built from cpp11's and asm6x's output by its build.cmd.
RTS_REPO = "TI/RTS6x"


def ident(product, *parts):
    """A stable 24-hex-digit identifier, seeded by the product it belongs to.

    Xcode wants these unique within a project. Seeding with the product as well
    as the name keeps three projects from sharing identifiers, which is not
    strictly a fault across separate files but makes two of them impossible to
    tell apart when reading a diff.
    """
    digest = hashlib.sha1((product + ":" + ":".join(parts)).encode()).hexdigest()
    return digest[:24].upper()


def from_makefile(root, variables):
    """The .cpp named by these Makefile variables, relative to the repository."""
    text = open(os.path.join(root, "Makefile")).read()
    found = []
    for variable in variables:
        match = re.search(r"^%s *:?= (.*?)(?=\n[A-Z#]|\n\n)" % variable, text, re.S | re.M)
        if not match:
            sys.exit("could not find %s in %s/Makefile" % (variable, root))
        # A slash in the middle: src/backend/X86_64.cpp and runtime/Shortest.cpp
        # are both one path. A character class without one matched nothing at
        # all rather than failing, which is how the Shalimar half went missing.
        found += re.findall(r"([A-Za-z0-9_]+(?:/[A-Za-z0-9_]+)*\.cpp)", match.group(1))
    return found


def composed_of(root, variable):
    """The $(NAMES) one Makefile variable is made of, as a set.

    This exists because of how this script failed once before: a variable was
    added to the Makefile, SRC was made to include it, and nothing here read
    it - so every project quietly built a smaller program than make did. The
    lists below say which variables to read; this says which ones the Makefile
    actually uses, and main() insists the two agree. Adding a variable and
    forgetting this script is then a loud failure instead of a silent one.
    """
    text = open(os.path.join(root, "Makefile")).read()
    match = re.search(r"^%s *:?= (.*?)(?=\n[A-Z#]|\n\n)" % variable, text, re.S | re.M)
    if not match:
        sys.exit("could not find %s in %s/Makefile" % (variable, root))
    return set(re.findall(r"\$\(([A-Za-z0-9_]+)\)", match.group(1)))


def by_glob(root, directories):
    """The .cpp in these directories - for a Makefile that says $(wildcard ...).

    Compiler-C's does, so there is no list to read and the directories are the
    list. Sorted, so that a project regenerated on two machines is the same
    file on both.
    """
    found = []
    for directory in directories:
        where = os.path.join(root, directory)
        if not os.path.isdir(where):
            sys.exit("no %s in %s" % (directory, root))
        for f in sorted(os.listdir(where)):
            if f.endswith(".cpp"):
                found.append(directory + "/" + f)
    return found


def headers_under(root, directories):
    found = []
    for directory in directories:
        top = os.path.join(root, directory)
        for where, _, files in os.walk(top):
            for f in files:
                if f.endswith(".h"):
                    found.append(os.path.relpath(os.path.join(where, f), root))
    return sorted(found)


# What the editor is made of, as the Makefile now says it: the core both front
# ends compile, the terminal's own half, and the Shalimar session. SRC is
# CORE_SRC and TERMINAL_SRC together and names no files of its own, so reading
# it would find nothing - which composed_of is here to keep true.
EDITOR_VARIABLES = ("CORE_SRC", "TERMINAL_SRC", "SHM_SRC")


def ride_sources():
    """The editor's sources, minus the Windows terminal, which clang here cannot build."""
    names = from_makefile(HERE, EDITOR_VARIABLES)
    names = [n for n in names if not n.endswith("terminal_win.cpp")]
    # $(TERM_SRC) is chosen by the Makefile at build time; on a Mac it is this.
    if "src/terminal.cpp" not in names:
        names.append("src/terminal.cpp")
    return sorted(set(names))


# The three, and where each one's sources come from. Everything specific to a
# project is here; nothing below this knows which one it is writing.
def projects():
    return [
        {
            "product": "RIDE.exe",
            "root": HERE,
            "out": os.path.join(HERE, "Editor.xcodeproj"),
            "sources": ride_sources(),
            "headers": headers_under(HERE, ("src",)),
            "include": "$(SRCROOT)/src",
            # The editor drives these three, so building it builds them
            # first. Not a link dependency - all four are separate programs
            # and nothing of cc1, shc or c2s ends up inside ed1 - but a real
            # ordering:
            # a change to a compiler and the change to the editor that goes
            # with it are one build and one issue list, which is the whole
            # reason for a workspace rather than three windows.
            # These strings must be the *products* the other two projects use,
            # because the remote identifiers are derived from them. Getting one
            # wrong does not make Xcode complain - it silently drops the
            # dependency and builds only this target, which is how renaming
            # cc1 to cc1.exe stopped the workspace building the compilers
            # without anything saying so.
            "depends": [("c90.exe", "../Toolchain/" + CC1_REPO + "/ide/cc1.xcodeproj"),
                        ("cpp11.exe", "../Toolchain/" + CXX1_REPO + "/ide/cxx1.xcodeproj"),
                        ("vm6747.exe", "../Toolchain/" + VM_REPO + "/vm6747.xcodeproj"),
                        ("asm6x.exe", "../Toolchain/" + ASM_REPO + "/asm6x.xcodeproj"),
                        ("masm.exe", "../Toolchain/" + MASM_REPO + "/masm.xcodeproj"),
                        ("link.exe", "../Toolchain/" + LINK_REPO + "/link.xcodeproj"),
                        ("lnk6x.exe", "../Toolchain/" + LNK6X_REPO + "/lnk6x.xcodeproj"),
                        ("sim6747.exe", "../Toolchain/" + SIM_REPO + "/sim6747.xcodeproj"),
                        ("shalimar.exe", "../Toolchain/" + SHC_REPO + "/ide/shc.xcodeproj"),
                        ("c2s.exe", "../Toolchain/Compilers/Converter-C2S/c2s.xcodeproj")],
        },
        {
            "product": "c90.exe",
            "root": os.path.join(SIBLINGS, CC1_REPO),
            # **The compiler's own project, which RIDE opens and never writes**:
            # its ide/generate.py writes it with the ids ident() would give, and
            # --check below says whether it is current (2026-09-30; RIDE wrote a
            # copy into the compiler's root until then).
            "out": os.path.join(SIBLINGS, CC1_REPO, "ide", "cc1.xcodeproj"),
            "foreign": True,
            # Its Makefile says $(wildcard src/*.cpp) $(wildcard src/backend/*.cpp),
            # so the directories are the list.
            "sources": by_glob(os.path.join(SIBLINGS, CC1_REPO),
                               ("src", "src/backend")),
            "headers": headers_under(os.path.join(SIBLINGS, CC1_REPO), ("src",)),
            "include": "$(SRCROOT)/src $(SRCROOT)/lib",
            # INCDIR = $(CURDIR)/lib in its Makefile. $(SRCROOT) is where the
            # .xcodeproj sits, which is that same directory.
            "defines": [("CC1_INCLUDE_DIR", "$(SRCROOT)/lib")],
        },
        {
            # shalimar since 3.5: the VM6747 clone of Compiler-S, whose Makefile
            # builds shalimar.exe. The project file keeps its name; the product
            # is what changed, and that is what the identifiers derive from.
            "product": "shalimar.exe",
            "root": os.path.join(SIBLINGS, SHC_REPO),
            "out": os.path.join(SIBLINGS, SHC_REPO, "ide", "shc.xcodeproj"),
            "foreign": True,          # its own ide/generate.py writes it, runtime phase and all
            # SOURCES names runtime/Shortest.cpp as well as src/, which is why
            # paths here are relative to the repository and not to src/.
            "sources": sorted(set(from_makefile(os.path.join(SIBLINGS, SHC_REPO),
                                                ("SOURCES",)))),
            "headers": headers_under(os.path.join(SIBLINGS, SHC_REPO),
                                     ("src", "runtime")),
            "include": "$(SRCROOT)/src $(SRCROOT)/runtime",
            # `make` builds shalimar.exe and both runtime archives; a project that
            # built only the first would be the smaller program this script
            # exists to prevent. Since 3.5 the phase also writes the C6000
            # runtime, which is cpp11's output - so cpp11.exe is built first,
            # the ordering workspace.mk states as `make ... all tms6747
            # CXX1=$(OUT)/cpp11.exe` and RIDE.sln as a project dependency.
            # The path is from *this* project's directory, not the editor's:
            # a path written from the editor's directory would land inside Compiler-Si,
            # and Xcode drops a reference it cannot follow without a word -
            # the dependency graph then says "shalimar.exe (no dependencies)".
            "depends": [("cpp11.exe",
                         os.path.relpath(os.path.join(SIBLINGS, CXX1_REPO, "ide", "cxx1.xcodeproj"),
                                         os.path.join(SIBLINGS, SHC_REPO)))],
            "script": shc_runtime_script(),
            # The archives go with it, for the reason they were built beside it
            # in the first place: shc looks for lib/ next to its own binary, so
            # a copy that took the compiler and left the runtime would be the
            # incomplete copy step this whole arrangement exists to avoid. The
            # C6000 runtime directory the same, and rm first: cp -R onto a
            # directory that exists copies into it.
            "install_extra": ('mkdir -p "$dest/lib"\n'
                              'cp -f "$BUILT_PRODUCTS_DIR"/lib/*.a "$dest/lib/"\n'
                              'rm -rf "$dest/lib/shmrt-tms6747" "$dest/lib/shmrt-tms6747-debug"\n'
                              'cp -R "$BUILT_PRODUCTS_DIR/lib/shmrt-tms6747" "$BUILT_PRODUCTS_DIR/lib/shmrt-tms6747-debug" "$dest/lib/"\n'),
        },
        {
            "product": "cpp11.exe",
            "root": os.path.join(SIBLINGS, CXX1_REPO),
            "out": os.path.join(SIBLINGS, CXX1_REPO, "ide", "cxx1.xcodeproj"),
            "foreign": True,          # its own ide/generate.py writes it, as cc1's
            # SRCS is wildcards over src/, src/parser and src/backend, and in
            # 4.5 src/optimizer too: C++Optimize, which cpp11 is built from,
            # keeps its optimizer there. Asked for only where it exists.
            "sources": by_glob(os.path.join(SIBLINGS, CXX1_REPO),
                               ("src", "src/parser", "src/backend") +
                               (("src/optimizer",) if os.path.isdir(os.path.join(
                                   SIBLINGS, CXX1_REPO, "src", "optimizer")) else ())),
            "headers": headers_under(os.path.join(SIBLINGS, CXX1_REPO), ("src",)),
            "include": "$(SRCROOT)/src",
            # Both header directories, compiled in as its Makefile compiles
            # them: lib/ holds the C headers and include/ the C++ ones on top.
            # cxx1's own ide/ project once carried only the first, and a
            # binary built that way answered `cannot find <vector>` - see its
            # ide/README.md. The driver looks beside itself first in any case;
            # these are the fallback for a binary that was moved on its own.
            "defines": [("CXX1_INCLUDE_DIR", "$(SRCROOT)/lib"),
                        ("CXX1_CXX_INCLUDE_DIR", "$(SRCROOT)/include")],
        },
        {
            # The emulator that runs the fourth target's programs: plain
            # C++14 under src/, nothing else, and the editor finds it beside
            # itself as it finds the compilers.
            "product": "vm6747.exe",
            "root": os.path.join(SIBLINGS, VM_REPO),
            "out": os.path.join(SIBLINGS, VM_REPO, "vm6747.xcodeproj"),
            "sources": by_glob(os.path.join(SIBLINGS, VM_REPO), ("src",)),
            "headers": headers_under(os.path.join(SIBLINGS, VM_REPO), ("src",)),
            "include": "$(SRCROOT)/src",
        },
        {
            # The C6000 assembler: the same shape as the emulator, plain
            # C++14 under src/.
            "product": "asm6x.exe",
            "root": os.path.join(SIBLINGS, ASM_REPO),
            "out": os.path.join(SIBLINGS, ASM_REPO, "asm6x.xcodeproj"),
            "sources": by_glob(os.path.join(SIBLINGS, ASM_REPO), ("src",)),
            "headers": headers_under(os.path.join(SIBLINGS, ASM_REPO), ("src",)),
            "include": "$(SRCROOT)/src",
        },
        {
            # The x86-64 assembler: the same shape again.
            "product": "masm.exe",
            "root": os.path.join(SIBLINGS, MASM_REPO),
            "out": os.path.join(SIBLINGS, MASM_REPO, "masm.xcodeproj"),
            "sources": by_glob(os.path.join(SIBLINGS, MASM_REPO), ("src",)),
            "headers": headers_under(os.path.join(SIBLINGS, MASM_REPO), ("src",)),
            "include": "$(SRCROOT)/src",
        },
        {
            # The x86-64 linker: the same shape once more.
            "product": "link.exe",
            "root": os.path.join(SIBLINGS, LINK_REPO),
            "out": os.path.join(SIBLINGS, LINK_REPO, "link.xcodeproj"),
            "sources": by_glob(os.path.join(SIBLINGS, LINK_REPO), ("src",)),
            "headers": headers_under(os.path.join(SIBLINGS, LINK_REPO), ("src",)),
            "include": "$(SRCROOT)/src",
        },
        {
            # The C6747 simulator: the emulator's shape, plain C++14 under src/,
            # named sim6747.exe so it builds beside the emulator (5.0).
            "product": "sim6747.exe",
            "root": os.path.join(SIBLINGS, SIM_REPO),
            "out": os.path.join(SIBLINGS, SIM_REPO, "sim6747.xcodeproj"),
            "sources": by_glob(os.path.join(SIBLINGS, SIM_REPO), ("src",)),
            "headers": headers_under(os.path.join(SIBLINGS, SIM_REPO), ("src",)),
            "include": "$(SRCROOT)/src",
        },
        {
            # The C6000 linker: the same shape.
            "product": "lnk6x.exe",
            "root": os.path.join(SIBLINGS, LNK6X_REPO),
            "out": os.path.join(SIBLINGS, LNK6X_REPO, "lnk6x.xcodeproj"),
            "sources": by_glob(os.path.join(SIBLINGS, LNK6X_REPO), ("src",)),
            "headers": headers_under(os.path.join(SIBLINGS, LNK6X_REPO), ("src",)),
            "include": "$(SRCROOT)/src",
        },
        {
            "product": "c2s.exe",
            "root": os.path.join(SIBLINGS, "Compilers", "Converter-C2S"),
            "out": os.path.join(SIBLINGS, "Compilers", "Converter-C2S", "c2s.xcodeproj"),
            # Its Makefile is five wildcards over five directories, so the
            # directories are the list - the same shape as cc1's. src/s/vendor
            # is Compiler-S's front end, copied rather than linked, and it is
            # compiled here like everything else.
            "sources": by_glob(os.path.join(SIBLINGS, "Compilers", "Converter-C2S"),
                               ("src", "src/c", "src/s", "src/s/vendor",
                                "src/convert")),
            "headers": headers_under(os.path.join(SIBLINGS, "Compilers", "Converter-C2S"),
                                     ("src",)),
            "include": "$(SRCROOT)/src",
        },
    ]


# Xcode's own template turns on warnings none of these three Makefiles uses -
# -Wshorten-64-to-32 among them - and with warnings as errors that makes the
# project refuse a program `make` builds without complaint. cc1 failed exactly
# that way the first time this workspace was tried.
#
# The point of generating these from the Makefiles is that Xcode builds what
# make builds. A project that is *stricter* than the build it mirrors diverges
# just as surely as one that is laxer; it simply fails instead of passing. So
# the Xcode-only extras are turned off and the flag set is the Makefile's:
# -Wall -Wextra -pedantic, as errors.
# And the intermediates go outside the checkout. With no SYMROOT an Xcode build
# lands in <project>/build, which is how RIDE came to hold 92 object files
# under a target name that had been renamed away months earlier - invisible to
# make clean, because it is not make's. $(TMPDIR) is per-user and Xcode expands
# it from the environment.
#
# A workspace build ignores SYMROOT in any case - the workspace arena wins, and
# that is how these products come to sit under DerivedData at a path nobody can
# type. Where they *end up* is the install phase's business, below; SYMROOT
# still decides where a build of one .xcodeproj on its own goes.
# MACOSX_DEPLOYMENT_TARGET is the lowest the installed Xcode accepts: 27.0
# refuses the 10.15 these once carried ("the range of supported deployment
# target versions is 12.0 to 27.0.x"), and so refused to build every project
# made here. The Makefiles name no minimum and build for the machine they run on.
COMMON = """				ALWAYS_SEARCH_USER_PATHS = NO;
				OBJROOT = "$(TMPDIR)/ride-xcode";
				SYMROOT = "$(TMPDIR)/ride-xcode";
				CLANG_CXX_LANGUAGE_STANDARD = "c++14";
				CLANG_ENABLE_OBJC_ARC = YES;
				CLANG_WARN_IMPLICIT_SIGN_CONVERSION = NO;
				CODE_SIGN_STYLE = Automatic;
				GCC_TREAT_WARNINGS_AS_ERRORS = YES;
				GCC_WARN_64_TO_32_BIT_CONVERSION = NO;
				MACOSX_DEPLOYMENT_TARGET = 12.0;
				PRODUCT_NAME = %s;
				SDKROOT = macosx;
				USER_HEADER_SEARCH_PATHS = "%s";
				WARNING_CFLAGS = (
					"-Wall",
					"-Wextra",
					"-pedantic",
				);%s"""


# A Makefile's -D flags are as much a part of what it builds as its source
# list. cc1's CC1_INCLUDE_DIR is the one that matters here: it is the absolute
# path to the lib/ holding the fifteen headers cc1 ships, and Driver.cpp
# defaults it to "" when nobody defines it, so a project that forgets it builds
# a compiler that runs, parses, and then cannot find <stdio.h> - reporting an
# empty list of directories it looked in, which reads as a broken installation
# rather than a project missing one line. That is what the workspace produced
# from 2026-08-22 until 2026-08-27, and no build and no suite could see it,
# because both compilers are built by make everywhere the suites run.
#
# The doubled backslashes are one level of pbxproj escaping over one level of
# shell: what has to reach clang is -DCC1_INCLUDE_DIR="/some/path".
# Relative to each project rather than written out, because the four are only
# ever expected side by side - the same assumption workspace.mk and the
# solution already make - and an absolute path here would be one machine's.
def build_dir(spec):
    """Where this project's finished program goes: RIDE's own root."""
    back = os.path.relpath(HERE, spec["root"])
    return "$(SRCROOT)" if back == "." else "$(SRCROOT)/" + back


def defines_setting(spec):
    defines = spec.get("defines", [])
    if not defines:
        return ""
    quote = r'\\\"'
    written = " ".join(name + "=" + quote + value + quote
                       for name, value in defines)
    return '\n\t\t\t\tGCC_PREPROCESSOR_DEFINITIONS = "%s";' % written


def project_text(spec):
    product = spec["product"]
    cpps = spec["sources"]
    hpps = spec["headers"]

    def i(*parts):
        return ident(product, *parts)

    PROJECT = i("project")
    TARGET = i("target")
    PRODUCT = i("product")
    MAIN_GROUP = i("group", "main")
    SRC_GROUP = i("group", "src")
    PRODUCTS_GROUP = i("group", "products")
    SOURCES_PHASE = i("phase", "sources")
    FRAMEWORKS = i("phase", "frameworks")
    PROJECT_CONFIGS = i("configlist", "project")
    TARGET_CONFIGS = i("configlist", "target")
    SCRIPT_PHASE = i("phase", "script")
    INSTALL_PHASE = i("phase", "install")

    # Order is the order they run in: shc's runtime after the compiler it
    # belongs beside, then the copy that takes both to RIDE's directory.
    # Only shc has a runtime, and it is the spec that says so rather than a
    # name test here; all four are copied.
    phases = []
    script = spec.get("script")

    common = COMMON % (product, spec["include"], defines_setting(spec))

    # Depending on a target in another project takes five objects per
    # dependency, and the remote identifiers have to be the ones that project
    # actually used - which is why ident() is seeded by product and derived
    # rather than invented. Getting one wrong gives Xcode a project it calls
    # damaged, with no clue which reference it could not follow.
    depends = spec.get("depends", [])
    dep_ids = []
    for other, where in depends:
        dep_ids.append({
            "product": other,
            "path": where,
            "file": i("projectref", other),          # the .xcodeproj on disk
            "group": i("projectproducts", other),    # its Products group, here
            "refproxy": i("referenceproxy", other),  # its product, seen from here
            "prodproxy": i("containerproxy", "product", other),
            "depproxy": i("containerproxy", "target", other),
            "dependency": i("targetdependency", other),
            # the two identifiers that belong to the *other* project
            "remote_target": ident(other, "target"),
            "remote_product": ident(other, "product"),
        })

    def config_id(which, name):
        return i("config", which, name)

    def build_configuration(which, name, extra):
        return ("\t\t%s /* %s */ = {\n\t\t\tisa = XCBuildConfiguration;\n"
                "\t\t\tbuildSettings = {\n%s\n%s\n\t\t\t};\n\t\t\tname = %s;\n\t\t};\n"
                % (config_id(which, name), name, common, extra, name))

    lines = []
    lines.append("// !$*UTF8*$!\n{\n\tarchiveVersion = 1;\n\tclasses = {\n\t};\n"
                 "\tobjectVersion = 56;\n\tobjects = {\n")

    lines.append("\n/* Begin PBXBuildFile section */\n")
    for name in cpps:
        lines.append("\t\t%s /* %s in Sources */ = {isa = PBXBuildFile; "
                     "fileRef = %s /* %s */; };\n"
                     % (i("build", name), name, i("file", name), name))
    lines.append("/* End PBXBuildFile section */\n")

    lines.append("\n/* Begin PBXFileReference section */\n")
    for name in cpps:
        lines.append("\t\t%s /* %s */ = {isa = PBXFileReference; "
                     "lastKnownFileType = sourcecode.cpp.cpp; path = %s; "
                     "sourceTree = \"<group>\"; };\n" % (i("file", name), name, name))
    for name in hpps:
        lines.append("\t\t%s /* %s */ = {isa = PBXFileReference; "
                     "lastKnownFileType = sourcecode.c.h; path = %s; "
                     "sourceTree = \"<group>\"; };\n" % (i("file", name), name, name))
    lines.append("\t\t%s /* %s */ = {isa = PBXFileReference; "
                 "explicitFileType = \"compiled.mach-o.executable\"; "
                 "includeInIndex = 0; path = %s; sourceTree = BUILT_PRODUCTS_DIR; };\n"
                 % (PRODUCT, product, product))
    for d in dep_ids:
        # Quoted, since one of these is ../C++/cxx1.xcodeproj and a '+' is not
        # a character an unquoted pbxproj string may hold - Xcode read the
        # project as damaged and the workspace lost the editor, silently.
        lines.append("\t\t%s /* %s.xcodeproj */ = {isa = PBXFileReference; "
                     "lastKnownFileType = \"wrapper.pb-project\"; name = %s.xcodeproj; "
                     "path = %s; sourceTree = \"<group>\"; };\n"
                     % (d["file"], d["product"], d["product"], pbx_quoted(d["path"])))
    lines.append("/* End PBXFileReference section */\n")

    if dep_ids:
        lines.append("\n/* Begin PBXContainerItemProxy section */\n")
        for d in dep_ids:
            # proxyType 2 is the other project's product; 1 is its target.
            lines.append("\t\t%s /* PBXContainerItemProxy */ = {\n"
                         "\t\t\tisa = PBXContainerItemProxy;\n"
                         "\t\t\tcontainerPortal = %s /* %s.xcodeproj */;\n"
                         "\t\t\tproxyType = 2;\n"
                         "\t\t\tremoteGlobalIDString = %s;\n"
                         "\t\t\tremoteInfo = %s;\n\t\t};\n"
                         % (d["prodproxy"], d["file"], d["product"],
                            d["remote_product"], d["product"]))
            lines.append("\t\t%s /* PBXContainerItemProxy */ = {\n"
                         "\t\t\tisa = PBXContainerItemProxy;\n"
                         "\t\t\tcontainerPortal = %s /* %s.xcodeproj */;\n"
                         "\t\t\tproxyType = 1;\n"
                         "\t\t\tremoteGlobalIDString = %s;\n"
                         "\t\t\tremoteInfo = %s;\n\t\t};\n"
                         % (d["depproxy"], d["file"], d["product"],
                            d["remote_target"], d["product"]))
        lines.append("/* End PBXContainerItemProxy section */\n")

    lines.append("\n/* Begin PBXFrameworksBuildPhase section */\n")
    lines.append("\t\t%s /* Frameworks */ = {\n\t\t\tisa = PBXFrameworksBuildPhase;\n"
                 "\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = (\n\t\t\t);\n"
                 "\t\t\trunOnlyForDeploymentPostprocessing = 0;\n\t\t};\n" % FRAMEWORKS)
    lines.append("/* End PBXFrameworksBuildPhase section */\n")

    lines.append("\n/* Begin PBXGroup section */\n")
    referenced = "".join("\t\t\t\t%s /* %s.xcodeproj */,\n" % (d["file"], d["product"])
                         for d in dep_ids)
    lines.append("\t\t%s = {\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n"
                 "\t\t\t\t%s /* Sources */,\n%s\t\t\t\t%s /* Products */,\n"
                 "\t\t\t);\n\t\t\tsourceTree = \"<group>\";\n\t\t};\n"
                 % (MAIN_GROUP, SRC_GROUP, referenced, PRODUCTS_GROUP))
    children = "".join("\t\t\t\t%s /* %s */,\n" % (i("file", n), n) for n in cpps + hpps)
    # The group has a name and no path: every file carries its own path from
    # the repository root, which is the only shape that takes src/, src/backend/
    # and runtime/ in one list.
    lines.append("\t\t%s /* Sources */ = {\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n%s"
                 "\t\t\t);\n\t\t\tname = Sources;\n\t\t\tsourceTree = \"<group>\";\n\t\t};\n"
                 % (SRC_GROUP, children))
    lines.append("\t\t%s /* Products */ = {\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n"
                 "\t\t\t\t%s /* %s */,\n\t\t\t);\n\t\t\tname = Products;\n"
                 "\t\t\tsourceTree = \"<group>\";\n\t\t};\n"
                 % (PRODUCTS_GROUP, PRODUCT, product))
    for d in dep_ids:
        lines.append("\t\t%s /* Products */ = {\n\t\t\tisa = PBXGroup;\n"
                     "\t\t\tchildren = (\n\t\t\t\t%s /* %s */,\n\t\t\t);\n"
                     "\t\t\tname = Products;\n\t\t\tsourceTree = \"<group>\";\n\t\t};\n"
                     % (d["group"], d["refproxy"], d["product"]))
    lines.append("/* End PBXGroup section */\n")

    if dep_ids:
        lines.append("\n/* Begin PBXReferenceProxy section */\n")
        for d in dep_ids:
            lines.append("\t\t%s /* %s */ = {\n\t\t\tisa = PBXReferenceProxy;\n"
                         "\t\t\tfileType = \"compiled.mach-o.executable\";\n"
                         "\t\t\tpath = %s;\n\t\t\tremoteRef = %s /* PBXContainerItemProxy */;\n"
                         "\t\t\tsourceTree = BUILT_PRODUCTS_DIR;\n\t\t};\n"
                         % (d["refproxy"], d["product"], d["product"], d["prodproxy"]))
        lines.append("/* End PBXReferenceProxy section */\n")

    lines.append("\n/* Begin PBXNativeTarget section */\n")
    lines.append("\t\t%s /* %s */ = {\n\t\t\tisa = PBXNativeTarget;\n"
                 "\t\t\tbuildConfigurationList = %s;\n\t\t\tbuildPhases = (\n"
                 "\t\t\t\t%s /* Sources */,\n\t\t\t\t%s /* Frameworks */,\n%s%s\t\t\t);\n"
                 "\t\t\tbuildRules = (\n\t\t\t);\n\t\t\tdependencies = (\n%s\t\t\t);\n"
                 "\t\t\tname = %s;\n\t\t\tproductName = %s;\n"
                 "\t\t\tproductReference = %s /* %s */;\n"
                 "\t\t\tproductType = \"com.apple.product-type.tool\";\n\t\t};\n"
                 % (TARGET, product, TARGET_CONFIGS, SOURCES_PHASE, FRAMEWORKS,
                    ("\t\t\t\t%s /* %s */,\n" % (SCRIPT_PHASE, script["name"])
                     if script else ""),
                    "\t\t\t\t%s /* %s */,\n" % (INSTALL_PHASE,
                                              install_phase(spec)["name"]),
                    "".join("\t\t\t\t%s /* PBXTargetDependency */,\n" % d["dependency"]
                            for d in dep_ids),
                    product, product, PRODUCT, product))
    lines.append("/* End PBXNativeTarget section */\n")

    if dep_ids:
        lines.append("\n/* Begin PBXTargetDependency section */\n")
        for d in dep_ids:
            lines.append("\t\t%s /* PBXTargetDependency */ = {\n"
                         "\t\t\tisa = PBXTargetDependency;\n\t\t\tname = %s;\n"
                         "\t\t\ttargetProxy = %s /* PBXContainerItemProxy */;\n\t\t};\n"
                         % (d["dependency"], d["product"], d["depproxy"]))
        lines.append("/* End PBXTargetDependency section */\n")

    lines.append("\n/* Begin PBXProject section */\n")
    lines.append("\t\t%s /* Project object */ = {\n\t\t\tisa = PBXProject;\n"
                 "\t\t\tattributes = {\n\t\t\t\tBuildIndependentTargetsInParallel = 1;\n"
                 "\t\t\t\tLastUpgradeCheck = 1600;\n\t\t\t};\n"
                 "\t\t\tbuildConfigurationList = %s;\n"
                 "\t\t\tdevelopmentRegion = en;\n\t\t\thasScannedForEncodings = 0;\n"
                 "\t\t\tknownRegions = (\n\t\t\t\ten,\n\t\t\t\tBase,\n\t\t\t);\n"
                 "\t\t\tmainGroup = %s;\n\t\t\tproductRefGroup = %s /* Products */;\n"
                 "\t\t\tprojectDirPath = \"\";\n%s\t\t\tprojectRoot = \"\";\n"
                 "\t\t\ttargets = (\n\t\t\t\t%s /* %s */,\n\t\t\t);\n\t\t};\n"
                 % (PROJECT, PROJECT_CONFIGS, MAIN_GROUP, PRODUCTS_GROUP,
                    ("\t\t\tprojectReferences = (\n" +
                     "".join("\t\t\t\t{\n\t\t\t\t\tProductGroup = %s /* Products */;\n"
                             "\t\t\t\t\tProjectRef = %s /* %s.xcodeproj */;\n\t\t\t\t},\n"
                             % (d["group"], d["file"], d["product"]) for d in dep_ids) +
                     "\t\t\t);\n") if dep_ids else "",
                    TARGET, product))
    lines.append("/* End PBXProject section */\n")

    if script:
        phases.append((SCRIPT_PHASE, script))
    phases.append((INSTALL_PHASE, install_phase(spec)))

    def listed(paths):
        return "".join("\t\t\t\t\"%s\",\n" % path for path in paths)
    lines.append("\n/* Begin PBXShellScriptBuildPhase section */\n")
    for phase_id, phase in phases:
        lines.append("\t\t%s /* %s */ = {\n"
                     "\t\t\tisa = PBXShellScriptBuildPhase;\n"
                     "%s"
                     "\t\t\tbuildActionMask = 2147483647;\n"
                     "\t\t\tfiles = (\n\t\t\t);\n"
                     "\t\t\tinputPaths = (\n%s\t\t\t);\n"
                     "\t\t\tname = \"%s\";\n"
                     "\t\t\toutputPaths = (\n%s\t\t\t);\n"
                     "\t\t\trunOnlyForDeploymentPostprocessing = 0;\n"
                     "\t\t\tshellPath = /bin/sh;\n"
                     "\t\t\tshellScript = %s;\n\t\t};\n"
                     % (phase_id,
                        phase["name"],
                        "\t\t\talwaysOutOfDate = 1;\n" if phase.get("always") else "",
                        listed(phase["inputs"]), phase["name"],
                        listed(phase["outputs"]), pbx_quoted(phase["shell"])))
    lines.append("/* End PBXShellScriptBuildPhase section */\n")

    lines.append("\n/* Begin PBXSourcesBuildPhase section */\n")
    compiled = "".join("\t\t\t\t%s /* %s in Sources */,\n" % (i("build", n), n)
                       for n in cpps)
    lines.append("\t\t%s /* Sources */ = {\n\t\t\tisa = PBXSourcesBuildPhase;\n"
                 "\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = (\n%s\t\t\t);\n"
                 "\t\t\trunOnlyForDeploymentPostprocessing = 0;\n\t\t};\n"
                 % (SOURCES_PHASE, compiled))
    lines.append("/* End PBXSourcesBuildPhase section */\n")

    lines.append("\n/* Begin XCBuildConfiguration section */\n")
    debug = "\t\t\t\tDEBUG_INFORMATION_FORMAT = dwarf;\n\t\t\t\tGCC_OPTIMIZATION_LEVEL = 0;"
    release = ("\t\t\t\tDEBUG_INFORMATION_FORMAT = \"dwarf-with-dsym\";\n"
               "\t\t\t\tGCC_OPTIMIZATION_LEVEL = 2;")
    for which in ("project", "target"):
        lines.append(build_configuration(which, "Debug", debug))
        lines.append(build_configuration(which, "Release", release))
    lines.append("/* End XCBuildConfiguration section */\n")

    lines.append("\n/* Begin XCConfigurationList section */\n")
    for listing, which in ((PROJECT_CONFIGS, "project"), (TARGET_CONFIGS, "target")):
        lines.append("\t\t%s /* %s */ = {\n\t\t\tisa = XCConfigurationList;\n"
                     "\t\t\tbuildConfigurations = (\n"
                     "\t\t\t\t%s /* Debug */,\n\t\t\t\t%s /* Release */,\n\t\t\t);\n"
                     "\t\t\tdefaultConfigurationIsVisible = 0;\n"
                     "\t\t\tdefaultConfigurationName = Debug;\n\t\t};\n"
                     % (listing, which, config_id(which, "Debug"),
                        config_id(which, "Release")))
    lines.append("/* End XCConfigurationList section */\n")

    lines.append("\t};\n\trootObject = %s /* Project object */;\n}\n" % PROJECT)
    return "".join(lines)


def guid(product):
    """A stable GUID for a generated .vcxproj, derived from the product name.

    Visual Studio wants one per project and wants the solution to agree with
    the project file about it. Deriving it means the two cannot disagree and a
    regenerated project is the same file, which is what --check rests on.
    """
    # **A fixed seed, not a name.** It spells the product's name from before the
    # rename to RIDE, and it stays: change it and every project's GUID changes,
    # in this repo's solution and in the eight sibling projects made here.
    d = hashlib.sha1(("rstudio-vcxproj:" + product).encode()).hexdigest().upper()
    return "{%s-%s-%s-%s-%s}" % (d[:8], d[8:12], d[12:16], d[16:20], d[20:32])


# **shc is not one file.** It links a runtime archive that it looks for beside
# its own binary - lib/ next to it, then ../lib - so a project that builds
# shc.exe and nothing else produces a compiler that compiles, writes correct
# assembly, and then dies at the link with
#
#   LINK : fatal error LNK1181: cannot open input file
#          '...\x64\Release\lib\shmrt-x86_64-windows-debug.lib'
#
# which reads as a broken compiler rather than an incomplete directory. That is
# what RIDE.sln produced until 2026-08-23, and no build and no suite could
# see it: -S needs no runtime, so only pressing Run on a Shalimar file said so.
#
# Twice from the same sources, as Compiler-S/build.bat does it: the release
# archive holds no debugger code at all, and the debug one is the same program
# plus a session dormant until SHM_DEBUG arms it. Both are named because a
# debug build links the second, and shipping one would leave exactly the half
# about to be used missing.
#
# Two things here are not typos. cl will not create the directory /Fo names -
# it says so three files later as "Cannot open compiler generated file", which
# reads as a disk problem - so the object directories are made first. And the
# doubled backslash in /Fo is required: a single one before a closing quote
# escapes the quote, and cl then answers "D8003: missing source filename".
# Read from Compiler-S's Makefile rather than written down, for the reason
# every other list here is: a runtime file added there and forgotten here gives
# an archive missing one object, and the link that fails names a symbol rather
# than a file. RUNTIME_SOURCES is the release list; DEBUG_RUNTIME_SOURCES is
# that plus runtime/Debug.cpp, and the $(RUNTIME_SOURCES) it opens with holds
# no .cpp of its own, so the two add up rather than overlapping.
def shc_runtime_sources():
    """(release, debug) - the runtime's own sources, as bare names."""
    root = os.path.join(SIBLINGS, SHC_REPO)
    release = from_makefile(root, ("RUNTIME_SOURCES",))
    debug = release + from_makefile(root, ("DEBUG_RUNTIME_SOURCES",))
    for path in debug:
        if not path.startswith("runtime/"):
            sys.exit("shc runtime source outside runtime/: " + path)
    strip = lambda names: tuple(n[len("runtime/"):-len(".cpp")] for n in names)
    return strip(release), strip(debug)


def shc_runtime_step():
    """The PostBuildEvent that puts shc's runtime in lib/ beside shalimar.exe."""
    def compiled(names, into):
        return " ".join('"$(ProjectDir)runtime\%s.cpp"' % n for n in names), \
               " ".join('"$(IntDir)%s\%s.obj"' % (into, n) for n in names)

    release, debug = shc_runtime_sources()
    release_src, release_obj = compiled(release, "rt")
    debug_src, debug_obj = compiled(debug, "rtd")
    flags = ("/nologo /std:c++14 /W4 /WX /EHsc /permissive- /O2 "
             "/D_CRT_SECURE_NO_WARNINGS")

    # **And the C6000 runtime, which is cpp11's output.** Compiler-S's
    # Makefile keeps it under its own `tms6747` rule because `make` alone
    # must not need the C++ clone; here the solution builds cpp11.exe first
    # (shalimar depends on it in RIDE.sln, below) and this step wants it beside
    # the output. Wanted, not hoped for: a missing cpp11.exe stops the build
    # and says so, since the alternative is an editor whose Shalimar cases
    # for the emulator are "not tried" and nothing says why. The Windows box
    # found it that way on 2026-09-15 - both archives built, no directory,
    # and F5 on a Shalimar file for tms6747 had nothing to run beside.
    #
    # One command per source, written whole every build: six small files,
    # and a stale .s left from a runtime source that was renamed would be
    # assembled beside the program with everything else.
    # Two directories, as the hosts have two archives: -O2 for Release, -O0 with SHM_DEBUG and
    # Debug.cpp for Debug, the latter beside RTS6x's rts6xd.lib in a Debug .out.
    c6000 = "".join(
        '"$(OutDir)cpp11.exe" -S -arch tms6747 -nologo -O2 "$(ProjectDir)runtime\\%s.cpp" '
        '-o "$(OutDir)lib\\shmrt-tms6747\\%s.s"\n'
        'if errorlevel 1 exit /b 1\n' % (n, n) for n in release) + "".join(
        '"$(OutDir)cpp11.exe" -S -arch tms6747 -nologo -O0 -DSHM_DEBUG=1 "$(ProjectDir)runtime\\%s.cpp" '
        '-o "$(OutDir)lib\\shmrt-tms6747-debug\\%s.s"\n'
        'if errorlevel 1 exit /b 1\n' % (n, n) for n in debug)

    return (
        '    <PostBuildEvent>\n'
        '      <Message>building the Shalimar runtime beside shalimar.exe</Message>\n'
        '      <Command>if not exist "$(OutDir)lib" mkdir "$(OutDir)lib"\n'
        'if not exist "$(IntDir)rt" mkdir "$(IntDir)rt"\n'
        'if not exist "$(IntDir)rtd" mkdir "$(IntDir)rtd"\n'
        'cl %s /Fo"$(IntDir)rt\\\\" /c %s\n'
        'if errorlevel 1 exit /b 1\n'
        'lib /nologo /out:"$(OutDir)lib\shmrt-x86_64-windows.lib" %s\n'
        'if errorlevel 1 exit /b 1\n'
        'cl %s /DSHM_DEBUG=1 /Fo"$(IntDir)rtd\\\\" /c %s\n'
        'if errorlevel 1 exit /b 1\n'
        'lib /nologo /out:"$(OutDir)lib\shmrt-x86_64-windows-debug.lib" %s\n'
        'if errorlevel 1 exit /b 1\n'
        'if not exist "$(OutDir)cpp11.exe" echo shc.vcxproj: no cpp11.exe in $(OutDir) - '
        'the C6000 runtime is its output; build RIDE.sln, which builds it first\n'
        'if not exist "$(OutDir)cpp11.exe" exit /b 1\n'
        'if not exist "$(OutDir)lib\\shmrt-tms6747" mkdir "$(OutDir)lib\\shmrt-tms6747"\n'
        'if not exist "$(OutDir)lib\\shmrt-tms6747-debug" mkdir "$(OutDir)lib\\shmrt-tms6747-debug"\n'
        '%s</Command>\n'
        '    </PostBuildEvent>\n'
        % (flags, release_src, release_obj, flags, debug_src, debug_obj, c6000.rstrip("\n")))


# The same job on the Mac, and it has to be done twice on the same principle:
# a project that builds shc.exe and nothing else produces a compiler that
# compiles, writes correct assembly, and then dies at the link naming an
# archive that is not there. Windows got this on 2026-08-23 and the Xcode side
# did not, because --check compares which .cpp a project builds and a build
# phase is not a .cpp.
#
# $BUILT_PRODUCTS_DIR is where shc.exe itself lands, and shc looks for lib/
# beside its own binary - so the archives go in without the editor, the
# workspace or shc knowing anything about each other.
#
# The flags are Compiler-S's CXXFLAGS and not Xcode's: what make builds is what
# this should build, and the runtime does not vary with the Debug/Release the
# *compiler* was built as. Both archives are written in either configuration,
# as `make all` writes both - a debug link takes the second, and shipping one
# would leave exactly the half about to be used missing.
#
# arm64-darwin because Compiler-S's Makefile says TARGET ?= arm64-darwin for
# any Darwin host, and the name has to be the one shc asks for.
SHC_RUNTIME_FLAGS = "-std=c++14 -Wall -Wextra -Werror -pedantic -O2"
SHC_RUNTIME_TARGET = "arm64-darwin"


# **The four programs are copied into RIDE's own directory when they are
# built.** That is the Mac half of one binary directory on every machine -
# `make -f workspace.mk` builds all four into $(CURDIR) there and RIDE.sln
# builds five into x64\\Release, while the workspace alone used to leave them
# under DerivedData. The editor finds what it drives with path::besideProgram
# before PATH, so this is also what makes a workspace-built RIDE.exe run the
# compilers it was built with rather than whichever ones are on the machine.
#
# **Copied, and not built there, and that is not the preference it looks
# like.** CONFIGURATION_BUILD_DIR does aim the link straight at this directory,
# and it was tried first: it works until you build Debug after Release without
# editing anything. Xcode plans a build before it runs it, decides the link is
# up to date because the *other* configuration's binary is newer than every
# input, and skips it - so the directory keeps the wrong build and says it
# succeeded. Clearing the product in a phase does not help, because the plan
# was already made: dsymutil then fails with `cannot parse the debug map ...
# No such file or directory` and the build ends with three of the four
# programs deleted. A copy has none of that in it. It always runs, it copies
# the configuration that was actually built, and a link Xcode skipped is a
# copy of a file that is already right.
#
# Debug and Release therefore land on each other here, as make's one build
# does: what stands in this directory is what you built last.
#
# `mv` over `cp` for the last step because the destination may be running -
# a rename replaces the directory entry and leaves the running image alone,
# where writing through it is "Text file busy".
#
# **And the copy is signed here, because Xcode signs a target after its script
# phases, not before.** A phase at the end of the list still runs before
# CodeSign, so what it has to copy is the linker's output - and the linker was
# given -no_adhoc_codesign precisely because Xcode meant to sign it later. On
# arm64 an unsigned Mach-O does not run at all: the copy would land, look
# right, and die with `Killed: 9`. First seen as a 22KB difference between the
# copy and the product, which is a signature's worth of hashes.
#
# Ad-hoc, as `-` says, which is what the linker does for a `make` build and
# what "Sign to Run Locally" means. The entitlements Xcode's own signature
# carries are not reproduced; they are for debugging the product in Xcode, and
# that is done in the build directory, not here.
def install_phase(spec):
    """Copies this project's finished program into RIDE's own directory."""
    extra = spec.get("install_extra", "")
    return {
        "name": "put the finished program in RIDE's directory",
        "shell": ("set -e\n"
                  'dest="%s"\n' % build_dir(spec).replace("$(SRCROOT)", "$SRCROOT") +
                  'mkdir -p "$dest"\n'
                  'cp -f "$BUILT_PRODUCTS_DIR/$PRODUCT_NAME" "$dest/.$PRODUCT_NAME.new"\n'
                  'mv -f "$dest/.$PRODUCT_NAME.new" "$dest/$PRODUCT_NAME"\n'
                  'codesign --force --sign - "$dest/$PRODUCT_NAME"\n' + extra),
        # It copies whatever was just built, so there is no up-to-date state
        # for it to be in. Said out loud, because a phase with no outputs is a
        # warning otherwise.
        "always": True,
        "inputs": [],
        "outputs": [],
    }


def shc_runtime_phase():
    """The build phase that puts shc's runtime archives in lib/ beside it."""
    release, debug = shc_runtime_sources()

    def archive(names, objects, into, extra, leaf):
        return ("mkdir -p \"%s\"\n" % objects +
                "".join('"$cxx" $flags %s-c "$SRCROOT/runtime/%s.cpp" '
                        '-o "%s/%s.o"\n' % (extra, name, objects, name)
                        for name in names) +
                'rm -f "$lib/%s"\n' % leaf +
                '"$ar" rcs "$lib/%s" "%s"/*.o\n' % (leaf, objects))

    # The C6000 runtime is a directory of assembly, one .s per release
    # source, written by the cpp11.exe this workspace just built (a target
    # dependency, so it is there); the emulator takes the directory whole
    # beside a Shalimar program. rm first for the reason ar gets it below.
    c6000 = ('cpp11="$BUILT_PRODUCTS_DIR/cpp11.exe"\n'
             'test -x "$cpp11" || { echo "shc.xcodeproj: no cpp11.exe beside the output - '
             'the C6000 runtime is its output" >&2; exit 1; }\n'
             'rm -rf "$lib/shmrt-tms6747" "$lib/shmrt-tms6747-debug"\n'
             'mkdir -p "$lib/shmrt-tms6747" "$lib/shmrt-tms6747-debug"\n' +
             "".join('"$cpp11" -S -arch tms6747 -nologo -O2 "$SRCROOT/runtime/%s.cpp" '
                     '-o "$lib/shmrt-tms6747/%s.s"\n' % (name, name) for name in release) +
             "".join('"$cpp11" -S -arch tms6747 -nologo -O0 -DSHM_DEBUG=1 "$SRCROOT/runtime/%s.cpp" '
                     '-o "$lib/shmrt-tms6747-debug/%s.s"\n' % (name, name) for name in debug))

    # rm before ar: `ar rcs` replaces members in an archive that is already
    # there, so a source deleted from the Makefile would live on inside it.
    return ("set -e\n"
            'cxx="$(xcrun --find clang++)"\n'
            'ar="$(xcrun --find ar)"\n'
            'flags="%s"\n' % SHC_RUNTIME_FLAGS +
            'lib="$BUILT_PRODUCTS_DIR/lib"\n'
            'mkdir -p "$lib"\n' +
            archive(release, "$DERIVED_FILE_DIR/runtime", "runtime", "",
                    "shmrt-%s.a" % SHC_RUNTIME_TARGET) +
            archive(debug, "$DERIVED_FILE_DIR/runtime-debug", "runtime-debug",
                    "-DSHM_DEBUG=1 ", "shmrt-%s-debug.a" % SHC_RUNTIME_TARGET) +
            c6000)


def shc_runtime_script():
    """The phase as project_text wants it: a name, a script, and its files."""
    release, debug = shc_runtime_sources()
    headers = ("shmrt", "Internal", "Shortest", "Debug")
    return {
        "name": "the Shalimar runtime, beside shalimar.exe",
        "shell": shc_runtime_phase(),
        # Named so Xcode can tell the phase is up to date and skip it. With no
        # outputs it runs on every build and says so as a warning; with these
        # it runs when a runtime source or header changes, which is the same
        # rule make follows.
        # cpp11.exe is an input too: a new compiler means new C6000 runtime.
        "inputs": (["$(SRCROOT)/runtime/%s.cpp" % n for n in debug] +
                   ["$(SRCROOT)/runtime/%s.h" % n for n in headers] +
                   ["$(BUILT_PRODUCTS_DIR)/cpp11.exe"]),
        "outputs": (["$(BUILT_PRODUCTS_DIR)/lib/shmrt-%s.a" % SHC_RUNTIME_TARGET,
                     "$(BUILT_PRODUCTS_DIR)/lib/shmrt-%s-debug.a" % SHC_RUNTIME_TARGET] +
                    ["$(BUILT_PRODUCTS_DIR)/lib/shmrt-tms6747/%s.s" % n for n in release]),
    }


def pbx_quoted(text):
    """One pbxproj string: quotes, backslashes and newlines are escaped."""
    return '"%s"' % (text.replace("\\", "\\\\")
                         .replace('"', '\\"')
                         .replace("\n", "\\n"))


def vcxproj_text(product, sources, defines, extra="", includes=(), disabled=(), target=None, props=None):
    """A command line tool for MSVC, held to the same flags build.bat uses.

    /std:c++14 /W4 /WX /EHsc /permissive- - the same four this project has
    always been built with on that machine, so the solution and build.bat
    produce the same program rather than two that differ in what they refused.

    `includes` and `disabled` are for a project whose own MSVC build carries
    them - cxx1's msvc/build.cmd puts its compat/ directory on the include
    path and turns off five warnings that fire on code it forked rather than
    wrote. A project written here has to say what that script says, or it is
    a stricter build of the same tree and fails where the tree's own passes.
    """
    configurations = "".join(
        '    <ProjectConfiguration Include="%s|x64">\n'
        '      <Configuration>%s</Configuration>\n'
        '      <Platform>x64</Platform>\n'
        '    </ProjectConfiguration>\n' % (c, c) for c in ("Debug", "Release"))

    per_config = ""
    for c, debug_libraries, optimisation in (("Debug", "true", "Disabled"),
                                             ("Release", "false", "MaxSpeed")):
        per_config += (
            '  <PropertyGroup Condition="\'$(Configuration)|$(Platform)\'==\'%s|x64\'" '
            'Label="Configuration">\n'
            '    <ConfigurationType>Application</ConfigurationType>\n'
            '    <UseDebugLibraries>%s</UseDebugLibraries>\n'
            '    <PlatformToolset>v143</PlatformToolset>\n'
            '    <CharacterSet>MultiByte</CharacterSet>\n'
            '  </PropertyGroup>\n' % (c, debug_libraries))

    definitions = ";".join(defines + ["%(PreprocessorDefinitions)"])
    compiled = "".join('    <ClCompile Include="%s" />\n' % s.replace("/", "\\")
                       for s in sources)

    return (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<Project DefaultTargets="Build" ToolsVersion="17.0" '
        'xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
        '  <ItemGroup Label="ProjectConfigurations">\n%s  </ItemGroup>\n'
        '  <PropertyGroup Label="Globals">\n'
        '    <VCProjectVersion>17.0</VCProjectVersion>\n'
        '    <ProjectGuid>%s</ProjectGuid>\n'
        '    <RootNamespace>%s</RootNamespace>\n'
        '    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>\n'
        '  </PropertyGroup>\n'
        '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />\n%s'
        '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />\n%s'
        '  <PropertyGroup>\n'
        '    <TargetName>%s</TargetName>\n'
        '  </PropertyGroup>\n'
        # OutDir is deliberately not set. The C++ default is already the rule
        # these projects want, and is the msbuild spelling of the Makefile's
        # BINDIR: built alone the program lands in this project's own
        # x64\\$(Configuration)\\, and built from a solution it lands in the
        # solution's output directory, beside the editor that drives it.
        # Compiler-C/msvc/cc1.vcxproj is hand-kept and had to override that
        # default; it now undoes the override for this same reason.
        '  <!-- OutDir is left to the C++ default on purpose: this project\'s\n'
        '       own x64\\$(Configuration)\\ when built alone, and the solution\'s\n'
        '       output directory when built from one - which is what puts the\n'
        '       editor and the compilers it drives in one place. -->\n'
        '  <ItemDefinitionGroup>\n'
        '    <ClCompile>\n'
        '      <LanguageStandard>stdcpp14</LanguageStandard>\n'
        '      <WarningLevel>Level4</WarningLevel>\n'
        '      <TreatWarningAsError>true</TreatWarningAsError>\n'
        '      <ExceptionHandling>Sync</ExceptionHandling>\n'
        '      <ConformanceMode>true</ConformanceMode>\n'
        '      <PreprocessorDefinitions>%s</PreprocessorDefinitions>\n'
        '%s%s'
        '    </ClCompile>\n'
        '    <Link>\n'
        '      <SubSystem>Console</SubSystem>\n'
        '    </Link>\n'
        '%s'
        '  </ItemDefinitionGroup>\n'
        '  <ItemGroup>\n%s  </ItemGroup>\n'
        '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />\n'
        '</Project>\n'
        % (configurations, guid(product), product, per_config,
           ('  <Import Project="$(MSBuildThisFileDirectory)%s" />\n' % props) if props else "",
           target or product,
           definitions,
           ('      <AdditionalIncludeDirectories>%s;%%(AdditionalIncludeDirectories)'
            '</AdditionalIncludeDirectories>\n' % ";".join(includes)) if includes else "",
           ('      <DisableSpecificWarnings>%s</DisableSpecificWarnings>\n'
            % ";".join(disabled)) if disabled else "",
           extra, compiled))


SOLUTION_FOLDER = "{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}"
# A Visual Studio Setup Project (Installer Projects 2022): devenv builds it, MSBuild cannot.
SETUP_PROJECT = "{54435603-DBB4-11D2-8724-00A0C9A8B90C}"


def cc1_guid():
    """cc1's own GUID, read out of the project it already has.

    Not derived like the other two: that file is hand-kept in Compiler-C and
    the solution has to name the GUID it actually uses. Reading it is the only
    way the two cannot drift apart.
    """
    return guid_in(os.path.join(SIBLINGS, CC1_REPO, "ide", "cc1.vcxproj"),
                   "cc1's own project")


def guid_in(where, what):
    """The ProjectGuid a hand-kept .vcxproj already has.

    Two of the four projects in the solution are not written here - cc1's,
    which belongs to another repository, and the window's, which is C++/CLI and
    whose settings were arrived at the hard way. The solution has to name the
    GUID each of them actually uses, and reading it is the only way the two
    cannot drift apart.
    """
    if not os.path.exists(where):
        sys.exit("no %s - the solution needs %s" % (where, what))
    match = re.search(r"<ProjectGuid>(\{[0-9A-Fa-f-]+\})</ProjectGuid>", open(where).read())
    if not match:
        sys.exit("no ProjectGuid in %s" % where)
    return match.group(1).upper()


CC1_GUID = cc1_guid()
CXX1_GUID = guid_in(os.path.join(SIBLINGS, CXX1_REPO, "ide", "cxx1.vcxproj"),
                    "cxx1's own project")
SHC_GUID = guid_in(os.path.join(SIBLINGS, SHC_REPO, "ide", "shc.vcxproj"),
                   "shalimar's own project")
GUI_GUID = guid_in(os.path.join(HERE, "winforms", "RIDEGui.vcxproj"),
                   "the window's own project")


def solution_text(entries):
    """RIDE.sln - the three, with winconsole depending on both compilers.

    A .sln says a dependency with ProjectSection(ProjectDependencies), which
    lists the GUIDs a project must be built after. That is the same idea as the
    Xcode workspace's target dependencies and it is written here for the same
    reason: a change to a compiler and the change to the editor that goes with
    it should be one build.
    """
    out = ("Microsoft Visual Studio Solution File, Format Version 12.00\n"
           "# Visual Studio Version 17\n"
           "VisualStudioVersion = 17.0.31903.59\n"
           "MinimumVisualStudioVersion = 10.0.40219.1\n")

    for name, path, project_guid, after in entries:
        out += 'Project("%s") = "%s", "%s", "%s"\n' % (
            SETUP_PROJECT if path.endswith(".vdproj") else SOLUTION_FOLDER,
            name, path.replace("/", "\\"), project_guid)
        if after:
            out += "\tProjectSection(ProjectDependencies) = postProject\n"
            for other in after:
                out += "\t\t%s = %s\n" % (other, other)
            out += "\tEndProjectSection\n"
        out += "EndProject\n"

    out += ("Global\n"
            "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n"
            "\t\tDebug|x64 = Debug|x64\n\t\tRelease|x64 = Release|x64\n"
            "\tEndGlobalSection\n"
            "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n")
    for _, path, project_guid, _ in entries:
        for c in ("Debug", "Release"):
            if path.endswith(".vdproj"):
                # Built on its own, after staging, by build-installer.bat - a solution build
                # would package whatever the last stage held. Building it in VS is asked by name.
                out += "\t\t%s.%s|x64.ActiveCfg = %s\n" % (project_guid, c, c)
                continue
            out += "\t\t%s.%s|x64.ActiveCfg = %s|x64\n" % (project_guid, c, c)
            out += "\t\t%s.%s|x64.Build.0 = %s|x64\n" % (project_guid, c, c)
    out += ("\tEndGlobalSection\n"
            "\tGlobalSection(SolutionProperties) = preSolution\n"
            "\t\tHideSolutionNode = FALSE\n\tEndGlobalSection\n"
            "EndGlobal\n")
    return out


def rts6x_vcxproj_text():
    """RTS6x on Windows: a Makefile project running its own build.cmd with the cpp11.exe and
    asm6x.exe the solution has just built, then putting rts6x.lib and printf6x.lib, and their Debug
    builds rts6xd.lib and printf6xd.lib, in
    lib\\rts6x-tms6747 beside the editor, where src/toolchain.cpp looks (5.1)."""
    command = ('set "CPP11=$(OutDir)cpp11.exe"\n'
               'set "ASM6X=$(OutDir)asm6x.exe"\n'
               'call "$(ProjectDir)build.cmd" || exit /b 1\n'
               'if not exist "$(OutDir)lib\\rts6x-tms6747" mkdir "$(OutDir)lib\\rts6x-tms6747"\n'
               'for %%l in (rts6x rts6xd printf6x printf6xd) do copy /y "$(ProjectDir)build\\%%l.lib" "$(OutDir)lib\\rts6x-tms6747\\%%l.lib" || exit /b 1\n'
               # Shalimar's C6000 runtime, packed beside it: shalimar.vcxproj wrote its .s, asm6x assembles them, ar6x packs.
               'for %%v in (shmrt6x:shmrt-tms6747 shmrt6xd:shmrt-tms6747-debug) do for /f "tokens=1,2 delims=:" %%a in ("%%v") do (\n'
               '  if exist "$(IntDir)%%a" rmdir /s /q "$(IntDir)%%a"\n'
               '  mkdir "$(IntDir)%%a"\n'
               '  "$(OutDir)asm6x.exe" "$(OutDir)lib\\%%b\\*.s" -o "$(IntDir)%%a" || exit /b 1\n'
               '  dir /b /s "$(IntDir)%%a\\*.obj" > "$(IntDir)%%a.list"\n'
               '  if exist "$(OutDir)lib\\rts6x-tms6747\\%%a.lib" del "$(OutDir)lib\\rts6x-tms6747\\%%a.lib"\n'
               '  "$(ProjectDir)build\\ar6x.exe" -r "$(OutDir)lib\\rts6x-tms6747\\%%a.lib" @"$(IntDir)%%a.list" || exit /b 1\n'
               ')')
    configs = "".join(
        '    <ProjectConfiguration Include="%s|x64">\n'
        '      <Configuration>%s</Configuration>\n'
        '      <Platform>x64</Platform>\n'
        '    </ProjectConfiguration>\n' % (c, c) for c in ("Debug", "Release"))
    return ('<?xml version="1.0" encoding="utf-8"?>\n'
            '<!-- Generated by RIDE\'s tools/make-projects.py; edit that, not this. -->\n'
            '<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
            '  <ItemGroup Label="ProjectConfigurations">\n' + configs +
            '  </ItemGroup>\n'
            '  <PropertyGroup Label="Globals">\n'
            '    <ProjectGuid>' + guid("rts6x") + '</ProjectGuid>\n'
            '    <Keyword>MakeFileProj</Keyword>\n'
            '    <RootNamespace>rts6x</RootNamespace>\n'
            '  </PropertyGroup>\n'
            '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />\n'
            '  <PropertyGroup Label="Configuration">\n'
            '    <ConfigurationType>Makefile</ConfigurationType>\n'
            '    <PlatformToolset>v143</PlatformToolset>\n'
            '  </PropertyGroup>\n'
            '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />\n'
            '  <PropertyGroup>\n'
            '    <NMakeBuildCommandLine>' + command + '</NMakeBuildCommandLine>\n'
            '    <NMakeReBuildCommandLine>' + command + '</NMakeReBuildCommandLine>\n'
            '    <NMakeCleanCommandLine>if exist "$(ProjectDir)build" rmdir /s /q "$(ProjectDir)build"</NMakeCleanCommandLine>\n'
            '  </PropertyGroup>\n'
            '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />\n'
            '</Project>\n')


def installer_xcodeproj_text():
    """packaging/macos/Installer.xcodeproj - the Xcode workspace's last project (2026-10-05).

    One aggregate target whose single step is build-pkg.sh, which builds every
    tool and the window itself and then the .pkg - so building the Installer
    scheme in RIDE.xcworkspace compiles all the other projects first and ends
    with dist/RIDE-5.17-macos.pkg.
    """
    i = lambda *parts: ident("Installer", *parts)
    script = 'cd \\"$PROJECT_DIR/../..\\" && sh packaging/macos/build-pkg.sh 5.17\\n'
    return ("// !$*UTF8*$!\n{\n\tarchiveVersion = 1;\n\tclasses = {\n\t};\n\tobjectVersion = 56;\n\tobjects = {\n\n"
            "\t\t%s = {\n\t\t\tisa = PBXAggregateTarget;\n\t\t\tbuildConfigurationList = %s;\n"
            "\t\t\tbuildPhases = (\n\t\t\t\t%s,\n\t\t\t);\n\t\t\tdependencies = (\n\t\t\t);\n"
            "\t\t\tname = Installer;\n\t\t\tproductName = Installer;\n\t\t};\n"
            "\t\t%s = {\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n\t\t\t);\n\t\t\tsourceTree = \"<group>\";\n\t\t};\n"
            "\t\t%s = {\n\t\t\tisa = PBXProject;\n\t\t\tbuildConfigurationList = %s;\n\t\t\tcompatibilityVersion = \"Xcode 14.0\";\n"
            "\t\t\tdevelopmentRegion = en;\n\t\t\thasScannedForEncodings = 0;\n\t\t\tknownRegions = (\n\t\t\t\ten,\n\t\t\t);\n"
            "\t\t\tmainGroup = %s;\n\t\t\tprojectDirPath = \"\";\n\t\t\tprojectRoot = \"\";\n\t\t\ttargets = (\n\t\t\t\t%s,\n\t\t\t);\n\t\t};\n"
            "\t\t%s = {\n\t\t\tisa = PBXShellScriptBuildPhase;\n\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = (\n\t\t\t);\n"
            "\t\t\tinputPaths = (\n\t\t\t);\n\t\t\tname = \"Build every project, then the .pkg\";\n\t\t\toutputPaths = (\n\t\t\t);\n"
            "\t\t\trunOnlyForDeploymentPostprocessing = 0;\n\t\t\tshellPath = /bin/sh;\n\t\t\tshellScript = \"%s\";\n\t\t\tshowEnvVarsInLog = 0;\n\t\t};\n"
            "\t\t%s = {\n\t\t\tisa = XCBuildConfiguration;\n\t\t\tbuildSettings = {\n\t\t\t};\n\t\t\tname = Debug;\n\t\t};\n"
            "\t\t%s = {\n\t\t\tisa = XCBuildConfiguration;\n\t\t\tbuildSettings = {\n\t\t\t};\n\t\t\tname = Release;\n\t\t};\n"
            "\t\t%s = {\n\t\t\tisa = XCBuildConfiguration;\n\t\t\tbuildSettings = {\n\t\t\t};\n\t\t\tname = Debug;\n\t\t};\n"
            "\t\t%s = {\n\t\t\tisa = XCBuildConfiguration;\n\t\t\tbuildSettings = {\n\t\t\t};\n\t\t\tname = Release;\n\t\t};\n"
            "\t\t%s = {\n\t\t\tisa = XCConfigurationList;\n\t\t\tbuildConfigurations = (\n\t\t\t\t%s,\n\t\t\t\t%s,\n\t\t\t);\n"
            "\t\t\tdefaultConfigurationIsVisible = 0;\n\t\t\tdefaultConfigurationName = Release;\n\t\t};\n"
            "\t\t%s = {\n\t\t\tisa = XCConfigurationList;\n\t\t\tbuildConfigurations = (\n\t\t\t\t%s,\n\t\t\t\t%s,\n\t\t\t);\n"
            "\t\t\tdefaultConfigurationIsVisible = 0;\n\t\t\tdefaultConfigurationName = Release;\n\t\t};\n"
            "\t};\n\trootObject = %s;\n}\n") % (
        i("target"), i("tlist"), i("script"),
        i("group"),
        i("project"), i("plist"), i("group"), i("target"),
        i("script"), script,
        i("pdebug"), i("prelease"), i("tdebug"), i("trelease"),
        i("plist"), i("pdebug"), i("prelease"),
        i("tlist"), i("tdebug"), i("trelease"),
        i("project"))


def workspace_mk_text():
    """The Linux answer, which is a Makefile because that is what Linux has.

    There is no workspace to open on that box and inventing one would be worse
    than using what is already there: four Makefiles that work. This recurses
    into all four and gives ed1 the same dependency it has in the other two,
    so `make -f workspace.mk` builds what the editor drives, then the editor.

    Raw, and it has to be: one recipe continues onto a second line, and a
    trailing backslash in an ordinary string would eat the newline and hand
    make a single line it cannot read.
    """
    return r"""# The three programs, built together. Generated by tools/make-projects.py.
#
#   make -f workspace.mk           what the editor drives, then the editor
#   make -f workspace.mk bin       and all four binaries in bin/
#   make -f workspace.mk check     and run every suite
#   make -f workspace.mk clean
#
# This is the Linux half of what RIDE.xcworkspace is on a Mac and
# RIDE.sln is on Windows: one thing to build, with ed1 after the two
# compilers it drives. It does not reimplement any of their builds - it calls
# the Makefile each repository already has, which is the only way this can stay
# true when one of them changes.
#
# The four are expected side by side. That is the one assumption here, and it
# is the same one the workspace and the solution make.

# Overridable, because they are not called this everywhere. On the Linux box
# the same two repositories are ~/ansicc and ~/shalimar:
#
#   make -f workspace.mk CC1_DIR=$HOME/ansicc SHC_DIR=$HOME/shalimar
# 3.5: the compilers are the VM6747 line - c90 and cpp11 with the three
# host targets and the TMS320C6747, shalimar with its three - and vm6747, the
# emulator that runs the fourth, is built with them. Compiler-C, C++ and
# Compiler-S stay sealed beside.
CC1_DIR ?= ../Toolchain/Compilers/Compiler-Ci
SHC_DIR ?= ../Toolchain/Compilers/Compiler-Si
C2S_DIR ?= ../Toolchain/Compilers/Converter-C2S
CXX1_DIR ?= ../Toolchain/Compilers/C++Optimize
VM_DIR ?= ../Toolchain/TI/VM6747
ASM_DIR ?= ../Toolchain/TI/ASM6x
MASM_DIR ?= ../Toolchain/Windows/MASM
LINK_DIR ?= ../Toolchain/Windows/LINK
LNK6X_DIR ?= ../Toolchain/TI/LNK6x
# 5.0: the C6747 simulator, which runs a linked .out as TI's does, beside the emulator.
SIM_DIR ?= ../Toolchain/TI/SIM6747
# 5.1: RTS6x, the project's own C6747 runtime, which a TI program links against instead of TI's.
RTS_DIR ?= ../Toolchain/TI/RTS6x

# ---- one directory, named once and given to all four ------------------------
#
# Each of the four Makefiles takes a BINDIR saying where its finished program
# goes, and each defaults to its own repository - so building any one of them
# alone is exactly what it always was. This is the only place that overrides
# the four at once, because this is the only build that knows all four exist.
#
# The default is RIDE's own root, which is where RIDE.exe is built and
# therefore the directory the editor searches first: it finds the compilers it
# drives with path::besideProgram, before PATH, so that a compiler shipped with
# this copy is the one this copy runs.
#
# **Built into, not collected into.** The rule this replaces built the three in
# three places and copied them here afterwards, and a copy step is a step that
# can be incomplete - shc.exe arrived without the runtime archives it links,
# which no build and no suite could see and only pressing Run revealed.
# Nothing is copied now; the three are simply told where to write.
#
# Absolute, because each sub-make runs in its own directory and a relative path
# would mean three different places.
#
# **bin/ by default, since the binaries are one directory now.** They used to
# land in the checkout root, scattered among the sources; a build that put five
# programs and a runtime there is hard to tell from the tree they were built
# from. One place, named bin, is where they go - and .gitignore has listed it
# for exactly this. Pass BINDIR= to override, as the product step does.
BINDIR ?= $(CURDIR)/bin
OUT := $(abspath $(BINDIR))

.PHONY: all cc1 cxx1 vm6747 sim6747 asm6x masm link lnk6x rts6x shmrt6x shc c2s editor confirm installer bin check clean

# `installer`, which comes after `confirm`: a workspace build checks that what the
# editor drives is beside it, and then packages it (2026-10-05).
all: installer

cc1:
	$(MAKE) -C $(CC1_DIR) BINDIR=$(OUT) OBJDIR=$(OUT)/obj/cc1

# `tms6747` as well as `all`: the Shalimar runtime for the C6000 is cpp11's
# output, a directory of .s files the emulator takes beside a program, so
# shalimar's build needs cpp11, so it waits for it: said only in a comment, a
# -j2 build on the Linux box started shalimar first and found no cpp11.exe.
shc: cxx1
	$(MAKE) -C $(SHC_DIR) BINDIR=$(OUT) BUILD=$(OUT)/obj/shc all tms6747 CXX1=$(OUT)/cpp11.exe

# The converter. Not a compiler and nothing links it - the editor runs it over
# the open file from the Language menu - but it is found the same way the
# compilers are, beside the editor, so it is built into the same place.
#
# All four are given an object directory as well as a binary directory now.
# Each Makefile already defaults its own objects outside its checkout; this
# names one place for all four so that a workspace build leaves a single
# directory behind and `clean` is one removal. Compiler-S calls the variable
# BUILD rather than OBJDIR, which is why that line reads differently.
c2s:
	$(MAKE) -C $(C2S_DIR) BINDIR=$(OUT) OBJDIR=$(OUT)/obj/c2s

# The C++ compiler, since 3.0, built the way cc1 is: its Makefile takes the
# same two variables. Its headers stay in its own tree - the driver finds
# them beside itself or through the paths compiled into it - so, unlike shc's
# runtime archives, nothing of it has to travel to $(OUT) but the binary.
cxx1:
	$(MAKE) -C $(CXX1_DIR) BINDIR=$(OUT) OBJDIR=$(OUT)/obj/cxx1
vm6747:
	$(MAKE) -C $(VM_DIR) BINDIR=$(OUT) OBJDIR=$(OUT)/obj/vm6747

asm6x:
	$(MAKE) -C $(ASM_DIR) BINDIR=$(OUT) OBJDIR=$(OUT)/obj/asm6x
masm:
	$(MAKE) -C $(MASM_DIR) BINDIR=$(OUT) OBJDIR=$(OUT)/obj/masm
link:
	$(MAKE) -C $(LINK_DIR) BINDIR=$(OUT) OBJDIR=$(OUT)/obj/link
lnk6x:
	$(MAKE) -C $(LNK6X_DIR) BINDIR=$(OUT) OBJDIR=$(OUT)/obj/lnk6x
# Its Makefile names its program vm6747.exe, the emulator's name; TARGET gives it its own.
sim6747:
	$(MAKE) -C $(SIM_DIR) BINDIR=$(OUT) OBJDIR=$(OUT)/obj/sim6747 TARGET=$(OUT)/sim6747.exe

# RTS6x is cpp11's and asm6x's output, so it waits for both, as shc waits for cpp11; its libraries,
# Release and Debug, go to lib/rts6x-tms6747, where the editor looks for them (src/toolchain.cpp).
rts6x: cxx1 asm6x
	$(MAKE) -C $(RTS_DIR) CPP11=$(OUT)/cpp11.exe ASM6X=$(OUT)/asm6x.exe OBJDIR=$(OUT)/obj/rts6x BINDIR=$(OUT)/obj/rts6x-bin
	mkdir -p $(OUT)/lib/rts6x-tms6747
	cp $(OUT)/obj/rts6x-bin/rts6x.lib $(OUT)/obj/rts6x-bin/printf6x.lib \
	   $(OUT)/obj/rts6x-bin/rts6xd.lib $(OUT)/obj/rts6x-bin/printf6xd.lib $(OUT)/lib/rts6x-tms6747/

# Shalimar's C6000 runtime packed as libraries beside RTS6x - shmrt6x.lib from shmrt-tms6747, shmrt6xd.lib from
# shmrt-tms6747-debug: asm6x assembles shc's .s, RTS6x's ar6x packs them. The .s stay for the emulator.
shmrt6x: rts6x shc
	for v in shmrt6x:shmrt-tms6747 shmrt6xd:shmrt-tms6747-debug; do lib=$${v%%:*}; dir=$${v#*:}; \
	  rm -rf $(OUT)/obj/$$lib && mkdir -p $(OUT)/obj/$$lib && \
	  $(OUT)/asm6x.exe "$(OUT)/lib/$$dir/*.s" -o $(OUT)/obj/$$lib/ && \
	  rm -f $(OUT)/lib/rts6x-tms6747/$$lib.lib && \
	  $(OUT)/obj/rts6x-bin/ar6x.exe -r $(OUT)/lib/rts6x-tms6747/$$lib.lib $(OUT)/obj/$$lib/*.obj || exit 1; done

# The dependency, said the same way it is said in the other three: the editor
# is built after the things it drives. Nothing of them ends up inside it.
editor: cc1 cxx1 vm6747 sim6747 asm6x masm link lnk6x rts6x shmrt6x shc c2s
	$(MAKE) BINDIR=$(OUT) OBJDIR=$(OUT)/obj/editor

# Asked of RIDE rather than answered here. The editor is the thing that
# knows what it drives - the list is in its own Makefile, beside the code that
# goes looking for them - and this only calls it once all four have been
# built into one place.
confirm: editor
	$(MAKE) BINDIR=$(OUT) OBJDIR=$(OUT)/obj/editor confirm

# Each suite is that project's own, called by the name that project uses -
# both compilers say `test` and only the editor says `check`. Calling `check`
# on all three was written first and failed on the first run, which is the
# argument for running one of these before believing it.
#
# And cc1's `test` is a Linux suite. It compares against gcc and runs x86-64
# binaries, so on a Mac it refuses and names what does run here instead. That
# refusal is cc1 being right, so this asks the host and runs what that host
# can - rather than the alternatives, which are to skip cc1 on a Mac or to
# ignore a failure and lose the real ones with it.
HOST := $(shell uname -s)

check: confirm
ifeq ($(HOST),Darwin)
	cd $(CC1_DIR) && CC1=$(OUT)/c90.exe ./tests/arm64.sh
	cd $(CC1_DIR) && CC1=$(OUT)/c90.exe ./tests/fingerprint.sh
else
	$(MAKE) -C $(CC1_DIR) test
endif
	cd $(CC1_DIR) && CC1=$(OUT)/c90.exe VM=$(OUT)/vm6747.exe ASM6X=$(OUT)/asm6x.exe LNK6X=$(OUT)/lnk6x.exe SIM6747=$(OUT)/sim6747.exe TIRTS=$(OUT)/lib/rts6x-tms6747 ./tests/tms6747.sh
	cd $(CXX1_DIR) && CXX1=$(OUT)/cpp11.exe VM=$(OUT)/vm6747.exe ASM6X=$(OUT)/asm6x.exe LNK6X=$(OUT)/lnk6x.exe SIM6747=$(OUT)/sim6747.exe TIRTS=$(OUT)/lib/rts6x-tms6747 ./tests/tms6747.sh
# The assembler against asm6x's recorded objects, python3 alone.
	cd $(ASM_DIR) && ASM=$(OUT)/asm6x.exe sh tests/run.sh
# And the x86-64 one against ml64's recorded objects, the same way.
	cd $(MASM_DIR) && ASM=$(OUT)/masm.exe sh tests/run.sh
# The linker against link.exe's recorded images, byte for byte. Its bed exits
# 2 when a probe had to be skipped for want of an input - kernel32.lib is
# Microsoft's and is not checked in; `sh tests/probes.sh` brings it back from
# the Windows box - and 1 when an image differed. A skip is not a failure
# here; a difference is.
	cd $(LINK_DIR) && LINK=$(OUT)/link.exe sh tests/run.sh || [ $$? -eq 2 ]
# The C6000 linker against lnk6x's recorded images, the same way and with the
# same two exits: TI's runtime library is the input that is not checked in.
	cd $(LNK6X_DIR) && LNK=$(OUT)/lnk6x.exe sh tests/run.sh || [ $$? -eq 2 ]
# The simulator's machine-code path and its oracle kit, against the sim6747.exe just built.
	cd $(SIM_DIR) && SIM6747=$(OUT)/sim6747.exe sh tests/c6x-all.sh
# LIBDIR too: Compiler-S's examples suite builds a C library from
# Compiler-C/examples, and this is the only place that knows where Compiler-C
# actually is on this machine - it is ~/ansicc on the Linux box. Without it
# that check found nothing and said nothing.
	$(MAKE) -C $(SHC_DIR) SHC=$(OUT)/shalimar.exe CC1=$(OUT)/c90.exe \
	    LIBDIR=$(abspath $(CC1_DIR))/examples/shalimar-library test
# The Shalimar corpus on the emulator, with the runtime cpp11 built.
	$(MAKE) -C $(SHC_DIR) BINDIR=$(OUT) BUILD=$(OUT)/obj/shc CXX1=$(OUT)/cpp11.exe test-tms6747
# The converter's suite is differential and needs both compilers as oracles.
# It is given the two just built into $(OUT), for the same reason the editor's
# is below: those are the ones this build produced, and they are the ones
# whose behaviour the converter's output is being judged against.
	$(MAKE) -C $(C2S_DIR) BINDIR=$(OUT) OBJDIR=$(OUT)/obj/c2s test CC1=$(OUT)/c90.exe SHC=$(OUT)/shalimar.exe
# cxx1's own suites, against the binary just built into $(OUT) - its Makefile
# runs them on $(TARGET), which BINDIR names. The differential suites ask the
# host's g++ or clang++ for the answers, so they run wherever the editor does.
	$(MAKE) -C $(CXX1_DIR) BINDIR=$(OUT) OBJDIR=$(OUT)/obj/cxx1 test
# The three just built into $(OUT), and not the copies in those repositories'
# own trees. Those are usually the same file and occasionally are not, and the
# occasion is exactly the one worth catching: this build wrote its compilers
# and its converter somewhere, and this is the suite that says whether what it
# wrote works.
#
# Naming the wrong ones does not fail - the editor's suite skips the cases that
# need a compiler and says so quietly - so the count fell from 792 and 232 to
# 686 and 115 and everything still read as green. A suite that skips is not a
# suite that passes.
	$(MAKE) BINDIR=$(OUT) OBJDIR=$(OUT)/obj/editor check CC1=$(OUT)/c90.exe CXX1=$(OUT)/cpp11.exe SHC=$(OUT)/shalimar.exe C2S=$(OUT)/c2s.exe

# The installer is the workspace's last project: after every program is built and
# confirmed, the platform's packager makes it from them - dist/RIDE-$(VER)-linux-x86_64.run
# on Linux, dist/RIDE-$(VER)-macos.pkg on a Mac. NOINSTALLER=1 skips it: the packagers
# pass it when they build the workspace themselves, so neither builds the other.
VER ?= 5.17
installer: confirm
ifneq ($(NOINSTALLER),)
	@echo "installer: skipped (NOINSTALLER)"
else ifeq ($(HOST),Darwin)
	sh packaging/macos/build-pkg.sh $(VER)
else
	BIN=$(OUT) sh packaging/linux/build-run.sh $(VER)
endif

# bin/ is where BINDIR points by default now, so `bin` is just an explicit
# name for the ordinary build - kept so a script or a habit that says `make -f
# workspace.mk bin` still works and lands in the same place.
BIN := bin

bin: all

clean:
	rm -rf $(BIN)
	$(MAKE) -C $(CC1_DIR) BINDIR=$(OUT) OBJDIR=$(OUT)/obj/cc1 clean
	$(MAKE) -C $(SHC_DIR) BINDIR=$(OUT) BUILD=$(OUT)/obj/shc clean
	$(MAKE) -C $(C2S_DIR) BINDIR=$(OUT) OBJDIR=$(OUT)/obj/c2s clean
	$(MAKE) -C $(CXX1_DIR) BINDIR=$(OUT) OBJDIR=$(OUT)/obj/cxx1 clean
	$(MAKE) BINDIR=$(OUT) OBJDIR=$(OUT)/obj/editor clean
	rm -rf $(OUT)/obj
"""


def workspace_text(specs):
    """RIDE.xcworkspace - the three projects, opened together.

    The editor first, because that is what somebody is usually here for, and
    the two compilers under it in the order the editor reaches for them.
    """
    rows = []
    for spec in specs:
        where = os.path.relpath(spec["out"], HERE)
        rows.append('   <FileRef\n      location = "group:%s">\n   </FileRef>\n' % where)
    # The installer last: its target builds every project above it, then the .pkg (2026-10-05).
    rows.append('   <FileRef\n      location = "group:packaging/macos/Installer.xcodeproj">\n   </FileRef>\n')
    return ('<?xml version="1.0" encoding="UTF-8"?>\n<Workspace\n   version = "1.0">\n'
            + "".join(rows) + '</Workspace>\n')


# ---- the projects that are kept by hand ------------------------------------
#
# Two of them, and neither can be generated for the same kind of reason: what
# is in them besides the source list is load-bearing and is not derivable from
# any Makefile.
#
# winforms/RIDEGui.vcxproj is C++/CLI. One file is compiled managed and
# every other file must be compiled native - a /clr translation unit that
# instantiates the same templates the native ones do corrupts the heap before
# main is reached, which is the first hazard in that directory's README. Those
# per-file settings are the project's whole reason for existing.
#
# Compiler-C/msvc/cc1.vcxproj belongs to another repository and works.
#
# So they are checked rather than written. The source list is the part that
# drifts - a file added to a Makefile and forgotten here - and it is the part
# that can be compared. This is what the comment in main() used to promise and
# nothing did: it said the sources were counted "below", and they were not.
def hand_kept_sources(path, inside):
    """The .cpp a hand-kept .vcxproj compiles, as paths inside its repository.

    A .vcxproj names its files relative to itself, so `..\src\Lexer.cpp` in
    msvc/ and `src/Lexer.cpp` from the Makefile are one file written two ways.
    """
    found = set()
    for named in re.findall(r'<ClCompile Include="([^"]+)"', open(path).read()):
        joined = os.path.normpath(os.path.join(inside, named.replace("\\", "/")))
        found.add(joined.replace(os.sep, "/"))
    return found


def window_sources():
    """What the window's project has to compile: the core, and its own two.

    Not TERMINAL_SRC, which is the other front end's drawing - and this is
    exactly the split CORE_SRC was made to write down.
    """
    core = from_makefile(HERE, ("CORE_SRC", "SHM_SRC"))
    return set(core) | {"winforms/bridge.cpp", "winforms/Program.cpp"}


def drift(path, inside, wanted):
    """What a hand-kept project and its Makefile disagree about, in words."""
    if not os.path.exists(path):
        return "there is no %s" % path
    there = hand_kept_sources(path, inside)
    missing = sorted(wanted - there)
    extra = sorted(there - wanted)
    said = []
    if missing:
        said.append("does not compile " + ", ".join(missing))
    if extra:
        said.append("compiles " + ", ".join(extra) + ", which no Makefile names")
    return "; ".join(said)


def named_sources(text, path):
    """Every source file a generated project names, as a set.

    Which files are built is the thing --check is about; the rest of a project
    file is arrangement. That distinction is not fussiness: **Xcode rewrites a
    .pbxproj when it builds it**, sorting the sections by identifier instead of
    by name. A byte-for-byte check therefore fails after every Xcode build and
    says the project has drifted when nothing has, which is a check nobody
    would keep for long.
    """
    if path.endswith(".pbxproj"):
        return set(re.findall(r'/\* ([^ *]+\.cpp) in Sources \*/', text))
    if path.endswith(".vcxproj"):
        return set(re.findall(r'<ClCompile Include="([^"]+)"', text))
    return None


def same_sources(there, wanted_text, path):
    """Whether what is on disk builds what the Makefile says it should.

    For the project formats, the source lists are compared. For everything else
    - the workspace, the solution, workspace.mk - the whole text is, because
    nothing rewrites those and every line in them was put there on purpose.
    """
    mine = named_sources(wanted_text, path)
    if mine is None:
        return there == wanted_text
    return named_sources(there, path) == mine


def main():
    checking = "--check" in sys.argv[1:]
    # GRIDE1: writing would overwrite the shared toolchain's own project files (rts6x.vcxproj, shc.vcxproj, ...)
    # and RIDE.sln, which also builds ar6x there. The toolchain has no Makefiles to read either. Ask only.
    if not checking:
        sys.exit("make-projects.py: in GRIDE1 this only checks (--check); it would overwrite ../toolchain's project files")
    specs = projects()
    # By product rather than by position, since a project joining the list -
    # cxx1 did, in 3.0 - moved every index after it.
    spec_of = {spec["product"]: spec for spec in specs}
    stale = []

    # Before anything is read: the Makefile must be made of the variables this
    # script knows about and no others. Silence here is what cost two builds.
    composition = {"SRC": {"CORE_SRC", "TERMINAL_SRC"},
                   "OBJ": {"SRC", "SHM_SRC", "OBJDIR"}}
    for variable, expected in composition.items():
        found = composed_of(HERE, variable)
        if found != expected:
            print("%s in the Makefile is made of %s, and this script reads %s."
                  % (variable, ", ".join(sorted(found)) or "nothing",
                     ", ".join(sorted(expected))))
            print("Teach tools/make-projects.py about the difference, or every")
            print("project here will build something other than what make builds.")
            return 1

    wanted = [(os.path.join(s["out"], "project.pbxproj"), project_text(s), s["product"])
              for s in specs if not s.get("foreign")]
    wanted.append((os.path.join(HERE, "RIDE.xcworkspace", "contents.xcworkspacedata"),
                   workspace_text(specs), "RIDE.xcworkspace"))

    # ---- Windows -----------------------------------------------------------
    #
    # cc1 already has a project on that machine - Compiler-C/msvc/cc1.vcxproj,
    # kept by hand and working - so the solution references it rather than
    # writing over it. It is not written here; its sources are checked against
    # its Makefile at the end of this function, with the window's.
    windows_sources = [n for n in from_makefile(HERE, EDITOR_VARIABLES)
                       if not n.endswith("terminal.cpp")]
    if "src/terminal_win.cpp" not in windows_sources:
        windows_sources.append("src/terminal_win.cpp")

    wanted.append((os.path.join(HERE, "RIDEConsole.vcxproj"),
                   vcxproj_text("RIDEConsole", sorted(set(windows_sources)),
                                ["_CRT_SECURE_NO_WARNINGS"],
                                target="$(PRODUCT)Console", props="product.props"),
                   "RIDEConsole.vcxproj"))
    # shalimar's is its own ide/shc.vcxproj, runtime step and all, as cc1's and cxx1's are theirs.
    # The converter's, which docs/ANALYSIS.md section 12 scheduled as part of
    # milestone 0 and which was never written. It is generated here rather
    # than by hand for the same reason the others are: its Makefile is five
    # wildcards, and a hand-kept list of twenty-two files drifts.
    wanted.append((os.path.join(SIBLINGS, "Compilers", "Converter-C2S", "c2s.vcxproj"),
                   vcxproj_text("c2s", spec_of["c2s.exe"]["sources"],
                                ["_CRT_SECURE_NO_WARNINGS"]),
                   "c2s.vcxproj"))

    # cxx1's is its own ide/cxx1.vcxproj, as cc1's is ide/cc1.vcxproj: neither is
    # written here. Both put the program beside the rest when a solution other than
    # their own builds them, and --check below says whether they are current.
    # vm6747's, at the root of its checkout: the emulator's msvc/build.cmd
    # passes the one define, and its Makefile the same warnings as cc1's.
    wanted.append((os.path.join(SIBLINGS, VM_REPO, "vm6747.vcxproj"),
                   vcxproj_text("vm6747", spec_of["vm6747.exe"]["sources"],
                                ["_CRT_SECURE_NO_WARNINGS"],
                                includes=("$(ProjectDir)src",)),
                   "vm6747.vcxproj"))
    wanted.append((os.path.join(SIBLINGS, ASM_REPO, "asm6x.vcxproj"),
                   vcxproj_text("asm6x", spec_of["asm6x.exe"]["sources"],
                                ["_CRT_SECURE_NO_WARNINGS"],
                                includes=("$(ProjectDir)src",)),
                   "asm6x.vcxproj"))
    wanted.append((os.path.join(SIBLINGS, MASM_REPO, "masm.vcxproj"),
                   vcxproj_text("masm", spec_of["masm.exe"]["sources"],
                                ["_CRT_SECURE_NO_WARNINGS"],
                                includes=("$(ProjectDir)src",)),
                   "masm.vcxproj"))
    # The linker's product is link.exe, the name of the thing it stands in
    # for. A project may be called link - MSBuild's Link task is a target, not
    # a project - and bin\link.exe beside the editor is not found by accident:
    # a build's bare `link.exe` is resolved by cmd from the current directory
    # (the project's) and then PATH, which vcvars starts with Microsoft's.
    wanted.append((os.path.join(SIBLINGS, LINK_REPO, "link.vcxproj"),
                   vcxproj_text("link", spec_of["link.exe"]["sources"],
                                ["_CRT_SECURE_NO_WARNINGS"],
                                includes=("$(ProjectDir)src",)),
                   "link.vcxproj"))
    # lnk6x.exe shares its name with TI's, as asm6x.exe does; the editor runs
    # TI's by full path under the "ti" directory, never by PATH.
    wanted.append((os.path.join(SIBLINGS, LNK6X_REPO, "lnk6x.vcxproj"),
                   vcxproj_text("lnk6x", spec_of["lnk6x.exe"]["sources"],
                                ["_CRT_SECURE_NO_WARNINGS"],
                                includes=("$(ProjectDir)src",)),
                   "lnk6x.vcxproj"))
    wanted.append((os.path.join(SIBLINGS, SIM_REPO, "sim6747.vcxproj"),
                   vcxproj_text("sim6747", spec_of["sim6747.exe"]["sources"],
                                ["_CRT_SECURE_NO_WARNINGS"],
                                includes=("$(ProjectDir)src",)),
                   "sim6747.vcxproj"))

    wanted.append((os.path.join(SIBLINGS, RTS_REPO, "rts6x.vcxproj"), rts6x_vcxproj_text(), "rts6x.vcxproj"))

    entries = [
        # Laid out on the Windows box as it is here: Compiler-Ci,
        # C++Optimize, Compiler-Si and VM6747 beside
        # this checkout, which is where tools/to-windows.sh puts them.
        ("c90", "../Toolchain/" + CC1_REPO.replace(os.sep, "/") + "/ide/cc1.vcxproj", CC1_GUID, []),
        ("cpp11", "../Toolchain/" + CXX1_REPO.replace(os.sep, "/") + "/ide/cxx1.vcxproj", CXX1_GUID, []),
        ("vm6747", "../Toolchain/" + VM_REPO.replace(os.sep, "/") + "/vm6747.vcxproj", guid("vm6747"), []),
        ("asm6x", "../Toolchain/" + ASM_REPO + "/asm6x.vcxproj", guid("asm6x"), []),
        ("masm", "../Toolchain/" + MASM_REPO + "/masm.vcxproj", guid("masm"), []),
        ("link", "../Toolchain/" + LINK_REPO + "/link.vcxproj", guid("link"), []),
        ("lnk6x", "../Toolchain/" + LNK6X_REPO + "/lnk6x.vcxproj", guid("lnk6x"), []),
        ("sim6747", "../Toolchain/" + SIM_REPO + "/sim6747.vcxproj", guid("sim6747"), []),
        # RTS6x after cpp11 and asm6x, whose output it is (rts6x_vcxproj_text).
        ("rts6x", "../Toolchain/" + RTS_REPO + "/rts6x.vcxproj", guid("rts6x"), [CXX1_GUID, guid("asm6x"), SHC_GUID]),
        # shalimar after cpp11: its post-build step compiles the Shalimar runtime
        # for the C6000 with the cpp11.exe beside it (shc_runtime_step).
        ("shalimar", "../Toolchain/" + SHC_REPO.replace(os.sep, "/") + "/ide/shc.vcxproj", SHC_GUID, [CXX1_GUID]),
        # c2s is built with them and not by them: the editor runs it over the
        # open file from the Language menu, and finds it beside itself the
        # same way it finds the compilers.
        ("c2s", "../Toolchain/Compilers/Converter-C2S/c2s.vcxproj", guid("c2s"), []),
        # the editor after both, which is the dependency this whole thing is
        # for - said in a .sln the way the workspace says it in a .xcodeproj.
        ("RIDEConsole", "RIDEConsole.vcxproj", guid("RIDEConsole"), [CC1_GUID, CXX1_GUID, guid("vm6747"), guid("asm6x"), guid("masm"), guid("link"), guid("lnk6x"), guid("sim6747"), guid("rts6x"), guid("shalimar"), guid("c2s")]),
        # The window, on the same footing as the console half. It is in the
        # solution for two reasons: so that one build makes all four, and
        # because being in a solution is what moves its output into the
        # solution's directory beside the rest - the C++ default does that on
        # its own, so the project file itself needs no OutDir. That matters
        # here: winforms/RIDEGui.vcxproj carries a warning that an earlier
        # version of it set OutDir, IntDir, BasicRuntimeChecks and a platform
        # version, and the binary died at startup with heap corruption before
        # main. Nothing in that file is touched to get this.
        ("RIDEGui", "winforms/RIDEGui.vcxproj", GUI_GUID, [CC1_GUID, CXX1_GUID, guid("vm6747"), guid("asm6x"), guid("masm"), guid("link"), guid("lnk6x"), guid("sim6747"), guid("rts6x"), guid("shalimar"), guid("c2s")]),
    ]
    # The installer, after every project above - said once, with all their GUIDs (2026-10-05).
    # A Visual Studio Setup Project since 2026-10-07: packaging/windows/make-setup.ps1 writes
    # its file list from the staged tree, so it is not written here.
    entries.append(("Installer", "packaging/windows/Installer.vdproj", guid("Installer"),
                     [e[2] for e in entries]))
    wanted.append((os.path.join(HERE, "packaging", "macos", "Installer.xcodeproj", "project.pbxproj"),
                   installer_xcodeproj_text(), "packaging/macos/Installer.xcodeproj"))
    wanted.append((os.path.join(HERE, "RIDE.sln"), solution_text(entries),
                   "RIDE.sln"))

    # ---- Linux -------------------------------------------------------------
    wanted.append((os.path.join(HERE, "workspace.mk"), workspace_mk_text(),
                   "workspace.mk"))

    # The compilers' own ide/ projects, written by their own generate.py: asked, never written.
    for repo in (CC1_REPO, CXX1_REPO, SHC_REPO):
        gen = os.path.join(SIBLINGS, repo, "ide", "generate.py")
        if subprocess.run([sys.executable, gen, "--check"], stdout=subprocess.DEVNULL).returncode != 0:
            stale.append("%s/ide (run its ide/generate.py)" % repo.replace(os.sep, "/"))

    # The one kept by hand, checked and never written - see hand_kept_sources.
    for what, path, inside, wanted_sources in (
            ("winforms/RIDEGui.vcxproj",
             os.path.join(HERE, "winforms", "RIDEGui.vcxproj"),
             "winforms", window_sources()),):
        wrong = drift(path, inside, wanted_sources)
        if wrong:
            stale.append("%s (%s)" % (what, wrong))

    for path, text, what in wanted:
        if checking:
            there = open(path).read() if os.path.exists(path) else None
            if there is None or not same_sources(there, text, path):
                stale.append(what)
            continue
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write(text)

    if stale:
        print("out of date with the Makefiles:\n  " + "\n  ".join(stale))
        print("A project that builds fewer files than make does is not an error -")
        print("it is a smaller program, and nothing says so.")
        print("  python3 tools/make-projects.py       for the generated ones")
        print("  python3 <compiler>/ide/generate.py  for cc1's, cxx1's and shc's, which are theirs")
        print("  the window's project is edited by hand, on purpose")
        return 1

    if checking:
        print("all five projects and the workspace are what the Makefiles say,")
        print("and so are cc1's, cxx1's and shc's own ide/ projects, and the window's, kept by hand")
        return 0

    for spec in specs:
        print("%-4s %d sources, %d headers  ->  %s"
              % (spec["product"], len(spec["sources"]), len(spec["headers"]),
                 os.path.relpath(spec["out"], SIBLINGS)))
    print("RIDE.xcworkspace  opens all five on a Mac")
    print("RIDE.sln          all six for Visual Studio 2022")
    print("workspace.mk         and for make on the Linux box")
    return 0


if __name__ == "__main__":
    sys.exit(main())
