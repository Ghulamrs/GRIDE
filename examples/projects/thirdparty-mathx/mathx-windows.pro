{
  "name": "thirdparty-mathx",
  "arch": "x86_64-windows",
  "include": ["third_party/mathx/include"],
  "libraries": ["third_party/mathx/libs/x86_64-windows/mathx.lib"],
  "groups": {
    "Sources": ["main.cpp"],
    "Library": ["third_party/mathx/include/mathx.h", "third_party/mathx/source/BUILD.txt"]
  },
  "build": {
    "target": "mathx-demo",
    "groups": ["Sources"]
  },
  "toolchain": "auto"
}
