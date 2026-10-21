# installer/macos - the macOS package

`build-pkg.sh` makes `dist/GRIDE-<ver>-macos.pkg`: RIDE's macOS editor (from `macos/`) and the shared
toolchain (from `toolchain/`). GRIDEd and GRIDE 2.0 are .NET Framework WinForms programs and run on Windows
only; they are not in this package.

## What it needs

- A Mac with macOS 12 or later (the app's and the tools' deployment target).
- Xcode 14 or later (the projects are `objectVersion = 56`) and its command-line tools
  (`xcode-select --install`): `xcodebuild`, `make`, `clang++`, `pkgbuild`, `productbuild`, `codesign`.
- Signing is optional. By default the app and tools are signed ad hoc and the package is unsigned, so another
  Mac asks the user to open it from Finder's context menu the first time. `--sign-app "Developer ID
  Application: ..."` and `--sign-pkg "Developer ID Installer: ..."` sign properly; notarization is not done.
- For the toolchain: the tool trees' macOS build files, which `toolchain/` does not carry today (below).

## How to run it

```
installer/macos/build-pkg.sh [version]                    # default 1.1.0; builds everything
installer/macos/build-pkg.sh 1.1.0 --tools-bin DIR        # tools already built for macOS in DIR (c90.exe ..., lib/)
installer/macos/build-pkg.sh 1.1.0 --app-only             # the editor alone, no toolchain
```

`--out DIR` puts the package elsewhere; `-j N` sets make's jobs. The build works in a temporary copy laid out
as RIDE's workspace expects (`RIDE/` = `macos/`, beside `Compilers/ TI/ Windows/` = `toolchain/`), so nothing is
written into the working copy except `dist/`.

## What it does

1. Copies `macos/` and `toolchain/{Compilers,TI,Windows}` (without build output) into the work tree.
2. Builds `RIDE.app` with `xcodebuild -project macos/Window.xcodeproj -scheme RIDE -configuration Release`.
   The console editor (`Editor.xcodeproj`, `RIDE.exe`) is built too when the ten tool `.xcodeproj` files it
   depends on are present; today they are not, so it is left out and the script says so.
3. Builds the toolchain for macOS 12 with each tool tree's own Makefile, in the order and with the variables of
   RIDE's `workspace.mk` (c90, cpp11, shalimar + its C6747 runtime, c2s, vm6747, asm6x, masm, link, lnk6x,
   sim6747, rts6x and the shmrt6x libraries), then checks that all ten programs and six libraries are there.
4. Stages RIDE's macOS layout: the app with the tools in `Contents/MacOS` and their headers and runtime in
   `Contents/Resources`; `/usr/local/gride-<ver>/` with `bin` (`bin/lib`), `include`, `lib`, `help`,
   `examples` (GRIDEx3's one `examples` folder: `projects`, cppparse among them, and `programs`) and `docs`; and command links
   in `/usr/local/bin` (all but `link`, which macOS has).
5. Compiles and runs a C, a C++ and a Shalimar probe with the staged compilers, both copies.
6. Makes `editor.pkg` and `toolchain.pkg` with `pkgbuild` and one product with `productbuild` and a
   `distribution.xml` offering two choices, **Editor** and **Toolchain**.

## Missing for a real Mac build

`toolchain/MANIFEST.md` left the tool trees' Makefiles and Xcode projects out. The script stops and lists
them when they are absent:

- Makefiles: `Compilers/{Compiler-Ci,C++Optimize,Compiler-Si,Converter-C2S}/Makefile`,
  `TI/{VM6747,ASM6x,LNK6x,SIM6747,RTS6x}/Makefile`, `Windows/{MASM,LINK}/Makefile`, and what they read:
  `TI/RTS6x/tools/provenance`, `Compilers/Converter-C2S/src/Makevars`.
- For the console editor: `Compilers/Compiler-Ci/ide/cc1.xcodeproj`, `Compilers/C++Optimize/ide/cxx1.xcodeproj`,
  `Compilers/Compiler-Si/ide/shc.xcodeproj`, `Compilers/Converter-C2S/c2s.xcodeproj`, `TI/VM6747/vm6747.xcodeproj`,
  `TI/ASM6x/asm6x.xcodeproj`, `TI/LNK6x/lnk6x.xcodeproj`, `TI/SIM6747/sim6747.xcodeproj`,
  `Windows/MASM/masm.xcodeproj`, `Windows/LINK/link.xcodeproj`.
- RIDE's `docs/ccs-reference` (TI's option definitions, which RIDE's app reads from `Resources/lib/ccs`) is
  not in GRIDEx2, so CCS projects get no option definitions from this package.

The source of those files is the tool repositories (`My Documents\GRIDEdworking\...` has them).

## Not tested

All of it: the script was written on Windows, where nothing macOS runs. It was checked with `bash -n` only.
Untested in particular: the xcodebuild of the copied `Window.xcodeproj`; the make recipes against the tool
Makefiles; the staged layout; `plutil` on the component plist; the `distribution.xml` and its two choices;
signing with real identities. The app keeps RIDE's bundle identifier (`com.ghulamrs.ride47`, from
`macos/macos/Info.plist`) under the name `GRIDE Editor <ver>.app` (`APP_NAME=` changes it).
`macos/packaging/macos/Installer.xcodeproj` (in RIDE's workspace) runs RIDE's own `build-pkg.sh`, which needs
RIDE's `workspace.mk` and `docs/`; use this script instead.
