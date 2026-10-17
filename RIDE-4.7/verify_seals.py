#!/usr/bin/env python3
"""Verify the integrity of RIDE and every project it drives, from MASTER.SEAL down.

    python3 verify_seals.py          check everything, print a report, exit 0 if intact
    python3 verify_seals.py -v       also list every file checked

Written apart from tools/seal and tools/master-seal on purpose: it recomputes
every CRC32 itself, so a fault in those tools cannot vouch for itself. It checks
  1. each project seal file's CRC32 and seal value against MASTER.SEAL,
  2. every file each project seal lists - present, same size, same CRC32,
  3. each project's own seal value, recomputed from its rows,
  4. the master seal, recomputed from the project seals,
  5. files git tracks in a sealed directory that the seal does not list.
"""
import binascii
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
VERBOSE = "-v" in sys.argv[1:]


def crc32(data):
    return binascii.crc32(data) & 0xFFFFFFFF


def read_master():
    rows, master = [], None
    with open(os.path.join(ROOT, "MASTER.SEAL"), encoding="utf-8") as fh:
        for line in fh:
            parts = line.split()
            if len(parts) == 5 and parts[4].endswith(".dat"):
                rows.append((parts[0], parts[1], parts[2], int(parts[3]), parts[4]))
            elif line.startswith("Master seal "):
                master = parts[2]
    return rows, master


def read_seal(path):
    """The seal value from the header and the (crc, size, file) rows."""
    value, rows = None, []
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            if " files, seal " in line:
                value = line.split()[-1]
                continue
            parts = line.split()
            if len(parts) == 3 and len(parts[0]) == 8:
                try:
                    rows.append((int(parts[0], 16), int(parts[1]), parts[2]))
                except ValueError:
                    pass
    return value, rows


# Source code, as tools/seal defines it (2026-10-05): written here again on purpose, not imported.
SOURCE_EXT = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".inc", ".m", ".mm", ".s", ".asm"}


def source(rel):
    ext = os.path.splitext(rel)[1].lower()
    return ext in SOURCE_EXT or (ext == "" and "/include/" in "/" + rel)


def unlisted(project, listed):
    """Source files git tracks under the sealed directories that the seal leaves out."""
    conf = os.path.join(project, "tools", "seal.json")
    dirs = json.load(open(conf))["dirs"] if os.path.exists(conf) else ["src", "include", "lib"]   # source code only, never examples
    try:
        out = subprocess.run(["git", "ls-files", "-z", "--"] + dirs, cwd=project,
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return []
    names = [n for n in out.decode().split("\0") if n and os.path.isfile(os.path.join(project, n))]
    return sorted(n for n in names if n not in listed and source(n))


def main():
    problems = 0
    rows, master = read_master()
    if not rows or master is None:
        print("MASTER.SEAL: unreadable or empty"); return 2
    print("MASTER.SEAL: %d projects, master seal %s" % (len(rows), master))

    for prog, seal, file_crc, count, rel in rows:
        path = os.path.normpath(os.path.join(ROOT, rel))
        project = os.path.dirname(path)
        bad = []
        if not os.path.exists(path):
            print("  %-9s MISSING seal file %s" % (prog, rel)); problems += 1; continue
        if "%08X" % crc32(open(path, "rb").read()) != file_crc:
            bad.append("the seal file itself was edited since MASTER.SEAL was written")
        value, files = read_seal(path)
        if value != seal:
            bad.append("seal value %s, MASTER.SEAL says %s" % (value, seal))
        if len(files) != count:
            bad.append("%d files listed, MASTER.SEAL says %d" % (len(files), count))
        recomputed = crc32("".join("%s %08X" % (f, c) for c, _, f in files).encode())
        if "%08X" % recomputed != value:
            bad.append("seal value does not match its own rows (%08X)" % recomputed)
        for c, n, f in files:
            p = os.path.join(project, f)
            if not os.path.isfile(p):
                bad.append("gone     %s" % f); continue
            data = open(p, "rb").read()
            if len(data) != n or crc32(data) != c:
                bad.append("changed  %s" % f)
            elif VERBOSE:
                print("      ok %s" % f)
        for f in unlisted(project, {f for _, _, f in files}):
            bad.append("added    %s (tracked, not in the seal)" % f)
        state = "intact" if not bad else "%d problem(s)" % len(bad)
        print("  %-9s %s  %4d files  %s" % (prog, seal, len(files), state))
        for b in bad:
            print("            %s" % b)
        problems += len(bad)

    recomputed = crc32("".join("%s %s" % (p, s) for p, s, _, _, _ in rows).encode())
    if "%08X" % recomputed != master:
        print("  master seal recomputes to %08X, MASTER.SEAL says %s" % (recomputed, master))
        problems += 1
    print("INTEGRITY %s: %d project(s), %d problem(s)"
          % ("OK" if not problems else "FAILED", len(rows), problems))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
