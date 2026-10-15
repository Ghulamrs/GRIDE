// =============================================================================
//  ToolchainSettings.h - tool paths and command templates for ONE target
//
//  Every build step is a command-line template with $(Macros), e.g.
//     "$(Compiler)" -S "$(Source)" -o "$(AsmFile)" $(ArchFlag) $(ConfigFlags)
//  The defaults come from toolchain.json (ToolchainDefinitions): the tool each role names
//  for the target, its operations' argument templates, the target's extensions and flag
//  spellings, the runtime library's directory. A value still at its default is not written
//  to the settings file, so it follows the definitions; a value the user set is kept.
// =============================================================================
#pragma once
#include "../Core/Json.h"
#include "../Core/LanguageTarget.h"
#include "../Core/PathUtil.h"
#include "../Services/RideToolchain.h"
#include "../Services/VisualStudio.h"
#include "ToolchainDefinitions.h"

namespace Gride { namespace Settings {

    using namespace Gride::Core;
    using namespace Gride::Services;

    public ref class ToolchainSettings
    {
    public:
        ToolchainSettings(Target t)
        {
            TargetKind = t;
            IncludeDirs = gcnew List<String^>();
            LibDirs = gcnew List<String^>();
            EnvironmentVars = gcnew List<String^>();
            ApplyDefaults();
        }

        // RIDE's installation root or bin folder, when the settings name one (GrideSettings sets it)
        static property String^ RideRoot;

        property Target  TargetKind;
        property String^ ToolDir;           // folder used for tool names that are not absolute
        property String^ CompilerC90;
        property String^ CompilerCpp11;
        property String^ CompilerShalimar;
        property String^ CompilerCl;        // the machine's own compiler, found through vcvars64.bat
        property String^ Assembler;         // masm (x86_64-windows), asm6x (tms6747)
        property String^ Linker;            // link (x86_64-windows), lnk6x (tms6747)
        property String^ DebugAssembler;    // clang, for a Debug build's CodeView on x86_64-windows
        property String^ DebugLinker;       // Microsoft's link.exe, the one that writes a PDB; empty = found through vcvars
        property String^ Runtime;           // a runtime library file handed to the link (rts6x.lib / rts6xd.lib by configuration)
        property String^ RuntimeDir;        // RTS6x's directory, lnk6x's -i (lib\rts6x-tms6747)
        property String^ Simulator;         // sim6747: runs the linked .out
        property String^ Emulator;          // vm6747: runs the assembly, and is the tms6747 debugger (--debug)
        property String^ Debugger;          // cdb for x86_64-windows, gdb / lldb elsewhere
        property List<String^>^ IncludeDirs;
        property List<String^>^ LibDirs;
        // command templates
        property String^ CompileCmd;        // c90, cpp11
        property String^ CompileCmdCl;      // cl
        property String^ CompileCmdShalimar; // shalimar: every file of the program in one command ($(Sources)); empty = CompileCmd
        property String^ AssembleCmd;
        property String^ LinkCmd;
        property String^ DebugAssembleCmd;  // the CodeView path of a Debug build (x86_64-windows)
        property String^ DebugLinkCmd;
        property String^ RunCmd;
        property String^ EmulateCmd;        // tms6747: the assembly on vm6747
        property String^ DebugCmd;
        // flag spellings, because MASM/LINK and TI style tools differ
        property String^ IncludeFlag;       // -I
        property String^ DefineFlag;        // -D
        property String^ LibDirFlag;        // -L  or /LIBPATH:
        property String^ LibFlag;           // -l  or (empty)
        property String^ AsmExt;            // .s / .asm
        property String^ ObjExt;            // .obj / .o
        // how the tools of this target are started
        property String^ SetupScript;       // e.g. vcvars64.bat; when set, every tool runs through cmd.exe after it
        property List<String^>^ EnvironmentVars;   // "NAME=value" (%VAR% expanded), added to the tool environment
        property int     OutputCodePage;    // code page of the tool output, 0 = OEM code page of the system
        property String^ ErrorPattern;      // extra regex with groups file, line, col, sev, code, msg

        property String^ Name { String^ get() { return TargetInfo::Name(TargetKind); } }
        property String^ Arch { String^ get() { return RideToolchain::Arch(TargetKind); } }

        String^ CompilerFor(Language l)
        {
            switch (l)
            {
            case Language::Cpp11:    return CompilerCpp11;
            case Language::Shalimar: return CompilerShalimar;
            default:                 return CompilerC90;
            }
        }

        String^ CompilerFor(RideTool t)
        {
            switch (t)
            {
            case RideTool::Cpp11:    return CompilerCpp11;
            case RideTool::Shalimar: return CompilerShalimar;
            case RideTool::Cl:
            case RideTool::HostCxx:  return CompilerCl;
            default:                 return CompilerC90;
            }
        }

        // tool name -> full path: an absolute name as it is; a name toolchain.json knows is looked for
        // as its lookup says (its environment variable, the tool folder, beside the IDE, the
        // installation, Visual Studio's folders, PATH); another name in the tool folder, beside the
        // IDE, the installation, PATH; when nowhere, the name under ToolDir, so a "not found" status
        // still names a place
        String^ Resolve(String^ tool) { return ResolveFor(nullptr, tool); }

        // the same for a role: the role's own tool definition is asked first, so the Debug linker's
        // link.exe is Microsoft's (through Visual Studio) and the linker's link.exe is RIDE's (in its bin)
        String^ ResolveFor(String^ role, String^ tool)
        {
            if (String::IsNullOrEmpty(tool)) return String::Empty;
            String^ t = Environment::ExpandEnvironmentVariables(tool);
            if (Path::IsPathRooted(t)) return t;
            String^ dir = String::IsNullOrEmpty(ToolDir) ? String::Empty : Environment::ExpandEnvironmentVariables(ToolDir);
            String^ under = dir->Length > 0 ? Path::Combine(dir, t) : t;
            ToolchainDefinitions^ defs = ToolchainDefinitions::Current;
            ToolDefinition^ d = role == nullptr ? nullptr : defs->ToolFor(TargetKind, role);
            if (d == nullptr || !d->HasName(t)) d = defs->ToolByName(t);
            String^ found = nullptr;
            if (d != nullptr) found = RideToolchain::FindTool(d, dir, RideRoot);
            else
            {
                if (File::Exists(under) || Directory::Exists(under)) return under;
                found = RideToolchain::Find(gcnew array<String^>{ t }, String::Empty, dir, RideRoot);
            }
            return found != nullptr ? found : under;
        }

        bool ToolExists(String^ tool)
        {
            String^ p = Resolve(tool);
            return !String::IsNullOrEmpty(p) && File::Exists(p);
        }

        // the environment variable that names a program of that name ($C90, $VM6747 ...): the
        // lookup of the tool the name belongs to
        static String^ EnvironmentVariableFor(String^ tool)
        {
            ToolDefinition^ d = ToolchainDefinitions::Current->ToolByName(tool);
            return d == nullptr ? String::Empty : d->Lookup->Env;
        }

        // where a tool name was looked for, for a "not found" message
        String^ LookedIn(String^ tool)
        {
            ToolDefinition^ d = ToolchainDefinitions::Current->ToolByName(tool);
            String^ dir = String::IsNullOrEmpty(ToolDir) ? String::Empty : Environment::ExpandEnvironmentVariables(ToolDir);
            return RideToolchain::SearchPlaces(d, dir, RideRoot);
        }

        // the RTS6x directory in force: the one named, else found as RIDE finds it
        String^ ResolveRuntimeDir()
        {
            if (!String::IsNullOrWhiteSpace(RuntimeDir))
            {
                String^ r = Resolve(RuntimeDir);
                if (Directory::Exists(r)) return r;
            }
            String^ found = RideToolchain::FindRuntimeDir(ToolDir, RideRoot);
            return found == nullptr ? (String::IsNullOrWhiteSpace(RuntimeDir) ? String::Empty : Resolve(RuntimeDir)) : found;
        }

        // everything from toolchain.json: the tools the target's roles name, their operations'
        // templates behind the tool's own macro, the target's extensions and flags
        void ApplyDefaults()
        {
            IncludeDirs->Clear();
            LibDirs->Clear();
            EnvironmentVars->Clear();
            SetupScript = String::Empty;
            OutputCodePage = 0;
            ErrorPattern = String::Empty;
            Runtime = String::Empty;
            ToolchainDefinitions^ defs = ToolchainDefinitions::Current;
            TargetDefinition^ td = defs->TargetOf(TargetKind);
            ToolDefinition^ cc = Role(defs, td, "cCompiler");
            ToolDefinition^ cxx = Role(defs, td, "cppCompiler");
            ToolDefinition^ shc = Role(defs, td, "shalimarCompiler");
            ToolDefinition^ host = Role(defs, td, "hostCompiler");
            ToolDefinition^ as = Role(defs, td, "assembler");
            ToolDefinition^ ld = Role(defs, td, "linker");
            ToolDefinition^ das = Role(defs, td, "debugAssembler");
            ToolDefinition^ dld = Role(defs, td, "debugLinker");
            ToolDefinition^ sim = Role(defs, td, "simulator");
            ToolDefinition^ emu = Role(defs, td, "emulator");
            ToolDefinition^ dbg = Role(defs, td, "debugger");
            CompilerC90 = ExeOf(cc);
            CompilerCpp11 = ExeOf(cxx);
            CompilerShalimar = ExeOf(shc);
            CompilerCl = ExeOf(host);
            Assembler = ExeOf(as);
            Linker = ExeOf(ld);
            DebugAssembler = ExeOf(das);
            DebugLinker = ExeOf(dld);
            Simulator = ExeOf(sim);
            Emulator = ExeOf(emu);
            Debugger = ExeOf(dbg);
            LibraryDefinition^ rts = td == nullptr ? nullptr : defs->Library(td->Runtime);
            RuntimeDir = rts == nullptr ? String::Empty : rts->Directory;
            AsmExt = td == nullptr ? ".s" : td->AssemblyExtension;
            ObjExt = td == nullptr ? ".o" : td->ObjectExtension;
            LibDirFlag = td == nullptr ? String::Empty : td->LibraryDirFlag;
            LibFlag = td == nullptr ? String::Empty : td->LibraryFlag;
            IncludeFlag = cc == nullptr ? String::Empty : cc->IncludeFlag;
            DefineFlag = cc == nullptr ? String::Empty : cc->DefineFlag;
            CompileCmd = Template("Compiler", cc, "compileToAssembly");
            CompileCmdCl = Template("Compiler", host, "compileToAssembly");
            CompileCmdShalimar = Template("Compiler", shc, "compileToAssembly");
            AssembleCmd = Template("Assembler", as, "assemble");
            LinkCmd = Template("Linker", ld, "link");
            DebugAssembleCmd = Template("DebugAssembler", das, "assemble");
            DebugLinkCmd = Template("DebugLinker", dld, "link");
            EmulateCmd = Template("Emulator", emu, "emulate");
            String^ run = td == nullptr ? "none" : td->Run;
            if (run == "program") RunCmd = "\"$(Program)\"";
            else if (run == "simulator") RunCmd = Template("Simulator", sim, "simulate");
            else RunCmd = String::Empty;
            String^ debug = td == nullptr ? "none" : td->Debug;
            if (debug == "simulator") DebugCmd = Template("Simulator", sim, "debug");
            else if (debug == "debugger") DebugCmd = Template("Debugger", dbg, "debug");
            else DebugCmd = String::Empty;
            String^ rideBin = RideToolchain::RideBin(RideRoot);
            ToolDir = rideBin != nullptr ? rideBin : RideToolchain::DefaultRideBin();
        }

        // the values the definitions give this target now
        ToolchainSettings^ Defaults() { return gcnew ToolchainSettings(TargetKind); }

        // after the definitions changed: every value still at the defaults they gave before follows the new ones
        void Rebase(ToolchainSettings^ before)
        {
            ToolchainSettings^ now = Defaults();
            if (before == nullptr) return;
            ToolDir = Follow(ToolDir, before->ToolDir, now->ToolDir);
            CompilerC90 = Follow(CompilerC90, before->CompilerC90, now->CompilerC90);
            CompilerCpp11 = Follow(CompilerCpp11, before->CompilerCpp11, now->CompilerCpp11);
            CompilerShalimar = Follow(CompilerShalimar, before->CompilerShalimar, now->CompilerShalimar);
            CompilerCl = Follow(CompilerCl, before->CompilerCl, now->CompilerCl);
            Assembler = Follow(Assembler, before->Assembler, now->Assembler);
            Linker = Follow(Linker, before->Linker, now->Linker);
            DebugAssembler = Follow(DebugAssembler, before->DebugAssembler, now->DebugAssembler);
            DebugLinker = Follow(DebugLinker, before->DebugLinker, now->DebugLinker);
            RuntimeDir = Follow(RuntimeDir, before->RuntimeDir, now->RuntimeDir);
            Simulator = Follow(Simulator, before->Simulator, now->Simulator);
            Emulator = Follow(Emulator, before->Emulator, now->Emulator);
            Debugger = Follow(Debugger, before->Debugger, now->Debugger);
            CompileCmd = Follow(CompileCmd, before->CompileCmd, now->CompileCmd);
            CompileCmdCl = Follow(CompileCmdCl, before->CompileCmdCl, now->CompileCmdCl);
            CompileCmdShalimar = Follow(CompileCmdShalimar, before->CompileCmdShalimar, now->CompileCmdShalimar);
            AssembleCmd = Follow(AssembleCmd, before->AssembleCmd, now->AssembleCmd);
            LinkCmd = Follow(LinkCmd, before->LinkCmd, now->LinkCmd);
            DebugAssembleCmd = Follow(DebugAssembleCmd, before->DebugAssembleCmd, now->DebugAssembleCmd);
            DebugLinkCmd = Follow(DebugLinkCmd, before->DebugLinkCmd, now->DebugLinkCmd);
            RunCmd = Follow(RunCmd, before->RunCmd, now->RunCmd);
            EmulateCmd = Follow(EmulateCmd, before->EmulateCmd, now->EmulateCmd);
            DebugCmd = Follow(DebugCmd, before->DebugCmd, now->DebugCmd);
            IncludeFlag = Follow(IncludeFlag, before->IncludeFlag, now->IncludeFlag);
            DefineFlag = Follow(DefineFlag, before->DefineFlag, now->DefineFlag);
            LibDirFlag = Follow(LibDirFlag, before->LibDirFlag, now->LibDirFlag);
            LibFlag = Follow(LibFlag, before->LibFlag, now->LibFlag);
            AsmExt = Follow(AsmExt, before->AsmExt, now->AsmExt);
            ObjExt = Follow(ObjExt, before->ObjExt, now->ObjExt);
        }

        // only what differs from the definitions' defaults is written, so an untouched value follows a changed toolchain.json
        JsonValue^ ToJson()
        {
            ToolchainSettings^ d = Defaults();
            JsonValue^ o = JsonValue::MakeObject();
            Put(o, "toolDir", ToolDir, d->ToolDir);
            JsonValue^ comp = JsonValue::MakeObject();
            Put(comp, "C90", CompilerC90, d->CompilerC90);
            Put(comp, "C++11", CompilerCpp11, d->CompilerCpp11);
            Put(comp, "Shalimar", CompilerShalimar, d->CompilerShalimar);
            Put(comp, "cl", CompilerCl, d->CompilerCl);
            if (comp->Count > 0) o->SetValue("compilers", comp);
            Put(o, "assembler", Assembler, d->Assembler);
            Put(o, "linker", Linker, d->Linker);
            Put(o, "debugAssembler", DebugAssembler, d->DebugAssembler);
            Put(o, "debugLinker", DebugLinker, d->DebugLinker);
            Put(o, "runtime", Runtime, d->Runtime);
            Put(o, "runtimeDir", RuntimeDir, d->RuntimeDir);
            Put(o, "simulator", Simulator, d->Simulator);
            Put(o, "emulator", Emulator, d->Emulator);
            Put(o, "debugger", Debugger, d->Debugger);
            if (IncludeDirs->Count > 0) o->SetList("includeDirs", IncludeDirs);
            if (LibDirs->Count > 0) o->SetList("libDirs", LibDirs);
            JsonValue^ cmd = JsonValue::MakeObject();
            Put(cmd, "compile", CompileCmd, d->CompileCmd);
            Put(cmd, "compileCl", CompileCmdCl, d->CompileCmdCl);
            Put(cmd, "compileShalimar", CompileCmdShalimar, d->CompileCmdShalimar);
            Put(cmd, "assemble", AssembleCmd, d->AssembleCmd);
            Put(cmd, "link", LinkCmd, d->LinkCmd);
            Put(cmd, "debugAssemble", DebugAssembleCmd, d->DebugAssembleCmd);
            Put(cmd, "debugLink", DebugLinkCmd, d->DebugLinkCmd);
            Put(cmd, "run", RunCmd, d->RunCmd);
            Put(cmd, "emulate", EmulateCmd, d->EmulateCmd);
            Put(cmd, "debug", DebugCmd, d->DebugCmd);
            if (cmd->Count > 0) o->SetValue("commands", cmd);
            JsonValue^ flags = JsonValue::MakeObject();
            Put(flags, "include", IncludeFlag, d->IncludeFlag);
            Put(flags, "define", DefineFlag, d->DefineFlag);
            Put(flags, "libDir", LibDirFlag, d->LibDirFlag);
            Put(flags, "lib", LibFlag, d->LibFlag);
            Put(flags, "asmExt", AsmExt, d->AsmExt);
            Put(flags, "objExt", ObjExt, d->ObjExt);
            if (flags->Count > 0) o->SetValue("flags", flags);
            JsonValue^ run = JsonValue::MakeObject();
            Put(run, "setupScript", SetupScript, String::Empty);
            if (EnvironmentVars->Count > 0) run->SetList("environment", EnvironmentVars);
            if (OutputCodePage != 0) run->SetNumber("outputCodePage", OutputCodePage);
            Put(run, "errorPattern", ErrorPattern, String::Empty);
            if (run->Count > 0) o->SetValue("run", run);
            return o;
        }

        // starts from defaults, so keys missing in an old settings file stay valid; a value that is
        // still one of the first release's defaults (grc90.exe, the --target=win templates ...) is
        // taken as unchanged and becomes RIDE's default
        static ToolchainSettings^ FromJson(Target t, JsonValue^ o)
        {
            ToolchainSettings^ s = gcnew ToolchainSettings(t);
            if (o == nullptr || !o->IsObject) return s;
            ToolchainSettings^ old = OldDefaults(t);
            s->ToolDir = Upgrade(o->GetString("toolDir", s->ToolDir), old->ToolDir, s->ToolDir);
            JsonValue^ comp = o->GetObject("compilers");
            if (comp != nullptr)
            {
                s->CompilerC90 = Upgrade(comp->GetString("C90", s->CompilerC90), old->CompilerC90, s->CompilerC90);
                s->CompilerCpp11 = Upgrade(comp->GetString("C++11", s->CompilerCpp11), old->CompilerCpp11, s->CompilerCpp11);
                s->CompilerShalimar = comp->GetString("Shalimar", s->CompilerShalimar);
                s->CompilerCl = comp->GetString("cl", s->CompilerCl);
            }
            s->Assembler = Upgrade(o->GetString("assembler", s->Assembler), old->Assembler, s->Assembler);
            s->Linker = Upgrade(o->GetString("linker", s->Linker), old->Linker, s->Linker);
            s->DebugAssembler = o->GetString("debugAssembler", s->DebugAssembler);
            s->DebugLinker = o->GetString("debugLinker", s->DebugLinker);
            s->Runtime = Upgrade(o->GetString("runtime", s->Runtime), old->Runtime, s->Runtime);
            s->RuntimeDir = o->GetString("runtimeDir", s->RuntimeDir);
            s->Simulator = Upgrade(o->GetString("simulator", s->Simulator), old->Simulator, s->Simulator);
            s->Emulator = o->GetString("emulator", s->Emulator);
            s->Debugger = o->GetString("debugger", s->Debugger);
            if (o->Has("includeDirs")) s->IncludeDirs = UpgradeList(o->GetStringList("includeDirs"), old->IncludeDirs, s->IncludeDirs);
            if (o->Has("libDirs")) s->LibDirs = UpgradeList(o->GetStringList("libDirs"), old->LibDirs, s->LibDirs);
            JsonValue^ cmd = o->GetObject("commands");
            if (cmd != nullptr)
            {
                s->CompileCmd = Upgrade(cmd->GetString("compile", s->CompileCmd), old->CompileCmd, s->CompileCmd);
                s->CompileCmdCl = cmd->GetString("compileCl", s->CompileCmdCl);
                s->CompileCmdShalimar = cmd->GetString("compileShalimar", s->CompileCmdShalimar);
                s->AssembleCmd = Upgrade(cmd->GetString("assemble", s->AssembleCmd), old->AssembleCmd, s->AssembleCmd);
                s->LinkCmd = Upgrade(cmd->GetString("link", s->LinkCmd), old->LinkCmd, s->LinkCmd);
                s->DebugAssembleCmd = cmd->GetString("debugAssemble", s->DebugAssembleCmd);
                s->DebugLinkCmd = cmd->GetString("debugLink", s->DebugLinkCmd);
                s->RunCmd = Upgrade(cmd->GetString("run", s->RunCmd), old->RunCmd, s->RunCmd);
                s->EmulateCmd = cmd->GetString("emulate", s->EmulateCmd);
                s->DebugCmd = Upgrade(cmd->GetString("debug", s->DebugCmd), old->DebugCmd, s->DebugCmd);
            }
            JsonValue^ flags = o->GetObject("flags");
            if (flags != nullptr)
            {
                s->IncludeFlag = flags->GetString("include", s->IncludeFlag);
                s->DefineFlag = flags->GetString("define", s->DefineFlag);
                s->LibDirFlag = Upgrade(flags->GetString("libDir", s->LibDirFlag), old->LibDirFlag, s->LibDirFlag);
                s->LibFlag = Upgrade(flags->GetString("lib", s->LibFlag), old->LibFlag, s->LibFlag);
                s->AsmExt = Upgrade(flags->GetString("asmExt", s->AsmExt), old->AsmExt, s->AsmExt);
                s->ObjExt = flags->GetString("objExt", s->ObjExt);
            }
            JsonValue^ run = o->GetObject("run");
            if (run != nullptr)
            {
                s->SetupScript = run->GetString("setupScript", s->SetupScript);
                if (run->Has("environment")) s->EnvironmentVars = run->GetStringList("environment");
                s->OutputCodePage = Math::Max(0, run->GetInt("outputCodePage", s->OutputCodePage));
                s->ErrorPattern = run->GetString("errorPattern", s->ErrorPattern);
            }
            return s;
        }

        ToolchainSettings^ Clone() { return FromJson(TargetKind, ToJson()); }

    private:
        static ToolDefinition^ Role(ToolchainDefinitions^ defs, TargetDefinition^ td, String^ role)
        {
            return td == nullptr ? nullptr : defs->Tool(td->ToolIdFor(role));
        }
        static String^ ExeOf(ToolDefinition^ d) { return d == nullptr ? String::Empty : d->Exe; }
        // "<macro of the tool>" followed by the operation's arguments; nothing when the tool or the operation is not defined
        static String^ Template(String^ macro, ToolDefinition^ d, String^ operation)
        {
            if (d == nullptr || d->Operation(operation) == nullptr) return String::Empty;
            String^ args = d->OperationArgs(operation)->Trim();
            return "\"$(" + macro + ")\"" + (args->Length > 0 ? " " + args : String::Empty);
        }
        static String^ Follow(String^ value, String^ before, String^ now)
        {
            return value != nullptr && String::Equals(value, before) ? now : value;
        }
        static void Put(JsonValue^ o, String^ key, String^ value, String^ def)
        {
            if (!String::Equals(value == nullptr ? String::Empty : value, def == nullptr ? String::Empty : def)) o->SetString(key, value);
        }

        static String^ Upgrade(String^ value, String^ oldDefault, String^ newDefault)
        {
            if (value != nullptr && oldDefault != nullptr && String::Compare(value->Trim(), oldDefault, true) == 0) return newDefault;
            return value;
        }

        static List<String^>^ UpgradeList(List<String^>^ value, List<String^>^ oldDefault, List<String^>^ newDefault)
        {
            if (value == nullptr || value->Count != oldDefault->Count) return value;
            for (int i = 0; i < value->Count; i++)
                if (String::Compare(value[i], oldDefault[i], true) != 0) return value;
            return gcnew List<String^>(newDefault);
        }

        // the defaults of the release before the RIDE port, so an untouched settings file follows the port
        static ToolchainSettings^ OldDefaults(Target t)
        {
            ToolchainSettings^ o = gcnew ToolchainSettings(t);
            o->CompilerC90 = "grc90.exe";
            o->CompilerCpp11 = "grcpp11.exe";
            o->Runtime = String::Empty;
            o->Simulator = String::Empty;
            o->Assembler = String::Empty;
            o->Linker = String::Empty;
            o->RunCmd = String::Empty;
            o->DebugCmd = String::Empty;
            o->AsmExt = ".asm";
            o->LibDirFlag = "-L";
            o->LibFlag = "-l";
            o->IncludeDirs->Clear();
            o->LibDirs->Clear();
            switch (t)
            {
            case Target::Windows:
                o->ToolDir = "C:\\GRIDE\\bin\\win";
                o->Assembler = "C:\\masm32\\bin\\ml.exe";
                o->Linker = "C:\\masm32\\bin\\link.exe";
                o->IncludeDirs->Add("C:\\GRIDE\\inc\\win");
                o->LibDirs->Add("C:\\GRIDE\\lib\\win");
                o->LibDirFlag = "/LIBPATH:";
                o->LibFlag = String::Empty;
                o->CompileCmd = "\"$(Compiler)\" \"$(Source)\" --target=win $(IncludeFlags) $(DefineFlags) -$(Optimise) $(CompileFlags) -o \"$(AsmFile)\"";
                o->AssembleCmd = "\"$(Assembler)\" /nologo /c /coff /Fo\"$(ObjFile)\" \"$(AsmFile)\"";
                o->LinkCmd = "\"$(Linker)\" /nologo /SUBSYSTEM:CONSOLE /OUT:\"$(Output)\" $(Objects) $(LibDirFlags) $(LibFlags) $(LinkFlags)";
                o->RunCmd = "\"$(Program)\"";
                break;
            case Target::Linux:
                o->ToolDir = "C:\\GRIDE\\bin\\linux";
                o->IncludeDirs->Add("C:\\GRIDE\\inc\\linux");
                o->LibDirs->Add("C:\\GRIDE\\lib\\linux");
                o->AsmExt = ".s";
                o->CompileCmd = "\"$(Compiler)\" \"$(Source)\" --target=linux $(IncludeFlags) $(DefineFlags) -$(Optimise) $(CompileFlags) -o \"$(AsmFile)\"";
                o->AssembleCmd = "\"$(Assembler)\" \"$(AsmFile)\" -o \"$(ObjFile)\"";
                o->LinkCmd = "\"$(Linker)\" $(Objects) -o \"$(Output)\" $(LibDirFlags) $(LibFlags) $(LinkFlags)";
                break;
            case Target::MacOS:
                o->ToolDir = "C:\\GRIDE\\bin\\macos";
                o->IncludeDirs->Add("C:\\GRIDE\\inc\\macos");
                o->LibDirs->Add("C:\\GRIDE\\lib\\macos");
                o->AsmExt = ".s";
                o->CompileCmd = "\"$(Compiler)\" \"$(Source)\" --target=macos $(IncludeFlags) $(DefineFlags) -$(Optimise) $(CompileFlags) -o \"$(AsmFile)\"";
                o->AssembleCmd = "\"$(Assembler)\" \"$(AsmFile)\" -o \"$(ObjFile)\"";
                o->LinkCmd = "\"$(Linker)\" $(Objects) -o \"$(Output)\" $(LibDirFlags) $(LibFlags) $(LinkFlags)";
                break;
            default:
                o->ToolDir = "C:\\GRIDE\\bin\\c6x";
                o->Assembler = "asm6x.exe";
                o->Linker = "lnk6x.exe";
                o->Runtime = "C:\\GRIDE\\lib\\c6x\\rts6747.lib";
                o->Simulator = "vm6747.exe";
                o->IncludeDirs->Add("C:\\GRIDE\\inc\\c6x");
                o->LibDirs->Add("C:\\GRIDE\\lib\\c6x");
                o->LibDirFlag = "-i";
                o->LibFlag = "-l";
                o->CompileCmd = "\"$(Compiler)\" \"$(Source)\" --target=c6747 $(IncludeFlags) $(DefineFlags) -$(Optimise) $(CompileFlags) -o \"$(AsmFile)\"";
                o->AssembleCmd = "\"$(Assembler)\" \"$(AsmFile)\" --output_file=\"$(ObjFile)\"";
                o->LinkCmd = "\"$(Linker)\" $(Objects) \"$(LinkerCmd)\" -o \"$(Output)\" $(LibDirFlags) $(LibFlags) $(LinkFlags)";
                o->RunCmd = "\"$(Simulator)\" \"$(Program)\"";
                o->DebugCmd = "\"$(Simulator)\" --debug \"$(Program)\"";
                break;
            }
            return o;
        }
    };
}}
