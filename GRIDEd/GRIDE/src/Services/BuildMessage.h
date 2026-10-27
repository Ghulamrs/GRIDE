// =============================================================================
//  BuildMessage.h - one error / warning line, and the parser that recognises
//  the usual compiler formats in tool output:
//     kalman.c(4): warning W102: 'Q' could be const          (MSVC / GRIDE style)
//     kalman.c(4,9): error E10: ...
//     "kalman.c", line 4: error #20: identifier "x" is undefined   (TI style)
//     kalman.c:4:9: error: ...                                 (GNU style)
//     "main.asm", ERROR!   at line 5: [E0002] ...              (TI asm6x)
//     LINK : fatal error LNK1104: cannot open file 'x.lib'     (LINK)
//  plus an optional per-toolchain pattern (Tools > Options > Toolchains).
// =============================================================================
#pragma once
#include "../pch.h"

namespace Gride { namespace Services {

    public enum class Severity { Error, Warning, Info };

    public ref class BuildMessage
    {
    public:
        BuildMessage()
        {
            Sev = Severity::Info; Line = 0; Column = 0;
            File = String::Empty; Code = String::Empty; Text = String::Empty; Project = String::Empty;
        }
        property Severity Sev;
        property String^ File;
        property int Line;
        property int Column;
        property String^ Code;
        property String^ Text;
        property String^ Project;

        virtual String^ ToString() override
        {
            Severity s = Sev;
            return String::Format("{0}({1}): {2} {3}: {4}", Path::GetFileName(File), Line, s.ToString()->ToLower(), Code, Text);
        }
    };

    public delegate void TextLineHandler(String^ line);
    public delegate void BuildMessageHandler(BuildMessage^ m);

    // Called from the tool output threads (two at a time: stdout and stderr), so
    // the built-in patterns are created by the type initializer and the cache of
    // custom patterns is locked. Matching one Regex from several threads is safe.
    public ref class BuildMessageParser abstract sealed
    {
    public:
        // workDir is used to make relative file names absolute
        static BuildMessage^ TryParse(String^ line, String^ workDir) { return TryParse(line, workDir, nullptr); }

        // custom (may be null) is tried before the built-in formats
        static BuildMessage^ TryParse(String^ line, String^ workDir, Regex^ custom)
        {
            if (String::IsNullOrEmpty(line)) return nullptr;
            Match^ m = nullptr;
            if (custom != nullptr)
            {
                Match^ c = custom->Match(line);
                if (c->Success) m = c;
            }
            if (m == nullptr)
                for each (Regex^ r in builtIn)
                {
                    Match^ t = r->Match(line);
                    if (t->Success) { m = t; break; }
                }
            if (m == nullptr) return nullptr;

            BuildMessage^ b = gcnew BuildMessage();
            String^ file = m->Groups["file"]->Success ? m->Groups["file"]->Value->Trim() : String::Empty;
            // "LINK : fatal error LNK1104 ..." names a tool, not a file
            if (file->Length > 0 && !Path::HasExtension(file) && file->IndexOfAny(gcnew array<wchar_t>{ L'\\', L'/' }) < 0) file = String::Empty;
            try
            {
                if (file->Length > 0 && !Path::IsPathRooted(file) && !String::IsNullOrEmpty(workDir)) file = Path::GetFullPath(Path::Combine(workDir, file));
            }
            catch (Exception^) { }
            b->File = file;
            int lineNo = 0, col = 0;
            if (Int32::TryParse(m->Groups["line"]->Value, lineNo)) b->Line = lineNo;
            if (Int32::TryParse(m->Groups["col"]->Value, col)) b->Column = col;
            String^ sev = "error";
            if (m->Groups["sev"]->Success && m->Groups["sev"]->Value->Length > 0) sev = m->Groups["sev"]->Value->ToLowerInvariant();
            if (sev->StartsWith("err") || sev == "fatal error") b->Sev = Severity::Error;
            else if (sev->StartsWith("warn")) b->Sev = Severity::Warning;
            else b->Sev = Severity::Info;
            b->Code = m->Groups["code"]->Value->Trim();
            b->Text = (m->Groups["msg"]->Success ? m->Groups["msg"]->Value : line)->Trim();
            return b;
        }

        // shalimar's one diagnostic: "Error: line N: message", on stdout, naming no file - the file is
        // the source being compiled (RIDE compile.cpp parseShalimar)
        static BuildMessage^ TryParseShalimar(String^ line, String^ source)
        {
            if (String::IsNullOrEmpty(line)) return nullptr;
            Match^ m = shalimar->Match(line);
            if (!m->Success) return nullptr;
            BuildMessage^ b = gcnew BuildMessage();
            b->Sev = Severity::Error;
            b->File = source == nullptr ? String::Empty : source;
            b->Line = Int32::Parse(m->Groups["line"]->Value);
            b->Column = 1;
            b->Text = m->Groups["msg"]->Value->Trim();
            return b;
        }

        // c90's preprocessor writes two lines: "file:line: <the directive>" and a caret line
        // "        ^ message"; the column is where the caret points past the "file:line: " prefix
        // (RIDE compile.cpp parseCc1Preprocessor). A caret with nothing after it is somebody underlining.
        static BuildMessage^ TryParseCaret(String^ previous, String^ line, String^ workDir)
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
            b->Sev = Severity::Error;
            b->File = Absolute(where->Substring(0, lineAt), workDir);
            b->Line = lineNo;
            int prefix = after + 2;
            b->Column = caret >= prefix ? caret - prefix + 1 : 1;
            b->Text = message;
            return b;
        }

        // a line naming a place with no severity word - "file:line[:col]:" as the compilers write it,
        // "file(line[,col]):" as cl and link do - for Enter or a double-click on the Output pane
        // (RIDE's whereIs_ in winforms/MainForm.h); null when the line names none
        static BuildMessage^ TryParseLocation(String^ line, String^ workDir)
        {
            if (String::IsNullOrEmpty(line)) return nullptr;
            Match^ m = location->Match(line);
            if (!m->Success) return nullptr;
            String^ file = m->Groups["file"]->Value->Trim();
            if (file->Length == 0) return nullptr;
            BuildMessage^ b = gcnew BuildMessage();
            b->Sev = Severity::Info;
            b->File = Absolute(file, workDir);
            b->Line = Int32::Parse(m->Groups["line"]->Value);
            b->Column = m->Groups["col"]->Success ? Int32::Parse(m->Groups["col"]->Value) : 1;
            b->Text = line->Trim();
            return b;
        }

        // a compiler announcing itself, naming the other files it compiled, or saying it is done: output, never a message
        static bool IsBanner(String^ line)
        {
            if (String::IsNullOrEmpty(line)) return false;
            String^ t = line->Trim();
            return t->Contains("G. R. Akhtar - ") || banner->IsMatch(t);
        }

        // a toolchain's custom error pattern; null when empty or not a valid regex
        static Regex^ Compile(String^ pattern)
        {
            if (String::IsNullOrWhiteSpace(pattern)) return nullptr;
            System::Threading::Monitor::Enter(custom);
            try
            {
                Regex^ r = nullptr;
                if (custom->TryGetValue(pattern, r)) return r;
                try { r = gcnew Regex(pattern, RegexOptions::IgnoreCase | RegexOptions::CultureInvariant); }
                catch (ArgumentException^) { r = nullptr; }
                custom[pattern] = r;
                return r;
            }
            finally { System::Threading::Monitor::Exit(custom); }
        }

    private:
        static String^ Absolute(String^ file, String^ workDir)
        {
            try
            {
                if (file->Length > 0 && !Path::IsPathRooted(file) && !String::IsNullOrEmpty(workDir)) return Path::GetFullPath(Path::Combine(workDir, file));
            }
            catch (Exception^) { }
            return file;
        }

        static initonly Dictionary<String^, Regex^>^ custom = gcnew Dictionary<String^, Regex^>();
        static initonly Regex^ shalimar = gcnew Regex("^Error: line (?<line>\\d+):\\s*(?<msg>.*)$", RegexOptions::Compiled);
        static initonly Regex^ location = gcnew Regex(
            "^\\s*(?<file>[^:(]*(?::[\\\\/][^:(]*)?)(?::(?<line>\\d+)(?::(?<col>\\d+))?:|\\((?<line>\\d+)(?:,(?<col>\\d+))?\\)\\s*:)",
            RegexOptions::Compiled);
        // and shalimar naming the program's other files it compiled ("shalimar: also compiled linalg.shl"): output too
        static initonly Regex^ banner = gcnew Regex("^(c90|cpp11|shalimar|shc|cc1|cxx1): (compilation completed successfully|also compiled )", RegexOptions::IgnoreCase | RegexOptions::Compiled);
        static initonly array<Regex^>^ builtIn = gcnew array<Regex^>{
            // MSVC / MASM / GRIDE:  kalman.c(4,9): error E10: ...
            gcnew Regex("^\\s*(?<file>[^(\"]+?)\\((?<line>\\d+)(,(?<col>\\d+))?\\)\\s*:\\s*(?<sev>fatal error|error|warning|remark|note)\\s*(?<code>[A-Za-z#]*\\d*)\\s*:?\\s*(?<msg>.*)$",
                        RegexOptions::IgnoreCase | RegexOptions::Compiled),
            // TI asm6x:  "main.asm", ERROR!   at line 5: [E0002] Illegal mnemonic specified
            gcnew Regex("^\\s*\"(?<file>[^\"]+)\",\\s*(?<sev>ERROR|WARNING|REMARK)!\\s+at\\s+(?:EOF|line\\s+(?<line>\\d+))\\s*:\\s*(?:\\[(?<code>\\w+)\\])?\\s*(?<msg>.*)$",
                        RegexOptions::IgnoreCase | RegexOptions::Compiled),
            // TI:  "kalman.c", line 4: error #20: identifier "x" is undefined
            gcnew Regex("^\\s*\"(?<file>[^\"]+)\",\\s*line\\s+(?<line>\\d+)(:\\s*|\\s+)(?<sev>fatal error|error|warning|remark)\\s*(?<code>#?\\w*)\\s*:?\\s*(?<msg>.*)$",
                        RegexOptions::IgnoreCase | RegexOptions::Compiled),
            // GNU:  kalman.c:4:9: error: ...
            gcnew Regex("^\\s*(?<file>(?:[A-Za-z]:)?[^:]+):(?<line>\\d+):((?<col>\\d+):)?\\s*(?<sev>fatal error|error|warning|note)\\s*:?\\s*(?<code>)(?<msg>.*)$",
                        RegexOptions::IgnoreCase | RegexOptions::Compiled),
            // LINK : fatal error LNK1104: cannot open file 'x.lib'
            gcnew Regex("^\\s*(?<file>(?:[A-Za-z]:)?[^:]*?)\\s*:\\s*(?<sev>fatal error|error|warning)\\s+(?<code>[A-Za-z]+\\d+)\\s*:\\s*(?<msg>.*)$",
                        RegexOptions::IgnoreCase | RegexOptions::Compiled),
            // RIDE's masm:  bad.s: line 3: unknown instruction 'BOGUS'
            gcnew Regex("^\\s*(?<file>(?:[A-Za-z]:)?[^:]+\\.(?:s|asm|S)):\\s+line\\s+(?<line>\\d+):\\s*(?<sev>)(?<code>)(?<msg>.*)$",
                        RegexOptions::Compiled)
        };
    };

    // Reads one tool's output line by line, as RIDE's parseDiagnostic reads a build's output: the
    // toolchain's own pattern first, then the built-in shapes, shalimar's "Error: line N:" (filed under
    // the source being compiled), and c90's two-line preprocessor diagnostic, for which the previous
    // line is kept. First keeps the first error, which is where RIDE puts the caret: c90 stops at
    // its first error anyway. One reader per tool run; the lines come from the tool's two streams.
    // Beyond RIDE, for the Error List: a line the tool writes about itself ("c90.exe: -g asks where
    // each line of C went...", "link: libcmt.lib: cannot open") is an error without a place.
    public ref class DiagnosticReader
    {
    public:
        DiagnosticReader(String^ workDir, Regex^ custom, String^ source, String^ toolName)
        {
            this->workDir = workDir; this->custom = custom; this->source = source;
            this->toolName = toolName == nullptr ? String::Empty : toolName->ToLowerInvariant();
            previous = nullptr; First = nullptr;
        }

        property BuildMessage^ First;
        property String^ Source { String^ get() { return source; } }

        BuildMessage^ Feed(String^ line)
        {
            BuildMessage^ m = nullptr;
            if (!BuildMessageParser::IsBanner(line))
            {
                m = BuildMessageParser::TryParse(line, workDir, custom);
                if (m == nullptr) m = BuildMessageParser::TryParseShalimar(line, source);
                if (m == nullptr) m = BuildMessageParser::TryParseCaret(previous, line, workDir);
                if (m == nullptr) m = OwnLine(line);
            }
            previous = line;
            if (m != nullptr && m->Sev == Severity::Error && First == nullptr) First = m;
            return m;
        }

    private:
        String^ workDir;
        Regex^ custom;
        String^ source;
        String^ toolName;
        String^ previous;

        // "<tool>: message" or "<path>\<tool>.exe: message"
        BuildMessage^ OwnLine(String^ line)
        {
            if (toolName->Length == 0 || String::IsNullOrEmpty(line)) return nullptr;
            int at = line->IndexOf(": ");
            if (at <= 0) return nullptr;
            String^ who = Path::GetFileName(line->Substring(0, at)->Trim())->ToLowerInvariant();
            if (who != toolName && who != toolName + ".exe") return nullptr;
            String^ text = line->Substring(at + 2)->Trim();
            if (text->Length == 0) return nullptr;
            BuildMessage^ b = gcnew BuildMessage();
            b->Sev = Severity::Error;
            b->File = String::Empty;
            b->Text = text;
            return b;
        }
    };
}}
