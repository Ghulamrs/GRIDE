#!/usr/bin/env python3
"""Washout: a copy of GRIDEd and every project it depends on - their whole source, nothing built.

    python washout.py PARENT            write the copy into PARENT\\GRIDEd
    python washout.py PARENT --force    replace PARENT\\GRIDEd if it is already there
    python washout.py PARENT --zip      and pack it as PARENT\\GRIDEd.zip, GRIDEd\\ its one top folder

**Everything the projects are, and nothing they make** (2026-10-15, moved here from RIDE-4.7):
this repository - GRIDEd\\ with its docs, lib\\ccs, toolchain.json and samples - and each
repository beside it that GRIDE's toolchain is built from:

    Compilers\\  Compiler-Ci (c90), C++Optimize (cpp11), Compiler-Si (shalimar), Converter-C2S (c2s)
    TI\\         ASM6x, LNK6x, RTS6x, SIM6747, VM6747
    Windows\\    MASM, LINK

A repository goes whole: every file git tracks in it, as it is on disk (a change not yet
committed included), and a tracked file missing from the disk as git has it. No file is left
out for its kind - tests, scripts, docs, binaries git tracks all go. What git does not track
does not: x64\\, obj\\, logs and test output are what building and testing made. .git does not.
GRIDE 2.0\\ does not either (2026-10-15): the compact IDE is not part of the solution.

The copy has one layout, the Windows box's: Compilers\\, TI\\ and Windows\\ inside GRIDEd\\, side
by side, because their projects and scripts name each other by relative path (shc.sln reaches
..\\..\\C++Optimize, RTS6x's build.cmd the built cpp11.exe). Build it as CLAUDE.md says.
Each repository is looked for inside this one (the Windows box) and then beside it (the Mac,
where Compilers/, TI/ and Windows/ sit beside the GRIDE checkout); either way the copy is the same.
"""
import os
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.abspath(__file__))
NAME = "GRIDEd"                  # the copy's folder, whatever this checkout is called (GRIDE on the Mac)
LEFT_OUT = ["GRIDE 2.0"]         # in this repository, not part of the solution

# The repositories beside this one that GRIDE's toolchain is built from, as this tree lays them out.
SIBLINGS = [
    "Compilers/Compiler-Ci",
    "Compilers/C++Optimize",
    "Compilers/Compiler-Si",
    "Compilers/Converter-C2S",
    "TI/ASM6x",
    "TI/LNK6x",
    "TI/RTS6x",
    "TI/SIM6747",
    "TI/VM6747",
    "Windows/MASM",
    "Windows/LINK",
]


def find(rel):
    """Where the repository rel is here: inside this tree, or beside it; None when neither."""
    for base in (ROOT, os.path.dirname(ROOT)):
        repo = os.path.join(base, rel)
        if os.path.isdir(os.path.join(repo, ".git")):
            return repo
    return None


def git(repo, *args):
    return subprocess.run(["git", "-c", "core.quotepath=off"] + list(args), cwd=repo,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout


def tracked(repo):
    """(mode, blob, path) of every entry git tracks in repo."""
    out = []
    for entry in git(repo, "ls-files", "-s", "-z").decode("utf-8").split("\0"):
        if entry:
            info, path = entry.split("\t", 1)
            mode, blob, _stage = info.split()
            out.append((mode, blob, path))
    return out


def wash(repo, dest, left_out=()):
    """Copy every tracked file of repo into dest; return (copied, restored, submodules) -
    restored are the tracked files missing from the disk, written as git has them."""
    copied, restored, submodules = 0, [], []
    for mode, blob, path in tracked(repo):
        if any(path.startswith(d + "/") for d in left_out):
            continue
        if mode == "160000":
            submodules.append(path)      # a submodule is a repository of its own, not a file
            continue
        src = os.path.join(repo, path)
        target = os.path.join(dest, path)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        if os.path.isfile(src):
            shutil.copy2(src, target)
        else:
            with open(target, "wb") as fh:
                fh.write(git(repo, "cat-file", "blob", blob))
            restored.append(path)
        copied += 1
    return copied, restored, submodules


def pack(dest):
    """Zip dest beside itself, the folder's own name the zip's one top folder; return the zip's path."""
    archive = dest + ".zip"
    top = os.path.dirname(dest)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for where, subdirs, names in os.walk(dest):
            subdirs.sort()
            for n in sorted(names):
                path = os.path.join(where, n)
                z.write(path, os.path.relpath(path, top))
    return archive


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    if len(args) != 1:
        print(__doc__)
        return 2
    parent = os.path.abspath(args[0])
    dest = os.path.join(parent, NAME)
    archive = dest + ".zip"

    missing = [s for s in SIBLINGS if find(s) is None]
    if missing:
        for s in missing:
            print("  %-26s MISSING - a repository neither inside nor beside %s" % (s, ROOT))
        print("washout: nothing written - every project must be here for the copy to build"); return 1
    repos = [(NAME, ROOT, "")] + [(rel, find(rel), rel) for rel in SIBLINGS]

    inside = lambda a, b: os.path.normcase(a + os.sep).startswith(os.path.normcase(b + os.sep))
    # nor inside Compilers\\, TI\\ or Windows\\, which hold the repositories
    for where in sorted({ROOT} | {os.path.dirname(repo) for _, repo, _ in repos[1:]}):
        if inside(dest, where) or inside(where, dest):
            print("washout: the copy cannot go inside %s, nor hold it" % where); return 2
    if os.path.exists(dest) and os.listdir(dest):
        if "--force" not in argv:
            print("washout: %s is not empty - give --force to replace it" % dest); return 2
        shutil.rmtree(dest)
    if "--zip" in argv and os.path.exists(archive):
        if "--force" not in argv:
            print("washout: %s is already there - give --force to replace it" % archive); return 2
        os.remove(archive)

    total, all_restored, all_submodules = 0, [], []
    # this repository first; where the siblings are inside it, .git\info\exclude keeps them from its list
    for name, repo, rel in repos:
        copied, restored, submodules = wash(repo, os.path.join(dest, rel), LEFT_OUT if repo == ROOT else ())
        print("  %-26s %5d files%s" % (name, copied,
                                       ", %d restored from git" % len(restored) if restored else ""))
        total += copied
        all_restored += [name + "/" + r for r in restored]
        all_submodules += [name + "/" + s for s in submodules]
    for r in all_restored:
        print("    restored %s" % r)
    for s in all_submodules:
        print("    submodule not copied: %s" % s)
    print("washout: %d files in %s" % (total, dest))
    if "--zip" in argv:
        print("washout: %s, %.1f MB" % (pack(dest), os.path.getsize(archive) / 1048576.0))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except (OSError, subprocess.CalledProcessError) as e:
        print("washout: %s" % e); sys.exit(1)
