#pragma once
// ============================================================================
//  GRIDE 2.0 - Visual Studio projects (R08: GRIDEd's VsProjectReader ported)
//  A .vcxproj / .vcproj - opened itself, or as one of a solution's C++ projects
//  - becomes a READ-ONLY GrideProject that builds with GRIDE's own toolchain
//  (RIDE's compilers, the Windows target): the files in the folders of its
//  .filters (or their directories), Debug and Release from the project's own
//  configurations (x64 first, then Win32) with their defines, undefines,
//  include directories, libraries, library directories and exclusions, the
//  program and its objects under .gride\ beside the project. What the file has
//  and GRIDE does not take is said in the report lines. The Visual Studio files
//  are never written; the project is read afresh at every open and build.
//  GRIDE 2.0 holds one project: a solution with several C++ projects names
//  them, and one is chosen.
//  R12: a .pro may stand for a Visual Studio project ("visualStudio": the
//  solution or project beside it) and keep GRIDE's state for it - the compiler
//  (cl, Visual Studio's own), the configuration, the program's arguments - as
//  GRIDEd's .grws entry does.
//  A project's configuration keeps the file's (VsConfig): its own ClCompile
//  settings go to cl through toolchain.json's projectSettings.
// ============================================================================
#include "../Toolchain/Toolchain.h"

namespace Gride
{
    // the .pro of a Visual Studio project (R12): what GRIDE keeps for it, the rest is the Visual Studio files'
    [DataContract(Name = "project", Namespace = "")]
    public ref class VsProjectState
    {
    public:
        [DataMember(Name = "formatVersion", Order = 0)] property int FormatVersion;
        [DataMember(Name = "name", Order = 1)] property String^ Name;
        [DataMember(Name = "activeConfiguration", Order = 8)] property String^ ActiveConfiguration;
        [DataMember(Name = "toolchain", Order = 10)] property String^ Toolchain;
        [DataMember(Name = "emulateOnly", Order = 11, EmitDefaultValue = false)] property bool EmulateOnly;
        [DataMember(Name = "programArgs", Order = 12, EmitDefaultValue = false)] property List<String^>^ ProgramArgs;
        [DataMember(Name = "visualStudio", Order = 14)] property String^ VisualStudio;
    };

    public ref class VsImporter abstract sealed
    {
    public:
        static bool IsProjectFile(String^ path) { return VsProject::IsProjectFile(path); }
        static bool IsSolution(String^ path) { return VsSolution::IsSolution(path); }
        static bool IsVsFile(String^ path) { return IsProjectFile(path) || IsSolution(path); }

        // the names of a solution's C++ projects, in its order
        static List<String^>^ CppProjectNames(String^ slnPath)
        {
            List<String^>^ r = gcnew List<String^>();
            for each (VsSolutionEntry^ e in VsSolution::Read(slnPath)->CppProjects()) r->Add(e->Name);
            return r;
        }

        // a .sln (its one C++ project, or the one member names; several and none named: refused, naming
        // them) or a .vcxproj / .vcproj (read with the solution that lists it, when one is found)
        static GrideProject^ Import(String^ path, String^ member)
        {
            String^ full = Path::GetFullPath(path);
            if (!File::Exists(full)) throw gcnew IOException(String::Format("not found: {0}", full));
            if (!IsSolution(full)) return Read(full, SolutionOf(full));
            VsSolution^ s = VsSolution::Read(full);
            List<VsSolutionEntry^>^ cpp = s->CppProjects();
            List<String^>^ names = gcnew List<String^>();
            for each (VsSolutionEntry^ c in cpp) names->Add(c->Name);
            String^ leaf = Path::GetFileName(full);
            if (cpp->Count == 0)
                throw gcnew IOException(String::Format("{0} holds no C++ project{1}", leaf, s->Notes->Count == 0 ? String::Empty : String::Concat(" (", String::Join("; ", s->Notes), ")")));
            VsSolutionEntry^ e = nullptr;
            if (!String::IsNullOrEmpty(member))
            {
                for each (VsSolutionEntry^ c in cpp)
                    if (String::Compare(c->Name, member, true) == 0 || PathUtil::SamePath(c->Path, member)) { e = c; break; }
                if (e == nullptr) throw gcnew IOException(String::Format("{0} has no C++ project {1} - its C++ projects: {2}", leaf, member, String::Join(", ", names)));
            }
            else if (cpp->Count == 1) e = cpp[0];
            else throw gcnew IOException(String::Format("{0} holds {1} C++ projects - open one of them: {2}", leaf, cpp->Count, String::Join(", ", names)));
            GrideProject^ p = Read(e->Path, full);
            SolutionLines(p, s, e);
            return p;
        }

        // the solution a project file is listed in: a .sln in its folder or up to three folders above
        // naming it (the first found), else nullptr - $(SolutionDir) then stays unresolved
        static String^ SolutionOf(String^ projectPath)
        {
            try
            {
                String^ dir = Path::GetDirectoryName(Path::GetFullPath(projectPath));
                for (int up = 0; up < 4 && !String::IsNullOrEmpty(dir); up++)
                {
                    array<String^>^ slns = Directory::GetFiles(dir, "*.sln");
                    Array::Sort(slns, StringComparer::OrdinalIgnoreCase);
                    for each (String^ sln in slns)
                    {
                        try
                        {
                            for each (VsSolutionEntry^ c in VsSolution::Read(sln)->CppProjects())
                                if (PathUtil::SamePath(c->Path, projectPath)) return sln;
                        }
                        catch (Exception^) { }
                    }
                    dir = Path::GetDirectoryName(dir);
                }
            }
            catch (Exception^) { }
            return nullptr;
        }

        // the reading as a GRIDE project (solutionPath may be empty: a project read alone)
        static GrideProject^ Read(String^ path, String^ solutionPath)
        {
            String^ solutionDir = String::IsNullOrWhiteSpace(solutionPath) ? String::Empty : Path::GetDirectoryName(Path::GetFullPath(solutionPath));
            VsProject^ r = VsProject::Read(path, solutionDir);
            GrideProject^ p = gcnew GrideProject();
            p->Name = r->Name;
            p->OutputName = r->Name;
            p->Target = Names::TargetName(TargetKind::Windows);
            p->FilePath = r->Path;
            p->Configurations = gcnew List<BuildConfiguration^>();
            p->Vs = gcnew VsInfo();
            p->Vs->Reading = r;
            p->Vs->SolutionPath = String::IsNullOrWhiteSpace(solutionPath) ? String::Empty : Path::GetFullPath(solutionPath);
            p->Vs->Version = r->Version;
            p->Vs->Toolset = r->Toolset;
            p->Vs->Guid = r->Guid;
            p->Vs->Label = LabelOf(r);
            for each (VsProjectConfig^ c in r->Configurations) p->Vs->Configurations->Add(c->Name);

            // the files, in their filters (else their directories); a source the project does not compile
            // (a None item) and one excluded everywhere are shown and never built
            bool anyCpp = false, anyC = false;
            List<String^>^ notBuilt = gcnew List<String^>();
            for each (VsProjectItem^ i in r->Items)
            {
                String^ rel;
                try { rel = PathUtil::MakeRelative(p->ProjectDir, i->Path); }
                catch (Exception^) { rel = i->Path; }
                String^ folder = i->Filter;
                if (folder->Length == 0)
                {
                    String^ dir = Path::GetDirectoryName(rel->Replace(L'/', L'\\'));
                    folder = Path::IsPathRooted(rel) || rel->StartsWith("..") ? Str("(outside)") : String::IsNullOrEmpty(dir) ? Str("(project root)") : dir;
                }
                if (!p->SourceFiles->Contains(rel)) { p->SourceFiles->Add(rel); p->Vs->FolderOf[rel] = folder; }
                String^ e = Path::GetExtension(i->Path)->ToLowerInvariant();
                bool source = e == ".cpp" || e == ".cxx" || e == ".cc" || e == ".c" || e == ".c++";
                bool compiled = String::Compare(i->Type, "ClCompile", true) == 0;
                if (compiled) { if (e == ".c") anyC = true; else anyCpp = true; }
                if ((source && !compiled) || i->Excluded->Contains("*")) if (!notBuilt->Contains(i->Path)) notBuilt->Add(i->Path);
            }
            p->Language = Names::LanguageName(anyCpp || !anyC ? LanguageKind::Cpp11 : LanguageKind::C90);

            // Debug and Release for Windows from the project's configurations: x64 before Win32; the rest are said
            List<String^>^ mappedFrom = gcnew List<String^>();
            List<String^>^ lines = gcnew List<String^>();
            bool outputNamed = false;
            for each (String^ name in gcnew array<String^>{ "Debug", "Release" })
            {
                VsProjectConfig^ c = Pick(r, name);
                BuildConfiguration^ b = gcnew BuildConfiguration(name);
                b->OutputDir = Path::Combine(r->Dir, String::Concat(".gride\\out\\", name));
                b->ExcludedFiles->AddRange(notBuilt);
                CcsConfiguration^ shown = gcnew CcsConfiguration();
                if (c != nullptr)
                {
                    mappedFrom->Add(String::Format("{0} from {1}", name, c->Name));
                    p->Vs->MappedConfigs[name] = c->Name;
                    shown->Name = String::Format("{0} (from {1})", name, c->Name);
                    b->Defines->AddRange(c->Defines);
                    b->IncludePathsHere->AddRange(c->IncludeDirs);
                    b->LibrariesHere->AddRange(c->Libs);
                    b->LibraryDirs->AddRange(c->LibDirs);
                    b->Optimise = OptimiseOf(c);
                    b->VsConfig = c;
                    List<String^>^ extra = gcnew List<String^>();
                    for each (String^ u in c->Undefines) extra->Add(String::Concat("-U", u));
                    b->Flags = String::Join(" ", extra);
                    if (c->AdditionalOptions->Length > 0)
                        lines->Add(String::Format("VS project {0} ({1}): cl's additional options go to cl only, not to GRIDE's compilers: {2}", r->Name, c->Name, c->AdditionalOptions));
                    if (c->ConfigurationType->Length > 0 && String::Compare(c->ConfigurationType, "Application", true) != 0)
                        lines->Add(String::Format("VS project {0} ({1}): a {2} in Visual Studio - GRIDE 2.0 links a program of it", r->Name, c->Name, c->ConfigurationType));
                    if (!outputNamed && c->TargetName->Length > 0 && !VsProject::HasMacro(c->TargetName)) { p->OutputName = c->TargetName; outputNamed = true; }
                    for each (VsProjectItem^ i in r->Items)
                        if (i->ExcludedIn(c->Name) && !i->Excluded->Contains("*") && !b->ExcludedFiles->Contains(i->Path)) b->ExcludedFiles->Add(i->Path);
                    for each (String^ n in c->Notes) lines->Add(String::Format("VS project {0} ({1}): not mapped - {2}", r->Name, c->Name, n));
                    // what the tree shows of it
                    if (c->Defines->Count > 0) shown->Options->Add(String::Concat("defines: ", String::Join(";", c->Defines)));
                    if (c->Undefines->Count > 0) shown->Options->Add(String::Concat("undefines: ", String::Join(";", c->Undefines)));
                    for each (String^ d in c->IncludeDirs) shown->Options->Add(String::Concat("include directory: ", d));
                    if (c->Optimization->Length > 0) shown->Options->Add(String::Format("optimization: {0} ($(Optimise) {1})", c->Optimization, b->Optimise));
                    if (c->Libs->Count > 0) shown->Options->Add(String::Concat("libraries: ", String::Join(";", c->Libs)));
                    for each (String^ d in c->LibDirs) shown->Options->Add(String::Concat("library directory: ", d));
                }
                else
                {
                    b->Optimise = name == "Debug" ? Str("O0") : Str("O2");
                    shown->Name = String::Format("{0} (GRIDE's defaults)", name);
                    lines->Add(String::Format("VS project {0}: no {1} configuration in the file - GRIDE's defaults for {1}", r->Name, name));
                }
                for each (String^ x in b->ExcludedFiles) shown->Options->Add(String::Concat("not built: ", PathUtil::MakeRelative(p->ProjectDir, x)));
                p->CcsConfigurations->Add(shown);
                p->Configurations->Add(b);
            }
            List<String^>^ others = gcnew List<String^>();
            for each (VsProjectConfig^ c in r->Configurations)
                if (!p->Vs->MappedConfigs->ContainsValue(c->Name)) others->Add(c->Name);
            p->Vs->ReportLines->Add(String::Format("VS project {0}: {1}; {2}{3}", r->Name, r->Version, String::Join(", ", mappedFrom),
                                                   others->Count > 0 ? String::Concat("; not built here: ", String::Join(", ", others)) : String::Empty));
            p->Vs->ReportLines->AddRange(lines);
            for each (String^ n in r->Notes) p->Vs->ReportLines->Add(String::Format("VS project {0}: not mapped - {1}", r->Name, n));
            p->Normalize();
            p->IsReadOnly = true;
            return p;
        }

        // a .pro: GRIDE's own project, or (R12) one standing for a Visual Studio project - that project, read as
        // it is, with the .pro's compiler word, configuration, arguments and emulateOnly over it
        static GrideProject^ LoadPro(String^ path)
        {
            GrideProject^ own = GrideProject::Load(path);
            if (String::IsNullOrWhiteSpace(own->VisualStudio)) return own;
            String^ full = Path::GetFullPath(path);
            String^ named = PathUtil::Resolve(Path::GetDirectoryName(full), own->VisualStudio);
            if (!File::Exists(named))
                throw gcnew IOException(String::Format("{0} names the Visual Studio file {1}, which is not there", Path::GetFileName(full), own->VisualStudio));
            GrideProject^ p = Import(named, nullptr);
            p->Toolchain = own->Toolchain;
            p->ProgramArgs = own->ProgramArgs == nullptr ? nullptr : gcnew List<String^>(own->ProgramArgs);
            p->EmulateOnly = own->EmulateOnly;
            if (p->FindConfig(own->ActiveConfiguration) != nullptr) p->ActiveConfiguration = p->FindConfig(own->ActiveConfiguration)->Name;
            p->Vs->StateFile = full;
            p->Normalize();
            p->Vs->ReportLines->Insert(1, String::Format("VS project {0}: opened through {1}, which keeps GRIDE's compiler ({2}), configuration and arguments for it",
                                                         p->Name, Path::GetFileName(full), String::IsNullOrEmpty(RawToolchain(full)) ? Str("auto") : RawToolchain(full)));
            return p;
        }

        // GRIDE's state for a Visual Studio project, written into the .pro that stands for it (only the .pro: the
        // Visual Studio files are never written). A compiler word meaning the compiler already written is kept as
        // written (cl stays cl, not RIDE's msvc), so a save with nothing changed leaves the file as it was.
        static void SaveState(GrideProject^ p)
        {
            if (p == nullptr || !p->IsVs || String::IsNullOrEmpty(p->Vs->StateFile)) return;
            GrideProject^ was = GrideProject::Load(p->Vs->StateFile);
            VsProjectState^ st = gcnew VsProjectState();
            st->FormatVersion = GrideProject::CurrentFormatVersion;
            st->Name = String::IsNullOrWhiteSpace(was->Name) ? p->Name : was->Name;
            st->ActiveConfiguration = p->ActiveConfiguration;
            String^ written = RawToolchain(p->Vs->StateFile);
            st->Toolchain = written != nullptr && String::Equals(ToolWords::Canonical(written), ToolWords::Canonical(p->Toolchain)) ? written : p->Toolchain;
            st->EmulateOnly = p->EmulateOnly;
            st->ProgramArgs = p->ProgramArgs != nullptr && p->ProgramArgs->Count > 0 ? gcnew List<String^>(p->ProgramArgs) : nullptr;
            st->VisualStudio = was->VisualStudio;
            JsonSave<VsProjectState>(p->Vs->StateFile, st);
        }

        // "Visual Studio 2022 v143", "Visual Studio 2008" - the title's and the tree's label
        static String^ LabelOf(VsProject^ r)
        {
            String^ v = r->Version;
            int comma = v->IndexOf(L',');
            String^ vs = comma > 0 ? v->Substring(0, comma) : v;
            return r->Toolset->Length > 0 ? String::Concat(vs, " ", r->Toolset) : vs;
        }

    private:
        // the compiler word as the .pro spells it (GrideProject keeps it canonical)
        static String^ RawToolchain(String^ pro)
        {
            try { VsProjectState^ st = JsonLoad<VsProjectState>(pro); return st == nullptr ? nullptr : st->Toolchain; }
            catch (Exception^) { return nullptr; }
        }

        // the solution's lines about the project: its notes, the projects it depends on (Visual Studio's to build)
        static void SolutionLines(GrideProject^ p, VsSolution^ s, VsSolutionEntry^ e)
        {
            p->Vs->SolutionFolder = s->FolderPathOf(e);
            List<String^>^ deps = gcnew List<String^>();
            for each (String^ g in e->Dependencies) { VsSolutionEntry^ d = s->ByGuid(g); deps->Add(d == nullptr ? g : d->Name); }
            if (deps->Count > 0)
                p->Vs->ReportLines->Add(String::Format("VS project {0} depends on {1} in {2} - GRIDE 2.0 builds one project; build {3} in Visual Studio",
                                                       e->Name, String::Join(", ", deps), Path::GetFileName(s->Path), deps->Count == 1 ? Str("it") : Str("them")));
            for each (String^ n in s->Notes) p->Vs->ReportLines->Add(String::Format("VS solution {0}: {1}", s->Name, n));
        }

        // the project's configuration GRIDE's Debug / Release is built from: that name on x64, else Win32, else the first platform
        static VsProjectConfig^ Pick(VsProject^ r, String^ name)
        {
            for each (String^ platform in gcnew array<String^>{ "x64", "Win32", "" })
                for each (VsProjectConfig^ c in r->Configurations)
                    if (String::Compare(c->Configuration, name, true) == 0 && (platform->Length == 0 || String::Compare(c->Platform, platform, true) == 0)) return c;
            return nullptr;
        }

        // cl's Optimization as the -O word ($(Optimise)): Disabled O0, MinSpace O1, MaxSpeed O2, Full O3; unsaid: the configuration's kind
        static String^ OptimiseOf(VsProjectConfig^ c)
        {
            String^ o = c->Optimization;
            if (String::Compare(o, "Disabled", true) == 0) return "O0";
            if (String::Compare(o, "MinSpace", true) == 0) return "O1";
            if (String::Compare(o, "MaxSpeed", true) == 0) return "O2";
            if (String::Compare(o, "Full", true) == 0) return "O3";
            return c->IsDebugLike ? Str("O0") : Str("O2");
        }
    };
}
