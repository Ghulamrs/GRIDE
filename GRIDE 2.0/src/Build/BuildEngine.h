#pragma once
// ============================================================================
//  GRIDE 2.0 - Build system, error parsing, run & convert (Phases 7, 8, 9, 10)
//
//  Pipeline per source file:   compile (C90 / C++11 / Shalimar -> target asm)
//                              -> assemble (MASM / ASM6x / as) -> object
//  Then:                       link (LINK / LNK6x / ld) objects + libs (+RTS6747)
//  Incremental: a file is rebuilt only when it, a project header, or the
//  build settings changed since its object file was made.
//  RIDE's rules: a part per compiler with its "$ group (compiler)" line, a
//  Shalimar program compiled as one, a Windows Debug build through clang and
//  Microsoft's link.exe /DEBUG, the C6747 program through asm6x and lnk6x
//  against RTS6x, the environment the compilers read, Visual Studio's folders
//  found without cmd.exe, the native question, and RIDE's refusals.
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

    // The shapes a tool's lines take, read as RIDE's parseDiagnostic reads a build's output
    // (compile.cpp): the toolchain's own pattern first, then the built-in ones - GNU's
    // file:line:col: error: message (c90, cpp11), MSVC's and MASM's file(line,col): error CODE:,
    // TI's "file", line N: error, asm6x's "file", ERROR! at line N:, LINK's, RIDE's masm's
    // file: line N: message - then shalimar's one diagnostic "Error: line N: message", which names
    // no file and is filed under the source being compiled, and c90's two-line preprocessor
    // diagnostic (the directive line, then a caret line carrying the message; the column is where
    // the caret points past the "file:line: " prefix). A compiler announcing itself, or saying it is
    // done, is output, never a message; a line a tool writes about itself ("c90.exe: -g asks where
    // each line of C went...") is an error with no place.
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
                // RIDE's masm:   bad.s: line 3: unknown instruction 'BOGUS'
                gcnew Regex(R"RX(^\s*(?<file>(?:[A-Za-z]:)?[^:]+\.(?:s|asm|S)):\s+line\s+(?<line>\d+):\s*(?<msg>.*)$)RX", RegexOptions::CultureInvariant),
                // error: something / warning #10247-D: something
                gcnew Regex(R"RX(^\s*>*\s*(?<sev>fatal error|error|warning)\s*(?<code>#?[\w-]*)\s*:\s*(?<msg>.*)$)RX", o)
            };
            shalimar = gcnew Regex(R"RX(^Error: line (?<line>\d+):\s*(?<msg>.*)$)RX", RegexOptions::CultureInvariant);
            banner = gcnew Regex(R"RX(^(c90|cpp11|shalimar|shc|cc1|cxx1): compilation completed successfully)RX", o);
            location = gcnew Regex(R"RX(^\s*(?<file>[^:(]*(?::[\\/][^:(]*)?)(?::(?<line>\d+)(?::(?<col>\d+))?:|\((?<line>\d+)(?:,(?<col>\d+))?\)\s*:))RX", RegexOptions::CultureInvariant);
        }

        // a compiler announcing itself, or saying it is done: output, never a message
        bool IsBanner(String^ line)
        {
            if (String::IsNullOrEmpty(line)) return false;
            String^ t = line->Trim();
            return t->Contains("G. R. Akhtar - ") || banner->IsMatch(t);
        }

        // shalimar's "Error: line N: message", on stdout, naming no file: filed under the source being compiled
        BuildMessage^ ParseShalimar(String^ line, String^ source)
        {
            if (String::IsNullOrEmpty(line)) return nullptr;
            Match^ m = shalimar->Match(line);
            if (!m->Success) return nullptr;
            BuildMessage^ b = gcnew BuildMessage();
            b->Severity = "error";
            b->FilePath = source == nullptr ? String::Empty : source;
            b->Line = Int32::Parse(m->Groups["line"]->Value);
            b->Column = 1;
            b->Code = String::Empty;
            b->Text = m->Groups["msg"]->Value->Trim();
            return b;
        }

        // c90's preprocessor: "file:line: <the directive>", then "        ^ message" - the column is where the
        // caret points past the "file:line: " prefix; a caret with nothing after it is somebody underlining
        BuildMessage^ ParseCaret(String^ previous, String^ line, String^ baseDir)
        {
            if (String::IsNullOrEmpty(previous) || String::IsNullOrEmpty(line)) return nullptr;
            int caret = line->IndexOf(L'^');
            if (caret < 0) return nullptr;
            for (int i = 0; i < caret; i++) if (line[i] != L' ' && line[i] != L'\t') return nullptr;
            String^ message = line->Substring(caret + 1)->Trim();
            if (message->Length == 0) return nullptr;
            int after = previous->LastIndexOf(": ");
            if (after <= 0) return nullptr;
            String^ where = previous->Substring(0, after);
            int lineAt = where->LastIndexOf(L':');
            if (lineAt <= 0 || lineAt + 1 >= where->Length) return nullptr;
            int lineNo;
            if (!Int32::TryParse(where->Substring(lineAt + 1), lineNo) || lineNo <= 0) return nullptr;
            BuildMessage^ b = gcnew BuildMessage();
            b->Severity = "error";
            b->FilePath = Absolute(where->Substring(0, lineAt), baseDir);
            b->Line = lineNo;
            int prefix = after + 2;
            b->Column = caret >= prefix ? caret - prefix + 1 : 1;
            b->Code = String::Empty;
            b->Text = message;
            return b;
        }

        // "<tool>: message" or "<path>\<tool>.exe: message": the tool about itself, an error with no place
        BuildMessage^ ParseOwnLine(String^ line, String^ toolName)
        {
            if (String::IsNullOrEmpty(toolName) || String::IsNullOrEmpty(line)) return nullptr;
            int at = line->IndexOf(": ");
            if (at <= 0) return nullptr;
            String^ who = Path::GetFileName(line->Substring(0, at)->Trim())->ToLowerInvariant();
            String^ name = toolName->ToLowerInvariant();
            if (who != name && who != String::Concat(name, ".exe")) return nullptr;
            String^ text = line->Substring(at + 2)->Trim();
            if (text->Length == 0) return nullptr;
            BuildMessage^ b = gcnew BuildMessage();
            b->Severity = "error";
            b->FilePath = String::Empty;
            b->Line = 0; b->Column = 0;
            b->Code = String::Empty;
            b->Text = text;
            return b;
        }

        // a line naming a place with no severity word - "file:line[:col]:" or "file(line[,col]):" - for a
        // double-click on the Output pane (RIDE's whereIs_); nullptr when the line names none
        BuildMessage^ ParseLocation(String^ line, String^ baseDir)
        {
            if (String::IsNullOrEmpty(line)) return nullptr;
            Match^ m = location->Match(line);
            if (!m->Success) return nullptr;
            String^ file = m->Groups["file"]->Value->Trim();
            if (file->Length == 0) return nullptr;
            BuildMessage^ b = gcnew BuildMessage();
            b->Severity = "info";
            b->FilePath = Absolute(file, baseDir);
            b->Line = Int32::Parse(m->Groups["line"]->Value);
            b->Column = m->Groups["col"]->Success ? Int32::Parse(m->Groups["col"]->Value) : 1;
            b->Code = String::Empty;
            b->Text = line->Trim();
            return b;
        }

        // RIDE's reading of one line of a tool's output, every rule in RIDE's order; previous is the
        // line before (the caret rule), source the file being compiled (shalimar's rule), toolName the
        // tool's own name (its own lines)
        BuildMessage^ ParseIn(String^ line, Regex^ customRegex, String^ baseDir, String^ previous, String^ source, String^ toolName)
        {
            if (IsBanner(line)) return nullptr;
            // shalimar's line before the general shapes: "Error: line 4: ..." would otherwise read as an error named "line 4: ..."
            BuildMessage^ m = source == nullptr ? nullptr : ParseShalimar(line, source);
            if (m == nullptr) m = Parse(line, customRegex, baseDir);
            if (m == nullptr) m = ParseCaret(previous, line, baseDir);
            if (m == nullptr) m = ParseOwnLine(line, toolName);
            return m;
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
        Regex^ shalimar;
        Regex^ banner;
        Regex^ location;

        static String^ Absolute(String^ file, String^ baseDir)
        {
            try
            {
                if (file->Length > 0 && !Path::IsPathRooted(file) && !String::IsNullOrEmpty(baseDir)) return Path::GetFullPath(Path::Combine(baseDir, file));
            }
            catch (Exception^) { }
            return file;
        }

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
        property BuildMessage^ FirstError;      // where RIDE puts the caret after a failed build: the first error
        property bool Native;                   // built again with the vendor's assembler and linker, as the native question asked
    };

    // The build, by RIDE's rules (ride-port\RIDE-conventions.md 4.3-4.7): a part per compiler, a Shalimar
    // program compiled as one, a Debug build's CodeView through clang and Microsoft's link.exe /DEBUG,
    // the C6747 program through asm6x and lnk6x against RTS6x with RIDE's own linker command file, the
    // environment the compilers read, Visual Studio's INCLUDE / LIB / PATH found without cmd.exe, the
    // native question, and the refusals (another machine's target only reaches assembly; a folder that
    // cannot be written to). Every tool, name and argument comes from the settings' entries, which come
    // from toolchain.json.
    public ref class BuildEngine
    {
    public:
        literal String^ StampFileName = ".gride-buildstamp";
        literal String^ LinkStampFileName = ".gride-linkstamp";

        // For BuildAction::CompileFile: the one file to compile (full path).
        property String^ SingleFile;
        // who answers the native question (RIDE's askNative), on the window's thread; nullptr: nothing here
        // can ask, and the build stands
        property Func<String^, bool>^ Asker;

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
            lines = gcnew List<String^>();
            failedStep = String::Empty;
            optionStore = project->OptionStore();
            clSettingsSaid = false; rideHeadersSaid = false; noCrtList = false;
        }

        static String^ OutputFileOf(GrideProject^ p)
        {
            TargetInfo^ t = TargetInfo::Get(p->TargetValue);
            String^ name = String::IsNullOrWhiteSpace(p->OutputName) ? p->Name : p->OutputName->Trim();
            return Path::Combine(p->OutputDirectory(), String::Concat(name, t->ExeExt));
        }

        // RIDE's configuration: Debug unless the name says Release
        static bool IsDebugConfig(String^ name)
        {
            return name == nullptr || name->IndexOf("release", StringComparison::OrdinalIgnoreCase) < 0;
        }

        // RIDE's refusal before any tool runs: the program's folder must take a file (a probe written and
        // removed); nullptr when it does, else RIDE's sentence
        static String^ Unwritable(String^ dir)
        {
            try
            {
                Directory::CreateDirectory(dir);
                String^ probe = Path::Combine(dir, ".gride-writes-here");
                File::WriteAllText(probe, String::Empty);
                File::Delete(probe);
                return nullptr;
            }
            catch (Exception^)
            {
                return String::Concat(dir, " cannot be written to, and the program would go there - copy the project to a folder of your own, such as Documents, and build it there");
            }
        }

        BuildResult^ Execute(BuildAction action)
        {
            System::Diagnostics::Stopwatch^ sw = System::Diagnostics::Stopwatch::StartNew();
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
                else if (ok && action != BuildAction::Clean)
                {
                    ok = DoBuild(action == BuildAction::Rebuild);
                    if (!ok && !token.IsCancellationRequested) ok = NativeFallback(action == BuildAction::Rebuild);
                }
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
            res->FirstError = firstError;
            res->Native = native;
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
            // RIDE's whyNotRun before a run of another machine's target
            if (!t->RunsHere && t->CrossOnly)
            {
                out->Invoke(String::Format("error: {0}", TargetInfo::WhyNotRun(p->TargetValue)));
                return -1;
            }
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
            v["exe"] = exe; v["Program"] = exe;
            v["outdir"] = Path::GetDirectoryName(exe); v["OutDir"] = Path::GetDirectoryName(exe);
            v["asmdir"] = Path::Combine(p->OutputDirectory(), "obj");
            v["projectdir"] = p->ProjectDir; v["ProjectDir"] = p->ProjectDir; v["WorkspaceDir"] = p->ProjectDir;
            v["name"] = p->Name; v["Name"] = p->Name; v["ProjectName"] = p->Name;
            v["target"] = t->Arch; v["arch"] = t->Arch; v["Arch"] = t->Arch; v["Target"] = p->Target;
            v["args"] = String::Empty; v["Arguments"] = String::Empty; v["ShalimarRuntimeDir"] = String::Empty;
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
            v["in"] = input; v["Source"] = input;
            v["out"] = outputPath; v["Output"] = outputPath;
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

        // the printed name of a tool: the file's name for it while the entry runs one of its programs
        // (link.exe is "link" as RIDE's linker and "link.exe" as Microsoft's), else the program's own
        static String^ ToolName(ToolDefinition^ t)
        {
            if (t == nullptr) return String::Empty;
            ToolEntry^ e = ToolchainFile::Current->Tool(t->Definition);
            if (e != nullptr && (String::IsNullOrWhiteSpace(t->ExecutablePath) || e->HasName(t->ExecutablePath))) return e->Name;
            String^ x = t->ExecutablePath->Trim()->Trim(L'"');
            try { return Path::GetFileNameWithoutExtension(x); }
            catch (ArgumentException^) { return x; }
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
        BuildMessage^ firstError;
        // tool currently running (read by the output callback)
        String^ currentTool;
        String^ currentToolName;    // the program's own name, for its lines about itself
        Regex^ currentRegex;        // pre-compiled custom error pattern of the running tool
        String^ currentDir;
        String^ currentSource;      // the file being compiled (shalimar's diagnostic names none)
        String^ previousLine;       // the line before (c90's preprocessor caret)
        // RIDE's rules
        List<String^>^ lines;       // the build's output, which the native question looks through
        String^ failedStep;         // compile / assemble / link: where the build failed
        bool placedError;           // an error naming a file and a line was seen: a fault in the source
        bool native;                // the vendor's assembler and linker, as the native question asked
        bool codeViewWarned;        // the missing clang is said once
        String^ lastPart;           // the part whose "$ group (compiler)" line was said last
        String^ envAssembler;       // what C90_AS ... name (worked out once a build)
        String^ envLinker;
        CompilerOptionStore^ optionStore;   // the project's Compiler Options (RIDE's options.cpp)
        String^ tiRuntimeDir;       // TI's lib, when the C6747 links against TI's runtime (the link's $(RuntimeDir))
        String^ tiExtraDir;         // the second directory TI's runtime is looked for in (RIDE's tilib)
        bool romModelForTi;         // TI's linker given --rom_model where the project names no model
        bool clSettingsSaid;        // a Visual Studio project's own settings for cl were named (once a build)
        bool rideHeadersSaid;       // an installation header directory kept from cl was named (once)
        bool noCrtList;             // every object cl's: they name their own C runtime, no CRT list is added

        void Out(String^ s)
        {
            if (output != nullptr) output->Invoke(s);
            System::Threading::Monitor::Enter(lines);
            try { lines->Add(s); }
            finally { System::Threading::Monitor::Exit(lines); }
        }

        bool OutputHas(String^ part)
        {
            System::Threading::Monitor::Enter(lines);
            try { for each (String^ l in lines) if (l != nullptr && l->IndexOf(part, StringComparison::OrdinalIgnoreCase) >= 0) return true; }
            finally { System::Threading::Monitor::Exit(lines); }
            return false;
        }

        void AddMessage(String^ sev, String^ text, String^ file, int line)
        {
            BuildMessage^ m = gcnew BuildMessage();
            m->Severity = sev; m->Text = text; m->FilePath = file == nullptr ? String::Empty : file;
            m->Line = line; m->Code = String::Empty; m->Tool = "GRIDE";
            Count(m);
            Out(String::Format("{0}: {1}", sev, text));
            if (message != nullptr) message->Invoke(m);
        }

        // the first error is where the caret goes: one with a place in the source beats one without
        // (a tool's own line on the other stream can arrive first)
        void Count(BuildMessage^ m)
        {
            if (String::Equals(m->Severity, "error"))
            {
                System::Threading::Interlocked::Increment(errors);
                System::Threading::Monitor::Enter(this);
                try
                {
                    bool placed = !String::IsNullOrEmpty(m->FilePath) && m->Line > 0;
                    bool firstPlaced = firstError != nullptr && !String::IsNullOrEmpty(firstError->FilePath) && firstError->Line > 0;
                    if (firstError == nullptr || (placed && !firstPlaced)) firstError = m;
                    if (placed) placedError = true;
                }
                finally { System::Threading::Monitor::Exit(this); }
            }
            else if (String::Equals(m->Severity, "warning")) System::Threading::Interlocked::Increment(warnings);
        }

        // the two streams of a tool come on two threads: one line at a time through here
        void OnToolLine(String^ line)
        {
            Out(line);
            BuildMessage^ m;
            System::Threading::Monitor::Enter(parser);
            try
            {
                m = parser->ParseIn(line, currentRegex, currentDir, previousLine, currentSource, currentToolName);
                previousLine = line;
            }
            finally { System::Threading::Monitor::Exit(parser); }
            if (m == nullptr) return;
            m->Tool = currentTool;
            Count(m);
            if (message != nullptr) message->Invoke(m);
        }

        // ---------------------------------------------------------------- clean
        bool Clean()
        {
            if (!Directory::Exists(outDir)) { Out("Nothing to clean."); return true; }
            // a CCS project's program goes to the temporary directory (RIDE's ride-ccs-<name>), which Clean may empty too
            bool ccsScratch = project->IsCcs && PathUtil::IsStrictlyUnder(GrideProject::CcsBuildRoot(project->Name), outDir);
            if (!PathUtil::IsStrictlyUnder(projectDir, outDir) && !ccsScratch)
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
            ToolDefinition^ t = id == nullptr ? nullptr : settings->GetTool(id);
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
                AddMessage("error", t->IsBareName()
                           ? String::Format("{0} not found: '{1}' - looked in {2}.", t->DisplayName, t->ExecutablePath, t->LookedIn())
                           : String::Format("{0} not found: '{1}'.", t->DisplayName, t->ExecutablePath), nullptr, 0);
                return nullptr;
            }
            return t;
        }

        // an entry that is there to be used: configured, and found
        ToolDefinition^ Usable(String^ id)
        {
            ToolDefinition^ t = id == nullptr ? nullptr : settings->GetTool(id);
            return t != nullptr && t->IsConfigured() && t->ResolvedPath() != nullptr ? t : nullptr;
        }

        static String^ LookedInPart(ToolDefinition^ t)
        {
            return t->IsBareName() ? String::Format(" (looked in {0})", t->LookedIn()) : String::Empty;
        }

        // the assembler of a step: the vendor's when the native question said so, the CodeView path's for
        // a Debug build of compiled code on a target carrying CodeView (the target's own, with a warning,
        // when it is not there), else the target's
        ToolDefinition^ AssemblerFor(bool codeView)
        {
            if (native && Usable(target->NativeAssemblerId) != nullptr) return settings->GetTool(target->NativeAssemblerId);
            if (codeView && target->DebugAssemblerId != nullptr)
            {
                ToolDefinition^ d = settings->GetTool(target->DebugAssemblerId);
                if (d != nullptr && d->IsConfigured())
                {
                    if (d->ResolvedPath() != nullptr) return d;
                    if (!codeViewWarned)
                    {
                        codeViewWarned = true;
                        AddMessage("warning", String::Format("{0} not found: '{1}' - a Debug build's CodeView goes through it; assembled with {2} instead, so the debugger will see no lines{3}",
                                                             ToolName(d), d->ExecutablePath, ToolName(settings->GetTool(target->AssemblerId)), LookedInPart(d)), nullptr, 0);
                    }
                }
            }
            return RequireTool(target->AssemblerId);
        }

        // the linker of the build: the vendor's when asked, Microsoft's link.exe /DEBUG for a Debug build
        // on a target carrying CodeView (the PDB), else the target's
        ToolDefinition^ LinkerFor(bool codeView)
        {
            if (native && Usable(target->NativeLinkerId) != nullptr) return settings->GetTool(target->NativeLinkerId);
            // on the emulated target TI's lnk6x stands in for a linker that is not there (RIDE's tilinker, TI's bin)
            if (target->Emulated && Usable(target->LinkerId) == nullptr && Usable(target->NativeLinkerId) != nullptr)
            {
                ToolDefinition^ ti = settings->GetTool(target->NativeLinkerId);
                Out(String::Format("[{0} is not there - linking with {1}, {2}]", ToolName(settings->GetTool(target->LinkerId)), FileToolName(ti), ti->ResolvedPath()));
                return ti;
            }
            if (codeView && target->DebugLinkerId != nullptr)
            {
                ToolDefinition^ d = settings->GetTool(target->DebugLinkerId);
                if (d != nullptr && d->IsConfigured())
                {
                    if (d->ResolvedPath() != nullptr) return d;
                    AddMessage("warning", String::Format("Microsoft's {0} not found: '{1}' - a Debug build's PDB needs it; linked with {2} instead, so the debugger will see no lines{3}",
                                                         ToolName(d), d->ExecutablePath, ToolName(settings->GetTool(target->LinkerId)), LookedInPart(d)), nullptr, 0);
                }
            }
            return RequireTool(target->LinkerId);
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

        // the defines, each after the tool's switch as RIDE's options.cpp writes them (quoted()): one holding a space,
        // a tab or a quote is quoted whole, an inner quote escaped, so a CCS project's "GREETING=hi there" reaches the
        // compiler as one argument (-D"GREETING=hi there"); -DNAME and -DNAME=1 as they are
        static String^ DefineArgument(String^ d)
        {
            String^ v = d->Trim();
            if (v->IndexOfAny(gcnew array<wchar_t>{ L' ', L'\t', L'"' }) < 0) return v;
            v = v->Replace("\"", "\\\"");
            if (v->EndsWith("\\")) v = String::Concat(v, "\\");          // a closing quote after a backslash stays a quote
            return String::Concat("\"", v, "\"");
        }

        static String^ JoinDefines(String^ sw, List<String^>^ items)
        {
            if (String::IsNullOrEmpty(sw) || items == nullptr) return String::Empty;
            StringBuilder^ sb = gcnew StringBuilder();
            for each (String^ i in items)
                if (!String::IsNullOrWhiteSpace(i)) sb->Append(sw)->Append(DefineArgument(i))->Append(L' ');
            return sb->ToString()->Trim();
        }

        // the values of the placeholders, under GRIDE's names ({in}, {configflags} ...) and under
        // toolchain.json's ($(Source), $(ConfigFlags) ...); the step's own files are set by the caller
        Dictionary<String^, String^>^ Vars(ToolDefinition^ tool)
        {
            Dictionary<String^, String^>^ d = gcnew Dictionary<String^, String^>();
            ToolEntry^ e = ToolchainFile::Current->Tool(tool->Definition);
            bool debug = IsDebugConfig(config->Name);
            String^ configFlags = ConfigFlags(tool);
            // the project's include folders, then the installation's a compiler is given (RIDE's includeFlags:
            // lib for c90, include for cpp11; none for shalimar, which takes no -I)
            List<String^>^ includeDirs = gcnew List<String^>(project->IncludePaths);
            includeDirs->AddRange(config->IncludePathsHere);
            includeDirs->AddRange(settings->SharedIncludes);
            includeDirs->AddRange(IdeSettingsPaths::InstallIncludeDirs(e));
            // the machine's own compiler (cl) reads Visual Studio's INCLUDE: RIDE's header directories would stand in front of it
            if (IsHostCompiler(tool)) KeepInstallHeadersFrom(includeDirs);
            String^ includes = JoinSwitch(tool->IncludeSwitch, includeDirs, true);
            String^ defines = JoinDefines(tool->DefineSwitch, config->Defines);
            String^ crt = CrtLibraries();
            String^ rt = RuntimePath();
            String^ runtime = rt == nullptr ? String::Empty : rt;
            String^ runtimeDir = rt == nullptr ? String::Empty : Path::GetDirectoryName(rt);
            d["target"] = target->Arch;
            d["arch"] = target->Arch;
            d["config"] = config->Name;
            d["configflags"] = configFlags;
            d["name"] = project->Name;
            d["projectdir"] = projectDir;
            d["outdir"] = outDir;
            d["objdir"] = objDir;
            d["asmdir"] = objDir;
            d["exe"] = exePath;
            d["map"] = Path::ChangeExtension(exePath, ".map");
            d["flags"] = config->Flags;
            d["linkflags"] = config->LinkFlags;
            d["asmflags"] = config->AsmFlags;
            d["includes"] = includes;
            d["defines"] = defines;
            d["crtlibs"] = crt;
            d["args"] = String::Empty;
            d["runtime"] = runtime;
            d["runtimedir"] = runtimeDir;
            d["Target"] = project->Target;
            d["Arch"] = target->Arch;
            d["ArchFlag"] = e == nullptr ? String::Empty : e->ArchFlag->Replace("$(Arch)", target->Arch);
            d["Config"] = debug ? Str("Debug") : Str("Release");
            d["ConfigFlags"] = configFlags;
            d["Name"] = project->Name;
            d["ProjectName"] = project->Name;
            d["ProjectDir"] = projectDir;
            d["WorkspaceDir"] = projectDir;
            d["OutDir"] = outDir;
            d["ObjDir"] = objDir;
            d["Output"] = exePath;
            d["Program"] = exePath;
            d["CompileFlags"] = config->Flags;
            d["LinkFlags"] = config->LinkFlags;
            d["IncludeFlags"] = includes;
            d["DefineFlags"] = defines;
            d["CrtLibs"] = crt;
            d["Arguments"] = String::Empty;
            d["Runtime"] = runtime;
            d["RuntimeDir"] = runtimeDir;
            // set by the step, or given a meaning by a later revision
            for each (String^ later in gcnew array<String^>{ "LibDirFlags", "AsmSyntaxFlag", "LangFlags", "ShalimarRuntimeDir", "ToolDir",
                                                          "Source", "Sources", "SourceDir", "AsmFile", "AsmFiles", "ObjFile", "Objects", "LibFlags", "LinkerCmd" })
                d[later] = String::Empty;
            // the configuration's -O word: a Visual Studio configuration's own (R08), else by its kind
            d["Optimise"] = !String::IsNullOrEmpty(config->Optimise) ? config->Optimise : debug ? Str("O0") : Str("O2");
            return d;
        }

        // the tool's flags for the configuration, as toolchain.json gives them (configFlags, with the
        // target's exception in configFlagsByTarget: shalimar's --debug off the C6747), without -g where
        // the target carries no debug information (emitsDebugInfo)
        String^ ConfigFlags(ToolDefinition^ tool)
        {
            ToolEntry^ e = ToolchainFile::Current->Tool(tool->Definition);
            if (e == nullptr) return String::Empty;
            String^ own;
            if (config->ConfigFlagsByRole != nullptr && config->ConfigFlagsByRole->TryGetValue(e->Role, own)) return own;
            bool debug = IsDebugConfig(config->Name);
            // RIDE's Compiler Options: a compiler listing options takes the project's (the defaults where it set none)
            if (CompilerOptionRules::Governs(e)) return CompilerOptionRules::Flags(optionStore, e, debug ? "Debug" : "Release", target->Entry);
            String^ flags = e->ConfigFlagsFor(debug ? "Debug" : "Release", target->Arch);
            if (!debug || target->EmitsDebugInfo) return flags;
            List<String^>^ kept = gcnew List<String^>();
            for each (String^ f in flags->Split(gcnew array<wchar_t>{ L' ' }, StringSplitOptions::RemoveEmptyEntries))
                if (!String::Equals(f, "-g")) kept->Add(f);
            return String::Join(" ", kept);
        }

        // the compiler's assembler spelling (RIDE's assemblerFlag, the compiler's asmSyntaxFlags):
        // codeview's for a Debug build carrying CodeView, assemblerNamed's for the host target when an
        // assembler is named, else none
        String^ AsmSyntaxFlag(ToolDefinition^ compiler, bool codeView)
        {
            ToolEntry^ e = ToolchainFile::Current->Tool(compiler->Definition);
            if (e == nullptr) return String::Empty;
            if (codeView) return e->AsmSyntaxFlag("codeview");
            ToolDefinition^ as = settings->GetTool(target->AssemblerId);
            return target->Host && as != nullptr && as->IsConfigured() ? e->AsmSyntaxFlag("assemblerNamed") : String::Empty;
        }

        // the C runtime the host link names for the configuration (the target's hostCrt library)
        String^ CrtLibraries()
        {
            LibraryEntry^ l = target->RuntimeLibrary;
            if (l == nullptr || !String::Equals(l->Kind, "hostCrt")) return String::Empty;
            return l->FileFor(IsDebugConfig(config->Name));
        }

        // the runtime library of the configuration: the library's Debug file (rts6xd.lib) beside the found one for a Debug build
        String^ RuntimePath()
        {
            if (target->RuntimeId == nullptr) return nullptr;
            ToolDefinition^ rt = settings->GetTool(target->RuntimeId);
            String^ found = rt != nullptr ? rt->ResolvedPath() : nullptr;
            if (found == nullptr || !IsDebugConfig(config->Name)) return found;
            LibraryEntry^ l = target->RuntimeLibrary;
            String^ debugName = l == nullptr ? String::Empty : l->FileFor(true);
            if (debugName->Length == 0) return found;
            String^ debugOne = Path::Combine(Path::GetDirectoryName(found), debugName);
            return File::Exists(debugOne) ? debugOne : found;
        }

        // ---------------------------------------------------------------- the environment
        // what every tool of the host target runs with: Visual Studio's INCLUDE, LIB and PATH, found
        // without cmd.exe (VendorTools.h), unless the tool has a setup script of its own, which sets them
        List<String^>^ ToolEnvironment(ToolDefinition^ tool)
        {
            List<String^>^ env = gcnew List<String^>();
            if (target->Host && String::IsNullOrWhiteSpace(tool->SetupScript)) env->AddRange(VisualStudio::EnvironmentLines());
            return env;
        }

        // the environment RIDE sets before a compiler runs (prepareFor): each compiler's *_AS and *_LD
        // name the project's assembler and linker - empty for a Debug build carrying CodeView, which clang
        // and link.exe take; the variables are the compilers' own (toolchain.json's environment)
        List<String^>^ CompileEnvironment(ToolDefinition^ compiler, bool codeView)
        {
            List<String^>^ env = ToolEnvironment(compiler);
            if (envAssembler == nullptr)
            {
                ToolDefinition^ as = Usable(target->AssemblerId);
                ToolDefinition^ ld = Usable(target->LinkerId);
                envAssembler = as == nullptr ? String::Empty : as->ResolvedPath();
                envLinker = ld == nullptr ? String::Empty : ld->ResolvedPath();
            }
            ToolEntry^ own = ToolchainFile::Current->Tool(compiler->Definition);
            if (CompilerOptionRules::Governs(own)) env->AddRange(CompilerOptionRules::Environment(optionStore, own, config->Name));
            for each (ToolEntry^ e in ToolchainFile::Current->Tools)
            {
                if (ToolIds::CompilerOfRole(e->Role) == nullptr) continue;
                for each (KeyValuePair<String^, String^> kv in e->Environment)
                {
                    if (kv.Key->EndsWith("_AS")) env->Add(String::Concat(kv.Key, "=", codeView ? String::Empty : envAssembler));
                    else if (kv.Key->EndsWith("_LD")) env->Add(String::Concat(kv.Key, "=", codeView ? String::Empty : envLinker));
                }
            }
            return env;
        }

        int RunTool(ToolDefinition^ tool, Dictionary<String^, String^>^ vars, List<String^>^ env)
        {
            String^ exe = tool->ResolvedPath();
            String^ args = ToolRunner::Expand(tool->Arguments, vars);
            Out(String::Format("> {0} {1}", PathUtil::Quote(exe), args));
            currentTool = tool->DisplayName;
            currentToolName = Path::GetFileNameWithoutExtension(exe);
            currentRegex = parser->Compile(tool->ErrorPattern);      // compiled before output threads start
            currentDir = projectDir;
            String^ in;
            currentSource = vars->TryGetValue("in", in) ? in : nullptr;
            previousLine = nullptr;
            int before = errors;
            int rc = ToolRunner::Run(exe, args, projectDir, gcnew Action<String^>(this, &BuildEngine::OnToolLine), token, tool, env);
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
              ->AppendLine(PathUtil::JoinList(config->Defines))->AppendLine(PathUtil::JoinList(project->IncludePaths))
              ->AppendLine(native ? "the vendor's assembler" : "the project's assembler");
            array<String^>^ ids = gcnew array<String^>{ ToolIds::CompilerC90, ToolIds::CompilerCpp11, ToolIds::CompilerShalimar,
                                                        target->AssemblerId, target->DebugAssemblerId, target->NativeAssemblerId };
            for each (String^ id in ids)
            {
                ToolDefinition^ t = id == nullptr ? nullptr : settings->GetTool(id);
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
              ->AppendLine(String::Join("|", libs->ToArray()))->AppendLine(String::Join("|", cmdFiles->ToArray()))
              ->AppendLine(String::Join("|", config->LibraryDirs->ToArray()));
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
                else if (TargetInfo::IsLinkerCommandFile(ext, target->Kind) && config->LinkerCommandFiles == nullptr && !config->Excludes(full))
                    cmdFiles->Add(full);
            }
            // a configuration that names its own command files (a CCS configuration's, exclusions applied) links with those
            if (config->LinkerCommandFiles != nullptr)
                for each (String^ c in config->LinkerCommandFiles) if (File::Exists(c)) cmdFiles->Add(c);
        }

        // ---------------------------------------------------------------- compile steps
        // Result of compiling one step.
        enum class StepResult { Skipped, UpToDate, Built, Failed, Cancelled };

        // one compile step: a source of its own, or a Shalimar program's sources together (shalimar
        // compiles a program as one command, its assembly and object named after the program)
        ref class Unit
        {
        public:
            Unit() { Sources = gcnew List<String^>(); }
            List<String^>^ Sources;     // full paths; the first one is $(Source)
            String^ CompilerId;         // nullptr: an assembly source, assembled as it is
            String^ Stem;
            String^ AsmFile;            // what the compiler writes (the source itself for an assembly source; cl's listing)
            String^ ObjFile;
            bool WritesObject;          // the compiler writes the object itself (cl): nothing is assembled after it
        };

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

        // the build's compile steps in project order (single: the one file of Compile File, which a
        // Shalimar file takes alone); nullptr with RIDE's refusal when the project's toolchain word names a
        // compiler that does not take a file
        List<Unit^>^ Units(Dictionary<String^, String^>^ stems, String^ single, String^% refusal, String^% refusedFile)
        {
            refusal = nullptr; refusedFile = nullptr;
            List<String^>^ files = gcnew List<String^>();
            if (single != nullptr) files->Add(single);
            else
                for each (String^ rel in project->SourceFiles)
                {
                    String^ src = project->ResolvePath(rel);
                    if (stems->ContainsKey(src) && !files->Contains(src)) files->Add(src);
                }
            Dictionary<String^, String^>^ compilers = gcnew Dictionary<String^, String^>(StringComparer::OrdinalIgnoreCase);
            int shalimarFiles = 0;
            for each (String^ src in files)
            {
                String^ ext = Path::GetExtension(src);
                String^ why;
                String^ compilerId = TargetInfo::CompilerFor(ext, settings, project->Toolchain, why);
                if (compilerId == nullptr && why->Length > 0) { refusal = why; refusedFile = src; return nullptr; }   // RIDE's refusal
                if (compilerId == nullptr && !TargetInfo::IsAssemblySource(ext)) continue;
                if (config->Excludes(src)) continue;                 // excluded in this configuration (a CCS project's sourceEntries)
                if (compilerId != nullptr && target->Emulated && String::Equals(ToolIds::RoleOfCompiler(compilerId), "hostCompiler"))
                {
                    // RIDE's whyNotRun: the machine's own compiler builds for the machine it is on
                    refusal = String::Format("{0} has no {1} target - it is c90's, cpp11's and shalimar's", ToolWords::WordOfTool(compilerId) == "msvc" ? Str("cl") : ToolWords::WordOfTool(compilerId), target->Arch);
                    refusedFile = src;
                    return nullptr;
                }
                compilers[src] = compilerId;
                if (String::Equals(compilerId, ToolIds::CompilerShalimar)) shalimarFiles++;
            }
            List<Unit^>^ units = gcnew List<Unit^>();
            Unit^ program = nullptr;
            for each (String^ src in files)
            {
                if (!compilers->ContainsKey(src)) continue;
                String^ compilerId = compilers[src];
                Unit^ u = gcnew Unit();
                u->CompilerId = compilerId;
                if (shalimarFiles > 1 && String::Equals(compilerId, ToolIds::CompilerShalimar))
                {
                    if (program != nullptr) { program->Sources->Add(src); continue; }
                    program = u;
                    u->Stem = ProgramStem(stems, compilers);
                }
                else u->Stem = stems[src];
                u->Sources->Add(src);
                bool writesObject = compilerId != nullptr && WritesObject(settings->GetTool(compilerId));
                u->AsmFile = compilerId == nullptr ? src : Path::Combine(objDir, String::Concat(u->Stem, writesObject ? Str(".asm") : target->AsmExt));
                u->WritesObject = writesObject;
                u->ObjFile = Path::Combine(objDir, String::Concat(u->Stem, target->ObjExt));
                units->Add(u);
            }
            return units;
        }

        // a Shalimar program's name: the program's, kept apart from the other files' object names
        String^ ProgramStem(Dictionary<String^, String^>^ stems, Dictionary<String^, String^>^ compilers)
        {
            HashSet<String^>^ used = gcnew HashSet<String^>(StringComparer::OrdinalIgnoreCase);
            for each (KeyValuePair<String^, String^> kv in stems)
            {
                String^ c;
                if (!compilers->TryGetValue(kv.Key, c) || !String::Equals(c, ToolIds::CompilerShalimar)) used->Add(kv.Value);
            }
            return MakeStem(Path::GetFileNameWithoutExtension(exePath), used);
        }

        // RIDE's parts: the files one compiler takes go together, the compilers in the order they first appear
        static List<Unit^>^ InParts(List<Unit^>^ units)
        {
            List<String^>^ order = gcnew List<String^>();
            for each (Unit^ u in units)
            {
                String^ k = u->CompilerId == nullptr ? String::Empty : u->CompilerId;
                if (!order->Contains(k)) order->Add(k);
            }
            List<Unit^>^ r = gcnew List<Unit^>();
            for each (String^ k in order)
                for each (Unit^ u in units)
                    if (String::Equals(u->CompilerId == nullptr ? String::Empty : u->CompilerId, k)) r->Add(u);
            return r;
        }

        // compile (if needed) and assemble one step; stopAtAssembly: another machine's target, or the
        // C6747 with emulateOnly - the assembly is what the build makes
        StepResult CompileUnit(Unit^ u, bool force, DateTime newestHeader, bool stopAtAssembly)
        {
            bool isAsm = u->CompilerId == nullptr;
            if (isAsm && stopAtAssembly) return StepResult::Skipped;      // assembly already: nothing here assembles it
            DateTime newestSource = DateTime::MinValue;
            for each (String^ s in u->Sources)
            {
                if (!File::Exists(s))
                {
                    AddMessage("error", String::Format("Source file not found: {0}", s), s, 0);
                    return StepResult::Failed;
                }
                if (TimeOf(s) > newestSource) newestSource = TimeOf(s);
            }
            String^ made = stopAtAssembly && !u->WritesObject ? u->AsmFile : u->ObjFile;
            DateTime madeTime = TimeOf(made);
            if (!force && madeTime != DateTime::MinValue && madeTime >= newestSource && madeTime >= newestHeader)
                return StepResult::UpToDate;

            List<String^>^ names = gcnew List<String^>();
            for each (String^ s in u->Sources) names->Add(Path::GetFileName(s));
            // a Debug build of compiled code on a target carrying CodeView (RIDE's debugsWithCodeView)
            bool codeView = !isAsm && IsDebugConfig(config->Name) && target->CodeView;
            if (!isAsm)
            {
                ToolDefinition^ compiler = RequireTool(u->CompilerId);
                if (compiler == nullptr) return StepResult::Failed;
                // RIDE's "$ group (compiler)" line as each part starts; the project is GRIDE 2.0's one group
                String^ word = ToolWords::WordOfTool(u->CompilerId);
                String^ part = String::Format("{0} ({1})", project->Name, String::Equals(word, ToolWords::Auto) ? ToolName(compiler) : word);
                if (u->WritesObject) part = String::Format("{0} ({1})", project->Name, FileToolName(compiler));     // the compiler's own name (cl)
                if (!String::Equals(part, lastPart)) { Out(String::Concat("$ ", part)); lastPart = part; }
                Out(String::Join(" ", names));
                TryDelete(u->AsmFile);
                if (u->WritesObject) TryDelete(u->ObjFile);
                String^ first = u->Sources[0];
                StringBuilder^ quoted = gcnew StringBuilder();
                for each (String^ s in u->Sources) quoted->Append(PathUtil::Quote(s))->Append(L' ');
                Dictionary<String^, String^>^ v = Vars(compiler);
                v["in"] = first;
                v["out"] = u->AsmFile;
                v["Source"] = first; v["Sources"] = quoted->ToString()->Trim(); v["SourceDir"] = Path::GetDirectoryName(first);
                v["AsmFile"] = u->AsmFile; v["ObjFile"] = u->ObjFile; v["Name"] = u->Stem;
                v["AsmSyntaxFlag"] = AsmSyntaxFlag(compiler, codeView);
                v["LangFlags"] = LanguageFlagsFor(compiler, first);
                if (IsHostCompiler(compiler))
                {
                    // a Visual Studio project's own settings go to cl after the configuration's flags (/W4 /permissive- /EHsc /MT ...)
                    String^ own = ProjectToolFlags(compiler);
                    String^ flags = String::Concat(config->Flags, " ", own)->Trim();
                    v["CompileFlags"] = flags; v["flags"] = flags;
                }
                int rc = RunTool(compiler, v, CompileEnvironment(compiler, codeView));
                if (rc == ToolRunner::ExitCancelled) return StepResult::Cancelled;
                if (rc != 0) { failedStep = "compile"; return StepResult::Failed; }
                if (u->WritesObject)
                {
                    // the compiler wrote the object: nothing is assembled after it
                    if (File::Exists(u->ObjFile)) return StepResult::Built;
                    AddMessage("error", String::Format("{0} did not produce '{1}'.", compiler->DisplayName, u->ObjFile), first, 0);
                    failedStep = "compile";
                    return StepResult::Failed;
                }
                if (!File::Exists(u->AsmFile))
                {
                    AddMessage("error", String::Format("{0} did not produce '{1}'.", compiler->DisplayName, u->AsmFile), first, 0);
                    failedStep = "compile";
                    return StepResult::Failed;
                }
                if (stopAtAssembly) return StepResult::Built;
            }
            else Out(String::Join(" ", names));

            // hand-written assembly is the target assembler's own spelling: the CodeView path is the compilers'
            ToolDefinition^ assembler = AssemblerFor(codeView);
            if (assembler == nullptr) return StepResult::Failed;
            TryDelete(u->ObjFile);
            Dictionary<String^, String^>^ va = Vars(assembler);
            va["in"] = u->AsmFile;
            va["out"] = u->ObjFile;
            va["AsmFile"] = u->AsmFile; va["ObjFile"] = u->ObjFile; va["Name"] = u->Stem;
            int arc = RunTool(assembler, va, ToolEnvironment(assembler));
            if (arc == ToolRunner::ExitCancelled) return StepResult::Cancelled;
            if (arc != 0)
            {
                failedStep = "assemble";
                if (target->Emulated) Out(String::Format("{0} refused the assembly - the emulator would run it, but it is not a TI program", ToolName(assembler)));
                return StepResult::Failed;
            }
            if (!File::Exists(u->ObjFile))
            {
                AddMessage("error", String::Format("{0} did not produce '{1}'.", assembler->DisplayName, u->ObjFile), u->AsmFile, 0);
                failedStep = "assemble";
                return StepResult::Failed;
            }
            return StepResult::Built;
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
            String^ full = Path::GetFullPath(SingleFile);
            if (!stems->ContainsKey(full))
            {
                AddMessage("error", String::Format("{0} is not a source file of this project.", Path::GetFileName(SingleFile)), SingleFile, 0);
                return false;
            }
            String^ why = Unwritable(objDir);
            if (why != nullptr) { AddMessage("error", why, nullptr, 0); return false; }
            String^ refusal;
            String^ refused;
            List<Unit^>^ units = Units(stems, full, refusal, refused);
            if (units == nullptr) { AddMessage("error", refusal, refused, 0); return false; }
            if (units->Count == 0) return false;
            bool stopAtAssembly = target->CrossOnly || (target->Emulated && project->EmulateOnly);
            StepResult r = CompileUnit(units[0], true, DateTime::MinValue, stopAtAssembly);
            if (r == StepResult::Skipped) Out(String::Format("{0} is assembly already - nothing here assembles it", Path::GetFileName(full)));
            return r == StepResult::Built || r == StepResult::Skipped;
        }

        bool DoBuild(bool rebuild)
        {
            // RIDE refuses a build whose program could not be written where it would go, before anything runs
            for each (String^ dir in gcnew array<String^>{ outDir, objDir })
            {
                String^ why = Unwritable(dir);
                if (why != nullptr) { AddMessage("error", why, nullptr, 0); return false; }
            }

            // Full rebuild when compile settings changed since last time (and for the vendor's tools).
            String^ stampPath = Path::Combine(objDir, StampFileName);
            String^ stamp = BuildStamp();
            String^ oldStamp = File::Exists(stampPath) ? File::ReadAllText(stampPath) : String::Empty;
            bool forceAll = rebuild || native || !String::Equals(stamp, oldStamp);
            if (forceAll && !rebuild && !native && oldStamp->Length > 0) Out("Build settings changed - rebuilding all files.");

            // Any change to a project header rebuilds every source file.
            // (Headers found only through include paths are not tracked - use Rebuild.)
            DateTime newestHeader;
            List<String^>^ cmdFiles = gcnew List<String^>();
            ScanProjectFiles(newestHeader, cmdFiles);

            String^ refusal;
            String^ refused;
            List<Unit^>^ units = Units(StemMap(), nullptr, refusal, refused);
            if (units == nullptr) { AddMessage("error", refusal, refused, 0); return false; }
            if (units->Count == 0)
            {
                AddMessage("error", "The project has no source files to build.", nullptr, 0);
                return false;
            }

            bool emulateOnly = target->Emulated && project->EmulateOnly;
            bool stopAtAssembly = target->CrossOnly || emulateOnly;
            bool anyFailed = false;
            bool anyBuilt = false;
            bool shalimarProgram = false;
            int compiledCount = 0;
            lastPart = nullptr;
            for each (Unit^ u in InParts(units))
            {
                if (token.IsCancellationRequested) return false;
                if (String::Equals(u->CompilerId, ToolIds::CompilerShalimar)) shalimarProgram = true;
                StepResult r = CompileUnit(u, forceAll, newestHeader, stopAtAssembly);
                if (r == StepResult::Cancelled) return false;
                if (r == StepResult::Failed) anyFailed = true;
                if (r == StepResult::Built) { anyBuilt = true; compiledCount++; }
            }

            if (anyFailed || errors > 0)
            {
                Out("Link skipped because of earlier errors.");
                return false;
            }
            File::WriteAllText(stampPath, stamp);
            if (compiledCount == 0) Out("All source files are up to date.");

            if (target->CrossOnly)
            {
                // another machine's target only reaches assembly here (RIDE's whyNotRun)
                Out(TargetInfo::WhyNotRun(target->Kind));
                Out(String::Format("[assembly written to {0} - nothing here assembles or links it]", project->RelativePath(objDir)));
                return true;
            }
            if (emulateOnly)
            {
                Out(String::Format("[no {0}: the project says emulateOnly - Run is Emulate on {1}]", target->ExeExt, RoleName("emulator")));
                return true;
            }

            List<String^>^ objs = gcnew List<String^>();
            bool clObjects = false, otherObjects = false;
            for each (Unit^ u in units) { objs->Add(u->ObjFile); if (u->WritesObject) clObjects = true; else otherObjects = true; }
            // a Debug build on a target carrying CodeView links with Microsoft's link.exe /DEBUG (the PDB); so do cl's
            // objects in either configuration (they are Microsoft's, with its CRT)
            ToolDefinition^ linker = clObjects && target->Host ? ClLinker() : LinkerFor(IsDebugConfig(config->Name) && target->CodeView);
            if (linker == nullptr) return false;
            noCrtList = clObjects && !otherObjects && target->Host && linker != settings->GetTool(target->LinkerId);
            if (noCrtList) Out(String::Format("[cl's objects name their own C runtime - {0} takes it from them, no CRT list is added]", FileToolName(linker)));
            return target->Emulated ? TiLink(linker, objs, cmdFiles, shalimarProgram, forceAll || anyBuilt)
                                    : HostLink(linker, objs, shalimarProgram, forceAll || anyBuilt);
        }

        // ---------------------------------------------------------------- cl (R12)
        // a compile template naming $(ObjFile) has the compiler write the object (cl): nothing is assembled after it
        static bool WritesObject(ToolDefinition^ t)
        {
            return t != nullptr && t->Arguments->IndexOf("$(ObjFile)", StringComparison::OrdinalIgnoreCase) >= 0;
        }

        // the machine's own compiler (the role hostCompiler: cl)
        static bool IsHostCompiler(ToolDefinition^ t)
        {
            ToolEntry^ e = t == nullptr ? nullptr : ToolchainFile::Current->Tool(t->Definition);
            return e != nullptr && String::Equals(e->Role, "hostCompiler");
        }

        // the compiler's language switch for the file (languageFlags: cl's /TC, /TP /EHsc /std:c++14)
        static String^ LanguageFlagsFor(ToolDefinition^ t, String^ source)
        {
            ToolEntry^ e = t == nullptr ? nullptr : ToolchainFile::Current->Tool(t->Definition);
            if (e == nullptr || e->LanguageFlags->Count == 0) return String::Empty;
            String^ lang = String::Equals(Path::GetExtension(source), ".c", StringComparison::OrdinalIgnoreCase) ? Str("C") : Str("C++");
            String^ f;
            return e->LanguageFlags->TryGetValue(lang, f) ? f : String::Empty;
        }

        // a Visual Studio project's own cl settings (its configuration's ClCompile values) as the switches cl's
        // projectSettings name, in the table's order, then the file's additional options; named once a build,
        // with the settings the table does not name. Empty for any other project.
        String^ ProjectToolFlags(ToolDefinition^ compiler)
        {
            VsProjectConfig^ c = config->VsConfig;
            ToolEntry^ e = ToolchainFile::Current->Tool(compiler->Definition);
            if (c == nullptr || e == nullptr) return String::Empty;
            List<String^>^ flags = gcnew List<String^>();
            for each (String^ setting in e->ProjectSettingOrder)
            {
                String^ value;
                if (!c->ClSettings->TryGetValue(setting, value)) continue;
                String^ f = e->ProjectSettingFlag(setting, value);
                if (!String::IsNullOrWhiteSpace(f)) flags->Add(f->Trim());
            }
            List<String^>^ unmapped = gcnew List<String^>();
            for each (KeyValuePair<String^, String^> kv in c->ClSettings)
                if (e->ProjectSettingFlag(kv.Key, kv.Value) == nullptr) unmapped->Add(String::Concat(kv.Key, " ", kv.Value));
            if (c->AdditionalOptions->Length > 0) flags->Add(c->AdditionalOptions);
            String^ own = String::Join(" ", flags);
            if (!clSettingsSaid && (own->Length > 0 || unmapped->Count > 0))
            {
                clSettingsSaid = true;
                if (own->Length > 0) Out(String::Format("[the project's own settings give cl: {0}]", own));
                if (unmapped->Count > 0) Out(String::Format("[not given to cl - toolchain.json's projectSettings names no switch for: {0}]", String::Join("; ", unmapped)));
            }
            return own;
        }

        // takes the installation's header directories (RIDE's include and lib, named or found) out of a host
        // compiler's list, and says so once
        void KeepInstallHeadersFrom(List<String^>^ inc)
        {
            List<String^>^ ride = gcnew List<String^>();
            for each (String^ role in gcnew array<String^>{ "cCompiler", "cppCompiler" })
                for each (String^ d in IdeSettingsPaths::InstallIncludeDirs(ToolchainFile::Current->ToolByRole(role))) ride->Add(d);
            List<String^>^ kept = gcnew List<String^>();
            for (int i = inc->Count - 1; i >= 0; i--)
            {
                String^ full;
                try { full = PathUtil::Resolve(projectDir, inc[i]); } catch (Exception^) { continue; }
                bool theirs = false;
                for each (String^ d in ride) if (PathUtil::SamePath(full, d)) theirs = true;
                if (!theirs) continue;
                if (!kept->Contains(full)) kept->Insert(0, full);
                inc->RemoveAt(i);
            }
            if (kept->Count == 0 || rideHeadersSaid) return;
            rideHeadersSaid = true;
            Out(String::Format("[{0} {1} not given to cl: the installation's headers would stand in front of Visual Studio's]", String::Join(" and ", kept), kept->Count == 1 ? Str("is") : Str("are")));
        }

        // the linker of cl's objects: Microsoft's link.exe (the Debug linker's entry, else the native one), else
        // - with a warning - the target's own
        ToolDefinition^ ClLinker()
        {
            ToolDefinition^ d = Usable(target->DebugLinkerId);
            if (d != nullptr) return d;
            d = Usable(target->NativeLinkerId);
            if (d != nullptr) return d;
            AddMessage("warning", String::Format("Microsoft's link.exe not found - cl's objects are linked with {0}", ToolName(settings->GetTool(target->LinkerId))), nullptr, 0);
            return RequireTool(target->LinkerId);
        }

        // the name of the tool the target's roles give a role (the file's name for it)
        String^ RoleName(String^ role)
        {
            ToolEntry^ e = target->Entry == nullptr ? nullptr : ToolchainFile::Current->Tool(target->Entry->ToolIdFor(role));
            return e == nullptr ? role : e->Name;
        }

        // ---------------------------------------------------------------- link
        // the project's libraries: a bare name (kernel32.lib) as it is, a path resolved; on a target with a
        // runtime library (RTS6x) every name a project may give it - libc.a, rts6740*, rts6x.lib ... - is
        // the configuration's file, which is linked when the project names none (RIDE's makeTiProgram)
        List<String^>^ ProjectLibraries(ToolDefinition^ linker, String^ runtime)
        {
            List<String^>^ libs = gcnew List<String^>();
            LibraryEntry^ rl = target->RuntimeLibrary;
            bool runtimeNamed = false;
            List<String^>^ named = gcnew List<String^>(project->Libraries);
            named->AddRange(config->LibrariesHere);
            if (target->Host) named->AddRange(settings->SharedLibraries);      // RIDE's shared libraries, on every host link
            for each (String^ l in named)
            {
                if (String::IsNullOrWhiteSpace(l)) continue;
                String^ lp = l->Trim();
                if (runtime != nullptr && rl != nullptr && (rl->IsAlias(lp) || IsTiRuntimeName(lp))) { lp = runtime; runtimeNamed = true; }
                else if (lp->IndexOf(L'\\') >= 0 || lp->IndexOf(L'/') >= 0 || File::Exists(Path::Combine(projectDir, lp)))
                    lp = PathUtil::Resolve(projectDir, lp);
                if (!libs->Contains(lp)) libs->Add(lp);
            }
            // added once: here, or through the linker's own {runtime}
            bool argsUseRuntime = linker->Arguments->IndexOf("{runtime}", StringComparison::OrdinalIgnoreCase) >= 0 ||
                                  linker->Arguments->IndexOf("$(Runtime)", StringComparison::OrdinalIgnoreCase) >= 0;
            if (runtime != nullptr && !runtimeNamed && !argsUseRuntime) libs->Add(runtime);
            return libs;
        }

        // TI's names for the C runtime (toolchain.json's ccs.tiRuntime: libc.a, rts6740_elf_eh.lib, rts6740_elf.lib)
        static bool IsTiRuntimeName(String^ lib)
        {
            CcsEntry^ cd = ToolchainFile::Current->Ccs;
            String^ leaf = Path::GetFileName(lib);
            for each (String^ n in gcnew array<String^>{ cd->RuntimeIndex, cd->RuntimeLibrary, cd->RuntimeFallback })
                if (!String::IsNullOrEmpty(n) && String::Equals(leaf, n, StringComparison::OrdinalIgnoreCase)) return true;
            return false;
        }

        // the link's inputs and whether it must run (it is up to date when nothing it reads changed)
        bool NeedLink(ToolDefinition^ linker, List<String^>^ objs, List<String^>^ libs, List<String^>^ cmdFiles, bool changed, String^% linkStamp)
        {
            String^ linkStampPath = Path::Combine(objDir, LinkStampFileName);
            linkStamp = LinkStamp(linker, libs, cmdFiles);
            String^ oldLinkStamp = File::Exists(linkStampPath) ? File::ReadAllText(linkStampPath) : String::Empty;
            DateTime exeTime = TimeOf(exePath);
            if (changed || exeTime == DateTime::MinValue || !String::Equals(linkStamp, oldLinkStamp)) return true;
            for each (String^ o in objs) if (TimeOf(o) > exeTime) return true;
            for each (String^ l in libs) if (File::Exists(l) && TimeOf(l) > exeTime) return true;
            for each (String^ c in cmdFiles) if (TimeOf(c) > exeTime) return true;
            Out(String::Format("{0} is up to date.", Path::GetFileName(exePath)));
            return false;
        }

        bool RunLink(ToolDefinition^ linker, List<String^>^ objs, List<String^>^ libs, List<String^>^ cmdFiles, String^ linkStamp)
        {
            StringBuilder^ objList = gcnew StringBuilder();
            for each (String^ o in objs) objList->Append(PathUtil::Quote(o))->Append(L' ');
            StringBuilder^ libList = gcnew StringBuilder();
            for each (String^ l in libs) libList->Append(linker->LibrarySwitch)->Append(PathUtil::Quote(l))->Append(L' ');
            StringBuilder^ cmdList = gcnew StringBuilder();
            for each (String^ c in cmdFiles) cmdList->Append(PathUtil::Quote(c))->Append(L' ');

            String^ linkStampPath = Path::Combine(objDir, LinkStampFileName);
            Directory::CreateDirectory(outDir);
            TryDelete(exePath);
            TryDelete(linkStampPath);
            Dictionary<String^, String^>^ vl = Vars(linker);
            vl["out"] = exePath; vl["Output"] = exePath;
            vl["objs"] = objList->ToString()->Trim(); vl["Objects"] = vl["objs"];
            vl["libs"] = libList->ToString()->Trim(); vl["LibFlags"] = vl["libs"];
            vl["cmdfiles"] = cmdList->ToString()->Trim();
            vl["LinkerCmd"] = cmdFiles->Count > 0 ? cmdFiles[0] : String::Empty;
            vl["LibDirFlags"] = LibraryDirFlags(false);
            if (noCrtList) { vl["CrtLibs"] = String::Empty; vl["crtlibs"] = String::Empty; }
            if (tiRuntimeDir != nullptr) { vl["RuntimeDir"] = tiRuntimeDir; vl["runtimedir"] = tiRuntimeDir; }     // the link's -i "$(RuntimeDir)" is TI's lib then
            if (romModelForTi)
            {
                String^ more = (config->LinkFlags + " --rom_model")->Trim();
                vl["LinkFlags"] = more; vl["linkflags"] = more;
            }
            int lrc = RunTool(linker, vl, ToolEnvironment(linker));
            if (lrc == ToolRunner::ExitCancelled) return false;
            if (lrc != 0) { failedStep = "link"; return false; }
            if (!File::Exists(exePath))
            {
                AddMessage("error", String::Format("{0} did not produce '{1}'.", linker->DisplayName, exePath), nullptr, 0);
                failedStep = "link";
                return false;
            }
            File::WriteAllText(linkStampPath, linkStamp);
            return true;
        }

        // the configuration's library search paths that are on this machine, as the target's libraryDirFlag
        // spells them; one that is not is left out, and said when `say` (RIDE's makeTiProgram)
        String^ LibraryDirFlags(bool say)
        {
            StringBuilder^ sb = gcnew StringBuilder();
            String^ flag = target->Entry == nullptr ? String::Empty : target->Entry->LibraryDirFlag;
            if (tiExtraDir != nullptr) sb->Append(flag)->Append(PathUtil::Quote(tiExtraDir))->Append(L' ');
            for each (String^ d in config->LibraryDirs)
            {
                String^ abs = PathUtil::Resolve(projectDir, d);
                if (!Directory::Exists(abs)) { if (say) Out(String::Format("[library search path not on this machine, left out: {0}]", d)); continue; }
                sb->Append(flag)->Append(PathUtil::Quote(abs))->Append(L' ');
            }
            return sb->ToString()->Trim();
        }

        // the host link (RIDE's linkRecipe): "$ linking with <linker>", then "[built <program>]"; a Shalimar
        // program takes the target's Shalimar runtime library first, the configuration's - the Debug one
        // carries the program's own session (R09: RIDE's shalimar links it itself; here the objects are linked)
        bool HostLink(ToolDefinition^ linker, List<String^>^ objs, bool shalimarProgram, bool changed)
        {
            List<String^>^ libs = ProjectLibraries(linker, nullptr);
            if (shalimarProgram)
            {
                bool debug = IsDebugConfig(config->Name);
                LibraryEntry^ shm = ToolchainFile::Current->LibraryByKind(target->Arch, "shalimarRuntime");
                String^ found = IdeSettingsPaths::FindLibraryFile(shm, debug);
                if (found != nullptr)
                {
                    libs->Insert(0, found);
                    Out(String::Format("[{0} linked first: the Shalimar runtime]", Path::GetFileName(found)));
                }
                else if (shm != nullptr)
                    Out(String::Format("[{0} is not beside GRIDE ({1}) - the link may miss the Shalimar runtime]", shm->FileFor(debug), shm->Directory));
            }
            List<String^>^ none = gcnew List<String^>();
            String^ linkStamp;
            if (!NeedLink(linker, objs, libs, none, changed, linkStamp)) return true;
            LibraryDirFlags(true);
            Out(String::Concat("$ linking with ", ToolName(linker)));
            if (!RunLink(linker, objs, libs, none, linkStamp)) return false;
            Out(String::Format("[built {0}]", exePath));
            return true;
        }

        // the C6747 program (RIDE's makeTiProgram): lnk6x against RTS6x - the configuration's library for
        // every name the project gives it, and alone when it names none - the Shalimar runtime's library
        // before it for a Shalimar program (its assembly copied in and assembled here where no library is
        // packed), the project's linker command files, else RIDE's own written into the object folder
        bool TiLink(ToolDefinition^ linker, List<String^>^ objs, List<String^>^ cmdFiles, bool shalimarProgram, bool changed)
        {
            bool debug = IsDebugConfig(config->Name);
            CcsEntry^ cd = ToolchainFile::Current->Ccs;
            // RTS6x unless TI's own compiler directory is named under Tools (RIDE 5.1): then, or when RTS6x is
            // not here, TI's runtime from the compiler directory's lib (and the second library directory, RIDE's
            // tilib), the exception-handling build where there is one (RIDE's makeTiProgram)
            String^ rt = RuntimePath();
            bool tiNamed = TiCompiler::Named();
            bool rts6x = !tiNamed && rt != nullptr;
            tiRuntimeDir = nullptr; tiExtraDir = nullptr; romModelForTi = false;
            String^ made = String::Format("[{0} TI objects made] no {1}: ", objs->Count, target->ExeExt);
            LibraryEntry^ rl = target->RuntimeLibrary;
            if (!rts6x)
            {
                String^ tiDir = TiCompiler::Dir();
                if (tiDir == nullptr)
                {
                    AddMessage("error", String::Concat(made, String::Format("a {0} build links against {1} in {2} beside GRIDE, or TI's runtime when its compiler directory is named under Tools > Options > RIDE Paths, and neither is here",
                                                                            target->Arch, rl == nullptr ? Str("its runtime") : rl->Name, rl == nullptr ? String::Empty : rl->Directory)), nullptr, 0);
                    return false;
                }
                tiRuntimeDir = TiCompiler::Lib();
                String^ extra = IdeSettingsPaths::TiLibDir;
                if (!String::IsNullOrWhiteSpace(extra))
                {
                    String^ x = System::Environment::ExpandEnvironmentVariables(extra->Trim()->Trim(L'"'));
                    if (Directory::Exists(x)) tiExtraDir = Path::GetFullPath(x);
                }
                bool eh = File::Exists(Path::Combine(tiRuntimeDir, cd->RuntimeLibrary)) || (tiExtraDir != nullptr && File::Exists(Path::Combine(tiExtraDir, cd->RuntimeLibrary)));
                if (!eh && !tiNamed)
                {
                    AddMessage("error", String::Concat(made, String::Format("{0} has only {1} - these objects need {2}, named under Tools > Options > RIDE Paths (TI library directory)", tiDir, cd->RuntimeFallback, cd->RuntimeLibrary)), nullptr, 0);
                    return false;
                }
                rt = eh ? cd->RuntimeLibrary : cd->RuntimeFallback;          // found through -i: TI's lib, the second directory
            }
            List<String^>^ libs = ProjectLibraries(linker, rt);
            if (shalimarProgram)
            {
                LibraryEntry^ shm = ToolchainFile::Current->LibraryByKind(target->Arch, "shalimarRuntime");
                String^ packedName = shm == nullptr ? String::Empty : shm->FileFor(debug);
                String^ packed = packedName->Length > 0 && rts6x ? Path::Combine(Path::GetDirectoryName(rt), packedName) : nullptr;
                if (packed != nullptr && File::Exists(packed)) { libs->Remove(packed); libs->Insert(0, packed); }
                else if (shm != nullptr && !AssembleShalimarRuntime(shm, debug, objs)) return false;
            }
            // the linker command file: the project's, else the one toolchain.json writes (RIDE's ti-link.cmd)
            bool ownCmd = cmdFiles->Count > 0;
            if (!ownCmd && target->Entry != nullptr)
            {
                LinkerCommandFileEntry^ f = ToolchainFile::Current->LinkerCommandFile(target->Entry->LinkerCommandFile);
                if (f != nullptr)
                {
                    String^ path = Path::Combine(objDir, f->Name);
                    String^ text = f->Text();
                    if (!File::Exists(path) || !String::Equals(File::ReadAllText(path), text)) File::WriteAllText(path, text, gcnew UTF8Encoding(false));
                    cmdFiles->Add(path);
                }
            }
            // TI's linker takes c_int00 from the library only under --rom_model, the model CCS defaults to
            bool tiLinker = target->NativeLinkerId != nullptr && linker == settings->GetTool(target->NativeLinkerId);
            romModelForTi = !rts6x && tiLinker && !ownCmd && config->LinkFlags->IndexOf("_model") < 0;
            String^ linkStamp;
            if (!NeedLink(linker, objs, libs, cmdFiles, changed, linkStamp)) return true;
            LibraryDirFlags(true);
            // "$ lnk6x N objects, shmrt6xd.lib, rts6xd.lib, c6747.cmd -o prog.out"
            List<String^>^ shown = gcnew List<String^>();
            for each (String^ l in libs) shown->Add(Path::GetFileName(l));
            if (ownCmd) for each (String^ c in cmdFiles) shown->Add(Path::GetFileName(c));
            String^ name = ToolName(linker);
            Out(String::Format("$ {0} {1} objects{2} -o {3}", name, objs->Count, shown->Count > 0 ? String::Concat(", ", String::Join(", ", shown)) : String::Empty, Path::GetFileName(exePath)));
            if (!RunLink(linker, objs, libs, cmdFiles, linkStamp))
            {
                if (!token.IsCancellationRequested) Out(String::Concat(name, " did not link it - see its messages above"));
                return false;
            }
            Out(String::Format("[linked {0}]", exePath));
            return true;
        }

        // a Shalimar program on the C6747 with no packed runtime library: the runtime's assembly (its
        // variable, else lib\shmrt-tms6747[-debug] under the installation) copied into obj\shmrt and
        // assembled there, its objects linked with the program's
        bool AssembleShalimarRuntime(LibraryEntry^ shm, bool debug, List<String^>^ objs)
        {
            String^ theirs = IdeSettingsPaths::FindShalimarRuntimeAsm(shm, debug);
            if (theirs == nullptr)
            {
                AddMessage("warning", String::Format("the Shalimar runtime is not here - no {0} beside {1}, no {2} under the installation: the link will miss it",
                                                     shm->FileFor(debug), target->RuntimeLibrary == nullptr ? Str("the runtime") : target->RuntimeLibrary->Name, shm->AssemblyDirectoryFor(debug)), nullptr, 0);
                return true;
            }
            String^ into = Path::Combine(objDir, "shmrt");
            Directory::CreateDirectory(into);
            ToolDefinition^ assembler = AssemblerFor(false);
            if (assembler == nullptr) return false;
            array<String^>^ files = Directory::GetFiles(theirs, String::Concat("*", target->AsmExt));
            Array::Sort<String^>(files, StringComparer::OrdinalIgnoreCase);
            for each (String^ s in files)
            {
                String^ copy = Path::Combine(into, Path::GetFileName(s));
                String^ obj = Path::ChangeExtension(copy, target->ObjExt);
                if (TimeOf(copy) != TimeOf(s)) File::Copy(s, copy, true);
                if (TimeOf(obj) < TimeOf(copy))
                {
                    Dictionary<String^, String^>^ va = Vars(assembler);
                    va["in"] = copy; va["out"] = obj;
                    va["AsmFile"] = copy; va["ObjFile"] = obj; va["Name"] = Path::GetFileNameWithoutExtension(copy);
                    int rc = RunTool(assembler, va, ToolEnvironment(assembler));
                    if (rc != 0 || !File::Exists(obj))
                    {
                        if (rc != ToolRunner::ExitCancelled) Out(String::Format("cannot assemble the Shalimar runtime's {0}", Path::GetFileName(s)));
                        return false;
                    }
                }
                objs->Add(obj);
            }
            return true;
        }

        // ---------------------------------------------------------------- the native question
        // RIDE's nativeFallbackWanted: a failed build the vendor's tools might make - the project's own
        // assembler or linker failed, the compilers named no fault in the source, nothing in the output
        // names an unresolved or duplicate symbol, and the settings say to ask (askNative). Yes builds
        // again with the target's nativeAssembler / nativeLinker; no, or nobody to ask, and the build stands.
        bool NativeFallback(bool rebuild)
        {
            if (!settings->AskNativeTools || native) return false;
            bool emulated = target->Emulated;
            if (!String::Equals(failedStep, "link") && (emulated || !String::Equals(failedStep, "assemble"))) return false;
            if (placedError) return false;
            for each (String^ fault in gcnew array<String^>{ "unresolved external", "undefined symbol", "no symbol index", "LNK2001", "LNK2019", "LNK1120",
                                                             "LNK2005", "already defined", "multiply defined", "redefined" })
                if (OutputHas(fault)) return false;
            ToolDefinition^ nas = emulated || target->NativeAssemblerId == nullptr ? nullptr : settings->GetTool(target->NativeAssemblerId);
            ToolDefinition^ nld = target->NativeLinkerId == nullptr ? nullptr : settings->GetTool(target->NativeLinkerId);
            ToolDefinition^ ownAs = settings->GetTool(target->AssemblerId);
            ToolDefinition^ ownLd = settings->GetTool(target->LinkerId);
            bool as = nas != nullptr && nas->IsConfigured() && ownAs != nullptr && ownAs->IsConfigured();
            bool ld = nld != nullptr && nld->IsConfigured() && ownLd != nullptr && ownLd->IsConfigured();
            if (!as && !ld) return false;
            String^ ours = as && ld ? String::Concat(ToolName(ownAs), " and ", ToolName(ownLd)) : as ? ToolName(ownAs) : ToolName(ownLd);
            String^ nasName = as ? FileToolName(nas) : String::Empty;
            String^ nldName = ld ? FileToolName(nld) : String::Empty;
            String^ theirs = emulated ? nldName
                           : as && ld ? String::Concat("Visual Studio's ", nasName, " and ", nldName)
                           : as ? String::Concat("Visual Studio's ", nasName) : String::Concat("Microsoft's ", nldName);
            bool available = (!as || nas->ResolvedPath() != nullptr) && (!ld || nld->ResolvedPath() != nullptr);
            if (!available)
            {
                Out(String::Concat("The project's own ", ours, " did not build it, and ", theirs,
                                   emulated ? " is not on this machine - Tools names TI's C6000 compiler directory" : " are not on this machine - no Visual Studio was found"));
                return false;
            }
            String^ question = String::Concat("The project's own ", ours, " did not build it. Use ", theirs, " for this build instead?");
            if (Asker == nullptr)
            {
                Out(String::Concat(question, " (nothing here can ask - the build stands)"));
                return false;
            }
            if (!Asker->Invoke(question)) return false;
            Out(String::Concat("building again with ", theirs, " in place of the project's ", ours, ", as asked"));
            // the retry's outcome is the build's: the first attempt's errors stand in the output above
            errors = 0; firstError = nullptr; placedError = false; failedStep = String::Empty;
            native = true;
            bool again = DoBuild(rebuild) && errors == 0;
            Out(again ? String::Concat("[built with ", theirs, ", not GRIDE's ", ours, "]") : String::Concat("[not built: ", theirs, " failed as well]"));
            return again;
        }

        // the file's name for the tool an entry follows (ml64, link.exe, TI's lnk6x), else the program's
        static String^ FileToolName(ToolDefinition^ t)
        {
            ToolEntry^ e = ToolchainFile::Current->Tool(t->Definition);
            return e != nullptr ? e->Name : ToolName(t);
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
