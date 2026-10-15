{
  "gride": "1.0",
  "type": "project",
  "name": "NavFilter",
  "language": "C90",
  "template": "DSP Application (C6747)",
  "targets": ["Windows", "TMS320C6747"],
  "folders": {
    "src": ["src/main.c", "src/kalman.c"],
    "inc": ["inc/kalman.h"],
    "lib": [],
    "linker": ["c6747.cmd"]
  },
  "configurations": {
    "Debug|Windows": {
      "outDir": "out/Debug/Win",
      "objDir": "obj/Debug/Win",
      "defines": ["DEBUG"],
      "includeDirs": ["inc"],
      "libDirs": [],
      "libs": [],
      "optimise": "O0"
    },
    "Release|Windows": {
      "outDir": "out/Release/Win",
      "objDir": "obj/Release/Win",
      "defines": ["NDEBUG"],
      "includeDirs": ["inc"],
      "libDirs": [],
      "libs": [],
      "optimise": "O2"
    },
    "Debug|TMS320C6747": {
      "outDir": "out/Debug/C6747",
      "objDir": "obj/Debug/C6747",
      "defines": ["DEBUG"],
      "includeDirs": ["inc"],
      "libDirs": [],
      "libs": ["rts6747.lib"],
      "optimise": "O0",
      "linkerCmd": "c6747.cmd"
    },
    "Release|TMS320C6747": {
      "outDir": "out/Release/C6747",
      "objDir": "obj/Release/C6747",
      "defines": ["NDEBUG"],
      "includeDirs": ["inc"],
      "libDirs": [],
      "libs": ["rts6747.lib"],
      "optimise": "O2",
      "linkerCmd": "c6747.cmd"
    }
  },
  "excludeFromBuild": [],
  "dependsOn": []
}
