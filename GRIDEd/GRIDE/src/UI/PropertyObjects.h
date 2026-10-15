// =============================================================================
//  PropertyObjects.h - small "view" classes shown in the Properties pane and
//  in Project > Properties. Attributes give the PropertyGrid its categories,
//  descriptions and read-only flags.
// =============================================================================
#pragma once
#include "../Model/Workspace.h"
#include "../Settings/GrideSettings.h"

#define GR_CAT(c)  [System::ComponentModel::Category(c)]
#define GR_DESC(d) [System::ComponentModel::Description(d)]
#define GR_RO      [System::ComponentModel::ReadOnly(true)]
#define GR_NAME(n) [System::ComponentModel::DisplayName(n)]

namespace Gride { namespace UI {

    using namespace Gride::Core;
    using namespace Gride::Model;
    using namespace Gride::Settings;

    // ---- a file selected in Project Explorer or open in the editor ------------
    public ref class FileProperties
    {
    public:
        FileProperties(String^ path, GrideProject^ project, GrideSettings^ s)
        {
            this->path = path; this->project = project;
            Gride::Core::Language pl = Gride::Core::Language::C90;
            if (project != nullptr) pl = project->Lang;
            lang = s->DetectLanguage(path, pl);
        }

        GR_CAT("File") GR_RO GR_DESC("File name.")
        property String^ Name { String^ get() { return Path::GetFileName(path); } }

        GR_CAT("File") GR_RO GR_NAME("Full Path")
        property String^ FullPath { String^ get() { return path; } }

        GR_CAT("File") GR_RO GR_NAME("Relative Path") GR_DESC("Path as stored in the .pro file.")
        property String^ RelativePath { String^ get() { return project != nullptr ? project->ToRelative(path) : path; } }

        GR_CAT("File") GR_RO
        property String^ Language { String^ get() { return LanguageInfo::Name(lang); } }

        GR_CAT("File") GR_RO GR_NAME("Size (bytes)")
        property long long Size { long long get() { if (!File::Exists(path)) return 0; return (gcnew FileInfo(path))->Length; } }

        GR_CAT("File") GR_RO
        property DateTime Modified { DateTime get() { return File::Exists(path) ? File::GetLastWriteTime(path) : DateTime::MinValue; } }

        GR_CAT("Build") GR_RO
        property String^ Project { String^ get() { if (project == nullptr) return "(none)"; return project->Name; } }

        GR_CAT("Build") GR_NAME("Excluded From Build") GR_DESC("Skip this file when building the project.")
        property bool Excluded
        {
            bool get() { return project != nullptr && project->IsExcluded(path); }
            void set(bool v) { if (project != nullptr && !project->IsReadOnly) { project->SetExcluded(path, v); project->Save(); } }
        }

    private:
        String^ path;
        GrideProject^ project;
        Gride::Core::Language lang;
    };

    // ---- a project node -----------------------------------------------------
    public ref class ProjectProperties
    {
    public:
        ProjectProperties(GrideProject^ p) { project = p; }

        GR_CAT("Project") GR_RO
        property String^ Name { String^ get() { return project->Name; } }

        GR_CAT("Project") GR_RO
        property String^ Language { String^ get() { return LanguageInfo::Name(project->Lang); } }

        GR_CAT("Project") GR_RO
        property String^ Template { String^ get() { return project->Template; } }

        GR_CAT("Project") GR_RO GR_NAME("Project File")
        property String^ FilePath { String^ get() { return project->FilePath; } }

        GR_CAT("Project") GR_RO
        property String^ Targets
        {
            String^ get()
            {
                List<String^>^ t = gcnew List<String^>();
                for each (Target x in project->Targets) t->Add(TargetInfo::Name(x));
                return String::Join(", ", t);
            }
        }

        GR_CAT("Project") GR_RO GR_NAME("Read-only (CCS / Visual Studio)")
        property bool ReadOnlyCcs { bool get() { return project->IsReadOnly; } }

        GR_CAT("Visual Studio") GR_RO GR_NAME("Version")
        property String^ VsVersion { String^ get() { return project->Vs != nullptr ? project->Vs->Version : String::Empty; } }

        GR_CAT("Visual Studio") GR_RO GR_NAME("Solution")
        property String^ VsSolution { String^ get() { return project->Vs != nullptr ? project->Vs->SolutionPath : String::Empty; } }

        GR_CAT("Visual Studio") GR_RO GR_NAME("Configurations")
        property String^ VsConfigurations { String^ get() { return project->Vs != nullptr ? String::Join(", ", project->Vs->Configurations) : String::Empty; } }

        GR_CAT("Project") GR_RO GR_NAME("Files")
        property int FileCount { int get() { return project->AllFiles()->Count; } }

        GR_CAT("Code Composer Studio") GR_RO GR_NAME("Device")
        property String^ Device { String^ get() { return project->Ccs != nullptr ? project->Ccs->DeviceVariant : String::Empty; } }

        GR_CAT("Code Composer Studio") GR_RO GR_NAME("Code Gen Tools")
        property String^ CodegenVersion { String^ get() { return project->Ccs != nullptr ? project->Ccs->CodegenVersion : String::Empty; } }

        GR_CAT("Code Composer Studio") GR_RO GR_NAME("Output Format")
        property String^ OutputFormat { String^ get() { return project->Ccs != nullptr ? project->Ccs->OutputFormat : String::Empty; } }

    private:
        GrideProject^ project;
    };

    // ---- editable view of one BuildConfiguration (Project > Properties) ----------
    public ref class ConfigurationView
    {
    public:
        ConfigurationView(BuildConfiguration^ c) { cfg = c; }

        GR_CAT("General") GR_RO
        property String^ Configuration { String^ get() { return cfg->Key; } }

        GR_CAT("General") GR_NAME("Output Directory") GR_DESC("Relative to the project folder.")
        property String^ OutDir { String^ get() { return cfg->OutDir; } void set(String^ v) { cfg->OutDir = v; } }

        GR_CAT("General") GR_NAME("Intermediate Directory") GR_DESC("Where .asm and object files go.")
        property String^ ObjDir { String^ get() { return cfg->ObjDir; } void set(String^ v) { cfg->ObjDir = v; } }

        GR_CAT("General") GR_NAME("Output Name") GR_DESC("Program name without extension. Empty = project name.")
        property String^ OutputName { String^ get() { return cfg->OutputName; } void set(String^ v) { cfg->OutputName = v; } }

        GR_CAT("Compiler") GR_NAME("Preprocessor Definitions") GR_DESC("Separate with ';'.")
        property String^ Defines { String^ get() { return Join(cfg->Defines); } void set(String^ v) { cfg->Defines = Split(v); } }

        GR_CAT("Compiler") GR_NAME("Include Directories") GR_DESC("Separate with ';'. Relative to the project folder.")
        property String^ IncludeDirs { String^ get() { return Join(cfg->IncludeDirs); } void set(String^ v) { cfg->IncludeDirs = Split(v); } }

        GR_CAT("Compiler") GR_NAME("Optimisation") GR_DESC("O0 (none) to O3 (full).")
        property String^ Optimise { String^ get() { return cfg->Optimise; } void set(String^ v) { cfg->Optimise = v; } }

        GR_CAT("Compiler") GR_NAME("Additional Options")
        property String^ CompileFlags { String^ get() { return cfg->ExtraCompileFlags; } void set(String^ v) { cfg->ExtraCompileFlags = v; } }

        GR_CAT("Linker") GR_NAME("Linker Command File") GR_DESC("Memory map for the DSP target (e.g. c6747.cmd).")
        property String^ LinkerCmd { String^ get() { return cfg->LinkerCmd; } void set(String^ v) { cfg->LinkerCmd = v; } }

        GR_CAT("Linker") GR_NAME("Libraries") GR_DESC("Separate with ';' (e.g. rts6747.lib).")
        property String^ Libs { String^ get() { return Join(cfg->Libs); } void set(String^ v) { cfg->Libs = Split(v); } }

        GR_CAT("Linker") GR_NAME("Library Directories") GR_DESC("Separate with ';'.")
        property String^ LibDirs { String^ get() { return Join(cfg->LibDirs); } void set(String^ v) { cfg->LibDirs = Split(v); } }

        GR_CAT("Linker") GR_NAME("Additional Options")
        property String^ LinkFlags { String^ get() { return cfg->ExtraLinkFlags; } void set(String^ v) { cfg->ExtraLinkFlags = v; } }

    private:
        BuildConfiguration^ cfg;
        static String^ Join(List<String^>^ l) { return String::Join(";", l); }
        static List<String^>^ Split(String^ v)
        {
            List<String^>^ r = gcnew List<String^>();
            if (v == nullptr) return r;
            for each (String^ s in v->Split(L';'))
                if (s->Trim()->Length > 0) r->Add(s->Trim());
            return r;
        }
    };

    // ---- workspace node -------------------------------------------------------
    public ref class WorkspaceProperties
    {
    public:
        WorkspaceProperties(Workspace^ w) { ws = w; }
        GR_CAT("Workspace") GR_RO property String^ Name { String^ get() { return ws->Name; } }
        GR_CAT("Workspace") GR_RO GR_NAME("Workspace File") property String^ FilePath { String^ get() { return ws->FilePath; } }
        GR_CAT("Workspace") GR_RO property int Projects { int get() { return ws->Projects->Count; } }
        GR_CAT("Workspace") GR_RO GR_NAME("Startup Project") property String^ Startup { String^ get() { return ws->StartupName; } }
        GR_CAT("Build") GR_RO GR_NAME("Active Configuration") property String^ Config { String^ get() { return ws->ActiveConfig; } }
        GR_CAT("Build") GR_RO GR_NAME("Active Target") property String^ TargetName { String^ get() { return TargetInfo::Name(ws->ActiveTarget); } }
    private:
        Workspace^ ws;
    };
}}

#undef GR_CAT
#undef GR_DESC
#undef GR_RO
#undef GR_NAME
