// =============================================================================
//  GrideProject.h - one project (.pro JSON file)
//
//  Layout on disk ("one step difference"):
//      Project1\Project1.pro        <- this file
//      Project1\src\*.c             <- sources, stored as "src/main.c"
//      Project1\inc\*.h
//  A read-only project imported from Code Composer Studio 7.4 uses the same
//  class (Kind = "ccs7", IsReadOnly = true) - see CcsProjectReader.h.
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
            if (f == nullptr) { f = gcnew ProjectFolder(name); Folders->Add(f); IsDirty = true; }
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
        // GRIDE 2.0's own .pro ("formatVersion" and no "gride"): read here, never written back
        static bool IsGrey(JsonValue^ j) { return !j->Has("gride") && j->Has("formatVersion"); }

        // the Visual Studio solution a GRIDE 2.0 .pro stands for ("visualStudio"), else null
        static String^ GreySolution(String^ path)
        {
            JsonValue^ j = JsonParser::ParseFile(path);
            if (!IsGrey(j)) return nullptr;
            String^ sln = j->GetString("visualStudio", String::Empty);
            return sln->Length > 0 ? PathUtil::MakeAbsolute(Path::GetDirectoryName(Path::GetFullPath(path)), sln) : nullptr;
        }

        static GrideProject^ FromGrey(String^ path, JsonValue^ j)
        {
            GrideProject^ p = gcnew GrideProject();
            p->FilePath = Path::GetFullPath(path);
            p->Name = j->GetString("name", Path::GetFileNameWithoutExtension(path));
            p->Lang = LanguageInfo::Parse(j->GetString("language", "C90"));
            p->Template = "Program";
            p->Targets->Add(Target::Windows); p->Targets->Add(Target::C6747);
            ProjectFolder^ f = gcnew ProjectFolder("Sources");
            f->Files = j->GetStringList("sourceFiles");
            p->Folders->Add(f);
            JsonValue^ cfgs = j->Get("configurations");
            bool listed = cfgs != nullptr && cfgs->IsArray && cfgs->Count > 0;
            for each (Target t in p->Targets)
            {
                if (!listed) { p->Configurations->Add(gcnew BuildConfiguration("Debug", t)); p->Configurations->Add(gcnew BuildConfiguration("Release", t)); continue; }
                for (int i = 0; i < cfgs->Count; i++)
                {
                    BuildConfiguration^ c = gcnew BuildConfiguration(cfgs->At(i)->GetString("name", "Debug"), t);
                    c->Defines = cfgs->At(i)->GetStringList("defines");
                    p->Configurations->Add(c);
                }
            }
            p->Toolchain = j->GetString("toolchain", "auto");
            if (j->Has("programArgs")) p->ProgramArgs = j->GetStringList("programArgs");
            p->IsReadOnly = true;
            p->IsDirty = false;
            return p;
        }

        static GrideProject^ Load(String^ path)
        {
            JsonValue^ j = JsonParser::ParseFile(path);
            if (IsGrey(j)) return FromGrey(path, j);
            GrideProject^ p = gcnew GrideProject();
            p->FilePath = Path::GetFullPath(path);
            p->raw = j;
            p->Name = j->GetString("name", Path::GetFileNameWithoutExtension(path));
            p->Lang = LanguageInfo::Parse(j->GetString("language", "C90"));
            p->Template = j->GetString("template", "Empty");

            for each (String^ t in j->GetStringList("targets")) p->Targets->Add(TargetInfo::Parse(t));

            JsonValue^ folders = j->GetObject("folders");
            if (folders != nullptr)
                for each (String^ name in folders->KeyNames)
                {
                    ProjectFolder^ f = gcnew ProjectFolder(name);
                    f->Files = folders->GetStringList(name);
                    p->Folders->Add(f);
                }

            JsonValue^ cfgs = j->GetObject("configurations");
            if (cfgs != nullptr)
                for each (String^ key in cfgs->KeyNames)
                    p->Configurations->Add(BuildConfiguration::FromJson(key, cfgs->Get(key)));

            p->DependsOn = j->GetStringList("dependsOn");
            p->ExcludedFiles = j->GetStringList("excludeFromBuild");
            p->Toolchain = j->GetString("toolchain", "auto");
            p->EmulateOnly = j->GetBool("emulateOnly", false);
            if (j->Has("programArgs")) p->ProgramArgs = j->GetStringList("programArgs");
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
            // start from the file we loaded so unknown keys survive a save
            JsonValue^ j = raw != nullptr ? raw : JsonValue::MakeObject();
            j->SetString("gride", "1.0");
            j->SetString("type", "project");
            j->SetString("name", Name);
            j->SetString("language", LanguageInfo::Name(Lang));
            j->SetString("template", Template);
            List<String^>^ t = gcnew List<String^>();
            for each (Target x in Targets) t->Add(TargetInfo::Name(x));
            j->SetList("targets", t);

            JsonValue^ folders = JsonValue::MakeObject();
            for each (ProjectFolder^ f in Folders) folders->SetList(f->Name, f->Files);
            j->SetValue("folders", folders);

            JsonValue^ cfgs = JsonValue::MakeObject();
            for each (BuildConfiguration^ c in Configurations) cfgs->SetValue(c->Key, c->ToJson());
            j->SetValue("configurations", cfgs);

            j->SetList("excludeFromBuild", ExcludedFiles);
            j->SetList("dependsOn", DependsOn);
            // written only when they say something, so a file written before the port is unchanged
            if (!String::IsNullOrWhiteSpace(Toolchain) && String::Compare(Toolchain, "auto", true) != 0) j->SetString("toolchain", Toolchain);
            else if (j->Has("toolchain")) j->Remove("toolchain");
            if (FolderToolchains->Count > 0)
            {
                JsonValue^ ft = JsonValue::MakeObject();
                for each (KeyValuePair<String^, String^> kv in FolderToolchains) ft->SetString(kv.Key, kv.Value);
                j->SetValue("folderToolchains", ft);
            }
            else if (j->Has("folderToolchains")) j->Remove("folderToolchains");
            if (EmulateOnly) j->SetBool("emulateOnly", true);
            else if (j->Has("emulateOnly")) j->Remove("emulateOnly");
            if (ProgramArgs->Count > 0) j->SetList("programArgs", ProgramArgs);
            else if (j->Has("programArgs")) j->Remove("programArgs");
            if (!Options->Empty) j->SetValue("options", Options->ToJson());
            else if (j->Has("options")) j->Remove("options");
            return j;
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
