// =============================================================================
//  BuildEngine.h - turns projects into programs with the configured toolchain
//
//    source (.c/.cpp/.shl) --compile--> .s --assemble--> .obj --link--> program
//    source (.asm)                      ------assemble--> .obj
//
//  Every step is a command template from ToolchainSettings (its defaults from
//  toolchain.json), expanded by MacroExpander and run by ToolRunner on a worker
//  thread. The rules are RIDE's (ride-port\RIDE-conventions.md, 4.3-4.7): a
//  compiler per folder, a Shalimar program compiled as one, a Debug build's
//  CodeView through clang and Microsoft's link.exe, the C6747 program through
//  asm6x and lnk6x against RTS6x with RIDE's own linker command file, the
//  environment the compilers read, the native-tools question, and the refusals
//  (a cross target only reaches assembly, a directory that cannot be written to).
//  A part given to cl is Microsoft's: cl writes the object, a Visual Studio
//  project's own cl settings go with it (toolchain.json's projectSettings), no
//  installation header directory stands in front of Visual Studio's, and
//  Microsoft's link.exe links the objects. Each progress line names the program
//  that runs and the file it writes; its whole command line goes to the Output
//  pane when Tools > Options asks for it, and always to the project build's log
//  (<obj folder>\<project>.build.log) with the folder, variables, output and exit code.
//  Missing tools give a clear error in the Output pane instead of a crash.
// =============================================================================
#pragma once
#include "MacroExpander.h"
#include "ToolRunner.h"
#include "VisualStudio.h"
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
            Succeeded = 0; Failed = 0; UpToDate = 0; Skipped = 0; errors = 0; warnings = 0;
            Cancelled = false; Action = BuildAction::Build; Program = String::Empty;
        }
        property int Succeeded;
        property int Failed;
        property int UpToDate;
        property int Skipped;
        property int Errors { int get() { return errors; } }
        property int Warnings { int get() { return warnings; } }
        property bool Cancelled;
        property TimeSpan Elapsed;
        property BuildAction Action;
        property String^ Program;      // the last program built (or linked), for a run
        property bool Ok { bool get() { return Failed == 0 && !Cancelled; } }
        // the first error of the build, where RIDE puts the caret (an error in a file nothing has opened opens it)
        property BuildMessage^ FirstError { BuildMessage^ get() { return firstError; } }

        // called from the tool output threads (stdout and stderr run in parallel)
        void Count(Severity s)
        {
            if (s == Severity::Error) System::Threading::Interlocked::Increment(errors);
            else if (s == Severity::Warning) System::Threading::Interlocked::Increment(warnings);
        }

        void Count(BuildMessage^ m)
        {
            Count(m->Sev);
            if (m->Sev == Severity::Error && m->Line > 0 && !String::IsNullOrEmpty(m->File))
                System::Threading::Interlocked::CompareExchange<BuildMessage^>(firstError, m, nullptr);
        }

    private:
        int errors;
        int warnings;
        BuildMessage^ firstError;
    };

    public delegate void BuildFinishedHandler(BuildSummary^ s);
    // the front end's answer to a yes/no question asked from the build thread (the native-tools question)
    public delegate bool BuildQuestionHandler(String^ question);

    public ref class BuildEngine
    {
    public:
        BuildEngine(GrideSettings^ settings)
        {
            this->settings = settings;
            runner = gcnew ToolRunner();
            runner->Line += gcnew TextLineHandler(this, &BuildEngine::OnToolLine);
            workers = gcnew List<ToolRunner^>();
            stepDone = gcnew System::Threading::AutoResetEvent(false);
            projectLines = gcnew List<String^>();
            busy = false;
            Asker = nullptr;
            logPath = nullptr;
            logWriter = nullptr;
            logLock = gcnew Object();
        }

        event TextLineHandler^ Output;            // worker thread
        event BuildMessageHandler^ Message;       // worker thread
        event BuildFinishedHandler^ Finished;     // worker thread

        // who answers the native-tools question; null means nothing here can ask, and the build stands
        property BuildQuestionHandler^ Asker;

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

        void Cancel() { runner->Cancel(); StopWorkers(); }

        // full path of the linked program for a project/config/target
        static String^ ProgramPath(GrideProject^ p, BuildConfiguration^ cfg)
        {
            String^ name = String::IsNullOrEmpty(cfg->OutputName) ? p->ProgramName() : cfg->OutputName;
            return Path::Combine(p->ToAbsolute(cfg->OutDir), name + TargetInfo::ProgramExtension(cfg->TargetKind));
        }

        // include files are never compiled or assembled ( .inc is an assembler include )
        static bool IsHeader(String^ path)
        {
            String^ e = Path::GetExtension(path)->ToLowerInvariant();
            return e == ".h" || e == ".hpp" || e == ".hh" || e == ".hxx" || e == ".inc" || e == ".inl";
        }

        // source files that take part in a build (compiled languages + assembly), from the folders the .pro's "build"
        // names; a CCS project's exclusions are per configuration
        List<String^>^ BuildableFiles(GrideProject^ p) { return BuildableFiles(p, nullptr); }
        List<String^>^ BuildableFiles(GrideProject^ p, String^ configName)
        {
            List<String^>^ r = gcnew List<String^>();
            HashSet<String^>^ seen = gcnew HashSet<String^>(StringComparer::OrdinalIgnoreCase);
            for each (String^ f in p->AllFiles())
            {
                if (p->IsExcludedIn(f, configName) || IsHeader(f) || !p->IsBuilt(f)) continue;
                Language l = settings->DetectLanguage(f, p->Lang);
                if ((LanguageInfo::IsCompiled(l) || l == Language::Assembly) && seen->Add(f)) r->Add(f);
            }
            return r;
        }

        // object / assembly file names: the base name of the source when it is unique
        // in the build, else a name made from its project-relative path
        // (src\a.c -> src_a, lib\a.c -> lib_a), so two a.c never share one a.obj
        static Dictionary<String^, String^>^ StemMap(GrideProject^ p, List<String^>^ sources)
        {
            Dictionary<String^, int>^ count = gcnew Dictionary<String^, int>(StringComparer::OrdinalIgnoreCase);
            HashSet<String^>^ distinct = gcnew HashSet<String^>(StringComparer::OrdinalIgnoreCase);
            for each (String^ s in sources)
            {
                if (!distinct->Add(s)) continue;
                String^ n = Path::GetFileNameWithoutExtension(s);
                int c = 0;
                count->TryGetValue(n, c);
                count[n] = c + 1;
            }
            Dictionary<String^, String^>^ map = gcnew Dictionary<String^, String^>(StringComparer::OrdinalIgnoreCase);
            HashSet<String^>^ used = gcnew HashSet<String^>(StringComparer::OrdinalIgnoreCase);
            for each (String^ s in sources)
            {
                String^ n = Path::GetFileNameWithoutExtension(s);
                if (count[n] == 1 && !map->ContainsKey(s)) { map[s] = n; used->Add(n); }
            }
            for each (String^ s in sources)
                if (!map->ContainsKey(s)) map[s] = MakeStem(p->ToRelative(s), used);
            return map;
        }

        // src/drivers/uart.c -> src_drivers_uart (unique within this build)
        static String^ MakeStem(String^ rel, HashSet<String^>^ used)
        {
            String^ r = rel->Replace(L'/', L'\\');
            if (Path::IsPathRooted(r)) r = Path::GetFileName(r);
            String^ noExt = Path::Combine(Path::GetDirectoryName(r), Path::GetFileNameWithoutExtension(r));
            StringBuilder^ sb = gcnew StringBuilder();
            for each (wchar_t c in noExt)
                sb->Append(Char::IsLetterOrDigit(c) || c == L'-' ? c : L'_');
            String^ stem = sb->ToString()->TrimStart(L'_');
            if (stem->Length == 0) stem = "file";
            String^ unique = stem;
            for (int i = 2; used->Contains(unique); i++) unique = String::Format("{0}_{1}", stem, i);
            used->Add(unique);
            return unique;
        }

        // the .asm the compiler writes for a source file (Disassembly pane); a Shalimar program's is one file named after the program
        String^ AsmFileFor(GrideProject^ p, BuildConfiguration^ cfg, String^ src)
        {
            String^ full = Path::GetFullPath(src);
            List<String^>^ files = BuildableFiles(p);
            if (!files->Contains(full)) files->Add(full);
            String^ stem;
            if (settings->DetectLanguage(full, p->Lang) == Language::Shalimar && ShalimarUnit(p, files)->Count > 1) stem = ProgramStem(p, cfg);
            else if (!StemMap(p, files)->TryGetValue(full, stem)) stem = Path::GetFileNameWithoutExtension(full);
            return Path::Combine(p->ToAbsolute(cfg->ObjDir), stem + settings->Toolchain(cfg->TargetKind)->AsmExt);
        }

        // the Shalimar files of a build: shalimar compiles a program as one (the file with main() and the files it calls into)
        List<String^>^ ShalimarUnit(GrideProject^ p, List<String^>^ sources)
        {
            List<String^>^ r = gcnew List<String^>();
            for each (String^ s in sources) if (settings->DetectLanguage(s, p->Lang) == Language::Shalimar) r->Add(s);
            return r;
        }

        static String^ ProgramStem(GrideProject^ p, BuildConfiguration^ cfg)
        {
            return String::IsNullOrEmpty(cfg->OutputName) ? p->ProgramName() : cfg->OutputName;
        }

        // RIDE's refusal before any compiler runs: the program's directory must take a file
        static String^ Unwritable(String^ dir)
        {
            try
            {
                PathUtil::EnsureDirectory(dir);
                String^ probe = Path::Combine(dir, ".gride-writes-here");
                File::WriteAllText(probe, String::Empty);
                File::Delete(probe);
                return nullptr;
            }
            catch (Exception^)
            {
                return dir + " cannot be written to, and the program would go there - copy the project to a folder of your own, such as Documents, and build it there";
            }
        }

    private:
        GrideSettings^ settings;
        ToolRunner^ runner;
        List<ToolRunner^>^ workers;     // the runners of the compile steps running now, which Cancel stops too
        System::Threading::AutoResetEvent^ stepDone;
        List<BuildRequest^>^ pending;
        BuildSummary^ summary;
        GrideProject^ currentProject;
        String^ currentWorkDir;
        Regex^ currentPattern;      // custom error pattern of the current toolchain (compiled before tools start)
        DiagnosticReader^ reader;   // reads the running tool's lines; one per tool run
        List<String^>^ projectLines; // every line of the current project's build, for the native-tools question
        String^ failedStep;          // compile / assemble / link, when the project failed
        bool placedError;            // a diagnostic naming a file and line was seen: a fault in the source
        bool codeViewWarned;
        bool codeViewMissing;        // a Debug build with no clang: built the Release way (MASM, masm, the linker), no line table
        bool ownCodeView;            // a file of this build wrote its own CodeView: GRIDE's link /DEBUG writes the PDB

        // whether a Debug build has a file whose compiler cannot write its own CodeView, which clang must then carry
        bool NeedsClang(GrideProject^ p, BuildConfiguration^ cfg, Target t)
        {
            for each (String^ src in BuildableFiles(p, cfg->Config))
            {
                Language lang = settings->DetectLanguage(src, p->Lang);
                if (lang == Language::Assembly) continue;
                RideTool tool = RideToolchain::Resolve(RideToolchain::FromWord(p->ToolchainFor(p->FolderOfFile(src))), lang);
                if (RideToolchain::DebugsWithCodeView(tool, t, true) && !RideToolchain::OwnCodeView(tool, t, true)) return true;
            }
            return false;
        }
        String^ writes;              // the file the running compiler writes: its echo of the path is not shown
        int toolLines;               // lines the running tool wrote
        int toolHidden;              // of them, the ones past MaxToolLines: counted, not shown
        String^ toolCommand;         // its command line, shown beneath its lines when it fails
        literal int MaxToolLines = 40;
        bool clSettingsSaid;         // the project's own cl settings were named (once a project build)
        bool rideHeadersSaid;        // an installation header directory kept from cl was named (once)
        bool crtSaid;                // the Windows C runtime of the link was named (once a project build)
        bool busy;
        String^ logPath;             // the project build's log, opened when its first tool runs; null: none
        StreamWriter^ logWriter;
        Object^ logLock;             // a tool's stdout and stderr are read on two threads
        DateTime logStart;
        List<String^>^ loggedEnvironment;   // the variables the command logged before was given

        // The Windows C runtime a link takes: "grt" (GRIDE's) or "msvc" (Microsoft's). Tools > Options says which;
        // automatic is Microsoft's only where Visual Studio is found and the link has cl's objects or a library that
        // names Microsoft's runtime by /DEFAULTLIB - both name its libraries themselves - and GRIDE's otherwise,
        // always where there is no Visual Studio (docs\Runtime_Design.md 13.1).
        String^ WindowsRuntime(bool clObjects, bool microsoftLibrary)
        {
            String^ choice = settings->Env->EffectiveWindowsRuntime();
            if (choice != "auto") return choice;
            return (clObjects || microsoftLibrary) && VisualStudio::Find() != nullptr ? "msvc" : "grt";
        }

        // Whether the archive or object at path names Microsoft's C runtime by /DEFAULTLIB (or -defaultlib:), as a
        // library cl built does: its objects' directives read from its bytes, as link reads them
        static bool NamesMicrosoftRuntime(String^ path)
        {
            array<String^>^ runtimes = gcnew array<String^>{ "libcmt", "libcmtd", "msvcrt", "msvcrtd", "libucrt", "libucrtd",
                "ucrt", "ucrtd", "libvcruntime", "libvcruntimed", "vcruntime", "vcruntimed", "libcpmt", "libcpmtd", "msvcprt", "msvcprtd" };
            try
            {
                FileInfo^ f = gcnew FileInfo(path);
                if (!f->Exists || f->Length > 64LL * 1024 * 1024) return false;
                String^ text = System::Text::Encoding::GetEncoding(28591)->GetString(File::ReadAllBytes(path))->ToLowerInvariant();
                for (int at = text->IndexOf("defaultlib:"); at >= 0; at = text->IndexOf("defaultlib:", at + 11))
                {
                    if (at == 0 || (text[at - 1] != '/' && text[at - 1] != '-')) continue;
                    int i = at + 11;
                    if (i < text->Length && text[i] == '"') i++;
                    int start = i;
                    while (i < text->Length && text[i] != '"' && text[i] != ' ' && text[i] > ' ') i++;
                    String^ name = text->Substring(start, i - start);
                    if (name->EndsWith(".lib")) name = name->Substring(0, name->Length - 4);
                    if (Array::IndexOf(runtimes, name) >= 0) return true;
                }
            }
            catch (Exception^) { }
            return false;
        }

        // Whether a library this host link takes - the configuration's (found as the link finds them, in the project
        // folder and its library folders), the .pro's, the shared ones - names Microsoft's C runtime
        bool LinksMicrosoftLibrary(GrideProject^ p, BuildConfiguration^ cfg)
        {
            List<String^>^ dirs = gcnew List<String^>();
            dirs->Add(p->ProjectDir);
            for each (String^ d in cfg->LibDirs) dirs->Add(p->ToAbsolute(d));
            List<String^>^ named = Filled(p, cfg->Libs);
            for each (String^ l in p->Libraries) named->Add(p->ToAbsolute(l));
            for each (String^ l in settings->Env->SharedLibraries) named->Add(Filled(p, l));
            for each (String^ l in named)
            {
                if (String::IsNullOrWhiteSpace(l)) continue;
                String^ name = l->Trim();
                String^ path = nullptr;
                if (Path::IsPathRooted(name)) path = name;
                else for each (String^ d in dirs) if (File::Exists(Path::Combine(d, name))) { path = Path::Combine(d, name); break; }
                if (path != nullptr && NamesMicrosoftRuntime(path)) return true;
            }
            return false;
        }

        // Microsoft's runtime libraries, each refused to a link with GRIDE's runtime (docs\Runtime_Design.md 13.4)
        literal String^ MicrosoftRuntimeRefused =
            " /NODEFAULTLIB:libcmt /NODEFAULTLIB:libcmtd /NODEFAULTLIB:msvcrt /NODEFAULTLIB:msvcrtd"
            " /NODEFAULTLIB:libucrt /NODEFAULTLIB:libucrtd /NODEFAULTLIB:ucrt /NODEFAULTLIB:ucrtd"
            " /NODEFAULTLIB:libvcruntime /NODEFAULTLIB:libvcruntimed /NODEFAULTLIB:vcruntime /NODEFAULTLIB:vcruntimed"
            " /NODEFAULTLIB:libcpmt /NODEFAULTLIB:libcpmtd /NODEFAULTLIB:msvcprt /NODEFAULTLIB:msvcprtd"
            " /NODEFAULTLIB:oldnames /NODEFAULTLIB:legacy_stdio_definitions";

        void Say(String^ s) { Output(s); Keep(s); }

        void Keep(String^ s)
        {
            System::Threading::Monitor::Enter(projectLines);
            try { projectLines->Add(s); }
            finally { System::Threading::Monitor::Exit(projectLines); }
            Log(s);
        }

        // ---- the project build's log, <obj folder>\<project>.build.log, whatever the Output pane shows ----------
        // The lines of the build, each tool's command line as it ran (> ...), the folder it ran in, the setup script
        // it ran after, the variables GRIDE set for it, all it wrote (the Output pane shows a tool's first
        // MaxToolLines) and its exit code. It is opened when the first tool runs, so a build with nothing to do keeps
        // the last one's; a log that cannot be written is let go - it never stops a build.
        void PrepareLog(GrideProject^ p, String^ objDir)
        {
            if (logPath != nullptr) return;     // the native tools' build again: the same log goes on
            logPath = Path::Combine(objDir, p->Name + ".build.log");
            logStart = DateTime::Now;
            loggedEnvironment = gcnew List<String^>();
        }

        void OpenLog()
        {
            if (logWriter != nullptr || logPath == nullptr) return;
            array<String^>^ before;
            System::Threading::Monitor::Enter(projectLines);
            try { before = projectLines->ToArray(); }
            finally { System::Threading::Monitor::Exit(projectLines); }
            System::Threading::Monitor::Enter(logLock);
            try
            {
                logWriter = gcnew StreamWriter(logPath, false, gcnew UTF8Encoding(false));
                logWriter->AutoFlush = true;
                logWriter->WriteLine(String::Format("GRIDE build log, {0:yyyy-MM-dd HH:mm:ss}", logStart));
                logWriter->WriteLine("Each tool's command line as GRIDE ran it (> ...), the folder it ran in, the variables GRIDE set for it, all it wrote, its exit code.");
                logWriter->WriteLine();
                for each (String^ s in before) logWriter->WriteLine(s);
            }
            catch (Exception^) { logWriter = nullptr; logPath = nullptr; }
            finally { System::Threading::Monitor::Exit(logLock); }
        }

        void Log(String^ s)
        {
            if (logWriter == nullptr) return;
            System::Threading::Monitor::Enter(logLock);
            try { if (logWriter != nullptr) logWriter->WriteLine(s); }
            catch (Exception^) { }
            finally { System::Threading::Monitor::Exit(logLock); }
        }

        // a tool's command line before it runs: in the Output pane when Tools > Options asks for it, always in the
        // log - with the variables GRIDE sets that differ from the command before's, the others named
        void ShowCommand(String^ command, String^ workDir, ToolRunOptions^ options)
        {
            toolCommand = command;
            if (settings->Env->ShowCommandLines) Output("> " + command);
            OpenLog();
            if (logWriter == nullptr) return;
            Log("> " + command);
            Log("  in " + workDir);
            if (options != nullptr && !String::IsNullOrWhiteSpace(options->SetupScript))
                Log("  through cmd.exe, after: call " + options->SetupScript->Trim());
            List<String^>^ env = options == nullptr ? gcnew List<String^>() : options->Environment;
            List<String^>^ same = gcnew List<String^>();
            for each (String^ v in env)
            {
                int eq = v->IndexOf(L'=');
                if (!loggedEnvironment->Contains(v)) Log("  set " + v);
                else same->Add(eq > 0 ? v->Substring(0, eq) : v);
            }
            if (same->Count > 0) Log("  set as before: " + String::Join(", ", same));
            loggedEnvironment = gcnew List<String^>(env);
        }

        void LogExit(int rc, TimeSpan took)
        {
            String^ what = rc == ToolRunner::ExitStartFailed ? " - not started" : rc == ToolRunner::ExitCancelled ? " - cancelled" : String::Empty;
            Log(String::Format("  exit code {0}{1} ({2:0.00} s)", rc, what, took.TotalSeconds));
        }

        // the project build's last line in its log, which is then closed
        void EndLog(int result)
        {
            if (logWriter != nullptr)
            {
                String^ how = result > 0 ? "built" : result == 0 ? "up to date" : result == -2 ? "skipped" : runner->Cancelled ? "cancelled" : "failed";
                Log(String::Format("------ {0}, {1:0.00} s ------", how, (DateTime::Now - logStart).TotalSeconds));
            }
            System::Threading::Monitor::Enter(logLock);
            try { if (logWriter != nullptr) logWriter->Close(); }
            catch (Exception^) { }
            finally { logWriter = nullptr; logPath = nullptr; System::Threading::Monitor::Exit(logLock); }
        }

        bool OutputHas(String^ part)
        {
            System::Threading::Monitor::Enter(projectLines);
            try { for each (String^ l in projectLines) if (l != nullptr && l->IndexOf(part, StringComparison::OrdinalIgnoreCase) >= 0) return true; }
            finally { System::Threading::Monitor::Exit(projectLines); }
            return false;
        }

        // a tool's line as the Output pane shows it: a file of the project named by its full path ("C:\...\proj\src\a.c:7:5:
        // error: ...") from the project's folder ("src\a.c:7:5: error: ..."), so the message stays in view; the Error
        // List keeps the full path, which double-click navigation uses
        String^ Shown(String^ line)
        {
            if (String::IsNullOrEmpty(line) || String::IsNullOrEmpty(currentWorkDir)) return line;
            String^ dir = currentWorkDir->TrimEnd(gcnew array<wchar_t>{ L'\\', L'/' });
            String^ t = line->TrimStart();
            String^ lead = line->Substring(0, line->Length - t->Length);
            for each (String^ d in gcnew array<String^>{ dir + "\\", dir->Replace(L'\\', L'/') + "/" })
                if (t->StartsWith(d, StringComparison::OrdinalIgnoreCase) && t->Length > d->Length) return lead + t->Substring(d->Length);
            return line;
        }

        // the reader for the next tool: its diagnostics are filed under `source` when they name no file
        void BeginTool(String^ source, String^ toolName)
        {
            reader = gcnew DiagnosticReader(currentWorkDir, currentPattern, source, toolName);
            toolLines = 0;
            toolHidden = 0;
        }

        // RIDE's own compilers write UTF-8 (their banner's first character shows it), so their lines are read
        // as UTF-8 unless the toolchain names a code page; the rest write the console's
        static ToolRunOptions^ ToolOptions(ToolRunOptions^ options, bool rideTool)
        {
            if (!rideTool || options == nullptr || options->OutputCodePage != 0) return options;
            ToolRunOptions^ o = gcnew ToolRunOptions();
            o->SetupScript = options->SetupScript;
            o->Environment = options->Environment;
            o->OutputCodePage = 65001;
            return o;
        }

        // runs one tool through the reader; a tool that could not be started gets RIDE's hint
        int RunTool(String^ command, String^ workDir, ToolRunOptions^ options, String^ source, String^ toolName, String^ targetName, bool rideTool)
        {
            BeginTool(source, toolName);
            ToolRunOptions^ o = ToolOptions(options, rideTool);
            ShowCommand(command, workDir, o);
            System::Diagnostics::Stopwatch^ took = System::Diagnostics::Stopwatch::StartNew();
            int rc = runner->Run(command, workDir, o);
            LogExit(rc, took->Elapsed);
            EndTool(rc, source, toolName, targetName);
            return rc;
        }

        void EndTool(int rc, String^ source, String^ toolName, String^ targetName)
        {
            if (toolHidden > 0) Say(String::Format("[{0}: {1} more line(s) not shown]", toolName, toolHidden));
            // a tool that failed: the command it was given, beneath what it said, unless the pane showed it before
            // it ran (Tools > Options); the log has it either way
            if (rc != 0 && rc != ToolRunner::ExitCancelled && !settings->Env->ShowCommandLines && !String::IsNullOrEmpty(toolCommand))
            {
                Output(String::Format("[{0} returned {1} - its command line:]", toolName, rc));
                Output("> " + toolCommand);
            }
            if (rc == ToolRunner::ExitStartFailed)
                Report(Severity::Error, source, toolName + " could not be run - name it under Tools > Options > Toolchains > " + targetName + ", or put it on PATH");
        }

        // one tool run of a compile step: decided before any tool runs, run on a worker, read back in project order
        ref class ToolPass
        {
        public:
            ToolPass() { Before = gcnew List<String^>(); Lines = gcnew List<String^>(); Code = 0; Ran = false; }
            List<String^>^ Before;      // said before it: the part's "$ group (compiler)" line, "[cpp11]  a.cpp  ->  obj\a.s"
            String^ Command;
            ToolRunOptions^ Options;
            String^ Source;             // the file its diagnostics are filed under when they name none
            String^ ToolName;
            String^ TargetName;         // the toolchain the hint names when it cannot be started
            String^ Echo;               // the file a compiler writes: its echo of the path is not shown
            String^ Writes;             // the file it writes, removed when it fails: half an object would look up to date
            String^ Step;               // compile / assemble, the failedStep when it fails
            String^ Refused;            // said after it fails (asm6x refusing assembly on the emulated target), or null
            List<String^>^ Lines;       // what it wrote, in order
            int Code;
            bool Ran;
            TimeSpan Took;
        };

        // a source's compile and the assembly of what it wrote, one after the other on a worker
        ref class CompileStep
        {
        public:
            CompileStep() { Passes = gcnew List<ToolPass^>(); Size = 0; Started = false; Done = false; }
            List<ToolPass^>^ Passes;
            Int64 Size;                 // its sources' bytes: the largest start first, so the longest compile does not start last
            bool Started;
            volatile bool Done;
        };

        ref class StepWorker
        {
        public:
            StepWorker(BuildEngine^ engine, CompileStep^ step, String^ workDir) { this->engine = engine; this->step = step; this->workDir = workDir; into = nullptr; }

            void Run()
            {
                ToolRunner^ r = gcnew ToolRunner();
                r->Line += gcnew TextLineHandler(this, &StepWorker::OnLine);
                engine->Enlist(r);
                try
                {
                    for each (ToolPass^ pass in step->Passes)
                    {
                        into = pass->Lines;
                        System::Diagnostics::Stopwatch^ took = System::Diagnostics::Stopwatch::StartNew();
                        pass->Code = r->Run(pass->Command, workDir, pass->Options);
                        pass->Took = took->Elapsed;
                        pass->Ran = true;
                        if (pass->Code != 0) { RemoveFile(pass->Writes); break; }
                    }
                }
                catch (Exception^ ex)
                {
                    for each (ToolPass^ pass in step->Passes)
                        if (!pass->Ran) { pass->Lines->Add("error: " + ex->Message); pass->Code = ToolRunner::ExitStartFailed; pass->Ran = true; break; }
                }
                finally
                {
                    engine->Dismiss(r);
                    step->Done = true;
                    engine->stepDone->Set();
                }
            }

        private:
            BuildEngine^ engine;
            CompileStep^ step;
            String^ workDir;
            List<String^>^ into;

            // a ToolRunner hands over its two streams one line at a time
            void OnLine(String^ line) { into->Add(line); }
        };

        void Enlist(ToolRunner^ r)
        {
            System::Threading::Monitor::Enter(workers);
            try { workers->Add(r); if (runner->Cancelled) r->Cancel(); }
            finally { System::Threading::Monitor::Exit(workers); }
        }

        void Dismiss(ToolRunner^ r)
        {
            System::Threading::Monitor::Enter(workers);
            try { workers->Remove(r); }
            finally { System::Threading::Monitor::Exit(workers); }
        }

        void StopWorkers()
        {
            System::Threading::Monitor::Enter(workers);
            try { for each (ToolRunner^ r in workers) r->Cancel(); }
            finally { System::Threading::Monitor::Exit(workers); }
        }

        static bool Empty(CompileStep^ s) { return s->Passes->Count == 0; }

        static void RemoveFile(String^ path)
        {
            try { if (!String::IsNullOrEmpty(path) && File::Exists(path)) File::Delete(path); } catch (Exception^) { }
        }

        // how many compile steps run at once (Tools > Options > Environment > General): 0 is one per processor
        int Jobs()
        {
            int n = settings->Env->BuildJobs;
            return n > 0 ? n : Math::Max(1, Environment::ProcessorCount);
        }

        // The compile steps on workers, Jobs() at a time, the largest first, each read back in project order once it
        // and those before it are done - so the Output pane, the Error List, the first error and the native question
        // are what one tool after another gave. The first step to fail stops the others (those running are stopped,
        // the rest never start), as the build stopped at it before. -1 when one failed or the build was cancelled, else 0.
        int RunSteps(List<CompileStep^>^ steps, String^ workDir)
        {
            int jobs = Jobs();
            List<CompileStep^>^ order = gcnew List<CompileStep^>(steps);
            order->Sort(gcnew Comparison<CompileStep^>(&BuildEngine::LargerFirst));
            int started = 0, read = 0;
            bool failed = false;
            for (;;)
            {
                while (!failed && read < steps->Count && steps[read]->Done)
                    if (ReadBack(steps[read++], workDir) != 0) { failed = true; StopWorkers(); }
                int running = 0;
                for each (CompileStep^ s in steps) if (s->Started && !s->Done) running++;
                if (failed || runner->Cancelled)
                {
                    if (running == 0) return -1;
                }
                else
                {
                    if (read == steps->Count) return 0;
                    while (running < jobs && started < order->Count)
                    {
                        CompileStep^ next = order[started++];
                        next->Started = true;
                        System::Threading::Thread^ t = gcnew System::Threading::Thread(
                            gcnew System::Threading::ThreadStart(gcnew StepWorker(this, next, workDir), &StepWorker::Run));
                        t->IsBackground = true;
                        t->Name = "GRIDE compile";
                        t->Start();
                        running++;
                    }
                }
                stepDone->WaitOne(100);
            }
        }

        static int LargerFirst(CompileStep^ a, CompileStep^ b) { return b->Size.CompareTo(a->Size); }

        // a finished step read as RunTool reads a tool while it runs; 0, or -1 with failedStep where it failed
        int ReadBack(CompileStep^ step, String^ workDir)
        {
            for each (ToolPass^ pass in step->Passes)
            {
                for each (String^ s in pass->Before) Say(s);
                if (!pass->Ran) return -1;
                BeginTool(pass->Source, pass->ToolName);
                ShowCommand(pass->Command, workDir, pass->Options);
                writes = pass->Echo;
                for each (String^ line in pass->Lines) OnToolLine(line);
                writes = nullptr;
                LogExit(pass->Code, pass->Took);
                EndTool(pass->Code, pass->Source, pass->ToolName, pass->TargetName);
                if (pass->Code == 0) continue;
                failedStep = pass->Step;
                if (pass->Refused != nullptr) Say(pass->Refused);
                return -1;
            }
            return 0;
        }

        void OnToolLine(String^ line)
        {
            Keep(line);
            DiagnosticReader^ r = reader;
            BuildMessage^ m = r != nullptr ? r->Feed(line) : BuildMessageParser::TryParse(line, currentWorkDir, currentPattern);
            // a tool failing on every line (an assembler handed a file it cannot read) would bury the build:
            // its first MaxToolLines lines are shown and listed, the rest counted
            if (++toolLines > MaxToolLines)
            {
                toolHidden++;
                if (m != nullptr) summary->Count(m);
                return;
            }
            // the compilers' banner and their echo of the file written say nothing the step's line has not
            String^ t = line == nullptr ? String::Empty : line->Trim();
            bool quiet = m == nullptr && (t->StartsWith(L"\u00A9") || (writes != nullptr && String::Compare(t, writes, true) == 0));
            if (!quiet) Output(Shown(line));
            if (m == nullptr) return;
            if (currentProject != nullptr) m->Project = currentProject->Name;
            if (m->Sev == Severity::Error && m->Line > 0 && !String::IsNullOrEmpty(m->File)) placedError = true;
            summary->Count(m);
            Message(m);
        }

        void Report(Severity sev, String^ file, String^ text)
        {
            BuildMessage^ m = gcnew BuildMessage();
            m->Sev = sev; m->File = file == nullptr ? String::Empty : file; m->Text = text;
            m->Project = currentProject != nullptr ? currentProject->Name : String::Empty;
            summary->Count(sev);
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
                        projectLines->Clear();
                        failedStep = String::Empty;
                        placedError = false;
                        codeViewWarned = false;
                        clSettingsSaid = false;
                        rideHeadersSaid = false;
                        crtSaid = false;
                        int result = BuildProject(p, r, false);
                        if (result == -1 && !runner->Cancelled) result = NativeFallback(p, r);
                        EndLog(result);
                        if (result > 0) summary->Succeeded++;
                        else if (result == 0) summary->UpToDate++;
                        else if (result == -2) summary->Skipped++;
                        else summary->Failed++;
                    }
            }
            catch (Exception^ ex)
            {
                Report(Severity::Error, nullptr, "internal build error: " + ex->Message);
                EndLog(-1);
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

        // ---- the native-tools question (RIDE's nativeFallbackWanted) ------------------------------
        // A failed build the vendor's tools might make: one of the project's own assembler/linker
        // failed, the compilers found no fault in the source, nothing in the output names an
        // unresolved or duplicate symbol, and the settings say to ask. Yes builds again with the
        // target's nativeAssembler / nativeLinker; no, or nobody to ask, and the build stands.
        int NativeFallback(GrideProject^ p, BuildRequest^ r)
        {
            if (!settings->Env->AskNativeTools) return -1;
            if (failedStep != "assemble" && failedStep != "link") return -1;
            if (placedError) return -1;
            for each (String^ fault in gcnew array<String^>{ "unresolved external", "undefined symbol", "no symbol index", "LNK2001", "LNK2019", "LNK1120",
                                                             "LNK2005", "already defined", "multiply defined", "redefined" })
                if (OutputHas(fault)) return -1;
            Target t = r->TargetKind;
            ToolchainDefinitions^ defs = ToolchainDefinitions::Current;
            ToolchainSettings^ tc = settings->Toolchain(t);
            bool emulated = RideToolchain::IsEmulated(t);
            ToolDefinition^ nas = emulated ? nullptr : defs->ToolFor(t, "nativeAssembler");
            ToolDefinition^ nld = defs->ToolFor(t, "nativeLinker");
            bool as = nas != nullptr && !String::IsNullOrWhiteSpace(tc->Assembler);
            bool ld = nld != nullptr && !String::IsNullOrWhiteSpace(tc->Linker);
            if (!as && !ld) return -1;
            String^ ours = as && ld ? RoleName(t, "assembler", tc->Assembler) + " and " + RoleName(t, "linker", tc->Linker)
                                    : as ? RoleName(t, "assembler", tc->Assembler) : RoleName(t, "linker", tc->Linker);
            String^ theirs = emulated ? nld->Name : as && ld ? "Visual Studio's " + nas->Name + " and " + nld->Name : as ? "Visual Studio's " + nas->Name : "Microsoft's " + nld->Name;
            String^ nasPath = as ? tc->ResolveFor("nativeAssembler", nas->Exe) : nullptr;
            String^ nldPath = ld ? tc->ResolveFor("nativeLinker", nld->Exe) : nullptr;
            bool available = (!as || (nasPath != nullptr && File::Exists(nasPath))) && (!ld || (nldPath != nullptr && File::Exists(nldPath)));
            if (!available)
            {
                Say("The project's own " + ours + " did not build it, and " + theirs +
                    (emulated ? " is not on this machine - Tools names TI's C6000 compiler directory" : " are not on this machine - no Visual Studio was found"));
                return -1;
            }
            String^ question = "The project's own " + ours + " did not build it. Use " + theirs + " for this build instead?";
            if (Asker == nullptr)
            {
                Say(question + " (nothing here can ask - the build stands)");
                return -1;
            }
            if (!Asker(question)) return -1;
            Say("building again with " + theirs + " in place of the project's " + ours + ", as asked");
            failedStep = String::Empty;
            int again = BuildProject(p, r, true);
            Say(again > 0 ? "[built with " + theirs + ", not GRIDE's " + ours + "]" : "[not built: " + theirs + " failed as well]");
            return again;
        }

        // the printed name of a tool the settings name (masm for masm.exe)
        static String^ ToolName(String^ tool)
        {
            ToolDefinition^ d = ToolchainDefinitions::Current->ToolByName(tool);
            return d != nullptr ? d->Name : Path::GetFileNameWithoutExtension(tool);
        }

        // the printed name of the tool a role names, when the setting still names it (link.exe is
        // "link" as RIDE's linker and "link.exe" as the Debug linker, Microsoft's)
        static String^ RoleName(Target t, String^ role, String^ tool)
        {
            ToolDefinition^ d = ToolchainDefinitions::Current->ToolFor(t, role);
            if (d != nullptr && d->HasName(tool)) return d->Name;
            return ToolName(tool);
        }

        // the definition of the program a compile runs: the chosen compiler's when the program is its own,
        // the tool's the definitions know the program as (cl.exe named in the C++ slot is cl), else null
        static ToolDefinition^ ProgramDefinition(RideTool tool, String^ program)
        {
            ToolDefinition^ own = RideToolchain::Definition(tool);
            if (own != nullptr && own->HasName(program)) return own;
            return String::IsNullOrEmpty(program) ? nullptr : ToolchainDefinitions::Current->ToolByName(program);
        }

        // which compilers the build's sources go to: cl, or anything else (another compiler, an assembly file)
        void PartsOf(GrideProject^ p, List<String^>^ sources, bool% cl, bool% other)
        {
            cl = false; other = false;
            for each (String^ src in sources)
            {
                Language lang = settings->DetectLanguage(src, p->Lang);
                if (lang != Language::Assembly && RideToolchain::Resolve(RideToolchain::FromWord(p->ToolchainFor(p->FolderOfFile(src))), lang) == RideTool::Cl) cl = true;
                else other = true;
            }
        }

        // a Visual Studio project's own cl settings (its ClCompile values) as the switches the tool's
        // projectSettings name, in the table's order, then the file's additional options; a setting the
        // table does not name goes to `unmapped`. Empty for any other project.
        static String^ ProjectToolFlags(GrideProject^ p, BuildConfiguration^ cfg, ToolDefinition^ d, List<String^>^ unmapped)
        {
            if (p == nullptr || !p->IsVs || p->Vs == nullptr || p->Vs->Reading == nullptr || d == nullptr) return String::Empty;
            String^ name;
            if (!p->Vs->MappedConfigs->TryGetValue(cfg->Config, name)) return String::Empty;
            VsProjectConfig^ c = p->Vs->Reading->Config(name);
            if (c == nullptr) return String::Empty;
            List<String^>^ flags = gcnew List<String^>();
            for each (String^ setting in d->ProjectSettingOrder)
            {
                String^ value;
                if (!c->ClSettings->TryGetValue(setting, value)) continue;
                String^ f = d->ProjectSettingFlag(setting, value);
                if (!String::IsNullOrWhiteSpace(f)) flags->Add(f->Trim());
            }
            if (unmapped != nullptr)
                for each (KeyValuePair<String^, String^> kv in c->ClSettings)
                    if (d->ProjectSettingFlag(kv.Key, kv.Value) == nullptr) unmapped->Add(kv.Key + " " + kv.Value);
            if (c->AdditionalOptions->Length > 0) flags->Add(c->AdditionalOptions);
            return String::Join(" ", flags);
        }

        // ---- one step's tool: the path, the template and the name, chosen by RIDE's rules -----------
        ref class Step
        {
        public:
            String^ Path; String^ Cmd; String^ Name; String^ Macro; String^ Role;
        };

        // the assembler of a step: the vendor's when asked, the CodeView path for a Debug build on a
        // target carrying CodeView (clang; the target's own when clang is not there, with a warning), else the target's
        Step^ AssemblerFor(ToolchainSettings^ tc, Target t, bool debug, bool native, RideTool tool)
        {
            ToolchainDefinitions^ defs = ToolchainDefinitions::Current;
            Step^ s = gcnew Step();
            if (native && !RideToolchain::IsEmulated(t))
            {
                ToolDefinition^ d = defs->ToolFor(t, "nativeAssembler");
                if (d != nullptr)
                {
                    s->Path = tc->ResolveFor("nativeAssembler", d->Exe); s->Name = d->Name; s->Macro = "NativeAssembler"; s->Role = "nativeAssembler";
                    s->Cmd = "\"$(NativeAssembler)\" " + d->OperationArgs("assemble");
                    return s;
                }
            }
            if (!codeViewMissing && RideToolchain::DebugsWithCodeView(tool, t, debug) && !RideToolchain::OwnCodeView(tool, t, debug) &&
                !String::IsNullOrWhiteSpace(tc->DebugAssembler) && !String::IsNullOrWhiteSpace(tc->DebugAssembleCmd))
            {
                String^ path = tc->ResolveFor("debugAssembler", tc->DebugAssembler);
                if (File::Exists(path))
                {
                    s->Path = path; s->Name = RoleName(t, "debugAssembler", tc->DebugAssembler); s->Macro = "DebugAssembler"; s->Role = "debugAssembler"; s->Cmd = tc->DebugAssembleCmd;
                    return s;
                }
                if (!codeViewWarned)
                {
                    codeViewWarned = true;
                    Report(Severity::Warning, nullptr, RoleName(t, "debugAssembler", tc->DebugAssembler) + " not found: " + path + " - a Debug build's CodeView goes through it; assembled with " +
                           RoleName(t, "assembler", tc->Assembler) + " instead, so the debugger will see no lines (looked in " + tc->LookedIn(tc->DebugAssembler) + ")");
                }
            }
            s->Path = tc->ResolveFor("assembler", tc->Assembler); s->Name = RoleName(t, "assembler", tc->Assembler); s->Macro = "Assembler"; s->Role = "assembler"; s->Cmd = tc->AssembleCmd;
            return s;
        }

        // the linker of the build: the vendor's when asked; Microsoft's link.exe for cl's objects (the Debug
        // linker's, else the vendor's - cl's objects want its CRT); link.exe /DEBUG for a CodeView Debug build; else the target's
        Step^ LinkerFor(ToolchainSettings^ tc, Target t, bool debug, bool native, bool codeView, bool microsoft)
        {
            ToolchainDefinitions^ defs = ToolchainDefinitions::Current;
            Step^ s = gcnew Step();
            if (native)
            {
                ToolDefinition^ d = defs->ToolFor(t, "nativeLinker");
                if (d != nullptr)
                {
                    s->Path = tc->ResolveFor("nativeLinker", d->Exe); s->Name = d->Name; s->Macro = "NativeLinker"; s->Role = "nativeLinker";
                    s->Cmd = "\"$(NativeLinker)\" " + d->OperationArgs("link");
                    return s;
                }
            }
            TargetDefinition^ td = defs->TargetOf(t);
            if (microsoft && td != nullptr && td->Host)
            {
                if (!String::IsNullOrWhiteSpace(tc->DebugLinker) && !String::IsNullOrWhiteSpace(tc->DebugLinkCmd))
                {
                    String^ path = tc->ResolveFor("debugLinker", tc->DebugLinker);
                    if (File::Exists(path))
                    {
                        s->Path = path; s->Name = RoleName(t, "debugLinker", tc->DebugLinker); s->Macro = "DebugLinker"; s->Role = "debugLinker"; s->Cmd = tc->DebugLinkCmd;
                        return s;
                    }
                }
                ToolDefinition^ d = defs->ToolFor(t, "nativeLinker");
                String^ theirs = d == nullptr ? nullptr : tc->ResolveFor("nativeLinker", d->Exe);
                if (theirs != nullptr && File::Exists(theirs))
                {
                    s->Path = theirs; s->Name = d->Name; s->Macro = "NativeLinker"; s->Role = "nativeLinker";
                    s->Cmd = "\"$(NativeLinker)\" " + d->OperationArgs("link");
                    return s;
                }
                Report(Severity::Warning, nullptr, "Microsoft's link.exe not found - cl's objects are linked with it; linked with " + RoleName(t, "linker", tc->Linker) +
                       " instead (looked in " + tc->LookedIn(String::IsNullOrWhiteSpace(tc->DebugLinker) ? (d == nullptr ? String::Empty : d->Exe) : tc->DebugLinker) + ")");
            }
            if (codeView && !String::IsNullOrWhiteSpace(tc->DebugLinker) && !String::IsNullOrWhiteSpace(tc->DebugLinkCmd))
            {
                String^ path = tc->ResolveFor("debugLinker", tc->DebugLinker);
                if (File::Exists(path))
                {
                    s->Path = path; s->Name = RoleName(t, "debugLinker", tc->DebugLinker); s->Macro = "DebugLinker"; s->Role = "debugLinker"; s->Cmd = tc->DebugLinkCmd;
                    return s;
                }
                Report(Severity::Warning, nullptr, "Microsoft's " + RoleName(t, "debugLinker", tc->DebugLinker) + " not found: " + path + " - a Debug build's PDB needs it; linked with " +
                       RoleName(t, "linker", tc->Linker) + " instead, so the debugger will see no lines (looked in " + tc->LookedIn(tc->DebugLinker) + ")");
            }
            s->Path = tc->ResolveFor("linker", tc->Linker); s->Name = RoleName(t, "linker", tc->Linker); s->Macro = "Linker"; s->Role = "linker"; s->Cmd = tc->LinkCmd;
            return s;
        }

        // ---- per project -------------------------------------------------------------
        // returns 1 = built, 0 = up to date, -1 = failed, -2 = skipped
        int BuildProject(GrideProject^ p, BuildRequest^ r, bool native)
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
            p->Names = NamesOf(p, cfg, tc, r);
            if (r->Action != BuildAction::Clean && !NamesKnown(p, cfg, tc)) return -1;
            String^ objDir = p->ToAbsolute(cfg->ObjDir);
            String^ outDir = p->ToAbsolute(cfg->OutDir);
            bool debug = cfg->IsDebug;
            bool emulated = RideToolchain::IsEmulated(t);
            currentWorkDir = p->ProjectDir;
            currentPattern = BuildMessageParser::Compile(tc->ErrorPattern);
            if (currentPattern == nullptr && !String::IsNullOrWhiteSpace(tc->ErrorPattern))
                Report(Severity::Warning, nullptr, "the error pattern of toolchain " + tc->Name + " is not a valid regular expression and is ignored");
            ToolRunOptions^ run = RunOptions(tc, t);
            bool viaSetup = !String::IsNullOrWhiteSpace(tc->SetupScript);   // tools may then come from the script's PATH

            // a Debug build's line table goes through clang (Visual Studio's C++ Clang tools); without it the
            // compilers write MASM and masm and the linker make the program - said once, before anything runs,
            // so masm is never handed clang's assembly
            codeViewMissing = false;
            ownCodeView = false;
            if (debug && !native && !emulated && !viaSetup && r->Action != BuildAction::Clean && RideToolchain::DebugsWithCodeView(RideTool::C90, t, true) &&
                NeedsClang(p, cfg, t))
            {
                String^ clang = String::IsNullOrWhiteSpace(tc->DebugAssembler) ? String::Empty : tc->ResolveFor("debugAssembler", tc->DebugAssembler);
                if (clang->Length == 0 || !File::Exists(clang) || String::IsNullOrWhiteSpace(tc->DebugAssembleCmd))
                {
                    codeViewMissing = true;
                    codeViewWarned = true;
                    Report(Severity::Warning, nullptr, RoleName(t, "debugAssembler", tc->DebugAssembler) + " not found - this Debug build has no source line table: " +
                           RoleName(t, "assembler", tc->Assembler) + " and " + RoleName(t, "linker", tc->Linker) + " make the program, the debugger shows no lines " +
                           "(Visual Studio's \"C++ Clang tools for Windows\" add it; looked in " + tc->LookedIn(tc->DebugAssembler) + ")");
                }
            }

            if (r->Action == BuildAction::Clean || r->Action == BuildAction::Rebuild)
            {
                // OutDir "." or "..\" would otherwise empty the project (or a parent) folder
                if (!CanClean(p, objDir, "object") || !CanClean(p, outDir, "output")) return -1;
                CleanDir(objDir);
                if (!PathUtil::SamePath(objDir, outDir)) CleanDir(outDir);
                if (r->Action == BuildAction::Clean) return 1;
            }

            // RIDE refuses a build whose program could not be written where it would go
            for each (String^ dir in gcnew array<String^>{ outDir, objDir })
            {
                String^ why = Unwritable(dir);
                if (why != nullptr) { Report(Severity::Error, nullptr, why); return -1; }
            }
            PrepareLog(p, objDir);

            MacroExpander^ mx = BaseMacros(p, cfg, tc, r);
            List<String^>^ objects = gcnew List<String^>();
            bool anyWork = false;
            DateTime newestHeader = NewestHeader(p);

            List<String^>^ sources = BuildableFiles(p, cfg->Config);
            List<String^>^ named = sources;      // the file set object names are chosen from
            if (r->Action == BuildAction::CompileFile)
            {
                sources = gcnew List<String^>();
                if (r->SingleFile != nullptr)
                {
                    String^ single = Path::GetFullPath(r->SingleFile);
                    sources->Add(single);
                    if (!named->Contains(single)) named->Add(single);
                }
            }
            Dictionary<String^, String^>^ stems = StemMap(p, named);
            // a Shalimar program is one compilation of every .shl (Compile File takes the one file alone)
            List<String^>^ unit = r->Action == BuildAction::CompileFile ? gcnew List<String^>() : ShalimarUnit(p, sources);
            bool unitDone = false;
            bool shalimarProgram = false;
            bool crossTarget = false;
            RideTool crossTool = RideTool::C90;
            bool codeView = false;
            String^ lastFolder = nullptr;

            // ---- compile + assemble each source: every step decided here, in project order, before any tool
            // runs; then RunSteps runs them Jobs() at a time ----
            if (r->Action != BuildAction::LinkOnly)
            {
                List<CompileStep^>^ steps = gcnew List<CompileStep^>();
                for each (String^ src in sources)
                {
                    if (runner->Cancelled) return -1;
                    Language lang = settings->DetectLanguage(src, p->Lang);
                    String^ name = stems[src];
                    List<String^>^ group = nullptr;
                    if (lang == Language::Shalimar && unit->Count > 1)
                    {
                        if (unitDone) continue;
                        unitDone = true;
                        group = unit;
                        name = ProgramStem(p, cfg);
                    }
                    String^ asmFile = lang == Language::Assembly ? src : Path::Combine(objDir, name + tc->AsmExt);
                    String^ objFile = Path::Combine(objDir, name + tc->ObjExt);
                    objects->Add(objFile);

                    // which compiler takes the file: the folder's word, else the project's, else the language (RIDE's resolve)
                    RideTool tool = RideToolchain::Resolve(RideToolchain::FromWord(p->ToolchainFor(p->FolderOfFile(src))), lang);
                    if (lang != Language::Assembly && !RideToolchain::CanCompile(tool, lang))
                    {
                        Report(Severity::Error, src, RideToolchain::Refusal(tool, lang));
                        return -1;
                    }
                    if (lang == Language::Shalimar) shalimarProgram = true;
                    // a cross host target only reaches assembly on this machine (whyNotRun)
                    bool crossOnly = lang != Language::Assembly && !RideToolchain::RunsHere(tool, t);
                    if (crossOnly) { crossTarget = true; crossTool = tool; }

                    DateTime newestSource = File::GetLastWriteTime(src);
                    if (group != nullptr) for each (String^ g in group) if (File::GetLastWriteTime(g) > newestSource) newestSource = File::GetLastWriteTime(g);
                    String^ made = crossOnly ? asmFile : objFile;
                    bool upToDate = r->Action == BuildAction::Build && File::Exists(made) &&
                                    File::GetLastWriteTime(made) >= newestSource &&
                                    File::GetLastWriteTime(made) >= newestHeader;
                    if (upToDate) continue;
                    anyWork = true;

                    mx->Set("Source", src);
                    mx->Set("Sources", group != nullptr ? MacroExpander::JoinQuoted(group) : MacroExpander::JoinQuoted(gcnew array<String^>{ src }));
                    mx->Set("SourceDir", Path::GetDirectoryName(src));
                    mx->Set("Name", name);
                    mx->Set("AsmFile", asmFile);
                    mx->Set("ObjFile", objFile);
                    SetCompilerMacros(mx, tool, lang, p, cfg, tc);
                    CompileStep^ step = gcnew CompileStep();
                    for each (String^ f in group != nullptr ? group : gcnew List<String^>(gcnew array<String^>{ src }))
                        if (File::Exists(f)) step->Size += (gcnew FileInfo(f))->Length;
                    steps->Add(step);

                    if (lang != Language::Assembly && r->Action != BuildAction::AssembleOnly)
                    {
                        ToolPass^ compile = gcnew ToolPass();
                        // RIDE's "$ group (compiler)" line as each part starts
                        String^ folder = p->FolderOfFile(src);
                        String^ heading = (String::IsNullOrEmpty(folder) ? p->Name : folder) + " (" + RideToolchain::Name(tool) + ")";
                        if (heading != lastFolder) { compile->Before->Add("$ " + heading); lastFolder = heading; }

                        String^ compiler = tc->ResolveFor(RideToolchain::RoleOf(tool), tc->CompilerFor(tool));
                        if (!File::Exists(compiler) && !viaSetup)
                        {
                            Report(Severity::Error, src, RideToolchain::Name(tool) + " not found: " + compiler +
                                   "  (looked in " + tc->LookedIn(tc->CompilerFor(tool)) + "; set it in Tools > Options > Toolchains > " + tc->Name + ")");
                            return -1;
                        }
                        mx->Set("Compiler", compiler);
                        String^ templ = tc->CompileCmd;
                        if (tool == RideTool::Cl || tool == RideTool::HostCxx) { if (!String::IsNullOrWhiteSpace(tc->CompileCmdCl)) templ = tc->CompileCmdCl; }
                        else if (tool == RideTool::Shalimar && !String::IsNullOrWhiteSpace(tc->CompileCmdShalimar)) templ = tc->CompileCmdShalimar;
                        // the line names the program that runs and the file it writes: a template naming $(ObjFile)
                        // has the compiler write the object itself (cl, or cl.exe named in the C++ slot), else the assembly
                        bool writesObject = templ->Contains("$(ObjFile)");
                        ToolDefinition^ ran = ProgramDefinition(tool, compiler);
                        String^ ranName = ran != nullptr ? ran->Name : Path::GetFileNameWithoutExtension(compiler);
                        String^ shownSources = group != nullptr ? String::Join(" ", Leaves(group)) : Path::GetFileName(src);
                        compile->Before->Add(String::Format("[{0}]  {1}  ->  {2}", ranName, shownSources, PathUtil::MakeRelative(p->ProjectDir, writesObject ? objFile : asmFile)));
                        bool own = !writesObject && RideToolchain::OwnCodeView(tool, t, debug);
                        bool cv = !writesObject && !own && !codeViewMissing && RideToolchain::DebugsWithCodeView(tool, t, debug);
                        if (own) ownCodeView = true;
                        List<String^>^ optionEnv = RideToolchain::OptionEnvironment(p->Options, RideToolchain::Definition(tool), cfg->Config);
                        // RIDE's own compilers write UTF-8; a program the definitions know is read as they say
                        bool utf8 = ran != nullptr ? String::Compare(ran->OutputEncoding, "utf-8", true) == 0 : tool != RideTool::Cl && tool != RideTool::HostCxx;
                        compile->Command = mx->Expand(templ);
                        compile->Options = ToolOptions(CompileOptions(run, tc, cv, optionEnv), utf8);
                        compile->Source = src; compile->ToolName = ranName; compile->TargetName = tc->Name;
                        compile->Echo = writesObject ? objFile : asmFile;
                        compile->Writes = compile->Echo;
                        compile->Step = "compile";
                        step->Passes->Add(compile);
                        if (writesObject) continue;      // the compiler writes the object itself: nothing to assemble
                        if (cv) codeView = true;
                    }

                    if (crossOnly) continue;
                    if (emulated && p->EmulateOnly) continue;     // the assembly is the program: vm6747 runs it
                    if (String::IsNullOrEmpty(tc->AssembleCmd)) continue;
                    Step^ as = AssemblerFor(tc, t, debug, native, tool);
                    if (!File::Exists(as->Path) && !viaSetup)
                    {
                        Report(Severity::Error, src, as->Name + " not found: " + as->Path +
                               "  (looked in " + tc->LookedIn(as->Role == "assembler" ? tc->Assembler : as->Path) + "; set it in Tools > Options > Toolchains > " + tc->Name + ")");
                        return -1;
                    }
                    mx->Set(as->Macro, as->Path);
                    ToolPass^ assemble = gcnew ToolPass();
                    assemble->Before->Add(String::Format("[{0}]  {1}  ->  {2}", as->Name, Path::GetFileName(asmFile), PathUtil::MakeRelative(p->ProjectDir, objFile)));
                    assemble->Command = mx->Expand(as->Cmd);
                    assemble->Options = run;
                    assemble->Source = asmFile; assemble->ToolName = as->Name; assemble->TargetName = tc->Name;
                    assemble->Writes = objFile;
                    assemble->Step = "assemble";
                    if (emulated) assemble->Refused = as->Name + " refused the assembly - the emulator would run it, but it is not a TI program";
                    step->Passes->Add(assemble);
                }
                steps->RemoveAll(gcnew Predicate<CompileStep^>(&BuildEngine::Empty));
                if (RunSteps(steps, p->ProjectDir) != 0) return -1;
            }
            else
            {
                for each (String^ src in sources)
                    objects->Add(Path::Combine(objDir, stems[src] + tc->ObjExt));
                if (unit->Count > 1)
                {
                    for each (String^ s in unit) objects->Remove(Path::Combine(objDir, stems[s] + tc->ObjExt));
                    objects->Add(Path::Combine(objDir, ProgramStem(p, cfg) + tc->ObjExt));
                    shalimarProgram = true;
                }
            }

            if (crossTarget)
            {
                Say(RideToolchain::WhyNotRun(crossTool, t));
                Say("[assembly written to " + PathUtil::MakeRelative(p->ProjectDir, objDir) + " - nothing here assembles or links it]");
                return 1;
            }
            if (r->Action == BuildAction::CompileFile || r->Action == BuildAction::AssembleOnly) return 1;
            if (emulated && p->EmulateOnly)
            {
                Say("[no .out: the project says emulateOnly - Run is Emulate on vm6747]");
                summary->Program = String::Empty;
                return 1;
            }
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
                summary->Program = program;
                return 0;
            }

            // cl's objects (a part the project or a folder gives to cl) are Microsoft's: its link.exe links them
            bool clObjects = false, otherObjects = false;
            PartsOf(p, sources, clObjects, otherObjects);
            bool clangCodeView = codeView || (debug && !emulated && !native && !codeViewMissing && !ownCodeView && NeedsClang(p, cfg, t) &&
                                              RideToolchain::DebugsWithCodeView(RideTool::C90, t, debug));
            Step^ ld = LinkerFor(tc, t, debug, native, clangCodeView, clObjects && !emulated);
            // the PDB of GRIDE's own link: /DEBUG, which it writes beside the program
            if (ownCodeView && !clangCodeView && !emulated && ld->Role == "linker")
            {
                String^ flags;
                if (!mx->Values->TryGetValue("LinkFlags", flags)) flags = String::Empty;
                if (flags->IndexOf("/DEBUG", StringComparison::OrdinalIgnoreCase) < 0) mx->Set("LinkFlags", (flags + " /DEBUG")->Trim());
            }
            // tms6747 with the named linker gone: TI's stands in, and the console says so (RIDE's tiLinker)
            if (emulated && !native && !viaSetup && !File::Exists(ld->Path))
            {
                Step^ theirs = LinkerFor(tc, t, debug, true, false, false);
                if (theirs->Role == "nativeLinker" && File::Exists(theirs->Path))
                {
                    Say("[" + ld->Path + " is not there - linking with TI's " + theirs->Path + "]");
                    ld = theirs;
                }
            }
            if (!File::Exists(ld->Path) && !viaSetup)
            {
                Report(Severity::Error, nullptr, ld->Name + " not found: " + ld->Path + "  (looked in " + tc->LookedIn(ld->Role == "linker" ? tc->Linker : ld->Path) +
                       "; set it in Tools > Options > Toolchains > " + tc->Name + ")");
                return -1;
            }
            mx->Set(ld->Macro, ld->Path);
            // the Windows C runtime: GRIDE's own (its five layered libraries and kernel32.lib, by their paths - no LIB, no Visual
            // Studio) or Microsoft's (libcmt and the UCRT, through LIB)
            String^ crt = String::Empty;
            if (!emulated && RideToolchain::HostCrt(t, "grt") != nullptr)
            {
                bool microsoftLibrary = LinksMicrosoftLibrary(p, cfg);
                crt = WindowsRuntime(clObjects, microsoftLibrary);
                if (crt == "msvc" && microsoftLibrary && !clObjects && settings->Env->EffectiveWindowsRuntime() == "auto")
                    Say("[a library this link takes names Microsoft's C runtime by /DEFAULTLIB - Microsoft's is linked]");
                String^ libs = RideToolchain::CrtLibraries(t, debug, crt, tc->ToolDir, ToolchainSettings::RideRoot);
                String^ where = RideToolchain::CrtDirectory(t, "grt", tc->ToolDir, ToolchainSettings::RideRoot);
                // a Debug build links the Debug variants (grtstartd.lib ...), as a Debug build on Microsoft's links libcmtd
                String^ layers = debug ? "grtstartd, grtcompatd, grtcxxd, grtcd, grtwind" : "grtstart, grtcompat, grtcxx, grtc, grtwin";
                if (libs == nullptr && settings->Env->EffectiveWindowsRuntime() == "auto" && VisualStudio::Find() != nullptr)
                {
                    Say("[GRIDE's C runtime is not in " + where + " - Microsoft's (Visual Studio's) is linked instead]");
                    crt = "msvc";
                    libs = RideToolchain::CrtLibraries(t, debug, crt, tc->ToolDir, ToolchainSettings::RideRoot);
                }
                if (libs == nullptr)
                {
                    Report(Severity::Error, nullptr, "GRIDE's C runtime is not there: " + layers + " and kernel32.lib are looked for in " + where +
                           " beside GRIDE (Setup installs them; Build Solution writes them into x64\\<Configuration>\\lib)");
                    failedStep = "link";
                    return -1;
                }
                // a library cl built names Microsoft's runtime by /DEFAULTLIB (libcmt, oldnames ...): with GRIDE's
                // runtime those names are refused, and what cl's code calls beyond the C library is grtcompat's
                if (crt == "grt") libs += MicrosoftRuntimeRefused;
                mx->Set("CrtLibs", libs);
                if (crt == "grt" && clObjects)
                    Report(Severity::Warning, nullptr, "cl's objects name Microsoft's C runtime, and GRIDE's runtime is chosen (Tools > Options > Projects > GRIDE Paths, Windows C runtime): they may not link");
                if (!crtSaid)
                {
                    crtSaid = true;
                    Say(crt == "grt" ? "[GRIDE's C runtime: " + layers + " and kernel32.lib from " + where + " - no Visual Studio or Windows SDK library]"
                                     : "[Microsoft's C runtime: " + RideToolchain::CrtLibraries(t, debug, crt, tc->ToolDir, ToolchainSettings::RideRoot) + ", found through LIB]");
                }
            }
            // every object cl's: they name their own C runtime (/DEFAULTLIB from /MT, /MTd, /MD ...), so no CRT list is added to theirs
            if (!emulated && clObjects && !otherObjects && ld->Role != "linker")
            {
                mx->Set("CrtLibs", String::Empty);
                Say("[cl's objects name their own C runtime - " + ld->Name + " takes it from them, no CRT list is added]");
            }
            int linked = emulated ? TiLink(p, cfg, tc, mx, run, ld, objects, program, debug, shalimarProgram, native)
                                  : HostLink(p, tc, mx, run, ld, objects, program, shalimarProgram || unit->Count > 0, debug, crt);
            if (linked != 0) { failedStep = "link"; return -1; }
            summary->Program = program;
            return 1;
        }

        // the host link: "$ linking with link.exe", then "[built <program>]"; a Shalimar program takes its
        // runtime library first (RIDE's shalimar links it itself; here the objects are linked), the
        // configuration's one - the Debug runtime is the one with the session in it
        int HostLink(GrideProject^ p, ToolchainSettings^ tc, MacroExpander^ mx, ToolRunOptions^ run, Step^ ld, List<String^>^ objects, String^ program, bool shalimarProgram, bool debug,
                     String^ crt)
        {
            if (shalimarProgram)
            {
                // the Shalimar runtime built for the C runtime in force (lib\x86_64-windows\shmrt.lib over GRIDE's, msvc\shmrt.lib over Microsoft's)
                LibraryDefinition^ lib = ToolchainDefinitions::Current->LibraryByKind(RideToolchain::Arch(tc->TargetKind), "shalimarRuntime", crt);
                String^ shm = RideToolchain::HostShalimarRuntime(tc->TargetKind, debug, tc->ToolDir, ToolchainSettings::RideRoot, crt);
                if (shm != nullptr)
                {
                    String^ libs;
                    if (!mx->Values->TryGetValue("LibFlags", libs)) libs = String::Empty;
                    mx->Set("LibFlags", (RideToolchain::Quote(shm) + " " + libs)->Trim());
                    Say("[" + Path::GetFileName(shm) + " linked first: the Shalimar runtime]");
                }
                else if (lib != nullptr)
                    Say("[" + lib->FileFor(debug) + " is not beside GRIDE (" + lib->Directory + ") - the link may miss the Shalimar runtime]");
            }
            mx->Set("Objects", MacroExpander::JoinQuoted(objects));
            Say("$ linking with " + ld->Name);
            Say(String::Format("[LINK] {0}", PathUtil::MakeRelative(p->ProjectDir, program)));
            if (RunTool(mx->Expand(ld->Cmd), p->ProjectDir, run, nullptr, ld->Name, tc->Name, false) != 0) return -1;
            Say("[built " + program + "]");
            return 0;
        }

        // the C6747 program (RIDE's makeTiProgram): lnk6x against RTS6x - the configuration's library
        // for every alias the project names (and the C runtime alone when it names none), the Shalimar
        // runtime's library before it for a Shalimar program (its assembly, assembled here, where no
        // library is packed), the project's linker command file where it is on disk, else RIDE's own
        // written into the object folder; a search path not on this machine is left out and said so
        int TiLink(GrideProject^ p, BuildConfiguration^ cfg, ToolchainSettings^ tc, MacroExpander^ mx, ToolRunOptions^ run, Step^ ld,
                   List<String^>^ objects, String^ program, bool debug, bool shalimarProgram, bool native)
        {
            String^ objDir = p->ToAbsolute(cfg->ObjDir);
            String^ runtimeDir = tc->ResolveRuntimeDir();
            // RTS6x unless TI's own compiler directory is named under Tools (RIDE 5.1): then, or when
            // RTS6x is not here, TI's runtime from the compiler directory's lib (and the second library
            // directory), the exception-handling build where there is one
            CcsDefinition^ cd = ToolchainDefinitions::Current->Ccs;
            bool tiNamed = RideToolchain::TiNamed();
            bool rts6x = !tiNamed && !String::IsNullOrEmpty(runtimeDir) && Directory::Exists(runtimeDir);
            String^ tiDir = rts6x ? nullptr : RideToolchain::TiDir();
            String^ made = String::Format("[{0} TI objects made] no .out: ", objects->Count);
            if (!rts6x && tiDir == nullptr)
            {
                LibraryDefinition^ rtsLib = ToolchainDefinitions::Current->RuntimeOf(cfg->TargetKind);
                Report(Severity::Error, nullptr, made + "a tms6747 build links against RTS6x in " + (rtsLib == nullptr ? "its directory" : rtsLib->Directory) +
                       " beside GRIDE, or TI's runtime when its compiler directory is named under Tools > Options > Projects > GRIDE Paths, and neither is here");
                return -1;
            }
            String^ tiLib = rts6x ? nullptr : Path::Combine(tiDir, cd->TiLib);
            String^ tiExtra = rts6x || String::IsNullOrWhiteSpace(RideToolchain::TiLibDir) ? nullptr : Environment::ExpandEnvironmentVariables(RideToolchain::TiLibDir->Trim());
            if (tiExtra != nullptr && !Directory::Exists(tiExtra)) tiExtra = nullptr;
            bool eh = rts6x || File::Exists(Path::Combine(tiLib, cd->RuntimeLibrary)) || (tiExtra != nullptr && File::Exists(Path::Combine(tiExtra, cd->RuntimeLibrary)));
            if (!eh && !tiNamed)
            {
                Report(Severity::Error, nullptr, made + tiDir + " has only " + cd->RuntimeFallback + " - these objects need " + cd->RuntimeLibrary +
                       ", named under Tools > Options > Projects > GRIDE Paths (TI library directory)");
                return -1;
            }
            // the C runtime in force, which every name a project has for it (libc.a, rts6740*, rts6x.lib ...) is read as
            List<String^>^ libs = gcnew List<String^>();
            String^ rts = rts6x ? RideToolchain::RtsLibrary(debug) : eh ? cd->RuntimeLibrary : cd->RuntimeFallback;
            bool namedRts = false;
            for each (String^ lib in Filled(p, cfg->Libs))
            {
                if (String::IsNullOrWhiteSpace(lib)) continue;
                String^ l = lib->Trim();
                if (RideToolchain::IsRtsAlias(l) || String::Compare(l, cd->RuntimeIndex, true) == 0 || String::Compare(l, cd->RuntimeLibrary, true) == 0 || String::Compare(l, cd->RuntimeFallback, true) == 0)
                {
                    l = rts; namedRts = true;
                }
                if (!libs->Contains(l)) libs->Add(l);
            }
            if (cfg->Libs->Count == 0 && rts->Length > 0) { libs->Add(rts); namedRts = true; }
            // the .pro's "libraries", for every configuration
            for each (String^ l in p->Libraries) if (!String::IsNullOrWhiteSpace(l) && !libs->Contains(p->ToAbsolute(l->Trim()))) libs->Add(p->ToAbsolute(l->Trim()));
            if (shalimarProgram)
            {
                String^ shm = RideToolchain::ShalimarRtsLibrary(debug);
                bool packed = shm->Length > 0 && !String::IsNullOrEmpty(runtimeDir) && File::Exists(Path::Combine(runtimeDir, shm));
                if (packed) { libs->Remove(shm); libs->Insert(0, shm); }
                else
                {
                    // the runtime's assembly, copied in and assembled here rather than beside the editor
                    String^ theirs = RideToolchain::FindShalimarRuntimeDir(tc->ToolDir, ToolchainSettings::RideRoot, debug);
                    if (!String::IsNullOrEmpty(theirs))
                    {
                        String^ into = Path::Combine(objDir, "shmrt");
                        PathUtil::EnsureDirectory(into);
                        Step^ as = AssemblerFor(tc, cfg->TargetKind, debug, native, RideTool::Shalimar);
                        mx->Set(as->Macro, as->Path);
                        for each (String^ s in Directory::GetFiles(theirs, "*" + tc->AsmExt))
                        {
                            String^ copy = Path::Combine(into, Path::GetFileName(s));
                            String^ obj = Path::ChangeExtension(copy, tc->ObjExt);
                            File::Copy(s, copy, true);
                            mx->Set("AsmFile", copy); mx->Set("ObjFile", obj);
                            if (RunTool(mx->Expand(as->Cmd), p->ProjectDir, run, copy, as->Name, tc->Name, false) != 0)
                            {
                                Say("cannot assemble the Shalimar runtime's " + Path::GetFileName(s));
                                return -1;
                            }
                            objects->Add(obj);
                        }
                    }
                }
            }
            // the linker command file: the project's where it is there, else RIDE's flat memory map
            String^ cmdFile = String::IsNullOrEmpty(cfg->LinkerCmd) ? String::Empty : p->ToAbsolute(cfg->LinkerCmd);
            bool ownCmd = cmdFile->Length > 0 && File::Exists(cmdFile);
            if (!ownCmd)
            {
                String^ text = RideToolchain::TiLinkCmd();
                String^ name = RideToolchain::TiLinkCmdName();
                if (text->Length > 0 && name->Length > 0)
                {
                    cmdFile = Path::Combine(objDir, name);
                    File::WriteAllText(cmdFile, text);
                }
            }
            mx->Set("LinkerCmd", cmdFile);
            List<String^>^ libDirs = gcnew List<String^>();
            if (tiLib != nullptr) mx->Set("RuntimeDir", tiLib);      // the link template's -i "$(RuntimeDir)" is TI's lib then
            if (tiExtra != nullptr) libDirs->Add(tiExtra);
            for each (String^ d in cfg->LibDirs)
            {
                String^ abs = p->ToAbsolute(d);
                if (Directory::Exists(abs)) libDirs->Add(abs);
                else Say("[library search path not on this machine, left out: " + d + "]");
            }
            libDirs->AddRange(Filled(p, tc->LibDirs));
            mx->Set("LibDirFlags", MacroExpander::JoinFlags(tc->LibDirFlag, libDirs));
            // TI's linker takes c_int00 from the library only under --rom_model, the model CCS defaults to
            if (!rts6x && ld->Role == "nativeLinker" && !ownCmd && cfg->ExtraLinkFlags->IndexOf("_model") < 0)
                mx->Set("LinkFlags", (cfg->ExtraLinkFlags + " --rom_model")->Trim());
            mx->Set("LibFlags", MacroExpander::JoinFlags(tc->LibFlag, libs));
            mx->Set("Objects", MacroExpander::JoinQuoted(objects));
            Say("$ " + ld->Name + " " + objects->Count + " objects, " + String::Join(", ", libs) + (ownCmd ? ", " + Path::GetFileName(cmdFile) : String::Empty) +
                " -o " + Path::GetFileName(program));
            if (RunTool(mx->Expand(ld->Cmd), p->ProjectDir, run, nullptr, ld->Name, tc->Name, false) != 0)
            {
                Say(ld->Name + " did not link it - see its messages above");
                return -1;
            }
            Say("[linked " + program + "]");
            return 0;
        }

        static List<String^>^ Leaves(List<String^>^ files)
        {
            List<String^>^ r = gcnew List<String^>();
            for each (String^ f in files) r->Add(Path::GetFileName(f));
            return r;
        }

        // the options every tool of the target runs with: the setup script, the user's variables,
        // and on the host target Visual Studio's directories (INCLUDE, LIB, PATH) found without cmd.exe
        static ToolRunOptions^ RunOptions(ToolchainSettings^ tc, Target t)
        {
            ToolRunOptions^ o = gcnew ToolRunOptions();
            o->SetupScript = tc->SetupScript == nullptr ? String::Empty : tc->SetupScript;
            if (tc->EnvironmentVars != nullptr) o->Environment = gcnew List<String^>(tc->EnvironmentVars);
            TargetDefinition^ td = ToolchainDefinitions::Current->TargetOf(t);
            if (td != nullptr && td->Host && String::IsNullOrWhiteSpace(o->SetupScript)) o->Environment->AddRange(VisualStudio::EnvironmentLines());
            o->OutputCodePage = tc->OutputCodePage;
            return o;
        }

        // the environment RIDE sets before a compiler runs (prepareFor): each compiler's *_AS and *_LD
        // name the project's assembler and linker, empty for a Debug build carrying CodeView (clang and
        // link.exe take that build); the variables' names are the tools' own (toolchain.json environment)
        static ToolRunOptions^ CompileOptions(ToolRunOptions^ base, ToolchainSettings^ tc, bool codeView, List<String^>^ optionEnvironment)
        {
            ToolRunOptions^ o = gcnew ToolRunOptions();
            o->SetupScript = base->SetupScript;
            o->Environment = gcnew List<String^>(base->Environment);
            if (optionEnvironment != nullptr) o->Environment->AddRange(optionEnvironment);   // the Compiler Options' variables (CPP11_DECLINES)
            o->OutputCodePage = base->OutputCodePage;
            String^ as = codeView || String::IsNullOrWhiteSpace(tc->Assembler) ? String::Empty : tc->ResolveFor("assembler", tc->Assembler);
            String^ ld = codeView || String::IsNullOrWhiteSpace(tc->Linker) ? String::Empty : tc->ResolveFor("linker", tc->Linker);
            for each (ToolDefinition^ d in ToolchainDefinitions::Current->Tools)
            {
                if (d->Role != "cCompiler" && d->Role != "cppCompiler" && d->Role != "shalimarCompiler") continue;
                for each (KeyValuePair<String^, String^> kv in d->Environment)
                {
                    if (kv.Key->EndsWith("_AS")) o->Environment->Add(kv.Key + "=" + as);
                    else if (kv.Key->EndsWith("_LD")) o->Environment->Add(kv.Key + "=" + ld);
                }
            }
            return o;
        }

        // the macros that depend on the compiler a file goes to: the target flag as that compiler
        // spells it, the configuration flags from the project's Compiler Options, -masm, cl's language
        // switches, and the include and define flags in the compiler's own spelling - the project's
        // directories, the target's, the shared ones of the settings, then the installation's headers
        // the compiler reads (RIDE's includeFlags)
        void SetCompilerMacros(MacroExpander^ mx, RideTool tool, Language lang, GrideProject^ p, BuildConfiguration^ cfg, ToolchainSettings^ tc)
        {
            bool debug = cfg->IsDebug;
            // no clang: the Release spelling (-masm=masm) and no -g, which only clang's assembly carries
            bool noLineTable = debug && codeViewMissing && RideToolchain::DebugsWithCodeView(tool, cfg->TargetKind, true) &&
                               !RideToolchain::OwnCodeView(tool, cfg->TargetKind, true);
            String^ flags = RideToolchain::ConfigFlags(tool, debug, cfg->TargetKind, p->Options, p->Names);
            if (noLineTable)
            {
                List<String^>^ kept = gcnew List<String^>();
                for each (String^ f in flags->Split(gcnew array<wchar_t>{ L' ' }, StringSplitOptions::RemoveEmptyEntries)) if (f != "-g") kept->Add(f);
                flags = String::Join(" ", kept);
            }
            mx->Set("ArchFlag", RideToolchain::ArchFlag(tool, cfg->TargetKind));
            mx->Set("ConfigFlags", flags);
            mx->Set("AsmSyntaxFlag", RideToolchain::AssemblerFlag(tool, cfg->TargetKind, debug && !noLineTable, !String::IsNullOrWhiteSpace(tc->Assembler)));
            mx->Set("LangFlags", RideToolchain::LanguageFlags(tool, lang));
            // the compiler's own spellings; the target's settings stand for the C compiler's family,
            // and a compiler with no include flag (shalimar) takes no -I and no -D
            String^ ownInclude = RideToolchain::IncludeFlagOf(tool);
            String^ ownDefine = RideToolchain::DefineFlagOf(tool);
            bool hostTool = tool == RideTool::Cl || tool == RideTool::HostCxx;
            String^ includeFlag = hostTool ? ownInclude : tc->IncludeFlag;
            String^ defineFlag = hostTool ? ownDefine : tc->DefineFlag;
            List<String^>^ inc = gcnew List<String^>();
            for each (String^ d in cfg->IncludeDirs) inc->Add(p->ToAbsolute(d));
            // the .pro's "include", for every configuration
            for each (String^ d in p->Includes) if (!String::IsNullOrWhiteSpace(d)) inc->Add(p->ToAbsolute(d->Trim()));
            inc->AddRange(Filled(p, tc->IncludeDirs));
            for each (String^ d in settings->Env->SharedIncludes)
                if (!String::IsNullOrWhiteSpace(d)) inc->Add(Filled(p, d->Trim()));
            for each (String^ d in RideToolchain::InstallIncludeDirs(tool, ToolchainSettings::RideRoot, settings->Env->RideIncludeDir, settings->Env->RideLibDir))
                if (!inc->Contains(d)) inc->Add(d);
            // the host's compiler reads its own headers (Visual Studio's INCLUDE): an installation's header
            // directory, however it came (shared includes, the target's), would stand in front of them
            if (hostTool) KeepInstallHeadersFrom(inc);
            mx->Set("IncludeFlags", String::IsNullOrEmpty(ownInclude) ? String::Empty : MacroExpander::JoinFlags(includeFlag, inc));
            mx->Set("DefineFlags", String::IsNullOrEmpty(ownDefine) ? String::Empty : MacroExpander::JoinDefines(defineFlag, Filled(p, cfg->Defines)));
            // a Visual Studio project's own cl settings go to cl after the configuration's flags (/W4 /permissive- /EHsc /MT ...)
            String^ own = String::Empty;
            if (tool == RideTool::Cl && lang != Language::Assembly)
            {
                List<String^>^ unmapped = gcnew List<String^>();
                own = ProjectToolFlags(p, cfg, RideToolchain::Definition(tool), unmapped);
                if (!clSettingsSaid && (own->Length > 0 || unmapped->Count > 0))
                {
                    clSettingsSaid = true;
                    if (own->Length > 0) Say("[the project's own settings give cl: " + own + "]");
                    if (unmapped->Count > 0) Say("[not given to cl - toolchain.json's projectSettings names no switch for: " + String::Join("; ", unmapped) + "]");
                }
            }
            mx->Set("CompileFlags", (cfg->ExtraCompileFlags + " " + own)->Trim());
        }

        // takes the installation's header directories (RIDE's include and lib) out of a host compiler's list, and says so once
        void KeepInstallHeadersFrom(List<String^>^ inc)
        {
            InstallationDefinition^ inst = ToolchainDefinitions::Current->Installation;
            List<String^>^ ride = gcnew List<String^>();
            for each (String^ d in gcnew array<String^>{ RideToolchain::InstallDir(ToolchainSettings::RideRoot, inst->IncludeDir, settings->Env->RideIncludeDir),
                                                         RideToolchain::InstallDir(ToolchainSettings::RideRoot, inst->LibDir, settings->Env->RideLibDir) })
                if (!String::IsNullOrEmpty(d)) ride->Add(d);
            List<String^>^ kept = gcnew List<String^>();
            for (int i = inc->Count - 1; i >= 0; i--)
            {
                bool theirs = false;
                for each (String^ d in ride) if (PathUtil::SamePath(inc[i], d)) theirs = true;
                if (!theirs) continue;
                if (!kept->Contains(inc[i])) kept->Insert(0, inc[i]);
                inc->RemoveAt(i);
            }
            if (kept->Count == 0 || rideHeadersSaid) return;
            rideHeadersSaid = true;
            Say("[" + String::Join(" and ", kept) + (kept->Count == 1 ? " is" : " are") + " not given to cl: the installation's headers would stand in front of Visual Studio's]");
        }

        // the names a project's lists may use (NamedPaths): the .pro's "paths", settings.json's, read afresh for each
        // build, then GRIDE's own - the project's and workspace's folders, Config, Arch, the installation's folders,
        // and each library of toolchain.json under its key, the folder it is found in. Folders end in a backslash.
        NamedPaths^ NamesOf(GrideProject^ p, BuildConfiguration^ cfg, ToolchainSettings^ tc, BuildRequest^ r)
        {
            NamedPaths^ n = gcnew NamedPaths();
            for each (KeyValuePair<String^, String^> kv in p->Paths) n->Add(kv.Key, kv.Value);
            try
            {
                JsonValue^ doc = File::Exists(settings->FilePath) ? JsonParser::ParseFile(settings->FilePath) : nullptr;
                JsonValue^ named = doc == nullptr ? nullptr : doc->GetObject("paths");
                if (named != nullptr) for each (String^ k in named->KeyNames) n->Add(k, named->GetString(k, String::Empty));
            }
            catch (Exception^) { /* an unreadable file: its other keys say so where they are read */ }
            String^ root = ToolchainSettings::RideRoot;
            InstallationDefinition^ inst = ToolchainDefinitions::Current->Installation;
            n->Add("ProjectDir", PathUtil::WithTrailingSlash(p->ProjectDir));
            n->Add("WorkspaceDir", PathUtil::WithTrailingSlash(String::IsNullOrEmpty(r->WorkspaceDir) ? p->ProjectDir : r->WorkspaceDir));
            n->Add("Config", cfg->Config);
            n->Add("Arch", RideToolchain::Arch(cfg->TargetKind));
            n->Add("GrideRoot", PathUtil::WithTrailingSlash(RideToolchain::InstallRoot(root)));
            n->Add("GrideRoot", PathUtil::WithTrailingSlash(PathUtil::AppDirectory()));
            n->Add("GrideInclude", PathUtil::WithTrailingSlash(RideToolchain::InstallDir(root, inst->IncludeDir, settings->Env->RideIncludeDir)));
            n->Add("GrideInclude", PathUtil::WithTrailingSlash(Path::Combine(PathUtil::AppDirectory(), inst->IncludeDir)));
            n->Add("GrideLib", PathUtil::WithTrailingSlash(RideToolchain::InstallDir(root, inst->LibDir, settings->Env->RideLibDir)));
            n->Add("GrideLib", PathUtil::WithTrailingSlash(Path::Combine(PathUtil::AppDirectory(), inst->LibDir)));
            for each (LibraryDefinition^ l in ToolchainDefinitions::Current->Libraries)
                n->Add(l->Id, PathUtil::WithTrailingSlash(RideToolchain::FindLibraryDir(l, tc->ToolDir, root)));
            return n;
        }

        // every name the lists use is known, or the build stops here, naming it and the entry: a compiler given a
        // $(NAME) says something far stranger. A CCS or Visual Studio project's $(...) are its own, and stay.
        bool NamesKnown(GrideProject^ p, BuildConfiguration^ cfg, ToolchainSettings^ tc)
        {
            NamedPaths^ n = p->Names;
            n->Unknown = nullptr;
            for each (IEnumerable<String^>^ list in gcnew array<IEnumerable<String^>^>{ p->Includes, p->Libraries, cfg->IncludeDirs, cfg->LibDirs, cfg->Libs,
                     cfg->Defines, tc->IncludeDirs, tc->LibDirs, settings->Env->SharedIncludes, settings->Env->SharedLibraries })
                n->ExpandAll(list);
            for each (RideTool t in RideToolchain::Compilers()) RideToolchain::ConfigFlags(t, cfg->IsDebug, cfg->TargetKind, p->Options, n);
            if (n->Unknown == nullptr || p->IsReadOnly) return true;
            Report(Severity::Error, p->FilePath, "$(" + n->Unknown + ") in \"" + n->UnknownIn + "\" is not a known name, or names itself: name it under \"paths\" in the .pro or in settings.json");
            return false;
        }

        // an entry, or a list, of a project or of Tools > Options with its names filled in
        static String^ Filled(GrideProject^ p, String^ s) { return p->Names == nullptr ? Environment::ExpandEnvironmentVariables(s) : p->Names->Expand(s); }

        static List<String^>^ Filled(GrideProject^ p, IEnumerable<String^>^ list)
        {
            List<String^>^ r = gcnew List<String^>();
            for each (String^ s in list) r->Add(Filled(p, s));
            return r;
        }

        MacroExpander^ BaseMacros(GrideProject^ p, BuildConfiguration^ cfg, ToolchainSettings^ tc, BuildRequest^ r)
        {
            MacroExpander^ mx = gcnew MacroExpander();
            mx->Set("ToolDir", tc->ToolDir);
            mx->Set("Assembler", tc->ResolveFor("assembler", tc->Assembler));
            mx->Set("Linker", tc->ResolveFor("linker", tc->Linker));
            mx->Set("DebugAssembler", tc->ResolveFor("debugAssembler", tc->DebugAssembler));
            mx->Set("DebugLinker", tc->ResolveFor("debugLinker", tc->DebugLinker));
            mx->Set("Runtime", tc->Resolve(tc->Runtime));
            mx->Set("RuntimeDir", tc->ResolveRuntimeDir());
            mx->Set("Simulator", tc->ResolveFor("simulator", tc->Simulator));
            mx->Set("Emulator", tc->ResolveFor("emulator", tc->Emulator));
            mx->Set("Debugger", tc->ResolveFor("debugger", tc->Debugger));
            mx->Set("Arch", RideToolchain::Arch(cfg->TargetKind));
            mx->Set("ArchFlag", RideToolchain::ArchFlag(RideTool::C90, cfg->TargetKind));
            mx->Set("ConfigFlags", RideToolchain::ConfigFlags(RideTool::C90, cfg->IsDebug, cfg->TargetKind, p->Options, p->Names));
            mx->Set("CrtLibs", RideToolchain::CrtLibraries(cfg->TargetKind, cfg->IsDebug));
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
            // the .pro's "include", for every configuration
            for each (String^ d in p->Includes) if (!String::IsNullOrWhiteSpace(d)) inc->Add(p->ToAbsolute(d->Trim()));
            inc->AddRange(Filled(p, tc->IncludeDirs));
            mx->Set("IncludeFlags", MacroExpander::JoinFlags(tc->IncludeFlag, inc));
            mx->Set("DefineFlags", MacroExpander::JoinDefines(tc->DefineFlag, Filled(p, cfg->Defines)));

            List<String^>^ libDirs = gcnew List<String^>();
            for each (String^ d in cfg->LibDirs) libDirs->Add(p->ToAbsolute(d));
            libDirs->AddRange(Filled(p, tc->LibDirs));
            mx->Set("LibDirFlags", MacroExpander::JoinFlags(tc->LibDirFlag, libDirs));
            // the project's libraries, then the shared ones every host link takes (RIDE's settings.json "libraries")
            List<String^>^ libs = Filled(p, cfg->Libs);
            // the .pro's "libraries", for every configuration
            for each (String^ l in p->Libraries) if (!String::IsNullOrWhiteSpace(l)) libs->Add(p->ToAbsolute(l->Trim()));
            if (!RideToolchain::IsEmulated(cfg->TargetKind))
                for each (String^ l in settings->Env->SharedLibraries)
                    if (!String::IsNullOrWhiteSpace(l) && !libs->Contains(l->Trim())) libs->Add(Filled(p, l->Trim()));
            mx->Set("LibFlags", MacroExpander::JoinFlags(tc->LibFlag, libs));
            return mx;
        }

        DateTime NewestHeader(GrideProject^ p)
        {
            DateTime newest = DateTime::MinValue;
            for each (String^ f in p->AllFiles())
            {
                if (!IsHeader(f)) continue;
                if (File::Exists(f))
                {
                    DateTime t = File::GetLastWriteTime(f);
                    if (t > newest) newest = t;
                }
            }
            return newest;
        }

        // Clean deletes every file in the folder, so it must lie strictly inside the project folder
        bool CanClean(GrideProject^ p, String^ dir, String^ what)
        {
            if (!Directory::Exists(dir) || PathUtil::IsStrictlyUnder(p->ProjectDir, dir)) return true;
            Report(Severity::Error, nullptr, String::Format(
                "Clean refused: the {0} folder '{1}' is not a sub-folder of the project folder '{2}'. "
                "Nothing was deleted. Change it in Project > Properties.", what, dir, p->ProjectDir));
            return false;
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
