// =============================================================================
//  Workspace.h - a group of projects (.grws JSON), like a Visual Studio .sln
//
//  {
//    "gride": "1.0", "type": "workspace", "name": "INS_Suite",
//    "projects": [ { "path": "NavFilter/NavFilter.pro" },
//                  { "path": "ImuDriver", "kind": "ccs7", "readOnly": true } ],
//    "startup": "NavFilter", "activeConfig": "Debug", "activeTarget": "TMS320C6747"
//  }
// =============================================================================
#pragma once
#include "GrideProject.h"
#include "CcsProjectReader.h"

namespace Gride { namespace Model {

    using namespace Gride::Core;

    public ref class Workspace
    {
    public:
        Workspace()
        {
            Name = "Workspace";
            Projects = gcnew List<GrideProject^>();
            LoadErrors = gcnew List<String^>();
            StartupName = String::Empty;
            ActiveConfig = "Debug";
            ActiveTarget = Target::Windows;
            IsTemporary = false;
            IsDirty = false;
            Breakpoints = gcnew Dictionary<String^, SortedSet<int>^>(StringComparer::OrdinalIgnoreCase);
        }

        property String^ FilePath;
        property String^ Name;
        property List<GrideProject^>^ Projects;
        property List<String^>^ LoadErrors;
        property String^ StartupName;
        property String^ ActiveConfig;
        property Target ActiveTarget;
        property bool IsTemporary;       // opened from a single .pro - not yet saved as .grws
        property bool IsDirty;
        // full path -> 1-based lines; shared with the editors and the debugger
        property Dictionary<String^, SortedSet<int>^>^ Breakpoints;

        property String^ Directory
        {
            String^ get() { return String::IsNullOrEmpty(FilePath) ? String::Empty : Path::GetDirectoryName(FilePath); }
        }

        property GrideProject^ StartupProject
        {
            GrideProject^ get()
            {
                GrideProject^ p = FindProject(StartupName);
                if (p == nullptr && Projects->Count > 0) p = Projects[0];
                return p;
            }
        }

        GrideProject^ FindProject(String^ name)
        {
            if (String::IsNullOrEmpty(name)) return nullptr;
            for each (GrideProject^ p in Projects)
                if (String::Compare(p->Name, name, true) == 0) return p;
            return nullptr;
        }

        GrideProject^ FindProjectForFile(String^ abs)
        {
            for each (GrideProject^ p in Projects)
                if (p->ContainsFile(abs)) return p;
            return nullptr;
        }

        bool AddProject(GrideProject^ p)
        {
            for each (GrideProject^ x in Projects)
                if (PathUtil::SamePath(x->FilePath, p->FilePath)) return false;
            Projects->Add(p);
            if (String::IsNullOrEmpty(StartupName)) StartupName = p->Name;
            IsDirty = true;
            return true;
        }

        void RemoveProject(GrideProject^ p)
        {
            Projects->Remove(p);
            for each (GrideProject^ x in Projects) x->DependsOn->Remove(p->Name);
            if (String::Compare(StartupName, p->Name, true) == 0)
                StartupName = Projects->Count > 0 ? Projects[0]->Name : String::Empty;
            IsDirty = true;
        }

        // projects ordered so that dependencies are built first
        List<GrideProject^>^ BuildOrder()
        {
            List<GrideProject^>^ result = gcnew List<GrideProject^>();
            HashSet<GrideProject^>^ visiting = gcnew HashSet<GrideProject^>();
            for each (GrideProject^ p in Projects) Visit(p, result, visiting);
            return result;
        }

        // dependencies of one project (recursively) followed by the project itself
        List<GrideProject^>^ BuildOrderFor(GrideProject^ root)
        {
            List<GrideProject^>^ result = gcnew List<GrideProject^>();
            Visit(root, result, gcnew HashSet<GrideProject^>());
            return result;
        }

        // ---- load / save --------------------------------------------------------
        // cgToolRoot is needed to expand ${CG_TOOL_ROOT} in CCS projects
        static Workspace^ Load(String^ path, String^ cgToolRoot)
        {
            JsonValue^ j = JsonParser::ParseFile(path);
            Workspace^ w = gcnew Workspace();
            w->FilePath = Path::GetFullPath(path);
            w->Name = j->GetString("name", Path::GetFileNameWithoutExtension(path));
            w->StartupName = j->GetString("startup", String::Empty);
            w->ActiveConfig = j->GetString("activeConfig", "Debug");
            w->ActiveTarget = TargetInfo::Parse(j->GetString("activeTarget", "Windows"));

            JsonValue^ list = j->Get("projects");
            if (list != nullptr && list->IsArray)
                for (int i = 0; i < list->Count; i++)
                {
                    JsonValue^ e = list->At(i);
                    String^ rel = e->GetString("path", String::Empty);
                    String^ kind = e->GetString("kind", "gride");
                    String^ abs = PathUtil::MakeAbsolute(w->Directory, rel);
                    try
                    {
                        GrideProject^ p;
                        if (kind == "ccs7") p = CcsProjectReader::Read(abs, cgToolRoot);
                        else p = GrideProject::Load(abs);
                        w->Projects->Add(p);
                    }
                    catch (Exception^ ex)
                    {
                        w->LoadErrors->Add(rel + ": " + ex->Message);
                    }
                }
            JsonValue^ bps = j->GetObject("breakpoints");
            if (bps != nullptr)
                for each (String^ rel in bps->KeyNames)
                {
                    SortedSet<int>^ lines = gcnew SortedSet<int>();
                    JsonValue^ arr = bps->Get(rel);
                    if (arr == nullptr || !arr->IsArray) continue;   // hand-edited / malformed entry
                    for (int i = 0; i < arr->Count; i++) lines->Add((int)arr->At(i)->AsNumber());
                    if (lines->Count > 0) w->Breakpoints[PathUtil::MakeAbsolute(w->Directory, rel)] = lines;
                }
            w->IsDirty = false;
            return w;
        }

        // wraps a single project (opened from .pro or CCS folder) in a workspace
        static Workspace^ ForSingleProject(GrideProject^ p)
        {
            Workspace^ w = gcnew Workspace();
            w->Name = p->Name;
            w->FilePath = Path::Combine(p->ProjectDir, p->Name + ".grws");
            w->Projects->Add(p);
            w->StartupName = p->Name;
            if (p->Targets->Count > 0) w->ActiveTarget = p->Targets[0];
            w->IsTemporary = true;
            return w;
        }

        void Save()
        {
            JsonValue^ j = JsonValue::MakeObject();
            j->SetString("gride", "1.0");
            j->SetString("type", "workspace");
            j->SetString("name", Name);
            JsonValue^ list = JsonValue::MakeArray();
            for each (GrideProject^ p in Projects)
            {
                JsonValue^ e = JsonValue::MakeObject();
                if (p->IsCcs)
                {
                    e->SetString("path", PathUtil::MakeRelative(Directory, p->ProjectDir));
                    e->SetString("kind", "ccs7");
                    e->SetBool("readOnly", true);
                }
                else e->SetString("path", PathUtil::MakeRelative(Directory, p->FilePath));
                list->Add(e);
            }
            j->SetValue("projects", list);
            j->SetString("startup", StartupName);
            j->SetString("activeConfig", ActiveConfig);
            j->SetString("activeTarget", TargetInfo::Name(ActiveTarget));
            JsonValue^ bps = JsonValue::MakeObject();
            for each (KeyValuePair<String^, SortedSet<int>^> kv in Breakpoints)
            {
                if (kv.Value->Count == 0) continue;
                JsonValue^ arr = JsonValue::MakeArray();
                for each (int l in kv.Value) arr->Add(JsonValue::FromNumber(l));
                bps->SetValue(PathUtil::MakeRelative(Directory, kv.Key), arr);
            }
            j->SetValue("breakpoints", bps);
            JsonWriter::WriteFile(FilePath, j);
            IsTemporary = false;
            IsDirty = false;
        }

        // saves every writable project that changed
        void SaveProjects()
        {
            for each (GrideProject^ p in Projects)
                if (p->IsDirty && !p->IsReadOnly) p->Save();
        }

    private:
        void Visit(GrideProject^ p, List<GrideProject^>^ result, HashSet<GrideProject^>^ visiting)
        {
            if (p == nullptr || result->Contains(p) || visiting->Contains(p)) return;
            visiting->Add(p);
            for each (String^ dep in p->DependsOn) Visit(FindProject(dep), result, visiting);
            result->Add(p);
        }
    };
}}
