// =============================================================================
//  RideToolchain.h - the calling conventions of RIDE's toolchain, as RIDE's
//  src/toolchain.cpp, compile.cpp and editor.cpp have them (ride-port\RIDE-conventions.md):
//  which compiler takes which language, what runs on this machine, the flags of a
//  configuration, how a tool is found and the command lines RIDE shows and runs.
//  Every name, word, flag and library comes from toolchain.json (ToolchainDefinitions);
//  nothing of the toolchain is spelled here. RideTool is the role a compiler plays.
// =============================================================================
#pragma once
#include "../Core/LanguageTarget.h"
#include "../Settings/ToolchainDefinitions.h"
#include "../Settings/CompilerOptions.h"
#include "VisualStudio.h"

namespace Gride { namespace Services {

    using namespace Gride::Core;
    using namespace Gride::Settings;

    // RIDE's ToolchainKind: auto, cc1 (the C compiler), msvc (the host's compiler), shc (the Shalimar
    // compiler), cxx (the host's C++ elsewhere than Windows), cxx1 (the C++ compiler)
    public enum class RideTool { Auto, C90, Cl, Shalimar, HostCxx, Cpp11 };

    public ref class RideToolchain abstract sealed
    {
    public:
        // ---- the definitions behind a role --------------------------------------------------
        static String^ RoleOf(RideTool t)
        {
            switch (t)
            {
            case RideTool::C90:      return "cCompiler";
            case RideTool::Cpp11:    return "cppCompiler";
            case RideTool::Shalimar: return "shalimarCompiler";
            case RideTool::Cl:       return "hostCompiler";
            case RideTool::HostCxx:  return "hostCxx";
            default:                 return String::Empty;
            }
        }

        static RideTool ToolOfRole(String^ role)
        {
            if (role == "cCompiler") return RideTool::C90;
            if (role == "cppCompiler") return RideTool::Cpp11;
            if (role == "shalimarCompiler") return RideTool::Shalimar;
            if (role == "hostCompiler") return RideTool::Cl;
            if (role == "hostCxx") return RideTool::HostCxx;
            return RideTool::Auto;
        }

        static ToolDefinition^ Definition(RideTool t)
        {
            String^ role = RoleOf(t);
            return role->Length == 0 ? nullptr : Defs()->ToolByRole(role);
        }

        static TargetDefinition^ TargetDef(Target t) { return Defs()->TargetOf(t); }

        // ---- words and names ------------------------------------------------------------
        // the word a .pro or settings file spells a compiler with (toolchainWord)
        static String^ Word(RideTool t)
        {
            ToolDefinition^ d = Definition(t);
            if (d == nullptr) return "auto";
            return d->Words->Count > 0 ? d->Words[0] : d->Name;
        }

        // and back (toolchainFrom): the words of the tools, then their program names; the host's C++
        // elsewhere is the host's compiler here (hostCppToolchain)
        static RideTool FromWord(String^ word)
        {
            if (String::IsNullOrWhiteSpace(word)) return RideTool::Auto;
            String^ w = word->Trim();
            if (String::Compare(w, "auto", true) == 0) return RideTool::Auto;
            ToolDefinition^ d = Defs()->ToolByWord(w);
            if (d == nullptr) d = Defs()->ToolByName(w);
            if (d == nullptr) return RideTool::Auto;
            RideTool t = ToolOfRole(d->Role);
            if (t == RideTool::HostCxx)
            {
                TargetDefinition^ host = Defs()->HostTarget();
                if (host != nullptr && host->ToolIdFor("hostCompiler")->Length > 0 && host->ToolIdFor("hostCompiler") != d->Id) return RideTool::Cl;
            }
            return t;
        }

        // the name RIDE prints (toolchainName)
        static String^ Name(RideTool t)
        {
            ToolDefinition^ d = Definition(t);
            return d == nullptr ? "auto" : d->Name;
        }

        // the compilers a project or folder may choose, in the order of the definitions
        static array<RideTool>^ Compilers()
        {
            List<RideTool>^ r = gcnew List<RideTool>();
            for each (RideTool t in gcnew array<RideTool>{ RideTool::C90, RideTool::Cpp11, RideTool::Shalimar, RideTool::Cl })
                if (Definition(t) != nullptr) r->Add(t);
            return r->ToArray();
        }

        // the program names looked for, in order (editor.cpp: Editor::Editor): the exe, then the aliases
        static array<String^>^ Aliases(RideTool t)
        {
            ToolDefinition^ d = Definition(t);
            return d == nullptr ? gcnew array<String^>(0) : d->AllNames();
        }

        // the environment variable that names each program ($C90, $CPP11, $SHALIMAR, $CXX)
        static String^ EnvironmentVariable(RideTool t)
        {
            ToolDefinition^ d = Definition(t);
            return d == nullptr ? String::Empty : d->Lookup->Env;
        }

        // the language a compiler is for, as RIDE spells it and toolchain.json's languages list it
        static String^ LanguageWord(Language l)
        {
            switch (l)
            {
            case Language::C90:      return "C";
            case Language::Cpp11:    return "C++";
            case Language::Shalimar: return "Shalimar";
            case Language::Assembly: return "assembly";
            case Language::Header:   return "a header";
            default:                 return "plain text";
            }
        }

        // ---- targets ----------------------------------------------------------------------
        static String^ Arch(Target t)
        {
            TargetDefinition^ d = TargetDef(t);
            return d == nullptr ? String::Empty : d->Arch;
        }

        static Target TargetOfArch(String^ arch)
        {
            TargetDefinition^ d = Defs()->TargetByArch(arch);
            return d == nullptr ? Target::Windows : TargetInfo::Parse(d->IdeTarget);
        }

        static String^ HostArch()
        {
            TargetDefinition^ d = Defs()->HostTarget();
            return d == nullptr ? String::Empty : d->Arch;
        }
        static bool IsEmulated(Target t)
        {
            TargetDefinition^ d = TargetDef(t);
            return d != nullptr && d->Emulated;
        }
        static bool IsHostArch(Target t) { return !IsEmulated(t); }

        // the emulated target (the C6747), where there is one
        static TargetDefinition^ EmulatedTarget()
        {
            for each (TargetDefinition^ d in Defs()->Targets) if (d->Emulated) return d;
            return nullptr;
        }

        // a compiler that takes a target (c90, cpp11, shalimar); cl and the host's C++ build for the machine they are on
        static bool UsesArch(RideTool t)
        {
            ToolDefinition^ d = Definition(t);
            return d != nullptr && d->TakesArch();
        }

        // the target as each compiler spells it: -arch x to c90 and cpp11, --target=x to shalimar
        static String^ ArchFlag(RideTool t, Target target)
        {
            ToolDefinition^ d = Definition(t);
            if (d == nullptr || !d->TakesArch()) return String::Empty;
            return d->ArchFlag->Replace("$(Arch)", Arch(target));
        }

        // ---- which compiler takes the file ------------------------------------------------
        // the project's or the group's choice wins; else the language chooses (resolve)
        static RideTool Resolve(RideTool chosen, Language lang)
        {
            if (chosen != RideTool::Auto) return chosen;
            ToolDefinition^ d = Defs()->CompilerOf(LanguageWord(lang));
            RideTool t = d == nullptr ? RideTool::Auto : ToolOfRole(d->Role);
            return t == RideTool::Auto ? RideTool::C90 : t;
        }

        static bool CanCompile(RideTool t, Language lang)
        {
            ToolDefinition^ d = Definition(t);
            if (d == nullptr) return false;
            if (lang != Language::C90 && lang != Language::Cpp11 && lang != Language::Shalimar) return false;
            return d->CompilesLanguage(LanguageWord(lang));
        }

        static String^ Refusal(RideTool t, Language lang)
        {
            ToolDefinition^ takes = Defs()->CompilerOf(LanguageWord(lang));
            String^ picks = takes == nullptr ? String::Empty : ", and it picks " + takes->Name;
            if (lang == Language::Shalimar && t != RideTool::Shalimar)
                return Name(t) + " does not compile Shalimar - choose the automatic compiler" + picks;
            if (lang == Language::Other)
                return "nothing to compile: no extension names a language - .c, .cpp or .shl picks the compiler";
            if (lang != Language::C90 && lang != Language::Cpp11 && lang != Language::Shalimar)
                return "nothing to compile: this is " + LanguageWord(lang) + ", not C or C++";
            ToolDefinition^ d = Definition(t);
            if (d != nullptr && !d->CompilesLanguage(LanguageWord(lang)))
                return d->Name + " compiles " + String::Join(" and ", d->Languages) + ", not " + LanguageWord(lang) + " - choose the automatic compiler" + picks;
            return "cannot compile this file";
        }

        // ---- what runs here ---------------------------------------------------------------
        static bool RunsHere(RideTool t, Target target)
        {
            if (!UsesArch(t)) return true;          // cl builds for the machine it is on, whatever the target says (RIDE)
            if (IsEmulated(target)) return true;
            return String::Compare(Arch(target), HostArch(), true) == 0;
        }

        static String^ WhyNotRun(RideTool t, Target target)
        {
            if (RunsHere(t, target)) return String::Empty;
            if (IsEmulated(target))
            {
                List<String^>^ owners = gcnew List<String^>();
                for each (ToolDefinition^ d in Defs()->Tools) if (d->TakesArch() && d->ServesTarget(Arch(target))) owners->Add(d->Name + "'s");
                String^ list = owners->Count == 0 ? "no compiler's" : owners->Count == 1 ? owners[0] :
                               String::Join(", ", owners->GetRange(0, owners->Count - 1)) + " and " + owners[owners->Count - 1];
                return Name(t) + " has no " + Arch(target) + " target - it is " + list;
            }
            return Arch(target) + " only reaches " + AssemblyOnlyFlag() + " here - switch to " + HostArch() + " to run it";
        }

        // the switch that stops the C compiler after the assembly (the first word of its compile template)
        static String^ AssemblyOnlyFlag()
        {
            ToolDefinition^ d = Definition(RideTool::C90);
            String^ args = d == nullptr ? String::Empty : d->OperationArgs("compileToAssembly")->Trim();
            int sp = args->IndexOf(L' ');
            return sp > 0 ? args->Substring(0, sp) : args;
        }

        // ---- debug information ------------------------------------------------------------
        static bool EmitsDebugInfo(RideTool t, Target target)
        {
            if (!UsesArch(t)) return true;
            TargetDefinition^ d = TargetDef(target);
            return d != nullptr && !String::IsNullOrEmpty(d->DebugInfo);
        }

        // a Debug build of c90, cpp11 or shalimar for a target carrying CodeView: clang's assembler
        // and link.exe /DEBUG make the PDB cdb reads, whatever assembler and linker are named
        static bool DebugsWithCodeView(RideTool t, Target target, bool debug)
        {
            TargetDefinition^ d = TargetDef(target);
            return debug && d != nullptr && String::Compare(d->DebugInfo, "codeview", true) == 0 && UsesArch(t);
        }

        // the compiler's asmSyntaxFlags: codeview for a CodeView Debug build, assemblerNamed for a host
        // build with an assembler named; "" otherwise (assemblerFlag)
        static String^ AssemblerFlag(RideTool t, Target target, bool debug, bool assemblerNamed)
        {
            ToolDefinition^ d = Definition(t);
            if (d == nullptr) return String::Empty;
            if (DebugsWithCodeView(t, target, debug)) return d->AsmSyntaxFlag("codeview");
            TargetDefinition^ td = TargetDef(target);
            if (td != nullptr && td->Host && assemblerNamed) return d->AsmSyntaxFlag("assemblerNamed");
            return String::Empty;
        }

        // the configuration's flags: the Compiler Options of the store (the project's, or the defaults
        // when none is given) for a compiler that lists options; else RIDE's flags before the dialog
        // existed (configFlags), the exception for the arch when it has one, without -g where the
        // target carries no line table
        static String^ ConfigFlags(RideTool t, bool debug, Target target) { return ConfigFlags(t, debug, target, nullptr); }

        static String^ ConfigFlags(RideTool t, bool debug, Target target, CompilerOptionStore^ store)
        {
            ToolDefinition^ d = Definition(t);
            if (d == nullptr) return String::Empty;
            if (d->Options->Count > 0 && Defs()->CompilerOptions->Count > 0)
                return OptionFlags(store == nullptr ? gcnew CompilerOptionStore() : store, d, debug ? "Debug" : "Release", target);
            String^ flags = d->ConfigFlagsFor(debug ? "Debug" : "Release", Arch(target));
            if (debug && !EmitsDebugInfo(t, target))
            {
                List<String^>^ kept = gcnew List<String^>();
                for each (String^ f in flags->Split(gcnew array<wchar_t>{ L' ' }, StringSplitOptions::RemoveEmptyEntries))
                    if (f != "-g") kept->Add(f);
                flags = String::Join(" ", kept);
            }
            return flags;
        }

        // ---- RIDE's Compiler Options (options::flags, available, environment) ------------------------
        // whether an option means anything for a target, and why not when it does not: the file's
        // needs (lineTable: the compiler writes one for the target; emulated / notEmulated) and its sentence
        static bool OptionAvailable(String^ id, Target target, String^% why)
        {
            why = String::Empty;
            CompilerOptionDefinition^ o = Defs()->CompilerOption(id);
            if (o == nullptr || String::IsNullOrEmpty(o->Needs)) return true;
            bool ok;
            if (String::Compare(o->Needs, "lineTable", true) == 0)
            {
                ToolDefinition^ tool = Defs()->ToolOfOption(id);
                ok = tool == nullptr ? EmitsDebugInfo(RideTool::C90, target) : EmitsDebugInfo(ToolOfRole(tool->Role), target);
            }
            else if (String::Compare(o->Needs, "emulated", true) == 0) ok = IsEmulated(target);
            else if (String::Compare(o->Needs, "notEmulated", true) == 0) ok = !IsEmulated(target);
            else ok = true;
            if (!ok) why = o->Unavailable->Replace("$(Arch)", Arch(target));
            return ok;
        }

        // the flags a compiler is given for a configuration and target, from a store, in the order
        // the tool's options list them: a check's flagOn / flagOff, a choice's flag unless it is the
        // silent one, a text list's flagEach per entry (quoted when it holds a space), an option that
        // does not apply to the target writing nothing
        static String^ OptionFlags(CompilerOptionStore^ store, ToolDefinition^ tool, String^ config, Target target)
        {
            StringBuilder^ sb = gcnew StringBuilder();
            if (store == nullptr || tool == nullptr) return String::Empty;
            String^ why;
            for each (String^ id in tool->Options)
            {
                CompilerOptionDefinition^ o = Defs()->CompilerOption(id);
                if (o == nullptr || o->EnvironmentVariable->Length > 0) continue;
                String^ value = store->Value(config, id);
                if (o->IsCheck())
                {
                    if (value == "1" && o->FlagOn->Length > 0 && OptionAvailable(id, target, why)) Append(sb, o->FlagOn);
                    else if (value == "0" && o->FlagOff->Length > 0 && OptionAvailable(id, target, why)) Append(sb, o->FlagOff);
                }
                else if (o->IsChoice())
                {
                    if (value != o->SilentChoice() && value->Length > 0 && o->Flag->Length > 0 && OptionAvailable(id, target, why))
                        Append(sb, o->Flag->Replace("$(Value)", value));
                }
                else if (o->FlagEach->Length > 0 && OptionAvailable(id, target, why))
                {
                    for each (String^ item in CompilerOptionStore::Entries(value))
                        Append(sb, o->FlagEach->Replace("$(Item)", item->IndexOfAny(gcnew array<wchar_t>{ L' ', L'\t', L'"' }) < 0 ? item : "\"" + item + "\""));
                }
            }
            return sb->ToString();
        }

        // the environment a compile runs under: NAME=value for every option of the tool that names a
        // variable (RIDE's CPP11_DECLINES: 1 when asked for, empty otherwise)
        static List<String^>^ OptionEnvironment(CompilerOptionStore^ store, ToolDefinition^ tool, String^ config)
        {
            List<String^>^ r = gcnew List<String^>();
            if (tool == nullptr) return r;
            for each (String^ id in tool->Options)
            {
                CompilerOptionDefinition^ o = Defs()->CompilerOption(id);
                if (o == nullptr || o->EnvironmentVariable->Length == 0) continue;
                String^ value = store == nullptr ? o->DefaultFor(config) : store->Value(config, id);
                r->Add(o->EnvironmentVariable + "=" + (o->IsCheck() ? (value == "1" ? "1" : String::Empty) : value));
            }
            return r;
        }

        // the switch a compiler takes for a language (cl's /TC, /TP ...)
        static String^ LanguageFlags(RideTool t, Language lang)
        {
            ToolDefinition^ d = Definition(t);
            return d == nullptr ? String::Empty : d->LanguageFlag(LanguageWord(lang));
        }

        static String^ IncludeFlagOf(RideTool t)
        {
            ToolDefinition^ d = Definition(t);
            return d == nullptr ? String::Empty : d->IncludeFlag;
        }
        static String^ DefineFlagOf(RideTool t)
        {
            ToolDefinition^ d = Definition(t);
            return d == nullptr ? String::Empty : d->DefineFlag;
        }

        // the header directories a compiler is given: the project's, then the installation's it reads
        // (installIncludeDirs: lib for c90, include for cpp11); a compiler with no include flag takes none (includeFlags)
        static String^ IncludeFlags(RideTool t, IEnumerable<String^>^ projectDirs, String^ installInclude, String^ installLib)
        {
            ToolDefinition^ d = Definition(t);
            if (d == nullptr || String::IsNullOrEmpty(d->IncludeFlag)) return String::Empty;
            String^ flag = d->IncludeFlag;
            StringBuilder^ sb = gcnew StringBuilder();
            if (projectDirs != nullptr)
                for each (String^ dir in projectDirs)
                    if (!String::IsNullOrWhiteSpace(dir)) Append(sb, flag + Quote(dir));
            InstallationDefinition^ inst = Defs()->Installation;
            for each (String^ which in d->InstallIncludeDirs)
            {
                String^ dir = nullptr;
                if (String::Compare(which, inst->LibDir, true) == 0 || String::Compare(which, "lib", true) == 0) dir = installLib;
                else if (String::Compare(which, inst->IncludeDir, true) == 0 || String::Compare(which, "include", true) == 0) dir = installInclude;
                if (!String::IsNullOrEmpty(dir)) Append(sb, flag + Quote(dir));
            }
            return sb->ToString();
        }

        // ---- finding the tools --------------------------------------------------------------
        static String^ Quote(String^ s) { return "\"" + (s == nullptr ? String::Empty : s) + "\""; }

        // the folder holding the IDE (GRIDE.exe), or null outside an application
        static String^ IdeDirectory()
        {
            try { return Path::GetDirectoryName(System::Reflection::Assembly::GetEntryAssembly()->Location); }
            catch (Exception^) { return nullptr; }
        }

        // the installation's bin folder: the root given (settings), else the installation's default
        // roots; a folder counts when one of the markers is in it or in its binDir
        static String^ RideBin(String^ rideRoot)
        {
            InstallationDefinition^ inst = Defs()->Installation;
            List<String^>^ roots = gcnew List<String^>();
            if (!String::IsNullOrWhiteSpace(rideRoot)) roots->Add(Environment::ExpandEnvironmentVariables(rideRoot->Trim()));
            for each (String^ r in inst->DefaultRoots)
            {
                String^ x = Environment::ExpandEnvironmentVariables(r);
                if (!x->Contains("%") && !roots->Contains(x)) roots->Add(x);
            }
            for each (String^ r in roots)
            {
                try
                {
                    if (HasMarker(r, inst->Markers)) return r;
                    String^ bin = String::IsNullOrEmpty(inst->BinDir) ? r : Path::Combine(r, inst->BinDir);
                    if (HasMarker(bin, inst->Markers)) return bin;
                }
                catch (Exception^) { }
            }
            return nullptr;
        }

        // the installation's root: the folder above its bin (settings.json's "include" and "lib" are
        // relative to it), or the bin itself when the installation keeps no bin folder
        static String^ InstallRoot(String^ rideRoot)
        {
            String^ bin = RideBin(rideRoot);
            if (bin == nullptr) return nullptr;
            InstallationDefinition^ inst = Defs()->Installation;
            if (String::IsNullOrEmpty(inst->BinDir)) return bin;
            String^ up = Path::GetDirectoryName(bin);
            return up != nullptr && String::Compare(Path::GetFileName(bin), inst->BinDir, true) == 0 ? up : bin;
        }

        // one of the installation's header directories (RIDE's includeDir / libDir): the one named in
        // the settings, made absolute against the installation, else the directory of that name under
        // it; null when neither is there
        static String^ InstallDir(String^ rideRoot, String^ which, String^ named)
        {
            String^ root = InstallRoot(rideRoot);
            try
            {
                if (!String::IsNullOrWhiteSpace(named))
                {
                    String^ n = Environment::ExpandEnvironmentVariables(named->Trim());
                    String^ dir = Path::IsPathRooted(n) ? n : root == nullptr ? n : Path::Combine(root, n);
                    return Directory::Exists(dir) ? Path::GetFullPath(dir) : nullptr;
                }
                if (root == nullptr || String::IsNullOrEmpty(which)) return nullptr;
                String^ dir = Path::Combine(root, which);
                return Directory::Exists(dir) ? dir : nullptr;
            }
            catch (Exception^) { return nullptr; }
        }

        // the installation's header directories a compiler is given after the project's (installIncludeDirs:
        // lib for c90, include for cpp11), the settings' own names standing in for the installation's
        static List<String^>^ InstallIncludeDirs(RideTool t, String^ rideRoot, String^ namedInclude, String^ namedLib)
        {
            List<String^>^ r = gcnew List<String^>();
            ToolDefinition^ d = Definition(t);
            if (d == nullptr || String::IsNullOrEmpty(d->IncludeFlag)) return r;
            InstallationDefinition^ inst = Defs()->Installation;
            for each (String^ which in d->InstallIncludeDirs)
            {
                String^ dir = nullptr;
                if (String::Compare(which, inst->LibDir, true) == 0 || String::Compare(which, "lib", true) == 0) dir = InstallDir(rideRoot, inst->LibDir, namedLib);
                else if (String::Compare(which, inst->IncludeDir, true) == 0 || String::Compare(which, "include", true) == 0) dir = InstallDir(rideRoot, inst->IncludeDir, namedInclude);
                if (dir != nullptr && !r->Contains(dir)) r->Add(dir);
            }
            return r;
        }

        // ---- TI's C6000 compiler directory (RIDE's settings ti / tilib, detectedTi) --------------------
        // the directory named under Tools > Options (the one with bin\lnk6x), and a second library directory
        static property String^ TiCompilerDir;
        static property String^ TiLibDir;

        // TI's compiler directory in force: the one named when it is there, else the newest one CCS's
        // installer left under the roots the file names (detectedTi); null when none
        static String^ TiDir()
        {
            if (!String::IsNullOrWhiteSpace(TiCompilerDir))
            {
                String^ d = Environment::ExpandEnvironmentVariables(TiCompilerDir->Trim());
                if (Directory::Exists(d)) return Path::GetFullPath(d);
            }
            return DetectedTiDir();
        }
        static bool TiNamed()
        {
            if (String::IsNullOrWhiteSpace(TiCompilerDir)) return false;
            String^ d = Environment::ExpandEnvironmentVariables(TiCompilerDir->Trim());
            return Directory::Exists(d);
        }

        // the newest compiler directory with the native linker in its bin, under each root and each
        // layout the file names (C:\ti\ti-cgt-c6000_*, C:\ti\ccs*\tools\compiler\...); versions read as numbers
        static String^ DetectedTiDir()
        {
            CcsDefinition^ cd = Defs()->Ccs;
            TargetDefinition^ emu = EmulatedTarget();
            ToolDefinition^ ld = emu == nullptr ? nullptr : Defs()->Tool(emu->ToolIdFor("nativeLinker"));
            if (ld == nullptr || cd->TiRoots->Count == 0) return nullptr;
            String^ found = nullptr;
            List<String^>^ candidates = gcnew List<String^>();
            for each (String^ r in cd->TiRoots)
            {
                String^ root = Environment::ExpandEnvironmentVariables(r);
                if (root->Contains("%") || !Directory::Exists(root)) continue;
                for each (String^ under in cd->TiUnder) CompilersUnder(root, under, cd, candidates);
            }
            for each (String^ c in candidates)
            {
                bool hasLinker = false;
                for each (String^ n in ld->AllNames()) if (File::Exists(Path::Combine(Path::Combine(c, cd->TiBin), n))) { hasLinker = true; break; }
                if (!hasLinker) continue;
                if (found == nullptr || Newer(VersionOf(Path::GetFileName(c)), VersionOf(Path::GetFileName(found)))) found = c;
            }
            return found;
        }

        // the installation's bin folder as a name, when none is found: the first default root
        static String^ DefaultRideBin()
        {
            InstallationDefinition^ inst = Defs()->Installation;
            if (inst->DefaultRoots->Count == 0) return String::Empty;
            String^ r = Environment::ExpandEnvironmentVariables(inst->DefaultRoots[0]);
            return String::IsNullOrEmpty(inst->BinDir) ? r : Path::Combine(r, inst->BinDir);
        }

        // Where a program is: its environment variable, the tool folder, beside the IDE, the installation's
        // bin, then PATH (editor.cpp, path::besideProgram, path::onPath). Null when nowhere.
        static String^ Find(array<String^>^ names, String^ envVar, String^ toolDir, String^ rideRoot)
        {
            return Find(names, envVar, toolDir, rideRoot, true);
        }

        static String^ Find(array<String^>^ names, String^ envVar, String^ toolDir, String^ rideRoot, bool onPath)
        {
            if (!String::IsNullOrEmpty(envVar))
            {
                String^ named = Environment::GetEnvironmentVariable(envVar);
                if (!String::IsNullOrWhiteSpace(named) && File::Exists(named)) return Path::GetFullPath(named);
            }
            for each (String^ d in SearchDirs(toolDir, rideRoot))
                for each (String^ n in names)
                {
                    try
                    {
                        String^ full = Path::Combine(d, n);
                        if (File::Exists(full)) return Path::GetFullPath(full);
                    }
                    catch (Exception^) { }
                }
            if (!onPath) return nullptr;
            String^ path = Environment::GetEnvironmentVariable("PATH");
            if (path != nullptr)
                for each (String^ d in path->Split(L';'))
                {
                    if (d->Length == 0 || d == ".") continue;
                    for each (String^ n in names)
                    {
                        try
                        {
                            String^ full = Path::Combine(d->Trim(L'"'), n);
                            if (File::Exists(full)) return Path::GetFullPath(full);
                        }
                        catch (Exception^) { }
                    }
                }
            return nullptr;
        }

        static String^ Find(String^ name, String^ envVar, String^ toolDir, String^ rideRoot)
        {
            List<String^>^ names = gcnew List<String^>();
            names->Add(name);
            if (!name->EndsWith(".exe", StringComparison::OrdinalIgnoreCase)) names->Add(name + ".exe");
            return Find(names->ToArray(), envVar, toolDir, rideRoot);
        }

        // a tool by its definition, as its lookup says: the environment variable; the tool folder,
        // beside the IDE and the installation's bin when it is the installation's (installDir named);
        // Visual Studio's folders when it is reached through vcvars; PATH when the lookup allows it
        static String^ FindTool(ToolDefinition^ d, String^ toolDir, String^ rideRoot)
        {
            if (d == nullptr) return nullptr;
            array<String^>^ names = d->AllNames();
            if (!String::IsNullOrEmpty(d->Lookup->Env))
            {
                String^ named = Environment::GetEnvironmentVariable(d->Lookup->Env);
                if (!String::IsNullOrWhiteSpace(named) && File::Exists(named)) return Path::GetFullPath(named);
            }
            List<String^>^ dirs = gcnew List<String^>();
            if (!String::IsNullOrEmpty(d->Lookup->InstallDir)) dirs->AddRange(SearchDirs(toolDir, rideRoot));
            if (d->Lookup->Vcvars)
            {
                VsInstallation^ vs = VisualStudio::Find();
                if (vs != nullptr) dirs->AddRange(vs->BinDirs());
            }
            String^ tiBin = TiBinFor(d);
            if (tiBin != nullptr) dirs->Add(tiBin);
            for each (String^ own in d->Lookup->Dirs) dirs->Add(Environment::ExpandEnvironmentVariables(own));
            for each (String^ dir in dirs)
                for each (String^ n in names)
                {
                    try
                    {
                        String^ full = Path::Combine(dir, n);
                        if (File::Exists(full)) return Path::GetFullPath(full);
                    }
                    catch (Exception^) { }
                }
            if (!d->Lookup->OnPath) return nullptr;
            return Find(names, String::Empty, String::Empty, nullptr, true);
        }

        // the places a tool is looked for, for a "not found" message
        static String^ SearchPlaces(ToolDefinition^ d, String^ toolDir, String^ rideRoot)
        {
            List<String^>^ places = gcnew List<String^>();
            String^ envVar = d == nullptr ? String::Empty : d->Lookup->Env;
            if (!String::IsNullOrEmpty(envVar))
            {
                String^ v = Environment::GetEnvironmentVariable(envVar);
                places->Add("$" + envVar + (String::IsNullOrWhiteSpace(v) ? " (not set)" : " = " + v));
            }
            if (d == nullptr || !String::IsNullOrEmpty(d->Lookup->InstallDir))
            {
                if (!String::IsNullOrWhiteSpace(toolDir)) places->Add("the tool folder " + Environment::ExpandEnvironmentVariables(toolDir));
                String^ ide = IdeDirectory();
                if (ide != nullptr) places->Add("beside GRIDE.exe (" + ide + ")");
                String^ bin = RideBin(rideRoot);
                places->Add(bin != nullptr ? "the installation's " + bin : "the installation (not found: " + DefaultRideBin() + ")");
            }
            if (d != nullptr && d->Lookup->Vcvars)
            {
                VsInstallation^ vs = VisualStudio::Find();
                places->Add(vs == nullptr ? "Visual Studio (none found)" : "Visual Studio's " + String::Join(", ", vs->BinDirs()));
            }
            if (d != nullptr && IsTiTool(d))
            {
                String^ tiBin = TiBinFor(d);
                places->Add(tiBin != nullptr ? "TI's compiler directory's " + tiBin
                                             : "TI's compiler directory (none named under Tools > Options > Projects, none found under " + String::Join(", ", Defs()->Ccs->TiRoots) + ")");
            }
            if (d != nullptr)
                for each (String^ own in d->Lookup->Dirs) places->Add(Environment::ExpandEnvironmentVariables(own));
            if (d == nullptr || d->Lookup->OnPath) places->Add("PATH");
            return String::Join(", ", places);
        }

        // TI's own tools (the vendor's for the emulated target) live in TI's compiler directory's bin
        static bool IsTiTool(ToolDefinition^ d)
        {
            TargetDefinition^ emu = EmulatedTarget();
            return d != nullptr && emu != nullptr && (d->Role == "nativeLinker" || d->Role == "nativeAssembler") && d->ServesTarget(emu->Arch);
        }
        static String^ TiBinFor(ToolDefinition^ d)
        {
            if (!IsTiTool(d)) return nullptr;
            String^ ti = TiDir();
            if (ti == nullptr) return nullptr;
            String^ bin = String::IsNullOrEmpty(Defs()->Ccs->TiBin) ? ti : Path::Combine(ti, Defs()->Ccs->TiBin);
            return Directory::Exists(bin) ? bin : nullptr;
        }

        // a library's directory: its environment variable, then <dir>\<directory> under the tool folder,
        // beside the IDE and the installation, when the marker (or the directory) is there
        static String^ FindLibraryDir(LibraryDefinition^ lib, String^ toolDir, String^ rideRoot)
        {
            if (lib == nullptr || String::IsNullOrEmpty(lib->Directory)) return nullptr;
            if (!String::IsNullOrEmpty(lib->DirectoryEnv))
            {
                String^ env = Environment::GetEnvironmentVariable(lib->DirectoryEnv);
                if (!String::IsNullOrWhiteSpace(env) && Directory::Exists(env) && HasFile(env, lib->Marker)) return Path::GetFullPath(env);
            }
            for each (String^ d in SearchDirs(toolDir, rideRoot))
            {
                try
                {
                    String^ dir = Path::Combine(d, lib->Directory);
                    if (Directory::Exists(dir) && HasFile(dir, lib->Marker)) return dir;
                }
                catch (Exception^) { }
            }
            return nullptr;
        }

        // the C runtime's directory of the emulated target (RTS6x): $RTS6X, else lib\rts6x-tms6747 with rts6x.lib in it
        static String^ FindRuntimeDir(String^ toolDir, String^ rideRoot)
        {
            TargetDefinition^ t = EmulatedTarget();
            return t == nullptr ? nullptr : FindLibraryDir(Defs()->Library(t->Runtime), toolDir, rideRoot);
        }

        // the Shalimar runtime's assembly for the emulator: its environment variable, else the
        // configuration's directory, the Release one standing in for a Debug one that is not there
        static String^ FindShalimarRuntimeDir(String^ toolDir, String^ rideRoot, bool debug)
        {
            TargetDefinition^ t = EmulatedTarget();
            LibraryDefinition^ lib = t == nullptr ? nullptr : Defs()->LibraryByKind(t->Arch, "shalimarRuntime");
            if (lib == nullptr) return nullptr;
            String^ envName = lib->AssemblyDirectoryEnvFor(debug);
            if (!String::IsNullOrEmpty(envName))
            {
                String^ env = Environment::GetEnvironmentVariable(envName);
                if (!String::IsNullOrWhiteSpace(env) && Directory::Exists(env)) return Path::GetFullPath(env);
            }
            for each (String^ d in SearchDirs(toolDir, rideRoot))
            {
                try
                {
                    for each (bool which in gcnew array<bool>{ debug, false })
                    {
                        String^ rel = lib->AssemblyDirectoryFor(which);
                        if (rel->Length == 0) continue;
                        String^ dir = Path::Combine(d, rel);
                        if (Directory::Exists(dir)) return dir;
                    }
                }
                catch (Exception^) { }
            }
            return nullptr;
        }

        // ---- the lines RIDE shows ---------------------------------------------------------------
        // "$ c90 -S src.c -arch x86_64-windows -g -D_DEBUG=1" (shownCommand) for a compile to assembly
        static String^ ShownCompileCommand(RideTool t, String^ program, String^ source, Language lang, Target target,
                                           String^ configFlags, String^ assemblerFlag, String^ includeFlags)
        {
            ToolDefinition^ d = Definition(t);
            if (d == nullptr) return program + " " + source;
            OperationDefinition^ op = d->Operation("compileToAssembly");
            String^ shown = op == nullptr ? String::Empty : (op->Shown->Length > 0 ? op->Shown : op->Args);
            return Tidy(program + " " + Fill(shown, t, source, lang, target, configFlags, assemblerFlag, includeFlags));
        }

        // "$ c90 src.c -o ride-run ..." / the tms6747 chain (shownProgramCommand)
        static String^ ShownRunCommand(RideTool t, String^ program, String^ source, Language lang, Target target,
                                       String^ configFlags, String^ assemblerFlag, String^ includeFlags, bool emulate)
        {
            TargetDefinition^ td = TargetDef(target);
            ToolDefinition^ d = Definition(t);
            if (td != nullptr && td->Emulated && UsesArch(t))
            {
                String^ chain = emulate && td->ShownEmulate->Length > 0 ? td->ShownEmulate : td->ShownRun;
                chain = chain->Replace("$(Compiler)", program)
                             ->Replace("$(Assembler)", ToolName(td, "assembler"))->Replace("$(Linker)", ToolName(td, "linker"))
                             ->Replace("$(Simulator)", ToolName(td, "simulator"))->Replace("$(Emulator)", ToolName(td, "emulator"));
                String^ shmrt = String::Empty;
                if (t == RideTool::Shalimar)
                {
                    LibraryDefinition^ lib = Defs()->LibraryByKind(td->Arch, "shalimarRuntime");
                    if (lib != nullptr) shmrt = lib->AssemblyDirectoryFor(false)->Replace(L'\\', L'/');
                }
                chain = chain->Replace("$(ShalimarRuntimeDir)", shmrt);
                return Tidy(Fill(chain, t, source, lang, target, configFlags, assemblerFlag, includeFlags));
            }
            OperationDefinition^ op = d == nullptr ? nullptr : d->Operation("compileAndLink");
            String^ shown = op != nullptr && op->Shown->Length > 0 ? op->Shown : (td != nullptr ? td->ShownRun->Replace("$(Compiler)", String::Empty) : String::Empty);
            return Tidy(program + " " + Fill(shown, t, source, lang, target, configFlags, assemblerFlag, includeFlags));
        }

        // the command that runs a built program (launchCommand): the program and its arguments, or the
        // emulator's launch operation - the program, the Shalimar runtime directory, then the arguments
        static String^ LaunchCommand(String^ program, bool shalimar, IEnumerable<String^>^ args, String^ emulator, String^ shalimarRuntimeDir)
        {
            if (IsEmulatedProgram(program)) return EmulateCommand(emulator, program, shalimar, args, shalimarRuntimeDir);
            StringBuilder^ quoted = gcnew StringBuilder();
            if (args != nullptr) for each (String^ a in args) Append(quoted, Quote(a));
            return Tidy(Quote(program) + " " + quoted->ToString());
        }

        // the emulator on a .s, a directory of them or a .vm: its launch operation - the program, the
        // Shalimar runtime's assembly directory for a Shalimar program, then the arguments, in the order
        // the emulator reads them
        static String^ EmulateCommand(String^ emulator, String^ program, bool shalimar, IEnumerable<String^>^ args, String^ shalimarRuntimeDir)
        {
            StringBuilder^ quoted = gcnew StringBuilder();
            if (args != nullptr) for each (String^ a in args) Append(quoted, Quote(a));
            TargetDefinition^ td = EmulatedTarget();
            ToolDefinition^ vm = td == nullptr ? nullptr : Defs()->Tool(td->ToolIdFor("emulator"));
            String^ templ = vm == nullptr || vm->Operation("launch") == nullptr ? "\"$(Program)\" $(ShalimarRuntimeDir) $(Arguments)" : vm->OperationArgs("launch");
            String^ line = templ->Replace("$(Program)", program)
                                ->Replace("$(ShalimarRuntimeDir)", shalimar && !String::IsNullOrEmpty(shalimarRuntimeDir) ? Quote(shalimarRuntimeDir) : String::Empty)
                                ->Replace("$(Arguments)", quoted->ToString());
            return Tidy(Quote(emulator) + " " + line);
        }

        // RIDE's simulationMissing: why the simulator cannot run the program, or ""
        static String^ SimulationMissing(String^ simulator, String^ out)
        {
            TargetDefinition^ td = EmulatedTarget();
            String^ sim = ToolName(td, "simulator");
            if (sim->Length == 0) sim = "the simulator";
            if (String::IsNullOrEmpty(simulator) || !File::Exists(simulator))
                return "no " + sim + " beside GRIDE - the C6747 simulator ships with it from 5.0";
            if (!File::Exists(out))
            {
                LibraryDefinition^ rts = td == nullptr ? nullptr : Defs()->Library(td->Runtime);
                return "no " + Path::GetFileName(out) + ": a " + (td == nullptr ? "tms6747" : td->Arch) + " build links one only with " +
                       ToolName(td, "assembler") + " and " + ToolName(td, "linker") + " beside GRIDE and a runtime - " +
                       (rts == nullptr ? "RTS6x" : rts->Name) + " in " + (rts == nullptr ? "lib/rts6x-tms6747" : rts->Directory->Replace(L'\\', L'/')) +
                       " beside it, or TI's named under Tools";
            }
            return String::Empty;
        }

        // a .s or a .vm is the emulator's program
        static bool IsEmulatedProgram(String^ program)
        {
            TargetDefinition^ td = EmulatedTarget();
            if (td == nullptr || String::IsNullOrEmpty(program)) return false;
            String^ leaf = Path::GetFileName(program->TrimEnd(L'\\', L'/'));
            return (td->EmulatedProgramExtension->Length > 0 && leaf->EndsWith(td->EmulatedProgramExtension, StringComparison::OrdinalIgnoreCase)) ||
                   (td->AssemblyExtension->Length > 0 && leaf->EndsWith(td->AssemblyExtension, StringComparison::OrdinalIgnoreCase));
        }

        // sim6747 --run -c prog.out (simulateCommand)
        static String^ SimulateCommand(String^ simulator, String^ out)
        {
            TargetDefinition^ td = EmulatedTarget();
            ToolDefinition^ sim = td == nullptr ? nullptr : Defs()->Tool(td->ToolIdFor("simulator"));
            String^ args = sim == nullptr ? "\"$(Program)\"" : sim->OperationArgs("simulate");
            return Tidy(Quote(simulator) + " " + args->Replace("$(Program)", out));
        }

        // the .out a tms6747 build linked beside <program>.vm / .s / .exe (tiProgramOf)
        static String^ TiProgramOf(String^ program)
        {
            TargetDefinition^ td = EmulatedTarget();
            TargetDefinition^ host = Defs()->HostTarget();
            String^ stem = program->TrimEnd(L'\\', L'/');
            List<String^>^ exts = gcnew List<String^>();
            if (td != nullptr) { exts->Add(td->EmulatedProgramExtension); exts->Add(td->AssemblyExtension); exts->Add(td->ProgramExtension); }
            if (host != nullptr) exts->Add(host->ProgramExtension);
            for each (String^ e in exts)
                if (e->Length > 0 && stem->EndsWith(e, StringComparison::OrdinalIgnoreCase)) { stem = stem->Substring(0, stem->Length - e->Length); break; }
            return stem + (td == nullptr ? ".out" : td->ProgramExtension);
        }

        // "$ sim6747 --run prog.out" / "$ vm6747 prog.vm" (runnerLine)
        static String^ RunnerLine(bool simulator, String^ program)
        {
            TargetDefinition^ td = EmulatedTarget();
            ToolDefinition^ tool = td == nullptr ? nullptr : Defs()->Tool(td->ToolIdFor(simulator ? "simulator" : "emulator"));
            String^ file = Path::GetFileName(simulator ? TiProgramOf(program) : program->TrimEnd(L'\\', L'/'));
            if (tool == nullptr) return "$ " + file;
            OperationDefinition^ op = tool->Operation(simulator ? "simulate" : "emulate");
            String^ shown = op == nullptr ? "$(Program)" : (op->Shown->Length > 0 ? op->Shown : op->Args);
            return Tidy("$ " + tool->Name + " " + shown->Replace("$(Program)", file)->Replace("$(AsmFiles)", file));
        }

        // the name a target's role prints (the tool's name, or nothing)
        static String^ ToolName(TargetDefinition^ td, String^ role)
        {
            ToolDefinition^ d = td == nullptr ? nullptr : Defs()->Tool(td->ToolIdFor(role));
            return d == nullptr ? String::Empty : d->Name;
        }

        // RIDE's own linker command file for the emulated target (compile.cpp tiLinkCmd)
        static String^ TiLinkCmd()
        {
            TargetDefinition^ td = EmulatedTarget();
            LinkerCommandFileDefinition^ f = td == nullptr ? nullptr : Defs()->LinkerCommandFile(td->LinkerCommandFile);
            return f == nullptr ? String::Empty : f->Text();
        }
        static String^ TiLinkCmdName()
        {
            TargetDefinition^ td = EmulatedTarget();
            LinkerCommandFileDefinition^ f = td == nullptr ? nullptr : Defs()->LinkerCommandFile(td->LinkerCommandFile);
            return f == nullptr ? String::Empty : f->Name;
        }

        // the C runtime library of a configuration on the emulated target (rts6xd.lib / rts6x.lib); a
        // project's alias for it (libc.a, rts6740*, rts6x.lib ...) is read as that (makeTiProgram)
        static String^ RtsLibrary(bool debug)
        {
            TargetDefinition^ td = EmulatedTarget();
            LibraryDefinition^ lib = td == nullptr ? nullptr : Defs()->Library(td->Runtime);
            return lib == nullptr ? String::Empty : lib->FileFor(debug);
        }
        static bool IsRtsAlias(String^ lib)
        {
            TargetDefinition^ td = EmulatedTarget();
            LibraryDefinition^ rts = td == nullptr ? nullptr : Defs()->Library(td->Runtime);
            return rts != nullptr && rts->IsAlias(lib);
        }
        static String^ ShalimarRtsLibrary(bool debug)
        {
            TargetDefinition^ td = EmulatedTarget();
            LibraryDefinition^ lib = td == nullptr ? nullptr : Defs()->LibraryByKind(td->Arch, "shalimarRuntime");
            return lib == nullptr ? String::Empty : lib->FileFor(debug);
        }

        // the Shalimar runtime library a host link takes (RIDE's shalimar links it itself; GRIDE links
        // the objects): the configuration's file in the library's directory, found as the other
        // libraries are; null where the file defines none or it is not there
        static String^ HostShalimarRuntime(Target target, bool debug, String^ toolDir, String^ rideRoot)
        {
            LibraryDefinition^ lib = Defs()->LibraryByKind(Arch(target), "shalimarRuntime");
            if (lib == nullptr || lib->FileFor(debug)->Length == 0) return nullptr;
            String^ dir = FindLibraryDir(lib, toolDir, rideRoot);
            if (dir == nullptr) return nullptr;
            String^ file = Path::Combine(dir, lib->FileFor(debug));
            return File::Exists(file) ? file : nullptr;
        }

        // the C runtime the host link names on a target (linkRecipe's CRT list); "" where the linker brings its own
        static String^ CrtLibraries(Target target, bool debug)
        {
            LibraryDefinition^ lib = Defs()->LibraryByKind(Arch(target), "hostCrt");
            return lib == nullptr ? String::Empty : lib->FileFor(debug);
        }
        static String^ WindowsCrtLibraries(bool debug)
        {
            TargetDefinition^ host = Defs()->HostTarget();
            return host == nullptr ? String::Empty : CrtLibraries(TargetInfo::Parse(host->IdeTarget), debug);
        }

    private:
        static ToolchainDefinitions^ Defs() { return ToolchainDefinitions::Current; }

        // the compiler directories under <root>\<under> (a `*` in `under` names any folder starting so),
        // their names starting as the file's compilerDirs say
        static void CompilersUnder(String^ root, String^ under, CcsDefinition^ cd, List<String^>^ into)
        {
            List<String^>^ dirs = gcnew List<String^>();
            dirs->Add(root);
            try
            {
                for each (String^ part in (under == nullptr ? String::Empty : under)->Split(gcnew array<wchar_t>{ L'\\', L'/' }, StringSplitOptions::RemoveEmptyEntries))
                {
                    List<String^>^ next = gcnew List<String^>();
                    for each (String^ d in dirs)
                    {
                        if (!Directory::Exists(d)) continue;
                        if (part->EndsWith("*")) next->AddRange(Directory::GetDirectories(d, part));
                        else next->Add(Path::Combine(d, part));
                    }
                    dirs = next;
                }
                for each (String^ d in dirs)
                {
                    if (!Directory::Exists(d)) continue;
                    for each (String^ sub in Directory::GetDirectories(d))
                    {
                        String^ leaf = Path::GetFileName(sub);
                        for each (KeyValuePair<String^, String^> kv in cd->CompilerDirs)
                            if (kv.Value->Length > 0 && leaf->StartsWith(kv.Value, StringComparison::OrdinalIgnoreCase)) { into->Add(sub); break; }
                    }
                }
            }
            catch (Exception^) { }
        }

        // the numbers after the last '_' of ti-cgt-c6000_8.2.2, so 8.10 is newer than 8.2 (settings.cpp's versionOf)
        static List<int>^ VersionOf(String^ name)
        {
            List<int>^ parts = gcnew List<int>();
            int at = name->LastIndexOf(L'_');
            int i = at < 0 ? 0 : at + 1;
            while (i < name->Length)
            {
                if (!Char::IsDigit(name[i])) { i++; continue; }
                int n = 0;
                while (i < name->Length && Char::IsDigit(name[i])) n = n * 10 + (name[i++] - L'0');
                parts->Add(n);
            }
            return parts;
        }
        static bool Newer(List<int>^ a, List<int>^ b)
        {
            for (int i = 0; i < Math::Max(a->Count, b->Count); i++)
            {
                int x = i < a->Count ? a[i] : -1, y = i < b->Count ? b[i] : -1;
                if (x != y) return x > y;
            }
            return false;
        }

        static bool HasMarker(String^ dir, List<String^>^ markers)
        {
            if (String::IsNullOrEmpty(dir) || !Directory::Exists(dir)) return false;
            for each (String^ m in markers) if (File::Exists(Path::Combine(dir, m))) return true;
            return false;
        }
        static bool HasFile(String^ dir, String^ marker)
        {
            return String::IsNullOrEmpty(marker) || File::Exists(Path::Combine(dir, marker));
        }
        // the tool folder, beside the IDE, the installation's bin
        static List<String^>^ SearchDirs(String^ toolDir, String^ rideRoot)
        {
            List<String^>^ dirs = gcnew List<String^>();
            if (!String::IsNullOrWhiteSpace(toolDir)) dirs->Add(Environment::ExpandEnvironmentVariables(toolDir));
            String^ ide = IdeDirectory();
            if (ide != nullptr) dirs->Add(ide);
            String^ ride = RideBin(rideRoot);
            if (ride != nullptr) dirs->Add(ride);
            return dirs;
        }
        static String^ Fill(String^ templ, RideTool t, String^ source, Language lang, Target target,
                            String^ configFlags, String^ assemblerFlag, String^ includeFlags)
        {
            return templ->Replace("$(Source)", source == nullptr ? String::Empty : source)
                        ->Replace("$(ArchFlag)", ArchFlag(t, target))
                        ->Replace("$(ConfigFlags)", configFlags == nullptr ? String::Empty : configFlags)
                        ->Replace("$(AsmSyntaxFlag)", assemblerFlag == nullptr ? String::Empty : assemblerFlag)
                        ->Replace("$(IncludeFlags)", includeFlags == nullptr ? String::Empty : includeFlags)
                        ->Replace("$(LangFlags)", LanguageFlags(t, lang))
                        ->Replace("$(DefineFlags)", String::Empty)->Replace("$(CompileFlags)", String::Empty)->Replace("$(LibFlags)", String::Empty);
        }
        // one space between words, none at the ends
        static String^ Tidy(String^ s)
        {
            return String::Join(" ", s->Split(gcnew array<wchar_t>{ L' ' }, StringSplitOptions::RemoveEmptyEntries));
        }
        static void Append(StringBuilder^ sb, String^ s)
        {
            if (String::IsNullOrEmpty(s)) return;
            if (sb->Length > 0) sb->Append(L' ');
            sb->Append(s);
        }
    };
}}
