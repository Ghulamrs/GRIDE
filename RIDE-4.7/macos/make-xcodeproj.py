#!/usr/bin/env python3
"""Writes Window.xcodeproj, the macOS window's Xcode project, from Makefile.

    python3 make-xcodeproj.py

The source list is read out of this directory's Makefile (CORE_SRC, SHM_SRC,
BRIDGE_SRC, MAC_SRC), so the project cannot build a different program from
`make`. The identifiers are derived from the file names, so writing it again
from an unchanged Makefile gives a byte-identical file.
"""

import hashlib
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))


def ident(*parts):
    return hashlib.md5("/".join(parts).encode()).hexdigest()[:24].upper()


def listed(makefile, name):
    text = open(makefile).read()
    m = re.search(r"^" + name + r"\s*:=\s*((?:.*\\\n)*.*)$", text, re.M)
    if not m:
        raise SystemExit("no %s in %s" % (name, makefile))
    return m.group(1).replace("\\\n", " ").split()


def main():
    makefile = os.path.join(HERE, "Makefile")
    core = listed(makefile, "CORE_SRC") + listed(makefile, "SHM_SRC") + listed(makefile, "BRIDGE_SRC")
    mac = listed(makefile, "MAC_SRC")
    headers = sorted(f for f in os.listdir(HERE) if f.endswith(".h"))

    files = []  # (path relative to macos/, group, compiled)
    for f in mac:
        files.append((f, "Window", True))
    for f in headers:
        files.append((f, "Window", False))
    files.append(("Info.plist", "Window", False))
    for f in core:
        files.append(("../" + f, "Core", True))

    def ftype(path):
        ext = os.path.splitext(path)[1]
        return {".mm": "sourcecode.cpp.objcpp", ".cpp": "sourcecode.cpp.cpp",
                ".h": "sourcecode.c.h", ".plist": "text.plist.xml"}[ext]

    target = ident("target")
    project = ident("project")
    main_group = ident("group", "main")
    products_group = ident("group", "products")
    groups = {"Window": ident("group", "Window"), "Core": ident("group", "Core"),
              "Frameworks": ident("group", "Frameworks")}
    app_ref = ident("product")
    cocoa_ref = ident("framework", "Cocoa")
    cocoa_build = ident("build", "Cocoa")
    sources_phase = ident("phase", "sources")
    frameworks_phase = ident("phase", "frameworks")
    resources_phase = ident("phase", "resources")
    bundle_phase = ident("phase", "bundle")
    proj_configs = ident("configlist", "project")
    target_configs = ident("configlist", "target")

    out = []
    w = out.append
    w("// !$*UTF8*$!\n{\n\tarchiveVersion = 1;\n\tclasses = {\n\t};\n\tobjectVersion = 56;\n\tobjects = {\n")

    w("\n/* Begin PBXBuildFile section */\n")
    for path, _, compiled in files:
        if compiled:
            w("\t\t%s /* %s in Sources */ = {isa = PBXBuildFile; fileRef = %s /* %s */; };\n"
              % (ident("build", path), os.path.basename(path), ident("ref", path), os.path.basename(path)))
    w("\t\t%s /* Cocoa.framework in Frameworks */ = {isa = PBXBuildFile; fileRef = %s /* Cocoa.framework */; };\n"
      % (cocoa_build, cocoa_ref))
    w("/* End PBXBuildFile section */\n")

    w("\n/* Begin PBXFileReference section */\n")
    for path, _, _ in files:
        w("\t\t%s /* %s */ = {isa = PBXFileReference; lastKnownFileType = %s; name = %s; path = \"%s\"; sourceTree = \"<group>\"; };\n"
          % (ident("ref", path), os.path.basename(path), ftype(path), os.path.basename(path), path))
    w("\t\t%s /* RIDE.app */ = {isa = PBXFileReference; explicitFileType = wrapper.application; includeInIndex = 0; path = RIDE.app; sourceTree = BUILT_PRODUCTS_DIR; };\n" % app_ref)
    w("\t\t%s /* Cocoa.framework */ = {isa = PBXFileReference; lastKnownFileType = wrapper.framework; name = Cocoa.framework; path = System/Library/Frameworks/Cocoa.framework; sourceTree = SDKROOT; };\n" % cocoa_ref)
    w("/* End PBXFileReference section */\n")

    w("\n/* Begin PBXFrameworksBuildPhase section */\n")
    w("\t\t%s /* Frameworks */ = {\n\t\t\tisa = PBXFrameworksBuildPhase;\n\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = (\n\t\t\t\t%s /* Cocoa.framework in Frameworks */,\n\t\t\t);\n\t\t\trunOnlyForDeploymentPostprocessing = 0;\n\t\t};\n" % (frameworks_phase, cocoa_build))
    w("/* End PBXFrameworksBuildPhase section */\n")

    w("\n/* Begin PBXGroup section */\n")
    w("\t\t%s = {\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n" % main_group)
    for name in ("Window", "Core", "Frameworks"):
        w("\t\t\t\t%s /* %s */,\n" % (groups[name], name))
    w("\t\t\t\t%s /* Products */,\n\t\t\t);\n\t\t\tsourceTree = \"<group>\";\n\t\t};\n" % products_group)
    for name in ("Window", "Core"):
        w("\t\t%s /* %s */ = {\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n" % (groups[name], name))
        for path, group, _ in files:
            if group == name:
                w("\t\t\t\t%s /* %s */,\n" % (ident("ref", path), os.path.basename(path)))
        w("\t\t\t);\n\t\t\tname = %s;\n\t\t\tsourceTree = \"<group>\";\n\t\t};\n" % name)
    w("\t\t%s /* Frameworks */ = {\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n\t\t\t\t%s /* Cocoa.framework */,\n\t\t\t);\n\t\t\tname = Frameworks;\n\t\t\tsourceTree = \"<group>\";\n\t\t};\n" % (groups["Frameworks"], cocoa_ref))
    w("\t\t%s /* Products */ = {\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n\t\t\t\t%s /* RIDE.app */,\n\t\t\t);\n\t\t\tname = Products;\n\t\t\tsourceTree = \"<group>\";\n\t\t};\n" % (products_group, app_ref))
    w("/* End PBXGroup section */\n")

    w("\n/* Begin PBXNativeTarget section */\n")
    w("\t\t%s /* RIDE */ = {\n\t\t\tisa = PBXNativeTarget;\n\t\t\tbuildConfigurationList = %s;\n\t\t\tbuildPhases = (\n\t\t\t\t%s /* Sources */,\n\t\t\t\t%s /* Frameworks */,\n\t\t\t\t%s /* Resources */,\n\t\t\t\t%s /* Dock the compilers and the manual */,\n\t\t\t);\n\t\t\tbuildRules = (\n\t\t\t);\n\t\t\tdependencies = (\n\t\t\t);\n\t\t\tname = RIDE;\n\t\t\tproductName = RIDE;\n\t\t\tproductReference = %s /* RIDE.app */;\n\t\t\tproductType = \"com.apple.product-type.application\";\n\t\t};\n"
      % (target, target_configs, sources_phase, frameworks_phase, resources_phase, bundle_phase, app_ref))
    w("/* End PBXNativeTarget section */\n")

    w("\n/* Begin PBXProject section */\n")
    w("\t\t%s /* Project object */ = {\n\t\t\tisa = PBXProject;\n\t\t\tattributes = {\n\t\t\t\tBuildIndependentTargetsInParallel = 1;\n\t\t\t\tLastUpgradeCheck = 1600;\n\t\t\t};\n\t\t\tbuildConfigurationList = %s;\n\t\t\tcompatibilityVersion = \"Xcode 14.0\";\n\t\t\tdevelopmentRegion = en;\n\t\t\thasScannedForEncodings = 0;\n\t\t\tknownRegions = (\n\t\t\t\ten,\n\t\t\t\tBase,\n\t\t\t);\n\t\t\tmainGroup = %s;\n\t\t\tproductRefGroup = %s /* Products */;\n\t\t\tprojectDirPath = \"\";\n\t\t\tprojectRoot = \"\";\n\t\t\ttargets = (\n\t\t\t\t%s /* RIDE */,\n\t\t\t);\n\t\t};\n"
      % (project, proj_configs, main_group, products_group, target))
    w("/* End PBXProject section */\n")

    w("\n/* Begin PBXResourcesBuildPhase section */\n")
    w("\t\t%s /* Resources */ = {\n\t\t\tisa = PBXResourcesBuildPhase;\n\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = (\n\t\t\t);\n\t\t\trunOnlyForDeploymentPostprocessing = 0;\n\t\t};\n" % resources_phase)
    w("/* End PBXResourcesBuildPhase section */\n")

    script = ("set -e\\n"
              "dest=\\\"$TARGET_BUILD_DIR/$EXECUTABLE_FOLDER_PATH\\\"\\n"
              "for tool in \\\"$SRCROOT\\\"/../bin/*.exe; do [ -f \\\"$tool\\\" ] || continue; "
              "case \\\"$tool\\\" in */RIDE.exe) continue;; esac; cp -p \\\"$tool\\\" \\\"$dest/\\\"; done\\n"
              "res=\\\"$TARGET_BUILD_DIR/$UNLOCALIZED_RESOURCES_FOLDER_PATH\\\"; mkdir -p \\\"$res\\\"\\n"
              "if [ -d \\\"$SRCROOT/../bin/lib\\\" ]; then rm -rf \\\"$dest/lib\\\" \\\"$res/lib\\\"; cp -Rp \\\"$SRCROOT/../bin/lib\\\" \\\"$res/lib\\\"; ln -s ../Resources/lib \\\"$dest/lib\\\"; fi\\n"
              "res=\\\"$TARGET_BUILD_DIR/$UNLOCALIZED_RESOURCES_FOLDER_PATH\\\"\\n"
              "mkdir -p \\\"$res\\\" && rm -rf \\\"$res/help\\\" && cp -R \\\"$SRCROOT/../help\\\" \\\"$res/help\\\"\\n"
              "# The sample projects and programs, copied into ~/Documents/RIDE on first use.\\n"
              "cp \\\"$SRCROOT/RIDE.icns\\\" \\\"$res/RIDE.icns\\\"\\n"
              "for d in projects programs; do rm -rf \\\"$res/$d\\\"; cp -R \\\"$SRCROOT/../$d\\\" \\\"$res/$d\\\"; done\\n"
              "# Re-signed here too: when only ../bin changed, Xcode skips its own CodeSign.\\n"
              "if [ \\\"$CODE_SIGNING_ALLOWED\\\" = YES ]; then xattr -cr \\\"$TARGET_BUILD_DIR/$WRAPPER_NAME\\\"; codesign --force --deep --sign \\\"${EXPANDED_CODE_SIGN_IDENTITY:--}\\\" \\\"$TARGET_BUILD_DIR/$WRAPPER_NAME\\\"; fi\\n")
    w("\n/* Begin PBXShellScriptBuildPhase section */\n")
    w("\t\t%s /* Dock the compilers and the manual */ = {\n\t\t\tisa = PBXShellScriptBuildPhase;\n\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = (\n\t\t\t);\n\t\t\tinputPaths = (\n\t\t\t);\n\t\t\tname = \"Dock the compilers and the manual\";\n\t\t\toutputPaths = (\n\t\t\t);\n\t\t\talwaysOutOfDate = 1;\n\t\t\trunOnlyForDeploymentPostprocessing = 0;\n\t\t\tshellPath = /bin/sh;\n\t\t\tshellScript = \"%s\";\n\t\t};\n" % (bundle_phase, script))
    w("/* End PBXShellScriptBuildPhase section */\n")

    w("\n/* Begin PBXSourcesBuildPhase section */\n")
    w("\t\t%s /* Sources */ = {\n\t\t\tisa = PBXSourcesBuildPhase;\n\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = (\n" % sources_phase)
    for path, _, compiled in files:
        if compiled:
            w("\t\t\t\t%s /* %s in Sources */,\n" % (ident("build", path), os.path.basename(path)))
    w("\t\t\t);\n\t\t\trunOnlyForDeploymentPostprocessing = 0;\n\t\t};\n")
    w("/* End PBXSourcesBuildPhase section */\n")

    common_project = {
        "CLANG_CXX_LANGUAGE_STANDARD": "\"c++14\"",
        "CLANG_ENABLE_OBJC_ARC": "YES",
        "GCC_WARN_UNUSED_VARIABLE": "YES",
        "MACOSX_DEPLOYMENT_TARGET": "12.0",
        "SDKROOT": "macosx",
        "HEADER_SEARCH_PATHS": "(\n\t\t\t\t\t\"$(SRCROOT)/../src\",\n\t\t\t\t\t\"$(SRCROOT)/../winforms\",\n\t\t\t\t)",
        "WARNING_CFLAGS": "(\n\t\t\t\t\t\"-Wall\",\n\t\t\t\t\t\"-Wextra\",\n\t\t\t\t)",
        "GCC_TREAT_WARNINGS_AS_ERRORS": "YES",
        "ALWAYS_SEARCH_USER_PATHS": "NO",
    }
    common_target = {
        "PRODUCT_NAME": "RIDE",
        "PRODUCT_BUNDLE_IDENTIFIER": "com.ghulamrs.ride47",
        "INFOPLIST_FILE": "Info.plist",
        "CODE_SIGN_IDENTITY": "\"-\"",
        "CODE_SIGN_STYLE": "Manual",
        "COMBINE_HIDPI_IMAGES": "YES",
        "ENABLE_HARDENED_RUNTIME": "NO",
    }

    def config(key, name, settings):
        w("\t\t%s /* %s */ = {\n\t\t\tisa = XCBuildConfiguration;\n\t\t\tbuildSettings = {\n" % (key, name))
        for k in sorted(settings):
            w("\t\t\t\t%s = %s;\n" % (k, settings[k]))
        w("\t\t\t};\n\t\t\tname = %s;\n\t\t};\n" % name)

    w("\n/* Begin XCBuildConfiguration section */\n")
    debug = dict(common_project, GCC_OPTIMIZATION_LEVEL="0", ONLY_ACTIVE_ARCH="YES", DEBUG_INFORMATION_FORMAT="dwarf",
                 GCC_PREPROCESSOR_DEFINITIONS="(\n\t\t\t\t\t\"DEBUG=1\",\n\t\t\t\t)")
    release = dict(common_project, GCC_OPTIMIZATION_LEVEL="2", DEBUG_INFORMATION_FORMAT="\"dwarf-with-dsym\"")
    config(ident("config", "project", "Debug"), "Debug", debug)
    config(ident("config", "project", "Release"), "Release", release)
    config(ident("config", "target", "Debug"), "Debug", common_target)
    config(ident("config", "target", "Release"), "Release", common_target)
    w("/* End XCBuildConfiguration section */\n")

    w("\n/* Begin XCConfigurationList section */\n")
    for key, scope in ((proj_configs, "project"), (target_configs, "target")):
        w("\t\t%s = {\n\t\t\tisa = XCConfigurationList;\n\t\t\tbuildConfigurations = (\n\t\t\t\t%s /* Debug */,\n\t\t\t\t%s /* Release */,\n\t\t\t);\n\t\t\tdefaultConfigurationIsVisible = 0;\n\t\t\tdefaultConfigurationName = Release;\n\t\t};\n"
          % (key, ident("config", scope, "Debug"), ident("config", scope, "Release")))
    w("/* End XCConfigurationList section */\n")

    w("\t};\n\trootObject = %s /* Project object */;\n}\n" % project)

    where = os.path.join(HERE, "Window.xcodeproj")
    os.makedirs(where, exist_ok=True)
    with open(os.path.join(where, "project.pbxproj"), "w") as f:
        f.write("".join(out))
    print("wrote", os.path.relpath(os.path.join(where, "project.pbxproj"), HERE), "-", len(files), "files")


if __name__ == "__main__":
    main()
