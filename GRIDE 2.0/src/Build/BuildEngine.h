#pragma once
// ============================================================================
//  GRIDE 2.0 - Build system, error parsing, run & convert (Phases 7, 8, 9, 10)
//
//  Pipeline per source file:   compile (C90 / C++11 / Shalimar -> target asm)
//                              -> assemble (MASM / ASM6x / as) -> object
//  Then:                       link (LINK / LNK6x / ld) objects + libs (+RTS6747)
//  Incremental: a file is rebuilt only when it, a project header, or the
//  build settings changed since its object file was made.
// ============================================================================
#include "../Toolchain/Toolchain.h"

namespace Gride
{
    public ref class BuildMessage
    {
    public:
        property String^ Severity;      // "error", "warning", "info"
        property String^ FilePath;
        property int Line;
        property int Column;
        property String^ Code;
        property String^ Text;
        property String^ Tool;
    };

    public ref class ErrorParser
    {
    public:
        ErrorParser()
        {
            RegexOptions o = RegexOptions::IgnoreCase | RegexOptions::CultureInvariant;
            custom = gcnew Dictionary<String^, Regex^>();
            builtIn = gcnew array<Regex^>{
                // TI asm6x:   "main.asm", ERROR!   at line 5: [E0002] Illegal mnemonic specified
                gcnew Regex(R"RX(^\s*"(?<file>[^"]+)",\s*(?<sev>ERROR|WARNING|REMARK)!\s+at\s+(?:EOF|line\s+(?<line>\d+))\s*:\s*(?:\[(?<code>\w+)\])?\s*(?<msg>.*)$)RX", o),
                // TI:   "main.c", line 12: error #20: identifier "x" is undefined
                gcnew Regex(R"RX(^\s*"(?<file>[^"]+)",\s*line\s+(?<line>\d+)(?:\s*\(col\.\s*(?<col>\d+)\))?\s*:\s*(?<sev>fatal error|error|warning|remark)\s*(?:#(?<code>[\w-]+))?\s*:?\s*(?<msg>.*)$)RX", o),
                // MSVC / MASM:   C:\src\a.asm(12) : error A2008: syntax error
                gcnew Regex(R"RX(^\s*(?<file>(?:[A-Za-z]:)?[^:(]+?)\((?<line>\d+)(?:,(?<col>\d+))?\)\s*:\s*(?<sev>fatal error|error|warning)\s*(?<code>[A-Za-z]+\d+)?\s*:\s*(?<msg>.*)$)RX", o),
                // GCC / Clang style:   src/a.c:12:5: error: expected ';'
                gcnew Regex(R"RX(^\s*(?<file>(?:[A-Za-z]:)?[^:]+):(?<line>\d+):(?:(?<col>\d+):)?\s*(?<sev>fatal error|error|warning|note)\s*:\s*(?<msg>.*)$)RX", o),
                // LINK : fatal error LNK1104: cannot open file 'x.lib'
                gcnew Regex(R"RX(^\s*(?<file>(?:[A-Za-z]:)?[^:]*?)\s*:\s*(?<sev>fatal error|error|warning)\s+(?<code>[A-Za-z]+\d+)\s*:\s*(?<msg>.*)$)RX", o),
                // error: something / warning #10247-D: something
                gcnew Regex(R"RX(^\s*>*\s*(?<sev>fatal error|error|warning)\s*(?<code>#?[\w-]*)\s*:\s*(?<msg>.*)$)RX", o)
            };
        }

        // Compiles a tool's custom pattern (named groups file, line, col, sev, code, msg).
        // Returns nullptr when empty or invalid. Thread-safe.
        Regex^ Compile(String^ pattern)
        {
            if (String::IsNullOrWhiteSpace(pattern)) return nullptr;
            System::Threading::Monitor::Enter(custom);
            try { return GetCustom(pattern); }
            finally { System::Threading::Monitor::Exit(custom); }
        }

        BuildMessage^ Parse(String^ line, String^ customPattern, String^ baseDir)
        {
            return Parse(line, Compile(customPattern), baseDir);
        }

        // Safe to call from several threads at once (Regex matching is thread-safe).
        BuildMessage^ Parse(String^ line, Regex^ customRegex, String^ baseDir)
        {
            if (String::IsNullOrWhiteSpace(line)) return nullptr;
            Match^ m = nullptr;
            if (customRegex != nullptr)
            {
                Match^ t = customRegex->Match(line);
                if (t->Success) m = t;
            }
            if (m == nullptr)
            {
                for each (Regex^ r in builtIn)
                {
                    Match^ t = r->Match(line);
                    if (t->Success) { m = t; break; }
                }
            }
            if (m == nullptr) return nullptr;

            BuildMessage^ b = gcnew BuildMessage();
            String^ sev = m->Groups["sev"]->Success ? m->Groups["sev"]->Value->ToLowerInvariant() : Str("error");
            b->Severity = sev->Contains("error") ? Str("error") : (String::Equals(sev, "warning") ? Str("warning") : Str("info"));
            b->Code = m->Groups["code"]->Success ? m->Groups["code"]->Value : String::Empty;
            b->Text = m->Groups["msg"]->Success ? m->Groups["msg"]->Value->Trim() : line->Trim();
            int n = 0;
            if (m->Groups["line"]->Success && Int32::TryParse(m->Groups["line"]->Value, n)) b->Line = n;
            n = 0;
            if (m->Groups["col"]->Success && Int32::TryParse(m->Groups["col"]->Value, n)) b->Column = n;
            b->FilePath = String::Empty;
            if (m->Groups["file"]->Success)
            {
                String^ f = m->Groups["file"]->Value->Trim();
                // "LINK : fatal error ..." - a tool name, not a file
                if (f->Length > 0 && !Path::HasExtension(f) && f->IndexOfAny(gcnew array<wchar_t>{ L'\\', L'/' }) < 0) f = String::Empty;
                if (f->Length > 0)
                {
                    try
                    {
                        b->FilePath = Path::IsPathRooted(f) || String::IsNullOrEmpty(baseDir)
                            ? f : Path::GetFullPath(Path::Combine(baseDir, f));
                    }
                    catch (Exception^) { b->FilePath = f; }
                }
            }
            return b;
        }

    private:
        array<Regex^>^ builtIn;
        Dictionary<String^, Regex^>^ custom;

        Regex^ GetCustom(String^ pattern)
        {
            Regex^ r = nullptr;
            if (custom->TryGetValue(pattern, r)) return r;
            try { r = gcnew Regex(pattern, RegexOptions::IgnoreCase | RegexOptions::CultureInvariant); }
            catch (ArgumentException^) { r = nullptr; }
            custom[pattern] = r;
            return r;
        }
    };

    public enum class BuildAction { Build, Rebuild, Clean, CompileFile };

    public ref class BuildResult
    {
    public:
        property bool Success;
        property bool Cancelled;
        property String^ OutputFile;
        property int Errors;
        property int Warnings;
        property TimeSpan Elapsed;
    };

    public ref class BuildEngine
    {
    public:
        literal String^ StampFileName = ".gride-buildstamp";
        literal String^ LinkStampFileName = ".gride-linkstamp";

        // For BuildAction::CompileFile: the one file to compile (full path).
        property String^ SingleFile;

        BuildEngine(GrideProject^ project, IdeSettings^ settings, Action<String^>^ output,
                    Action<BuildMessage^>^ message, CancellationToken token)
        {
            this->project = project;
            this->settings = settings;
            this->output = output;
            this->message = message;
            this->token = token;
            parser = gcnew ErrorParser();
            target = TargetInfo::Get(project->TargetValue);
            config = project->ActiveConfig();
            projectDir = project->ProjectDir;
            outDir = project->OutputDirectory();
            objDir = Path::Combine(outDir, "obj");
            exePath = OutputFileOf(project);
        }

        static String^ OutputFileOf(GrideProject^ p)
        {
            TargetInfo^ t = TargetInfo::Get(p->TargetValue);
            String^ name = String::IsNullOrWhiteSpace(p->OutputName) ? p->Name : p->OutputName->Trim();
            return Path::Combine(p->OutputDirectory(), String::Concat(name, t->ExeExt));
        }

        BuildResult^ Execute(BuildAction action)
        {
            Diagnostics::Stopwatch^ sw = Diagnostics::Stopwatch::StartNew();
            BuildResult^ res = gcnew BuildResult();
            res->OutputFile = exePath;
            String^ verb = "Build";
            if (action == BuildAction::Clean) verb = "Clean";
            else if (action == BuildAction::Rebuild) verb = "Rebuild";
            else if (action == BuildAction::CompileFile) verb = "Compile";
            Out(String::Format("------ {0} started: Project: {1}, Configuration: {2}, Target: {3} ------",
                               verb, project->Name, config->Name, project->Target));
            bool ok = true;
            try
            {
                if (action == BuildAction::Clean || action == BuildAction::Rebuild) ok = Clean();
                if (ok && action == BuildAction::CompileFile) ok = DoCompileFile();
                else if (ok && action != BuildAction::Clean) ok = DoBuild(action == BuildAction::Rebuild);
            }
            catch (Exception^ ex)
            {
                AddMessage("error", String::Format("Internal build error: {0}", ex->Message), nullptr, 0);
                ok = false;
            }
            sw->Stop();
            res->Cancelled = token.IsCancellationRequested;
            res->Success = ok && !res->Cancelled && errors == 0;
            res->Errors = errors;
            res->Warnings = warnings;
            res->Elapsed = sw->Elapsed;
            Out(String::Format("{0} - {1} error(s), {2} warning(s)  [{3:0.0} s]",
                               res->Cancelled ? Str("Cancelled") : (res->Success ? Str("Succeeded") : Str("Failed")),
                               errors, warnings, sw->Elapsed.TotalSeconds));
            Out(String::Format("========== {0}: {1} succeeded, {2} failed ==========",
                               verb, res->Success ? 1 : 0, res->Success ? 0 : 1));
            return res;
        }

        // ---------------------------------------------------------------- run (Phase 9)
        static int RunOutput(GrideProject^ p, IdeSettings^ s, Action<String^>^ out, CancellationToken token)
        {
            TargetInfo^ t = TargetInfo::Get(p->TargetValue);
            String^ exe = OutputFileOf(p);
            if (!File::Exists(exe))
            {
                out->Invoke(String::Format("error: output '{0}' not found. Build the project first.", exe));
                return -1;
            }
            if (t->RunsNatively)
            {
                // New console window that stays open after the program ends.
                ProcessStartInfo^ psi = gcnew ProcessStartInfo("cmd.exe",
                    String::Format("/c \"\"{0}\" & echo. & pause\"", exe));
                psi->UseShellExecute = false;
                psi->CreateNoWindow = false;
                psi->WorkingDirectory = Path::GetDirectoryName(exe);
                try
                {
                    Process::Start(psi);
                    out->Invoke(String::Format("Started {0} in a new console window.", Path::GetFileName(exe)));
                    return 0;
                }
                catch (Exception^ ex)
                {
                    out->Invoke(String::Format("error: could not start program: {0}", ex->Message));
                    return -1;
                }
            }
            ToolDefinition^ runner = s->GetTool(t->RunnerId);
            String^ runnerExe = runner != nullptr ? runner->ResolvedPath() : nullptr;
            if (runnerExe == nullptr)
            {
                out->Invoke(String::Format("error: {0} is not configured or not found. Set it in Tools > Options > Toolchains.",
                                           runner != nullptr ? runner->DisplayName : t->RunnerId));
                return -1;
            }
            Dictionary<String^, String^>^ v = gcnew Dictionary<String^, String^>();
            v["exe"] = exe;
            v["outdir"] = Path::GetDirectoryName(exe);
            v["projectdir"] = p->ProjectDir;
            v["name"] = p->Name;
            v["target"] = t->TargetSwitch;
            String^ args = ToolRunner::Expand(runner->Arguments, v);
            out->Invoke(String::Format("> {0} {1}", PathUtil::Quote(runnerExe), args));
            int rc = ToolRunner::Run(runnerExe, args, p->ProjectDir, out, token, runner);
            out->Invoke(String::Format("Program exited with code {0}.", rc));
            return rc;
        }

        // ---------------------------------------------------------------- convert (Phase 10)
        static int RunConversion(ToolDefinition^ tool, String^ input, String^ outputPath, String^ workDir,
                                 Action<String^>^ out, CancellationToken token)
        {
            String^ exe = tool->ResolvedPath();
            if (exe == nullptr)
            {
                out->Invoke(String::Format("error: {0} is not configured or not found.", tool->DisplayName));
                return -1;
            }
            Dictionary<String^, String^>^ v = gcnew Dictionary<String^, String^>();
            v["in"] = input;
            v["out"] = outputPath;
            v["includes"] = String::Empty;
            v["defines"] = String::Empty;
            v["flags"] = String::Empty;
            String^ args = ToolRunner::Expand(tool->Arguments, v);
            out->Invoke(String::Format("------ Convert: {0} -> {1} ------", Path::GetFileName(input), Path::GetFileName(outputPath)));
            out->Invoke(String::Format("> {0} {1}", PathUtil::Quote(exe), args));
            try { if (File::Exists(outputPath)) File::Delete(outputPath); } catch (Exception^) {}
            int rc = ToolRunner::Run(exe, args, workDir, out, token, tool);
            if (rc == 0 && !File::Exists(outputPath))
            {
                out->Invoke("error: the converter finished but did not create the output file.");
                rc = -1;
            }
            out->Invoke(rc == 0 ? Str("Conversion succeeded.") : String::Format("Conversion failed (code {0}).", rc));
            return rc;
        }

    private:
        GrideProject^ project;
        IdeSettings^ settings;
        Action<String^>^ output;
        Action<BuildMessage^>^ message;
        CancellationToken token;
        ErrorParser^ parser;
        TargetInfo^ target;
        BuildConfiguration^ config;
        String^ projectDir;
        String^ outDir;
        String^ objDir;
        String^ exePath;
        int errors;
        int warnings;
        // tool currently running (read by the output callback)
        String^ currentTool;
        Regex^ currentRegex;    // pre-compiled custom error pattern of the running tool
        String^ currentDir;

        void Out(String^ s) { if (output != nullptr) output->Invoke(s); }

        void AddMessage(String^ sev, String^ text, String^ file, int line)
        {
            BuildMessage^ m = gcnew BuildMessage();
            m->Severity = sev; m->Text = text; m->FilePath = file == nullptr ? String::Empty : file;
            m->Line = line; m->Code = String::Empty; m->Tool = "GRIDE";
            Count(m);
            Out(String::Format("{0}: {1}", sev, text));
            if (message != nullptr) message->Invoke(m);
        }

        void Count(BuildMessage^ m)
        {
            if (String::Equals(m->Severity, "error")) System::Threading::Interlocked::Increment(errors);
            else if (String::Equals(m->Severity, "warning")) System::Threading::Interlocked::Increment(warnings);
        }

        void OnToolLine(String^ line)
        {
            Out(line);
            BuildMessage^ m = parser->Parse(line, currentRegex, currentDir);
            if (m == nullptr) return;
            m->Tool = currentTool;
            Count(m);
            if (message != nullptr) message->Invoke(m);
        }

        // ---------------------------------------------------------------- clean
        bool Clean()
        {
            if (!Directory::Exists(outDir)) { Out("Nothing to clean."); return true; }
            if (!PathUtil::IsStrictlyUnder(projectDir, outDir))
            {
                AddMessage("error", String::Format("Refusing to clean '{0}': it is not inside the project folder.", outDir), nullptr, 0);
                return false;
            }
            try
            {
                Directory::Delete(outDir, true);
                Out(String::Format("Deleted {0}", outDir));
                return true;
            }
            catch (Exception^ ex)
            {
                AddMessage("error", String::Format("Clean failed: {0}", ex->Message), nullptr, 0);
                return false;
            }
        }

        // ---------------------------------------------------------------- tools
        ToolDefinition^ RequireTool(String^ id)
        {
            ToolDefinition^ t = settings->GetTool(id);
            if (t == nullptr)
            {
                AddMessage("error", String::Format("Unknown tool '{0}'.", id), nullptr, 0);
                return nullptr;
            }
            if (!t->IsConfigured())
            {
                AddMessage("error", String::Format("{0} is not configured. Set its path in Tools > Options > Toolchains.", t->DisplayName), nullptr, 0);
                return nullptr;
            }
            if (t->ResolvedPath() == nullptr)
            {
                AddMessage("error", String::Format("{0} not found: '{1}'.", t->DisplayName, t->ExecutablePath), nullptr, 0);
                return nullptr;
            }
            return t;
        }

        String^ JoinSwitch(String^ sw, List<String^>^ items, bool resolvePaths)
        {
            if (String::IsNullOrEmpty(sw) || items == nullptr) return String::Empty;
            StringBuilder^ sb = gcnew StringBuilder();
            for each (String^ i in items)
            {
                if (String::IsNullOrWhiteSpace(i)) continue;
                String^ v = resolvePaths ? PathUtil::Quote(PathUtil::Resolve(projectDir, i)) : i->Trim();
                sb->Append(sw)->Append(v)->Append(L' ');
            }
            return sb->ToString()->Trim();
        }

        Dictionary<String^, String^>^ Vars(ToolDefinition^ tool)
        {
            Dictionary<String^, String^>^ d = gcnew Dictionary<String^, String^>();
            d["target"] = target->TargetSwitch;
            d["config"] = config->Name;
            d["name"] = project->Name;
            d["projectdir"] = projectDir;
            d["outdir"] = outDir;
            d["objdir"] = objDir;
            d["exe"] = exePath;
            d["map"] = Path::ChangeExtension(exePath, ".map");
            d["flags"] = config->Flags;
            d["linkflags"] = config->LinkFlags;
            d["asmflags"] = config->AsmFlags;
            d["includes"] = JoinSwitch(tool->IncludeSwitch, project->IncludePaths, true);
            d["defines"] = JoinSwitch(tool->DefineSwitch, config->Defines, false);
            String^ rt = RuntimePath();
            d["runtime"] = rt == nullptr ? String::Empty : rt;
            return d;
        }

        String^ RuntimePath()
        {
            if (target->RuntimeId == nullptr) return nullptr;
            ToolDefinition^ rt = settings->GetTool(target->RuntimeId);
            return rt != nullptr ? rt->ResolvedPath() : nullptr;
        }

        int RunTool(ToolDefinition^ tool, Dictionary<String^, String^>^ vars)
        {
            String^ exe = tool->ResolvedPath();
            String^ args = ToolRunner::Expand(tool->Arguments, vars);
            Out(String::Format("> {0} {1}", PathUtil::Quote(exe), args));
            currentTool = tool->DisplayName;
            currentRegex = parser->Compile(tool->ErrorPattern);      // compiled before output threads start
            currentDir = projectDir;
            int before = errors;
            int rc = ToolRunner::Run(exe, args, projectDir, gcnew Action<String^>(this, &BuildEngine::OnToolLine), token, tool);
            if (rc != 0 && rc != ToolRunner::ExitCancelled && errors == before)
                AddMessage("error", String::Format("{0} exited with code {1}.", tool->DisplayName, rc), nullptr, 0);
            return rc;
        }

        // Fingerprint of everything that should force a full rebuild when changed.
        String^ BuildStamp()
        {
            StringBuilder^ sb = gcnew StringBuilder();
            sb->AppendLine(project->Target)->AppendLine(config->Name)->AppendLine(config->Flags)
              ->AppendLine(config->AsmFlags)
              ->AppendLine(PathUtil::JoinList(config->Defines))->AppendLine(PathUtil::JoinList(project->IncludePaths));
            array<String^>^ ids = gcnew array<String^>{ ToolIds::CompilerC90, ToolIds::CompilerCpp11,
                                                        ToolIds::CompilerShalimar, target->AssemblerId };
            for each (String^ id in ids)
            {
                ToolDefinition^ t = settings->GetTool(id);
                if (t != nullptr) sb->AppendLine(t->ExecutablePath)->AppendLine(t->Arguments)->AppendLine(t->IncludeSwitch)
                                    ->AppendLine(t->DefineSwitch)->AppendLine(t->Environment)->AppendLine(t->SetupScript);
            }
            return sb->ToString();
        }

        // Everything the link step depends on besides the object files.
        String^ LinkStamp(ToolDefinition^ linker, List<String^>^ libs, List<String^>^ cmdFiles)
        {
            StringBuilder^ sb = gcnew StringBuilder();
            sb->AppendLine(exePath)->AppendLine(config->LinkFlags)
              ->AppendLine(linker->ExecutablePath)->AppendLine(linker->Arguments)->AppendLine(linker->LibrarySwitch)
              ->AppendLine(linker->Environment)->AppendLine(linker->SetupScript)
              ->AppendLine(String::Join("|", libs->ToArray()))->AppendLine(String::Join("|", cmdFiles->ToArray()));
            return sb->ToString();
        }

        static DateTime TimeOf(String^ f)
        {
            return File::Exists(f) ? File::GetLastWriteTimeUtc(f) : DateTime::MinValue;
        }

        static void TryDelete(String^ f)
        {
            try { if (File::Exists(f)) File::Delete(f); } catch (Exception^) {}
        }

        // Newest project header and the TMS linker command files.
        void ScanProjectFiles(DateTime% newestHeader, List<String^>^ cmdFiles)
        {
            newestHeader = DateTime::MinValue;
            for each (String^ rel in project->SourceFiles)
            {
                String^ full = project->ResolvePath(rel);
                String^ ext = Path::GetExtension(full);
                if (TargetInfo::IsHeader(ext))
                {
                    DateTime t = TimeOf(full);
                    if (t > newestHeader) newestHeader = t;
                }
                else if (TargetInfo::IsLinkerCommandFile(ext, target->Kind))
                    cmdFiles->Add(full);
            }
        }

        // Result of compiling one project entry.
        enum class StepResult { Skipped, UpToDate, Built, Failed, Cancelled };

        // compile (if needed) + assemble one source file into obj
        StepResult CompileOne(String^ src, String^ stem, String^ obj, bool force, DateTime newestHeader)
        {
            String^ ext = Path::GetExtension(src);
            String^ compilerId = TargetInfo::CompilerFor(ext, settings);
            bool isAsm = TargetInfo::IsAssemblySource(ext);
            if (compilerId == nullptr && !isAsm) return StepResult::Skipped;

            if (!File::Exists(src))
            {
                AddMessage("error", String::Format("Source file not found: {0}", src), src, 0);
                return StepResult::Failed;
            }
            DateTime objTime = TimeOf(obj);
            if (!force && objTime != DateTime::MinValue && objTime >= TimeOf(src) && objTime >= newestHeader)
                return StepResult::UpToDate;

            Out(Path::GetFileName(src));
            String^ asmInput = src;
            if (!isAsm)
            {
                ToolDefinition^ compiler = RequireTool(compilerId);
                if (compiler == nullptr) return StepResult::Failed;
                String^ asmOut = Path::Combine(objDir, String::Concat(stem, target->AsmExt));
                TryDelete(asmOut);
                Dictionary<String^, String^>^ v = Vars(compiler);
                v["in"] = src;
                v["out"] = asmOut;
                int rc = RunTool(compiler, v);
                if (rc == ToolRunner::ExitCancelled) return StepResult::Cancelled;
                if (rc != 0) return StepResult::Failed;
                if (!File::Exists(asmOut))
                {
                    AddMessage("error", String::Format("{0} did not produce '{1}'.", compiler->DisplayName, asmOut), src, 0);
                    return StepResult::Failed;
                }
                asmInput = asmOut;
            }

            ToolDefinition^ assembler = RequireTool(target->AssemblerId);
            if (assembler == nullptr) return StepResult::Failed;
            TryDelete(obj);
            Dictionary<String^, String^>^ va = Vars(assembler);
            va["in"] = asmInput;
            va["out"] = obj;
            int arc = RunTool(assembler, va);
            if (arc == ToolRunner::ExitCancelled) return StepResult::Cancelled;
            if (arc != 0) return StepResult::Failed;
            if (!File::Exists(obj))
            {
                AddMessage("error", String::Format("{0} did not produce '{1}'.", assembler->DisplayName, obj), asmInput, 0);
                return StepResult::Failed;
            }
            return StepResult::Built;
        }

        // Object file names for every source entry, in project order (stable across builds).
        Dictionary<String^, String^>^ StemMap()
        {
            Dictionary<String^, String^>^ map = gcnew Dictionary<String^, String^>(StringComparer::OrdinalIgnoreCase);
            HashSet<String^>^ used = gcnew HashSet<String^>(StringComparer::OrdinalIgnoreCase);
            for each (String^ rel in project->SourceFiles)
            {
                String^ full = project->ResolvePath(rel);
                if (!TargetInfo::IsSource(Path::GetExtension(full), settings) || map->ContainsKey(full)) continue;
                map[full] = MakeStem(rel, used);
            }
            return map;
        }

        // Ctrl+F7: compile + assemble just one file (no link).
        bool DoCompileFile()
        {
            if (String::IsNullOrEmpty(SingleFile))
            {
                AddMessage("error", "No file selected to compile.", nullptr, 0);
                return false;
            }
            Dictionary<String^, String^>^ stems = StemMap();
            String^ stem = nullptr;
            if (!stems->TryGetValue(Path::GetFullPath(SingleFile), stem))
            {
                AddMessage("error", String::Format("{0} is not a source file of this project.", Path::GetFileName(SingleFile)), SingleFile, 0);
                return false;
            }
            Directory::CreateDirectory(objDir);
            String^ obj = Path::Combine(objDir, String::Concat(stem, target->ObjExt));
            StepResult r = CompileOne(Path::GetFullPath(SingleFile), stem, obj, true, DateTime::MinValue);
            return r == StepResult::Built;
        }

        bool DoBuild(bool rebuild)
        {
            Directory::CreateDirectory(objDir);

            // Full rebuild when compile settings changed since last time.
            String^ stampPath = Path::Combine(objDir, StampFileName);
            String^ stamp = BuildStamp();
            String^ oldStamp = File::Exists(stampPath) ? File::ReadAllText(stampPath) : String::Empty;
            bool forceAll = rebuild || !String::Equals(stamp, oldStamp);
            if (forceAll && !rebuild && oldStamp->Length > 0) Out("Build settings changed - rebuilding all files.");

            // Any change to a project header rebuilds every source file.
            // (Headers found only through include paths are not tracked - use Rebuild.)
            DateTime newestHeader;
            List<String^>^ cmdFiles = gcnew List<String^>();
            ScanProjectFiles(newestHeader, cmdFiles);

            Dictionary<String^, String^>^ stems = StemMap();
            List<String^>^ objs = gcnew List<String^>();
            bool anyFailed = false;
            bool anyBuilt = false;
            int compiledCount = 0;

            for each (String^ rel in project->SourceFiles)
            {
                if (token.IsCancellationRequested) return false;
                String^ src = project->ResolvePath(rel);
                String^ stem = nullptr;
                if (!stems->TryGetValue(src, stem)) continue;              // header / other file
                String^ obj = Path::Combine(objDir, String::Concat(stem, target->ObjExt));
                if (objs->Contains(obj)) continue;                          // listed twice
                objs->Add(obj);
                StepResult r = CompileOne(src, stem, obj, forceAll, newestHeader);
                if (r == StepResult::Cancelled) return false;
                if (r == StepResult::Failed) anyFailed = true;
                if (r == StepResult::Built) { anyBuilt = true; compiledCount++; }
            }

            if (anyFailed || errors > 0)
            {
                Out("Link skipped because of earlier errors.");
                return false;
            }
            if (objs->Count == 0)
            {
                AddMessage("error", "The project has no source files to build.", nullptr, 0);
                return false;
            }
            File::WriteAllText(stampPath, stamp);
            if (compiledCount == 0) Out("All source files are up to date.");

            // ---- link inputs
            ToolDefinition^ linker = RequireTool(target->LinkerId);
            if (linker == nullptr) return false;

            List<String^>^ libs = gcnew List<String^>();
            for each (String^ l in project->Libraries)
            {
                if (String::IsNullOrWhiteSpace(l)) continue;
                String^ lp = l->Trim();
                // bare names (kernel32.lib) are passed as-is, paths are resolved
                if (lp->IndexOf(L'\\') >= 0 || lp->IndexOf(L'/') >= 0 || File::Exists(Path::Combine(projectDir, lp)))
                    lp = PathUtil::Resolve(projectDir, lp);
                libs->Add(lp);
            }
            String^ rt = RuntimePath();
            bool argsUseRuntime = linker->Arguments->IndexOf("{runtime}", StringComparison::OrdinalIgnoreCase) >= 0;
            if (target->RuntimeId != nullptr)
            {
                if (rt == nullptr) Out("warning: RTS6747 runtime library is not configured (Tools > Options > Toolchains).");
                else if (!argsUseRuntime) libs->Add(rt);        // added once: either here or via {runtime}
            }

            // ---- relink needed?
            String^ linkStampPath = Path::Combine(objDir, LinkStampFileName);
            String^ linkStamp = LinkStamp(linker, libs, cmdFiles);
            String^ oldLinkStamp = File::Exists(linkStampPath) ? File::ReadAllText(linkStampPath) : String::Empty;
            DateTime exeTime = TimeOf(exePath);
            bool needLink = forceAll || anyBuilt || exeTime == DateTime::MinValue || !String::Equals(linkStamp, oldLinkStamp);
            if (!needLink)
                for each (String^ o in objs) if (TimeOf(o) > exeTime) { needLink = true; break; }
            if (!needLink)
                for each (String^ l in libs) if (File::Exists(l) && TimeOf(l) > exeTime) { needLink = true; break; }
            if (!needLink)
                for each (String^ c in cmdFiles) if (TimeOf(c) > exeTime) { needLink = true; break; }
            if (!needLink)
            {
                Out(String::Format("{0} is up to date.", Path::GetFileName(exePath)));
                return true;
            }

            StringBuilder^ objList = gcnew StringBuilder();
            for each (String^ o in objs) objList->Append(PathUtil::Quote(o))->Append(L' ');
            StringBuilder^ libList = gcnew StringBuilder();
            for each (String^ l in libs) libList->Append(linker->LibrarySwitch)->Append(PathUtil::Quote(l))->Append(L' ');
            StringBuilder^ cmdList = gcnew StringBuilder();
            for each (String^ c in cmdFiles) cmdList->Append(PathUtil::Quote(c))->Append(L' ');

            Directory::CreateDirectory(outDir);
            TryDelete(exePath);
            TryDelete(linkStampPath);
            Dictionary<String^, String^>^ vl = Vars(linker);
            vl["out"] = exePath;
            vl["objs"] = objList->ToString()->Trim();
            vl["libs"] = libList->ToString()->Trim();
            vl["cmdfiles"] = cmdList->ToString()->Trim();
            Out(String::Format("Linking {0}", Path::GetFileName(exePath)));
            int lrc = RunTool(linker, vl);
            if (lrc != 0) return false;
            if (!File::Exists(exePath))
            {
                AddMessage("error", String::Format("{0} did not produce '{1}'.", linker->DisplayName, exePath), nullptr, 0);
                return false;
            }
            File::WriteAllText(linkStampPath, linkStamp);
            Out(String::Format("{0} -> {1}", project->Name, exePath));
            return true;
        }

        // src\drivers\uart.c -> src_drivers_uart (unique within this build)
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
    };
}
