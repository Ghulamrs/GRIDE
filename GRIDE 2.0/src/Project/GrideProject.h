#pragma once
// ============================================================================
//  GRIDE 2.0 - Project system (Phase 4)
//  A project is one JSON ".pro" file. Paths inside it are relative to the
//  folder that holds the .pro file.
// ============================================================================
#include "../Settings/IdeSettings.h"

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

        static array<String^>^ LanguageNames() { return gcnew array<String^>{ "C90", "C++11", "Shalimar" }; }
        static array<String^>^ TargetNames()   { return gcnew array<String^>{ "Windows", "Linux", "macOS", "TMS6747" }; }
    };

    [DataContract(Name = "configuration", Namespace = "")]
    public ref class BuildConfiguration
    {
    public:
        [DataMember(Name = "name", Order = 0)]      property String^ Name;
        [DataMember(Name = "defines", Order = 1)]   property List<String^>^ Defines;
        [DataMember(Name = "flags", Order = 2)]     property String^ Flags;
        [DataMember(Name = "linkFlags", Order = 3)] property String^ LinkFlags;
        [DataMember(Name = "outputDir", Order = 4)] property String^ OutputDir;   // may use $(Target) $(Config) $(Name)
        [DataMember(Name = "asmFlags", Order = 5)]  property String^ AsmFlags;    // {asmflags}

        BuildConfiguration() { Normalize(); }
        BuildConfiguration(String^ name) { Normalize(); Name = name; }

        void Normalize()
        {
            if (String::IsNullOrWhiteSpace(Name)) Name = "Debug";
            if (Defines == nullptr) Defines = gcnew List<String^>();
            if (Flags == nullptr) Flags = String::Empty;
            if (LinkFlags == nullptr) LinkFlags = String::Empty;
            if (AsmFlags == nullptr) AsmFlags = String::Empty;
            if (String::IsNullOrWhiteSpace(OutputDir)) OutputDir = "build\\$(Target)\\$(Config)";
        }
    };

    // Read-only information shown for imported CCS projects (not serialized).
    public ref class CcsConfiguration
    {
    public:
        property String^ Name;
        property List<String^>^ Options;
        CcsConfiguration() { Options = gcnew List<String^>(); }
    };

    [DataContract(Name = "project", Namespace = "")]
    public ref class GrideProject
    {
    public:
        literal int CurrentFormatVersion = 2;

        [DataMember(Name = "formatVersion", Order = 0)]       property int FormatVersion;
        [DataMember(Name = "name", Order = 1)]                property String^ Name;
        [DataMember(Name = "language", Order = 2)]            property String^ Language;
        [DataMember(Name = "target", Order = 3)]              property String^ Target;
        [DataMember(Name = "outputName", Order = 4)]          property String^ OutputName;
        [DataMember(Name = "sourceFiles", Order = 5)]         property List<String^>^ SourceFiles;
        [DataMember(Name = "includePaths", Order = 6)]        property List<String^>^ IncludePaths;
        [DataMember(Name = "libraries", Order = 7)]           property List<String^>^ Libraries;
        [DataMember(Name = "activeConfiguration", Order = 8)] property String^ ActiveConfiguration;
        [DataMember(Name = "configurations", Order = 9)]      property List<BuildConfiguration^>^ Configurations;

        // ---- runtime-only state (not saved)
        property String^ FilePath;
        property bool IsReadOnly;                              // true for imported CCS 7.4 projects
        property List<CcsConfiguration^>^ CcsConfigurations;

        property LanguageKind LanguageValue { LanguageKind get() { return Names::ParseLanguage(Language); } }
        property TargetKind TargetValue     { TargetKind get()   { return Names::ParseTarget(Target); } }
        property String^ ProjectDir         { String^ get()      { return Path::GetDirectoryName(Path::GetFullPath(FilePath)); } }

        GrideProject() { Normalize(); }

        void Normalize()
        {
            FormatVersion = CurrentFormatVersion;
            if (String::IsNullOrWhiteSpace(Name)) Name = "Project";
            Language = Names::LanguageName(Names::ParseLanguage(Language));
            Target = Names::TargetName(Names::ParseTarget(Target));
            if (String::IsNullOrWhiteSpace(OutputName)) OutputName = Name;
            if (SourceFiles == nullptr) SourceFiles = gcnew List<String^>();
            if (IncludePaths == nullptr) IncludePaths = gcnew List<String^>();
            if (Libraries == nullptr) Libraries = gcnew List<String^>();
            if (Configurations == nullptr) Configurations = gcnew List<BuildConfiguration^>();
            Configurations->RemoveAll(gcnew Predicate<BuildConfiguration^>(&GrideProject::IsNullConfig));
            for each (BuildConfiguration^ c in Configurations) c->Normalize();
            if (Configurations->Count == 0)
            {
                BuildConfiguration^ dbg = gcnew BuildConfiguration("Debug");
                dbg->Defines->Add("_DEBUG");
                BuildConfiguration^ rel = gcnew BuildConfiguration("Release");
                rel->Defines->Add("NDEBUG");
                Configurations->Add(dbg);
                Configurations->Add(rel);
            }
            if (FindConfig(ActiveConfiguration) == nullptr) ActiveConfiguration = Configurations[0]->Name;
            if (CcsConfigurations == nullptr) CcsConfigurations = gcnew List<CcsConfiguration^>();
        }

        BuildConfiguration^ FindConfig(String^ name)
        {
            if (name == nullptr || Configurations == nullptr) return nullptr;
            for each (BuildConfiguration^ c in Configurations)
                if (String::Equals(c->Name, name, StringComparison::OrdinalIgnoreCase)) return c;
            return nullptr;
        }

        BuildConfiguration^ ActiveConfig()
        {
            BuildConfiguration^ c = FindConfig(ActiveConfiguration);
            return c != nullptr ? c : Configurations[0];
        }

        String^ ResolvePath(String^ rel) { return PathUtil::Resolve(ProjectDir, rel); }
        String^ RelativePath(String^ full) { return PathUtil::MakeRelative(ProjectDir, full); }

        int IndexOfFile(String^ full)
        {
            for (int i = 0; i < SourceFiles->Count; i++)
                if (PathUtil::SamePath(ResolvePath(SourceFiles[i]), full)) return i;
            return -1;
        }

        bool ContainsFile(String^ full) { return IndexOfFile(full) >= 0; }

        bool AddFile(String^ full)
        {
            if (ContainsFile(full)) return false;
            SourceFiles->Add(RelativePath(full));
            return true;
        }

        bool RemoveFile(String^ full)
        {
            int i = IndexOfFile(full);
            if (i < 0) return false;
            SourceFiles->RemoveAt(i);
            return true;
        }

        bool RenameFile(String^ oldFull, String^ newFull)
        {
            int i = IndexOfFile(oldFull);
            if (i < 0) return false;
            SourceFiles[i] = RelativePath(newFull);
            return true;
        }

        // Output folder of the active configuration (absolute).
        String^ OutputDirectory()
        {
            BuildConfiguration^ c = ActiveConfig();
            String^ d = c->OutputDir
                ->Replace("$(Target)", Target)
                ->Replace("$(Config)", c->Name)
                ->Replace("$(Name)", Name);
            return PathUtil::Resolve(ProjectDir, d);
        }

        // ---------------------------------------------------------------
        static GrideProject^ Create(String^ name, String^ folder, LanguageKind lang, TargetKind target)
        {
            GrideProject^ p = gcnew GrideProject();
            p->Name = name;
            p->OutputName = name;
            p->Language = Names::LanguageName(lang);
            p->Target = Names::TargetName(target);
            p->FilePath = Path::Combine(folder, String::Concat(name, ".pro"));
            p->Normalize();
            return p;
        }

        static GrideProject^ Load(String^ path)
        {
            GrideProject^ p = JsonLoad<GrideProject>(path);
            if (p == nullptr) throw gcnew InvalidDataException("The project file is empty.");
            p->FilePath = Path::GetFullPath(path);
            p->IsReadOnly = false;
            p->Normalize();
            return p;
        }

        void Save()
        {
            if (IsReadOnly) throw gcnew InvalidOperationException("Imported CCS projects are read-only.");
            Normalize();
            JsonSave<GrideProject>(FilePath, this);
        }

    private:
        static bool IsNullConfig(BuildConfiguration^ c) { return c == nullptr; }
    };
}
