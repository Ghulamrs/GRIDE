#pragma once
// ============================================================================
//  GRIDE 2.0 - Project system (Phase 4)
//  A project is one JSON ".pro" file. Paths inside it are relative to the
//  folder that holds the .pro file.
// ============================================================================
#include "../Settings/IdeSettings.h"
#include "../Import/CcsReading.h"
#include "../Import/VsSolution.h"

namespace Gride
{
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

        // ---- what a project read from another IDE carries per configuration (a CCS project's; not saved)
        property List<String^>^ IncludePathsHere;      // after the project's include folders
        property List<String^>^ LibrariesHere;         // after the project's libraries
        property List<String^>^ LibraryDirs;           // the linker's search paths ($(LibDirFlags))
        property List<String^>^ ExcludedFiles;         // full paths this configuration does not build
        property List<String^>^ LinkerCommandFiles;    // the configuration's own; nullptr: the project's .cmd files
        property Dictionary<String^, String^>^ ConfigFlagsByRole;   // a compiler role's configuration flags instead of the toolchain's (cCompiler, cppCompiler); nullptr: the toolchain's
        property String^ Optimise;                     // $(Optimise): a Visual Studio configuration's -O word (O0 .. O3); nullptr: by the configuration's kind
        property VsProjectConfig^ VsConfig;            // the Visual Studio configuration it is built from (its ClCompile settings go to cl), or nullptr

        BuildConfiguration() { Normalize(); }
        BuildConfiguration(String^ name) { Normalize(); Name = name; }

        bool Excludes(String^ full)
        {
            if (ExcludedFiles == nullptr) return false;
            for each (String^ e in ExcludedFiles) if (PathUtil::SamePath(e, full)) return true;
            return false;
        }

        void Normalize()
        {
            if (String::IsNullOrWhiteSpace(Name)) Name = "Debug";
            if (Defines == nullptr) Defines = gcnew List<String^>();
            if (Flags == nullptr) Flags = String::Empty;
            if (LinkFlags == nullptr) LinkFlags = String::Empty;
            if (AsmFlags == nullptr) AsmFlags = String::Empty;
            if (String::IsNullOrWhiteSpace(OutputDir)) OutputDir = "build\\$(Target)\\$(Config)";
            if (IncludePathsHere == nullptr) IncludePathsHere = gcnew List<String^>();
            if (LibrariesHere == nullptr) LibrariesHere = gcnew List<String^>();
            if (LibraryDirs == nullptr) LibraryDirs = gcnew List<String^>();
            if (ExcludedFiles == nullptr) ExcludedFiles = gcnew List<String^>();
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

    // what a project read from Visual Studio carries (R08; not saved)
    public ref class VsInfo
    {
    public:
        VsInfo()
        {
            SolutionPath = String::Empty; SolutionFolder = String::Empty; Version = String::Empty; Label = String::Empty; Toolset = String::Empty; Guid = String::Empty;
            Configurations = gcnew List<String^>(); MappedConfigs = gcnew Dictionary<String^, String^>(); ReportLines = gcnew List<String^>();
            FolderOf = gcnew Dictionary<String^, String^>(StringComparer::OrdinalIgnoreCase); Reading = nullptr; StateFile = String::Empty;
        }
        property String^ SolutionPath;           // the .sln it was read with, or ""
        property String^ SolutionFolder;         // the solution folder it sits in ("Libs/Inner"), or ""
        property String^ Version;                // "Visual Studio 2022, ToolsVersion Current, toolset v143"
        property String^ Label;                  // "Visual Studio 2022 v143"
        property String^ Toolset;
        property String^ Guid;
        property List<String^>^ Configurations;  // the file's own: Debug|x64 ...
        property Dictionary<String^, String^>^ MappedConfigs;   // GRIDE's Debug / Release -> the file's configuration built from
        property List<String^>^ ReportLines;     // what was read and what is not mapped, for the Output pane
        property Dictionary<String^, String^>^ FolderOf;        // a file as SourceFiles has it -> the folder the tree shows it in (its filter)
        property String^ StateFile;              // the .pro naming it, which keeps GRIDE's state for it (R12), or "": the settings keep it
        property VsProject^ Reading;             // the reading behind it, for the mapping dialog
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
        // RIDE's toolchain word: auto (each file's own compiler), c90 / cc1, cpp11 / cxx1, shalimar / shc
        [DataMember(Name = "toolchain", Order = 10)]          property String^ Toolchain;
        // RIDE's emulateOnly: a C6747 build stops at the assembly, which vm6747 runs - no asm6x, no lnk6x, no .out (written only when true)
        [DataMember(Name = "emulateOnly", Order = 11, EmitDefaultValue = false)] property bool EmulateOnly;
        // the program's arguments for a run, each one whole (written only when there are some)
        [DataMember(Name = "programArgs", Order = 12, EmitDefaultValue = false)] property List<String^>^ ProgramArgs;

        // RIDE's Compiler Options of the project, as RIDE's .pro has them ("options": { "debug": {...}, "release": {...} },
        // the differences from the defaults only; written only when there are some)
        [DataMember(Name = "options", Order = 13, EmitDefaultValue = false)] property Dictionary<String^, Dictionary<String^, String^>^>^ Options;
        // a Visual Studio solution or project the .pro stands for (R12, written only when there is one): the project
        // is read from it, read-only, and the .pro keeps GRIDE's state for it - the compiler, the configuration,
        // the program's arguments (GRIDEd's .grws entry of kind vs, in GRIDE 2.0's one-project form)
        [DataMember(Name = "visualStudio", Order = 14, EmitDefaultValue = false)] property String^ VisualStudio;

        List<String^>^ Arguments() { return ProgramArgs == nullptr ? gcnew List<String^>() : ProgramArgs; }
        CompilerOptionStore^ OptionStore() { return CompilerOptionStore::FromData(Options); }
        void SetOptionStore(CompilerOptionStore^ store) { Options = store == nullptr ? nullptr : store->ToData(); }

        // ---- runtime-only state (not saved)
        property String^ FilePath;
        property bool IsReadOnly;                              // true for imported CCS 7.4 projects
        property List<CcsConfiguration^>^ CcsConfigurations;
        property CcsReading^ Ccs;                              // a CCS project's reading (read afresh at every open and build), or nullptr
        property List<String^>^ CcsReportLines;                // RIDE's Messages lines about it, for the Output pane
        property bool IsCcs { bool get() { return Ccs != nullptr; } }
        property VsInfo^ Vs;                                   // a Visual Studio project's reading and report (R08), or nullptr
        property bool IsVs { bool get() { return Vs != nullptr; } }
        property bool IsImported { bool get() { return IsCcs || IsVs; } }   // read from another IDE: read-only, built, its state in the settings
        List<String^>^ ImportReportLines() { return IsVs ? Vs->ReportLines : CcsReportLines; }

        // where a CCS project's program goes: the temporary directory, never the CCS folder (RIDE's ride-ccs-<name>)
        static String^ CcsBuildRoot(String^ projectName)
        {
            StringBuilder^ safe = gcnew StringBuilder();
            for each (wchar_t c in projectName) safe->Append(Char::IsLetterOrDigit(c) || c == L'-' || c == L'_' ? c : L'_');
            return Path::Combine(Path::GetTempPath(), String::Concat("gride-ccs-", safe->ToString()));
        }

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
            if (CcsReportLines == nullptr) CcsReportLines = gcnew List<String^>();
            Toolchain = ToolWords::Canonical(Toolchain);
            if (ProgramArgs != nullptr && ProgramArgs->Count == 0) ProgramArgs = nullptr;
            if (Options != nullptr && Options->Count == 0) Options = nullptr;
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

        // a new project as RIDE starts one: the compiler and the Compiler Options the settings name (RIDE's
        // settings.json compiler and options)
        static GrideProject^ Create(String^ name, String^ folder, LanguageKind lang, TargetKind target, IdeSettings^ s)
        {
            GrideProject^ p = Create(name, folder, lang, target);
            if (s == nullptr) return p;
            p->Toolchain = s->DefaultCompiler;
            p->SetOptionStore(s->OptionStore());
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
