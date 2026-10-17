{
  "name": "thirdparty-mathx",
  "toolchain": "auto",
  "arch": "arm64-darwin",
  "groups": {
    "Sources": [
      "main.cpp"
    ],
    "Library": []
  },
  "include": [
    "third_party/mathx/include"
  ],
  "libraries": [
    "third_party/mathx/libs/arm64-darwin/libmathx.a"
  ],
  "open": "main.cpp",
  "build": {
    "target": "mathx-demo",
    "groups": [
      "Sources"
    ]
  }
}
