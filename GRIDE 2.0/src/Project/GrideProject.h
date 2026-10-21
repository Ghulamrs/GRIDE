#pragma once
// ============================================================================
//  GRIDE 2.0 - Project system (Phase 4)
//  A project is one JSON ".pro" file. Paths inside it are relative to the
//  folder that holds the .pro file.
//
//  **One .pro format for the three outlooks** (2026-10-20): White's, which is
//  RIDE's - "groups" (each a list of files, or { files, toolchain }),
//  "include", "libraries", "arch", "build" { target, groups, args } - with the
//  keys only GRIDE 2.0 has beside them (language, buildConfigurations,
//  activeConfiguration), which White and GRIDEd leave alone. GRIDEd's
//  "configurations" (an object, "Debug|Windows": {...}) is theirs, kept as read. Read with Json.h, which
//  takes RIDE's // comments. A .pro of GRIDE 2.0's own form before it
//  (formatVersion, sourceFiles) is read as it was and saved in the one
//  format; one standing for a Visual Studio project keeps that form.
// ============================================================================
#include "../Settings/IdeSettings.h"
#include "../Common/Json.h"
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

        // ---- the one format's groups (runtime state, written by Save in that format): the groups in order, each
        // file's group as SourceFiles names the file, a group's own compiler word, and the groups the program is
        // built from ("build" "groups"; nullptr: every one)
        property List<String^>^ Groups;
        property Dictionary<String^, String^>^ GroupOf;
        property Dictionary<String^, String^>^ GroupToolchains;
        property List<String^>^ BuildGroups;
        property bool HasGroups { bool get() { return Groups != nullptr && Groups->Count > 0; } }

        String^ GroupOfFile(String^ rel)
        {
            String^ g;
            if (GroupOf != nullptr && GroupOf->TryGetValue(rel, g)) return g;
            return GroupForNew(rel);
        }

        // the compiler word in force for a file: its group's own, else the project's (White's group "toolchain")
        String^ ToolchainFor(String^ full)
        {
            int i = IndexOfFile(full);
            String^ g; String^ w;
            if (i >= 0 && GroupOf != nullptr && GroupToolchains != nullptr && GroupOf->TryGetValue(SourceFiles[i], g) &&
                GroupToolchains->TryGetValue(g, w)) return w;
            return Toolchain;
        }

        // a file goes into the program when its group is one the build names (every group when it names none)
        bool IsBuilt(String^ rel)
        {
            String^ g;
            if (BuildGroups == nullptr || GroupOf == nullptr || !GroupOf->TryGetValue(rel, g)) return true;
            for each (String^ b in BuildGroups) if (String::Equals(b, g, StringComparison::OrdinalIgnoreCase)) return true;
            return false;
        }

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
            String^ rel = RelativePath(full);
            SourceFiles->Add(rel);
            if (HasGroups)
            {
                String^ g = GroupForNew(rel);
                if (!Groups->Contains(g))
                {
                    Groups->Add(g);
                    // a new group of sources is built, as every file a GRIDE 2.0 project is given
                    if (BuildGroups != nullptr && IsSourceName(rel)) BuildGroups->Add(g);
                }
                GroupOf[rel] = g;
            }
            return true;
        }

        bool RemoveFile(String^ full)
        {
            int i = IndexOfFile(full);
            if (i < 0) return false;
            if (GroupOf != nullptr) GroupOf->Remove(SourceFiles[i]);
            SourceFiles->RemoveAt(i);
            return true;
        }

        bool RenameFile(String^ oldFull, String^ newFull)
        {
            int i = IndexOfFile(oldFull);
            if (i < 0) return false;
            String^ g;
            if (GroupOf != nullptr && GroupOf->TryGetValue(SourceFiles[i], g)) { GroupOf->Remove(SourceFiles[i]); GroupOf[RelativePath(newFull)] = g; }
            SourceFiles[i] = RelativePath(newFull);
            return true;
        }

        // the group a file with none goes into: the group of a file of its kind in its own folder, else of any
        // file of its kind, else White's own names, Sources and Headers
        String^ GroupForNew(String^ rel)
        {
            bool header = IsHeaderName(rel);
            String^ dir = DirOf(rel);
            String^ sameKind = nullptr;
            if (GroupOf != nullptr)
                for each (String^ f in SourceFiles)
                {
                    String^ g;
                    if (!GroupOf->TryGetValue(f, g) || IsHeaderName(f) != header) continue;
                    if (String::Equals(DirOf(f), dir, StringComparison::OrdinalIgnoreCase)) return g;
                    if (sameKind == nullptr) sameKind = g;
                }
            if (sameKind != nullptr) return sameKind;
            return header ? Str("Headers") : Str("Sources");
        }
        static String^ DirOf(String^ rel) { String^ d = Path::GetDirectoryName(rel->Replace(L'/', L'\\')); return d == nullptr ? String::Empty : d; }
        // by the name alone, as the one format's groups are kept (Toolchain.h's TargetInfo includes this file)
        static bool IsHeaderName(String^ rel)
        {
            String^ e = Path::GetExtension(rel)->ToLowerInvariant();
            return e == ".h" || e == ".hpp" || e == ".hh" || e == ".hxx" || e == ".inc" || e == ".inl";
        }
        static bool IsSourceName(String^ rel)
        {
            String^ e = Path::GetExtension(rel)->ToLowerInvariant();
            return e == ".c" || e == ".cpp" || e == ".cc" || e == ".cxx" || e == ".shl" || e == ".asm" || e == ".s";
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
            JsonValue^ j = JsonParser::ParseFile(path);
            if (j == nullptr || !j->IsObject) throw gcnew InvalidDataException("The project file is empty.");
            // GRIDE 2.0's own form before the one format, and a .pro standing for a Visual Studio project
            GrideProject^ p = j->Has("formatVersion") && !j->Has("groups") ? JsonLoad<GrideProject>(path) : FromOneFormat(j, path);
            if (p == nullptr) throw gcnew InvalidDataException("The project file is empty.");
            p->raw = j;
            p->FilePath = Path::GetFullPath(path);
            p->IsReadOnly = false;
            p->Normalize();
            return p;
        }

        void Save()
        {
            if (IsReadOnly) throw gcnew InvalidOperationException("Imported CCS projects are read-only.");
            Normalize();
            if (!String::IsNullOrWhiteSpace(VisualStudio)) { JsonSave<GrideProject>(FilePath, this); return; }
            JsonValue^ j = ToOneFormat();
            JsonWriter::WriteFile(FilePath, j);
            raw = j;
        }

        // ---- the one format ------------------------------------------------------------------------------------
        static GrideProject^ FromOneFormat(JsonValue^ j, String^ path)
        {
            GrideProject^ p = gcnew GrideProject();
            p->Name = j->GetString("name", Path::GetFileNameWithoutExtension(path));
            p->Toolchain = j->GetString("toolchain", "auto");
            p->Target = Names::TargetName(TargetOfArch(j->GetString("arch", String::Empty)));
            p->SourceFiles = gcnew List<String^>();
            p->Groups = gcnew List<String^>();
            p->GroupOf = gcnew Dictionary<String^, String^>(StringComparer::OrdinalIgnoreCase);
            p->GroupToolchains = gcnew Dictionary<String^, String^>(StringComparer::OrdinalIgnoreCase);
            JsonValue^ groups = j->GetObject("groups");
            if (groups != nullptr)
                for each (String^ g in groups->KeyNames)
                {
                    JsonValue^ e = groups->Get(g);
                    List<String^>^ files = e != nullptr && e->IsObject ? e->GetStringList("files") : groups->GetStringList(g);
                    if (e != nullptr && e->IsObject)
                    {
                        String^ w = e->GetString("toolchain", String::Empty);
                        if (!String::IsNullOrWhiteSpace(w) && !String::Equals(w, "auto", StringComparison::OrdinalIgnoreCase)) p->GroupToolchains[g] = w;
                    }
                    p->Groups->Add(g);
                    for each (String^ f in files)
                    {
                        if (String::IsNullOrWhiteSpace(f) || p->GroupOf->ContainsKey(f)) continue;
                        p->SourceFiles->Add(f);
                        p->GroupOf[f] = g;
                    }
                }
            p->IncludePaths = j->GetStringList("include");
            p->Libraries = j->GetStringList("libraries");
            p->EmulateOnly = j->GetBool("emulateOnly", false);
            JsonValue^ o = j->GetObject("options");
            if (o != nullptr)
            {
                p->Options = gcnew Dictionary<String^, Dictionary<String^, String^>^>();
                for each (String^ c in o->KeyNames)
                {
                    JsonValue^ one = o->Get(c);
                    if (one == nullptr || !one->IsObject) continue;
                    Dictionary<String^, String^>^ d = gcnew Dictionary<String^, String^>();
                    for each (String^ k in one->KeyNames) { String^ v = one->Get(k)->AsString(); if (v != nullptr) d[k] = v; }
                    p->Options[c] = d;
                }
            }
            JsonValue^ build = j->GetObject("build");
            if (build != nullptr)
            {
                p->OutputName = build->GetString("target", p->Name);
                if (build->Has("groups")) p->BuildGroups = build->GetStringList("groups");
                if (build->Has("args")) p->ProgramArgs = build->GetStringList("args");
            }
            // White's rule for a .pro with no "build" at all: its Sources group is the program
            else if (!j->Has("build") && p->Groups->Contains("Sources"))
            {
                p->BuildGroups = gcnew List<String^>();
                p->BuildGroups->Add("Sources");
            }
            // GRIDE 2.0's own, beside the one format
            p->Language = j->Has("language") ? j->GetString("language", "C90") : Names::LanguageName(LanguageOfFiles(p->SourceFiles));
            // "buildConfigurations"; a list under "configurations" is how GRIDE 2.0 wrote them before (GRIDEd's is an object)
            JsonValue^ cfgs = j->Get("buildConfigurations");
            if (cfgs == nullptr || !cfgs->IsArray) cfgs = j->Get("configurations");
            if (cfgs != nullptr && cfgs->IsArray)
            {
                p->Configurations = gcnew List<BuildConfiguration^>();
                for (int i = 0; i < cfgs->Count; i++)
                {
                    JsonValue^ c = cfgs->At(i);
                    if (c == nullptr || !c->IsObject) continue;
                    BuildConfiguration^ b = gcnew BuildConfiguration(c->GetString("name", "Debug"));
                    b->Defines = c->GetStringList("defines");
                    b->Flags = c->GetString("flags", String::Empty);
                    b->LinkFlags = c->GetString("linkFlags", String::Empty);
                    b->OutputDir = c->GetString("outputDir", String::Empty);
                    b->AsmFlags = c->GetString("asmFlags", String::Empty);
                    b->Normalize();
                    p->Configurations->Add(b);
                }
            }
            p->ActiveConfiguration = j->GetString("activeConfiguration", nullptr);
            // no groups at all: nothing to keep apart, and every file the project is given is built
            if (p->Groups->Count == 0) { p->Groups = nullptr; p->GroupOf = nullptr; p->BuildGroups = nullptr; }
            return p;
        }

        JsonValue^ ToOneFormat()
        {
            // the file as it was read, so what only White or a later GRIDE writes ("indent", "tabs", "open") survives
            JsonValue^ j = raw != nullptr && raw->IsObject ? raw : JsonValue::MakeObject();
            bool saidLanguage = j->Has("language"), saidActive = j->Has("activeConfiguration");
            // a list under "configurations" was GRIDE 2.0's own spelling; an object there is GRIDEd's, and stays
            JsonValue^ was = j->Get("configurations");
            if (was != nullptr && was->IsArray) j->Remove("configurations");
            // GRIDE 2.0's own spellings before the one format, said in its words below
            for each (String^ k in gcnew array<String^> { "formatVersion", "sourceFiles", "includePaths", "outputName", "programArgs", "target", "visualStudio" }) j->Remove(k);

            j->SetString("name", Name);
            j->SetString("toolchain", String::IsNullOrWhiteSpace(Toolchain) ? Str("auto") : Toolchain);
            // the platform: the arch as written while it still means this target (mathx-macos.pro stays arm64-darwin).
            // **A project that names none and has not been given another target writes none** (2026-10-21), as Black
            // and White do: to Black no arch means any target, and writing Windows here pinned it.
            String^ arch = j->GetString("arch", String::Empty);
            if (arch->Length == 0 && TargetValue == TargetKind::Windows) j->Remove("arch");
            else
            {
                if (arch->Length == 0 || TargetOfArch(arch) != TargetValue) arch = ArchOf(TargetValue);
                j->SetString("arch", arch);
            }

            List<String^>^ order = Groups != nullptr ? gcnew List<String^>(Groups) : gcnew List<String^>();
            Dictionary<String^, List<String^>^>^ members = gcnew Dictionary<String^, List<String^>^>(StringComparer::OrdinalIgnoreCase);
            for each (String^ rel in SourceFiles)
            {
                String^ g = GroupOfFile(rel);
                if (!order->Contains(g)) order->Add(g);
                if (!members->ContainsKey(g)) members[g] = gcnew List<String^>();
                members[g]->Add(rel->Replace(L'\\', L'/'));
            }
            JsonValue^ groups = JsonValue::MakeObject();
            for each (String^ g in order)
            {
                List<String^>^ files = members->ContainsKey(g) ? members[g] : gcnew List<String^>();
                String^ w;
                if (GroupToolchains != nullptr && GroupToolchains->TryGetValue(g, w))
                {
                    JsonValue^ e = JsonValue::MakeObject();
                    e->SetList("files", files);
                    e->SetString("toolchain", w);
                    groups->SetValue(g, e);
                }
                else groups->SetList(g, files);
            }
            j->SetValue("groups", groups);
            if (IncludePaths->Count > 0) j->SetList("include", IncludePaths); else j->Remove("include");
            if (Libraries->Count > 0) j->SetList("libraries", Libraries); else j->Remove("libraries");
            if (EmulateOnly) j->SetBool("emulateOnly", true); else j->Remove("emulateOnly");
            if (Options != nullptr && Options->Count > 0)
            {
                JsonValue^ o = JsonValue::MakeObject();
                for each (KeyValuePair<String^, Dictionary<String^, String^>^> c in Options)
                {
                    JsonValue^ one = JsonValue::MakeObject();
                    for each (KeyValuePair<String^, String^> kv in c.Value) one->SetString(kv.Key, kv.Value);
                    o->SetValue(c.Key, one);
                }
                j->SetValue("options", o);
            }
            else j->Remove("options");
            JsonValue^ build = JsonValue::MakeObject();
            build->SetString("target", String::IsNullOrWhiteSpace(OutputName) ? Name : OutputName->Trim());
            // the groups named, else (a project not grouped when it was read) every group with a source in it
            List<String^>^ built = HasGroups ? BuildGroups : nullptr;
            if (built == nullptr)
            {
                built = gcnew List<String^>();
                for each (String^ g in order)
                    if (members->ContainsKey(g))
                        for each (String^ f in members[g]) if (IsSourceName(f)) { built->Add(g); break; }
            }
            build->SetList("groups", built);
            if (ProgramArgs != nullptr && ProgramArgs->Count > 0) build->SetList("args", ProgramArgs);
            j->SetValue("build", build);

            // ---- GRIDE 2.0's own, which the one format has no word for; each written only when it says something
            if (saidLanguage || LanguageValue != LanguageOfFiles(SourceFiles)) j->SetString("language", Language);
            if (!DefaultConfigurations())
            {
                JsonValue^ cfgs = JsonValue::MakeArray();
                for each (BuildConfiguration^ c in Configurations)
                {
                    JsonValue^ one = JsonValue::MakeObject();
                    one->SetString("name", c->Name);
                    one->SetList("defines", c->Defines);
                    one->SetString("flags", c->Flags);
                    one->SetString("linkFlags", c->LinkFlags);
                    one->SetString("outputDir", c->OutputDir);
                    one->SetString("asmFlags", c->AsmFlags);
                    cfgs->Add(one);
                }
                j->SetValue("buildConfigurations", cfgs);
            }
            else j->Remove("buildConfigurations");
            if (saidActive || !String::Equals(ActiveConfiguration, Configurations[0]->Name, StringComparison::OrdinalIgnoreCase))
                j->SetString("activeConfiguration", ActiveConfiguration);
            return j;
        }

        // Debug with _DEBUG and Release with NDEBUG, nothing else - what Normalize makes for a project without any
        bool DefaultConfigurations()
        {
            if (Configurations->Count != 2) return false;
            array<String^>^ names = { "Debug", "Release" };
            array<String^>^ defines = { "_DEBUG", "NDEBUG" };
            for (int i = 0; i < 2; i++)
            {
                BuildConfiguration^ c = Configurations[i];
                if (!String::Equals(c->Name, names[i]) || c->Defines->Count != 1 || !String::Equals(c->Defines[0], defines[i]) ||
                    c->Flags->Length > 0 || c->LinkFlags->Length > 0 || c->AsmFlags->Length > 0 ||
                    !String::Equals(c->OutputDir, "build\\$(Target)\\$(Config)")) return false;
            }
            return true;
        }

        // "arch", as toolchain.json names the targets; this IDE runs on Windows, so another machine's host is this one's
        static TargetKind TargetOfArch(String^ arch)
        {
            if (String::IsNullOrWhiteSpace(arch)) return TargetKind::Windows;
            String^ a = arch->Trim()->ToLowerInvariant();
            if (a->Contains("6747") || a->StartsWith("tms")) return TargetKind::TMS6747;
            if (a->EndsWith("-windows") || a->EndsWith("-linux") || a->EndsWith("-darwin")) return TargetKind::Windows;
            return Names::ParseTarget(arch);
        }
        static String^ ArchOf(TargetKind k) { return k == TargetKind::TMS6747 ? Str("tms6747") : Str("x86_64-windows"); }
        // the language of a project that does not say one: C++ when a file is C++, else Shalimar when one is, else C
        static LanguageKind LanguageOfFiles(List<String^>^ files)
        {
            bool shalimar = false;
            for each (String^ f in files)
            {
                String^ e = Path::GetExtension(f)->ToLowerInvariant();
                if (e == ".cpp" || e == ".cc" || e == ".cxx" || e == ".hpp" || e == ".hh") return LanguageKind::Cpp11;
                if (e == ".shl") shalimar = true;
            }
            return shalimar ? LanguageKind::Shalimar : LanguageKind::C90;
        }

    private:
        JsonValue^ raw;
        static bool IsNullConfig(BuildConfiguration^ c) { return c == nullptr; }
    };
}
