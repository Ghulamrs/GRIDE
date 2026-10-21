#!/usr/bin/env python3
"""Washout: a copy of the GRIDE tree - the three outlooks and the toolchain they build - its whole source, nothing built.

    python washout.py PARENT              write the copy into PARENT\\<tree>
    python washout.py PARENT --force      replace PARENT\\<tree> if it is already there
    python washout.py PARENT --zip        and pack it as PARENT\\<tree>.zip, <tree>\\ its one top folder
    python washout.py PARENT --tree DIR   wash the tree at DIR rather than the one this file is in
    python washout.py PARENT --name NAME  call the copy NAME rather than <tree> (GRIDEx3, say)

<tree> is the tree's own folder name, and every folder in the copy keeps its own: nothing is renamed but the
copy's top folder, and only when --name asks.

**Everything the projects are, and nothing they make** (2026-10-15; the shared-toolchain tree, 2026-10-16;
GRIDEx3 rearranged, 2026-10-22): the tree as its README.md lays it out -

    GRIDEd\\      Black - GRIDEd; GRIDE.sln builds GRIDE and the eleven tools into GRIDEd\\x64\\<Config>
    GRIDE 2.0\\   Grey - GRIDE 2.0; its GRIDE.sln builds GRIDE and the same eleven tools into its own x64\\<Config>
    GRIDE 1.0\\   White - The Simple; RIDE 4.7's window over RIDE's portable core in src\\, its GRIDE.sln the same
                 build into its own x64\\<Config>
    toolchain\\   the tools the three solutions build, as ..\\toolchain\\...: Compilers\\ (c90, cpp11, shalimar,
                 c2s), TI\\ (asm6x, lnk6x, rts6x and its ar6x, sim6747, vm6747), Windows\\ (masm, link);
                 MANIFEST.md, copy-toolchain.py, msbuild\\Toolchain.targets
    examples\\    the examples the three share: projects\\ (the .pro projects, the CCS three, cppparse's .sln),
                 programs\\ (single files), toolchain-demo\\
    docs\\        the one Help the three share
    installer\\   windows\\ (the MSI's scripts) and macos\\
    macos\\       the editor's Mac build (RIDE's Window.xcodeproj)

The tree goes whole: every file git tracks in it, as it is on disk (a change not yet committed included),
and a tracked file missing from the disk as git has it. No file is left out for its kind - tests, scripts,
docs, binaries git tracks all go. What git does not track does not: x64\\, obj\\, the tools' build\\,
installer\\dist\\, .vs\\, what building an example leaves beside it are what building and testing made. .git
does not. The three outlooks go. The tool trees under toolchain\\ are the minimal copies this tree keeps
(toolchain\\MANIFEST.md), not repositories of their own: they go as the tree has them, and washout does not
reach for the old tool repositories (the Compilers\\, TI\\ and Windows\\ a working folder may still keep
beside the outlooks).

A tree that is not a git working copy (a copy brought to the Mac without .git) is taken from the disk
instead, less what building makes (BUILT) and the file system's own litter (LITTER); washout says so.

Then it checks that the copy builds the way the tree does: every project each .sln names, and every file,
import and project reference those projects name, is in the copy. A project a solution names by an absolute
path is outside any copy; washout lists it, and the solution loads without it. An item a project only lists (None, Text, Image, Content - cppparse's
..\\README.md, runtime\\ and libcpp\\ today) is not read by the build: one the tree lacks is listed, not fatal.
Build the copy as README.md says, one solution at a time (W7).
"""
import ntpath
import os
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.abspath(__file__))

# Taken from the disk only (no git): folders building makes, as paths in the tree - the .gitignore's own list.
# A source folder may share a name with one - GRIDE 2.0\src\Debug is source - so that one is kept (SOURCE).
BUILT = [
    re.compile(r"(^|/)(x64|x86|obj|bin|Debug|Release|\.vs|\.git)(/|$)"),   # a solution's or an example's output
    re.compile(r"^toolchain/.*/build(/|$)"),                   # a tool's IntDir, ar6x's own output
    re.compile(r"^installer/dist(/|$)"),                       # the MSI and its checks
    re.compile(r"^examples/.*/(build|out|\.gride)(/|$)"),      # what an outlook builds an example into
    re.compile(r"^(Compilers|TI|Windows)(/|$)"),               # the old tool repositories beside the outlooks
]
SOURCE = re.compile(r"^GRIDE 2\.0/src/Debug(/|$)")             # GRIDE 2.0's debugger, named like a build folder
LITTER = {".DS_Store", "Thumbs.db", "desktop.ini"}
# and Visual Studio's per-user files and Python's caches, as the .gitignore names them
LITTER_ENDS = (".user", ".suo", ".aps", ".VC.db", ".VC.opendb", ".pyc")

SLN_PROJECT = re.compile(r'^Project\("\{[^}]+\}"\) = "([^"]+)", "([^"]+)"', re.M)
PROJECT_ITEM = re.compile(r'<(ClCompile|ClInclude|ResourceCompile|None|Text|Image|CustomBuild|Content|MASM'
                          r'|ProjectReference|Import) (?:Include|Project)="([^"]+)"')
LISTED_ONLY = {"None", "Text", "Image", "Content"}   # shown in Solution Explorer; the build never reads them


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


def on_disk(tree):
    """(None, None, path) of every file in tree that building did not make - for a tree without git."""
    out = []
    for where, subdirs, names in os.walk(tree):
        rel = os.path.relpath(where, tree).replace(os.sep, "/")
        rel = "" if rel == "." else rel + "/"
        subdirs[:] = sorted(d for d in subdirs
                            if SOURCE.search(rel + d) or not any(b.search(rel + d) for b in BUILT))
        out += [(None, None, rel + n) for n in sorted(names) if n not in LITTER and not n.endswith(LITTER_ENDS)]
    return out


def wash(tree, dest, entries):
    """Copy entries of tree into dest; return (copied by top folder, restored, submodules) -
    restored are the tracked files missing from the disk, written as git has them."""
    copied, restored, submodules = {}, [], []
    for mode, blob, path in entries:
        if mode == "160000":
            submodules.append(path)      # a submodule is a repository of its own, not a file
            continue
        src = os.path.join(tree, path)
        target = os.path.join(dest, path)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        if os.path.isfile(src):
            shutil.copy2(src, target)
        else:
            with open(target, "wb") as fh:
                fh.write(git(tree, "cat-file", "blob", blob))
            restored.append(path)
        top = path.split("/", 1)[0] if "/" in path else "(top files)"
        copied[top] = copied.get(top, 0) + 1
    return copied, restored, submodules


def resolve(base, name, dest):
    """Where name, as a project or solution writes it relative to base, is: (path, None), or (None, why)."""
    if ntpath.isabs(name) or name.startswith("\\\\"):
        return None, "absolute"
    path = os.path.normpath(os.path.join(base, name.replace("\\", os.sep)))
    if os.path.relpath(path, dest).startswith(os.pardir):
        return None, "outside"
    return path, None


def check(dest):
    """Every project the copy's solutions name, and every item those projects name, is in the copy.
    Return (missing, listed, outside): what the build needs and is not there; what a project only lists and
    is not there (the tree lacks it too, or it would have been copied); what no copy can hold."""
    missing, listed, outside, projects = [], [], [], []
    for where, subdirs, names in os.walk(dest):
        subdirs.sort()
        for sln in sorted(n for n in names if n.endswith(".sln")):
            path = os.path.join(where, sln)
            text = open(path, encoding="utf-8-sig", errors="replace").read()
            named = [(n, p) for n, p in SLN_PROJECT.findall(text) if p.lower().endswith("proj")]
            inside = 0
            for name, rel in named:
                proj, why = resolve(where, rel, dest)
                if why:
                    outside.append("%s: %s  %s" % (os.path.relpath(path, dest), name, rel))
                elif not os.path.isfile(proj):
                    missing.append("%s: project %s  %s" % (os.path.relpath(path, dest), name, rel))
                else:
                    inside += 1
                    projects.append(proj)
            print("  %-36s %2d projects, %d in the copy" % (os.path.relpath(path, dest), len(named), inside))
    seen, items = set(), 0
    while projects:                      # a project reference brings its project in, as the build does
        proj = projects.pop()
        if os.path.normcase(proj) in seen:
            continue
        seen.add(os.path.normcase(proj))
        text = open(proj, encoding="utf-8-sig", errors="replace").read()
        for kind, names in PROJECT_ITEM.findall(text):
            for item in names.split(";"):
                if not item or "$(" in item or "%(" in item or "*" in item:
                    continue             # MSBuild's own, a property's or a wildcard's: the build resolves those
                path, why = resolve(os.path.dirname(proj), item, dest)
                items += 1
                if why or not os.path.exists(path):
                    (listed if kind in LISTED_ONLY else missing).append(
                        "%s: %s %s" % (os.path.relpath(proj, dest), kind, item))
                elif kind == "ProjectReference":
                    projects.append(path)
    print("  %d projects name %d files, imports and references" % (len(seen), items))
    return missing, listed, outside


def main(argv):
    args, tree, name, i = [], ROOT, None, 0
    while i < len(argv):
        if argv[i] == "--tree" and i + 1 < len(argv):
            tree, i = os.path.abspath(argv[i + 1]), i + 2
            continue
        if argv[i] == "--name" and i + 1 < len(argv):
            name, i = argv[i + 1], i + 2
            continue
        if not argv[i].startswith("--"):
            args.append(argv[i])
        i += 1
    if len(args) != 1 or not os.path.isdir(tree):
        print(__doc__)
        return 2
    if name is not None and (not name or name != os.path.basename(name) or name in (".", "..")):
        print("washout: --name takes a folder name, not a path"); return 2
    name = name or os.path.basename(tree.rstrip("\\/"))
    dest = os.path.join(os.path.abspath(args[0]), name)
    archive = dest + ".zip"

    inside = lambda a, b: os.path.normcase(a + os.sep).startswith(os.path.normcase(b + os.sep))
    if inside(dest, tree) or inside(tree, dest):
        print("washout: the copy cannot go inside %s, nor hold it" % tree); return 2
    if os.path.exists(dest) and os.listdir(dest):
        if "--force" not in argv:
            print("washout: %s is not empty - give --force to replace it" % dest); return 2
        shutil.rmtree(dest)
    if "--zip" in argv and os.path.exists(archive):
        if "--force" not in argv:
            print("washout: %s is already there - give --force to replace it" % archive); return 2
        os.remove(archive)

    if os.path.exists(os.path.join(tree, ".git")):
        entries = tracked(tree)
        print("washout: %s - every file git tracks" % tree)
    else:
        entries = on_disk(tree)
        print("washout: %s is not a git working copy - its files from the disk, less what building made" % tree)
    copied, restored, submodules = wash(tree, dest, entries)
    for top in sorted(copied):
        print("  %-36s %5d files" % (top, copied[top]))
    for r in restored:
        print("    restored from git %s" % r)
    for s in submodules:
        print("    submodule not copied: %s" % s)
    print("washout: %d files in %s" % (sum(copied.values()), dest))

    missing, listed, outside = check(dest)
    for o in outside:
        print("    outside any copy: %s" % o)
    for l in listed:
        print("    listed only, not in the tree either: %s" % l)
    for m in missing:
        print("    MISSING %s" % m)
    if missing:
        print("washout: the copy does not hold everything its projects name - it will not build as the tree does")
        return 1
    print("washout: every project and every item they name is in the copy")
    if "--zip" in argv:
        print("washout: %s, %.1f MB" % (pack(dest), os.path.getsize(archive) / 1048576.0))
    return 0


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


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except (OSError, subprocess.CalledProcessError) as e:
        print("washout: %s" % e); sys.exit(1)
