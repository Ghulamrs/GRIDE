{
  "name": "cpp-sampleext",
  "groups": {
    "Sources": ["Math.cpp"],
    "Headers": ["Matrix.h", "Quat.h", "Vector.h"]
  },
  "build": {
    "target": "cpp-sampleext",
    "groups": ["Sources"]
  },
  "toolchain": "auto",
  "configurations": {
    "Debug|Windows": {
      "outDir": "out/Debug/Win",
      "objDir": "obj/Debug/Win",
      "defines": ["DEBUG"],
      "includeDirs": ["inc"],
      "libDirs": [],
      "libs": [],
      "optimise": "O0"
    }
  }
}
