// =============================================================================
//  BuildConfiguration.h - settings of one "Config|Target" pair of a project,
//  e.g. "Debug|TMS320C6747". Stored inside the .pro under "configurations".
// =============================================================================
#pragma once
#include "../Core/Json.h"
#include "../Core/LanguageTarget.h"

namespace Gride { namespace Model {

    using namespace Gride::Core;

    public ref class BuildConfiguration
    {
    public:
        BuildConfiguration(String^ config, Target target)
        {
            Config = config;
            TargetKind = target;
            Defines = gcnew List<String^>();
            IncludeDirs = gcnew List<String^>();
            LibDirs = gcnew List<String^>();
            Libs = gcnew List<String^>();
            ApplyDefaults();
        }

        property String^ Config;              // "Debug" / "Release"
        property Target  TargetKind;
        property String^ OutDir;              // relative to project, e.g. out/Debug/C6747
        property String^ ObjDir;              // relative to project, e.g. obj/Debug/C6747
        property List<String^>^ Defines;
        property List<String^>^ IncludeDirs;  // relative or absolute
        property List<String^>^ LibDirs;
        property List<String^>^ Libs;
        property String^ Optimise;            // O0 .. O3
        property String^ LinkerCmd;           // linker command file (DSP)
        property String^ ExtraCompileFlags;
        property String^ ExtraLinkFlags;
        property String^ OutputName;          // program name without extension (default = project name)

        property String^ Key { String^ get() { return ConfigName::Make(Config, TargetKind); } }
        property bool IsDebug { bool get() { return String::Compare(Config, "Debug", true) == 0; } }

        void ApplyDefaults()
        {
            String^ folder = TargetInfo::FolderName(TargetKind);
            OutDir = "out/" + Config + "/" + folder;
            ObjDir = "obj/" + Config + "/" + folder;
            Defines->Clear();
            if (IsDebug) { Defines->Add("DEBUG"); Optimise = "O0"; }
            else         { Defines->Add("NDEBUG"); Optimise = "O2"; }
            IncludeDirs->Clear();
            IncludeDirs->Add("inc");
            LibDirs->Clear();
            Libs->Clear();
            if (TargetKind == Target::C6747)
            {
                // RTS6x's library; rts6x.lib, rts6xd.lib, rts6747.lib and libc.a are all read as the
                // configuration's one (rts6xd.lib for Debug) when the program is linked
                LinkerCmd = "c6747.cmd";
                Libs->Add("rts6x.lib");
            }
            else LinkerCmd = String::Empty;
            ExtraCompileFlags = String::Empty;
            ExtraLinkFlags = String::Empty;
            OutputName = String::Empty;
        }

        BuildConfiguration^ Clone()
        {
            BuildConfiguration^ c = gcnew BuildConfiguration(Config, TargetKind);
            c->OutDir = OutDir; c->ObjDir = ObjDir;
            c->Defines = gcnew List<String^>(Defines);
            c->IncludeDirs = gcnew List<String^>(IncludeDirs);
            c->LibDirs = gcnew List<String^>(LibDirs);
            c->Libs = gcnew List<String^>(Libs);
            c->Optimise = Optimise; c->LinkerCmd = LinkerCmd;
            c->ExtraCompileFlags = ExtraCompileFlags; c->ExtraLinkFlags = ExtraLinkFlags;
            c->OutputName = OutputName;
            return c;
        }

        JsonValue^ ToJson()
        {
            JsonValue^ o = JsonValue::MakeObject();
            o->SetString("outDir", OutDir);
            o->SetString("objDir", ObjDir);
            o->SetList("defines", Defines);
            o->SetList("includeDirs", IncludeDirs);
            o->SetList("libDirs", LibDirs);
            o->SetList("libs", Libs);
            o->SetString("optimise", Optimise);
            if (!String::IsNullOrEmpty(LinkerCmd)) o->SetString("linkerCmd", LinkerCmd);
            if (!String::IsNullOrEmpty(ExtraCompileFlags)) o->SetString("compileFlags", ExtraCompileFlags);
            if (!String::IsNullOrEmpty(ExtraLinkFlags)) o->SetString("linkFlags", ExtraLinkFlags);
            if (!String::IsNullOrEmpty(OutputName)) o->SetString("outputName", OutputName);
            return o;
        }

        static BuildConfiguration^ FromJson(String^ key, JsonValue^ o)
        {
            BuildConfiguration^ c = gcnew BuildConfiguration(ConfigName::ConfigPart(key),
                                                             TargetInfo::Parse(ConfigName::TargetPart(key)));
            if (o == nullptr || !o->IsObject) return c;
            c->OutDir = o->GetString("outDir", c->OutDir);
            c->ObjDir = o->GetString("objDir", c->ObjDir);
            if (o->Has("defines"))     c->Defines = o->GetStringList("defines");
            if (o->Has("includeDirs")) c->IncludeDirs = o->GetStringList("includeDirs");
            if (o->Has("libDirs"))     c->LibDirs = o->GetStringList("libDirs");
            if (o->Has("libs"))        c->Libs = o->GetStringList("libs");
            c->Optimise = o->GetString("optimise", c->Optimise);
            c->LinkerCmd = o->GetString("linkerCmd", c->LinkerCmd);
            c->ExtraCompileFlags = o->GetString("compileFlags", String::Empty);
            c->ExtraLinkFlags = o->GetString("linkFlags", String::Empty);
            c->OutputName = o->GetString("outputName", String::Empty);
            return c;
        }
    };
}}
