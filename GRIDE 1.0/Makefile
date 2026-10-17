# RIDE - an editor that drives the cc1 compiler and shc. RIDE is the
# terminal half and the window is RIDE.exe on Windows; this builds the
# terminal one. .exe on every machine, not only Windows: the three programs in
# this family carry one name each wherever they are.
#
# The binaries were called ed1 and ed1gui until 2026-08-22, on the grounds that
# the product name was what the pair was called and the binaries kept their own
# names. That is reversed: one name, everywhere.
#
# Everything except one file is ordinary C++14 and builds anywhere. The
# exception is the terminal, and even that is smaller than it looks: Windows 10
# and later understand the same escape sequences as a Unix terminal once
# ENABLE_VIRTUAL_TERMINAL_PROCESSING is set, so src/terminal_win.cpp differs
# from src/terminal.cpp only in how the console is put into raw mode. The
# drawing, the key decoding and the status bar are the same code on both.
#
# On Windows with MSVC, use build.bat instead - it calls cl directly, since
# that machine has no make.

UNAME_S := $(shell uname -s 2>/dev/null)

ifeq ($(origin CXX),default)
  ifeq ($(UNAME_S),Darwin)
    CXX := clang++
  else
    CXX := g++
  endif
endif

# C++14, not because nothing newer works here but because cc1 is C++14 and the
# arena it is developed in holds itself to what it compiles. That is why
# src/path.cpp exists: <filesystem> is C++17.
CXXFLAGS := -std=c++14 -Wall -Wextra -Werror -pedantic -O2

# MSYS and MinGW report themselves here, and want the Windows console.
ifneq (,$(findstring MINGW,$(UNAME_S)))
  TERM_SRC := src/terminal_win.cpp
else ifneq (,$(findstring MSYS,$(UNAME_S)))
  TERM_SRC := src/terminal_win.cpp
else
  TERM_SRC := src/terminal.cpp
endif

# Two front ends over one core, and this is where that split is written down.
# CORE_SRC is what both of them compile: every rule the editor has, and none of
# the drawing. The window compiles exactly this list plus its own two files, so
# tools/make-projects.py checks winforms/RIDEGui.vcxproj against it - that
# project is kept by hand, and a file added here and forgotten there is a link
# error on the one machine that builds the window and nowhere else.
CORE_SRC := src/buffer.cpp src/compile.cpp src/convert.cpp \
       src/indent.cpp src/syntax.cpp \
       src/toolchain.cpp src/json.cpp src/project.cpp src/find.cpp \
       src/utf8.cpp src/workspace.cpp src/symbols.cpp src/demangle_win.cpp \
       src/path.cpp src/process.cpp src/debugger.cpp src/settings.cpp src/options.cpp src/about.cpp \
       src/ccs/ccsproject.cpp src/ccs/ccsoptions.cpp src/ccs/ccsxml.cpp src/ccs/ccsworkspace.cpp

# The terminal's own half. src/help.cpp is here rather than in the core because
# only this front end shows the manual - the window's Help menu has Keys and
# About and no Contents.
#
# TERMINAL_SRC and TERM_SRC above are different things: that one is a single
# file, which of the two terminals this machine has.
TERMINAL_SRC := src/main.cpp src/editor.cpp src/menu.cpp src/tree.cpp \
       src/help.cpp \
       src/terminal_common.cpp \
       $(TERM_SRC)

SRC := $(CORE_SRC) $(TERMINAL_SRC)

# The Shalimar half lives apart from the three DWARF debuggers on purpose: a
# Shalimar program stops itself, so nothing here has anything to say to gdb,
# lldb or cdb, and src/debugger.cpp has nothing to say to it.
SHM_SRC := src/shalimar/channel.cpp src/shalimar/session.cpp

# The objects go under obj/ rather than beside the sources they came from,
# so that a listing of src/ is the code and nothing else.
# Objects are built in build/obj, which git ignores, so nothing is written outside GRIDE1. Nothing
# intermediate is ever written next
# to the sources, so `tar` on this repository carries source and nothing else,
# and a clean is a directory removal that cannot reach a tracked file.
#
# Overridable, and `?=` on purpose: workspace.mk names one place for all four,
# and a command line beats both.
OBJDIR ?= build/obj
OBJ := $(patsubst src/%.cpp,$(OBJDIR)/%.o,$(SRC) $(SHM_SRC))

# Where the finished program goes. `.` is this directory, which is what every
# suite and script here already expects, so a plain `make` is unchanged. The
# workspace build names one directory and has all three programs built into
# it - the editor and the two compilers it drives - so that what RIDE finds
# beside itself is what was just built, rather than what somebody remembered
# to copy.
BINDIR ?= .

# **The product's name, once** - the program built here is $(PRODUCT).exe, and
# src/product.h, product.props and packaging/windows/make-setup.ps1 spell it the same. A rename is those.
PRODUCT := RIDE
PRODUCT_LOWER := ride
EDITOR := $(BINDIR)/$(PRODUCT).exe

# **The compilers the checking drives, defaulted to the ones standing in BINDIR.**
#
# They were not defaulted to anything, so a plain `make check` ran 783 unit and
# 162 session checks and said "0 failed" - while the full suite is 912 and 297.
# 264 checks skipped, and the skipping is announced ("no shc named, so those
# cases are not tried") but the total is not, so the headline reads like a pass.
# That is the hazard in Compiler-C's verification notes (its own repository, not in GRIDE1) wearing local clothes:
# a green suite that tested less than the reader thinks.
#
# The absurd part was where the binaries were: BINDIR, the directory `confirm`
# had just finished checking them into. Naming them is now the default rather
# than something to remember on the command line.
#
# `$(wildcard)` and not a bare path, so a machine that genuinely has no cc1
# still skips those cases with its own message rather than failing to find a
# file - the behaviour before this, kept for the case it was right for. And
# `?=`, so CC1 in the environment or on the command line still wins.
# c90, cpp11 and shalimar since 3.5: the VM6747 line, which carries tms6747
# in the first two as well as the three host targets.
CC1 ?= $(abspath $(wildcard $(BINDIR)/c90.exe))
CXX1 ?= $(abspath $(wildcard $(BINDIR)/cpp11.exe))
SHC ?= $(abspath $(wildcard $(BINDIR)/shalimar.exe))
C2S ?= $(abspath $(wildcard $(BINDIR)/c2s.exe))

# Exported because the two suites read them differently: `session` is handed
# them on its command line, and `test` picks them out of the environment.
export CC1
export CXX1
export SHC
export C2S

# Which Shalimar runtime this machine's shc builds, spelled the same way
# Compiler-S/Makefile spells it. Named here because `confirm` below has to ask
# for the archive by name, and a glob would pass on a directory holding some
# other machine's.
ifeq ($(UNAME_S),Darwin)
  SHM_TARGET ?= arm64-darwin
else
  SHM_TARGET ?= x86_64-linux
endif

$(EDITOR): $(OBJ)
	@mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ)

# One rule for both, making whatever directory the object goes in. A second
# pattern rule for the subdirectory would be ambiguous with this one - the
# stem 'shalimar/channel' matches it too, and which of the two make prefers is
# not something to have to know.
$(OBJDIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

# Which headers each object depends on is the compiler's answer, not a list
# kept by hand here. The list that used to be here had gone stale: editor.cpp
# had come to include debugger.h and the line for editor.o did not say so, so a
# member added to Debugger rebuilt debugger.o and not editor.o. One binary then
# held two ideas of where that class's members were, and it segfaulted - after
# a run of tests that had looked like a parser bug. A clean build hid it, which
# is the worst thing a bug of this kind can do.
-include $(OBJ:.o=.d)

# The two pieces with a contract: the layout rules, and the reading of cc1's
# diagnostic - which has to cope with a Windows path whose drive letter is
# followed by a colon that is not a separator.
# **A compiler named but not there is a mistake, not an absence**, and the two
# have to be told apart because they look identical to a suite that skips.
#
# Empty is an absence: this machine has no cc1, those cases cannot run, and the
# suite says so case by case and carries on. That is right and is kept.
#
# A path that does not exist is somebody having typed one - a stale directory, a
# renamed binary, a build that did not happen. Skipping there turns a typo into
# a green run of a smaller suite, which is the same fault as the one that made
# these default to BINDIR: `make session CC1=/nowhere/cc1.exe` used to report
# "205 checks, 0 failed" and look exactly like a pass.
#
# All three are reported before exiting rather than the first, because a wrong
# path usually means the whole set is wrong and finding them one run at a time
# is three runs.
check-tools:
	@bad=0; \
	[ -z "$(CC1)" ] || [ -x "$(CC1)" ] || { echo "CC1 names '$(CC1)', which is not there." >&2; bad=1; }; \
	[ -z "$(CXX1)" ] || [ -x "$(CXX1)" ] || { echo "CXX1 names '$(CXX1)', which is not there." >&2; bad=1; }; \
	[ -z "$(SHC)" ] || [ -x "$(SHC)" ] || { echo "SHC names '$(SHC)', which is not there." >&2; bad=1; }; \
	[ -z "$(C2S)" ] || [ -x "$(C2S)" ] || { echo "C2S names '$(C2S)', which is not there." >&2; bad=1; }; \
	[ $$bad -eq 0 ] || { \
	    echo "  Name one that exists, or leave it unset - unset skips those cases on purpose." >&2; \
	    exit 1; }

# The compilers are handed to the suites absolutely: a build runs them from
# another directory, where `bin/c90.exe` names nothing. TEST_FLAGS=--require-tools fails a skip (D14).
test: tests/test check-tools
	CC1="$(abspath $(CC1))" CXX1="$(abspath $(CXX1))" SHC="$(abspath $(SHC))" C2S="$(abspath $(C2S))" ./tests/test $(TEST_FLAGS)

tests/test: tests/test.cpp src/compile.cpp src/indent.cpp src/syntax.cpp \
            src/toolchain.cpp src/json.cpp src/project.cpp src/find.cpp \
       src/utf8.cpp src/workspace.cpp src/symbols.cpp src/demangle_win.cpp \
            src/path.cpp src/process.cpp src/debugger.cpp src/settings.cpp src/options.cpp src/about.cpp src/help.cpp \
            src/buffer.cpp src/ccs/ccsproject.cpp src/ccs/ccsoptions.cpp src/ccs/ccsxml.cpp src/ccs/ccsworkspace.cpp \
            src/ccs/ccsproject.h src/ccs/ccsoptions.h src/ccs/ccsxml.h src/ccs/ccsworkspace.h \
            winforms/bridge.cpp winforms/bridge.h src/compile.h src/convert.h \
            src/indent.h src/syntax.h \
            src/json.h src/project.h src/path.h src/buffer.h
	$(CXX) $(CXXFLAGS) -pthread -Isrc -Iwinforms -o $@ tests/test.cpp winforms/bridge.cpp \
	    src/compile.cpp src/convert.cpp src/indent.cpp \
	    src/syntax.cpp src/toolchain.cpp src/json.cpp src/project.cpp src/find.cpp \
       src/utf8.cpp src/workspace.cpp src/symbols.cpp src/demangle_win.cpp \
	    src/path.cpp src/process.cpp src/debugger.cpp src/settings.cpp src/options.cpp src/about.cpp src/help.cpp \
	    src/buffer.cpp src/ccs/ccsproject.cpp src/ccs/ccsoptions.cpp src/ccs/ccsxml.cpp src/ccs/ccsworkspace.cpp $(SHM_SRC)

# The other half of the checking: the editor itself, driven by keystrokes.
# CC1, CXX1 and SHC name compilers for the build cases, and C2S the converter
# for the Language menu's Convert; without them those cases are skipped rather
# than failed.
session: tests/session $(EDITOR) check-tools
	CC1="$(abspath $(CC1))" CXX1="$(abspath $(CXX1))" SHC="$(abspath $(SHC))" C2S="$(abspath $(C2S))" ./tests/session $(EDITOR)

tests/session: tests/session.cpp src/path.cpp src/path.h
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/session.cpp src/path.cpp

check: test session

# ---- what RIDE drives, and the confirmation that it is there -------------
#
# The editor links against none of these. It *runs* them, and it finds them
# beside itself - path::besideProgram, asked before PATH, so that a compiler
# shipped with this copy is the one this copy runs. Which means "built" and
# "usable" are two different states, and until now nothing checked the second.
#
# That gap is not hypothetical: a bin/ was assembled holding shc.exe without
# the runtime archives shc links, and every build stayed green and every suite
# passed, because a suite that cannot find a compiler skips its cases and says
# so quietly. The failure waited for somebody to open a Shalimar file and press
# Run, and then read as a broken compiler rather than an incomplete directory.
#
# Both archives are named, not only the release one. Debug is the editor's
# default configuration and links the other file, so checking one of the two
# would confirm exactly the half that was not about to be used.
# c2s is here for the same reason the two compilers are: the editor runs it
# and finds it beside itself, so "built" and "usable" are two states and this
# checks the second. It is not a compiler and nothing links it - the Language
# menu's two Convert items run it over the open file.
# cxx1 joined in 3.0, found the same way and for the same reason. 3.5 docks
# the VM6747 line instead - c90.exe and cpp11.exe, the same compilers with
# the TMS320C6747 as a fourth target, shalimar.exe the same Shalimar compiler with
# its three - and vm6747.exe, the emulator that runs the fourth target's
# programs, found beside the editor like the compilers. 4.0 adds masm.exe,
# the x86-64 assembler that stands in for ml64 when settings.json names it,
# and the two linkers: link.exe for x86-64, in place of Microsoft's, and
# lnk6x.exe for the C6000, in place of TI's, each when settings.json names it.
DEPENDENCIES := c90.exe cpp11.exe vm6747.exe sim6747.exe asm6x.exe masm.exe link.exe lnk6x.exe shalimar.exe c2s.exe \
       lib/shmrt-$(SHM_TARGET).a lib/shmrt-$(SHM_TARGET)-debug.a \
       lib/shmrt-tms6747/Runtime.s lib/shmrt-tms6747-debug/Debug.s lib/rts6x-tms6747/rts6x.lib lib/rts6x-tms6747/rts6xd.lib \
       lib/rts6x-tms6747/printf6x.lib lib/rts6x-tms6747/printf6xd.lib \
       lib/rts6x-tms6747/shmrt6x.lib lib/rts6x-tms6747/shmrt6xd.lib

confirm: $(EDITOR)
	@missing=0; \
	for dep in $(DEPENDENCIES); do \
	    if [ -e "$(BINDIR)/$$dep" ]; then \
	        echo "  ok       $$dep"; \
	    else \
	        echo "  MISSING  $$dep"; \
	        missing=1; \
	    fi; \
	done; \
	if [ $$missing -ne 0 ]; then \
	    echo ""; \
	    echo "$(PRODUCT).exe is in $(BINDIR) without what it drives. Build the four"; \
	    echo "together with 'make -f workspace.mk', or name them with \$$CC1, \$$CXX1 and \$$SHC."; \
	    exit 1; \
	fi; \
	echo ""; \
	echo "$(PRODUCT).exe and everything it drives are in $(BINDIR)"

# The Xcode project is generated from the source list above rather than kept by
# hand, so it cannot fall behind it. Run this after adding or removing a file.
xcodeproj:
	python3 tools/make-projects.py

# What gets used, as against what gets built. The binaries land beside their
# objects because that is where a build puts them; this is where the product
# lives - one directory holding what you would actually run, away from the
# project space it was compiled in.
#
# PRODUCT_DIR names it, so a different one can be asked for without editing this.
PRODUCT_DIR ?= $(HOME)/$(PRODUCT_LOWER)
# Where cxx1's headers are copied from for the product. The binary comes from
# BINDIR like the others; the headers stay in the checkout,
# Compilers/C++Optimize on every machine.
CXX1_DIR ?= ../toolchain/Compilers/C++Optimize
CC1_DIR ?= ../toolchain/Compilers/Compiler-Ci

# `confirm` and not `$(EDITOR)`, for the reason build.bat gives on its own
# product rule: an editor without its compilers is not a product, it is half of
# one that fails at the first Ctrl-B. This rule shipped the editor alone until
# 2026-08-24 while the Windows one had already been fixed, so a Mac or Linux
# product could not build anything it was given. Depending on `confirm` means
# the same list that guards the build guards the product, and a missing
# compiler stops this rather than being discovered by the person reviewing it.
product: confirm
# Emptied first, for the reason workspace.mk's `bin` rule gives: a binary that
# was renamed leaves its old self here otherwise, and a directory holding both
# RIDE.exe and the name before it is one where nobody can say which was run.
# It cost a stray quad.shl sitting in bin/ from 2026-08-18 to notice this rule
# never had what that one does.
#
# The two directories this rule fills, and not $(PRODUCT_DIR) itself. PRODUCT_DIR is
# whatever the caller says, so `make product PRODUCT_DIR=$$HOME` would turn a
# wholesale rm -rf into deleting a home directory. Nothing here needs that risk
# to do its job.
	rm -rf "$(PRODUCT_DIR)/bin" "$(PRODUCT_DIR)/projects" "$(PRODUCT_DIR)/programs"
	mkdir -p "$(PRODUCT_DIR)/bin/lib"
	cp $(EDITOR) "$(PRODUCT_DIR)/bin/"
	cp $(BINDIR)/c90.exe $(BINDIR)/cpp11.exe $(BINDIR)/vm6747.exe $(BINDIR)/shalimar.exe $(BINDIR)/c2s.exe "$(PRODUCT_DIR)/bin/"
# The headers go with the compilers, one directory above bin/ - because
# bin/lib/ is shc's runtime. include/ is cpp11's: its C++ headers and the C
# ones they wrap, in one directory; lib/ is c90's. Each looks there for its
# own before the paths compiled into it, which name the checkout it was built
# from - a product that outlives that checkout would otherwise compile
# nothing that says #include. settings.json beside them tells the editor the
# same two directories, and is where a vcvars64.bat is named when it has to be.
	rm -rf "$(PRODUCT_DIR)/include" "$(PRODUCT_DIR)/lib"
	cp -R $(CXX1_DIR)/include "$(PRODUCT_DIR)/include"
	cp $(CXX1_DIR)/lib/*.h "$(PRODUCT_DIR)/include/"
	cp -R $(CC1_DIR)/lib "$(PRODUCT_DIR)/lib"
# TI's option definitions, which a CCS project's unstored options are read from (src/ccs/ccsoptions.h).
	mkdir -p "$(PRODUCT_DIR)/lib/ccs"
	cp docs/ccs-reference/ti-option-definitions/*.tsv "$(PRODUCT_DIR)/lib/ccs/"
	printf '{\n  "include": "include",\n  "lib": "lib",\n  "vcvars": "",\n  "compiler": "auto",\n  "indent": 4,\n  "tabs": false,\n  "font": "",\n  "includes": [],\n  "libraries": []\n}\n' > "$(PRODUCT_DIR)/settings.json"
# Into bin/lib/ rather than anywhere tidier, because that is where shc looks:
# beside its own binary. Both archives, debug included - see DEPENDENCIES.
	cp $(BINDIR)/lib/shmrt-$(SHM_TARGET).a \
	   $(BINDIR)/lib/shmrt-$(SHM_TARGET)-debug.a "$(PRODUCT_DIR)/bin/lib/"
	cp -R $(BINDIR)/lib/shmrt-tms6747 $(BINDIR)/lib/shmrt-tms6747-debug "$(PRODUCT_DIR)/bin/lib/"
	cp -R $(BINDIR)/lib/rts6x-tms6747 "$(PRODUCT_DIR)/bin/lib/"
	cp README.md "$(PRODUCT_DIR)/"
# The samples: projects/ (each in its folder, the CCS ones in ccs/) and programs/ (single files,
# with the headers they include). There is no examples/ since 03-10-2026.
	cp -R projects "$(PRODUCT_DIR)/projects"
	cp -R programs "$(PRODUCT_DIR)/programs"
	@echo "$(PRODUCT) is in $(PRODUCT_DIR)"

# build/ is Xcode's, not make's, and it lands inside the checkout unless the
# project is told otherwise - which is the one place in this workspace that
# still builds where it should not. Removed here so that "clean" means clean
# whichever tool last built, rather than only the one being asked.
clean:
	rm -rf $(OBJDIR) build
	rm -f $(EDITOR) tests/test tests/session

.PHONY: check-tools test session check confirm xcodeproj product clean
