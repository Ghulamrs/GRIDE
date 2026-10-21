{
  "name": "average-c90",
  "groups": {
    "Sources": ["src/main.c", "src/rng.c", "src/stats.c"],
    "Headers": ["src/rng.h", "src/stats.h"]
  },
  "build": {
    "target": "average-c90",
    "groups": ["Sources"]
  },
  "toolchain": "auto"
}
