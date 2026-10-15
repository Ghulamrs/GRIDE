// =============================================================================
//  LanguageTarget.h - the 3 languages and 4 targets GRIDE knows about
// =============================================================================
#pragma once
#include "../pch.h"

namespace Gride { namespace Core {

    // Source languages. Assembly/Header/Other are file kinds that can live in
    // any project; C90, Cpp11 and Shalimar are the three compiled languages.
    public enum class Language { C90, Cpp11, Shalimar, Assembly, Header, LinkerCmd, Other };

    public enum class Target { Windows, Linux, MacOS, C6747 };

    public enum class BuildConfigKind { Debug, Release };

    // -------------------------------------------------------------------------
    public ref class LanguageInfo abstract sealed
    {
    public:
        // name stored in .pro / settings ("C90", "C++11", "Shalimar")
        static String^ Name(Language l)
        {
            switch (l)
            {
            case Language::C90:       return "C90";
            case Language::Cpp11:     return "C++11";
            case Language::Shalimar:  return "Shalimar";
            case Language::Assembly:  return "Assembly";
            case Language::Header:    return "Header";
            case Language::LinkerCmd: return "Linker command";
            default:                  return "Other";
            }
        }

        static Language Parse(String^ s)
        {
            if (String::IsNullOrEmpty(s)) return Language::C90;
            String^ t = s->Trim()->ToUpperInvariant();
            if (t == "C90" || t == "C" || t == "C89") return Language::C90;
            if (t == "C++11" || t == "CPP11" || t == "C++" || t == "CPP") return Language::Cpp11;
            if (t == "SHALIMAR") return Language::Shalimar;
            if (t == "ASSEMBLY" || t == "ASM") return Language::Assembly;
            if (t == "HEADER") return Language::Header;
            // must round-trip with Name(): "Linker command"
            if (t == "LINKER COMMAND" || t == "LINKERCMD" || t == "LINKER") return Language::LinkerCmd;
            return Language::Other;
        }

        // the three compiled languages, in menu order
        static array<Language>^ Compiled()
        {
            return gcnew array<Language>{ Language::C90, Language::Cpp11, Language::Shalimar };
        }

        static bool IsCompiled(Language l)
        {
            return l == Language::C90 || l == Language::Cpp11 || l == Language::Shalimar;
        }
    };

    // -------------------------------------------------------------------------
    public ref class TargetInfo abstract sealed
    {
    public:
        // key used in JSON and in configuration names "Debug|TMS320C6747"
        static String^ Name(Target t)
        {
            switch (t)
            {
            case Target::Windows: return "Windows";
            case Target::Linux:   return "Linux";
            case Target::MacOS:   return "macOS";
            default:              return "TMS320C6747";
            }
        }

        static String^ DisplayName(Target t)
        {
            switch (t)
            {
            case Target::Windows: return "Windows (MASM / LINK)";
            case Target::Linux:   return "Linux";
            case Target::MacOS:   return "macOS";
            default:              return "TMS320C6747 (DSP)";
            }
        }

        // short folder name used in out/<Config>/<dir>
        static String^ FolderName(Target t)
        {
            switch (t)
            {
            case Target::Windows: return "Win";
            case Target::Linux:   return "Linux";
            case Target::MacOS:   return "macOS";
            default:              return "C6747";
            }
        }

        static Target Parse(String^ s)
        {
            if (String::IsNullOrEmpty(s)) return Target::Windows;
            String^ t = s->Trim()->ToUpperInvariant();
            if (t->StartsWith("WIN")) return Target::Windows;
            if (t->StartsWith("LINUX")) return Target::Linux;
            if (t->StartsWith("MAC")) return Target::MacOS;
            if (t->Contains("6747") || t->Contains("C6") || t->Contains("DSP") || t->Contains("TMS")) return Target::C6747;
            return Target::Windows;
        }

        static array<Target>^ All()
        {
            return gcnew array<Target>{ Target::Windows, Target::Linux, Target::MacOS, Target::C6747 };
        }

        // default output extension of a linked program
        static String^ ProgramExtension(Target t)
        {
            switch (t)
            {
            case Target::Windows: return ".exe";
            case Target::C6747:   return ".out";
            default:              return "";
            }
        }
    };

    // -------------------------------------------------------------------------
    public ref class ConfigName abstract sealed
    {
    public:
        // "Debug|TMS320C6747"
        static String^ Make(String^ config, Target t) { return config + "|" + TargetInfo::Name(t); }
        static String^ ConfigPart(String^ key)
        {
            int i = key->IndexOf(L'|');
            return i < 0 ? key : key->Substring(0, i);
        }
        static String^ TargetPart(String^ key)
        {
            int i = key->IndexOf(L'|');
            if (i < 0) return String::Empty;
            return key->Substring(i + 1);
        }
    };
}}
