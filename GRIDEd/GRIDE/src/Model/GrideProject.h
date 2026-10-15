// =============================================================================
//  GrideProject.h - one project (.pro JSON file)
//
//  Layout on disk ("one step difference"):
//      NavFilter\NavFilter.pro      <- this file
//      NavFilter\src\*.c            <- sources, stored as "src/main.c"
//      NavFilter\inc\*.h
//  A read-only project imported from Code Composer Studio 7.4 uses the same
//  class (Kind = "ccs7", IsReadOnly = true) - see CcsProjectReader.h.
// =============================================================================
#pragma once
#include "../Core/Json.h"
#include "../Core/PathUtil.h"
#include "../Core/LanguageTarget.h"
#include "BuildConfiguration.h"

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
            LinkerCommandFile = String::Empty;
        }
        property String^ DeviceVariant;
        property String^ DeviceFamily;
        property String^ CodegenVersion;
        property String^ OutputFormat;
        property String^ LinkerCommandFile;
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
            Kind = "gride";
            IsReadOnly = false;
            Ccs = nullptr;
            raw = nullptr;
            IsDirty = false;
        }

        // ---- identity ---------------------------------------------------------
        property String^ FilePath;            // full path of the .pro (or CCS .project)
        property String^ Name;
        property Language Lang;
        property String^ Template;
        property String^ Kind;                // "gride" or "ccs7"
        property bool IsReadOnly;
        property bool IsDirty;
        property CcsInfo^ Ccs;

        // ---- content ----------------------------------------------------------
        property List<Target>^ Targets;
        property List<ProjectFolder^>^ Folders;
        property List<BuildConfiguration^>^ Configurations;
        property List<String^>^ DependsOn;    // other project names in the workspace
        property List<String^>^ ExcludedFiles;

        property String^ ProjectDir
        {
            String^ get() { return String::IsNullOrEmpty(FilePath) ? String::Empty : Path::GetDirectoryName(FilePath); }
        }
        property bool IsCcs { bool get() { return Kind == "ccs7"; } }

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
        static GrideProject^ Load(String^ path)
        {
            JsonValue^ j = JsonParser::ParseFile(path);
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
