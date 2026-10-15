#pragma once
// ============================================================================
//  GRIDE 2.0 - IDE settings (Phase 3)
//  Stored as JSON in %APPDATA%\GRIDE\settings.json
// ============================================================================
#include "../Common/Common.h"

namespace Gride
{
    // Well-known tool ids. Every external program the IDE calls is a "tool"
    // whose path / command line is configurable in Tools > Options > Toolchains.
    public ref class ToolIds abstract sealed
    {
    public:
        literal String^ CompilerC90        = "compiler.c90";
        literal String^ CompilerCpp11      = "compiler.cpp11";
        literal String^ CompilerShalimar   = "compiler.shalimar";
        literal String^ WinAssembler       = "win.masm";
        literal String^ WinLinker          = "win.link";
        literal String^ LinuxAssembler     = "linux.as";
        literal String^ LinuxLinker        = "linux.ld";
        literal String^ LinuxRunner        = "linux.run";
        literal String^ MacAssembler       = "macos.as";
        literal String^ MacLinker          = "macos.ld";
        literal String^ MacRunner          = "macos.run";
        literal String^ TmsAssembler       = "tms.asm6x";
        literal String^ TmsLinker          = "tms.lnk6x";
        literal String^ TmsRuntime         = "tms.rts6747";
        literal String^ TmsSimulator       = "tms.vm6747";
        literal String^ ConvertCToShalimar = "convert.c2shalimar";
        literal String^ ConvertShalimarToC = "convert.shalimar2c";
    };

    [DataContract(Name = "tool", Namespace = "")]
    public ref class ToolDefinition
    {
    public:
        [DataMember(Name = "id", Order = 0)]            property String^ Id;
        [DataMember(Name = "displayName", Order = 1)]   property String^ DisplayName;
        [DataMember(Name = "category", Order = 2)]      property String^ Category;
        [DataMember(Name = "path", Order = 3)]          property String^ ExecutablePath;
        [DataMember(Name = "arguments", Order = 4)]     property String^ Arguments;
        [DataMember(Name = "includeSwitch", Order = 5)] property String^ IncludeSwitch;
        [DataMember(Name = "defineSwitch", Order = 6)]  property String^ DefineSwitch;
        [DataMember(Name = "librarySwitch", Order = 7)] property String^ LibrarySwitch;
        [DataMember(Name = "errorPattern", Order = 8)]  property String^ ErrorPattern;
        // Extra environment for the process: "NAME=value;NAME2=value" (%VAR% is expanded).
        [DataMember(Name = "environment", Order = 9)]   property String^ Environment;
        // Optional batch file run first in the same shell, e.g. vcvars32.bat for MASM/LINK.
        [DataMember(Name = "setupScript", Order = 10)]  property String^ SetupScript;
        // Output code page of the tool: 0 = OEM (console default), 65001 = UTF-8, ...
        [DataMember(Name = "outputCodePage", Order = 11)] property int OutputCodePage;

        ToolDefinition() {}

        ToolDefinition(String^ id, String^ name, String^ category, String^ path, String^ args,
                       String^ inc, String^ def, String^ lib)
        {
            Id = id; DisplayName = name; Category = category; ExecutablePath = path; Arguments = args;
            IncludeSwitch = inc; DefineSwitch = def; LibrarySwitch = lib; ErrorPattern = String::Empty;
            Environment = String::Empty; SetupScript = String::Empty; OutputCodePage = 0;
        }

        void Normalize()
        {
            if (DisplayName == nullptr)    DisplayName = Id;
            if (Category == nullptr)       Category = "Tool";
            if (ExecutablePath == nullptr) ExecutablePath = String::Empty;
            if (Arguments == nullptr)      Arguments = String::Empty;
            if (IncludeSwitch == nullptr)  IncludeSwitch = String::Empty;
            if (DefineSwitch == nullptr)   DefineSwitch = String::Empty;
            if (LibrarySwitch == nullptr)  LibrarySwitch = String::Empty;
            if (ErrorPattern == nullptr)   ErrorPattern = String::Empty;
            if (Environment == nullptr)    Environment = String::Empty;
            if (SetupScript == nullptr)    SetupScript = String::Empty;
            if (OutputCodePage < 0)        OutputCodePage = 0;
        }

        bool IsConfigured() { return !String::IsNullOrWhiteSpace(ExecutablePath); }

        // Runtime libraries are plain files; executables may live on PATH.
        String^ ResolvedPath()
        {
            if (!IsConfigured()) return nullptr;
            if (String::Equals(Category, "Runtime"))
            {
                String^ x = System::Environment::ExpandEnvironmentVariables(ExecutablePath->Trim()->Trim(L'"'));
                return File::Exists(x) ? Path::GetFullPath(x) : nullptr;
            }
            String^ found = PathUtil::FindExecutable(ExecutablePath);
            // With a setup script (e.g. vcvars32.bat) a bare name like ml.exe is only on
            // PATH after the script ran - let cmd resolve it then.
            if (found == nullptr && !String::IsNullOrWhiteSpace(SetupScript) &&
                ExecutablePath->IndexOfAny(gcnew array<wchar_t>{ L'\\', L'/' }) < 0)
                return ExecutablePath->Trim()->Trim(L'"');
            return found;
        }

        String^ StatusText()
        {
            if (!IsConfigured()) return "Not configured";
            return ResolvedPath() != nullptr ? Str("OK") : Str("Not found");
        }
    };

    [DataContract(Name = "settings", Namespace = "")]
    public ref class IdeSettings
    {
    public:
        literal int CurrentFormatVersion = 3;

        [DataMember(Order = 0)]  property int FormatVersion;
        // ---- General
        [DataMember(Order = 1)]  property bool ReopenLastProject;
        [DataMember(Order = 2)]  property bool SaveBeforeBuild;
        [DataMember(Order = 3)]  property String^ LastProject;
        [DataMember(Order = 4)]  property List<String^>^ RecentProjects;
        [DataMember(Order = 5)]  property List<String^>^ RecentFiles;
        // ---- Editor
        [DataMember(Order = 10)] property String^ EditorFontName;
        [DataMember(Order = 11)] property float EditorFontSize;
        [DataMember(Order = 12)] property int TabSize;
        [DataMember(Order = 13)] property bool InsertSpaces;
        [DataMember(Order = 14)] property bool ShowLineNumbers;
        [DataMember(Order = 15)] property bool AutoIndent;
        [DataMember(Order = 16)] property String^ ColorText;
        [DataMember(Order = 17)] property String^ ColorKeyword;
        [DataMember(Order = 18)] property String^ ColorType;
        [DataMember(Order = 19)] property String^ ColorComment;
        [DataMember(Order = 20)] property String^ ColorString;
        [DataMember(Order = 21)] property String^ ColorNumber;
        [DataMember(Order = 22)] property String^ ColorPreprocessor;
        [DataMember(Order = 23)] property String^ ColorBackground;
        // ---- Languages
        [DataMember(Order = 30)] property String^ ShalimarExtensions;     // e.g. ".shl;.sml"
        [DataMember(Order = 31)] property List<String^>^ ShalimarKeywords;
        // ---- Window layout
        [DataMember(Order = 40)] property int WinX;
        [DataMember(Order = 41)] property int WinY;
        [DataMember(Order = 42)] property int WinW;
        [DataMember(Order = 43)] property int WinH;
        [DataMember(Order = 44)] property bool WinMaximized;
        [DataMember(Order = 45)] property int ExplorerWidth;
        [DataMember(Order = 46)] property int PropertiesWidth;
        [DataMember(Order = 47)] property int BottomHeight;
        // ---- Toolchains
        [DataMember(Order = 50)] property List<ToolDefinition^>^ Tools;

        IdeSettings() { ApplyDefaults(); }

        // ---------------------------------------------------------------
        void ApplyDefaults()
        {
            FormatVersion = CurrentFormatVersion;
            ReopenLastProject = true;
            SaveBeforeBuild = true;
            LastProject = String::Empty;
            RecentProjects = gcnew List<String^>();
            RecentFiles = gcnew List<String^>();
            EditorFontName = "Consolas";
            EditorFontSize = 10.0f;
            TabSize = 4;
            InsertSpaces = true;
            ShowLineNumbers = true;
            AutoIndent = true;
            ColorText = "#000000";
            ColorKeyword = "#0000FF";
            ColorType = "#2B91AF";
            ColorComment = "#008000";
            ColorString = "#A31515";
            ColorNumber = "#098658";
            ColorPreprocessor = "#808080";
            ColorBackground = "#FFFFFF";
            ShalimarExtensions = ".shl";
            ShalimarKeywords = DefaultShalimarKeywords();
            WinX = WinY = WinW = WinH = 0;
            WinMaximized = false;
            ExplorerWidth = 260;
            PropertiesWidth = 260;
            BottomHeight = 200;
            Tools = DefaultTools();
        }

        // Called after loading: fills anything missing from an older/edited file.
        void Normalize()
        {
            IdeSettings^ d = gcnew IdeSettings();
            if (FormatVersion < 1)
            {
                // Booleans default to false when absent: restore sensible values.
                ReopenLastProject = true; SaveBeforeBuild = true;
                ShowLineNumbers = true; AutoIndent = true; InsertSpaces = true;
            }
            int oldVersion = FormatVersion;
            FormatVersion = CurrentFormatVersion;
            if (LastProject == nullptr) LastProject = String::Empty;
            if (RecentProjects == nullptr) RecentProjects = gcnew List<String^>();
            if (RecentFiles == nullptr) RecentFiles = gcnew List<String^>();
            if (String::IsNullOrWhiteSpace(EditorFontName)) EditorFontName = d->EditorFontName;
            if (EditorFontSize < 6.0f || EditorFontSize > 48.0f) EditorFontSize = d->EditorFontSize;
            if (TabSize < 1 || TabSize > 16) TabSize = 4;
            if (String::IsNullOrWhiteSpace(ColorText)) ColorText = d->ColorText;
            if (String::IsNullOrWhiteSpace(ColorKeyword)) ColorKeyword = d->ColorKeyword;
            if (String::IsNullOrWhiteSpace(ColorType)) ColorType = d->ColorType;
            if (String::IsNullOrWhiteSpace(ColorComment)) ColorComment = d->ColorComment;
            if (String::IsNullOrWhiteSpace(ColorString)) ColorString = d->ColorString;
            if (String::IsNullOrWhiteSpace(ColorNumber)) ColorNumber = d->ColorNumber;
            if (String::IsNullOrWhiteSpace(ColorPreprocessor)) ColorPreprocessor = d->ColorPreprocessor;
            if (String::IsNullOrWhiteSpace(ColorBackground)) ColorBackground = d->ColorBackground;
            if (String::IsNullOrWhiteSpace(ShalimarExtensions)) ShalimarExtensions = d->ShalimarExtensions;
            if (ShalimarKeywords == nullptr) ShalimarKeywords = d->ShalimarKeywords;
            if (ExplorerWidth < 80) ExplorerWidth = d->ExplorerWidth;
            if (PropertiesWidth < 80) PropertiesWidth = d->PropertiesWidth;
            if (BottomHeight < 60) BottomHeight = d->BottomHeight;
            if (Tools == nullptr) Tools = gcnew List<ToolDefinition^>();
            // drop broken entries, then add any tool this version knows but the file lacks
            Tools->RemoveAll(gcnew Predicate<ToolDefinition^>(&IdeSettings::IsBrokenTool));
            for each (ToolDefinition^ t in Tools) t->Normalize();
            for each (ToolDefinition^ def in d->Tools)
                if (GetTool(def->Id) == nullptr) Tools->Add(def);
            if (oldVersion < 3)
            {
                // v3 introduced {asmflags}: upgrade assembler arguments still at the old default.
                for each (ToolDefinition^ t in Tools)
                {
                    ToolDefinition^ def = d->GetTool(t->Id);
                    if (def == nullptr || !String::Equals(t->Category, def->Category)) continue;
                    String^ oldDefault = def->Arguments->Replace("{asmflags} ", String::Empty)->Replace("{asmflags}", "{flags}");
                    String^ oldDefault2 = def->Arguments->Replace("{asmflags} ", String::Empty);
                    if (def->Arguments->Contains("{asmflags}") &&
                        (String::Equals(t->Arguments, oldDefault) || String::Equals(t->Arguments, oldDefault2)))
                        t->Arguments = def->Arguments;
                }
            }
        }

        ToolDefinition^ GetTool(String^ id)
        {
            if (Tools == nullptr) return nullptr;
            for each (ToolDefinition^ t in Tools)
                if (t != nullptr && String::Equals(t->Id, id, StringComparison::OrdinalIgnoreCase)) return t;
            return nullptr;
        }

        List<String^>^ ShalimarExtensionList()
        {
            List<String^>^ r = gcnew List<String^>();
            for each (String^ e in PathUtil::SplitList(ShalimarExtensions))
            {
                String^ x = e->Trim()->ToLowerInvariant();
                if (!x->StartsWith(".")) x = String::Concat(".", x);
                if (x->Length > 1) r->Add(x);
            }
            if (r->Count == 0) r->Add(".shl");
            return r;
        }

        bool IsShalimarExtension(String^ ext)
        {
            if (String::IsNullOrEmpty(ext)) return false;
            return ShalimarExtensionList()->Contains(ext->ToLowerInvariant());
        }

        String^ PrimaryShalimarExtension() { return ShalimarExtensionList()[0]; }

        void AddRecentProject(String^ path) { AddRecent(RecentProjects, path); LastProject = path; }
        void AddRecentFile(String^ path)    { AddRecent(RecentFiles, path); }

        // ---------------------------------------------------------------
        static String^ SettingsFolder()
        {
            return Path::Combine(System::Environment::GetFolderPath(System::Environment::SpecialFolder::ApplicationData), "GRIDE");
        }
        static String^ SettingsPath() { return Path::Combine(SettingsFolder(), "settings.json"); }

        static IdeSettings^ Load(String^% warning)
        {
            warning = nullptr;
            String^ path = SettingsPath();
            IdeSettings^ s = nullptr;
            if (File::Exists(path))
            {
                try
                {
                    s = JsonLoad<IdeSettings>(path);
                }
                catch (Exception^ ex)
                {
                    warning = String::Format("Settings file could not be read and was reset.\n{0}\nA copy was kept as settings.json.bad", ex->Message);
                    try { File::Copy(path, String::Concat(path, ".bad"), true); } catch (Exception^) {}
                    s = nullptr;
                }
            }
            if (s == nullptr) s = gcnew IdeSettings();
            s->Normalize();
            return s;
        }

        bool Save(String^% error)
        {
            error = nullptr;
            try
            {
                JsonSave<IdeSettings>(SettingsPath(), this);
                return true;
            }
            catch (Exception^ ex)
            {
                error = ex->Message;
                return false;
            }
        }

    private:
        static bool IsBrokenTool(ToolDefinition^ t) { return t == nullptr || String::IsNullOrWhiteSpace(t->Id); }

        static void AddRecent(List<String^>^ list, String^ path)
        {
            if (list == nullptr || String::IsNullOrWhiteSpace(path)) return;
            for (int i = list->Count - 1; i >= 0; i--)
                if (String::Equals(list[i], path, StringComparison::OrdinalIgnoreCase)) list->RemoveAt(i);
            list->Insert(0, path);
            while (list->Count > 10) list->RemoveAt(list->Count - 1);
        }

        static List<String^>^ DefaultShalimarKeywords()
        {
            // Placeholder set - edit in Tools > Options > Languages to match the Shalimar grammar.
            List<String^>^ k = gcnew List<String^>();
            k->AddRange(gcnew array<String^>{
                "if", "else", "while", "for", "do", "return", "break", "continue",
                "int", "char", "void", "const", "var", "func", "proc", "begin", "end",
                "true", "false" });
            return k;
        }

        static List<ToolDefinition^>^ DefaultTools()
        {
            List<ToolDefinition^>^ t = gcnew List<ToolDefinition^>();
            String^ compileArgs = "{flags} {includes} {defines} -o \"{out}\" \"{in}\"";
            // ---- Compilers (your own toolchain; they emit assembly for {target})
            t->Add(gcnew ToolDefinition(ToolIds::CompilerC90, "C90 Compiler", "Compiler", "", compileArgs, "-I", "-D", ""));
            t->Add(gcnew ToolDefinition(ToolIds::CompilerCpp11, "C++11 Compiler", "Compiler", "", compileArgs, "-I", "-D", ""));
            t->Add(gcnew ToolDefinition(ToolIds::CompilerShalimar, "Shalimar Compiler", "Compiler", "", compileArgs, "-I", "-D", ""));
            // ---- Windows: MASM / LINK
            t->Add(gcnew ToolDefinition(ToolIds::WinAssembler, "MASM Assembler (ml)", "Windows", "ml.exe",
                "/nologo /c /Zi {asmflags} {includes} /Fo\"{out}\" \"{in}\"", "/I", "/D", ""));
            t->Add(gcnew ToolDefinition(ToolIds::WinLinker, "Microsoft Linker (LINK)", "Windows", "link.exe",
                "/nologo /SUBSYSTEM:CONSOLE {linkflags} /OUT:\"{out}\" {objs} {libs}", "", "", ""));
            // ---- Linux / macOS (cross tools, WSL, or remote scripts - configure as needed)
            t->Add(gcnew ToolDefinition(ToolIds::LinuxAssembler, "Linux Assembler", "Linux", "", "{asmflags} {includes} -o \"{out}\" \"{in}\"", "-I", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::LinuxLinker, "Linux Linker", "Linux", "", "{linkflags} -o \"{out}\" {objs} {libs}", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::LinuxRunner, "Linux Runner", "Linux", "", "\"{exe}\"", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::MacAssembler, "macOS Assembler", "macOS", "", "{asmflags} {includes} -o \"{out}\" \"{in}\"", "-I", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::MacLinker, "macOS Linker", "macOS", "", "{linkflags} -o \"{out}\" {objs} {libs}", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::MacRunner, "macOS Runner", "macOS", "", "\"{exe}\"", "", "", ""));
            // ---- TMS320C6747 DSP
            t->Add(gcnew ToolDefinition(ToolIds::TmsAssembler, "ASM6x Assembler", "TMS6747", "asm6x.exe",
                "{asmflags} {includes} \"{in}\" \"{out}\"", "-i", "-d", ""));
            t->Add(gcnew ToolDefinition(ToolIds::TmsLinker, "LNK6x Linker", "TMS6747", "lnk6x.exe",
                "{linkflags} -o \"{out}\" -m \"{map}\" {cmdfiles} {objs} {libs}", "", "", "-l"));
            t->Add(gcnew ToolDefinition(ToolIds::TmsRuntime, "RTS6747 Runtime Library (file)", "Runtime", "", "", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::TmsSimulator, "VM6747 Virtual Machine", "TMS6747", "", "\"{exe}\"", "", "", ""));
            // ---- Converters
            t->Add(gcnew ToolDefinition(ToolIds::ConvertCToShalimar, "C to Shalimar Converter", "Converter", "", "\"{in}\" -o \"{out}\"", "", "", ""));
            t->Add(gcnew ToolDefinition(ToolIds::ConvertShalimarToC, "Shalimar to C Converter", "Converter", "", "\"{in}\" -o \"{out}\"", "", "", ""));
            return t;
        }
    };
}
