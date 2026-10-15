// =============================================================================
//  ToolchainSettings.h - tool paths and command templates for ONE target
//
//  Every build step is a command-line template with $(Macros), e.g.
//     "$(Assembler)" "$(AsmFile)" --output_file="$(ObjFile)"
//  so the real C90 / C++11 / Shalimar compilers, MASM/LINK and
//  ASM6x/LNK6x/RTS6747/VM6747 can be plugged in later without code changes.
// =============================================================================
#pragma once
#include "../Core/Json.h"
#include "../Core/LanguageTarget.h"
#include "../Core/PathUtil.h"

namespace Gride { namespace Settings {

    using namespace Gride::Core;

    public ref class ToolchainSettings
    {
    public:
        ToolchainSettings(Target t)
        {
            TargetKind = t;
            IncludeDirs = gcnew List<String^>();
            LibDirs = gcnew List<String^>();
            ApplyDefaults();
        }

        property Target  TargetKind;
        property String^ ToolDir;           // folder used for tool names that are not absolute
        property String^ CompilerC90;
        property String^ CompilerCpp11;
        property String^ CompilerShalimar;
        property String^ Assembler;
        property String^ Linker;
        property String^ Runtime;           // runtime library (RTS6747.lib)
        property String^ Simulator;         // program runner / simulator (VM6747)
        property List<String^>^ IncludeDirs;
        property List<String^>^ LibDirs;
        // command templates
        property String^ CompileCmd;
        property String^ AssembleCmd;
        property String^ LinkCmd;
        property String^ RunCmd;
        property String^ DebugCmd;
        // flag spellings, because MASM/LINK and TI style tools differ
        property String^ IncludeFlag;       // -I
        property String^ DefineFlag;        // -D
        property String^ LibDirFlag;        // -L  or /LIBPATH:
        property String^ LibFlag;           // -l  or (empty)
        property String^ AsmExt;            // .asm
        property String^ ObjExt;            // .obj / .o

        property String^ Name { String^ get() { return TargetInfo::Name(TargetKind); } }

        String^ CompilerFor(Language l)
        {
            switch (l)
            {
            case Language::Cpp11:    return CompilerCpp11;
            case Language::Shalimar: return CompilerShalimar;
            default:                 return CompilerC90;
            }
        }

        // tool name -> full path (relative names are looked up in ToolDir)
        String^ Resolve(String^ tool)
        {
            if (String::IsNullOrEmpty(tool)) return String::Empty;
            String^ t = Environment::ExpandEnvironmentVariables(tool);
            if (Path::IsPathRooted(t) || String::IsNullOrEmpty(ToolDir)) return t;
            return Path::Combine(Environment::ExpandEnvironmentVariables(ToolDir), t);
        }

        bool ToolExists(String^ tool)
        {
            String^ p = Resolve(tool);
            return !String::IsNullOrEmpty(p) && File::Exists(p);
        }

        void ApplyDefaults()
        {
            IncludeDirs->Clear();
            LibDirs->Clear();
            CompilerC90 = "grc90.exe";
            CompilerCpp11 = "grcpp11.exe";
            CompilerShalimar = "shalimar.exe";
            Runtime = String::Empty;
            Simulator = String::Empty;
            RunCmd = String::Empty;
            DebugCmd = String::Empty;
            AsmExt = ".asm";
            IncludeFlag = "-I";
            DefineFlag = "-D";
            LibDirFlag = "-L";
            LibFlag = "-l";
            ObjExt = ".o";

            switch (TargetKind)
            {
            case Target::Windows:
                ToolDir = "C:\\GRIDE\\bin\\win";
                Assembler = "C:\\masm32\\bin\\ml.exe";
                Linker = "C:\\masm32\\bin\\link.exe";
                IncludeDirs->Add("C:\\GRIDE\\inc\\win");
                LibDirs->Add("C:\\GRIDE\\lib\\win");
                ObjExt = ".obj";
                LibDirFlag = "/LIBPATH:";
                LibFlag = String::Empty;
                CompileCmd = "\"$(Compiler)\" \"$(Source)\" --target=win $(IncludeFlags) $(DefineFlags) -$(Optimise) $(CompileFlags) -o \"$(AsmFile)\"";
                AssembleCmd = "\"$(Assembler)\" /nologo /c /coff /Fo\"$(ObjFile)\" \"$(AsmFile)\"";
                LinkCmd = "\"$(Linker)\" /nologo /SUBSYSTEM:CONSOLE /OUT:\"$(Output)\" $(Objects) $(LibDirFlags) $(LibFlags) $(LinkFlags)";
                RunCmd = "\"$(Program)\"";
                break;

            case Target::Linux:
                ToolDir = "C:\\GRIDE\\bin\\linux";
                Assembler = String::Empty;
                Linker = String::Empty;
                IncludeDirs->Add("C:\\GRIDE\\inc\\linux");
                LibDirs->Add("C:\\GRIDE\\lib\\linux");
                AsmExt = ".s";
                CompileCmd = "\"$(Compiler)\" \"$(Source)\" --target=linux $(IncludeFlags) $(DefineFlags) -$(Optimise) $(CompileFlags) -o \"$(AsmFile)\"";
                AssembleCmd = "\"$(Assembler)\" \"$(AsmFile)\" -o \"$(ObjFile)\"";
                LinkCmd = "\"$(Linker)\" $(Objects) -o \"$(Output)\" $(LibDirFlags) $(LibFlags) $(LinkFlags)";
                break;

            case Target::MacOS:
                ToolDir = "C:\\GRIDE\\bin\\macos";
                Assembler = String::Empty;
                Linker = String::Empty;
                IncludeDirs->Add("C:\\GRIDE\\inc\\macos");
                LibDirs->Add("C:\\GRIDE\\lib\\macos");
                AsmExt = ".s";
                CompileCmd = "\"$(Compiler)\" \"$(Source)\" --target=macos $(IncludeFlags) $(DefineFlags) -$(Optimise) $(CompileFlags) -o \"$(AsmFile)\"";
                AssembleCmd = "\"$(Assembler)\" \"$(AsmFile)\" -o \"$(ObjFile)\"";
                LinkCmd = "\"$(Linker)\" $(Objects) -o \"$(Output)\" $(LibDirFlags) $(LibFlags) $(LinkFlags)";
                break;

            default: // TMS320C6747
                ToolDir = "C:\\GRIDE\\bin\\c6x";
                Assembler = "asm6x.exe";
                Linker = "lnk6x.exe";
                Runtime = "C:\\GRIDE\\lib\\c6x\\rts6747.lib";
                Simulator = "vm6747.exe";
                IncludeDirs->Add("C:\\GRIDE\\inc\\c6x");
                LibDirs->Add("C:\\GRIDE\\lib\\c6x");
                ObjExt = ".obj";
                LibDirFlag = "-i";
                LibFlag = "-l";
                CompileCmd = "\"$(Compiler)\" \"$(Source)\" --target=c6747 $(IncludeFlags) $(DefineFlags) -$(Optimise) $(CompileFlags) -o \"$(AsmFile)\"";
                AssembleCmd = "\"$(Assembler)\" \"$(AsmFile)\" --output_file=\"$(ObjFile)\"";
                LinkCmd = "\"$(Linker)\" $(Objects) \"$(LinkerCmd)\" -o \"$(Output)\" $(LibDirFlags) $(LibFlags) $(LinkFlags)";
                RunCmd = "\"$(Simulator)\" \"$(Program)\"";
                DebugCmd = "\"$(Simulator)\" --debug \"$(Program)\"";
                break;
            }
        }

        JsonValue^ ToJson()
        {
            JsonValue^ o = JsonValue::MakeObject();
            o->SetString("toolDir", ToolDir);
            JsonValue^ comp = JsonValue::MakeObject();
            comp->SetString("C90", CompilerC90);
            comp->SetString("C++11", CompilerCpp11);
            comp->SetString("Shalimar", CompilerShalimar);
            o->SetValue("compilers", comp);
            o->SetString("assembler", Assembler);
            o->SetString("linker", Linker);
            o->SetString("runtime", Runtime);
            o->SetString("simulator", Simulator);
            o->SetList("includeDirs", IncludeDirs);
            o->SetList("libDirs", LibDirs);
            JsonValue^ cmd = JsonValue::MakeObject();
            cmd->SetString("compile", CompileCmd);
            cmd->SetString("assemble", AssembleCmd);
            cmd->SetString("link", LinkCmd);
            cmd->SetString("run", RunCmd);
            cmd->SetString("debug", DebugCmd);
            o->SetValue("commands", cmd);
            JsonValue^ flags = JsonValue::MakeObject();
            flags->SetString("include", IncludeFlag);
            flags->SetString("define", DefineFlag);
            flags->SetString("libDir", LibDirFlag);
            flags->SetString("lib", LibFlag);
            flags->SetString("asmExt", AsmExt);
            flags->SetString("objExt", ObjExt);
            o->SetValue("flags", flags);
            return o;
        }

        // starts from defaults, so keys missing in an old settings file stay valid
        static ToolchainSettings^ FromJson(Target t, JsonValue^ o)
        {
            ToolchainSettings^ s = gcnew ToolchainSettings(t);
            if (o == nullptr || !o->IsObject) return s;
            s->ToolDir = o->GetString("toolDir", s->ToolDir);
            JsonValue^ comp = o->GetObject("compilers");
            if (comp != nullptr)
            {
                s->CompilerC90 = comp->GetString("C90", s->CompilerC90);
                s->CompilerCpp11 = comp->GetString("C++11", s->CompilerCpp11);
                s->CompilerShalimar = comp->GetString("Shalimar", s->CompilerShalimar);
            }
            s->Assembler = o->GetString("assembler", s->Assembler);
            s->Linker = o->GetString("linker", s->Linker);
            s->Runtime = o->GetString("runtime", s->Runtime);
            s->Simulator = o->GetString("simulator", s->Simulator);
            if (o->Has("includeDirs")) s->IncludeDirs = o->GetStringList("includeDirs");
            if (o->Has("libDirs")) s->LibDirs = o->GetStringList("libDirs");
            JsonValue^ cmd = o->GetObject("commands");
            if (cmd != nullptr)
            {
                s->CompileCmd = cmd->GetString("compile", s->CompileCmd);
                s->AssembleCmd = cmd->GetString("assemble", s->AssembleCmd);
                s->LinkCmd = cmd->GetString("link", s->LinkCmd);
                s->RunCmd = cmd->GetString("run", s->RunCmd);
                s->DebugCmd = cmd->GetString("debug", s->DebugCmd);
            }
            JsonValue^ flags = o->GetObject("flags");
            if (flags != nullptr)
            {
                s->IncludeFlag = flags->GetString("include", s->IncludeFlag);
                s->DefineFlag = flags->GetString("define", s->DefineFlag);
                s->LibDirFlag = flags->GetString("libDir", s->LibDirFlag);
                s->LibFlag = flags->GetString("lib", s->LibFlag);
                s->AsmExt = flags->GetString("asmExt", s->AsmExt);
                s->ObjExt = flags->GetString("objExt", s->ObjExt);
            }
            return s;
        }

        ToolchainSettings^ Clone() { return FromJson(TargetKind, ToJson()); }
    };
}}
