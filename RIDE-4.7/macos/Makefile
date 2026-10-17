# RIDE.app - the editor in a macOS window. The third front end over the one
# core: the terminal editor is ../Makefile's, the Windows Forms window is
# ../winforms/RIDEGui.vcxproj, and this is AppKit through Objective-C++.
#
#     make            build ../../build/RIDE-4.7/RIDE.app
#     make run        build it and open it
#     make clean
#
# Like the Windows window it compiles the core and ../winforms/bridge.cpp -
# the C interface both windows speak - and none of the terminal's drawing.
# The .mm files are Objective-C++; ../src stays C++14 and is not touched.

# Built on a Mac only; check-sources and clean work anywhere.
ON_MAC := $(filter Darwin,$(shell uname -s))

CXX ?= clang++
ifeq ($(origin CXX),default)
  CXX := clang++
endif

# The core's own flags, as ../Makefile has them. The window's files add ARC
# and leave out -pedantic, which argues with Objective-C syntax rather than
# with anything written here.
# ARCHS names what the program runs on - one, as the machine building it, unless asked for both:
# `make ARCHS="arm64 x86_64"` builds a universal RIDE. DEBUG=1 adds -g, and a .dSYM beside the app.
# A deprecation the next SDK brings is a warning, not a failed build, even under -Werror (L14).
MACOS_MIN ?= 12.0
WERROR ?= -Werror -Wno-error=deprecated-declarations
ARCHS ?=
ARCH_FLAGS := $(foreach a,$(ARCHS),-arch $(a))
DEBUG ?=
DEBUG_FLAGS := $(if $(DEBUG),-g)
CXXFLAGS := -std=c++14 -Wall -Wextra $(WERROR) -O2 -mmacosx-version-min=$(MACOS_MIN) $(ARCH_FLAGS) $(DEBUG_FLAGS)
CORE_FLAGS := $(CXXFLAGS) -pedantic
MAC_FLAGS := $(CXXFLAGS) -fobjc-arc -I../src -I../winforms
FRAMEWORKS := -framework Cocoa

# Kept equal to CORE_SRC and SHM_SRC in ../Makefile - the list the Windows
# window compiles too. `make check-sources` says when they have drifted.
CORE_SRC := src/buffer.cpp src/compile.cpp src/convert.cpp \
       src/indent.cpp src/syntax.cpp \
       src/toolchain.cpp src/json.cpp src/project.cpp src/find.cpp \
       src/utf8.cpp src/workspace.cpp src/symbols.cpp src/demangle_win.cpp \
       src/path.cpp src/process.cpp src/debugger.cpp src/settings.cpp src/options.cpp src/about.cpp \
       src/ccs/ccsproject.cpp src/ccs/ccsoptions.cpp src/ccs/ccsxml.cpp src/ccs/ccsworkspace.cpp
SHM_SRC := src/shalimar/channel.cpp src/shalimar/session.cpp
BRIDGE_SRC := winforms/bridge.cpp
MAC_SRC := main.mm WindowController.mm CodeView.mm LineNumbers.mm

# Outside the checkout, beside the terminal editor's objects, so that a
# listing of the repository is source and nothing else.
BUILD ?= ../../build/RIDE-4.7
OBJDIR ?= $(BUILD)/mac-obj
APP ?= $(BUILD)/RIDE.app

CORE_OBJ := $(patsubst %.cpp,$(OBJDIR)/core/%.o,$(CORE_SRC) $(SHM_SRC) $(BRIDGE_SRC))
MAC_OBJ := $(patsubst %.mm,$(OBJDIR)/mac/%.o,$(MAC_SRC))

EXE := $(APP)/Contents/MacOS/RIDE

# Ad hoc, for this Mac, by default. For another's: SIGN="Developer ID Application: ..." and
# HARDENED=--options=runtime, then notarize - macos/README.md, "Signing".
SIGN ?= -
HARDENED ?=

all: $(APP)

mac-only:
	@[ -n "$(ON_MAC)" ] || { echo "RIDE.app is built on a Mac - use ../Makefile for the terminal editor" >&2; exit 1; }

$(OBJDIR)/core/%.o: ../%.cpp | mac-only
	@mkdir -p $(dir $@)
	$(CXX) $(CORE_FLAGS) -I../src -MMD -MP -c -o $@ $<

$(OBJDIR)/mac/%.o: %.mm | mac-only
	@mkdir -p $(dir $@)
	$(CXX) $(MAC_FLAGS) -MMD -MP -c -o $@ $<

-include $(CORE_OBJ:.o=.d) $(MAC_OBJ:.o=.d)

$(EXE): $(CORE_OBJ) $(MAC_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(FRAMEWORKS) -o $@ $(CORE_OBJ) $(MAC_OBJ)
	@if [ -n "$(DEBUG)" ]; then dsymutil $@ -o $(APP).dSYM; fi

# The bundle: the program, its Info.plist, the manual, and - docked beside the
# editor as on Windows - whatever compilers ../bin holds, with the Shalimar
# runtime in lib/ beside them. The editor finds them there first.
$(APP): $(EXE) Info.plist RIDE.icns
	@mkdir -p $(APP)/Contents/Resources
	cp Info.plist $(APP)/Contents/Info.plist
	@printf 'APPL????' > $(APP)/Contents/PkgInfo
	@for tool in ../bin/*.exe; do \
	    [ -f "$$tool" ] || continue; \
	    case "$$tool" in */RIDE.exe) continue;; esac; \
	    cp -p "$$tool" $(APP)/Contents/MacOS/; \
	done
	@if [ -d ../bin/lib ]; then rm -rf $(APP)/Contents/MacOS/lib $(APP)/Contents/Resources/lib; mkdir -p $(APP)/Contents/Resources; cp -Rp ../bin/lib $(APP)/Contents/Resources/lib; ln -s ../Resources/lib $(APP)/Contents/MacOS/lib; fi
	@# What git tracks and nothing else, so a demo's own build output never ships; a plain copy outside a checkout.
	@for d in help projects programs; do rm -rf $(APP)/Contents/Resources/$$d; \
	    if git -C .. rev-parse --git-dir >/dev/null 2>&1; then \
	        git -C .. ls-files -z -- $$d | (cd .. && tar --null -T - -cf -) | tar -xf - -C $(APP)/Contents/Resources; \
	    else cp -R ../$$d $(APP)/Contents/Resources/$$d; fi; done
	cp RIDE.icns $(APP)/Contents/Resources/RIDE.icns
	@# Each Mach-O helper signed on its own, then the app - not --deep, which Apple has deprecated and
	@# which would treat a non-Mach-O file as code (L12). SIGN names a Developer ID for distribution.
	@for tool in $(APP)/Contents/MacOS/*; do 	    [ "$$tool" = "$(EXE)" ] && continue; 	    [ -f "$$tool" ] && file -b "$$tool" | grep -q Mach-O && codesign --force $(HARDENED) --sign "$(SIGN)" "$$tool"; 	done; true
	codesign --force $(HARDENED) --sign "$(SIGN)" $(APP)
	@touch $(APP)
	@echo "built $(APP)"

run: $(APP)
	open $(APP)

check-sources:
	@want=$$(sed -n '/^CORE_SRC :=/,/[^\\]$$/p' ../Makefile | tr -s ' \\\t\n' ' ' | sed 's/CORE_SRC :=//'); \
	have=$$(sed -n '/^CORE_SRC :=/,/[^\\]$$/p' Makefile | tr -s ' \\\t\n' ' ' | sed 's/CORE_SRC :=//'); \
	[ "$$want" = "$$have" ] && echo "the core list matches ../Makefile" || \
	    { echo "CORE_SRC differs from ../Makefile:"; echo "  there: $$want"; echo "  here:  $$have"; exit 1; }

clean:
	rm -rf $(OBJDIR) $(APP)

.PHONY: all run check-sources clean mac-only
