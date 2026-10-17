// =============================================================================
//  ToolVersions.h - Help > About: the IDE's own version and each toolchain
//  program's, as the program reports it itself (--version, else its banner
//  with no arguments). Nothing is written down here but the IDE's own 1.0,
//  which a program that reports no version of its own goes with.
//  The same file is in GRIDEd and GRIDE 2.0, so both About boxes read alike.
// =============================================================================
#pragma once

namespace GrideAbout {

    using namespace System;
    using namespace System::IO;
    using namespace System::Text;
    using namespace System::Text::RegularExpressions;
    using namespace System::Collections::Generic;

    public ref class ToolVersion
    {
    public:
        String^ Name;
        String^ Version;        // "1.3", or empty when the program does not say
        String^ Description;    // what the program calls itself, the author and version taken out
    };

    public ref class ToolVersions abstract sealed
    {
    public:
        literal String^ IdeVersion = "1.0";
        literal String^ Author = L"\x00A9" L"2026 G. R. Akhtar";   // the copyright sign by its code, whatever the file's encoding

        static array<String^>^ Names()
        {
            return gcnew array<String^>{ "c90", "cpp11", "shalimar", "c2s", "asm6x", "lnk6x", "sim6747", "vm6747", "masm", "link" };
        }

        // the program beside the IDE asked for its version: --version first, then no arguments (c90 says it there)
        static ToolVersion^ Ask(String^ dir, String^ name)
        {
            ToolVersion^ v = gcnew ToolVersion();
            v->Name = name; v->Version = String::Empty; v->Description = String::Empty;
            String^ exe = Path::Combine(dir, name + ".exe");
            if (!File::Exists(exe)) { v->Description = "not found beside GRIDE.exe"; return v; }
            for each (String^ args in gcnew array<String^>{ "--version", String::Empty })
            {
                array<String^>^ lines = Run(exe, args)->Split(gcnew array<wchar_t>{ '\n' }, StringSplitOptions::RemoveEmptyEntries);
                for each (String^ raw in lines)
                {
                    String^ line = Own(raw->Trim());
                    if (line->Length == 0 || IsComplaint(line)) continue;
                    Match^ m = Regex::Match(line, "(?<![\\w.])(\\d+\\.\\d+(?:\\.\\d+)?)(?![\\w.])");
                    if (v->Version->Length == 0 && m->Success) v->Version = m->Groups[1]->Value;
                    if (v->Description->Length == 0 && !line->StartsWith("Version", StringComparison::OrdinalIgnoreCase))
                        v->Description = Tidy(name, m->Success ? line->Remove(m->Index, m->Length) : line);
                }
                if (v->Version->Length > 0) break;
            }
            // a program that does not say its version goes with the IDE's (masm, link)
            if (v->Version->Length == 0)
            {
                v->Version = IdeVersion;
                v->Description = name == "masm" ? "x64 assembler - GRIDE's version"
                               : name == "link" ? "x64 linker - GRIDE's version"
                               : "GRIDE's version";
            }
            return v;
        }

        // the whole About text below the title: version, author, the toolchain table, the framework
        static String^ Text(String^ dir, String^ settingsPath)
        {
            StringBuilder^ sb = gcnew StringBuilder();
            sb->AppendLine("Version " + IdeVersion);
            sb->AppendLine(Author);
            sb->AppendLine();
            sb->AppendLine("C90, C++11 and Shalimar for Windows and the TMS320C6747.");
            sb->AppendLine();
            sb->AppendLine("Toolchain - as each program reports itself");
            for each (String^ n in Names())
            {
                ToolVersion^ v = Ask(dir, n);
                sb->AppendLine(String::Format("  {0,-9} {1,-6} {2}", v->Name, v->Version->Length > 0 ? v->Version : "-", v->Description));
            }
            sb->AppendLine();
            sb->AppendLine(System::Runtime::InteropServices::RuntimeInformation::FrameworkDescription);
            if (!String::IsNullOrEmpty(settingsPath)) sb->AppendLine("Settings: " + settingsPath);
            return sb->ToString()->TrimEnd();
        }

    private:
        static String^ Run(String^ exe, String^ args)
        {
            try
            {
                System::Diagnostics::ProcessStartInfo^ si = gcnew System::Diagnostics::ProcessStartInfo(exe, args);
                si->UseShellExecute = false; si->CreateNoWindow = true;
                si->RedirectStandardOutput = true; si->RedirectStandardError = true; si->RedirectStandardInput = true;
                si->StandardOutputEncoding = Encoding::UTF8; si->StandardErrorEncoding = Encoding::UTF8;
                System::Diagnostics::Process^ p = System::Diagnostics::Process::Start(si);
                p->StandardInput->Close();
                System::Threading::Tasks::Task<String^>^ out = p->StandardOutput->ReadToEndAsync();
                System::Threading::Tasks::Task<String^>^ err = p->StandardError->ReadToEndAsync();
                if (!p->WaitForExit(3000)) { try { p->Kill(); } catch (Exception^) {} return String::Empty; }
                return out->Result + "\n" + err->Result;
            }
            catch (Exception^) { return String::Empty; }
        }

        // "(c)2026 G. R. Akhtar - Shalimar 1.3" -> "Shalimar 1.3"
        static String^ Own(String^ line)
        {
            int dash = line->IndexOf(" - ");
            if (dash > 0 && line->IndexOf("Akhtar") >= 0 && line->IndexOf("Akhtar") < dash) return line->Substring(dash + 3)->Trim();
            return line;
        }

        static bool IsComplaint(String^ line)
        {
            return line->StartsWith("usage", StringComparison::OrdinalIgnoreCase) || line->IndexOf("unknown", StringComparison::OrdinalIgnoreCase) >= 0
                || line->IndexOf("no file matches", StringComparison::OrdinalIgnoreCase) >= 0 || line->IndexOf("not one this", StringComparison::OrdinalIgnoreCase) >= 0;
        }

        // the program's words with its own name, a dangling comma and outer brackets taken away
        static String^ Tidy(String^ name, String^ s)
        {
            s = Regex::Replace(s, "\\s+([,)])", "$1");
            s = Regex::Replace(s, "\\s{2,}", " ")->Trim();
            if (s->StartsWith(name, StringComparison::OrdinalIgnoreCase) && s->Length > name->Length && !Char::IsLetterOrDigit(s[name->Length]))
                s = s->Substring(name->Length)->TrimStart(gcnew array<wchar_t>{ ' ', ',', '-' });
            s = s->Trim()->TrimEnd(gcnew array<wchar_t>{ ',', '-', ' ' });
            if (s->StartsWith("(") && s->EndsWith(")")) s = s->Substring(1, s->Length - 2);
            if (s->StartsWith("- ")) s = s->Substring(2);
            return s->Length > 0 ? s : name;
        }
    };
}
