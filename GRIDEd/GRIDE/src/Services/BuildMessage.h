// =============================================================================
//  BuildMessage.h - one error / warning line, and the parser that recognises
//  the usual compiler formats in tool output:
//     kalman.c(4): warning W102: 'Q' could be const          (MSVC / GRIDE style)
//     kalman.c(4,9): error E10: ...
//     "kalman.c", line 4: error #20: identifier "x" is undefined   (TI style)
//     kalman.c:4:9: error: ...                                 (GNU style)
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

    public ref class BuildMessageParser abstract sealed
    {
    public:
        // workDir is used to make relative file names absolute
        static BuildMessage^ TryParse(String^ line, String^ workDir)
        {
            if (String::IsNullOrEmpty(line)) return nullptr;
            Match^ m = MsStyle()->Match(line);
            if (!m->Success) m = TiStyle()->Match(line);
            if (!m->Success) m = GnuStyle()->Match(line);
            if (!m->Success) return nullptr;

            BuildMessage^ b = gcnew BuildMessage();
            String^ file = m->Groups["file"]->Value->Trim();
            try
            {
                if (!Path::IsPathRooted(file) && !String::IsNullOrEmpty(workDir)) file = Path::GetFullPath(Path::Combine(workDir, file));
            }
            catch (Exception^) { }
            b->File = file;
            int lineNo = 0, col = 0;
            if (Int32::TryParse(m->Groups["line"]->Value, lineNo)) b->Line = lineNo;
            if (Int32::TryParse(m->Groups["col"]->Value, col)) b->Column = col;
            String^ sev = m->Groups["sev"]->Value->ToLowerInvariant();
            if (sev->StartsWith("err") || sev == "fatal error") b->Sev = Severity::Error;
            else if (sev->StartsWith("warn")) b->Sev = Severity::Warning;
            else b->Sev = Severity::Info;
            b->Code = m->Groups["code"]->Value->Trim();
            b->Text = m->Groups["msg"]->Value->Trim();
            return b;
        }

    private:
        static Regex^ msStyle = nullptr;
        static Regex^ tiStyle = nullptr;
        static Regex^ gnuStyle = nullptr;

        static Regex^ MsStyle()
        {
            if (msStyle == nullptr)
                msStyle = gcnew Regex("^\\s*(?<file>[^(\"]+?)\\((?<line>\\d+)(,(?<col>\\d+))?\\)\\s*:\\s*(?<sev>fatal error|error|warning|remark|note)\\s*(?<code>[A-Za-z#]*\\d*)\\s*:?\\s*(?<msg>.*)$",
                                      RegexOptions::IgnoreCase | RegexOptions::Compiled);
            return msStyle;
        }
        static Regex^ TiStyle()
        {
            if (tiStyle == nullptr)
                tiStyle = gcnew Regex("^\\s*\"(?<file>[^\"]+)\",\\s*line\\s+(?<line>\\d+)(:\\s*|\\s+)(?<sev>fatal error|error|warning|remark)\\s*(?<code>#?\\w*)\\s*:?\\s*(?<msg>.*)$",
                                      RegexOptions::IgnoreCase | RegexOptions::Compiled);
            return tiStyle;
        }
        static Regex^ GnuStyle()
        {
            if (gnuStyle == nullptr)
                gnuStyle = gcnew Regex("^\\s*(?<file>(?:[A-Za-z]:)?[^:]+):(?<line>\\d+):((?<col>\\d+):)?\\s*(?<sev>fatal error|error|warning|note)\\s*:?\\s*(?<code>)(?<msg>.*)$",
                                       RegexOptions::IgnoreCase | RegexOptions::Compiled);
            return gnuStyle;
        }
    };
}}
