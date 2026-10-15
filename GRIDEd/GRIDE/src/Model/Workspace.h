// =============================================================================
//  Workspace.h - a group of projects (.grws JSON), like a Visual Studio .sln
//
//  {
//    "gride": "1.0", "type": "workspace", "name": "cppparse",
//    "projects": [ { "path": "cppparse/cppparse.vcxproj", "kind": "vs", "readOnly": true, "solution": "cppparse.sln",
//                    "toolchain": "cl", "programArgs": ["--health", "."] } ],
//    "solution": "cppparse.sln",
//    "startup": "cppparse", "activeConfig": "Release", "activeTarget": "Windows"
//  }
//  A project read from Visual Studio's or CCS's files ("vs", "ccs7") is never written; what
//  GRIDE keeps for it - the compiler word, the folders' own, the program's arguments,
//  emulateOnly - is kept in its entry here.
// =============================================================================
#pragma once
#include "GrideProject.h"
#include "CcsProjectReader.h"
#include "VsProjectReader.h"

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
            SolutionPath = String::Empty;
            SolutionFolders = gcnew List<String^>();
            SolutionItems = gcnew Dictionary<String^, List<String^>^>();
        }

        // a Visual Studio solution opened as a workspace: the .sln, its solution folders ("Libs/Inner",
        // outer ones first) and the files they hold (absolute), which the explorer shows under them
        property String^ SolutionPath;
        property List<String^>^ SolutionFolders;
        property Dictionary<String^, List<String^>^>^ SolutionItems;

        // the solution's C++ projects read-only, their dependencies by GUID, the solution folders and
        // items; a project that cannot be read, and every non-C++ project, is a line in LoadErrors
        static Workspace^ ForSolution(VsSolution^ s)
        {
            Workspace^ w = gcnew Workspace();
            w->Name = s->Name;
            w->FilePath = Path::Combine(s->Dir, s->Name + ".grws");
            w->IsTemporary = true;
            w->SolutionPath = s->Path;
            w->ActiveTarget = Target::Windows;
            for each (String^ f in s->FolderPaths()) w->SolutionFolders->Add(f);
            for each (VsSolutionEntry^ e in s->Entries)
            {
                if (!e->IsFolder || e->Items->Count == 0) continue;
                String^ chain = s->FolderPathOf(e);
                String^ own = chain->Length > 0 ? chain + "/" + e->Name : e->Name;
                if (!w->SolutionItems->ContainsKey(own)) w->SolutionItems[own] = gcnew List<String^>();
                w->SolutionItems[own]->AddRange(e->Items);
            }
            Dictionary<String^, GrideProject^>^ byGuid = gcnew Dictionary<String^, GrideProject^>();
            for each (VsSolutionEntry^ e in s->CppProjects())
            {
                try
                {
                    GrideProject^ p = VsProjectReader::Read(e->Path, s->Path);
                    if (p->Name->Length == 0 || String::Compare(p->Name, Path::GetFileNameWithoutExtension(e->Path), true) == 0) p->Name = e->Name;
                    p->Vs->SolutionFolder = s->FolderPathOf(e);
                    w->Projects->Add(p);
                    byGuid[e->Guid] = p;
                }
                catch (Exception^ ex) { w->LoadErrors->Add(e->RelativePath + ": " + ex->Message); }
            }
            for each (VsSolutionEntry^ e in s->CppProjects())
            {
                GrideProject^ p;
                if (!byGuid->TryGetValue(e->Guid, p)) continue;
                for each (String^ dep in e->Dependencies)
                {
                    GrideProject^ d;
                    if (byGuid->TryGetValue(dep, d) && d != p && !p->DependsOn->Contains(d->Name)) p->DependsOn->Add(d->Name);
                }
            }
            for each (String^ n in s->Notes) w->LoadErrors->Add(n);
            w->StartupName = w->Projects->Count > 0 ? w->Projects[0]->Name : String::Empty;
            w->IsDirty = false;
            return w;
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
            String^ sln = j->GetString("solution", String::Empty);
            if (sln->Length > 0) w->SolutionPath = PathUtil::MakeAbsolute(w->Directory, sln);
            if (j->Has("solutionFolders")) w->SolutionFolders = j->GetStringList("solutionFolders");
            JsonValue^ items = j->GetObject("solutionItems");
            if (items != nullptr)
                for each (String^ folder in items->KeyNames)
                {
                    List<String^>^ abs = gcnew List<String^>();
                    for each (String^ rel in items->GetStringList(folder)) abs->Add(PathUtil::MakeAbsolute(w->Directory, rel));
                    w->SolutionItems[folder] = abs;
                }

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
                        else if (kind == "vs")
                        {
                            String^ own = e->GetString("solution", String::Empty);
                            p = VsProjectReader::Read(abs, own->Length > 0 ? PathUtil::MakeAbsolute(w->Directory, own) : w->SolutionPath);
                            p->Vs->SolutionFolder = e->GetString("solutionFolder", String::Empty);
                        }
                        else p = GrideProject::Load(abs);
                        if (p->IsImported) ReadImportedState(e, p);
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
            w->savedText = JsonWriter::Write(w->ToJson());
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

        // true when saving would write something different from the file as loaded / last saved
        // (breakpoints and the active configuration are edited without setting IsDirty)
        bool HasChanges()
        {
            if (IsDirty || IsTemporary || savedText == nullptr) return true;
            return !String::Equals(JsonWriter::Write(ToJson()), savedText);
        }

        void Save()
        {
            JsonValue^ j = ToJson();
            JsonWriter::WriteFile(FilePath, j);
            savedText = JsonWriter::Write(j);
            IsTemporary = false;
            IsDirty = false;
        }

        JsonValue^ ToJson()
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
                else if (p->IsVs)
                {
                    e->SetString("path", PathUtil::MakeRelative(Directory, p->FilePath));
                    e->SetString("kind", "vs");
                    e->SetBool("readOnly", true);
                    if (p->Vs != nullptr && p->Vs->SolutionPath->Length > 0) e->SetString("solution", PathUtil::MakeRelative(Directory, p->Vs->SolutionPath));
                    if (p->Vs != nullptr && p->Vs->SolutionFolder->Length > 0) e->SetString("solutionFolder", p->Vs->SolutionFolder);
                }
                else e->SetString("path", PathUtil::MakeRelative(Directory, p->FilePath));
                if (p->IsImported) WriteImportedState(e, p);
                list->Add(e);
            }
            j->SetValue("projects", list);
            if (SolutionPath->Length > 0) j->SetString("solution", PathUtil::MakeRelative(Directory, SolutionPath));
            if (SolutionFolders->Count > 0) j->SetList("solutionFolders", SolutionFolders);
            if (SolutionItems->Count > 0)
            {
                JsonValue^ items = JsonValue::MakeObject();
                for each (KeyValuePair<String^, List<String^>^> kv in SolutionItems)
                {
                    List<String^>^ rel = gcnew List<String^>();
                    for each (String^ abs in kv.Value) rel->Add(PathUtil::MakeRelative(Directory, abs));
                    items->SetList(kv.Key, rel);
                }
                j->SetValue("solutionItems", items);
            }
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
            return j;
        }

        // saves every writable project that changed
        void SaveProjects()
        {
            for each (GrideProject^ p in Projects)
                if (p->IsDirty && !p->IsReadOnly) p->Save();
        }

        // what GRIDE keeps for an imported project, whose own files are never written, in its entry:
        // each field only when it says something, so an entry without them is as before
        static void WriteImportedState(JsonValue^ e, GrideProject^ p)
        {
            if (!String::IsNullOrWhiteSpace(p->Toolchain) && String::Compare(p->Toolchain->Trim(), "auto", true) != 0) e->SetString("toolchain", p->Toolchain->Trim());
            if (p->FolderToolchains->Count > 0)
            {
                JsonValue^ ft = JsonValue::MakeObject();
                for each (KeyValuePair<String^, String^> kv in p->FolderToolchains) ft->SetString(kv.Key, kv.Value);
                e->SetValue("folderToolchains", ft);
            }
            if (p->ProgramArgs->Count > 0) e->SetList("programArgs", p->ProgramArgs);
            if (p->EmulateOnly) e->SetBool("emulateOnly", true);
        }

        static void ReadImportedState(JsonValue^ e, GrideProject^ p)
        {
            p->Toolchain = e->GetString("toolchain", "auto");
            JsonValue^ ft = e->GetObject("folderToolchains");
            if (ft != nullptr)
                for each (String^ name in ft->KeyNames)
                {
                    String^ w = ft->GetString(name, String::Empty);
                    if (!String::IsNullOrWhiteSpace(w) && String::Compare(w->Trim(), "auto", true) != 0) p->FolderToolchains[name] = w->Trim();
                }
            if (e->Has("programArgs")) p->ProgramArgs = e->GetStringList("programArgs");
            p->EmulateOnly = e->GetBool("emulateOnly", false);
            p->IsDirty = false;
        }

    private:
        String^ savedText;      // JSON of the file as loaded / last saved

        void Visit(GrideProject^ p, List<GrideProject^>^ result, HashSet<GrideProject^>^ visiting)
        {
            if (p == nullptr || result->Contains(p) || visiting->Contains(p)) return;
            visiting->Add(p);
            for each (String^ dep in p->DependsOn) Visit(FindProject(dep), result, visiting);
            result->Add(p);
        }
    };
}}
