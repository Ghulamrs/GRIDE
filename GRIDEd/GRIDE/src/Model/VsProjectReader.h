// =============================================================================
//  VsProjectReader.h - a Visual Studio C++ project as a read-only GrideProject
//
//  The reading (VsSolution.h) becomes a project of Kind "vs": the files in the
//  folders of its .filters (or their directories), the Debug and Release
//  configurations for the Windows target from the project's own (x64 first, then
//  Win32), with its defines, include directories, libraries and optimisation;
//  what the file has and GRIDE does not take is said in the report lines. The
//  build is GRIDE's own toolchain into .gride\ under the project - Microsoft's
//  cl and link.exe when the workspace names cl as its compiler, with the file's
//  own cl settings (ClSettings, through toolchain.json's projectSettings); the
//  Visual Studio files are never written.
// =============================================================================
#pragma once
#include "GrideProject.h"

namespace Gride { namespace Model {

    using namespace Gride::Core;

    public ref class VsProjectReader abstract sealed
    {
    public:
        static bool IsVsProject(String^ path) { return VsProject::IsProjectFile(path) && File::Exists(path); }
        static bool IsSolution(String^ path) { return VsSolution::IsSolution(path) && File::Exists(path); }

        // solutionPath may be empty (a project opened alone: $(SolutionDir) is then unresolved and said so)
        static GrideProject^ Read(String^ path, String^ solutionPath)
        {
            String^ solutionDir = String::IsNullOrWhiteSpace(solutionPath) ? String::Empty : Path::GetDirectoryName(Path::GetFullPath(solutionPath));
            VsProject^ r = VsProject::Read(path, solutionDir);
            GrideProject^ p = gcnew GrideProject();
            p->Kind = "vs";
            p->IsReadOnly = true;
            p->FilePath = r->Path;
            p->Name = r->Name;
            p->Template = "Visual Studio project (" + r->Kind + (r->Toolset->Length > 0 ? ", " + r->Toolset : String::Empty) + ")";
            p->Targets->Add(Target::Windows);
            p->Vs = gcnew VsInfo();
            p->Vs->Reading = r;
            p->Vs->SolutionPath = String::IsNullOrWhiteSpace(solutionPath) ? String::Empty : Path::GetFullPath(solutionPath);
            p->Vs->Version = r->Version;
            p->Vs->Toolset = r->Toolset;
            p->Vs->Guid = r->Guid;
            for each (VsProjectConfig^ c in r->Configurations) p->Vs->Configurations->Add(c->Name);

            // the files, in their filters; a file outside the project folder under "(outside)" when no filter names it
            bool anyCpp = false, anyC = false;
            for each (VsProjectItem^ i in r->Items)
            {
                String^ rel = PathUtil::MakeRelative(p->ProjectDir, i->Path);
                String^ folder = i->Filter;
                if (folder->Length == 0)
                {
                    String^ dir = Path::GetDirectoryName(rel->Replace(L'/', L'\\'));
                    folder = Path::IsPathRooted(rel) || rel->StartsWith("..") ? "(outside)" : String::IsNullOrEmpty(dir) ? "(project root)" : dir;
                }
                ProjectFolder^ f = p->GetOrAddFolder(folder);
                if (!f->Files->Contains(rel)) f->Files->Add(rel);
                String^ e = Path::GetExtension(i->Path)->ToLowerInvariant();
                bool source = e == ".cpp" || e == ".cxx" || e == ".cc" || e == ".c" || e == ".c++";
                if (String::Compare(i->Type, "ClCompile", true) == 0) { if (e == ".c") anyC = true; else anyCpp = true; }
                // a source the project does not compile (a None item) is shown and left out of the build
                if (source && String::Compare(i->Type, "ClCompile", true) != 0 && !p->ExcludedFiles->Contains(rel)) p->ExcludedFiles->Add(rel);
                if (i->Excluded->Contains("*") && !p->ExcludedFiles->Contains(rel)) p->ExcludedFiles->Add(rel);
            }
            p->Lang = anyCpp || !anyC ? Language::Cpp11 : Language::C90;

            // Debug and Release for Windows from the project's configurations: x64 before Win32; others are said
            List<String^>^ mappedFrom = gcnew List<String^>();
            for each (String^ name in gcnew array<String^>{ "Debug", "Release" })
            {
                VsProjectConfig^ c = Pick(r, name);
                BuildConfiguration^ b = gcnew BuildConfiguration(name, Target::Windows);
                b->Defines->Clear(); b->IncludeDirs->Clear(); b->Libs->Clear(); b->LibDirs->Clear();
                b->OutDir = ".gride/out/" + name;
                b->ObjDir = ".gride/obj/" + name;
                b->LinkerCmd = String::Empty;
                if (c != nullptr)
                {
                    mappedFrom->Add(name + " from " + c->Name);
                    p->Vs->MappedConfigs[name] = c->Name;
                    b->Defines->AddRange(c->Defines);
                    for each (String^ inc in c->IncludeDirs) b->IncludeDirs->Add(Relative(p->ProjectDir, inc));
                    for each (String^ lib in c->Libs) b->Libs->Add(lib);
                    for each (String^ d in c->LibDirs) b->LibDirs->Add(Relative(p->ProjectDir, d));
                    b->Optimise = OptimiseOf(c);
                    List<String^>^ extra = gcnew List<String^>();
                    for each (String^ u in c->Undefines) extra->Add("-U" + u);
                    if (c->AdditionalOptions->Length > 0) p->Vs->ReportLines->Add("VS project " + r->Name + " (" + c->Name + "): cl's additional options are given to cl only (Project > Properties > Compiler), not to RIDE's compilers: " + c->AdditionalOptions);
                    b->ExtraCompileFlags = String::Join(" ", extra);
                    if (c->TargetName->Length > 0 && !VsProject::HasMacro(c->TargetName) && String::Compare(c->TargetName, r->Name, true) != 0) b->OutputName = c->TargetName;
                    List<String^>^ excluded = gcnew List<String^>();
                    for each (VsProjectItem^ i in r->Items)
                        if (i->ExcludedIn(c->Name) && !i->Excluded->Contains("*")) excluded->Add(PathUtil::MakeRelative(p->ProjectDir, i->Path)->Replace(L'/', L'\\'));
                    p->ExcludedByConfig[name] = excluded;
                    for each (String^ n in c->Notes) p->Vs->ReportLines->Add("VS project " + r->Name + " (" + c->Name + "): not mapped - " + n);
                }
                else
                {
                    b->Optimise = name == "Debug" ? "O0" : "O2";
                    p->Vs->ReportLines->Add("VS project " + r->Name + ": no " + name + " configuration in the file - GRIDE's defaults for " + name);
                }
                p->Configurations->Add(b);
            }
            // what the file has beyond Debug and Release for x64 / Win32
            List<String^>^ others = gcnew List<String^>();
            for each (VsProjectConfig^ c in r->Configurations)
                if (!p->Vs->MappedConfigs->ContainsValue(c->Name)) others->Add(c->Name);
            p->Vs->ReportLines->Insert(0, "VS project " + r->Name + ": " + r->Version + "; " + String::Join(", ", mappedFrom) +
                                          (others->Count > 0 ? "; not built here: " + String::Join(", ", others) : String::Empty));
            for each (String^ n in r->Notes) p->Vs->ReportLines->Add("VS project " + r->Name + ": not mapped - " + n);
            p->IsDirty = false;
            return p;
        }

    private:
        // the project's configuration GRIDE's Debug / Release is built from: that name on x64, else Win32, else the first platform
        static VsProjectConfig^ Pick(VsProject^ r, String^ name)
        {
            for each (String^ platform in gcnew array<String^>{ "x64", "Win32", "" })
                for each (VsProjectConfig^ c in r->Configurations)
                    if (String::Compare(c->Configuration, name, true) == 0 && (platform->Length == 0 || String::Compare(c->Platform, platform, true) == 0)) return c;
            return nullptr;
        }

        // cl's Optimization as GRIDE's -O word: Disabled -O0, MinSpace -O1, MaxSpeed -O2, Full -O3; unsaid: the configuration's kind
        static String^ OptimiseOf(VsProjectConfig^ c)
        {
            String^ o = c->Optimization;
            if (String::Compare(o, "Disabled", true) == 0) return "O0";
            if (String::Compare(o, "MinSpace", true) == 0) return "O1";
            if (String::Compare(o, "MaxSpeed", true) == 0) return "O2";
            if (String::Compare(o, "Full", true) == 0) return "O3";
            return c->IsDebugLike ? "O0" : "O2";
        }

        static String^ Relative(String^ dir, String^ abs)
        {
            try
            {
                String^ rel = PathUtil::MakeRelative(dir, abs);
                if (!rel->StartsWith("..") && !Path::IsPathRooted(rel)) return rel;
            }
            catch (Exception^) { }
            return abs;
        }
    };
}}
