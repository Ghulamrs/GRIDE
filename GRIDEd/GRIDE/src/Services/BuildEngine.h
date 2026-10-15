// =============================================================================
//  BuildEngine.h - turns projects into programs with the configured toolchain
//
//    source (.c/.cpp/.shl) --compile--> .asm --assemble--> .obj --link--> program
//    source (.asm)                       ------assemble--> .obj
//
//  Every step is a command template from ToolchainSettings, expanded by
//  MacroExpander and run by ToolRunner on a worker thread. Missing tools give
//  a clear error in the Output pane instead of a crash.
// =============================================================================
#pragma once
#include "MacroExpander.h"
#include "ToolRunner.h"
#include "../Model/Workspace.h"
#include "../Settings/GrideSettings.h"

namespace Gride { namespace Services {

    using namespace Gride::Core;
    using namespace Gride::Model;
    using namespace Gride::Settings;

    public enum class BuildAction { Build, Rebuild, Clean, CompileFile, AssembleOnly, LinkOnly };

    public ref class BuildRequest
    {
    public:
        BuildRequest()
        {
            Projects = gcnew List<GrideProject^>();
            Config = "Debug"; TargetKind = Target::Windows;
            Action = BuildAction::Build; SingleFile = nullptr; WorkspaceDir = String::Empty;
        }
        property List<GrideProject^>^ Projects;   // already in build order
        property String^ Config;
        property Target TargetKind;
        property BuildAction Action;
        property String^ SingleFile;               // for CompileFile
        property String^ WorkspaceDir;
    };

    public ref class BuildSummary
    {
    public:
        BuildSummary()
        {
            Succeeded = 0; Failed = 0; UpToDate = 0; Skipped = 0; Errors = 0; Warnings = 0;
            Cancelled = false; Action = BuildAction::Build;
        }
        property int Succeeded;
        property int Failed;
        property int UpToDate;
        property int Skipped;
        property int Errors;
        property int Warnings;
        property bool Cancelled;
        property TimeSpan Elapsed;
        property BuildAction Action;
        property bool Ok { bool get() { return Failed == 0 && !Cancelled; } }
    };

    public delegate void BuildFinishedHandler(BuildSummary^ s);

    public ref class BuildEngine
    {
    public:
        BuildEngine(GrideSettings^ settings)
        {
            this->settings = settings;
            runner = gcnew ToolRunner();
            runner->Line += gcnew TextLineHandler(this, &BuildEngine::OnToolLine);
            busy = false;
        }

        event TextLineHandler^ Output;            // worker thread
        event BuildMessageHandler^ Message;       // worker thread
        event BuildFinishedHandler^ Finished;     // worker thread

        property bool IsBuilding { bool get() { return busy; } }
        property GrideSettings^ CurrentSettings { GrideSettings^ get() { return settings; } void set(GrideSettings^ s) { settings = s; } }

        // ---- public API -----------------------------------------------------------
        bool StartAsync(BuildRequest^ r)
        {
            List<BuildRequest^>^ one = gcnew List<BuildRequest^>();
            one->Add(r);
            return StartBatchAsync(one);
        }

        // several requests (Build > Batch Build) run one after another
        bool StartBatchAsync(List<BuildRequest^>^ requests)
        {
            if (busy || requests == nullptr || requests->Count == 0) return false;
            busy = true;
            pending = requests;
            System::Threading::Thread^ t = gcnew System::Threading::Thread(
                gcnew System::Threading::ThreadStart(this, &BuildEngine::ThreadMain));
            t->IsBackground = true;
            t->Name = "GRIDE build";
            t->Start();
            return true;
        }

        void Cancel() { runner->Cancel(); }

        // full path of the linked program for a project/config/target
        static String^ ProgramPath(GrideProject^ p, BuildConfiguration^ cfg)
        {
            String^ name = String::IsNullOrEmpty(cfg->OutputName) ? p->Name : cfg->OutputName;
            return Path::Combine(p->ToAbsolute(cfg->OutDir), name + TargetInfo::ProgramExtension(cfg->TargetKind));
        }

        // source files that take part in a build (compiled languages + assembly)
        List<String^>^ BuildableFiles(GrideProject^ p)
        {
            List<String^>^ r = gcnew List<String^>();
            for each (String^ f in p->AllFiles())
            {
                if (p->IsExcluded(f)) continue;
                Language l = settings->DetectLanguage(f, p->Lang);
                if (PathUtil::HasExtension(f, ".h") || PathUtil::HasExtension(f, ".hpp")) continue;
                if (LanguageInfo::IsCompiled(l) || l == Language::Assembly) r->Add(f);
            }
            return r;
        }

    private:
        GrideSettings^ settings;
        ToolRunner^ runner;
        List<BuildRequest^>^ pending;
        BuildSummary^ summary;
        GrideProject^ currentProject;
        String^ currentWorkDir;
        bool busy;

        void Say(String^ s) { Output(s); }

        void OnToolLine(String^ line)
        {
            Output(line);
            BuildMessage^ m = BuildMessageParser::TryParse(line, currentWorkDir);
            if (m == nullptr) return;
            if (currentProject != nullptr) m->Project = currentProject->Name;
            if (m->Sev == Severity::Error) summary->Errors++;
            else if (m->Sev == Severity::Warning) summary->Warnings++;
            Message(m);
        }

        void Report(Severity sev, String^ file, String^ text)
        {
            BuildMessage^ m = gcnew BuildMessage();
            m->Sev = sev; m->File = file == nullptr ? String::Empty : file; m->Text = text;
            m->Project = currentProject != nullptr ? currentProject->Name : String::Empty;
            if (sev == Severity::Error) summary->Errors++;
            else if (sev == Severity::Warning) summary->Warnings++;
            String^ prefix = "info";
            if (sev == Severity::Error) prefix = "error";
            else if (sev == Severity::Warning) prefix = "warning";
            Say(prefix + ": " + text);
            Message(m);
        }

        void ThreadMain()
        {
            List<BuildRequest^>^ requests = pending;
            BuildRequest^ first = requests[0];
            summary = gcnew BuildSummary();
            summary->Action = first->Action;
            DateTime start = DateTime::Now;
            runner->Reset();
            try
            {
                for each (BuildRequest^ r in requests)
                    for each (GrideProject^ p in r->Projects)
                    {
                        if (runner->Cancelled) break;
                        currentProject = p;
                        int result = BuildProject(p, r);
                        if (result > 0) summary->Succeeded++;
                        else if (result == 0) summary->UpToDate++;
                        else if (result == -2) summary->Skipped++;
                        else summary->Failed++;
                    }
            }
            catch (Exception^ ex)
            {
                Report(Severity::Error, nullptr, "internal build error: " + ex->Message);
                summary->Failed++;
            }
            summary->Cancelled = runner->Cancelled;
            summary->Elapsed = DateTime::Now - start;
            currentProject = nullptr;

            String^ verb = "Build";
            if (first->Action == BuildAction::Clean) verb = "Clean";
            else if (first->Action == BuildAction::Rebuild) verb = "Rebuild All";
            if (summary->Cancelled) Say("========== " + verb + " cancelled ==========");
            else Say(String::Format("========== {0}: {1} succeeded, {2} failed, {3} up-to-date, {4} skipped ==========",
                                    verb, summary->Succeeded, summary->Failed, summary->UpToDate, summary->Skipped));
            Say(String::Format("Elapsed {0:0.00} s", summary->Elapsed.TotalSeconds));
            busy = false;
            Finished(summary);
        }

        // ---- per project -------------------------------------------------------------
        // returns 1 = built, 0 = up to date, -1 = failed, -2 = skipped
        int BuildProject(GrideProject^ p, BuildRequest^ r)
        {
            Target t = r->TargetKind;
            String^ actionName = "Build";
            if (r->Action == BuildAction::Clean) actionName = "Clean";
            else if (r->Action == BuildAction::Rebuild) actionName = "Rebuild All";
            Say(String::Format("------ {0} started: Project {1}, Configuration {2} | {3} ------",
                               actionName, p->Name, r->Config, TargetInfo::Name(t)));

            if (!p->SupportsTarget(t))
            {
                Say("Project " + p->Name + " does not list target " + TargetInfo::Name(t) + " - skipped.");
                return -2;
            }

            ToolchainSettings^ tc = settings->Toolchain(t);
            BuildConfiguration^ cfg = p->GetConfiguration(r->Config, t);
            String^ objDir = p->ToAbsolute(cfg->ObjDir);
            String^ outDir = p->ToAbsolute(cfg->OutDir);
            currentWorkDir = p->ProjectDir;

            if (r->Action == BuildAction::Clean || r->Action == BuildAction::Rebuild)
            {
                CleanDir(objDir);
                CleanDir(outDir);
                if (r->Action == BuildAction::Clean) return 1;
            }

            PathUtil::EnsureDirectory(objDir);
            PathUtil::EnsureDirectory(outDir);

            MacroExpander^ mx = BaseMacros(p, cfg, tc, r);
            List<String^>^ objects = gcnew List<String^>();
            bool anyWork = false;
            DateTime newestHeader = NewestHeader(p);

            List<String^>^ sources = BuildableFiles(p);
            if (r->Action == BuildAction::CompileFile)
            {
                sources = gcnew List<String^>();
                if (r->SingleFile != nullptr) sources->Add(r->SingleFile);
            }

            // ---- compile + assemble each source ----
            if (r->Action != BuildAction::LinkOnly)
            {
                for each (String^ src in sources)
                {
                    if (runner->Cancelled) return -1;
                    Language lang = settings->DetectLanguage(src, p->Lang);
                    String^ name = Path::GetFileNameWithoutExtension(src);
                    String^ asmFile = lang == Language::Assembly ? src : Path::Combine(objDir, name + tc->AsmExt);
                    String^ objFile = Path::Combine(objDir, name + tc->ObjExt);
                    objects->Add(objFile);

                    bool upToDate = r->Action == BuildAction::Build && File::Exists(objFile) &&
                                    File::GetLastWriteTime(objFile) >= File::GetLastWriteTime(src) &&
                                    File::GetLastWriteTime(objFile) >= newestHeader;
                    if (upToDate) continue;
                    anyWork = true;

                    mx->Set("Source", src);
                    mx->Set("SourceDir", Path::GetDirectoryName(src));
                    mx->Set("Name", name);
                    mx->Set("AsmFile", asmFile);
                    mx->Set("ObjFile", objFile);

                    if (lang != Language::Assembly && r->Action != BuildAction::AssembleOnly)
                    {
                        String^ compiler = tc->Resolve(tc->CompilerFor(lang));
                        if (!File::Exists(compiler))
                        {
                            Report(Severity::Error, src, LanguageInfo::Name(lang) + " compiler not found: " + compiler +
                                   "  (set it in Tools > Options > Toolchains > " + tc->Name + ")");
                            return -1;
                        }
                        mx->Set("Compiler", compiler);
                        Say(String::Format("[{0}]  {1}  ->  {2}", LanguageInfo::Name(lang), Path::GetFileName(src),
                                           PathUtil::MakeRelative(p->ProjectDir, asmFile)));
                        if (runner->Run(mx->Expand(tc->CompileCmd), p->ProjectDir) != 0) return -1;
                    }

                    if (String::IsNullOrEmpty(tc->AssembleCmd)) continue;
                    String^ assembler = tc->Resolve(tc->Assembler);
                    if (!File::Exists(assembler))
                    {
                        Report(Severity::Error, src, "assembler not found: " + assembler +
                               "  (set it in Tools > Options > Toolchains > " + tc->Name + ")");
                        return -1;
                    }
                    Say(String::Format("[ASM]  {0}  ->  {1}", Path::GetFileName(asmFile), PathUtil::MakeRelative(p->ProjectDir, objFile)));
                    if (runner->Run(mx->Expand(tc->AssembleCmd), p->ProjectDir) != 0) return -1;
                }
            }
            else
            {
                for each (String^ src in sources)
                    objects->Add(Path::Combine(objDir, Path::GetFileNameWithoutExtension(src) + tc->ObjExt));
            }

            if (r->Action == BuildAction::CompileFile || r->Action == BuildAction::AssembleOnly) return 1;
            if (objects->Count == 0)
            {
                Report(Severity::Warning, nullptr, "project " + p->Name + " has no source files to build");
                return 1;
            }
            if (p->Template != nullptr && p->Template->Contains("Library"))
            {
                Say("Library project: objects are kept in " + PathUtil::MakeRelative(p->ProjectDir, objDir));
                return anyWork ? 1 : 0;
            }

            // ---- link ----
            String^ program = BuildEngine::ProgramPath(p, cfg);
            bool linkUpToDate = !anyWork && r->Action == BuildAction::Build && File::Exists(program);
            if (linkUpToDate)
            {
                for each (String^ o in objects)
                    if (File::Exists(o) && File::GetLastWriteTime(o) > File::GetLastWriteTime(program)) linkUpToDate = false;
            }
            if (linkUpToDate)
            {
                Say(p->Name + " is up to date.");
                return 0;
            }

            String^ linker = tc->Resolve(tc->Linker);
            if (!File::Exists(linker))
            {
                Report(Severity::Error, nullptr, "linker not found: " + linker + "  (set it in Tools > Options > Toolchains > " + tc->Name + ")");
                return -1;
            }
            mx->Set("Objects", MacroExpander::JoinQuoted(objects));
            Say(String::Format("[LINK] {0}", PathUtil::MakeRelative(p->ProjectDir, program)));
            if (runner->Run(mx->Expand(tc->LinkCmd), p->ProjectDir) != 0) return -1;
            return 1;
        }

        MacroExpander^ BaseMacros(GrideProject^ p, BuildConfiguration^ cfg, ToolchainSettings^ tc, BuildRequest^ r)
        {
            MacroExpander^ mx = gcnew MacroExpander();
            mx->Set("ToolDir", tc->ToolDir);
            mx->Set("Assembler", tc->Resolve(tc->Assembler));
            mx->Set("Linker", tc->Resolve(tc->Linker));
            mx->Set("Runtime", tc->Resolve(tc->Runtime));
            mx->Set("Simulator", tc->Resolve(tc->Simulator));
            mx->Set("Config", cfg->Config);
            mx->Set("Target", TargetInfo::Name(cfg->TargetKind));
            mx->Set("ProjectDir", p->ProjectDir);
            mx->Set("ProjectName", p->Name);
            mx->Set("WorkspaceDir", r->WorkspaceDir);
            mx->Set("ObjDir", p->ToAbsolute(cfg->ObjDir));
            mx->Set("OutDir", p->ToAbsolute(cfg->OutDir));
            String^ program = BuildEngine::ProgramPath(p, cfg);
            mx->Set("Output", program);
            mx->Set("Program", program);
            mx->Set("Optimise", cfg->Optimise);
            mx->Set("CompileFlags", cfg->ExtraCompileFlags);
            mx->Set("LinkFlags", cfg->ExtraLinkFlags);
            mx->Set("LinkerCmd", String::IsNullOrEmpty(cfg->LinkerCmd) ? String::Empty : p->ToAbsolute(cfg->LinkerCmd));

            List<String^>^ inc = gcnew List<String^>();
            for each (String^ d in cfg->IncludeDirs) inc->Add(p->ToAbsolute(d));
            inc->AddRange(tc->IncludeDirs);
            mx->Set("IncludeFlags", MacroExpander::JoinFlags(tc->IncludeFlag, inc));
            mx->Set("DefineFlags", MacroExpander::JoinFlags(tc->DefineFlag, cfg->Defines)->Replace("\"", ""));

            List<String^>^ libDirs = gcnew List<String^>();
            for each (String^ d in cfg->LibDirs) libDirs->Add(p->ToAbsolute(d));
            libDirs->AddRange(tc->LibDirs);
            mx->Set("LibDirFlags", MacroExpander::JoinFlags(tc->LibDirFlag, libDirs));
            mx->Set("LibFlags", MacroExpander::JoinFlags(tc->LibFlag, cfg->Libs));
            return mx;
        }

        DateTime NewestHeader(GrideProject^ p)
        {
            DateTime newest = DateTime::MinValue;
            for each (String^ f in p->AllFiles())
            {
                if (!(PathUtil::HasExtension(f, ".h") || PathUtil::HasExtension(f, ".hpp"))) continue;
                if (File::Exists(f))
                {
                    DateTime t = File::GetLastWriteTime(f);
                    if (t > newest) newest = t;
                }
            }
            return newest;
        }

        void CleanDir(String^ dir)
        {
            if (!Directory::Exists(dir)) return;
            int n = 0;
            for each (String^ f in Directory::GetFiles(dir))
            {
                try { File::Delete(f); n++; } catch (Exception^) { }
            }
            Say(String::Format("Cleaned {0} file(s) in {1}", n, dir));
        }
    };
}}
