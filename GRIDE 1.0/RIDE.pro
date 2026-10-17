{
  "name": "RIDE",
  "toolchain": "auto",
  "arch": "x86_64-windows",
  "indent": 4,
  "tabs": false,
  "groups": {
    "Editor": [
      "src/about.cpp",
      "src/buffer.cpp",
      "src/editor.cpp",
      "src/find.cpp",
      "src/help.cpp",
      "src/main.cpp",
      "src/menu.cpp",
      "src/tree.cpp"
    ],
    "Rules": [
      "src/indent.cpp",
      "src/json.cpp",
      "src/path.cpp",
      "src/process.cpp",
      "src/project.cpp",
      "src/settings.cpp",
      "src/options.cpp",
      "src/symbols.cpp",
      "src/syntax.cpp",
      "src/utf8.cpp",
      "src/workspace.cpp"
    ],
    "Compilers": [
      "src/compile.cpp",
      "src/convert.cpp",
      "src/debugger.cpp",
      "src/toolchain.cpp"
    ],
    "Terminal": [
      "src/demangle_win.cpp",
      "src/terminal.cpp",
      "src/terminal_common.cpp",
      "src/terminal_win.cpp"
    ],
    "CCS projects": [
      "src/ccs/ccsproject.cpp",
      "src/ccs/ccsoptions.cpp",
      "src/ccs/ccsxml.cpp",
      "src/ccs/ccsworkspace.cpp",
      "src/ccs/ccsproject.h",
      "src/ccs/ccsoptions.h",
      "src/ccs/ccsxml.h",
      "src/ccs/ccsworkspace.h"
    ],
    "Shalimar debugging": [
      "src/shalimar/channel.cpp",
      "src/shalimar/session.cpp",
      "src/shalimar/channel.h",
      "src/shalimar/session.h"
    ],
    "Headers": [
      "src/about.h",
      "src/buffer.h",
      "src/compile.h",
      "src/convert.h",
      "src/debugger.h",
      "src/editor.h",
      "src/find.h",
      "src/help.h",
      "src/indent.h",
      "src/json.h",
      "src/menu.h",
      "src/path.h",
      "src/process.h",
      "src/product.h",
      "src/project.h",
      "src/settings.h",
      "src/options.h",
      "src/symbols.h",
      "src/syntax.h",
      "src/terminal.h",
      "src/toolchain.h",
      "src/tree.h",
      "src/utf8.h",
      "src/workspace.h"
    ],
    "Tests": [
      "tests/session.cpp",
      "tests/test.cpp"
    ],
    "Samples": [
      "projects/c-example/counter.c",
      "projects/c-example/counter.h",
      "programs/gcd.shl",
      "projects/c-example/hello.c",
      "programs/primes.shl",
      "programs/projectile.c",
      "programs/rotmat.shl",
      "programs/smart.cpp",
      "projects/cpp-table/table.cpp",
      "projects/cpp-table/table.h",
      "projects/cpp-vector3/vector3.cpp",
      "projects/cpp-vector3/vector3.h"
    ],
    "Build": [
      "Makefile",
      "README.md",
      "RIDE.sln",
      "build.bat",
      "workspace.mk",
      "tools/check-help.sh",
      "tools/make-projects.py",
      "tools/to-linux.sh",
      "tools/to-windows.sh",
      "product.props"
    ],
    "Windows Forms": [
      "winforms/MainForm.h",
      "winforms/Program.cpp",
      "winforms/RIDEGui.vcxproj",
      "winforms/bridge.cpp",
      "winforms/bridge.h",
      "winforms/show.ps1"
    ],
    "macOS window": [
      "macos/CodeView.h",
      "macos/CodeView.mm",
      "macos/Info.plist",
      "macos/LineNumbers.h",
      "macos/LineNumbers.mm",
      "macos/Makefile",
      "macos/README.md",
      "macos/Text.h",
      "macos/WindowController.h",
      "macos/WindowController.mm",
      "macos/main.mm",
      "macos/make-xcodeproj.py"
    ]
  }
}
