{
  "name": "cppparse",
  "toolchain": "cpp11",
  "arch": "x86_64-windows",
  "include": [
    "cppparse/driver",
    "cppparse/common",
    "cppparse/frontend",
    "cppparse/middle",
    "cppparse/backend",
    "cppparse/analyzer",
    "cppparse/analyzer/report"
  ],
  "groups": {
    "driver": ["cppparse/driver/main.cpp"],
    "common": [
      "cppparse/common/Literal.cpp",
      "cppparse/common/Parallel.cpp",
      "cppparse/common/PathUtil.cpp",
      "cppparse/common/Target.cpp"
    ],
    "frontend": [
      "cppparse/frontend/CxxAst.cpp",
      "cppparse/frontend/CxxParser.cpp",
      "cppparse/frontend/Pp.cpp",
      "cppparse/frontend/Sema.cpp",
      "cppparse/frontend/SemaExpr.cpp"
    ],
    "middle": [
      "cppparse/middle/Ir.cpp",
      "cppparse/middle/IrGen.cpp",
      "cppparse/middle/Optimizer.cpp",
      "cppparse/middle/Ssa.cpp"
    ],
    "backend": [
      "cppparse/backend/BackEnd.cpp",
      "cppparse/backend/Coff.cpp",
      "cppparse/backend/Lower.cpp",
      "cppparse/backend/Mangle.cpp",
      "cppparse/backend/MangleSema.cpp",
      "cppparse/backend/MapFile.cpp",
      "cppparse/backend/X86.cpp"
    ],
    "analyzer": [
      "cppparse/analyzer/Ast.cpp",
      "cppparse/analyzer/BodyParser.cpp",
      "cppparse/analyzer/CodeBase.cpp",
      "cppparse/analyzer/Compiler.cpp",
      "cppparse/analyzer/Instantiate.cpp",
      "cppparse/analyzer/IrBuilder.cpp",
      "cppparse/analyzer/Lexer.cpp",
      "cppparse/analyzer/Model.cpp",
      "cppparse/analyzer/Parser.cpp",
      "cppparse/analyzer/ProjectReader.cpp",
      "cppparse/analyzer/TypeCheck.cpp",
      "cppparse/analyzer/Types.cpp"
    ],
    "report": [
      "cppparse/analyzer/report/Diagram.cpp",
      "cppparse/analyzer/report/Health.cpp",
      "cppparse/analyzer/report/Html.cpp",
      "cppparse/analyzer/report/Pdf.cpp",
      "cppparse/analyzer/report/Report.cpp",
      "cppparse/analyzer/report/Symbols.cpp"
    ],
    "headers": [
      "cppparse/analyzer/Ast.hpp",
      "cppparse/analyzer/BodyParser.hpp",
      "cppparse/analyzer/CodeBase.hpp",
      "cppparse/analyzer/Compiler.hpp",
      "cppparse/analyzer/Instantiate.hpp",
      "cppparse/analyzer/IrBuilder.hpp",
      "cppparse/analyzer/Lexer.hpp",
      "cppparse/analyzer/Model.hpp",
      "cppparse/analyzer/Parser.hpp",
      "cppparse/analyzer/ProjectReader.hpp",
      "cppparse/analyzer/TypeCheck.hpp",
      "cppparse/analyzer/Types.hpp",
      "cppparse/analyzer/report/Diagram.hpp",
      "cppparse/analyzer/report/Health.hpp",
      "cppparse/analyzer/report/Html.hpp",
      "cppparse/analyzer/report/Pdf.hpp",
      "cppparse/analyzer/report/Report.hpp",
      "cppparse/analyzer/report/Symbols.hpp",
      "cppparse/backend/BackEnd.hpp",
      "cppparse/backend/Coff.hpp",
      "cppparse/backend/Lower.hpp",
      "cppparse/backend/Mangle.hpp",
      "cppparse/backend/MapFile.hpp",
      "cppparse/backend/X86.hpp",
      "cppparse/common/Literal.hpp",
      "cppparse/common/Parallel.hpp",
      "cppparse/common/PathUtil.hpp",
      "cppparse/common/Target.hpp",
      "cppparse/frontend/CxxAst.hpp",
      "cppparse/frontend/CxxParser.hpp",
      "cppparse/frontend/Pp.hpp",
      "cppparse/frontend/Sema.hpp",
      "cppparse/middle/Ir.hpp",
      "cppparse/middle/IrGen.hpp",
      "cppparse/middle/Optimizer.hpp",
      "cppparse/middle/Ssa.hpp"
    ]
  },
  "build": {
    "target": "cppparse",
    "groups": ["driver", "common", "frontend", "middle", "backend", "analyzer", "report"],
    "args": ["--health", "."]
  }
}
