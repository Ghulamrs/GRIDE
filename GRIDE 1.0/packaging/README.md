# Packaging

RIDE and the programs it drives, as one installable thing on each platform.
They are packaged together because they are used together: the editor drives
c90, cpp11 and shalimar, and for the C6747 target asm6x, lnk6x, vm6747 and
sim6747 with the RTS6x libraries, and for Windows its own masm and link - and
is not much good without them. `MASTER.SEAL` lists the twelve, and
`docs/ride-architecture.html` draws them.

| | | built and installed |
| --- | --- | --- |
| macOS | [`macos/build-pkg.sh`](macos/build-pkg.sh) | `RIDE-<ver>-macos.pkg`, into `/Applications/RIDE <ver>.app` |
| Windows | [`windows/build-installer.bat`](windows/build-installer.bat) | `RIDE-<ver>.msi`, into `C:\Program Files\RIDE <ver>` |
| Linux | [`linux/build-run.sh`](linux/build-run.sh) | `RIDE-<ver>-linux-x86_64.run`, into `/opt/ride-<ver>` |

The programs keep their own names - `RIDE.exe`, `c90.exe`, `cpp11.exe`,
`shalimar.exe` and the rest - and each install is versioned, so several
releases sit side by side.

## The release scripts, and what they refuse

`packaging/release.sh` (Mac, Linux) and `packaging/windows/release.cmd` build a release from fresh
checkouts of every repository's default branch. Before a line is compiled they refuse:

- **sources that do not match their seals** - `verify_seals.py`, from `MASTER.SEAL` down.

After the build, `confirm` stops a release whose `bin/` is MISSING anything RIDE drives - the six
libraries in `bin/lib/rts6x-tms6747` among it (`rts6x.lib`, `rts6xd.lib`, `printf6x.lib`, `printf6xd.lib`,
`shmrt6x.lib`, `shmrt6xd.lib`) - and when the installer is made, `tools/master-seal artefacts` writes a
second table into `MASTER.SEAL`: the SHA-256 and size of every executable and library packaged. That
`MASTER.SEAL` is left beside `RELEASE.txt`. **The seal is of sources, and a development `bin/` is sealed by
nothing**: only the artefact table of a release says which binaries were built from the sealed sources.

On Windows `RELEASE_DRYRUN=1` stops after the checks (with `RELEASE_BIN` naming a built `bin\`, after
`build.bat confirm` over it too), and `RELEASE_PIN` stands in for VM6747's pin there, to rehearse a refusal.

## What packaging had to fix first

Neither program could be installed anywhere before this, and both reasons are
about how they find their own files.

**shc took the directory out of `argv[0]`.** argv[0] is not a path: started
through PATH — which is how an installed compiler is always started — a program
is handed a bare name, whose directory is `.`, so the runtime was looked for in
`./lib/shmrt-*.a`. It asks the machine now, and resolves the answer, because
Homebrew exposes binaries as symbolic links into a cellar.

**shc looked in one place.** Beside itself in `lib/`, which is the source tree.
It also tries `../lib` now — `<prefix>/bin` holding the compiler and
`<prefix>/lib` the runtime — because a package that had to put its runtime in
`<prefix>/bin/lib` to be found would be a package nobody could read.

**cc1 needed no code change**: its include directory is compiled in through
`CC1_INCLUDE_DIR`, and `INCDIR` is a plain `=` in its Makefile, so both
packages set it at build time to where the headers will actually live.

## The installer checks the compilers, not the files

A package that installs a compiler which cannot compile is the failure worth
catching, and it is not caught by checking that a file exists. The Linux
installer compiles and runs a program with the cpp11 it has just put down, and
says so; the release scripts' `confirm` and the seals cover the rest.
