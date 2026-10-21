// =============================================================================
//  CcsProjectReader.h - read-only import of Code Composer Studio 7.4 / 5.5 projects
//
//  CCS is Eclipse based. A project folder holds three XML files:
//     .project      Eclipse: project name, linked resources
//     .cproject     CDT: build configurations, include paths, defines, libs
//     .ccsproject   TI: device variant, code-generation tools version
//  CcsReading reads them as RIDE does (every option mapped, named as RIDE's own
//  instead, or unsupported; TI's definitions for what CCS left unstored); this
//  turns the reading into an in-memory GrideProject (Kind = "ccs7", IsReadOnly =
//  true). Nothing is ever written back to the CCS files.
// =============================================================================
#pragma once
#include "GrideProject.h"
#include "CcsReading.h"

namespace Gride { namespace Model {

    using namespace System::Xml;
    using namespace Gride::Core;

    public ref class CcsProjectReader abstract sealed
    {
    public:
        // pathOrFolder may be the project folder, its .project or its .ccsproject
        static String^ ResolveFolder(String^ pathOrFolder)
        {
            if (String::IsNullOrEmpty(pathOrFolder)) return nullptr;
            if (Directory::Exists(pathOrFolder)) return Path::GetFullPath(pathOrFolder);
            if (File::Exists(pathOrFolder)) return Path::GetDirectoryName(Path::GetFullPath(pathOrFolder));
            return nullptr;
        }

        static bool IsCcsProject(String^ pathOrFolder)
        {
            String^ dir = ResolveFolder(pathOrFolder);
            if (dir == nullptr) return false;
            return File::Exists(Path::Combine(dir, ".project")) &&
                   (File::Exists(Path::Combine(dir, ".ccsproject")) || File::Exists(Path::Combine(dir, ".cproject")));
        }

        // a CCS workspace folder (.metadata), which is not a project: its members open one at a time
        static bool IsCcsWorkspace(String^ folder) { return CcsWorkspace::IsWorkspace(folder); }

        // cgToolRoot = the CCS root (or the compiler directory) from GRIDE settings (may be empty)
        static GrideProject^ Read(String^ pathOrFolder, String^ cgToolRoot)
        {
            String^ dir = ResolveFolder(pathOrFolder);
            if (dir == nullptr || !IsCcsProject(dir))
                throw gcnew IOException("Not a Code Composer Studio project: " + pathOrFolder);

            // the workspace the folder is registered in, for WORKSPACE_LOC, path variables and build macros
            CcsWorkspace^ ws = nullptr;
            String^ wsDir = CcsWorkspace::WorkspaceOf(dir);
            if (wsDir->Length > 0) { String^ error; ws = CcsWorkspace::Read(wsDir, error); }
            CcsReading^ r = CcsReading::Read(dir, cgToolRoot, ws);

            GrideProject^ p = gcnew GrideProject();
            p->Kind = "ccs7";
            p->IsReadOnly = true;
            p->FilePath = Path::Combine(dir, ".project");
            p->Name = r->Name;
            p->Template = r->CcsVersion->Length == 0 ? "CCS 5.5 project" : "CCS 7.4 project";
            p->Targets->Add(Target::C6747);
            p->Ccs = gcnew CcsInfo();
            p->Ccs->DeviceVariant = r->Device;
            p->Ccs->DeviceFamily = r->Family;
            p->Ccs->CodegenVersion = r->CgtVersion;
            p->Ccs->OutputFormat = r->Elf ? "ELF" : "COFF";
            p->Ccs->Workspace = r->Workspace;
            p->Ccs->Reading = r;
            // the linker command file: the first .cmd of a configuration (the folder's, relative), as the record says
            for each (CcsConfig^ c in r->Configs)
                if (c->Link->CmdFiles->Count > 0) { p->Ccs->LinkerCommandFile = Relative(dir, c->Link->CmdFiles[0]); break; }

            // the files, grouped by their folder relative to the project; linked ones under "(linked)"
            bool anyCpp = false;
            List<String^>^ all = gcnew List<String^>();
            for each (CcsConfig^ c in r->Configs)
            {
                for each (String^ s in c->Sources) if (!all->Contains(s)) all->Add(s);
                for each (String^ s in c->Link->CmdFiles) if (!all->Contains(s)) all->Add(s);
            }
            for each (String^ h in r->Headers) { String^ abs = r->Dir + "/" + h; if (!all->Contains(abs)) all->Add(abs); }
            for each (String^ n in r->NotBuilt) { String^ abs = Path::IsPathRooted(n) ? n : r->Dir + "/" + n; if (!all->Contains(abs)) all->Add(abs); }
            // a file every configuration excludes is still shown, excluded; one some configuration excludes is excluded there
            for each (CcsConfig^ c in r->Configs)
                for each (String^ e in c->Excluded) { String^ abs = r->Dir + "/" + e; if (File::Exists(abs->Replace(L'/', L'\\')) && !all->Contains(abs)) all->Add(abs); }
            for each (String^ absSlashed in all)
            {
                String^ abs = absSlashed->Replace(L'/', L'\\');
                String^ rel = PathUtil::MakeRelative(dir, abs);
                String^ folder = Path::GetDirectoryName(rel->Replace(L'/', L'\\'));
                if (String::IsNullOrEmpty(folder)) folder = "(project root)";
                if (Path::IsPathRooted(rel)) folder = "(linked)";
                folder = folder->Replace(L'\\', L'/');
                ProjectFolder^ f = p->GetOrAddFolder(folder);
                if (!f->Files->Contains(rel)) f->Files->Add(rel);
                if (PathUtil::HasExtension(abs, ".cpp")) anyCpp = true;
            }
            p->Lang = anyCpp ? Language::Cpp11 : Language::C90;

            // the configurations as GRIDEd builds them: RIDE's mapping, into the fields the engine reads
            for each (CcsConfig^ c in r->Configs)
            {
                BuildConfiguration^ b = gcnew BuildConfiguration(c->IsDebug ? "Debug" : "Release", Target::C6747);
                b->Defines->Clear(); b->IncludeDirs->Clear(); b->Libs->Clear(); b->LibDirs->Clear();
                b->OutDir = ".gride/out/" + c->Name;
                b->ObjDir = ".gride/obj/" + c->Name;
                b->Defines->AddRange(c->Defines);
                for each (String^ inc in c->Includes) b->IncludeDirs->Add(Relative(dir, inc));
                b->Optimise = c->Opt->Length == 0 ? "O0" : c->Opt->TrimStart(L'-');
                // the undefines and --no_compress go as the compiler's extra flags (the defines carry the define flag)
                List<String^>^ extra = gcnew List<String^>();
                for each (String^ u in c->Undefines) extra->Add("-U" + u);
                if (c->NoCompress) extra->Add("--no_compress");
                b->ExtraCompileFlags = String::Join(" ", extra);
                for each (String^ s in c->Link->SearchPaths) b->LibDirs->Add(Relative(dir, s));
                b->Libs->AddRange(c->Link->Libraries);
                List<String^>^ link = gcnew List<String^>();
                if (c->Link->Heap->Length > 0) link->Add("--heap_size=" + c->Link->Heap);
                if (c->Link->Stack->Length > 0) link->Add("--stack_size=" + c->Link->Stack);
                if (c->Link->RomModel == 1) link->Add("--rom_model");
                else if (c->Link->RomModel == 0) link->Add("--ram_model");
                b->ExtraLinkFlags = String::Join(" ", link);
                if (c->Link->CmdFiles->Count > 0) b->LinkerCmd = Relative(dir, c->Link->CmdFiles[0]);
                p->Configurations->Add(b);
                List<String^>^ excluded = gcnew List<String^>();
                for each (String^ e in c->Excluded) excluded->Add(e->Replace(L'/', L'\\'));
                // a source of the folder the configuration does not list is excluded there too (a .cmd is not a source)
                p->ExcludedByConfig[b->Config] = excluded;
            }
            if (p->Configurations->Count == 0)
            {
                p->Configurations->Add(gcnew BuildConfiguration("Debug", Target::C6747));
                p->Configurations->Add(gcnew BuildConfiguration("Release", Target::C6747));
                for each (BuildConfiguration^ c in p->Configurations)
                {
                    c->OutDir = ".gride/out/" + c->Config;
                    c->ObjDir = ".gride/obj/" + c->Config;
                }
            }
            if (!String::IsNullOrEmpty(p->Ccs->LinkerCommandFile))
                for each (BuildConfiguration^ c in p->Configurations)
                    if (String::IsNullOrEmpty(c->LinkerCmd)) c->LinkerCmd = p->Ccs->LinkerCommandFile;

            // the lines the Output pane shows (RIDE's Messages and the sources line)
            for each (CcsConfig^ c in r->Configs) p->Ccs->ReportLines->Add(r->Report(c->IsDebug));
            String^ sources = r->SourceReport();
            if (sources->Length > 0) p->Ccs->ReportLines->Add(sources);
            for each (String^ refusal in r->Refusals) p->Ccs->ReportLines->Add("CCS project " + r->Name + ": GRIDE would refuse it - " + refusal + "; read all the same, the build stands on what was read");
            p->IsDirty = false;
            return p;
        }

    private:
        // a path inside the project folder is kept relative; one outside stays absolute
        static String^ Relative(String^ dir, String^ absSlashed)
        {
            String^ abs = absSlashed->Replace(L'/', L'\\');
            try
            {
                if (Path::IsPathRooted(abs))
                {
                    String^ rel = PathUtil::MakeRelative(dir, abs);
                    if (!rel->StartsWith("..") && !Path::IsPathRooted(rel)) return rel;
                }
            }
            catch (Exception^) { }
            return abs;
        }
    };
}}
