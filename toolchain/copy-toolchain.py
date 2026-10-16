"""Copies the toolchain's 11 projects from GRIDEdworking into <IDE root>\\toolchain, minimal copies only
(MANIFEST.md beside this file says what and why), then checks that every item a project file names is in the copy.

    python -I copy-toolchain.py "<IDE root>" [--from "D:\\GRA\\My Documents\\GRIDEdworking"]
"""
import os, re, shutil, sys

SRC = r"D:\GRA\My Documents\GRIDEdworking"
MANIFEST = {
    r"Compilers\Compiler-Ci":   [r"ide\cc1.vcxproj", r"ide\cc1.vcxproj.filters", "src", "msvc", "lib"],
    r"Compilers\C++Optimize":   [r"ide\cxx1.vcxproj", r"ide\cxx1.vcxproj.filters", "src", "msvc", "include", "lib"],
    r"Compilers\Compiler-Si":   [r"ide\shc.vcxproj", r"ide\shc.vcxproj.filters", "src", "runtime"],
    r"Compilers\Converter-C2S": ["c2s.vcxproj", "src"],
    r"TI\ASM6x":                ["asm6x.vcxproj", "src"],
    r"TI\LNK6x":                ["lnk6x.vcxproj", "src"],
    r"TI\RTS6x":                ["rts6x.vcxproj", r"tools\ar6x\ar6x.vcxproj", "src", r"tools\ar6x\ar6x.cpp", "printf6x.members"],
    r"TI\SIM6747":              ["sim6747.vcxproj", "src"],
    r"TI\VM6747":               ["vm6747.vcxproj", "src"],
    r"Windows\MASM":            ["masm.vcxproj", "src"],
    r"Windows\LINK":            ["link.vcxproj", "src"],
}
SKIP_DIRS = {"build", "x64", "obj", ".vs", "Debug", "Release"}
# the tree's own files: changed or written for the IDE's solution, never taken from the tool trees again
OURS = {
    os.path.join("TI", "RTS6x", "rts6x.vcxproj"),
    os.path.join("TI", "RTS6x", "tools", "ar6x", "ar6x.vcxproj"),
    os.path.join("Compilers", "Compiler-Si", "ide", "shc.vcxproj"),
    os.path.join("Windows", "LINK", "src", "image.cpp"),          # lc and tls start at 0 (Debug's C4701 under /WX)
    # cpp11's reference-to-array parameters on the C6747 (T (&)[N]; 2026-10-16, as GRIDEd's copy)
    os.path.join("Compilers", "C++Optimize", "src", "Type.h"),
    os.path.join("Compilers", "C++Optimize", "src", "Type.cpp"),
    os.path.join("Compilers", "C++Optimize", "src", "Mangle.cpp"),
    os.path.join("Compilers", "C++Optimize", "src", "parser", "ParserType.cpp"),
    os.path.join("Compilers", "C++Optimize", "src", "parser", "ParserTemplate.cpp"),
}


def copy_tree(a, b, dest):
    n = 0
    for d, dirs, files in os.walk(a):
        dirs[:] = [x for x in dirs if x not in SKIP_DIRS and not x.startswith(".")]
        for f in files:
            if f.startswith(".git"):
                continue
            s = os.path.join(d, f)
            t = os.path.join(b, os.path.relpath(s, a))
            if os.path.relpath(t, dest) in OURS and os.path.exists(t):
                print(f"  kept the tree's own {os.path.relpath(t, dest)}")
                n += 1
                continue
            os.makedirs(os.path.dirname(t), exist_ok=True)
            shutil.copy2(s, t)
            n += 1
    return n


def main():
    root = sys.argv[1]
    src = sys.argv[sys.argv.index("--from") + 1] if "--from" in sys.argv else SRC
    dest = os.path.join(root, "toolchain")
    total, missing = 0, []
    for tree, parts in MANIFEST.items():
        n = 0
        for p in parts:
            s, t = os.path.join(src, tree, p), os.path.join(dest, tree, p)
            if os.path.join(tree, p) in OURS:
                if os.path.exists(t):
                    print(f"  kept the tree's own {os.path.join(tree, p)}")
                    n += 1
                else:
                    missing.append(os.path.join(tree, p) + "   (the tree's own: take it from the IDE tree, not the tool tree)")
                continue
            if os.path.isdir(s):
                n += copy_tree(s, t, dest)
            elif os.path.isfile(s):
                os.makedirs(os.path.dirname(t), exist_ok=True)
                shutil.copy2(s, t)
                n += 1
            else:
                missing.append(os.path.join(tree, p))
        total += n
        print(f"{tree:28} {n:4} files")
        # every item a project file names must be in the copy
        for proj in [p for p in parts if p.endswith(".vcxproj")]:
            path = os.path.join(dest, tree, proj)
            if not os.path.exists(path):
                continue
            text = open(path, encoding="utf-8-sig").read()
            for item in re.findall(r'<(?:ClCompile|ClInclude|ResourceCompile|None|Text|Image|CustomBuild) Include="([^"]+)"', text):
                if "$(" in item or "*" in item:
                    continue
                f = os.path.normpath(os.path.join(dest, tree, os.path.dirname(proj), item))
                if not os.path.exists(f):
                    missing.append(os.path.relpath(f, dest))
    print(f"{total} files copied into {dest}")
    if missing:
        print("MISSING:", *missing, sep="\n  ")
        sys.exit(1)
    print("every item the project files name is in the copy")


main()
