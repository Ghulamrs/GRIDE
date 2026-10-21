// =============================================================================
//  GrideSettings.h - everything stored in settings.json (%APPDATA%\GRIDE, shared with the other outlooks)
//
//    EnvironmentSettings  theme, fonts, editor options, folders
//    ToolchainSettings    one per target (see ToolchainSettings.h)
//    LanguageSettings     one per language: extensions, keywords, comments
//    ConverterSettings    C <-> Shalimar converter tools
//    LayoutSettings       window position, pane sizes, hidden panes/toolbars
//    RecentList           recent files / workspaces
//    ExternalTool         Tools > External Tools entries
//    KeyBindings          command id -> shortcut text ("Ctrl+Shift+B")
// =============================================================================
#pragma once
#include "ToolchainSettings.h"

namespace Gride { namespace Settings {

    using namespace Gride::Core;

    // -------------------------------------------------------------------------
    public ref class EnvironmentSettings
    {
    public:
        EnvironmentSettings()
        {
            Theme = "Dark";
            FontName = "Consolas";
            FontSize = 10.0f;
            TabSize = 4;
            InsertSpaces = false;
            LineNumbers = true;
            ShowWhitespace = false;
            ReopenLastWorkspace = true;
            SaveBeforeBuild = true;
            JumpToFirstError = true;
            AskNativeTools = true;
            RunInIde = true;
            DefaultProjectLocation = Path::Combine(Environment::GetFolderPath(Environment::SpecialFolder::MyDocuments), "GRIDE Projects");
            CcsToolRoot = "C:\\ti\\ccsv7\\tools\\compiler\\c6000_7.4.24";
            HelpDirectory = Path::Combine(PathUtil::AppDirectory(), "docs");
            RideRoot = String::Empty;
            RideIncludeDir = String::Empty;
            RideLibDir = String::Empty;
            Vcvars = String::Empty;
            TiCompilerDir = String::Empty;
            TiLibDir = String::Empty;
            DefaultCompiler = "auto";
            SharedIncludes = gcnew List<String^>();
            SharedLibraries = gcnew List<String^>();
            CcsEnabled = true;
            CcsRoot = String::Empty;
            C6747Debugger = "auto";
            SyntaxColors = gcnew Dictionary<String^, String^>();
        }
        property String^ Theme;
        property String^ FontName;
        property float   FontSize;
        property int     TabSize;
        property bool    InsertSpaces;
        property bool    LineNumbers;
        property bool    ShowWhitespace;
        property bool    ReopenLastWorkspace;
        property bool    SaveBeforeBuild;
        property bool    JumpToFirstError;        // a failed build puts the caret on its first error (RIDE)
        property bool    AskNativeTools;          // RIDE's askNative: offer the vendor's assembler/linker when the project's own fail
        property bool    RunInIde;                // Start Without Debugging runs in the Output pane (RIDE); off: an external console window
        property String^ DefaultProjectLocation;
        property String^ CcsToolRoot;            // value of ${CG_TOOL_ROOT} for CCS imports
        property String^ HelpDirectory;
        // RIDE's installation (its bin folder, or the folder above it); empty = found under Program Files
        property String^ RideRoot;
        // RIDE's settings.json, merged (section 11 of RIDE-conventions.md); each empty value means what RIDE's does
        property String^ RideIncludeDir;         // "include": cpp11's headers, relative to the installation; empty: its include folder
        property String^ RideLibDir;             // "lib": c90's headers; empty: its lib folder
        property String^ Vcvars;                 // "vcvars": Visual Studio's vcvars64.bat, whose installation is taken before vswhere's
        property String^ TiCompilerDir;          // "ti": TI's C6000 compiler directory (bin\lnk6x); named, a tms6747 build links against TI's runtime
        property String^ TiLibDir;               // "tilib": a second directory with rts6740_elf_eh.lib
        property String^ DefaultCompiler;        // "compiler": the word a new project starts with (auto, c90, cpp11, shalimar, cl)
        property List<String^>^ SharedIncludes;  // "includes": header directories every compile searches after the project's own
        property List<String^>^ SharedLibraries; // "libraries": libraries every host link takes
        property bool    CcsEnabled;             // "ccs.enabled": File > Open > CCS Project
        property String^ CcsRoot;                // "ccs.root": the CCS installation ${CG_TOOL_ROOT} is found under (C:\ti\ccsv7)
        property String^ C6747Debugger;          // auto (sim6747 on a .out with lines, else vm6747), sim6747, vm6747; GRIDE_DEBUG_* at start win
        property Dictionary<String^, String^>^ SyntaxColors;   // token -> "#RRGGBB" override

        // what ${CG_TOOL_ROOT} is resolved from, in RIDE's order: the CCS root when one is named (the
        // compiler directory is found under it by version), else TI's compiler directory named outright,
        // else the compiler directory GRIDEd named before the port (its own setting)
        String^ CcsCompilerRoot()
        {
            if (!String::IsNullOrWhiteSpace(CcsRoot)) return CcsRoot->Trim();
            if (!String::IsNullOrWhiteSpace(TiCompilerDir)) return TiCompilerDir->Trim();
            return CcsToolRoot == nullptr ? String::Empty : CcsToolRoot;
        }

        // the C6747 debugger choice in force: GRIDE_DEBUG_VM6747 / GRIDE_DEBUG_SIM6747 in the environment
        // at start win over the setting, as in RIDE
        String^ EffectiveC6747Debugger()
        {
            String^ vm = Environment::GetEnvironmentVariable("GRIDE_DEBUG_VM6747");
            if (!String::IsNullOrWhiteSpace(vm) && vm->Trim() != "0") return "vm6747";
            String^ sim = Environment::GetEnvironmentVariable("GRIDE_DEBUG_SIM6747");
            if (!String::IsNullOrWhiteSpace(sim) && sim->Trim() != "0") return "sim6747";
            return String::IsNullOrWhiteSpace(C6747Debugger) ? "auto" : C6747Debugger->Trim()->ToLowerInvariant();
        }

        JsonValue^ ToJson()
        {
            JsonValue^ o = JsonValue::MakeObject();
            o->SetString("theme", Theme);
            o->SetString("font", FontName);
            o->SetNumber("fontSize", FontSize);
            o->SetNumber("tabSize", TabSize);
            o->SetBool("insertSpaces", InsertSpaces);
            o->SetBool("lineNumbers", LineNumbers);
            o->SetBool("showWhitespace", ShowWhitespace);
            o->SetBool("reopenLastWorkspace", ReopenLastWorkspace);
            o->SetBool("saveBeforeBuild", SaveBeforeBuild);
            o->SetBool("jumpToFirstError", JumpToFirstError);
            o->SetBool("askNativeTools", AskNativeTools);
            o->SetBool("runInIde", RunInIde);
            o->SetString("defaultProjectLocation", DefaultProjectLocation);
            o->SetString("ccsToolRoot", CcsToolRoot);
            o->SetString("helpDirectory", HelpDirectory);
            // GRIDE's names since GRIDEx3: no RIDE in settings.json (FromJson still reads rideRoot ...)
            o->SetString("grideRoot", RideRoot);
            o->SetString("grideIncludeDir", RideIncludeDir);
            o->SetString("grideLibDir", RideLibDir);
            o->SetString("vcvars", Vcvars);
            o->SetString("tiCompilerDir", TiCompilerDir);
            o->SetString("tiLibDir", TiLibDir);
            o->SetString("defaultCompiler", DefaultCompiler);
            o->SetList("sharedIncludes", SharedIncludes);
            o->SetList("sharedLibraries", SharedLibraries);
            o->SetBool("ccsEnabled", CcsEnabled);
            o->SetString("ccsRoot", CcsRoot);
            o->SetString("c6747Debugger", C6747Debugger);
            JsonValue^ c = JsonValue::MakeObject();
            for each (KeyValuePair<String^, String^> kv in SyntaxColors) c->SetString(kv.Key, kv.Value);
            o->SetValue("syntaxColors", c);
            return o;
        }
        void FromJson(JsonValue^ o)
        {
            if (o == nullptr) return;
            Theme = o->GetString("theme", Theme);
            FontName = o->GetString("font", FontName);
            FontSize = (float)o->GetNumber("fontSize", FontSize);
            TabSize = o->GetInt("tabSize", TabSize);
            InsertSpaces = o->GetBool("insertSpaces", InsertSpaces);
            LineNumbers = o->GetBool("lineNumbers", LineNumbers);
            ShowWhitespace = o->GetBool("showWhitespace", ShowWhitespace);
            ReopenLastWorkspace = o->GetBool("reopenLastWorkspace", ReopenLastWorkspace);
            SaveBeforeBuild = o->GetBool("saveBeforeBuild", SaveBeforeBuild);
            JumpToFirstError = o->GetBool("jumpToFirstError", JumpToFirstError);
            AskNativeTools = o->GetBool("askNativeTools", AskNativeTools);
            RunInIde = o->GetBool("runInIde", RunInIde);
            DefaultProjectLocation = o->GetString("defaultProjectLocation", DefaultProjectLocation);
            CcsToolRoot = o->GetString("ccsToolRoot", CcsToolRoot);
            HelpDirectory = o->GetString("helpDirectory", HelpDirectory);
            RideRoot = o->GetString("grideRoot", o->GetString("rideRoot", RideRoot));
            RideIncludeDir = o->GetString("grideIncludeDir", o->GetString("rideIncludeDir", RideIncludeDir));
            RideLibDir = o->GetString("grideLibDir", o->GetString("rideLibDir", RideLibDir));
            Vcvars = o->GetString("vcvars", Vcvars);
            TiCompilerDir = o->GetString("tiCompilerDir", TiCompilerDir);
            TiLibDir = o->GetString("tiLibDir", TiLibDir);
            DefaultCompiler = o->GetString("defaultCompiler", DefaultCompiler);
            if (String::IsNullOrWhiteSpace(DefaultCompiler)) DefaultCompiler = "auto";
            if (o->Has("sharedIncludes")) SharedIncludes = o->GetStringList("sharedIncludes");
            if (o->Has("sharedLibraries")) SharedLibraries = o->GetStringList("sharedLibraries");
            CcsEnabled = o->GetBool("ccsEnabled", CcsEnabled);
            CcsRoot = o->GetString("ccsRoot", CcsRoot);
            C6747Debugger = o->GetString("c6747Debugger", C6747Debugger);
            JsonValue^ c = o->GetObject("syntaxColors");
            if (c != nullptr)
                for each (String^ k in c->KeyNames) SyntaxColors[k] = c->GetString(k, String::Empty);
        }
    };

    // -------------------------------------------------------------------------
    public ref class LanguageSettings
    {
    public:
        LanguageSettings(Language l)
        {
            Lang = l;
            Extensions = gcnew List<String^>();
            Keywords = gcnew List<String^>();
            Types = gcnew List<String^>();
            ApplyDefaults();
        }
        property Language Lang;
        property List<String^>^ Extensions;
        property List<String^>^ Keywords;
        property List<String^>^ Types;
        property String^ LineComment;           // "//" (empty for C90)
        property String^ BlockCommentStart;     // "/*"
        property String^ BlockCommentEnd;       // "*/"
        property bool    Preprocessor;          // highlight #include etc.

        property String^ Name { String^ get() { return LanguageInfo::Name(Lang); } }

        bool OwnsExtension(String^ ext)
        {
            for each (String^ e in Extensions)
                if (String::Compare(e, ext, true) == 0) return true;
            return false;
        }

        void ApplyDefaults()
        {
            Extensions->Clear(); Keywords->Clear(); Types->Clear();
            BlockCommentStart = "/*"; BlockCommentEnd = "*/";
            LineComment = "//";
            Preprocessor = true;
            array<String^>^ c90kw = { "auto", "break", "case", "const", "continue", "default", "do", "else", "enum",
                                      "extern", "for", "goto", "if", "register", "return", "sizeof", "static", "struct",
                                      "switch", "typedef", "union", "volatile", "while" };
            array<String^>^ c90ty = { "char", "double", "float", "int", "long", "short", "signed", "unsigned", "void" };
            switch (Lang)
            {
            case Language::C90:
            {
                Extensions->AddRange(gcnew array<String^>{ ".c", ".h" });
                Keywords->AddRange(c90kw);
                Types->AddRange(c90ty);
                LineComment = String::Empty;          // C90 has no // comments
                break;
            }
            case Language::Cpp11:
            {
                Extensions->AddRange(gcnew array<String^>{ ".cpp", ".cc", ".cxx", ".hpp", ".hh" });
                Keywords->AddRange(c90kw);
                Keywords->AddRange(gcnew array<String^>{ "alignas", "alignof", "asm", "catch", "class", "constexpr",
                    "const_cast", "decltype", "delete", "dynamic_cast", "explicit", "export", "false", "final",
                    "friend", "inline", "mutable", "namespace", "new", "noexcept", "nullptr", "operator", "override",
                    "private", "protected", "public", "reinterpret_cast", "static_assert", "static_cast", "template",
                    "this", "thread_local", "throw", "true", "try", "typeid", "typename", "using", "virtual" });
                Types->AddRange(c90ty);
                Types->AddRange(gcnew array<String^>{ "bool", "wchar_t", "char16_t", "char32_t", "auto" });
                break;
            }
            case Language::Shalimar:
            {
                // Placeholder word list - edit it in Tools > Options > Languages > Shalimar
                Extensions->Add(".shl");
                Keywords->AddRange(c90kw);
                Types->AddRange(c90ty);
                break;
            }
            case Language::Assembly:
            {
                Extensions->AddRange(gcnew array<String^>{ ".asm", ".s", ".sa", ".inc" });
                Keywords->AddRange(gcnew array<String^>{ ".text", ".data", ".bss", ".sect", ".global", ".def", ".ref",
                    ".word", ".half", ".byte", ".float", ".space", ".align", ".equ", ".set", ".include", ".end",
                    "PROC", "ENDP", "SEGMENT", "ENDS", "END", "DB", "DW", "DD", "DQ", "MACRO", "ENDM", "INCLUDE",
                    "PUBLIC", "EXTERN", ".MODEL", ".CODE", ".DATA", ".STACK" });
                LineComment = ";";
                BlockCommentStart = String::Empty; BlockCommentEnd = String::Empty;
                Preprocessor = false;
                break;
            }
            case Language::LinkerCmd:
            {
                Extensions->AddRange(gcnew array<String^>{ ".cmd", ".ld" });
                Keywords->AddRange(gcnew array<String^>{ "MEMORY", "SECTIONS", "ORIGIN", "LENGTH", "org", "len",
                    "-stack", "-heap", "-l", "-o", "-m", "-c", "-cr", "-x" });
                Preprocessor = false;
                break;
            }
            default:
                break;
            }
        }

        JsonValue^ ToJson()
        {
            JsonValue^ o = JsonValue::MakeObject();
            o->SetList("ext", Extensions);
            o->SetList("keywords", Keywords);
            o->SetList("types", Types);
            o->SetString("lineComment", LineComment);
            o->SetString("blockCommentStart", BlockCommentStart);
            o->SetString("blockCommentEnd", BlockCommentEnd);
            o->SetBool("preprocessor", Preprocessor);
            return o;
        }
        void FromJson(JsonValue^ o)
        {
            if (o == nullptr) return;
            if (o->Has("ext")) Extensions = o->GetStringList("ext");
            if (o->Has("keywords")) Keywords = o->GetStringList("keywords");
            if (o->Has("types")) Types = o->GetStringList("types");
            LineComment = o->GetString("lineComment", LineComment);
            BlockCommentStart = o->GetString("blockCommentStart", BlockCommentStart);
            BlockCommentEnd = o->GetString("blockCommentEnd", BlockCommentEnd);
            Preprocessor = o->GetBool("preprocessor", Preprocessor);
        }
    };

    // -------------------------------------------------------------------------
    // Tools > Convert. Empty tool and template (the default) mean toolchain.json's converter (c2s)
    // with its convertToShalimar / convertToC operation; a value here takes its place.
    public ref class ConverterSettings
    {
    public:
        ConverterSettings()
        {
            CToShalimar = String::Empty;
            ShalimarToC = String::Empty;
            CommandTemplate = String::Empty;
            OpenResult = true;
        }
        property String^ CToShalimar;       // a converter program in place of c2s, C -> Shalimar
        property String^ ShalimarToC;       // the same, Shalimar -> C
        property String^ CommandTemplate;   // $(Tool) $(Input) $(Output); empty: the converter's operation
        property bool OpenResult;

        // GRIDE 1.0's defaults, which named programs that never shipped: read back as "not set"
        literal String^ Gride1CToShalimar = "C:\\GRIDE\\bin\\c2shl.exe";
        literal String^ Gride1ShalimarToC = "C:\\GRIDE\\bin\\shl2c.exe";
        literal String^ Gride1Template = "\"$(Tool)\" \"$(Input)\" -o \"$(Output)\"";
        static String^ Unless(String^ value, String^ gride1)
        {
            return value != nullptr && String::Compare(value->Trim(), gride1, true) == 0 ? String::Empty : value;
        }

        JsonValue^ ToJson()
        {
            JsonValue^ o = JsonValue::MakeObject();
            o->SetString("CToShalimar", CToShalimar);
            o->SetString("ShalimarToC", ShalimarToC);
            o->SetString("command", CommandTemplate);
            o->SetBool("openResult", OpenResult);
            return o;
        }
        void FromJson(JsonValue^ o)
        {
            if (o == nullptr) return;
            CToShalimar = Unless(o->GetString("CToShalimar", CToShalimar), Gride1CToShalimar);
            ShalimarToC = Unless(o->GetString("ShalimarToC", ShalimarToC), Gride1ShalimarToC);
            CommandTemplate = Unless(o->GetString("command", CommandTemplate), Gride1Template);
            OpenResult = o->GetBool("openResult", OpenResult);
        }
    };

    // -------------------------------------------------------------------------
    public ref class LayoutSettings
    {
    public:
        LayoutSettings()
        {
            Left = 80; Top = 60; Width = 1400; Height = 880; Maximized = true;
            LeftWidth = 270; RightWidth = 270; BottomHeight = 220;
            HiddenPanes = gcnew List<String^>();
            HiddenToolbars = gcnew List<String^>();
            HiddenToolbars->Add("Text Editor");
            StatusBar = true;
        }
        property int Left;
        property int Top;
        property int Width;
        property int Height;
        property bool Maximized;
        property int LeftWidth;
        property int RightWidth;
        property int BottomHeight;
        property List<String^>^ HiddenPanes;
        property List<String^>^ HiddenToolbars;
        property bool StatusBar;

        JsonValue^ ToJson()
        {
            JsonValue^ o = JsonValue::MakeObject();
            o->SetNumber("left", Left); o->SetNumber("top", Top);
            o->SetNumber("width", Width); o->SetNumber("height", Height);
            o->SetBool("maximized", Maximized);
            o->SetNumber("explorer", LeftWidth);
            o->SetNumber("properties", RightWidth);
            o->SetNumber("output", BottomHeight);
            o->SetList("hiddenPanes", HiddenPanes);
            o->SetList("hiddenToolbars", HiddenToolbars);
            o->SetBool("statusBar", StatusBar);
            return o;
        }
        void FromJson(JsonValue^ o)
        {
            if (o == nullptr) return;
            Left = o->GetInt("left", Left); Top = o->GetInt("top", Top);
            Width = o->GetInt("width", Width); Height = o->GetInt("height", Height);
            Maximized = o->GetBool("maximized", Maximized);
            LeftWidth = o->GetInt("explorer", LeftWidth);
            RightWidth = o->GetInt("properties", RightWidth);
            BottomHeight = o->GetInt("output", BottomHeight);
            if (o->Has("hiddenPanes")) HiddenPanes = o->GetStringList("hiddenPanes");
            if (o->Has("hiddenToolbars")) HiddenToolbars = o->GetStringList("hiddenToolbars");
            StatusBar = o->GetBool("statusBar", StatusBar);
        }
    };

    // -------------------------------------------------------------------------
    public ref class RecentList
    {
    public:
        RecentList() { Files = gcnew List<String^>(); Workspaces = gcnew List<String^>(); }
        property List<String^>^ Files;
        property List<String^>^ Workspaces;

        static void Push(List<String^>^ list, String^ path)
        {
            for (int i = list->Count - 1; i >= 0; i--)
                if (String::Compare(list[i], path, true) == 0) list->RemoveAt(i);
            list->Insert(0, path);
            while (list->Count > 10) list->RemoveAt(list->Count - 1);
        }

        JsonValue^ ToJson()
        {
            JsonValue^ o = JsonValue::MakeObject();
            o->SetList("files", Files);
            o->SetList("workspaces", Workspaces);
            return o;
        }
        void FromJson(JsonValue^ o)
        {
            if (o == nullptr) return;
            Files = o->GetStringList("files");
            Workspaces = o->GetStringList("workspaces");
        }
    };

    // -------------------------------------------------------------------------
    public ref class ExternalTool
    {
    public:
        ExternalTool() { Title = "New Tool"; Command = String::Empty; Arguments = String::Empty; InitialDir = "$(ProjectDir)"; }
        property String^ Title;
        property String^ Command;
        property String^ Arguments;
        property String^ InitialDir;
    };

    // -------------------------------------------------------------------------
    //  GrideSettings - the root object
    // -------------------------------------------------------------------------
    public ref class GrideSettings
    {
    public:
        GrideSettings()
        {
            Env = gcnew EnvironmentSettings();
            Toolchains = gcnew List<ToolchainSettings^>();
            for each (Target t in TargetInfo::All()) Toolchains->Add(gcnew ToolchainSettings(t));
            Languages = gcnew List<LanguageSettings^>();
            Languages->Add(gcnew LanguageSettings(Language::C90));
            Languages->Add(gcnew LanguageSettings(Language::Cpp11));
            Languages->Add(gcnew LanguageSettings(Language::Shalimar));
            Languages->Add(gcnew LanguageSettings(Language::Assembly));
            Languages->Add(gcnew LanguageSettings(Language::LinkerCmd));
            Converter = gcnew ConverterSettings();
            Layout = gcnew LayoutSettings();
            Recent = gcnew RecentList();
            ExternalTools = gcnew List<ExternalTool^>();
            KeyBindings = gcnew Dictionary<String^, String^>();
            KeySet = "visualstudio";
            Snippets = DefaultSnippets();
            CompilerOptions = gcnew CompilerOptionStore();
            CcsProjects = JsonValue::MakeObject();
            LastWorkspace = String::Empty;
            FilePath = String::Empty;
            IsPortable = false;
        }

        property EnvironmentSettings^ Env;
        property List<ToolchainSettings^>^ Toolchains;
        property List<LanguageSettings^>^ Languages;
        property ConverterSettings^ Converter;
        property LayoutSettings^ Layout;
        property RecentList^ Recent;
        property List<ExternalTool^>^ ExternalTools;
        property Dictionary<String^, String^>^ KeyBindings;
        property String^ KeySet;                             // "visualstudio" or "gride" (RIDE's keys): the key set under the bindings
        property Dictionary<String^, String^>^ Snippets;     // name -> text (Tools > Code Snippets)
        // RIDE's installation-level Compiler Options (settings.json "options"): what a new project starts with
        property CompilerOptionStore^ CompilerOptions;
        // RIDE's own state for a CCS project, never written into CCS's files (settings.json "ccs.projects"):
        // absolute directory with forward slashes -> { args, emulateOnly, toolchain, folderToolchains }
        property JsonValue^ CcsProjects;
        property String^ LastWorkspace;
        property String^ FilePath;      // where this file lives
        property bool IsPortable;       // true when it sits next to GRIDE.exe

        static String^ CcsKey(String^ dir)
        {
            try { return Path::GetFullPath(dir)->TrimEnd(L'\\', L'/')->Replace("\\", "/"); }
            catch (Exception^) { return dir; }
        }
        // the entry's key as it was written, a Windows path being the same whatever its case
        String^ CcsKeyOf(String^ dir)
        {
            String^ key = CcsKey(dir);
            if (CcsProjects != nullptr)
                for each (String^ k in CcsProjects->KeyNames)
                    if (String::Compare(k, key, true) == 0) return k;
            return key;
        }
        JsonValue^ CcsState(String^ dir) { return CcsProjects == nullptr ? nullptr : CcsProjects->GetObject(CcsKeyOf(dir)); }
        void RememberCcsState(String^ dir, JsonValue^ state)
        {
            if (CcsProjects == nullptr) CcsProjects = JsonValue::MakeObject();
            String^ key = CcsKeyOf(dir);
            if (state == nullptr || state->Count == 0) CcsProjects->Remove(key);
            else CcsProjects->SetValue(key, state);
        }

        // the statics the toolchain code reads (where it has no settings object): RIDE's root, TI's
        // directories, the named vcvars64.bat
        void PublishPaths()
        {
            ToolchainSettings::RideRoot = Env->RideRoot;
            RideToolchain::TiCompilerDir = Env->TiCompilerDir;
            RideToolchain::TiLibDir = Env->TiLibDir;
            VisualStudio::NamedVcvars = Env->Vcvars;
        }

        ToolchainSettings^ Toolchain(Target t)
        {
            for each (ToolchainSettings^ s in Toolchains)
                if (s->TargetKind == t) return s;
            ToolchainSettings^ n = gcnew ToolchainSettings(t);
            Toolchains->Add(n);
            return n;
        }

        LanguageSettings^ LanguageOf(Language l)
        {
            for each (LanguageSettings^ s in Languages)
                if (s->Lang == l) return s;
            return nullptr;
        }

        // decides the language of a file from its extension; headers (.h)
        // follow the project language
        Language DetectLanguage(String^ path, Language projectLang)
        {
            String^ ext = Path::GetExtension(path);
            if (String::Compare(ext, ".h", true) == 0)
                return (projectLang == Language::Cpp11 || projectLang == Language::Shalimar) ? projectLang : Language::C90;
            for each (LanguageSettings^ s in Languages)
                if (s->OwnsExtension(ext)) return s->Lang;
            return Language::Other;
        }

        // ---- JSON --------------------------------------------------------------
        JsonValue^ ToJson()
        {
            JsonValue^ o = JsonValue::MakeObject();
            o->SetString("gride", "1.0");
            o->SetValue("environment", Env->ToJson());

            JsonValue^ tc = JsonValue::MakeObject();
            for each (ToolchainSettings^ t in Toolchains) tc->SetValue(t->Name, t->ToJson());
            o->SetValue("toolchains", tc);

            JsonValue^ langs = JsonValue::MakeObject();
            for each (LanguageSettings^ l in Languages) langs->SetValue(l->Name, l->ToJson());
            o->SetValue("languages", langs);

            o->SetValue("converters", Converter->ToJson());

            JsonValue^ tools = JsonValue::MakeArray();
            for each (ExternalTool^ x in ExternalTools)
            {
                JsonValue^ e = JsonValue::MakeObject();
                e->SetString("title", x->Title);
                e->SetString("command", x->Command);
                e->SetString("arguments", x->Arguments);
                e->SetString("initialDir", x->InitialDir);
                tools->Add(e);
            }
            o->SetValue("externalTools", tools);

            JsonValue^ keys = JsonValue::MakeObject();
            for each (KeyValuePair<String^, String^> kv in KeyBindings) keys->SetString(kv.Key, kv.Value);
            o->SetValue("keyBindings", keys);
            o->SetString("keySet", KeySet);

            JsonValue^ snip = JsonValue::MakeObject();
            for each (KeyValuePair<String^, String^> kv in Snippets) snip->SetString(kv.Key, kv.Value);
            o->SetValue("snippets", snip);

            o->SetValue("compilerOptions", CompilerOptions->ToJson());
            if (CcsProjects != nullptr && CcsProjects->Count > 0) o->SetValue("ccsProjects", CcsProjects);
            o->SetValue("recent", Recent->ToJson());
            o->SetString("lastWorkspace", LastWorkspace);
            o->SetValue("layout", Layout->ToJson());
            return o;
        }

        static GrideSettings^ FromJson(JsonValue^ o)
        {
            GrideSettings^ s = gcnew GrideSettings();
            if (o == nullptr || !o->IsObject) return s;
            s->Env->FromJson(o->GetObject("environment"));
            // the toolchains' defaults and tool search read RIDE's root (and the other paths) as soon as they are known
            s->PublishPaths();

            JsonValue^ tc = o->GetObject("toolchains");
            if (tc != nullptr)
            {
                s->Toolchains->Clear();
                for each (Target t in TargetInfo::All())
                    s->Toolchains->Add(ToolchainSettings::FromJson(t, tc->GetObject(TargetInfo::Name(t))));
            }

            JsonValue^ langs = o->GetObject("languages");
            if (langs != nullptr)
                for each (LanguageSettings^ l in s->Languages) l->FromJson(langs->GetObject(l->Name));

            s->Converter->FromJson(o->GetObject("converters"));

            JsonValue^ tools = o->Get("externalTools");
            if (tools != nullptr && tools->IsArray)
                for (int i = 0; i < tools->Count; i++)
                {
                    JsonValue^ e = tools->At(i);
                    ExternalTool^ x = gcnew ExternalTool();
                    x->Title = e->GetString("title", x->Title);
                    x->Command = e->GetString("command", x->Command);
                    x->Arguments = e->GetString("arguments", x->Arguments);
                    x->InitialDir = e->GetString("initialDir", x->InitialDir);
                    s->ExternalTools->Add(x);
                }

            JsonValue^ keys = o->GetObject("keyBindings");
            if (keys != nullptr)
                for each (String^ k in keys->KeyNames) s->KeyBindings[k] = keys->GetString(k, String::Empty);
            s->KeySet = o->GetString("keySet", s->KeySet);
            if (String::Compare(s->KeySet, "ride", true) == 0) s->KeySet = "gride";   // the name before GRIDEx3

            JsonValue^ snip = o->GetObject("snippets");
            if (snip != nullptr)
            {
                s->Snippets->Clear();
                for each (String^ k in snip->KeyNames) s->Snippets[k] = snip->GetString(k, String::Empty);
            }

            s->CompilerOptions->FromJson(o->GetObject("compilerOptions"));
            JsonValue^ ccs = o->GetObject("ccsProjects");
            if (ccs != nullptr) s->CcsProjects = ccs;
            s->Recent->FromJson(o->GetObject("recent"));
            s->LastWorkspace = o->GetString("lastWorkspace", String::Empty);
            s->Layout->FromJson(o->GetObject("layout"));
            return s;
        }

        static Dictionary<String^, String^>^ DefaultSnippets()
        {
            Dictionary<String^, String^>^ d = gcnew Dictionary<String^, String^>();
            d["for loop (C90)"] = "for (i = 0; i < n; i++)\n{\n    \n}\n";
            d["if / else"] = "if ()\n{\n}\nelse\n{\n}\n";
            d["switch"] = "switch (x)\n{\ncase 0:\n    break;\ndefault:\n    break;\n}\n";
            d["file header"] = "/*\n * File   : \n * Author : \n * Date   : \n * Brief  : \n */\n";
            d["C6747 interrupt handler"] = "interrupt void isr_name(void)\n{\n    /* clear the interrupt flag here */\n}\n";
            d["include guard"] = "#ifndef NAME_H\n#define NAME_H\n\n\n#endif /* NAME_H */\n";
            return d;
        }

        GrideSettings^ Clone()
        {
            GrideSettings^ c = FromJson(JsonParser::Parse(JsonWriter::Write(ToJson())));
            c->FilePath = FilePath;
            c->IsPortable = IsPortable;
            return c;
        }
    };

    // -------------------------------------------------------------------------
    //  SettingsManager - finds, loads and saves settings.json, GRIDE's one settings file
    //    1. <folder of GRIDE.exe>\settings.json   (portable install)
    //    2. %APPDATA%\GRIDE\settings.json         (normal install)
    //  The same file whichever outlook is installed (GRIDEx3): Black's keys sit beside Grey's and White's,
    //  whose names never meet Black's, so a save keeps the keys it does not own. gride.settings.json,
    //  the name before GRIDEx3, is read once when settings.json holds none of Black's keys, then removed.
    // -------------------------------------------------------------------------
    public ref class SettingsManager abstract sealed
    {
    public:
        literal String^ FileName = "settings.json";
        literal String^ FormerFileName = "gride.settings.json";

        static String^ PortablePath() { return Path::Combine(PathUtil::AppDirectory(), FileName); }
        static String^ UserPath()
        {
            return Path::Combine(Path::Combine(Environment::GetFolderPath(Environment::SpecialFolder::ApplicationData), "GRIDE"),
                                 FileName);
        }

        // warning receives a message when the file was unreadable (it is then backed up); the toolchain
        // definitions are loaded first, since the settings' defaults come from them
        static GrideSettings^ Load(String^% warning)
        {
            warning = nullptr;
            bool portable = File::Exists(PortablePath()) || File::Exists(Path::Combine(PathUtil::AppDirectory(), FormerFileName));
            String^ path = portable ? PortablePath() : UserPath();
            String^ former = Path::Combine(Path::GetDirectoryName(path), FormerFileName);
            ToolchainDefinitions::Current = ToolchainDefinitions::Load(ToolchainDefinitions::ShippedPath(), ToolchainDefinitions::UserPath(portable));

            JsonValue^ shared = nullptr;
            if (File::Exists(path))
            {
                try
                {
                    shared = JsonParser::ParseFile(path);
                    if (!shared->IsObject) throw gcnew FormatException("the file is not a JSON object");
                }
                catch (Exception^ ex)
                {
                    String^ backup = path + ".bad";
                    try { File::Copy(path, backup, true); } catch (Exception^) { }
                    warning = "Settings file could not be read and was reset to defaults.\n" + ex->Message +
                              "\nA copy was kept as: " + backup;
                    shared = nullptr;
                }
            }

            // Black's own keys: from settings.json, else (once) from the file of the former name
            JsonValue^ mine = Holds(shared) ? shared : nullptr;
            bool moving = false;
            if (mine == nullptr && File::Exists(former))
            {
                try { mine = JsonParser::ParseFile(former); moving = mine->IsObject; } catch (Exception^) { mine = nullptr; }
                if (!moving) mine = nullptr;
            }
            GrideSettings^ s = gcnew GrideSettings();
            if (mine != nullptr)
            {
                try { s = GrideSettings::FromJson(mine); }
                catch (Exception^ ex)
                {
                    warning = "Settings could not be read and were reset to defaults.\n" + ex->Message;
                    s = gcnew GrideSettings();
                }
            }

            s->FilePath = path;
            s->IsPortable = portable;
            if (moving || !Holds(shared))
            {
                // the former file goes once its keys are safely in settings.json
                if (Save(s) && moving && !PathUtil::SamePath(former, path))
                    try { File::Delete(former); } catch (Exception^) { }
            }
            return s;
        }

        // Black's keys over what the file holds now - the other outlooks' keys stay as they are
        static bool Save(GrideSettings^ s)
        {
            try
            {
                JsonValue^ doc = nullptr;
                if (File::Exists(s->FilePath))
                    try { doc = JsonParser::ParseFile(s->FilePath); } catch (Exception^) { doc = nullptr; }
                if (doc == nullptr || !doc->IsObject) doc = JsonValue::MakeObject();
                JsonValue^ own = s->ToJson();
                for each (String^ k in own->KeyNames) doc->SetValue(k, own->Get(k));
                JsonWriter::WriteFile(s->FilePath, doc);
                return true;
            }
            catch (Exception^) { return false; /* read-only media: keep running with in-memory settings */ }
        }

    private:
        // whether a settings object holds Black's settings ("gride" marks its format, "environment" its first section)
        static bool Holds(JsonValue^ o) { return o != nullptr && o->IsObject && (o->Has("gride") || o->Has("environment")); }
    };
}}
