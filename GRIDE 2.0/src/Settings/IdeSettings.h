#pragma once
// ============================================================================
//  GRIDE 2.0 - IDE settings (Phase 3)
//  Stored as JSON in %APPDATA%\GRIDE\settings.json
//  The tools are a flat list, one entry per thing the IDE runs, each with a
//  program and an argument template. Every entry is made from toolchain.json
//  (ToolchainFile.h) - RIDE's names, arguments and lookup - and follows the
//  file until the user changes it under Tools > Options, where it is marked
//  custom; "Restore from toolchain.json" takes the file's definition back.
// ============================================================================
#include "CompilerOptions.h"

namespace Gride
{
    public enum class LanguageKind { C90, Cpp11, Shalimar };
    public enum class TargetKind { Windows, Linux, macOS, TMS6747 };

    public ref class Names abstract sealed
    {
    public:
        static String^ LanguageName(LanguageKind k)
        {
            switch (k)
            {
            case LanguageKind::Cpp11:    return "C++11";
            case LanguageKind::Shalimar: return "Shalimar";
            default:                     return "C90";
            }
        }

        static LanguageKind ParseLanguage(String^ s)
        {
            if (s == nullptr) return LanguageKind::C90;
            String^ t = s->Trim();
            if (String::Equals(t, "C++11", StringComparison::OrdinalIgnoreCase) ||
                String::Equals(t, "Cpp11", StringComparison::OrdinalIgnoreCase)) return LanguageKind::Cpp11;
            if (String::Equals(t, "Shalimar", StringComparison::OrdinalIgnoreCase)) return LanguageKind::Shalimar;
            return LanguageKind::C90;
        }

        static String^ TargetName(TargetKind k)
        {
            switch (k)
            {
            case TargetKind::Linux:   return "Linux";
            case TargetKind::macOS:   return "macOS";
            case TargetKind::TMS6747: return "TMS6747";
            default:                  return "Windows";
            }
        }

        static TargetKind ParseTarget(String^ s)
        {
            if (s == nullptr) return TargetKind::Windows;
            String^ t = s->Trim();
            if (String::Equals(t, "Linux", StringComparison::OrdinalIgnoreCase)) return TargetKind::Linux;
            if (String::Equals(t, "macOS", StringComparison::OrdinalIgnoreCase) ||
                String::Equals(t, "Mac", StringComparison::OrdinalIgnoreCase)) return TargetKind::macOS;
            if (String::Equals(t, "TMS6747", StringComparison::OrdinalIgnoreCase) ||
                String::Equals(t, "TMS320C6747", StringComparison::OrdinalIgnoreCase) ||
                String::Equals(t, "DSP", StringComparison::OrdinalIgnoreCase)) return TargetKind::TMS6747;
            return TargetKind::Windows;
        }

        // whether a name (the IDE's, or toolchain.json's ideTarget) means the kind; an unknown name means none
        static bool IsTargetName(String^ s, TargetKind k)
        {
            if (String::IsNullOrWhiteSpace(s)) return false;
            if (ParseTarget(s) != k) return false;
            return k != TargetKind::Windows || String::Equals(s->Trim(), "Windows", StringComparison::OrdinalIgnoreCase);
        }

        static array<String^>^ LanguageNames() { return gcnew array<String^>{ "C90", "C++11", "Shalimar" }; }
        static array<String^>^ TargetNames()   { return gcnew array<String^>{ "Windows", "Linux", "macOS", "TMS6747" }; }
    };

    // how a tool id of the IDE is bound to toolchain.json: the target whose roles table names the
    // tool (none for the compilers and the converter, which serve every target), the role, and the
    // operation whose arguments it runs; "runtime" binds to the target's runtime library instead
    public ref class ToolBinding
    {
    public:
        ToolBinding(String^ id, String^ category, bool hasTarget, TargetKind kind, String^ role, String^ operation)
        {
            Id = id; Category = category; HasTarget = hasTarget; Kind = kind; Role = role; Operation = operation;
        }
        property String^ Id;
        property String^ Category;
        property bool HasTarget;
        property TargetKind Kind;
        property String^ Role;
        property String^ Operation;
        property bool IsRuntime { bool get() { return String::Equals(Role, "runtime"); } }
        property bool IsRunner { bool get() { return Role->Length == 0; } }
    };

    // Well-known tool ids. Every external program the IDE calls is a "tool"
    // whose path / command line is configurable in Tools > Options > Toolchains.
    public ref class ToolIds abstract sealed
    {
    public:
        literal String^ CompilerC90        = "compiler.c90";
        literal String^ CompilerCpp11      = "compiler.cpp11";
        literal String^ CompilerShalimar   = "compiler.shalimar";
        literal String^ CompilerCl         = "compiler.cl";       // Microsoft's cl, the machine's own (R12: a Visual Studio project names it)
        literal String^ WinAssembler       = "win.masm";
        literal String^ WinLinker          = "win.link";
        literal String^ WinDebugAssembler  = "win.clang";      // a Debug build's CodeView path: clang on the GNU spelling
        literal String^ WinDebugLinker     = "win.mslink";     // Microsoft's link.exe /DEBUG, which writes the PDB
        literal String^ WinNativeAssembler = "win.ml64";       // offered when the project's own assembler fails (the native question)
        literal String^ WinDebugger        = "win.cdb";        // the Windows debugger (cdb, which reads the PDB)
        literal String^ WinFallbackDebugger = "win.lldb";      // the one standing in where cdb is not installed (lldb, $RIDE_LLDB)
        literal String^ LinuxAssembler     = "linux.as";
        literal String^ LinuxLinker        = "linux.ld";
        literal String^ LinuxRunner        = "linux.run";
        literal String^ MacAssembler       = "macos.as";
        literal String^ MacLinker          = "macos.ld";
        literal String^ MacRunner          = "macos.run";
        literal String^ TmsAssembler       = "tms.asm6x";
        literal String^ TmsLinker          = "tms.lnk6x";
        literal String^ TmsNativeLinker    = "tms.tilnk6x";    // TI's lnk6x, offered when RIDE's fails
        literal String^ TmsRuntime         = "tms.rts6747";
        literal String^ TmsSimulator       = "tms.vm6747";      // the emulator, which runs the assembly
        literal String^ TmsSim             = "tms.sim6747";     // the simulator, which runs the linked .out
        literal String^ ConvertCToShalimar = "convert.c2shalimar";
        literal String^ ConvertShalimarToC = "convert.shalimar2c";

        // the ids in the Options page's order, each bound to toolchain.json
        static array<ToolBinding^>^ Bindings()
        {
            return gcnew array<ToolBinding^>{
                gcnew ToolBinding(CompilerC90, "Compiler", false, TargetKind::Windows, "cCompiler", "compileToAssembly"),
                gcnew ToolBinding(CompilerCpp11, "Compiler", false, TargetKind::Windows, "cppCompiler", "compileToAssembly"),
                gcnew ToolBinding(CompilerShalimar, "Compiler", false, TargetKind::Windows, "shalimarCompiler", "compileToAssembly"),
                gcnew ToolBinding(CompilerCl, "Compiler", false, TargetKind::Windows, "hostCompiler", "compileToAssembly"),
                gcnew ToolBinding(WinAssembler, "Windows", true, TargetKind::Windows, "assembler", "assemble"),
                gcnew ToolBinding(WinLinker, "Windows", true, TargetKind::Windows, "linker", "link"),
                gcnew ToolBinding(WinDebugAssembler, "Windows", true, TargetKind::Windows, "debugAssembler", "assemble"),
                gcnew ToolBinding(WinDebugLinker, "Windows", true, TargetKind::Windows, "debugLinker", "link"),
                gcnew ToolBinding(WinNativeAssembler, "Windows", true, TargetKind::Windows, "nativeAssembler", "assemble"),
                gcnew ToolBinding(WinDebugger, "Windows", true, TargetKind::Windows, "debugger", "debug"),
                gcnew ToolBinding(WinFallbackDebugger, "Windows", true, TargetKind::Windows, "fallbackDebugger", "debug"),
                gcnew ToolBinding(LinuxAssembler, "Linux", true, TargetKind::Linux, "assembler", "assemble"),
                gcnew ToolBinding(LinuxLinker, "Linux", true, TargetKind::Linux, "linker", "link"),
                gcnew ToolBinding(LinuxRunner, "Linux", true, TargetKind::Linux, "", ""),
                gcnew ToolBinding(MacAssembler, "macOS", true, TargetKind::macOS, "assembler", "assemble"),
                gcnew ToolBinding(MacLinker, "macOS", true, TargetKind::macOS, "linker", "link"),
                gcnew ToolBinding(MacRunner, "macOS", true, TargetKind::macOS, "", ""),
                gcnew ToolBinding(TmsAssembler, "TMS6747", true, TargetKind::TMS6747, "assembler", "assemble"),
                gcnew ToolBinding(TmsLinker, "TMS6747", true, TargetKind::TMS6747, "linker", "link"),
                gcnew ToolBinding(TmsNativeLinker, "TMS6747", true, TargetKind::TMS6747, "nativeLinker", "link"),
                gcnew ToolBinding(TmsRuntime, "Runtime", true, TargetKind::TMS6747, "runtime", ""),
                gcnew ToolBinding(TmsSimulator, "TMS6747", true, TargetKind::TMS6747, "emulator", "launch"),
                gcnew ToolBinding(TmsSim, "TMS6747", true, TargetKind::TMS6747, "simulator", "simulate"),
                gcnew ToolBinding(ConvertCToShalimar, "Converter", false, TargetKind::Windows, "converter", "convertToShalimar"),
                gcnew ToolBinding(ConvertShalimarToC, "Converter", false, TargetKind::Windows, "converter", "convertToC") };
        }
        static ToolBinding^ BindingOf(String^ id)
        {
            for each (ToolBinding^ b in Bindings()) if (String::Equals(b->Id, id, StringComparison::OrdinalIgnoreCase)) return b;
            return nullptr;
        }
        // the compiler id of a role in toolchain.json, and back
        static String^ CompilerOfRole(String^ role)
        {
            if (String::Equals(role, "cCompiler")) return CompilerC90;
            if (String::Equals(role, "cppCompiler")) return CompilerCpp11;
            if (String::Equals(role, "shalimarCompiler")) return CompilerShalimar;
            if (String::Equals(role, "hostCompiler")) return CompilerCl;
            return nullptr;
        }
        static String^ RoleOfCompiler(String^ toolId)
        {
            ToolBinding^ b = BindingOf(toolId);
            return b != nullptr && CompilerOfRole(b->Role) != nullptr ? b->Role : nullptr;
        }
        static array<String^>^ CompilerIds() { return gcnew array<String^>{ CompilerC90, CompilerCpp11, CompilerShalimar, CompilerCl }; }

        // the file's target entry for a kind (by its ideTarget), or nullptr when the file lacks it
        static TargetEntry^ TargetEntryOf(TargetKind k)
        {
            for each (TargetEntry^ t in ToolchainFile::Current->Targets)
                if (Names::IsTargetName(t->IdeTarget, k)) return t;
            return nullptr;
        }
        // the file's tool behind an id: the target's tool for the role, or the first tool of the role
        static ToolEntry^ EntryOf(String^ toolId)
        {
            ToolBinding^ b = BindingOf(toolId);
            if (b == nullptr || b->IsRunner || b->IsRuntime) return nullptr;
            if (!b->HasTarget) return ToolchainFile::Current->ToolByRole(b->Role);
            TargetEntry^ t = TargetEntryOf(b->Kind);
            return t == nullptr ? nullptr : ToolchainFile::Current->Tool(t->ToolIdFor(b->Role));
        }
        // the IDE's id standing for a target's role: the one bound to the role, else the one bound to the
        // tool the role names (Windows' nativeLinker is mslink, the Debug linker's entry); nullptr for none
        static String^ IdOfRole(TargetKind k, String^ role)
        {
            for each (ToolBinding^ b in Bindings())
                if (b->HasTarget && b->Kind == k && String::Equals(b->Role, role)) return b->Id;
            TargetEntry^ t = TargetEntryOf(k);
            String^ tool = t == nullptr ? String::Empty : t->ToolIdFor(role);
            if (tool->Length == 0) return nullptr;
            for each (ToolBinding^ b in Bindings())
            {
                if (!b->HasTarget || b->Kind != k) continue;
                ToolEntry^ e = EntryOf(b->Id);
                if (e != nullptr && String::Equals(e->Id, tool)) return b->Id;
            }
            return nullptr;
        }
    };

    // RIDE's toolchain words (toolchain.cpp: toolchainFrom / toolchainWord): what a project may
    // name as its compiler - auto takes each file's own, the others take every file or refuse it.
    // The words are the compilers' "words" in toolchain.json (c90 / cc1, cpp11 / cxx1, shalimar / shc)
    public ref class ToolWords abstract sealed
    {
    public:
        literal String^ Auto = "auto";

        static array<String^>^ Words()
        {
            List<String^>^ r = gcnew List<String^>();
            r->Add(Auto);
            for each (String^ id in ToolIds::CompilerIds())
            {
                String^ w = WordOfTool(id);
                if (!String::Equals(w, Auto) && !r->Contains(w)) r->Add(w);
            }
            return r->ToArray();
        }

        // the compiler's first word; anything else is auto
        static String^ Canonical(String^ word)
        {
            String^ id = ToolOfWord(word);
            return id == nullptr ? Auto : WordOfTool(id);
        }

        // the compiler a word names; nullptr for auto or an unknown word
        static String^ ToolOfWord(String^ word)
        {
            if (String::IsNullOrWhiteSpace(word)) return nullptr;
            ToolEntry^ e = ToolchainFile::Current->ToolByWord(word);
            return e == nullptr ? nullptr : ToolIds::CompilerOfRole(e->Role);
        }

        static String^ WordOfTool(String^ toolId)
        {
            ToolEntry^ e = ToolIds::EntryOf(toolId);
            if (e == nullptr || ToolIds::RoleOfCompiler(toolId) == nullptr) return Auto;
            return e->Words->Count > 0 ? e->Words[0] : e->Name;
        }

        static String^ LanguageOfTool(String^ toolId)
        {
            ToolEntry^ e = ToolIds::EntryOf(toolId);
            return e != nullptr && e->Languages->Count > 0 ? e->Languages[0] : String::Empty;
        }

        // RIDE's refusal: the named compiler does not take the file's language
        static String^ Refusal(String^ namedTool, String^ fileTool)
        {
            String^ picks = String::Concat(", and it picks ", WordOfTool(fileTool));
            if (String::Equals(fileTool, ToolIds::CompilerShalimar))
                return String::Concat(WordOfTool(namedTool), " does not compile Shalimar - choose the automatic compiler", picks);
            return String::Concat(WordOfTool(namedTool), " compiles ", LanguageOfTool(namedTool), ", not ", LanguageOfTool(fileTool),
                                  " - choose the automatic compiler", picks);
        }
    };

    // Where RIDE's tools are looked for (editor.cpp: beside the program; the installation): the
    // IDE's own folder, the installation root named under Tools > Options (root, or its bin), else
    // toolchain.json's default roots holding one of its markers under its bin
    public ref class IdeSettingsPaths abstract sealed
    {
    public:
        static String^ RideRoot;                 // the setting in force (IdeSettings::Normalize keeps it current)
        static String^ RideIncludeDir;           // RIDE's settings.json "include" / "lib": the installation's header
        static String^ RideLibDir;               // directories named (relative to the installation, or absolute); "" for the defaults
        static String^ CcsRoot;                  // RIDE's ccs.root: the CCS installation ${CG_TOOL_ROOT} is found under
        static String^ TiLibDir;                 // RIDE's tilib: a second directory TI's runtime is looked for in

        static String^ IdeDirectory()
        {
            try { return AppDomain::CurrentDomain->BaseDirectory->TrimEnd(L'\\'); }
            catch (Exception^) { return nullptr; }
        }

        static String^ BinDir() { return ToolchainFile::Current->Installation->BinDir; }

        // RIDE's installation root: the named one (a root, or its bin), else the first default root with a marker in its bin
        static String^ RideRootDir()
        {
            String^ named = String::IsNullOrWhiteSpace(RideRoot) ? String::Empty : System::Environment::ExpandEnvironmentVariables(RideRoot->Trim()->Trim(L'"'));
            if (named->Length > 0)
            {
                if (!Directory::Exists(named)) return nullptr;
                // the bin named: the root above it
                String^ bin = BinDir();
                if (bin->Length > 0 && String::Equals(Path::GetFileName(named->TrimEnd(L'\\')), bin, StringComparison::OrdinalIgnoreCase) && !HasMarker(named))
                    return Path::GetDirectoryName(named->TrimEnd(L'\\'));
                return named;
            }
            for each (String^ r in ToolchainFile::Current->Installation->DefaultRoots)
            {
                String^ root = System::Environment::ExpandEnvironmentVariables(r);
                try
                {
                    if (Directory::Exists(root) && (HasMarker(Path::Combine(root, BinDir())) || HasMarker(root))) return root;
                }
                catch (ArgumentException^) { }
            }
            return nullptr;
        }

        // the folders an installation-relative directory is looked for under, in order: beside the
        // IDE, under RIDE's bin (where it keeps lib), under its root
        static List<String^>^ BaseDirs()
        {
            List<String^>^ dirs = gcnew List<String^>();
            String^ ide = IdeDirectory();
            if (ide != nullptr) dirs->Add(ide);
            String^ root = RideRootDir();
            if (root != nullptr)
            {
                String^ bin = Path::Combine(root, BinDir());
                if (bin != root) dirs->Add(bin);
                dirs->Add(root);
            }
            return dirs;
        }

        // a library's directory (toolchain.json's libraries): the variable naming it outright, else the
        // installation-relative directory under the base folders, with its marker file in it
        static String^ FindLibraryDir(LibraryEntry^ lib)
        {
            if (lib == nullptr || String::IsNullOrEmpty(lib->Directory)) return nullptr;
            try
            {
                if (!String::IsNullOrEmpty(lib->DirectoryEnv))
                {
                    String^ env = System::Environment::GetEnvironmentVariable(lib->DirectoryEnv);
                    if (!String::IsNullOrWhiteSpace(env) && Directory::Exists(env) && HasFile(env, lib->Marker)) return Path::GetFullPath(env);
                }
                for each (String^ d in BaseDirs())
                {
                    String^ dir = Path::Combine(d, lib->Directory);
                    if (Directory::Exists(dir) && HasFile(dir, lib->Marker)) return dir;
                }
            }
            catch (Exception^) { }
            return nullptr;
        }

        // the Shalimar runtime's assembly for the emulator and for a C6747 link with no packed library:
        // its variable, else the configuration's directory, the Release one standing in for a Debug one that is not there
        // a library's file for the configuration (the Debug one, else the Release one) in its directory under the base folders, or nullptr
        static String^ FindLibraryFile(LibraryEntry^ lib, bool debug)
        {
            if (lib == nullptr) return nullptr;
            try
            {
                for each (String^ d in BaseDirs())
                    for each (bool which in gcnew array<bool>{ debug, false })
                    {
                        String^ name = lib->FileFor(which);
                        if (String::IsNullOrEmpty(name)) continue;
                        String^ f = Path::Combine(Path::Combine(d, lib->Directory), name);
                        if (File::Exists(f)) return Path::GetFullPath(f);
                    }
            }
            catch (Exception^) { }
            return nullptr;
        }

        static String^ FindShalimarRuntimeAsm(LibraryEntry^ lib, bool debug)
        {
            if (lib == nullptr) return nullptr;
            try
            {
                String^ envName = lib->AssemblyDirectoryEnvFor(debug);
                if (!String::IsNullOrEmpty(envName))
                {
                    String^ env = System::Environment::GetEnvironmentVariable(envName);
                    if (!String::IsNullOrWhiteSpace(env) && Directory::Exists(env)) return Path::GetFullPath(env);
                }
                for each (String^ d in BaseDirs())
                    for each (bool which in gcnew array<bool>{ debug, false })
                    {
                        String^ rel = lib->AssemblyDirectoryFor(which);
                        if (rel->Length == 0) continue;
                        String^ dir = Path::Combine(d, rel);
                        if (Directory::Exists(dir)) return dir;
                    }
            }
            catch (Exception^) { }
            return nullptr;
        }

        // the installation's header directories a compiler is given (RIDE's includeFlags: lib for c90,
        // include for cpp11), each where it is found under the base folders
        static List<String^>^ InstallIncludeDirs(ToolEntry^ e)
        {
            List<String^>^ r = gcnew List<String^>();
            if (e == nullptr || String::IsNullOrEmpty(e->IncludeFlag)) return r;
            InstallationEntry^ inst = ToolchainFile::Current->Installation;
            for each (String^ which in e->InstallIncludeDirs)
            {
                String^ name = which;
                bool isLib = String::Equals(which, "lib", StringComparison::OrdinalIgnoreCase) || String::Equals(which, inst->LibDir, StringComparison::OrdinalIgnoreCase);
                bool isInclude = String::Equals(which, "include", StringComparison::OrdinalIgnoreCase) || String::Equals(which, inst->IncludeDir, StringComparison::OrdinalIgnoreCase);
                if (isLib && inst->LibDir->Length > 0) name = inst->LibDir;
                else if (isInclude && inst->IncludeDir->Length > 0) name = inst->IncludeDir;
                // the directory the settings name (RIDE's installedDir): absolute, or against the installation; nothing when it is not there
                String^ named = isLib ? RideLibDir : isInclude ? RideIncludeDir : nullptr;
                if (!String::IsNullOrWhiteSpace(named))
                {
                    try
                    {
                        String^ n = System::Environment::ExpandEnvironmentVariables(named->Trim()->Trim(L'"'));
                        String^ root = RideRootDir();
                        String^ dir = Path::IsPathRooted(n) ? n : Path::Combine(root != nullptr ? root : IdeDirectory(), n);
                        if (Directory::Exists(dir) && !r->Contains(Path::GetFullPath(dir))) r->Add(Path::GetFullPath(dir));
                    }
                    catch (Exception^) { }
                    continue;
                }
                for each (String^ d in BaseDirs())
                {
                    try
                    {
                        String^ dir = Path::Combine(d, name);
                        if (Directory::Exists(dir)) { if (!r->Contains(dir)) r->Add(dir); break; }
                    }
                    catch (Exception^) { }
                }
            }
            return r;
        }

        // the folders a tool of an installation sub-folder is looked for in, in order
        static List<String^>^ SearchDirs(String^ installDir)
        {
            List<String^>^ dirs = gcnew List<String^>();
            String^ ide = IdeDirectory();
            if (ide != nullptr)
            {
                dirs->Add(ide);
                if (!String::IsNullOrEmpty(installDir)) dirs->Add(Path::Combine(ide, installDir));
            }
            String^ root = RideRootDir();
            if (root != nullptr)
            {
                String^ bin = Path::Combine(root, BinDir());
                // RIDE keeps lib beside its programs, under bin: <root>\bin\lib\... as well as <root>\lib\...
                if (!String::IsNullOrEmpty(installDir)) { dirs->Add(Path::Combine(root, installDir)); if (bin != root) dirs->Add(Path::Combine(bin, installDir)); }
                if (bin != root) dirs->Add(bin);
                dirs->Add(root);
            }
            return dirs;
        }

    private:
        static bool HasMarker(String^ dir)
        {
            try
            {
                for each (String^ m in ToolchainFile::Current->Installation->Markers)
                    if (File::Exists(Path::Combine(dir, m))) return true;
            }
            catch (Exception^) { }
            return false;
        }
        static bool HasFile(String^ dir, String^ marker)
        {
            return String::IsNullOrEmpty(marker) || File::Exists(Path::Combine(dir, marker));
        }
    };

    [DataContract(Name = "tool", Namespace = "")]
    public ref class ToolDefinition
    {
    public:
        [DataMember(Name = "id", Order = 0)]            property String^ Id;
        [DataMember(Name = "displayName", Order = 1)]   property String^ DisplayName;
        [DataMember(Name = "category", Order = 2)]      property String^ Category;
        [DataMember(Name = "path", Order = 3)]          property String^ ExecutablePath;
        [DataMember(Name = "arguments", Order = 4)]     property String^ Arguments;
        [DataMember(Name = "includeSwitch", Order = 5)] property String^ IncludeSwitch;
        [DataMember(Name = "defineSwitch", Order = 6)]  property String^ DefineSwitch;
        [DataMember(Name = "librarySwitch", Order = 7)] property String^ LibrarySwitch;
        [DataMember(Name = "errorPattern", Order = 8)]  property String^ ErrorPattern;
        // Extra environment for the process: "NAME=value;NAME2=value" (%VAR% is expanded).
        [DataMember(Name = "environment", Order = 9)]   property String^ Environment;
        // Optional batch file run first in the same shell, e.g. vcvars32.bat for MASM/LINK.
        [DataMember(Name = "setupScript", Order = 10)]  property String^ SetupScript;
        // Output code page of the tool: 0 = OEM (console default), 65001 = UTF-8, ...
        [DataMember(Name = "outputCodePage", Order = 11)] property int OutputCodePage;
        // changed under Tools > Options: the program, arguments or switches no longer follow toolchain.json
        [DataMember(Name = "custom", Order = 12)]       property bool Custom;

        // ---- from toolchain.json (the defaults' values, copied onto a loaded entry by id, not saved)
        property String^ EnvVar;              // the environment variable that names it outright ($C90, $VM6747, $RTS6X ...)
        property List<String^>^ Aliases;      // the other names tried, in order (cc1.exe, shc.exe, ASM6x.exe ...)
        property String^ InstallDir;          // the installation sub-folder it lives in (bin; lib\rts6x-tms6747 for the runtime)
        property List<String^>^ Dirs;         // folders of its own, looked in before PATH (cdb: the Windows SDK's Debuggers\x64)
        property bool OnPath;                // also looked for on PATH (not RIDE's link, which Microsoft's would be taken for)
        property bool Vcvars;                 // Visual Studio's: looked for in its folders (VendorTools.h), as vcvars64.bat would put them on PATH
        property bool TiCompilerDir;          // TI's: looked for in TI's C6000 compiler directory's bin
        property String^ Definition;          // the file's tool (or library) id behind this entry; empty when the file lacks it
        property String^ Operation;           // the operation whose arguments the entry runs
        property String^ About;               // the file's description

        ToolDefinition() { Init(); }

        ToolDefinition(String^ id, String^ name, String^ category, String^ path, String^ args,
                       String^ inc, String^ def, String^ lib)
        {
            Init();
            Id = id; DisplayName = name; Category = category; ExecutablePath = path; Arguments = args;
            IncludeSwitch = inc; DefineSwitch = def; LibrarySwitch = lib;
        }

        // the lookup, chained onto the constructor
        ToolDefinition^ LookedFor(String^ envVar, String^ installDir, bool onPath, IEnumerable<String^>^ aliases)
        {
            EnvVar = envVar == nullptr ? String::Empty : envVar;
            InstallDir = installDir == nullptr ? String::Empty : installDir;
            OnPath = onPath;
            Aliases = gcnew List<String^>();
            if (aliases != nullptr) for each (String^ a in aliases) if (!String::IsNullOrWhiteSpace(a) && !Aliases->Contains(a)) Aliases->Add(a);
            return this;
        }

        void Normalize()
        {
            if (DisplayName == nullptr)    DisplayName = Id;
            if (Category == nullptr)       Category = "Tool";
            if (ExecutablePath == nullptr) ExecutablePath = String::Empty;
            if (Arguments == nullptr)      Arguments = String::Empty;
            if (IncludeSwitch == nullptr)  IncludeSwitch = String::Empty;
            if (DefineSwitch == nullptr)   DefineSwitch = String::Empty;
            if (LibrarySwitch == nullptr)  LibrarySwitch = String::Empty;
            if (ErrorPattern == nullptr)   ErrorPattern = String::Empty;
            if (Environment == nullptr)    Environment = String::Empty;
            if (SetupScript == nullptr)    SetupScript = String::Empty;
            if (OutputCodePage < 0)        OutputCodePage = 0;
            if (Aliases == nullptr)        Aliases = gcnew List<String^>();
            if (EnvVar == nullptr)         EnvVar = String::Empty;
            if (InstallDir == nullptr)     InstallDir = String::Empty;
            if (Dirs == nullptr)           Dirs = gcnew List<String^>();
            if (Definition == nullptr)     Definition = String::Empty;
            if (Operation == nullptr)      Operation = String::Empty;
            if (About == nullptr)          About = String::Empty;
        }

        // the lookup of the default definition with this id (a loaded file carries none)
        void CopyLookupFrom(ToolDefinition^ def)
        {
            if (def == nullptr) return;
            EnvVar = def->EnvVar; InstallDir = def->InstallDir; OnPath = def->OnPath; Vcvars = def->Vcvars; TiCompilerDir = def->TiCompilerDir;
            Dirs = gcnew List<String^>(def->Dirs);
            Aliases = gcnew List<String^>(def->Aliases);
            Definition = def->Definition; Operation = def->Operation; About = def->About;
        }

        // the file's definition: what Tools > Options shows as the program, arguments and switches
        // (the error regex, environment and setup script are the user's own and stay)
        void TakeFrom(ToolDefinition^ def)
        {
            if (def == nullptr) return;
            CopyLookupFrom(def);
            DisplayName = def->DisplayName; Category = def->Category;
            ExecutablePath = def->ExecutablePath; Arguments = def->Arguments;
            IncludeSwitch = def->IncludeSwitch; DefineSwitch = def->DefineSwitch; LibrarySwitch = def->LibrarySwitch;
            OutputCodePage = def->OutputCodePage;
            Custom = false;
        }

        bool SameProgramAs(ToolDefinition^ other)
        {
            return other != nullptr && String::Equals(ExecutablePath, other->ExecutablePath) && String::Equals(Arguments, other->Arguments);
        }

        bool IsConfigured() { return !String::IsNullOrWhiteSpace(ExecutablePath); }
        property bool IsRuntime { bool get() { return String::Equals(Category, "Runtime"); } }
        property bool Defined { bool get() { return !String::IsNullOrEmpty(Definition); } }

        // Where the tool is: a path as given, else the name looked for as RIDE looks - the tool's
        // environment variable, beside the IDE, RIDE's installation, then PATH - the aliases after
        // the name. A runtime library is a file in its installation directory ($RTS6X names the
        // directory). nullptr when nowhere.
        String^ ResolvedPath()
        {
            if (!IsConfigured()) return nullptr;
            String^ x = System::Environment::ExpandEnvironmentVariables(ExecutablePath->Trim()->Trim(L'"'));
            try
            {
                if (Path::IsPathRooted(x) || x->IndexOf(L'\\') >= 0 || x->IndexOf(L'/') >= 0)
                    return File::Exists(x) ? Path::GetFullPath(x) : nullptr;
            }
            catch (ArgumentException^) { return nullptr; }
            String^ found = FindNamed(x);
            if (found == nullptr)
                for each (String^ alias in Aliases)
                {
                    found = FindNamed(alias);
                    if (found != nullptr) break;
                }
            if (found != nullptr) return found;
            // With a setup script (e.g. vcvars32.bat) a bare name like ml.exe is only on
            // PATH after the script ran - let cmd resolve it then.
            if (!String::IsNullOrWhiteSpace(SetupScript)) return x;
            return nullptr;
        }

        // whether the program is a bare name, looked for, rather than a path
        bool IsBareName()
        {
            if (!IsConfigured()) return false;
            String^ x = ExecutablePath->Trim()->Trim(L'"');
            return !(x->IndexOf(L'\\') >= 0 || x->IndexOf(L'/') >= 0 || x->IndexOf(L':') >= 0);
        }

        // the places a name of this tool is looked for, in order, for a "not found" message
        String^ LookedIn()
        {
            StringBuilder^ sb = gcnew StringBuilder();
            if (EnvVar->Length > 0) sb->Append("$")->Append(EnvVar)->Append(", ");
            if (InstallDir->Length > 0) for each (String^ d in IdeSettingsPaths::SearchDirs(InstallDir)) sb->Append(d)->Append(", ");
            if (Vcvars) sb->Append("Visual Studio's folders (")->Append(VisualStudio::Where())->Append("), ");
            if (TiCompilerDir) sb->Append("TI's C6000 compiler directory (")->Append(TiCompiler::Dir() == nullptr ? Str("none found") : TiCompiler::Bin())->Append("), ");
            for each (String^ own in Dirs) sb->Append(System::Environment::ExpandEnvironmentVariables(own))->Append(", ");
            if (OnPath && !IsRuntime) sb->Append("PATH");
            return sb->ToString()->TrimEnd(L' ', L',');
        }

        String^ StatusText()
        {
            if (!IsConfigured()) return "Not configured";
            return ResolvedPath() != nullptr ? Str("OK") : Str("Not found");
        }

    private:
        void Init()
        {
            Id = String::Empty; DisplayName = String::Empty; Category = "Tool"; ExecutablePath = String::Empty; Arguments = String::Empty;
            IncludeSwitch = String::Empty; DefineSwitch = String::Empty; LibrarySwitch = String::Empty; ErrorPattern = String::Empty;
            Environment = String::Empty; SetupScript = String::Empty; OutputCodePage = 0; Custom = false;
            Aliases = gcnew List<String^>(); EnvVar = String::Empty; InstallDir = String::Empty; Dirs = gcnew List<String^>(); OnPath = true; Vcvars = false; TiCompilerDir = false;
            Definition = String::Empty; Operation = String::Empty; About = String::Empty;
        }

        String^ FindNamed(String^ name)
        {
            if (String::IsNullOrWhiteSpace(name)) return nullptr;
            try
            {
                if (EnvVar->Length > 0)
                {
                    String^ env = System::Environment::GetEnvironmentVariable(EnvVar);
                    if (!String::IsNullOrWhiteSpace(env))
                    {
                        String^ e = System::Environment::ExpandEnvironmentVariables(env->Trim()->Trim(L'"'));
                        if (File::Exists(e)) return Path::GetFullPath(e);
                        if (Directory::Exists(e) && File::Exists(Path::Combine(e, name))) return Path::GetFullPath(Path::Combine(e, name));
                    }
                }
                // beside the IDE and in the installation only for a tool that lives there (installDir): Visual
                // Studio's and TI's tools are never taken from RIDE's bin, where RIDE's own link and lnk6x are
                if (InstallDir->Length > 0)
                    for each (String^ dir in IdeSettingsPaths::SearchDirs(InstallDir))
                    {
                        String^ c = Path::Combine(dir, name);
                        if (File::Exists(c)) return Path::GetFullPath(c);
                    }
                if (Vcvars)
                {
                    String^ v = VisualStudio::FindTool(gcnew array<String^>{ name });
                    if (v != nullptr) return v;
                }
                if (TiCompilerDir)
                {
                    String^ bin = TiCompiler::Bin();
                    if (bin != nullptr && File::Exists(Path::Combine(bin, name))) return Path::GetFullPath(Path::Combine(bin, name));
                }
                for each (String^ own in Dirs)
                {
                    String^ c = Path::Combine(System::Environment::ExpandEnvironmentVariables(own), name);
                    if (File::Exists(c)) return Path::GetFullPath(c);
                }
                if (OnPath && !IsRuntime) return PathUtil::FindExecutable(name);
            }
            catch (ArgumentException^) {}
            return nullptr;
        }
    };

    // RIDE's ccs.projects.<dir>: GRIDE's own state for a CCS project, never written into CCS's files
    [DataContract(Name = "ccsProject", Namespace = "")]
    public ref class CcsProjectState
    {
    public:
        [DataMember(Name = "configuration", Order = 0)] property String^ Configuration;
        [DataMember(Name = "programArgs", Order = 1, EmitDefaultValue = false)] property List<String^>^ ProgramArgs;
        [DataMember(Name = "emulateOnly", Order = 2, EmitDefaultValue = false)] property bool EmulateOnly;
        // the compiler word chosen for it (R12; GRIDEd's R13 kept it too): "" or auto for the files' own
        [DataMember(Name = "toolchain", Order = 3, EmitDefaultValue = false)] property String^ Toolchain;
    };

    [DataContract(Name = "settings", Namespace = "")]
    public ref class IdeSettings
    {
    public:
        literal int CurrentFormatVersion = 5;

        [DataMember(Order = 0)]  property int FormatVersion;
        // ---- General
        [DataMember(Order = 1)]  property bool ReopenLastProject;
        [DataMember(Order = 2)]  property bool SaveBeforeBuild;
        [DataMember(Order = 3)]  property String^ LastProject;
        [DataMember(Order = 4)]  property List<String^>^ RecentProjects;
        [DataMember(Order = 5)]  property List<String^>^ RecentFiles;
        // RIDE's installation (its root, or its bin): where the tools are looked for after the IDE's own folder
        [DataMember(Order = 6)]  property String^ RideRoot;
        // RIDE's way after a failed build: the editor on the first error
        [DataMember(Order = 7)]  property bool JumpToFirstError;
        // RIDE's askNative: offer Visual Studio's (or TI's) assembler and linker when the project's own fail
        [DataMember(Order = 8)]  property bool AskNativeTools;
        // a program runs inside the IDE, in the Output pane with an input line (RIDE's window); off: in a console window of its own
        [DataMember(Order = 9)]  property bool RunInIde;
        // ---- RIDE's settings.json, merged (R07)
        [DataMember(Order = 60)] property String^ RideIncludeDir;            // "include": cpp11's headers, against the installation ("" = its include)
        [DataMember(Order = 61)] property String^ RideLibDir;                // "lib": c90's headers ("" = the installation's lib)
        [DataMember(Order = 62)] property String^ Vcvars;                    // "vcvars": a vcvars64.bat whose Visual Studio is taken (never run)
        [DataMember(Order = 63)] property String^ TiCompilerDir;             // "ti": TI's C6000 compiler directory ("" = the newest one found)
        [DataMember(Order = 64)] property String^ TiLibDir;                  // "tilib": a second directory TI's runtime is looked for in
        [DataMember(Order = 65)] property String^ DefaultCompiler;           // "compiler": the toolchain word a new project starts with
        [DataMember(Order = 66)] property List<String^>^ SharedIncludes;     // "includes": given to every compile after the project's
        [DataMember(Order = 67)] property List<String^>^ SharedLibraries;    // "libraries": given to every host link
        [DataMember(Order = 68)] property bool CcsEnabled;                   // "ccs.enabled": CCS projects open
        [DataMember(Order = 69)] property String^ CcsRoot;                   // "ccs.root": the CCS installation ${CG_TOOL_ROOT} is found under
        [DataMember(Order = 70)] property String^ C6747Debugger;             // the C6747's debugger: "" RIDE's choice, else the emulator's or the simulator's tool id
        // "options": the Compiler Options a new project starts with (RIDE's form: debug / release, the differences)
        [DataMember(Order = 71, EmitDefaultValue = false)] property Dictionary<String^, Dictionary<String^, String^>^>^ CompilerOptions;
        // "ccs.projects": a CCS project's state, by its folder
        [DataMember(Order = 72, EmitDefaultValue = false)] property Dictionary<String^, CcsProjectState^>^ CcsProjects;
        // a Visual Studio project's state, the same, by its project file (R08)
        [DataMember(Order = 73, EmitDefaultValue = false)] property Dictionary<String^, CcsProjectState^>^ VsProjects;
        // ---- Editor
        [DataMember(Order = 10)] property String^ EditorFontName;
        [DataMember(Order = 11)] property float EditorFontSize;
        [DataMember(Order = 12)] property int TabSize;
        [DataMember(Order = 13)] property bool InsertSpaces;
        [DataMember(Order = 14)] property bool ShowLineNumbers;
        [DataMember(Order = 15)] property bool AutoIndent;
        [DataMember(Order = 16)] property String^ ColorText;
        [DataMember(Order = 17)] property String^ ColorKeyword;
        [DataMember(Order = 18)] property String^ ColorType;
        [DataMember(Order = 19)] property String^ ColorComment;
        [DataMember(Order = 20)] property String^ ColorString;
        [DataMember(Order = 21)] property String^ ColorNumber;
        [DataMember(Order = 22)] property String^ ColorPreprocessor;
        [DataMember(Order = 23)] property String^ ColorBackground;
        // ---- Languages
        [DataMember(Order = 30)] property String^ ShalimarExtensions;     // e.g. ".shl;.sml"
        [DataMember(Order = 31)] property List<String^>^ ShalimarKeywords;
        // ---- Window layout
        [DataMember(Order = 40)] property int WinX;
        [DataMember(Order = 41)] property int WinY;
        [DataMember(Order = 42)] property int WinW;
        [DataMember(Order = 43)] property int WinH;
        [DataMember(Order = 44)] property bool WinMaximized;
        [DataMember(Order = 45)] property int ExplorerWidth;
        [DataMember(Order = 46)] property int PropertiesWidth;
        [DataMember(Order = 47)] property int BottomHeight;
        // ---- Toolchains
        [DataMember(Order = 50)] property List<ToolDefinition^>^ Tools;

        IdeSettings() { ApplyDefaults(); }

        // the defaults before a file is read, so a member the file lacks keeps its default (a bool would read as false)
        [OnDeserializing]
        void BeforeDeserializing(StreamingContext context) { ApplyDefaults(); }

        // ---------------------------------------------------------------
        void ApplyDefaults()
        {
            FormatVersion = CurrentFormatVersion;
            ReopenLastProject = true;
            SaveBeforeBuild = true;
            LastProject = String::Empty;
            RecentProjects = gcnew List<String^>();
            RecentFiles = gcnew List<String^>();
            RideRoot = String::Empty;
            JumpToFirstError = true;
            AskNativeTools = true;
            RunInIde = true;
            RideIncludeDir = String::Empty;
            RideLibDir = String::Empty;
            Vcvars = String::Empty;
            TiCompilerDir = String::Empty;
            TiLibDir = String::Empty;
            DefaultCompiler = ToolWords::Auto;
            SharedIncludes = gcnew List<String^>();
            SharedLibraries = gcnew List<String^>();
            CcsEnabled = true;
            CcsRoot = String::Empty;
            C6747Debugger = String::Empty;
            CompilerOptions = nullptr;
            CcsProjects = nullptr;
            VsProjects = nullptr;
            EditorFontName = "Consolas";
            EditorFontSize = 10.0f;
            TabSize = 4;
            InsertSpaces = true;
            ShowLineNumbers = true;
            AutoIndent = true;
            ColorText = "#000000";
            ColorKeyword = "#0000FF";
            ColorType = "#2B91AF";
            ColorComment = "#008000";
            ColorString = "#A31515";
            ColorNumber = "#098658";
            ColorPreprocessor = "#808080";
            ColorBackground = "#FFFFFF";
            ShalimarExtensions = ".shl";
            ShalimarKeywords = DefaultShalimarKeywords();
            WinX = 0; WinY = 0; WinW = 0; WinH = 0;
            WinMaximized = false;
            ExplorerWidth = 260;
            PropertiesWidth = 260;
            BottomHeight = 200;
            Tools = DefaultTools();
        }

        // Called after loading: fills anything missing from an older/edited file, and brings every
        // tool that is not marked custom up to toolchain.json's definition.
        void Normalize()
        {
            IdeSettings^ d = gcnew IdeSettings();
            if (FormatVersion < 1)
            {
                // Booleans default to false when absent: restore sensible values.
                ReopenLastProject = true; SaveBeforeBuild = true;
                ShowLineNumbers = true; AutoIndent = true; InsertSpaces = true;
            }
            int oldVersion = FormatVersion;
            FormatVersion = CurrentFormatVersion;
            if (LastProject == nullptr) LastProject = String::Empty;
            if (RecentProjects == nullptr) RecentProjects = gcnew List<String^>();
            if (RecentFiles == nullptr) RecentFiles = gcnew List<String^>();
            if (RideRoot == nullptr) RideRoot = String::Empty;
            if (RideIncludeDir == nullptr) RideIncludeDir = String::Empty;
            if (RideLibDir == nullptr) RideLibDir = String::Empty;
            if (Vcvars == nullptr) Vcvars = String::Empty;
            if (TiCompilerDir == nullptr) TiCompilerDir = String::Empty;
            if (TiLibDir == nullptr) TiLibDir = String::Empty;
            DefaultCompiler = ToolWords::Canonical(DefaultCompiler);
            if (SharedIncludes == nullptr) SharedIncludes = gcnew List<String^>();
            if (SharedLibraries == nullptr) SharedLibraries = gcnew List<String^>();
            if (CcsRoot == nullptr) CcsRoot = String::Empty;
            if (C6747Debugger == nullptr) C6747Debugger = String::Empty;
            if (CompilerOptions != nullptr && CompilerOptions->Count == 0) CompilerOptions = nullptr;
            if (CcsProjects != nullptr && CcsProjects->Count == 0) CcsProjects = nullptr;
            if (VsProjects != nullptr && VsProjects->Count == 0) VsProjects = nullptr;
            PublishPaths();
            if (String::IsNullOrWhiteSpace(EditorFontName)) EditorFontName = d->EditorFontName;
            if (EditorFontSize < 6.0f || EditorFontSize > 48.0f) EditorFontSize = d->EditorFontSize;
            if (TabSize < 1 || TabSize > 16) TabSize = 4;
            if (String::IsNullOrWhiteSpace(ColorText)) ColorText = d->ColorText;
            if (String::IsNullOrWhiteSpace(ColorKeyword)) ColorKeyword = d->ColorKeyword;
            if (String::IsNullOrWhiteSpace(ColorType)) ColorType = d->ColorType;
            if (String::IsNullOrWhiteSpace(ColorComment)) ColorComment = d->ColorComment;
            if (String::IsNullOrWhiteSpace(ColorString)) ColorString = d->ColorString;
            if (String::IsNullOrWhiteSpace(ColorNumber)) ColorNumber = d->ColorNumber;
            if (String::IsNullOrWhiteSpace(ColorPreprocessor)) ColorPreprocessor = d->ColorPreprocessor;
            if (String::IsNullOrWhiteSpace(ColorBackground)) ColorBackground = d->ColorBackground;
            if (String::IsNullOrWhiteSpace(ShalimarExtensions)) ShalimarExtensions = d->ShalimarExtensions;
            if (ShalimarKeywords == nullptr) ShalimarKeywords = d->ShalimarKeywords;
            if (ExplorerWidth < 80) ExplorerWidth = d->ExplorerWidth;
            if (PropertiesWidth < 80) PropertiesWidth = d->PropertiesWidth;
            if (BottomHeight < 60) BottomHeight = d->BottomHeight;
            if (Tools == nullptr) Tools = gcnew List<ToolDefinition^>();
            // drop broken entries, then add any tool this version knows but the file lacks
            Tools->RemoveAll(gcnew Predicate<ToolDefinition^>(&IdeSettings::IsBrokenTool));
            for each (ToolDefinition^ t in Tools) t->Normalize();
            for each (ToolDefinition^ def in d->Tools)
                if (GetTool(def->Id) == nullptr) Tools->Add(def);
            if (oldVersion < 3)
            {
                // v3 introduced {asmflags}: upgrade assembler arguments still at the old default.
                List<ToolDefinition^>^ v2 = LegacyTools();
                for each (ToolDefinition^ t in Tools)
                {
                    ToolDefinition^ def = FindIn(v2, t->Id);
                    if (def == nullptr || !String::Equals(t->Category, def->Category)) continue;
                    String^ oldDefault = def->Arguments->Replace("{asmflags} ", String::Empty)->Replace("{asmflags}", "{flags}");
                    String^ oldDefault2 = def->Arguments->Replace("{asmflags} ", String::Empty);
                    if (def->Arguments->Contains("{asmflags}") &&
                        (String::Equals(t->Arguments, oldDefault) || String::Equals(t->Arguments, oldDefault2)))
                        t->Arguments = def->Arguments;
                }
            }
            if (oldVersion < 5)
            {
                // before version 5 a tool carried no custom mark: one still at an earlier default (version
                // 4's RIDE names, or the names before) follows toolchain.json, one the user changed is custom
                List<ToolDefinition^>^ v4 = LegacyToolsV4();
                List<ToolDefinition^>^ v3 = LegacyTools();
                for each (ToolDefinition^ t in Tools)
                    t->Custom = !(t->SameProgramAs(FindIn(v4, t->Id)) || t->SameProgramAs(FindIn(v3, t->Id)) || t->SameProgramAs(d->GetTool(t->Id)));
            }
            for each (ToolDefinition^ t in Tools)
            {
                ToolDefinition^ def = d->GetTool(t->Id);
                t->CopyLookupFrom(def);
                if (!t->Custom) t->TakeFrom(def);
            }
        }

        ToolDefinition^ GetTool(String^ id) { return FindIn(Tools, id); }

        // the paths the toolchain reads where no settings object reaches (the installation's header
        // directories, Visual Studio's named vcvars64.bat, TI's directories, the CCS root)
        void PublishPaths()
        {
            IdeSettingsPaths::RideRoot = RideRoot;
            IdeSettingsPaths::RideIncludeDir = RideIncludeDir;
            IdeSettingsPaths::RideLibDir = RideLibDir;
            IdeSettingsPaths::CcsRoot = CcsRoot;
            IdeSettingsPaths::TiLibDir = TiLibDir;
            VisualStudio::NamedVcvars = Vcvars;
            TiCompiler::NamedDir = TiCompilerDir;
        }

        // the Compiler Options a new project starts with
        CompilerOptionStore^ OptionStore() { return CompilerOptionStore::FromData(CompilerOptions); }
        void SetOptionStore(CompilerOptionStore^ store) { CompilerOptions = store == nullptr ? nullptr : store->ToData(); }

        // the C6747's debugger in force: RIDE_DEBUG_VM6747 or RIDE_DEBUG_SIM6747 in the environment at start
        // wins (as in RIDE), else the setting; "" is RIDE's choice
        String^ EffectiveC6747Debugger()
        {
            for each (String^ id in gcnew array<String^>{ ToolIds::TmsSimulator, ToolIds::TmsSim })
            {
                ToolEntry^ e = ToolIds::EntryOf(id);
                if (e == nullptr) continue;
                String^ v = System::Environment::GetEnvironmentVariable(String::Concat("RIDE_DEBUG_", e->Name->ToUpperInvariant()));
                if (!String::IsNullOrWhiteSpace(v) && v->Trim() != "0") return id;
            }
            return C6747Debugger;
        }

        // a CCS project's state, by its folder (slashes and case aside)
        static String^ CcsKey(String^ dir)
        {
            try { return Path::GetFullPath(dir)->TrimEnd(L'\\', L'/')->Replace(L'\\', L'/')->ToLowerInvariant(); }
            catch (Exception^) { return dir == nullptr ? String::Empty : dir->ToLowerInvariant(); }
        }
        CcsProjectState^ CcsState(String^ dir) { return StateIn(CcsProjects, dir); }
        // kept only where it differs from a fresh open (Debug, no arguments, not emulate-only)
        void RememberCcsState(String^ dir, String^ configuration, List<String^>^ args, bool emulateOnly)
        {
            CcsProjects = Remember(CcsProjects, dir, configuration, args, emulateOnly, nullptr);
        }
        void RememberCcsState(String^ dir, String^ configuration, List<String^>^ args, bool emulateOnly, String^ toolchain)
        {
            CcsProjects = Remember(CcsProjects, dir, configuration, args, emulateOnly, toolchain);
        }
        // a Visual Studio project's, by its .vcxproj / .vcproj (R08)
        CcsProjectState^ VsState(String^ projectFile) { return StateIn(VsProjects, projectFile); }
        void RememberVsState(String^ projectFile, String^ configuration, List<String^>^ args, bool emulateOnly)
        {
            VsProjects = Remember(VsProjects, projectFile, configuration, args, emulateOnly, nullptr);
        }
        void RememberVsState(String^ projectFile, String^ configuration, List<String^>^ args, bool emulateOnly, String^ toolchain)
        {
            VsProjects = Remember(VsProjects, projectFile, configuration, args, emulateOnly, toolchain);
        }

        static CcsProjectState^ StateIn(Dictionary<String^, CcsProjectState^>^ states, String^ where)
        {
            CcsProjectState^ st;
            return states != nullptr && states->TryGetValue(CcsKey(where), st) ? st : nullptr;
        }
        static Dictionary<String^, CcsProjectState^>^ Remember(Dictionary<String^, CcsProjectState^>^ states, String^ where, String^ configuration, List<String^>^ args, bool emulateOnly, String^ toolchain)
        {
            String^ key = CcsKey(where);
            bool ownCompilers = String::IsNullOrEmpty(toolchain) || String::Equals(ToolWords::Canonical(toolchain), ToolWords::Auto);
            bool plain = (configuration == nullptr || String::Equals(configuration, "Debug")) && (args == nullptr || args->Count == 0) && !emulateOnly && ownCompilers;
            if (plain) { if (states != nullptr) { states->Remove(key); if (states->Count == 0) states = nullptr; } return states; }
            if (states == nullptr) states = gcnew Dictionary<String^, CcsProjectState^>();
            CcsProjectState^ st = gcnew CcsProjectState();
            st->Configuration = configuration;
            st->ProgramArgs = args != nullptr && args->Count > 0 ? gcnew List<String^>(args) : nullptr;
            st->EmulateOnly = emulateOnly;
            st->Toolchain = ownCompilers ? nullptr : toolchain;
            states[key] = st;
            return states;
        }

        List<String^>^ ShalimarExtensionList()
        {
            List<String^>^ r = gcnew List<String^>();
            for each (String^ e in PathUtil::SplitList(ShalimarExtensions))
            {
                String^ x = e->Trim()->ToLowerInvariant();
                if (!x->StartsWith(".")) x = String::Concat(".", x);
                if (x->Length > 1) r->Add(x);
            }
            if (r->Count == 0) r->Add(".shl");
            return r;
        }

        bool IsShalimarExtension(String^ ext)
        {
            if (String::IsNullOrEmpty(ext)) return false;
            return ShalimarExtensionList()->Contains(ext->ToLowerInvariant());
        }

        String^ PrimaryShalimarExtension() { return ShalimarExtensionList()[0]; }

        void AddRecentProject(String^ path) { AddRecent(RecentProjects, path); LastProject = path; }
        void AddRecentFile(String^ path)    { AddRecent(RecentFiles, path); }

        // ---------------------------------------------------------------
        // settings.json beside GRIDE.exe when one is there (a portable layout, as toolchain.user.json beside
        // it is), else %APPDATA%\GRIDE
        static String^ SettingsFolder()
        {
            String^ beside = IdeSettingsPaths::IdeDirectory();
            try { if (beside != nullptr && File::Exists(Path::Combine(beside, "settings.json"))) return beside; }
            catch (Exception^) { }
            return Path::Combine(System::Environment::GetFolderPath(System::Environment::SpecialFolder::ApplicationData), "GRIDE");
        }
        static String^ SettingsPath() { return Path::Combine(SettingsFolder(), "settings.json"); }

        static IdeSettings^ Load(String^% warning)
        {
            warning = nullptr;
            String^ path = SettingsPath();
            IdeSettings^ s = nullptr;
            if (File::Exists(path))
            {
                try
                {
                    s = JsonLoad<IdeSettings>(path);
                }
                catch (Exception^ ex)
                {
                    warning = String::Format("Settings file could not be read and was reset.\n{0}\nA copy was kept as settings.json.bad", ex->Message);
                    try { File::Copy(path, String::Concat(path, ".bad"), true); } catch (Exception^) {}
                    s = nullptr;
                }
            }
            if (s == nullptr) s = gcnew IdeSettings();
            s->Normalize();
            return s;
        }

        bool Save(String^% error)
        {
            error = nullptr;
            try
            {
                JsonSave<IdeSettings>(SettingsPath(), this);
                return true;
            }
            catch (Exception^ ex)
            {
                error = ex->Message;
                return false;
            }
        }

        // ---- the tools as toolchain.json defines them: one flat entry per tool id, bound through
        // the target's roles table (ToolIds::Bindings); an id the file does not cover stays empty
        static List<ToolDefinition^>^ DefaultTools()
        {
            List<ToolDefinition^>^ t = gcnew List<ToolDefinition^>();
            ToolchainFile^ f = ToolchainFile::Current;
            for each (ToolBinding^ b in ToolIds::Bindings())
            {
                ToolDefinition^ d = gcnew ToolDefinition(b->Id, b->Id, b->Category, "", "", "", "", "");
                TargetEntry^ te = b->HasTarget ? ToolIds::TargetEntryOf(b->Kind) : nullptr;
                if (b->IsRunner)
                {
                    // the program itself on that machine: not a tool of the file
                    d->DisplayName = String::Concat(Names::TargetName(b->Kind), " Runner");
                    d->Arguments = "\"{exe}\" {args}";
                    d->OnPath = true;
                }
                else if (b->IsRuntime)
                {
                    LibraryEntry^ lib = te == nullptr ? nullptr : f->Library(te->Runtime);
                    if (lib != nullptr && String::Equals(lib->Kind, "runtime"))
                    {
                        d->DisplayName = lib->Name;
                        d->ExecutablePath = lib->FileFor(false);
                        List<String^>^ aliases = gcnew List<String^>();
                        if (lib->FileFor(true)->Length > 0 && !String::Equals(lib->FileFor(true), d->ExecutablePath)) aliases->Add(lib->FileFor(true));
                        d->LookedFor(lib->DirectoryEnv, lib->Directory, false, aliases);
                        d->Definition = lib->Id; d->About = lib->Description;
                    }
                    else d->OnPath = false;
                }
                else
                {
                    ToolEntry^ e = ToolIds::EntryOf(b->Id);
                    if (e != nullptr)
                    {
                        ToolOperation^ op = e->Operation(b->Operation);
                        d->DisplayName = String::Equals(b->Role, "converter") && op != nullptr && op->Description->Length > 0
                                       ? String::Format("{0} ({1})", e->Name, op->Description) : e->Name;
                        d->ExecutablePath = e->Exe;
                        d->Arguments = op == nullptr ? String::Empty : op->Args;
                        d->IncludeSwitch = e->IncludeFlag;
                        d->DefineSwitch = e->DefineFlag;
                        bool linker = String::Equals(b->Role, "linker") || String::Equals(b->Role, "debugLinker") || String::Equals(b->Role, "nativeLinker");
                        d->LibrarySwitch = linker && te != nullptr ? te->LibraryFlag : String::Empty;
                        d->OutputCodePage = String::Equals(e->OutputEncoding, "utf-8", StringComparison::OrdinalIgnoreCase) ? 65001 : 0;
                        d->LookedFor(e->Lookup->Env, e->Lookup->InstallDir, e->Lookup->OnPath, e->Aliases);
                        d->Vcvars = e->Lookup->Vcvars;
                        d->Dirs = gcnew List<String^>(e->Lookup->Dirs);
                        d->TiCompilerDir = String::Equals(b->Role, "nativeLinker") && te != nullptr && te->Emulated;
                        d->Definition = e->Id; d->Operation = b->Operation; d->About = e->Description;
                    }
                }
                t->Add(d);
            }
            return t;
        }

    private:
        static bool IsBrokenTool(ToolDefinition^ t) { return t == nullptr || String::IsNullOrWhiteSpace(t->Id); }

        static ToolDefinition^ FindIn(List<ToolDefinition^>^ list, String^ id)
        {
            if (list == nullptr) return nullptr;
            for each (ToolDefinition^ t in list)
                if (t != nullptr && String::Equals(t->Id, id, StringComparison::OrdinalIgnoreCase)) return t;
            return nullptr;
        }

        static void AddRecent(List<String^>^ list, String^ path)
        {
            if (list == nullptr || String::IsNullOrWhiteSpace(path)) return;
            for (int i = list->Count - 1; i >= 0; i--)
                if (String::Equals(list[i], path, StringComparison::OrdinalIgnoreCase)) list->RemoveAt(i);
            list->Insert(0, path);
            while (list->Count > 10) list->RemoveAt(list->Count - 1);
        }

        static List<String^>^ DefaultShalimarKeywords()
        {
            // Placeholder set - edit in Tools > Options > Languages to match the Shalimar grammar.
            List<String^>^ k = gcnew List<String^>();
            k->AddRange(gcnew array<String^>{
                "if", "else", "while", "for", "do", "return", "break", "continue",
                "int", "char", "void", "const", "var", "func", "proc", "begin", "end",
                "true", "false" });
            return k;
        }

        // The tool defaults of settings files before version 4 (and, with {asmflags} taken out,
        // before version 3): migration data, so a loaded entry the user never touched can be told
        // from one they changed
        static List<ToolDefinition^>^ LegacyTools()
        {
            List<ToolDefinition^>^ t = gcnew List<ToolDefinition^>();
            String^ compileArgs = "{flags} {includes} {defines} -o \"{out}\" \"{in}\"";
            t->Add(gcnew ToolDefinition(ToolIds::CompilerC90, "C90 Compiler", "Compiler", "", compileArgs, "-I", "-D", ""));
            t->Add(gcnew ToolDefinition(ToolIds::CompilerCpp11, "C++11 Compiler", "Compiler", "", compileArgs, "-I", "-D", ""));
            t->Add(gcnew ToolDefinition(ToolIds::CompilerShalimar, "Shalimar Compiler", "Compiler", "", compileArgs, "-I", "-D", ""));
            t->Add(gcnew ToolDefinition(ToolIds::WinAssembler, "MASM Assembler (ml)", "Windows", "ml.exe",
                "/nologo /c /Zi {asmflags} {includes} /Fo\"{out}\" \"{in}\"", "/I", "/D", ""));
            t->Add(gcnew ToolDefinition(ToolIds::WinLinker, "Microsoft Linker (LINK)", "Windows", "link.exe",
                "/nologo /SUBSYSTEM:CONSOLE {linkflags} /OUT:\"{out}\" {objs} {libs}", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::LinuxAssembler, "Linux Assembler", "Linux", "", "{asmflags} {includes} -o \"{out}\" \"{in}\"", "-I", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::LinuxLinker, "Linux Linker", "Linux", "", "{linkflags} -o \"{out}\" {objs} {libs}", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::LinuxRunner, "Linux Runner", "Linux", "", "\"{exe}\"", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::MacAssembler, "macOS Assembler", "macOS", "", "{asmflags} {includes} -o \"{out}\" \"{in}\"", "-I", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::MacLinker, "macOS Linker", "macOS", "", "{linkflags} -o \"{out}\" {objs} {libs}", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::MacRunner, "macOS Runner", "macOS", "", "\"{exe}\"", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::TmsAssembler, "ASM6x Assembler", "TMS6747", "asm6x.exe",
                "{asmflags} {includes} \"{in}\" \"{out}\"", "-i", "-d", ""));
            t->Add(gcnew ToolDefinition(ToolIds::TmsLinker, "LNK6x Linker", "TMS6747", "lnk6x.exe",
                "{linkflags} -o \"{out}\" -m \"{map}\" {cmdfiles} {objs} {libs}", "", "", "-l"));
            t->Add(gcnew ToolDefinition(ToolIds::TmsRuntime, "RTS6747 Runtime Library (file)", "Runtime", "", "", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::TmsSimulator, "VM6747 Virtual Machine", "TMS6747", "", "\"{exe}\"", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::ConvertCToShalimar, "C to Shalimar Converter", "Converter", "", "\"{in}\" -o \"{out}\"", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::ConvertShalimarToC, "Shalimar to C Converter", "Converter", "", "\"{in}\" -o \"{out}\"", "", "", ""));
            return t;
        }

        // The tool defaults of version 4 (RIDE's names and arguments in the code, before toolchain.json): migration data
        static List<ToolDefinition^>^ LegacyToolsV4()
        {
            List<ToolDefinition^>^ t = gcnew List<ToolDefinition^>();
            String^ cArgs = "-S \"{in}\" -o \"{out}\" -arch {arch} {configflags} {flags} {includes} {defines}";
            t->Add(gcnew ToolDefinition(ToolIds::CompilerC90, "c90 (RIDE's C90 compiler)", "Compiler", "c90.exe", cArgs, "-I", "-D", ""));
            t->Add(gcnew ToolDefinition(ToolIds::CompilerCpp11, "cpp11 (RIDE's C++ compiler)", "Compiler", "cpp11.exe", cArgs, "-I", "-D", ""));
            t->Add(gcnew ToolDefinition(ToolIds::CompilerShalimar, "shalimar (RIDE's Shalimar compiler)", "Compiler", "shalimar.exe",
                "-S \"{in}\" -o \"{out}\" --target={arch} {configflags} {flags}", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::WinAssembler, "masm (RIDE's x86-64 assembler)", "Windows", "masm.exe", "-t x64 \"{in}\" -o \"{out}\"", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::WinLinker, "link (RIDE's Windows linker)", "Windows", "link.exe",
                "/nologo /subsystem:console /out:\"{out}\" {objs} {libs} {crtlibs} {linkflags}", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::LinuxAssembler, "as (Linux)", "Linux", "as", "\"{in}\" -o \"{out}\"", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::LinuxLinker, "cc (Linux linker)", "Linux", "cc", "{objs} -o \"{out}\" {libs} -lm {linkflags}", "", "", "-l"));
            t->Add(gcnew ToolDefinition(ToolIds::LinuxRunner, "Linux Runner", "Linux", "", "\"{exe}\" {args}", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::MacAssembler, "as (macOS)", "macOS", "as", "\"{in}\" -o \"{out}\"", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::MacLinker, "cc (macOS linker)", "macOS", "cc", "{objs} -o \"{out}\" {libs} -lm {linkflags}", "", "", "-l"));
            t->Add(gcnew ToolDefinition(ToolIds::MacRunner, "macOS Runner", "macOS", "", "\"{exe}\" {args}", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::TmsAssembler, "asm6x (RIDE's C6000 assembler)", "TMS6747", "asm6x.exe", "\"{in}\" -o \"{out}\"", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::TmsLinker, "lnk6x (RIDE's C6000 linker)", "TMS6747", "lnk6x.exe",
                "-mv6740 --abi=eabi -i \"{runtimedir}\" {cmdfiles} {linkflags} {objs} {libs} -o \"{out}\"", "", "", "-l"));
            t->Add(gcnew ToolDefinition(ToolIds::TmsRuntime, "RTS6x (RIDE's C runtime for the C6747, a library file)", "Runtime", "rts6x.lib", "", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::TmsSimulator, "vm6747 (RIDE's C6000 emulator, runs the assembly)", "TMS6747", "vm6747.exe", "\"{asmdir}\" {args}", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::TmsSim, "sim6747 (RIDE's C6747 simulator, runs the .out)", "TMS6747", "sim6747.exe", "--run -c \"{exe}\"", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::ConvertCToShalimar, "c2s: C to Shalimar", "Converter", "c2s.exe", "--to-shalimar \"{in}\" -o \"{out}\"", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::ConvertShalimarToC, "c2s: Shalimar to C", "Converter", "c2s.exe", "--to-c \"{in}\" -o \"{out}\"", "", "", ""));
            return t;
        }
    };
}
