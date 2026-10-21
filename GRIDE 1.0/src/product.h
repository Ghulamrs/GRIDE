#pragma once

// GRIDE 1.0 is GRIDE's window renamed - GRIDEx3's White outlook, "the simple", on the toolchain the three outlooks
// share; the console editor is not part of it.
//
// **The product's name, once.** Every name the editor shows or leaves on a disk is made from
// these; the programs' own file names come from the build - PRODUCT in the Makefiles, GrideProduct
// in product.props, $Product in packaging/windows/make-setup.ps1 - spelled the same. A rename is these lines, those three, and a search of the prose.

namespace editor {
namespace product {

// As people read it: "GRIDE 1.0", the Start menu, a message box's title.
constexpr const char* kName = "GRIDE";
// As a file name wants it: gride-run.s, gride-parts, .gride.
constexpr const char* kLower = "gride";
// The per-user state - the recent projects and files, the frame, Debug or
// Release. Not configuration: that is settings.json (.gride, or GRIDE 1.0 on Windows), and a
// project's own is its .pro. Under the home directory.
constexpr const char* kStateDirectory = ".gride";
constexpr const char* kStateFile = "state.json";

// The three compilers, as a user reads and types them: the Toolchain menu, --toolchain, a .pro's
// "toolchain", settings.json's "compiler" and the line a build prints. Their programs are these
// names with .exe; each compiler's own src/Name.h and its Makefile's PROGRAM spell it the same.
constexpr const char* kCompilerC = "c90";
constexpr const char* kCompilerCpp = "cpp11";
constexpr const char* kCompilerShalimar = "shalimar";

}
}
