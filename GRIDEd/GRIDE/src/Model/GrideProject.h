// =============================================================================
//  GrideProject.h - one project (.pro JSON file)
//
//  Layout on disk ("one step difference"):
//      Project1\Project1.pro        <- this file
//      Project1\src\*.c             <- sources, stored as "src/main.c"
//      Project1\inc\*.h
//  A read-only project imported from Code Composer Studio 7.4 uses the same
//  class (Kind = "ccs7", IsReadOnly = true) - see CcsProjectReader.h.
//
//  **One .pro format for the three outlooks** (2026-10-20): White's, which is
//  RIDE's - "groups" (a folder each, a list of files or { files, toolchain }),
//  "include", "libraries", "arch", "build" { target, groups, args } - and the
//  keys only GRIDEd has beside them (configurations, excludeFromBuild,
//  dependsOn, language, template, targets), which White leaves alone. A .pro
//  GRIDEd wrote before ("folders", "folderToolchains", "programArgs") is read
//  and saved in the one format.
// =============================================================================
#pragma once
#include "../Core/Json.h"
#include "../Core/PathUtil.h"
#include "../Core/LanguageTarget.h"
#include "BuildConfiguration.h"
#include "CcsReading.h"
#include "VsSolution.h"
#include "../Settings/CompilerOptions.h"

namespace Gride { namespace Model {

    using namespace Gride::Core;

    // A named group of files shown as a folder in Project Explorer
    public ref class ProjectFolder
    {
    public:
        ProjectFolder(String^ name) { Name = name; Files = gcnew List<String^>(); }
        property String^ Name;
        property List<String^>^ Files;      // relative paths with '/'
    };

    // Extra information read from a CCS 7.4 project
    public ref class CcsInfo
    {
    public:
        CcsInfo()
        {
            DeviceVariant = String::Empty;  DeviceFamily = String::Empty;
            CodegenVersion = String::Empty; OutputFormat = String::Empty;
            LinkerCommandFile = String::Empty; Workspace = String::Empty;
            ReportLines = gcnew List<String^>(); Reading = nullptr;
        }
        property String^ DeviceVariant;
        property String^ DeviceFamily;
        property String^ CodegenVersion;
        property String^ OutputFormat;
        property String^ LinkerCommandFile;
        property String^ Workspace;              // the CCS workspace the project is registered in, or ""
        property List<String^>^ ReportLines;     // RIDE's Messages lines for the Output pane
        property CcsReading^ Reading;            // the reading behind it, for the mapping dialog
    };

    // Extra information read from a Visual Studio project (VsProjectReader.h)
    public ref class VsInfo
    {
    public:
        VsInfo()
        {
            SolutionPath = String::Empty; SolutionFolder = String::Empty; Version = String::Empty; Toolset = String::Empty; Guid = String::Empty;
            Configurations = gcnew List<String^>(); MappedConfigs = gcnew Dictionary<String^, String^>();
            ReportLines = gcnew List<String^>(); Reading = nullptr;
        }
        property String^ SolutionPath;           // the .sln it was opened from, or ""
        property String^ SolutionFolder;         // the solution folder it sits in ("Libs/Inner"), or ""
        property String^ Version;                // "Visual Studio 2022, ToolsVersion Current, toolset v143"
        property String^ Toolset;
        property String^ Guid;
        property List<String^>^ Configurations;  // the file's own: Debug|x64 ...
        property Dictionary<String^, String^>^ MappedConfigs;   // GRIDE's Debug / Release -> the file's configuration built from
        property List<String^>^ ReportLines;     // what was read and what is not mapped, for the Output pane
        property VsProject^ Reading;             // the reading behind it, for the mapping dialog
    };

    public ref class GrideProject
    {
    public:
        GrideProject()
        {
            Name = "Project";
            Lang = Language::C90;
            Template = "Empty";
            Targets = gcnew List<Target>();
            Folders = gcnew List<ProjectFolder^>();
            Configurations = gcnew List<BuildConfiguration^>();
            DependsOn = gcnew List<String^>();
            ExcludedFiles = gcnew List<String^>();
            ExcludedByConfig = gcnew Dictionary<String^, List<String^>^>(StringComparer::OrdinalIgnoreCase);
            Toolchain = "auto";
            EmulateOnly = false;
            ProgramArgs = gcnew List<String^>();
            Options = gcnew CompilerOptionStore();
            FolderToolchains = gcnew Dictionary<String^, String^>(StringComparer::OrdinalIgnoreCase);
            Includes = gcnew List<String^>();
            Libraries = gcnew List<String^>();
            BuildGroups = nullptr;
            TargetName = String::Empty;
            Kind = "gride";
            IsReadOnly = false;
            Ccs = nullptr;
            Vs = nullptr;
            raw = nullptr;
            IsDirty = false;
        }

        // ---- identity ---------------------------------------------------------
        property String^ FilePath;            // full path of the .pro (or CCS .project)
        property String^ Name;
        property Language Lang;
        property String^ Template;
        property String^ Kind;                // "gride", "ccs7" or "vs"
        property bool IsReadOnly;
        property bool IsDirty;
        property CcsInfo^ Ccs;
        property VsInfo^ Vs;

        // ---- content ----------------------------------------------------------
        property List<Target>^ Targets;
        property List<ProjectFolder^>^ Folders;
        property List<BuildConfiguration^>^ Configurations;
        property List<String^>^ DependsOn;    // other project names in the workspace
        property List<String^>^ ExcludedFiles;
        // a CCS project's exclusions are per configuration (CDT's sourceEntries): configuration name -> relative files
        property Dictionary<String^, List<String^>^>^ ExcludedByConfig;
        // the compiler the project's C and C++ go to, as RIDE's .pro spells it: auto (the language
        // chooses: C to c90, C++ to cpp11, Shalimar to shalimar), c90, cpp11, shalimar, cl (msvc), c++
        property String^ Toolchain;
        // a folder (RIDE's group) naming its own compiler, which takes every file in it
        property Dictionary<String^, String^>^ FolderToolchains;
        // RIDE's emulateOnly: a tms6747 build stops at the assembly, which Run hands to vm6747; no .out is linked
        property bool EmulateOnly;
        // RIDE's build.args: what the program is given when it is run, one argument per entry, already split
        property List<String^>^ ProgramArgs;
        // RIDE's Compiler Options, per configuration (the .pro's "options"); a CCS project's stay at the defaults
        property CompilerOptionStore^ Options;
        // the one format's own: header folders and libraries for every configuration ("include", "libraries",
        // relative to the project), the folders the program is built from ("build" "groups"; nullptr: every
        // folder) and the program's name ("build" "target"; empty: the project's)
        property List<String^>^ Includes;
        property List<String^>^ Libraries;
        property List<String^>^ BuildGroups;
        property String^ TargetName;

        // the program's name when the configuration does not give one
        String^ ProgramName() { return String::IsNullOrWhiteSpace(TargetName) ? Name : TargetName; }

        // a file goes into the program when its folder is one the build names (every folder when it names none)
        bool IsBuilt(String^ abs)
        {
            if (BuildGroups == nullptr) return true;
            String^ folder = FolderOfFile(abs);
            if (folder == nullptr) return true;
            for each (String^ g in BuildGroups)
                if (String::Compare(g, folder, true) == 0) return true;
            return false;
        }

        // the compiler word in force for a file in a folder: the folder's, else the project's
        String^ ToolchainFor(String^ folderName)
        {
            String^ w;
            if (folderName != nullptr && FolderToolchains->TryGetValue(folderName, w) && !String::IsNullOrWhiteSpace(w)) return w;
            return String::IsNullOrWhiteSpace(Toolchain) ? "auto" : Toolchain;
        }

        void SetFolderToolchain(String^ folderName, String^ word)
        {
            if (String::IsNullOrWhiteSpace(word) || String::Compare(word->Trim(), "auto", true) == 0) FolderToolchains->Remove(folderName);
            else FolderToolchains[folderName] = word->Trim();
            IsDirty = true;
        }

        property String^ ProjectDir
        {
            String^ get() { return String::IsNullOrEmpty(FilePath) ? String::Empty : Path::GetDirectoryName(FilePath); }
        }
        property bool IsCcs { bool get() { return Kind == "ccs7"; } }
        property bool IsVs { bool get() { return Kind == "vs"; } }
        // a project read from another IDE's files, shown and built but never written (CCS, Visual Studio)
        property bool IsImported { bool get() { return IsCcs || IsVs; } }
        // the report lines of an imported project, for the Output pane
        List<String^>^ ImportReportLines()
        {
            if (IsCcs && Ccs != nullptr) return Ccs->ReportLines;
            if (IsVs && Vs != nullptr) return Vs->ReportLines;
            return gcnew List<String^>();
        }

        // ---- paths ------------------------------------------------------------
        String^ ToAbsolute(String^ rel) { return PathUtil::MakeAbsolute(ProjectDir, rel); }
        String^ ToRelative(String^ abs) { return PathUtil::MakeRelative(ProjectDir, abs); }

        // every file of the project as an absolute path
        List<String^>^ AllFiles()
        {
            List<String^>^ r = gcnew List<String^>();
            for each (ProjectFolder^ f in Folders)
                for each (String^ rel in f->Files) r->Add(ToAbsolute(rel));
            return r;
        }

        bool ContainsFile(String^ abs)
        {
            for each (String^ f in AllFiles())
                if (PathUtil::SamePath(f, abs)) return true;
            return false;
        }

        bool IsExcluded(String^ abs)
        {
            String^ rel = ToRelative(abs);
            for each (String^ e in ExcludedFiles)
                if (String::Compare(e, rel, true) == 0) return true;
            return false;
        }

        // excluded for a configuration: by the project's list, or by the configuration's own (a CCS project)
        bool IsExcludedIn(String^ abs, String^ configName)
        {
            if (IsExcluded(abs)) return true;
            List<String^>^ own;
            if (configName == nullptr || !ExcludedByConfig->TryGetValue(configName, own)) return false;
            String^ rel = ToRelative(abs)->Replace(L'/', L'\\');
            for each (String^ e in own)
            {
                String^ x = e->Replace(L'/', L'\\');
                if (String::Compare(x, rel, true) == 0) return true;
                if (rel->Length > x->Length && rel->StartsWith(x, StringComparison::OrdinalIgnoreCase) && rel[x->Length] == L'\\') return true;
            }
            return false;
        }

        void SetExcluded(String^ abs, bool excluded)
        {
            String^ rel = ToRelative(abs);
            for (int i = ExcludedFiles->Count - 1; i >= 0; i--)
                if (String::Compare(ExcludedFiles[i], rel, true) == 0) ExcludedFiles->RemoveAt(i);
            if (excluded) ExcludedFiles->Add(rel);
            IsDirty = true;
        }

        ProjectFolder^ FindFolder(String^ name)
        {
            for each (ProjectFolder^ f in Folders)
                if (String::Compare(f->Name, name, true) == 0) return f;
            return nullptr;
        }

        ProjectFolder^ GetOrAddFolder(String^ name)
        {
            ProjectFolder^ f = FindFolder(name);
            if (f == nullptr)
            {
                f = gcnew ProjectFolder(name); Folders->Add(f); IsDirty = true;
                // a folder made here is built, as every folder of a GRIDEd project is
                if (BuildGroups != nullptr) BuildGroups->Add(name);
            }
            return f;
        }

        // default logical folder for a file, decided by its extension
        static String^ DefaultFolderFor(String^ path)
        {
            String^ e = Path::GetExtension(path)->ToLowerInvariant();
            if (e == ".h" || e == ".hpp" || e == ".hh" || e == ".inc") return "inc";
            if (e == ".lib" || e == ".a" || e == ".obj") return "lib";
            if (e == ".cmd" || e == ".ld") return "linker";
            return "src";
        }

        bool AddFile(String^ abs, String^ folderName)
        {
            if (IsReadOnly || ContainsFile(abs)) return false;
            if (String::IsNullOrEmpty(folderName)) folderName = DefaultFolderFor(abs);
            GetOrAddFolder(folderName)->Files->Add(ToRelative(abs));
            IsDirty = true;
            return true;
        }

        bool RemoveFile(String^ abs)
        {
            if (IsReadOnly) return false;
            for each (ProjectFolder^ f in Folders)
                for (int i = 0; i < f->Files->Count; i++)
                    if (PathUtil::SamePath(ToAbsolute(f->Files[i]), abs))
                    {
                        f->Files->RemoveAt(i);
                        IsDirty = true;
                        return true;
                    }
            return false;
        }

        bool RenameFile(String^ oldAbs, String^ newAbs)
        {
            for each (ProjectFolder^ f in Folders)
                for (int i = 0; i < f->Files->Count; i++)
                    if (PathUtil::SamePath(ToAbsolute(f->Files[i]), oldAbs))
                    {
                        f->Files[i] = ToRelative(newAbs);
                        IsDirty = true;
                        return true;
                    }
            return false;
        }

        String^ FolderOfFile(String^ abs)
        {
            for each (ProjectFolder^ f in Folders)
                for each (String^ rel in f->Files)
                    if (PathUtil::SamePath(ToAbsolute(rel), abs)) return f->Name;
            return nullptr;
        }

        bool SupportsTarget(Target t)
        {
            return Targets->Count == 0 || Targets->Contains(t);
        }

        // returns the configuration, creating a default one when missing
        BuildConfiguration^ GetConfiguration(String^ config, Target t)
        {
            String^ key = ConfigName::Make(config, t);
            for each (BuildConfiguration^ c in Configurations)
                if (String::Compare(c->Key, key, true) == 0) return c;
            BuildConfiguration^ n = gcnew BuildConfiguration(config, t);
            Configurations->Add(n);
            if (!IsReadOnly) IsDirty = true;
            return n;
        }

        // ---- load / save --------------------------------------------------------
        static GrideProject^ Load(String^ path)
        {
            JsonValue^ j = JsonParser::ParseFile(path);
            GrideProject^ p = gcnew GrideProject();
            p->FilePath = Path::GetFullPath(path);
            p->raw = j;
            p->Name = j->GetString("name", Path::GetFileNameWithoutExtension(path));
            p->Template = j->GetString("template", "Empty");

            for each (String^ t in j->GetStringList("targets")) p->Targets->Add(TargetInfo::Parse(t));
            // the one format's "arch" is the platform built for (White's Target menu)
            String^ arch = j->GetString("arch", String::Empty);
            if (!String::IsNullOrWhiteSpace(arch) && !p->Targets->Contains(TargetOfArch(arch))) p->Targets->Insert(0, TargetOfArch(arch));

            // the folders: the one format's "groups" - a list of files, or { "files", "toolchain" } for a group
            // with its own compiler - else the "folders" GRIDEd wrote before it
            JsonValue^ groups = j->GetObject("groups");
            if (groups != nullptr)
                for each (String^ name in groups->KeyNames)
                {
                    ProjectFolder^ f = gcnew ProjectFolder(name);
                    JsonValue^ g = groups->Get(name);
                    if (g != nullptr && g->IsObject)
                    {
                        f->Files = g->GetStringList("files");
                        String^ w = g->GetString("toolchain", String::Empty);
                        if (!String::IsNullOrWhiteSpace(w) && String::Compare(w, "auto", true) != 0) p->FolderToolchains[name] = w;
                    }
                    else f->Files = groups->GetStringList(name);
                    p->Folders->Add(f);
                }
            JsonValue^ folders = groups == nullptr ? j->GetObject("folders") : nullptr;
            if (folders != nullptr)
                for each (String^ name in folders->KeyNames)
                {
                    ProjectFolder^ f = gcnew ProjectFolder(name);
                    f->Files = folders->GetStringList(name);
                    p->Folders->Add(f);
                }
            // the language a .pro of the one format does not say, from its files
            p->Lang = j->Has("language") ? LanguageInfo::Parse(j->GetString("language", "C90")) : LanguageOfFiles(p->Folders);
            p->Includes = j->GetStringList("include");
            p->Libraries = j->GetStringList("libraries");

            JsonValue^ cfgs = j->GetObject("configurations");
            if (cfgs != nullptr)
                for each (String^ key in cfgs->KeyNames)
                    p->Configurations->Add(BuildConfiguration::FromJson(key, cfgs->Get(key)));

            p->DependsOn = j->GetStringList("dependsOn");
            p->ExcludedFiles = j->GetStringList("excludeFromBuild");
            p->Toolchain = j->GetString("toolchain", "auto");
            p->EmulateOnly = j->GetBool("emulateOnly", false);
            if (j->Has("programArgs")) p->ProgramArgs = j->GetStringList("programArgs");
            // "build": the program's name, the groups it is built from and its arguments
            JsonValue^ build = j->GetObject("build");
            if (build != nullptr)
            {
                p->TargetName = build->GetString("target", String::Empty);
                if (build->Has("groups")) p->BuildGroups = build->GetStringList("groups");
                if (build->Has("args")) p->ProgramArgs = build->GetStringList("args");
            }
            // White's rule for a .pro of the one format with no "build" at all: its Sources group is the program
            else if (groups != nullptr && !j->Has("build") && p->FindFolder("Sources") != nullptr)
            {
                p->BuildGroups = gcnew List<String^>();
                p->BuildGroups->Add(p->FindFolder("Sources")->Name);
            }
            p->Options->FromJson(j->GetObject("options"));
            JsonValue^ ft = j->GetObject("folderToolchains");
            if (ft != nullptr)
                for each (String^ name in ft->KeyNames)
                {
                    String^ w = ft->GetString(name, String::Empty);
                    if (!String::IsNullOrWhiteSpace(w)) p->FolderToolchains[name] = w;
                }
            p->IsDirty = false;
            return p;
        }

        JsonValue^ ToJson()
        {
            // start from the file we loaded so unknown keys survive a save (White's "indent", "tabs", "open")
            JsonValue^ j = raw != nullptr ? raw : JsonValue::MakeObject();
            bool saidLanguage = j->Has("language");
            // GRIDEd's own spellings before the one format, said in its words below
            for each (String^ k in gcnew array<String^> { "gride", "type", "folders", "folderToolchains", "programArgs" }) j->Remove(k);

            // ---- the one format: White's keys, in White's order
            j->SetString("name", Name);
            j->SetString("toolchain", String::IsNullOrWhiteSpace(Toolchain) ? "auto" : Toolchain);
            if (Targets->Count > 0) j->SetString("arch", ArchOf(Targets[0]));
            JsonValue^ groups = JsonValue::MakeObject();
            for each (ProjectFolder^ f in Folders)
            {
                String^ w;
                if (FolderToolchains->TryGetValue(f->Name, w) && !String::IsNullOrWhiteSpace(w))
                {
                    JsonValue^ g = JsonValue::MakeObject();
                    g->SetList("files", f->Files);
                    g->SetString("toolchain", w);
                    groups->SetValue(f->Name, g);
                }
                else groups->SetList(f->Name, f->Files);
            }
            j->SetValue("groups", groups);
            if (Includes->Count > 0) j->SetList("include", Includes); else j->Remove("include");
            if (Libraries->Count > 0) j->SetList("libraries", Libraries); else j->Remove("libraries");
            if (EmulateOnly) j->SetBool("emulateOnly", true);
            else j->Remove("emulateOnly");
            if (!Options->Empty) j->SetValue("options", Options->ToJson());
            else j->Remove("options");
            JsonValue^ build = JsonValue::MakeObject();
            build->SetString("target", ProgramName());
            List<String^>^ built = BuildGroups;
            if (built == nullptr) { built = gcnew List<String^>(); for each (ProjectFolder^ f in Folders) built->Add(f->Name); }
            build->SetList("groups", built);
            if (ProgramArgs->Count > 0) build->SetList("args", ProgramArgs);
            j->SetValue("build", build);

            // ---- GRIDEd's own, which the one format has no word for; each written only when it says something
            if (saidLanguage || Lang != LanguageOfFiles(Folders)) j->SetString("language", LanguageInfo::Name(Lang));
            if (!String::IsNullOrEmpty(Template) && Template != "Empty") j->SetString("template", Template); else j->Remove("template");
            if (Targets->Count > 1)
            {
                List<String^>^ t = gcnew List<String^>();
                for each (Target x in Targets) t->Add(TargetInfo::Name(x));
                j->SetList("targets", t);
            }
            else j->Remove("targets");
            if (Configurations->Count > 0)
            {
                JsonValue^ cfgs = JsonValue::MakeObject();
                for each (BuildConfiguration^ c in Configurations) cfgs->SetValue(c->Key, c->ToJson());
                j->SetValue("configurations", cfgs);
            }
            else j->Remove("configurations");
            if (ExcludedFiles->Count > 0) j->SetList("excludeFromBuild", ExcludedFiles); else j->Remove("excludeFromBuild");
            if (DependsOn->Count > 0) j->SetList("dependsOn", DependsOn); else j->Remove("dependsOn");
            return j;
        }

        // ---- the one format's words for what GRIDEd holds otherwise
        // "arch", as toolchain.json names the four targets
        static String^ ArchOf(Target t)
        {
            switch (t)
            {
            case Target::Linux:  return "x86_64-linux";
            case Target::MacOS:  return "arm64-darwin";
            case Target::C6747:  return "tms6747";
            default:             return "x86_64-windows";
            }
        }
        static Target TargetOfArch(String^ arch)
        {
            String^ a = arch->Trim()->ToLowerInvariant();
            if (a->EndsWith("-linux")) return Target::Linux;
            if (a->EndsWith("-darwin")) return Target::MacOS;
            if (a->Contains("6747") || a->StartsWith("tms")) return Target::C6747;
            if (a->EndsWith("-windows")) return Target::Windows;
            return TargetInfo::Parse(arch);
        }
        // the language of a project that does not say one: C++ when a file is C++, else Shalimar when one is, else C
        static Language LanguageOfFiles(List<ProjectFolder^>^ folders)
        {
            bool shalimar = false;
            for each (ProjectFolder^ f in folders)
                for each (String^ rel in f->Files)
                {
                    String^ e = Path::GetExtension(rel)->ToLowerInvariant();
                    if (e == ".cpp" || e == ".cc" || e == ".cxx" || e == ".hpp" || e == ".hh") return Language::Cpp11;
                    if (e == ".shl") shalimar = true;
                }
            return shalimar ? Language::Shalimar : Language::C90;
        }

        void Save()
        {
            if (IsReadOnly) return;
            JsonValue^ j = ToJson();
            JsonWriter::WriteFile(FilePath, j);
            raw = j;
            IsDirty = false;
        }

        // Creates the folder skeleton + .pro for File > New > Project
        static GrideProject^ CreateNew(String^ parentDir, String^ name, Language lang, String^ templ, List<Target>^ targets)
        {
            String^ dir = Path::Combine(parentDir, name);
            PathUtil::EnsureDirectory(dir);
            PathUtil::EnsureDirectory(Path::Combine(dir, "src"));
            PathUtil::EnsureDirectory(Path::Combine(dir, "inc"));
            PathUtil::EnsureDirectory(Path::Combine(dir, "lib"));

            GrideProject^ p = gcnew GrideProject();
            p->FilePath = Path::Combine(dir, name + ".pro");
            p->Name = name;
            p->Lang = lang;
            p->Template = templ;
            p->Targets->AddRange(targets);
            p->Folders->Add(gcnew ProjectFolder("src"));
            p->Folders->Add(gcnew ProjectFolder("inc"));
            p->Folders->Add(gcnew ProjectFolder("lib"));
            for each (Target t in targets)
            {
                p->Configurations->Add(gcnew BuildConfiguration("Debug", t));
                p->Configurations->Add(gcnew BuildConfiguration("Release", t));
            }
            return p;
        }

    private:
        JsonValue^ raw;
    };
}}
