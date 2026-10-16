#pragma once
// ============================================================================
//  GRIDE 2.0 - Code Composer Studio 7.4 / 5.5 projects (Phase 11, RIDE's
//  reading since the RIDE port's R06)
//  A CCS project folder holds three XML files - .project (Eclipse: the name,
//  linked resources), .ccsproject (TI: the device, the compiler version, ELF or
//  COFF), .cproject (CDT: the configurations and their options). CcsReading
//  (CcsReading.h) reads them as RIDE does; this turns the reading into a
//  READ-ONLY GrideProject that builds as RIDE builds one: the C6747, each
//  configuration's mapped options, its exclusions, its linker command file and
//  libraries, the program in the temporary directory - nothing is ever written
//  into the CCS folder. The project is read afresh at every open and build.
// ============================================================================
#include "../Toolchain/Toolchain.h"

namespace Gride
{
    public ref class CcsImporter abstract sealed
    {
    public:
        // .project, .ccsproject or .cproject: each opens its folder (RIDE)
        static bool IsProjectFile(String^ path) { return CcsReading::IsProjectFile(path); }

        // path = the project folder or one of its three files. A workspace folder is refused with RIDE's
        // sentence naming its projects; RIDE's refusals of a project are notes, and the reading goes on.
        static GrideProject^ Import(String^ path)
        {
            String^ full = Path::GetFullPath(path);
            String^ dir = Directory::Exists(full) ? full : Path::GetDirectoryName(full);
            if (!CcsReading::IsProject(dir) && CcsWorkspace::IsWorkspace(dir))
            {
                String^ error;
                CcsWorkspace^ ws = CcsWorkspace::Read(dir, error);
                List<String^>^ names = gcnew List<String^>();
                if (ws != nullptr) for each (CcsWorkspaceMember^ m in ws->Projects) names->Add(m->Name);
                throw gcnew IOException(String::Format("{0} is a CCS workspace, not a project - open one of its projects: {1}",
                                                       Path::GetFileName(dir->TrimEnd(L'\\', L'/')), names->Count == 0 ? Str("(none registered)") : String::Join(", ", names)));
            }
            // the workspace the folder is registered in gives WORKSPACE_LOC, the path variables and the build macros
            CcsWorkspace^ ws = nullptr;
            String^ wsDir = CcsWorkspace::WorkspaceOf(dir);
            if (wsDir->Length > 0) { String^ error; ws = CcsWorkspace::Read(wsDir, error); }
            CcsReading^ r = CcsReading::Read(dir, CompilerRoot(), ws);
            return FromReading(r);
        }

        // what ${CG_TOOL_ROOT} is found from, in RIDE's order: the CCS installation named under Tools > Options
        // (ccs.root: the project's compiler version under its tools\compiler), else TI's compiler directory -
        // named there (ti), else the newest one found under the ccs section's roots
        static String^ CompilerRoot()
        {
            String^ root = IdeSettingsPaths::CcsRoot;
            if (!String::IsNullOrWhiteSpace(root)) return System::Environment::ExpandEnvironmentVariables(root->Trim()->Trim(L'"'));
            String^ ti = TiCompiler::Dir();
            return ti == nullptr ? String::Empty : ti;
        }

        // the reading as a GRIDE project
        static GrideProject^ FromReading(CcsReading^ r)
        {
            String^ dir = r->Dir->Replace(L'/', L'\\');
            GrideProject^ p = gcnew GrideProject();
            p->Name = r->Name;
            p->OutputName = r->Name;
            p->Target = Names::TargetName(TargetKind::TMS6747);
            p->FilePath = Path::Combine(dir, ".project");
            p->Configurations = gcnew List<BuildConfiguration^>();

            // the files: every configuration's sources and command files, the folder's headers, the kinds RIDE
            // does not build, and an excluded file all the same (shown, not built)
            List<String^>^ all = gcnew List<String^>();
            for each (CcsConfig^ c in r->Configs)
            {
                for each (String^ s in c->Sources) AddUnique(all, s);
                for each (String^ s in c->Link->CmdFiles) AddUnique(all, s);
                for each (String^ e in c->Excluded) { String^ abs = r->Dir + "/" + e; if (File::Exists(Native(abs))) AddUnique(all, abs); }
            }
            for each (String^ h in r->Headers) AddUnique(all, r->Dir + "/" + h);
            for each (String^ n in r->NotBuilt) AddUnique(all, Rooted(n) ? n : r->Dir + "/" + n);
            bool cpp = false;
            for each (String^ f in all)
            {
                p->AddFile(Native(f));
                String^ ext = Path::GetExtension(f)->ToLowerInvariant();
                if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c++") cpp = true;
            }
            p->Language = Names::LanguageName(cpp ? LanguageKind::Cpp11 : LanguageKind::C90);

            // the configurations as GRIDE builds them: RIDE's mapping into the fields the engine reads
            for each (CcsConfig^ c in r->Configs)
            {
                BuildConfiguration^ b = gcnew BuildConfiguration(c->IsDebug ? "Debug" : "Release");
                b->Defines->AddRange(c->Defines);
                // the compilers' configuration flags: CCS's optimisation (RIDE's default where it stored none:
                // -O2 for Release, nothing for Debug; -O0 is never written) and -g as CCS says (RIDE's default
                // where it did not); --no_compress is cpp11's (RIDE's cpp11.compress)
                List<String^>^ flags = gcnew List<String^>();
                String^ opt = c->Opt->Length > 0 ? c->Opt : (c->IsDebug ? String::Empty : Str("-O2"));
                if (opt->Length > 0 && opt != "-O0") flags->Add(opt);
                if (c->DebugSaid ? c->Debug : c->IsDebug) flags->Add("-g");
                String^ common = String::Join(" ", flags);
                b->ConfigFlagsByRole = gcnew Dictionary<String^, String^>();
                b->ConfigFlagsByRole["cCompiler"] = common;
                b->ConfigFlagsByRole["cppCompiler"] = c->NoCompress ? (common->Length > 0 ? common + " --no_compress" : Str("--no_compress")) : common;
                List<String^>^ extra = gcnew List<String^>();
                for each (String^ u in c->Undefines) extra->Add("-U" + u);
                b->Flags = String::Join(" ", extra);
                List<String^>^ link = gcnew List<String^>();
                if (c->Link->Heap->Length > 0) link->Add("--heap_size=" + c->Link->Heap);
                if (c->Link->Stack->Length > 0) link->Add("--stack_size=" + c->Link->Stack);
                if (c->Link->RomModel == 1) link->Add("--rom_model");
                else if (c->Link->RomModel == 0) link->Add("--ram_model");
                b->LinkFlags = String::Join(" ", link);
                b->OutputDir = Path::Combine(GrideProject::CcsBuildRoot(r->Name), b->Name);
                for each (String^ i in c->Includes) b->IncludePathsHere->Add(Native(i));
                for each (String^ s in c->Link->SearchPaths) b->LibraryDirs->Add(Native(s));
                b->LibrariesHere->AddRange(c->Link->Libraries);
                for each (String^ e in c->Excluded) b->ExcludedFiles->Add(Native(r->Dir + "/" + e));
                b->LinkerCommandFiles = gcnew List<String^>();
                for each (String^ cmd in c->Link->CmdFiles) b->LinkerCommandFiles->Add(Native(cmd));
                p->Configurations->Add(b);

                // what the tree shows under the project (read-only): RIDE's mapping of this configuration
                CcsConfiguration^ shown = gcnew CcsConfiguration();
                shown->Name = c->Name;
                shown->Options->AddRange(c->Mapped);
                for each (String^ cmd in c->Link->CmdFiles) shown->Options->Add("linker command file: " + cmd);
                for each (String^ u in c->Unsupported) shown->Options->Add("not supported, RIDE's default instead: " + u);
                for each (String^ o in c->OwnInstead) shown->Options->Add("RIDE's own instead of: " + o);
                for each (String^ e in c->Excluded) shown->Options->Add("excluded from the build: " + e);
                p->CcsConfigurations->Add(shown);
            }
            // no Debug or Release configuration read (RIDE refuses that, noted): GRIDE's two, still built in the temporary directory
            if (p->Configurations->Count == 0)
                for each (String^ name in gcnew array<String^>{ "Debug", "Release" })
                {
                    BuildConfiguration^ b = gcnew BuildConfiguration(name);
                    b->OutputDir = Path::Combine(GrideProject::CcsBuildRoot(r->Name), name);
                    b->LinkerCommandFiles = gcnew List<String^>();
                    p->Configurations->Add(b);
                }
            p->Normalize();
            p->IsReadOnly = true;
            p->Ccs = r;

            // the lines the Output pane shows at open (RIDE's Messages and the sources line)
            for each (CcsConfig^ c in r->Configs) p->CcsReportLines->Add(r->Report(c->IsDebug));
            String^ sources = r->SourceReport();
            if (sources->Length > 0) p->CcsReportLines->Add(sources);
            for each (String^ refusal in r->Refusals)
                p->CcsReportLines->Add(String::Format("CCS project {0}: RIDE would refuse it - {1}; read all the same, the build stands on what was read", r->Name, refusal));
            return p;
        }

    private:
        static String^ Native(String^ p) { return p->Replace(L'/', L'\\'); }
        static bool Rooted(String^ p) { return p->Length > 1 && (p[1] == L':' || p[0] == L'/' || p[0] == L'\\'); }
        static void AddUnique(List<String^>^ into, String^ one)
        {
            for each (String^ x in into) if (String::Equals(x, one, StringComparison::OrdinalIgnoreCase)) return;
            into->Add(one);
        }
    };
}
