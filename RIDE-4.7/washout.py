#!/usr/bin/env python3
"""Washout: a copy of RIDE and what it builds that holds what compiling needs, and nothing else.

    python3 washout.py DEST              copy RIDE's sources into DEST
    python3 washout.py DEST --workspace  and each project RIDE drives, beside it
    python3 washout.py DEST --force      replace DEST if it is already there

**What compiling needs, and no more** (2026-09-30): the sources - src/ and every
directory under it - the projects that build them - each compiler's ide/, and a
tool's own .vcxproj and .xcodeproj where it has no ide/ - and the headers those
projects compile against, lib/ and include/. Three more because the projects name
them: msvc/compat (the <unistd.h> MSVC lacks, for cc1 and cxx1), Shalimar's
runtime/ (its projects build the runtime from it) and, for RIDE, macos/ and
winforms/ (its two windows), product.props, and help/, projects/ and programs/,
which the macOS window's project copies into RIDE.app and cannot build without.
RIDE's projects/ and programs/ go as their sources (2026-10-02), the CCS
samples' .project, .cproject and .ccsproject with them, and the installers'
settings.json. **And the installer project of every target** (2026-10-05): packaging/ whole - Windows'
setup project (Installer.vdproj and Installer.sln, a Visual Studio Installer Projects 2022 project
since 2026-10-07, Inno retired) with build-installer.bat, stage.cmd and make-setup.ps1, the Xcode workspace's
Installer.xcodeproj with build-pkg.sh, Linux's build-run.sh and install-header.sh - with workspace.mk and each
project's Makefile, which the macOS and Linux installers build through, and docs/ and README.md, which every
installer ships. Nothing else goes: no test, seal or other script. A source directory keeps only
source and project files; ide/, lib/, include/, msvc/compat and packaging/ are whole.

What was measured: every path the solutions, projects and workspaces name was
listed and is here, and a copy builds - RIDE.sln on Windows, RIDE.xcworkspace and
each ide/ alone on the Mac. Binaries are dropped, the icons kept on purpose.
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))

# (project, whole directories, source directories, named files): what compiling needs.
# help/, projects/ and programs/ because the macOS window's project copies them into
# RIDE.app as it builds, and fails without them; the Windows projects do not use them.
# packaging/, workspace.mk, the Makefile, docs/ and README.md: the installer projects of the three targets,
# what they build through and what they ship (2026-10-05). The Windows setup project is named outright
# (2026-10-07): RIDE.sln lists it, so a washed copy without it opens with a project missing.
RIDE_PLAN = (os.path.basename(ROOT), ["Editor.xcodeproj", "RIDE.xcworkspace", "help", "projects", "programs",
                                      "packaging", "docs"],
             ["src", "macos", "winforms"],
             ["RIDE.sln", "RIDEConsole.vcxproj", "product.props", "workspace.mk", "Makefile", "README.md",
              "packaging/windows/Installer.vdproj", "packaging/windows/Installer.sln",
              "packaging/windows/make-setup.ps1"])
WORKSPACE = [
    ("../toolchain/Compilers/Compiler-Ci", ["ide", "lib", "msvc/compat"], ["src"], ["Makefile"]),
    ("../toolchain/Compilers/C++Optimize", ["ide", "lib", "include", "msvc/compat"], ["src"], ["Makefile"]),
    ("../toolchain/Compilers/Compiler-Si", ["ide"], ["src", "runtime"], ["Makefile"]),
    ("../toolchain/TI/VM6747", ["vm6747.xcodeproj"], ["src"], ["vm6747.vcxproj", "Makefile"]),
    ("../toolchain/TI/SIM6747", ["sim6747.xcodeproj"], ["src"], ["sim6747.vcxproj", "Makefile"]),
    ("../toolchain/Compilers/Converter-C2S", ["c2s.xcodeproj"], ["src"], ["c2s.vcxproj", "Makefile"]),
    ("../toolchain/TI/ASM6x", ["asm6x.xcodeproj"], ["src"], ["asm6x.vcxproj", "Makefile"]),
    ("../toolchain/Windows/MASM", ["masm.xcodeproj"], ["src"], ["masm.vcxproj", "Makefile"]),
    ("../toolchain/Windows/LINK", ["link.xcodeproj"], ["src"], ["link.vcxproj", "Makefile"]),
    ("../toolchain/TI/LNK6x", ["lnk6x.xcodeproj"], ["src"], ["lnk6x.vcxproj", "Makefile"]),
    # The C6000 run-time (5.1): its sources, assembly included, and the ar6x that packs them.
    ("../toolchain/TI/RTS6x", [], ["src", "tools/ar6x"], ["rts6x.vcxproj", "Makefile", "build.cmd", "printf6x.members"]),
]

# What a source directory keeps: source, and the project files kept beside it (macos/Window.xcodeproj).
SOURCE_EXT = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".inc", ".s", ".m", ".mm", ".rc", ".def",
              ".manifest", ".plist", ".ico", ".icns", ".props", ".sln", ".vcxproj", ".vdproj", ".filters",
              ".pbxproj", ".xcworkspacedata", ".xcscheme", ".xcsettings"}

BINARY_EXT = {".exe", ".com", ".obj", ".o", ".lib", ".a", ".dll", ".dylib", ".so", ".pdb",
              ".ilk", ".exp", ".idb", ".pch", ".ipch", ".out", ".hex", ".bin", ".elf",
              ".zip", ".gz", ".tgz", ".pkg", ".run", ".dmg", ".msi", ".cab", ".class",
              ".jar", ".pyc", ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".pdf", ".res",
              ".tlog", ".lastbuildstate", ".suo", ".db", ".sdf", ".xcuserstate"}
KEEP_BINARY_EXT = {".ico", ".icns"}
# The dot-files that are a project's source: a CCS project is these three files and its sources.
KEEP_DOTFILES = {".project", ".cproject", ".ccsproject"}
SKIP_DIRS = {"build", "bin", "obj", "x64", "x86", "Debug", "Release", "DerivedData",
             "xcuserdata", ".vs", ".git", "dist", "tests", "test", "__pycache__"}


def is_binary(path):
    """Binary by extension, or by contents: a NUL byte, or bytes that are not UTF-8."""
    ext = os.path.splitext(path)[1].lower()
    if ext in KEEP_BINARY_EXT:
        return False
    if ext in BINARY_EXT:
        return True
    with open(path, "rb") as fh:
        data = fh.read()
    if b"\0" in data:
        return True
    try:
        data.decode("utf-8")
    except UnicodeDecodeError:
        return True
    return False


def tracked(project):
    """Files git tracks in the project, or None when it is not a checkout."""
    try:
        out = subprocess.run(["git", "ls-files", "-z"], cwd=project, stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    return {n for n in out.decode().split("\0") if n}


def candidates(project, dirs, files):
    """Every file under dirs, and the named files, relative to project."""
    out = []
    for d in dirs:
        top = os.path.join(project, d)
        for where, subdirs, names in os.walk(top):
            subdirs[:] = sorted(s for s in subdirs if s not in SKIP_DIRS
                                and not s.endswith(".dSYM") and not s.startswith("."))
            for n in sorted(names):
                if not n.startswith(".") or n in KEEP_DOTFILES:
                    out.append(os.path.relpath(os.path.join(where, n), project))
    for f in files:
        if os.path.isfile(os.path.join(project, f)):
            out.append(f)
    return out


def wash(project, dest, dirs, files, sources=()):
    """Copy what compiling needs into dest - dirs whole, sources filtered to source and project
    files, and the named files; return (kept, dropped)."""
    known = tracked(project)
    kept, dropped = 0, []
    wanted = [(r, False) for r in candidates(project, dirs, files)] + \
             [(r, True) for r in candidates(project, sources, [])]
    seen = set()                         # a named file inside a whole directory is listed twice: once
    wanted = [w for w in wanted if not (w[0] in seen or seen.add(w[0]))]
    for rel, filtered in wanted:
        rel_posix = rel.replace(os.sep, "/")
        if known is not None and rel_posix not in known:
            continue                     # an untracked file is a build product or a stray
        if filtered and os.path.splitext(rel)[1].lower() not in SOURCE_EXT:
            continue                     # a README, a Makefile, a script: not what compiling needs
        src = os.path.join(project, rel)
        if is_binary(src):
            dropped.append(rel_posix)
            continue
        target = os.path.join(dest, rel)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copy2(src, target)
        kept += 1
    return kept, dropped


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    if len(args) != 1:
        print(__doc__)
        return 2
    dest = os.path.abspath(args[0])
    if os.path.abspath(dest).startswith(ROOT + os.sep) or dest == ROOT:
        print("washout: the copy cannot go inside the tree it washes"); return 2
    workspace = "--workspace" in argv
    ride_name = RIDE_PLAN[0]                     # this tree's own folder name, RIDE-4.7 - never a version written here
    ride_dest = os.path.join(dest, ride_name) if workspace else dest
    if os.path.exists(dest) and os.listdir(dest):
        if "--force" not in argv:
            print("washout: %s is not empty - give --force to replace it" % dest); return 2
        shutil.rmtree(dest)

    total_kept, total_dropped = 0, []
    _, whole, sources, files = RIDE_PLAN
    kept, dropped = wash(ROOT, ride_dest, whole, files, sources)
    print("  %-26s %4d files kept, %d binaries dropped" % (ride_name, kept, len(dropped)))
    total_kept += kept; total_dropped += [ride_name + "/" + d for d in dropped]
    if workspace:
        for rel, whole, sources, files in WORKSPACE:
            project = os.path.normpath(os.path.join(ROOT, rel))
            if not os.path.isdir(project):
                print("  %-26s MISSING - not beside RIDE here" % rel); continue
            name = os.path.relpath(project, os.path.dirname(ROOT))
            kept, dropped = wash(project, os.path.join(dest, name), whole, files, sources)
            print("  %-26s %4d files kept, %d binaries dropped" % (name, kept, len(dropped)))
            total_kept += kept; total_dropped += [name + "/" + d for d in dropped]
    for d in total_dropped:
        print("    dropped %s" % d)
    print("washout: %d source files in %s" % (total_kept, dest))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
