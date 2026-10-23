// =============================================================================
//  ToolchainDefinitions.h - toolchain.json: every tool, its arguments, the
//  libraries and where things are looked for, read from the shipped file beside
//  GRIDE.exe with the user's file laid over it field by field. Nothing of the
//  toolchain's names stays in the code: RideToolchain and ToolchainSettings ask
//  here. The format is written up in ride-port\TOOLCHAIN-FORMAT.md.
// =============================================================================
#pragma once
#include "../Core/Json.h"
#include "../Core/PathUtil.h"
#include "../Core/LanguageTarget.h"

namespace Gride { namespace Settings {

    using namespace Gride::Core;

    // one thing a tool does: the argument template and what each argument means
    public ref class OperationDefinition
    {
    public:
        OperationDefinition() { Id = String::Empty; Description = String::Empty; Args = String::Empty; Shown = String::Empty; Output = String::Empty; Arguments = gcnew Dictionary<String^, String^>(); }
        property String^ Id;
        property String^ Description;
        property String^ Args;
        property String^ Shown;        // the short line GRIDE prints for it, when it differs from Args
        property String^ Output;
        property Dictionary<String^, String^>^ Arguments;
    };

    public ref class LookupDefinition
    {
    public:
        LookupDefinition() { Env = String::Empty; InstallDir = String::Empty; Dirs = gcnew List<String^>(); OnPath = true; Vcvars = false; Description = String::Empty; }
        property String^ Env;
        property String^ InstallDir;
        property List<String^>^ Dirs;   // folders of its own (%VAR% expanded), looked in before PATH
        property bool OnPath;
        property bool Vcvars;
        property String^ Description;
    };

    // how a debugger is driven on its standard input: the dialect its answers are read in (gdb, lldb,
    // cdb - or shalimar, a program that stops itself), the preamble sent at start, the marker asked
    // for after every command, and the spelling of each command the IDE needs ($(File), $(Line),
    // $(Expr), $(Name), $(Value), $(N), $(Count), $(Address); the Shalimar session's $(Unit)); on a
    // pseudo-console where the debugger flushes its prompt only to one (cdb); with the variables the
    // child starts with (SHM_DEBUG=1) and, for the Shalimar session, the words its lines start with
    public ref class SessionDefinition
    {
    public:
        SessionDefinition()
        {
            Description = String::Empty; Dialect = String::Empty; Like = String::Empty; Marker = String::Empty; MarkerCommand = String::Empty;
            Console = false; AfterReady = String::Empty;
            Preamble = gcnew List<String^>(); Commands = gcnew Dictionary<String^, String^>();
            Environment = gcnew Dictionary<String^, String^>(); Protocol = gcnew Dictionary<String^, String^>();
        }
        property String^ Description;
        property String^ Dialect;               // gdb / lldb / cdb / shalimar: the shapes the answers are read in
        property String^ Like;                  // the tool whose session this one starts from
        property String^ Marker;                // the text that ends every answer
        property String^ MarkerCommand;         // the command that makes the debugger print it
        property bool Console;                  // driven on a pseudo-console
        property String^ AfterReady;            // the command id sent once after the program's ready line
        property List<String^>^ Preamble;
        property Dictionary<String^, String^>^ Commands;
        property Dictionary<String^, String^>^ Environment;    // NAME -> value the child starts with
        property Dictionary<String^, String^>^ Protocol;       // file / ready / stop / at / exit -> the first word of that line

        String^ Command(String^ id)
        {
            String^ v;
            return Commands->TryGetValue(id, v) ? v : String::Empty;
        }
        bool Has(String^ id) { return Commands->ContainsKey(id) && Commands[id]->Length > 0; }
        String^ ProtocolWord(String^ id)
        {
            String^ v;
            return Protocol->TryGetValue(id, v) ? v : String::Empty;
        }

        SessionDefinition^ Clone()
        {
            SessionDefinition^ c = gcnew SessionDefinition();
            c->Description = Description; c->Dialect = Dialect; c->Like = Like; c->Marker = Marker; c->MarkerCommand = MarkerCommand;
            c->Console = Console; c->AfterReady = AfterReady;
            c->Preamble = gcnew List<String^>(Preamble);
            for each (KeyValuePair<String^, String^> kv in Commands) c->Commands[kv.Key] = kv.Value;
            for each (KeyValuePair<String^, String^> kv in Environment) c->Environment[kv.Key] = kv.Value;
            for each (KeyValuePair<String^, String^> kv in Protocol) c->Protocol[kv.Key] = kv.Value;
            return c;
        }
    };

    public ref class ToolDefinition
    {
    public:
        ToolDefinition(String^ id)
        {
            Id = id; Name = id; Description = String::Empty; Role = String::Empty;
            Languages = gcnew List<String^>(); Targets = gcnew List<String^>();
            Exe = String::Empty; Aliases = gcnew List<String^>(); Words = gcnew List<String^>();
            Lookup = gcnew LookupDefinition(); VersionArgs = nullptr;
            OutputEncoding = "utf-8"; Diagnostics = "none";
            Environment = gcnew Dictionary<String^, String^>();
            ArchFlag = String::Empty; IncludeFlag = String::Empty; DefineFlag = String::Empty;
            InstallIncludeDirs = gcnew List<String^>();
            LanguageFlags = gcnew Dictionary<String^, String^>();
            ConfigFlags = gcnew Dictionary<String^, String^>();
            ConfigFlagsByTarget = gcnew Dictionary<String^, Dictionary<String^, String^>^>();
            AsmSyntaxFlags = gcnew Dictionary<String^, String^>();
            Operations = gcnew Dictionary<String^, OperationDefinition^>();
            OperationOrder = gcnew List<String^>();
            Options = gcnew List<String^>();
            ProjectSettings = gcnew Dictionary<String^, Dictionary<String^, String^>^>(StringComparer::OrdinalIgnoreCase);
            ProjectSettingOrder = gcnew List<String^>();
            Session = nullptr;
        }
        property SessionDefinition^ Session;   // for a tool that debugs (null for the others)
        property String^ Id;
        property String^ Name;
        property String^ Description;
        property String^ Role;
        property List<String^>^ Languages;     // C, C++, Shalimar
        property List<String^>^ Targets;       // arch words
        property String^ Exe;
        property List<String^>^ Aliases;
        property List<String^>^ Words;         // what a project file may call it
        property LookupDefinition^ Lookup;
        property String^ VersionArgs;          // null: no version query
        property String^ OutputEncoding;       // utf-8 / oem
        property String^ Diagnostics;          // gnu / msvc / shalimar / masm / ti / none
        property Dictionary<String^, String^>^ Environment;
        property String^ ArchFlag;
        property String^ IncludeFlag;
        property String^ DefineFlag;
        property List<String^>^ InstallIncludeDirs;
        property Dictionary<String^, String^>^ LanguageFlags;
        property Dictionary<String^, String^>^ ConfigFlags;
        property Dictionary<String^, Dictionary<String^, String^>^>^ ConfigFlagsByTarget;
        property Dictionary<String^, String^>^ AsmSyntaxFlags;
        property Dictionary<String^, OperationDefinition^>^ Operations;
        property List<String^>^ OperationOrder;
        property List<String^>^ Options;       // the compilerOptions ids the tool takes, in the order their flags are written
        // a Visual Studio project's own settings (MSBuild's ClCompile names) -> value -> the switch the tool takes for it;
        // "*" stands for any other value ($(Value) filled in); written in the order of ProjectSettingOrder
        property Dictionary<String^, Dictionary<String^, String^>^>^ ProjectSettings;
        property List<String^>^ ProjectSettingOrder;

        bool TakesOption(String^ id) { return Options->Contains(id); }

        // the switch for a project's setting: the value's own entry, else "*"; null when the table names neither
        // (an entry of "" is a setting read and given nothing)
        String^ ProjectSettingFlag(String^ name, String^ value)
        {
            Dictionary<String^, String^>^ m;
            if (name == nullptr || !ProjectSettings->TryGetValue(name, m)) return nullptr;
            String^ v = value == nullptr ? String::Empty : value->Trim();
            for each (KeyValuePair<String^, String^> kv in m)
                if (kv.Key != "*" && String::Compare(kv.Key, v, true) == 0) return kv.Value;
            String^ any;
            return m->TryGetValue("*", any) ? any->Replace("$(Value)", v) : nullptr;
        }

        // the exe first, then the aliases: the names tried in order
        array<String^>^ AllNames()
        {
            List<String^>^ r = gcnew List<String^>();
            if (!String::IsNullOrWhiteSpace(Exe)) r->Add(Exe);
            for each (String^ a in Aliases) if (!String::IsNullOrWhiteSpace(a) && !r->Contains(a)) r->Add(a);
            return r->ToArray();
        }

        bool TakesArch() { return !String::IsNullOrWhiteSpace(ArchFlag); }
        bool ServesTarget(String^ arch) { return Targets->Count == 0 || Targets->Contains(arch); }
        bool CompilesLanguage(String^ languageWord) { return Languages->Contains(languageWord); }

        OperationDefinition^ Operation(String^ id)
        {
            OperationDefinition^ op;
            return Operations->TryGetValue(id, op) ? op : nullptr;
        }
        String^ OperationArgs(String^ id)
        {
            OperationDefinition^ op = Operation(id);
            return op == nullptr ? String::Empty : op->Args;
        }

        // the configuration's flags: the exception for the arch when there is one
        String^ ConfigFlagsFor(String^ config, String^ arch)
        {
            Dictionary<String^, String^>^ byTarget;
            String^ v;
            if (arch != nullptr && ConfigFlagsByTarget->TryGetValue(arch, byTarget) && byTarget->TryGetValue(config, v)) return v;
            return ConfigFlags->TryGetValue(config, v) ? v : String::Empty;
        }
        String^ AsmSyntaxFlag(String^ key)
        {
            String^ v;
            return AsmSyntaxFlags->TryGetValue(key, v) ? v : String::Empty;
        }
        String^ LanguageFlag(String^ languageWord)
        {
            String^ v;
            return LanguageFlags->TryGetValue(languageWord, v) ? v : String::Empty;
        }
        bool HasName(String^ leafOrPath)
        {
            if (String::IsNullOrEmpty(leafOrPath)) return false;
            String^ leaf = Path::GetFileName(leafOrPath);
            for each (String^ n in AllNames())
                if (String::Compare(n, leaf, true) == 0 || String::Compare(Path::GetFileNameWithoutExtension(n), Path::GetFileNameWithoutExtension(leaf), true) == 0) return true;
            return false;
        }
    };

    public ref class LibraryDefinition
    {
    public:
        LibraryDefinition(String^ id)
        {
            Id = id; Name = id; Description = String::Empty; Kind = String::Empty; Target = String::Empty;
            Directory = String::Empty; DirectoryEnv = String::Empty; Marker = String::Empty;
            Files = gcnew Dictionary<String^, String^>(); Aliases = gcnew List<String^>();
            AssemblyDirectories = gcnew Dictionary<String^, String^>(); AssemblyDirectoryEnv = gcnew Dictionary<String^, String^>();
            When = "always";
        }
        property String^ Id;
        property String^ Name;
        property String^ Description;
        property String^ Kind;             // runtime / printf / shalimarRuntime / hostCrt
        property String^ Target;           // arch
        property String^ Directory;
        property String^ DirectoryEnv;
        property String^ Marker;
        property Dictionary<String^, String^>^ Files;   // config -> file(s)
        property List<String^>^ Aliases;
        property Dictionary<String^, String^>^ AssemblyDirectories;
        property Dictionary<String^, String^>^ AssemblyDirectoryEnv;
        property String^ When;

        String^ FileFor(bool debug)
        {
            String^ v;
            return Files->TryGetValue(debug ? "Debug" : "Release", v) ? v : String::Empty;
        }
        String^ AssemblyDirectoryFor(bool debug)
        {
            String^ v;
            return AssemblyDirectories->TryGetValue(debug ? "Debug" : "Release", v) ? v : String::Empty;
        }
        String^ AssemblyDirectoryEnvFor(bool debug)
        {
            String^ v;
            return AssemblyDirectoryEnv->TryGetValue(debug ? "Debug" : "Release", v) ? v : String::Empty;
        }
        // a project's name for this library: an alias, or one of its own files ('*' at the end matches any ending)
        bool IsAlias(String^ lib)
        {
            if (String::IsNullOrEmpty(lib)) return false;
            String^ l = Path::GetFileName(lib)->ToLowerInvariant();
            for each (String^ a in Aliases)
            {
                String^ x = a->ToLowerInvariant();
                if (x->EndsWith("*")) { if (l->StartsWith(x->Substring(0, x->Length - 1))) return true; }
                else if (x == l) return true;
            }
            for each (KeyValuePair<String^, String^> kv in Files)
                for each (String^ f in kv.Value->Split(gcnew array<wchar_t>{ L' ' }, StringSplitOptions::RemoveEmptyEntries))
                    if (f->ToLowerInvariant() == l) return true;
            return false;
        }
    };

    public ref class TargetDefinition
    {
    public:
        TargetDefinition(String^ arch)
        {
            Arch = arch; IdeTarget = String::Empty; Name = arch; Description = String::Empty;
            Host = false; Emulated = false; DebugInfo = String::Empty;
            AssemblyExtension = ".s"; ObjectExtension = ".o"; ProgramExtension = String::Empty; EmulatedProgramExtension = String::Empty;
            LibraryDirFlag = "-L"; LibraryFlag = "-l";
            Roles = gcnew Dictionary<String^, String^>();
            Runtime = String::Empty; LinkerCommandFile = String::Empty; Run = "none"; Debug = "debugger";
            ShownRun = String::Empty; ShownEmulate = String::Empty;
        }
        property String^ Arch;
        property String^ IdeTarget;
        property String^ Name;
        property String^ Description;
        property bool Host;
        property bool Emulated;
        property String^ DebugInfo;
        property String^ AssemblyExtension;
        property String^ ObjectExtension;
        property String^ ProgramExtension;
        property String^ EmulatedProgramExtension;
        property String^ LibraryDirFlag;
        property String^ LibraryFlag;
        property Dictionary<String^, String^>^ Roles;   // role -> tool id
        property String^ Runtime;                       // library id
        property String^ LinkerCommandFile;             // linkerCommandFiles id
        property String^ Run;                           // program / simulator / none
        property String^ Debug;                         // debugger / simulator / none
        property String^ ShownRun;
        property String^ ShownEmulate;

        String^ ToolIdFor(String^ role)
        {
            String^ v;
            return Roles->TryGetValue(role, v) ? v : String::Empty;
        }
    };

    public ref class LinkerCommandFileDefinition
    {
    public:
        LinkerCommandFileDefinition(String^ id) { Id = id; Name = id; Description = String::Empty; Lines = gcnew List<String^>(); }
        property String^ Id;
        property String^ Name;
        property String^ Description;
        property List<String^>^ Lines;
        String^ Text() { return String::Join("\n", Lines) + "\n"; }
    };

    public ref class InstallationDefinition
    {
    public:
        InstallationDefinition()
        {
            Description = String::Empty; DefaultRoots = gcnew List<String^>(); BinDir = String::Empty;
            Markers = gcnew List<String^>(); IncludeDir = String::Empty; LibDir = String::Empty;
        }
        property String^ Description;
        property List<String^>^ DefaultRoots;
        property String^ BinDir;
        property List<String^>^ Markers;
        property String^ IncludeDir;
        property String^ LibDir;
    };

    // where Visual Studio's tools and the Windows SDK are, so cl, ml64, link.exe and clang run without vcvars64.bat
    public ref class VisualStudioDefinition
    {
    public:
        VisualStudioDefinition()
        {
            Description = String::Empty; Vswhere = String::Empty; VswhereArgs = String::Empty;
            DefaultRoots = gcnew List<String^>(); Editions = gcnew List<String^>(); Vcvars = String::Empty;
            MsvcTools = String::Empty; HostBin = String::Empty; Lib = String::Empty; Include = String::Empty; LlvmBin = String::Empty;
            KitRegistryKey = String::Empty; KitRegistryValue = String::Empty; KitRoots = gcnew List<String^>(); KitVersionsUnder = String::Empty;
            KitLibDirs = gcnew List<String^>(); KitIncludeDirs = gcnew List<String^>(); KitBinDir = String::Empty;
            Environment = gcnew Dictionary<String^, String^>();
            MsvcVersion = String::Empty;
        }
        property String^ Description;
        // the compiler version to take under msvcTools ("14.44" for Visual Studio 2022's v143, or a whole
        // folder name); "" takes the newest - cl, link.exe and the INCLUDE / LIB directories all follow it
        property String^ MsvcVersion;
        property String^ Vswhere;
        property String^ VswhereArgs;
        property List<String^>^ DefaultRoots;
        property List<String^>^ Editions;
        property String^ Vcvars;
        property String^ MsvcTools;
        property String^ HostBin;
        property String^ Lib;
        property String^ Include;
        property String^ LlvmBin;
        property String^ KitRegistryKey;
        property String^ KitRegistryValue;
        property List<String^>^ KitRoots;
        property String^ KitVersionsUnder;
        property List<String^>^ KitLibDirs;
        property List<String^>^ KitIncludeDirs;
        property String^ KitBinDir;
        property Dictionary<String^, String^>^ Environment;
        bool Defined() { return !String::IsNullOrEmpty(MsvcTools); }
    };

    // one row of RIDE's Compiler Options table (options.cpp): what the dialog shows, what the value
    // becomes on the command line, and when it applies
    public ref class CompilerOptionDefinition
    {
    public:
        CompilerOptionDefinition(String^ id)
        {
            Id = id; Tab = String::Empty; Label = id; Hint = String::Empty; Control = "check";
            Choices = gcnew List<String^>(); Defaults = gcnew Dictionary<String^, String^>(); DefaultValue = nullptr;
            FlagOn = String::Empty; FlagOff = String::Empty; Flag = String::Empty; Silent = nullptr; FlagEach = String::Empty;
            EnvironmentVariable = String::Empty; Needs = String::Empty; Unavailable = String::Empty;
        }
        property String^ Id;                       // c90.opt, general.jobs
        property String^ Tab;
        property String^ Label;
        property String^ Hint;
        property String^ Control;                  // check / choice / text
        property List<String^>^ Choices;
        property Dictionary<String^, String^>^ Defaults;   // per configuration name, when the file gives them so
        property String^ DefaultValue;             // one default for every configuration, or null
        property String^ FlagOn;
        property String^ FlagOff;
        property String^ Flag;                     // a choice's flag, $(Value) filled in
        property String^ Silent;                   // the choice that writes nothing; null: the first choice
        property String^ FlagEach;                 // a text list's flag per entry, $(Item) filled in
        property String^ EnvironmentVariable;
        property String^ Needs;                    // lineTable / emulated / notEmulated / ""
        property String^ Unavailable;              // the sentence said when it does not apply, $(Arch) filled in

        bool IsCheck() { return String::Compare(Control, "check", true) == 0; }
        bool IsChoice() { return String::Compare(Control, "choice", true) == 0; }
        bool IsText() { return String::Compare(Control, "text", true) == 0; }

        // the default of a configuration: the one named for it, else the one for all, else the
        // control's own (the first choice, 0 for a check, nothing for a text)
        String^ DefaultFor(String^ config)
        {
            String^ v;
            if (config != nullptr && Defaults->TryGetValue(config, v)) return v;
            if (DefaultValue != nullptr) return DefaultValue;
            if (IsChoice()) return Choices->Count > 0 ? Choices[0] : String::Empty;
            return IsCheck() ? "0" : String::Empty;
        }
        String^ SilentChoice() { return Silent != nullptr ? Silent : Choices->Count > 0 ? Choices[0] : String::Empty; }
        // the compiler word in front of the dot (c90.opt -> c90); general.* belongs to no one compiler
        String^ ToolWord()
        {
            int dot = Id->IndexOf(L'.');
            return dot > 0 ? Id->Substring(0, dot) : String::Empty;
        }
    };

    // reading CCS projects: TI's option definitions, which projects the toolchain builds, TI's runtime names
    public ref class CcsDefinition
    {
    public:
        CcsDefinition()
        {
            Description = String::Empty; DefinitionsDir = String::Empty; Definitions = gcnew Dictionary<String^, String^>();
            DeviceFamily = String::Empty; DeviceVariant = String::Empty; SiliconVersion = String::Empty;
            CompilerDirs = gcnew Dictionary<String^, String^>(); RuntimeIndex = String::Empty; RuntimeLibrary = String::Empty; RuntimeFallback = String::Empty;
            TiRoots = gcnew List<String^>(); TiUnder = gcnew List<String^>(); TiBin = String::Empty; TiLib = String::Empty;
        }
        property String^ Description;
        property String^ DefinitionsDir;
        property Dictionary<String^, String^>^ Definitions;    // major version -> file name
        property String^ DeviceFamily;
        property String^ DeviceVariant;
        property String^ SiliconVersion;
        property Dictionary<String^, String^>^ CompilerDirs;   // major version -> directory name prefix
        property String^ RuntimeIndex;
        property String^ RuntimeLibrary;
        property String^ RuntimeFallback;
        // where TI's compiler directories are looked for when none is named (RIDE's detectedTi), and their layout
        property List<String^>^ TiRoots;       // %SystemDrive%\ti
        property List<String^>^ TiUnder;       // "", ccs*\tools\compiler, ccs*\ccs\tools\compiler
        property String^ TiBin;                // bin: the linker's folder under the compiler directory
        property String^ TiLib;                // lib: the runtime's folder
        bool Defined() { return Definitions->Count > 0; }
    };

    // -------------------------------------------------------------------------
    public ref class ToolchainDefinitions
    {
    public:
        literal String^ FileName = "toolchain.json";
        literal String^ UserFileNamePortable = "toolchain.user.json";
        literal String^ EnvironmentOverride = "GRIDE_TOOLCHAIN";    // names the shipped file (tests, odd layouts)

        // the definitions in force; loaded from the shipped and user files on first use
        static property ToolchainDefinitions^ Current
        {
            ToolchainDefinitions^ get()
            {
                if (current == nullptr) current = Load(ShippedPath(), UserPath(false));
                return current;
            }
            void set(ToolchainDefinitions^ d) { current = d; }
        }

        // the shipped file: $GRIDE_TOOLCHAIN, beside the exe, else the source layout above a bin\<platform>\<config> folder
        static String^ ShippedPath()
        {
            String^ env = System::Environment::GetEnvironmentVariable(EnvironmentOverride);
            if (!String::IsNullOrWhiteSpace(env) && File::Exists(env)) return Path::GetFullPath(env);
            String^ dir = ExeDirectory();
            if (dir == nullptr) return FileName;
            String^ beside = Path::Combine(dir, FileName);
            if (File::Exists(beside)) return beside;
            try
            {
                String^ up = dir;
                for (int i = 0; i < 4 && up != nullptr; i++)
                {
                    up = Path::GetDirectoryName(up);
                    if (up == nullptr) break;
                    for each (String^ candidate in gcnew array<String^>{ Path::Combine(up, FileName), Path::Combine(Path::Combine(up, "GRIDE"), FileName) })
                        if (File::Exists(candidate)) return candidate;
                }
            }
            catch (Exception^) { }
            return beside;
        }

        static String^ UserPath(bool portable)
        {
            String^ dir = ExeDirectory();
            if (portable && dir != nullptr) return Path::Combine(dir, UserFileNamePortable);
            return Path::Combine(System::Environment::GetFolderPath(System::Environment::SpecialFolder::ApplicationData), "GRIDE\\" + FileName);
        }

        static String^ ExeDirectory()
        {
            try
            {
                System::Reflection::Assembly^ a = System::Reflection::Assembly::GetEntryAssembly();
                if (a == nullptr) a = System::Reflection::Assembly::GetExecutingAssembly();
                return Path::GetDirectoryName(a->Location);
            }
            catch (Exception^) { return nullptr; }
        }

        // reads both files; never throws - every fault is a line in Problems and the entry falls back
        static ToolchainDefinitions^ Load(String^ shippedPath, String^ userPath)
        {
            ToolchainDefinitions^ d = gcnew ToolchainDefinitions();
            d->ShippedFile = shippedPath == nullptr ? String::Empty : shippedPath;
            d->UserFile = userPath == nullptr ? String::Empty : userPath;
            d->ShippedJson = d->ReadFile(d->ShippedFile, "the shipped " + FileName, true);
            if (d->ShippedJson == nullptr) d->ShippedJson = JsonValue::MakeObject();
            d->CheckDocument(d->ShippedJson, d->ShippedLabel(), true);
            d->UserJson = d->UserFile->Length > 0 && File::Exists(d->UserFile) ? d->ReadFile(d->UserFile, "the user's " + FileName, false) : nullptr;
            if (d->UserJson != nullptr) d->CheckDocument(d->UserJson, d->UserLabel(), false);
            d->MergedJson = Clone(d->ShippedJson);
            if (d->UserJson != nullptr) MergeInto(d->MergedJson, d->UserJson);
            d->Build();
            return d;
        }

        static ToolchainDefinitions^ Reload()
        {
            ToolchainDefinitions^ was = current;
            current = Load(was == nullptr ? ShippedPath() : was->ShippedFile, was == nullptr ? UserPath(false) : was->UserFile);
            return current;
        }

        property String^ ShippedFile;
        property String^ UserFile;
        property JsonValue^ ShippedJson;
        property JsonValue^ UserJson;          // null until the user overrides something
        property JsonValue^ MergedJson;
        property List<String^>^ Problems;
        property int Format;
        property String^ Description;
        property InstallationDefinition^ Installation;
        property VisualStudioDefinition^ VisualStudio;
        property CcsDefinition^ Ccs;
        property List<TargetDefinition^>^ Targets;
        property List<ToolDefinition^>^ Tools;
        property List<LibraryDefinition^>^ Libraries;
        property List<LinkerCommandFileDefinition^>^ LinkerCommandFiles;
        property Dictionary<String^, String^>^ Placeholders;
        property bool ShippedFound;
        // RIDE's Compiler Options table: the tabs in order, the options in the order the tabs show them
        property String^ CompilerOptionsDescription;
        property List<String^>^ CompilerOptionTabs;
        property List<CompilerOptionDefinition^>^ CompilerOptions;

        CompilerOptionDefinition^ CompilerOption(String^ id)
        {
            if (id == nullptr) return nullptr;
            for each (CompilerOptionDefinition^ o in CompilerOptions) if (o->Id == id) return o;
            return nullptr;
        }
        // the options of a tab, in order
        List<CompilerOptionDefinition^>^ CompilerOptionsOf(String^ tab)
        {
            List<CompilerOptionDefinition^>^ r = gcnew List<CompilerOptionDefinition^>();
            for each (CompilerOptionDefinition^ o in CompilerOptions) if (String::Compare(o->Tab, tab, true) == 0) r->Add(o);
            return r;
        }
        // the compiler an option belongs to: the tool its word names, else the first tool listing it
        ToolDefinition^ ToolOfOption(String^ id)
        {
            CompilerOptionDefinition^ o = CompilerOption(id);
            ToolDefinition^ t = o == nullptr ? nullptr : ToolByWord(o->ToolWord());
            if (t != nullptr) return t;
            for each (ToolDefinition^ d in Tools) if (d->TakesOption(id)) return d;
            return nullptr;
        }

        // ---- lookups ------------------------------------------------------------------------
        TargetDefinition^ TargetByArch(String^ arch)
        {
            if (arch == nullptr) return nullptr;
            for each (TargetDefinition^ t in Targets) if (String::Compare(t->Arch, arch->Trim(), true) == 0) return t;
            return nullptr;
        }
        TargetDefinition^ TargetByIde(String^ ideName)
        {
            if (ideName == nullptr) return nullptr;
            for each (TargetDefinition^ t in Targets) if (String::Compare(t->IdeTarget, ideName, true) == 0) return t;
            return nullptr;
        }
        TargetDefinition^ TargetOf(Target t) { return TargetByIde(TargetInfo::Name(t)); }
        TargetDefinition^ HostTarget()
        {
            for each (TargetDefinition^ t in Targets) if (t->Host) return t;
            return nullptr;
        }
        ToolDefinition^ Tool(String^ id)
        {
            if (id == nullptr) return nullptr;
            for each (ToolDefinition^ t in Tools) if (t->Id == id) return t;
            return nullptr;
        }
        // the first tool of a role (the compilers are the same on every target)
        ToolDefinition^ ToolByRole(String^ role)
        {
            for each (ToolDefinition^ t in Tools) if (t->Role == role) return t;
            return nullptr;
        }
        // the tool playing a role for a target, by the target's roles table, else by role
        ToolDefinition^ ToolFor(Target target, String^ role)
        {
            TargetDefinition^ t = TargetOf(target);
            ToolDefinition^ tool = t == nullptr ? nullptr : Tool(t->ToolIdFor(role));
            return tool != nullptr ? tool : nullptr;
        }
        ToolDefinition^ ToolForArch(String^ arch, String^ role)
        {
            TargetDefinition^ t = TargetByArch(arch);
            return t == nullptr ? nullptr : Tool(t->ToolIdFor(role));
        }
        // the tool a project file's word names
        ToolDefinition^ ToolByWord(String^ word)
        {
            if (String::IsNullOrWhiteSpace(word)) return nullptr;
            String^ w = word->Trim();
            for each (ToolDefinition^ t in Tools)
                for each (String^ x in t->Words) if (String::Compare(x, w, true) == 0) return t;
            return nullptr;
        }
        // the tool a program name (exe or alias, with or without a folder) belongs to
        ToolDefinition^ ToolByName(String^ exeOrPath)
        {
            for each (ToolDefinition^ t in Tools) if (t->HasName(exeOrPath)) return t;
            return nullptr;
        }
        // the compiler of a language: the role the language belongs to
        ToolDefinition^ CompilerOf(String^ languageWord)
        {
            for each (String^ role in gcnew array<String^>{ "cCompiler", "cppCompiler", "shalimarCompiler" })
            {
                ToolDefinition^ t = ToolByRole(role);
                if (t != nullptr && t->CompilesLanguage(languageWord)) return t;
            }
            return nullptr;
        }
        LibraryDefinition^ Library(String^ id)
        {
            if (id == nullptr) return nullptr;
            for each (LibraryDefinition^ l in Libraries) if (l->Id == id) return l;
            return nullptr;
        }
        LibraryDefinition^ LibraryByKind(String^ arch, String^ kind)
        {
            for each (LibraryDefinition^ l in Libraries)
                if (l->Kind == kind && (String::IsNullOrEmpty(arch) || String::Compare(l->Target, arch, true) == 0)) return l;
            return nullptr;
        }
        // the C runtime of a target (its "runtime" entry)
        LibraryDefinition^ RuntimeOf(Target target)
        {
            TargetDefinition^ t = TargetOf(target);
            return t == nullptr ? nullptr : Library(t->Runtime);
        }
        LinkerCommandFileDefinition^ LinkerCommandFile(String^ id)
        {
            if (id == nullptr) return nullptr;
            for each (LinkerCommandFileDefinition^ f in LinkerCommandFiles) if (f->Id == id) return f;
            return nullptr;
        }
        LinkerCommandFileDefinition^ LinkerCommandFileOf(Target target)
        {
            TargetDefinition^ t = TargetOf(target);
            return t == nullptr ? nullptr : LinkerCommandFile(t->LinkerCommandFile);
        }

        // ---- the user's overrides ---------------------------------------------------------------
        bool IsOverridden(String^ section, String^ id)
        {
            if (UserJson == nullptr) return false;
            JsonValue^ s = UserJson->GetObject(section);
            return s != nullptr && s->Has(id);
        }
        // the user's entry of a section (made when absent), to be changed and saved
        JsonValue^ UserEntry(String^ section, String^ id)
        {
            if (UserJson == nullptr) UserJson = JsonValue::MakeObject();
            JsonValue^ s = UserJson->GetObject(section);
            if (s == nullptr) { s = JsonValue::MakeObject(); UserJson->SetValue(section, s); }
            JsonValue^ e = s->GetObject(id);
            if (e == nullptr) { e = JsonValue::MakeObject(); s->SetValue(id, e); }
            return e;
        }
        // replaces the user's entry with the fields given (an empty object means: as shipped)
        void SetUserEntry(String^ section, String^ id, JsonValue^ fields)
        {
            if (UserJson == nullptr) UserJson = JsonValue::MakeObject();
            JsonValue^ s = UserJson->GetObject(section);
            if (fields == nullptr || fields->Count == 0)
            {
                if (s != nullptr) { s->Remove(id); if (s->Count == 0) UserJson->Remove(section); }
                return;
            }
            if (s == nullptr) { s = JsonValue::MakeObject(); UserJson->SetValue(section, s); }
            s->SetValue(id, fields);
        }
        void RestoreShipped(String^ section, String^ id) { SetUserEntry(section, id, nullptr); }
        JsonValue^ ShippedEntry(String^ section, String^ id)
        {
            JsonValue^ s = ShippedJson->GetObject(section);
            return s == nullptr ? nullptr : s->GetObject(id);
        }
        JsonValue^ MergedEntry(String^ section, String^ id)
        {
            JsonValue^ s = MergedJson->GetObject(section);
            return s == nullptr ? nullptr : s->GetObject(id);
        }

        // writes the user's file (removes it when nothing is overridden) and reloads
        bool SaveUser(String^% error)
        {
            error = nullptr;
            try
            {
                if (String::IsNullOrEmpty(UserFile)) { error = "no user file is named"; return false; }
                if (UserJson == nullptr || UserJson->Count == 0)
                {
                    if (File::Exists(UserFile)) File::Delete(UserFile);
                }
                else
                {
                    PathUtil::EnsureDirectory(Path::GetDirectoryName(UserFile));
                    // Settings only: what the file is and how it overrides is the Help's to say (Settings_Files.html).
                    if (!UserJson->Has("format")) UserJson->SetNumber("format", Format > 0 ? Format : 1);
                    JsonWriter::WriteFile(UserFile, UserJson);
                }
                return true;
            }
            catch (Exception^ ex) { error = ex->Message; return false; }
        }

        // ---- helpers for the pages and the check dialog ----------------------------------------
        static array<String^>^ Roles()
        {
            return gcnew array<String^>{ "cCompiler", "cppCompiler", "shalimarCompiler", "hostCompiler", "hostCxx", "assembler", "linker",
                                         "debugAssembler", "debugLinker", "nativeAssembler", "nativeLinker", "simulator", "emulator", "ownDebugger", "debugger", "converter" };
        }

        static JsonValue^ Clone(JsonValue^ v) { return v == nullptr ? nullptr : JsonParser::Parse(JsonWriter::Write(v)); }

        // the user's fields laid over the shipped ones: objects recurse, everything else is replaced
        static void MergeInto(JsonValue^ into, JsonValue^ from)
        {
            if (into == nullptr || from == nullptr || !into->IsObject || !from->IsObject) return;
            for each (String^ k in from->KeyNames)
            {
                JsonValue^ theirs = from->Get(k);
                JsonValue^ mine = into->Get(k);
                if (theirs != nullptr && theirs->IsObject && mine != nullptr && mine->IsObject) MergeInto(mine, theirs);
                else into->SetValue(k, Clone(theirs));
            }
        }

    private:
        static ToolchainDefinitions^ current;

        ToolchainDefinitions()
        {
            Problems = gcnew List<String^>();
            Format = 0; Description = String::Empty;
            Installation = gcnew InstallationDefinition();
            VisualStudio = gcnew VisualStudioDefinition();
            Ccs = gcnew CcsDefinition();
            Targets = gcnew List<TargetDefinition^>();
            Tools = gcnew List<ToolDefinition^>();
            Libraries = gcnew List<LibraryDefinition^>();
            LinkerCommandFiles = gcnew List<LinkerCommandFileDefinition^>();
            Placeholders = gcnew Dictionary<String^, String^>();
            ShippedFound = false;
            ShippedFile = String::Empty; UserFile = String::Empty;
            CompilerOptionsDescription = String::Empty;
            CompilerOptionTabs = gcnew List<String^>();
            CompilerOptions = gcnew List<CompilerOptionDefinition^>();
        }

        String^ ShippedLabel() { return FileName + " (shipped, " + ShippedFile + ")"; }
        String^ UserLabel() { return FileName + " (user, " + UserFile + ")"; }

        JsonValue^ ReadFile(String^ path, String^ what, bool shipped)
        {
            if (String::IsNullOrEmpty(path) || !File::Exists(path))
            {
                if (shipped)
                    Problems->Add(what + " was not found - looked for " + (String::IsNullOrEmpty(path) ? FileName : path) +
                                  " (beside GRIDE.exe, or named by " + EnvironmentOverride + "); no tool is defined until it is there");
                return nullptr;
            }
            try
            {
                JsonValue^ v = JsonParser::ParseFile(path);
                if (v == nullptr || !v->IsObject)
                {
                    Problems->Add(what + " (" + path + ") does not hold a JSON object - " + (shipped ? "no tool is defined" : "the shipped file is used alone"));
                    return nullptr;
                }
                if (shipped) ShippedFound = true;
                return v;
            }
            catch (Exception^ ex)
            {
                Problems->Add(what + " (" + path + ") cannot be read: " + ex->Message + " - " + (shipped ? "no tool is defined until it is fixed" : "the shipped file is used alone"));
                return nullptr;
            }
        }

        // ---- validation: a field of the wrong kind is dropped with a message, and the other file's
        // value (or the default) stands in. The known fields and their kinds:
        static bool Expect(JsonValue^ obj, String^ key, JsonKind kind, String^ where, String^ file, List<String^>^ problems)
        {
            JsonValue^ v = obj->Get(key);
            if (v == nullptr || v->Kind == JsonKind::Null) return true;
            if (v->Kind == kind) return true;
            problems->Add(file + ": " + where + ": field '" + key + "' must be " + KindName(kind) + " - it is " + KindName(v->Kind) + " and is ignored (the other file's value, or the default, stands)");
            obj->Remove(key);
            return false;
        }
        static String^ KindName(JsonKind k)
        {
            switch (k)
            {
            case JsonKind::String: return "a string";
            case JsonKind::Number: return "a number";
            case JsonKind::Bool: return "true or false";
            case JsonKind::Array: return "a list";
            case JsonKind::Object: return "an object";
            default: return "null";
            }
        }
        static void ExpectStringMap(JsonValue^ obj, String^ key, String^ where, String^ file, List<String^>^ problems)
        {
            if (!Expect(obj, key, JsonKind::Object, where, file, problems)) return;
            JsonValue^ m = obj->GetObject(key);
            if (m == nullptr) return;
            for each (String^ k in m->KeyNames)
                if (k != "description") Expect(m, k, JsonKind::String, where + " > " + key, file, problems);
        }

        void CheckDocument(JsonValue^ doc, String^ file, bool shipped)
        {
            Expect(doc, "format", JsonKind::Number, "the document", file, Problems);
            Expect(doc, "description", JsonKind::String, "the document", file, Problems);
            for each (String^ section in gcnew array<String^>{ "installation", "visualStudio", "ccs", "compilerOptions", "placeholders", "targets", "tools", "libraries", "linkerCommandFiles" })
                Expect(doc, section, JsonKind::Object, "the document", file, Problems);
            JsonValue^ co = doc->GetObject("compilerOptions");
            if (co != nullptr)
            {
                Expect(co, "description", JsonKind::String, "compilerOptions", file, Problems);
                Expect(co, "tabs", JsonKind::Array, "compilerOptions", file, Problems);
                if (Expect(co, "options", JsonKind::Object, "compilerOptions", file, Problems) && co->GetObject("options") != nullptr)
                {
                    JsonValue^ opts = co->GetObject("options");
                    for each (String^ id in opts->KeyNames)
                    {
                        if (id == "description") continue;
                        String^ where = "compiler option '" + id + "'";
                        if (!Expect(opts, id, JsonKind::Object, "compilerOptions > options", file, Problems)) continue;
                        JsonValue^ o = opts->GetObject(id);
                        for each (String^ k in gcnew array<String^>{ "tab", "label", "hint", "control", "flagOn", "flagOff", "flag", "silent", "flagEach", "environment", "needs", "unavailable" })
                            Expect(o, k, JsonKind::String, where, file, Problems);
                        Expect(o, "choices", JsonKind::Array, where, file, Problems);
                        JsonValue^ def = o->Get("default");
                        if (def != nullptr && def->Kind != JsonKind::Null && def->Kind != JsonKind::String && def->Kind != JsonKind::Object)
                        {
                            Problems->Add(file + ": " + where + ": field 'default' must be a string or an object - it is " + KindName(def->Kind) + " and is ignored (the other file's value, or the default, stands)");
                            o->Remove("default");
                        }
                        else if (def != nullptr && def->Kind == JsonKind::Object) ExpectStringMap(o, "default", where, file, Problems);
                        if (shipped && !o->Has("control")) Problems->Add(file + ": " + where + ": field 'control' is missing - the option is taken as a check box");
                    }
                }
            }
            JsonValue^ ccs = doc->GetObject("ccs");
            if (ccs != nullptr)
            {
                for each (String^ k in gcnew array<String^>{ "description", "definitionsDir", "deviceFamily", "deviceVariant", "siliconVersion" }) Expect(ccs, k, JsonKind::String, "ccs", file, Problems);
                for each (String^ k in gcnew array<String^>{ "definitions", "compilerDirs", "tiRuntime" }) ExpectStringMap(ccs, k, "ccs", file, Problems);
                if (Expect(ccs, "tiCompiler", JsonKind::Object, "ccs", file, Problems) && ccs->GetObject("tiCompiler") != nullptr)
                {
                    JsonValue^ ti = ccs->GetObject("tiCompiler");
                    for each (String^ k in gcnew array<String^>{ "roots", "under" }) Expect(ti, k, JsonKind::Array, "ccs > tiCompiler", file, Problems);
                    for each (String^ k in gcnew array<String^>{ "bin", "lib", "description" }) Expect(ti, k, JsonKind::String, "ccs > tiCompiler", file, Problems);
                }
            }
            JsonValue^ inst = doc->GetObject("installation");
            if (inst != nullptr)
            {
                Expect(inst, "defaultRoots", JsonKind::Array, "installation", file, Problems);
                Expect(inst, "markers", JsonKind::Array, "installation", file, Problems);
                for each (String^ k in gcnew array<String^>{ "binDir", "includeDir", "libDir", "description" }) Expect(inst, k, JsonKind::String, "installation", file, Problems);
            }
            JsonValue^ vs = doc->GetObject("visualStudio");
            if (vs != nullptr)
            {
                for each (String^ k in gcnew array<String^>{ "description", "vswhere", "vswhereArgs", "vcvars", "msvcTools", "msvcVersion", "hostBin", "lib", "include", "llvmBin" })
                    Expect(vs, k, JsonKind::String, "visualStudio", file, Problems);
                for each (String^ k in gcnew array<String^>{ "defaultRoots", "editions" }) Expect(vs, k, JsonKind::Array, "visualStudio", file, Problems);
                ExpectStringMap(vs, "environment", "visualStudio", file, Problems);
                if (Expect(vs, "windowsKits", JsonKind::Object, "visualStudio", file, Problems) && vs->GetObject("windowsKits") != nullptr)
                {
                    JsonValue^ k = vs->GetObject("windowsKits");
                    for each (String^ s in gcnew array<String^>{ "registryKey", "registryValue", "versionsUnder", "binDir" }) Expect(k, s, JsonKind::String, "visualStudio > windowsKits", file, Problems);
                    for each (String^ s in gcnew array<String^>{ "roots", "libDirs", "includeDirs" }) Expect(k, s, JsonKind::Array, "visualStudio > windowsKits", file, Problems);
                }
            }
            JsonValue^ targets = doc->GetObject("targets");
            if (targets != nullptr)
                for each (String^ id in targets->KeyNames)
                {
                    if (id == "description") continue;
                    String^ where = "target '" + id + "'";
                    if (!Expect(targets, id, JsonKind::Object, "targets", file, Problems)) continue;
                    JsonValue^ t = targets->GetObject(id);
                    for each (String^ k in gcnew array<String^>{ "ideTarget", "name", "description", "debugInfo", "assemblyExtension", "objectExtension", "programExtension",
                                                                "emulatedProgramExtension", "libraryDirFlag", "libraryFlag", "runtime", "linkerCommandFile", "run", "debug", "shownRun", "shownEmulate" })
                        Expect(t, k, JsonKind::String, where, file, Problems);
                    for each (String^ k in gcnew array<String^>{ "host", "emulated" }) Expect(t, k, JsonKind::Bool, where, file, Problems);
                    ExpectStringMap(t, "roles", where, file, Problems);
                    if (shipped && !t->Has("ideTarget")) Problems->Add(file + ": " + where + ": field 'ideTarget' is missing - the target cannot be matched to the IDE's and is left out");
                }
            JsonValue^ tools = doc->GetObject("tools");
            if (tools != nullptr)
                for each (String^ id in tools->KeyNames)
                {
                    if (id == "description") continue;
                    String^ where = "tool '" + id + "'";
                    if (!Expect(tools, id, JsonKind::Object, "tools", file, Problems)) continue;
                    JsonValue^ t = tools->GetObject(id);
                    for each (String^ k in gcnew array<String^>{ "name", "description", "role", "exe", "outputEncoding", "diagnostics", "archFlag", "includeFlag", "defineFlag" })
                        Expect(t, k, JsonKind::String, where, file, Problems);
                    for each (String^ k in gcnew array<String^>{ "languages", "targets", "aliases", "words", "installIncludeDirs", "options" })
                        Expect(t, k, JsonKind::Array, where, file, Problems);
                    if (Expect(t, "lookup", JsonKind::Object, where, file, Problems) && t->GetObject("lookup") != nullptr)
                    {
                        JsonValue^ l = t->GetObject("lookup");
                        for each (String^ k in gcnew array<String^>{ "env", "installDir", "description" }) Expect(l, k, JsonKind::String, where + " > lookup", file, Problems);
                        for each (String^ k in gcnew array<String^>{ "path", "vcvars" }) Expect(l, k, JsonKind::Bool, where + " > lookup", file, Problems);
                    }
                    if (Expect(t, "version", JsonKind::Object, where, file, Problems) && t->GetObject("version") != nullptr)
                        Expect(t->GetObject("version"), "args", JsonKind::String, where + " > version", file, Problems);
                    for each (String^ k in gcnew array<String^>{ "environment", "languageFlags", "configFlags", "asmSyntaxFlags" }) ExpectStringMap(t, k, where, file, Problems);
                    JsonValue^ sess = t->Get("session");
                    if (sess != nullptr && sess->Kind != JsonKind::Null && sess->Kind != JsonKind::Object && sess->Kind != JsonKind::String)
                    {
                        Problems->Add(file + ": " + where + ": field 'session' must be an object or a tool id - it is " + KindName(sess->Kind) + " and is ignored (the other file's value, or the default, stands)");
                        t->Remove("session");
                    }
                    else if (sess != nullptr && sess->IsObject)
                    {
                        for each (String^ k in gcnew array<String^>{ "description", "dialect", "like", "marker", "markerCommand", "afterReady" }) Expect(sess, k, JsonKind::String, where + " > session", file, Problems);
                        Expect(sess, "console", JsonKind::Bool, where + " > session", file, Problems);
                        Expect(sess, "preamble", JsonKind::Array, where + " > session", file, Problems);
                        for each (String^ k in gcnew array<String^>{ "commands", "environment", "protocol" }) ExpectStringMap(sess, k, where + " > session", file, Problems);
                    }
                    if (Expect(t, "configFlagsByTarget", JsonKind::Object, where, file, Problems) && t->GetObject("configFlagsByTarget") != nullptr)
                    {
                        JsonValue^ by = t->GetObject("configFlagsByTarget");
                        for each (String^ arch in by->KeyNames) ExpectStringMap(by, arch, where + " > configFlagsByTarget", file, Problems);
                    }
                    if (Expect(t, "projectSettings", JsonKind::Object, where, file, Problems) && t->GetObject("projectSettings") != nullptr)
                    {
                        JsonValue^ ps = t->GetObject("projectSettings");
                        for each (String^ name in ps->KeyNames)
                            if (name != "description") ExpectStringMap(ps, name, where + " > projectSettings", file, Problems);
                    }
                    if (Expect(t, "operations", JsonKind::Object, where, file, Problems) && t->GetObject("operations") != nullptr)
                    {
                        JsonValue^ ops = t->GetObject("operations");
                        for each (String^ op in ops->KeyNames)
                        {
                            if (op == "description") continue;
                            String^ w = where + " > operation '" + op + "'";
                            if (!Expect(ops, op, JsonKind::Object, where + " > operations", file, Problems)) continue;
                            JsonValue^ o = ops->GetObject(op);
                            for each (String^ k in gcnew array<String^>{ "description", "args", "shown", "output" }) Expect(o, k, JsonKind::String, w, file, Problems);
                            ExpectStringMap(o, "arguments", w, file, Problems);
                            if (shipped && !o->Has("args")) Problems->Add(file + ": " + w + ": field 'args' is missing - the operation has no arguments");
                        }
                    }
                    if (shipped)
                    {
                        if (!t->Has("exe")) Problems->Add(file + ": " + where + ": field 'exe' is missing - the tool has no program name and cannot be found");
                        if (!t->Has("role")) Problems->Add(file + ": " + where + ": field 'role' is missing - the tool plays no part");
                    }
                }
            JsonValue^ libs = doc->GetObject("libraries");
            if (libs != nullptr)
                for each (String^ id in libs->KeyNames)
                {
                    if (id == "description") continue;
                    String^ where = "library '" + id + "'";
                    if (!Expect(libs, id, JsonKind::Object, "libraries", file, Problems)) continue;
                    JsonValue^ l = libs->GetObject(id);
                    for each (String^ k in gcnew array<String^>{ "name", "description", "kind", "target", "directory", "directoryEnv", "marker", "when" })
                        Expect(l, k, JsonKind::String, where, file, Problems);
                    Expect(l, "aliases", JsonKind::Array, where, file, Problems);
                    for each (String^ k in gcnew array<String^>{ "files", "assemblyDirectories", "assemblyDirectoryEnv" }) ExpectStringMap(l, k, where, file, Problems);
                    if (shipped && !l->Has("files")) Problems->Add(file + ": " + where + ": field 'files' is missing - the library names no file");
                }
            JsonValue^ cmds = doc->GetObject("linkerCommandFiles");
            if (cmds != nullptr)
                for each (String^ id in cmds->KeyNames)
                {
                    if (id == "description") continue;
                    String^ where = "linker command file '" + id + "'";
                    if (!Expect(cmds, id, JsonKind::Object, "linkerCommandFiles", file, Problems)) continue;
                    JsonValue^ c = cmds->GetObject(id);
                    for each (String^ k in gcnew array<String^>{ "name", "description" }) Expect(c, k, JsonKind::String, where, file, Problems);
                    Expect(c, "lines", JsonKind::Array, where, file, Problems);
                }
        }

        // ---- the typed model from the merged document --------------------------------------------
        static void ReadMap(JsonValue^ obj, String^ key, Dictionary<String^, String^>^ into)
        {
            into->Clear();
            JsonValue^ m = obj == nullptr ? nullptr : obj->GetObject(key);
            if (m == nullptr) return;
            for each (String^ k in m->KeyNames)
                if (k != "description") into[k] = m->GetString(k, String::Empty);
        }
        static List<String^>^ ReadList(JsonValue^ obj, String^ key)
        {
            List<String^>^ r = gcnew List<String^>();
            if (obj != nullptr && obj->Has(key)) for each (String^ s in obj->GetStringList(key)) if (!String::IsNullOrWhiteSpace(s)) r->Add(s->Trim());
            return r;
        }

        void Build()
        {
            JsonValue^ doc = MergedJson;
            Format = doc->GetInt("format", 0);
            Description = doc->GetString("description", String::Empty);
            JsonValue^ inst = doc->GetObject("installation");
            if (inst != nullptr)
            {
                Installation->Description = inst->GetString("description", String::Empty);
                Installation->DefaultRoots = ReadList(inst, "defaultRoots");
                Installation->BinDir = inst->GetString("binDir", String::Empty);
                Installation->Markers = ReadList(inst, "markers");
                Installation->IncludeDir = inst->GetString("includeDir", String::Empty);
                Installation->LibDir = inst->GetString("libDir", String::Empty);
            }
            JsonValue^ vs = doc->GetObject("visualStudio");
            if (vs != nullptr)
            {
                VisualStudio->Description = vs->GetString("description", String::Empty);
                VisualStudio->Vswhere = vs->GetString("vswhere", String::Empty);
                VisualStudio->VswhereArgs = vs->GetString("vswhereArgs", String::Empty);
                VisualStudio->DefaultRoots = ReadList(vs, "defaultRoots");
                VisualStudio->Editions = ReadList(vs, "editions");
                VisualStudio->Vcvars = vs->GetString("vcvars", String::Empty);
                VisualStudio->MsvcTools = vs->GetString("msvcTools", String::Empty);
                VisualStudio->MsvcVersion = vs->GetString("msvcVersion", String::Empty)->Trim();
                VisualStudio->HostBin = vs->GetString("hostBin", String::Empty);
                VisualStudio->Lib = vs->GetString("lib", String::Empty);
                VisualStudio->Include = vs->GetString("include", String::Empty);
                VisualStudio->LlvmBin = vs->GetString("llvmBin", String::Empty);
                ReadMap(vs, "environment", VisualStudio->Environment);
                JsonValue^ kits = vs->GetObject("windowsKits");
                if (kits != nullptr)
                {
                    VisualStudio->KitRegistryKey = kits->GetString("registryKey", String::Empty);
                    VisualStudio->KitRegistryValue = kits->GetString("registryValue", String::Empty);
                    VisualStudio->KitRoots = ReadList(kits, "roots");
                    VisualStudio->KitVersionsUnder = kits->GetString("versionsUnder", String::Empty);
                    VisualStudio->KitLibDirs = ReadList(kits, "libDirs");
                    VisualStudio->KitIncludeDirs = ReadList(kits, "includeDirs");
                    VisualStudio->KitBinDir = kits->GetString("binDir", String::Empty);
                }
            }
            JsonValue^ ccs = doc->GetObject("ccs");
            if (ccs != nullptr)
            {
                Ccs->Description = ccs->GetString("description", String::Empty);
                Ccs->DefinitionsDir = ccs->GetString("definitionsDir", String::Empty);
                ReadMap(ccs, "definitions", Ccs->Definitions);
                Ccs->DeviceFamily = ccs->GetString("deviceFamily", String::Empty);
                Ccs->DeviceVariant = ccs->GetString("deviceVariant", String::Empty);
                Ccs->SiliconVersion = ccs->GetString("siliconVersion", String::Empty);
                ReadMap(ccs, "compilerDirs", Ccs->CompilerDirs);
                JsonValue^ rt = ccs->GetObject("tiRuntime");
                if (rt != nullptr)
                {
                    Ccs->RuntimeIndex = rt->GetString("index", String::Empty);
                    Ccs->RuntimeLibrary = rt->GetString("library", String::Empty);
                    Ccs->RuntimeFallback = rt->GetString("fallback", String::Empty);
                }
                JsonValue^ ti = ccs->GetObject("tiCompiler");
                if (ti != nullptr)
                {
                    Ccs->TiRoots = ReadList(ti, "roots");
                    Ccs->TiUnder = gcnew List<String^>();
                    if (ti->Has("under")) for each (String^ s in ti->GetStringList("under")) Ccs->TiUnder->Add(s == nullptr ? String::Empty : s->Trim());
                    Ccs->TiBin = ti->GetString("bin", String::Empty);
                    Ccs->TiLib = ti->GetString("lib", String::Empty);
                }
            }
            JsonValue^ co = doc->GetObject("compilerOptions");
            if (co != nullptr)
            {
                CompilerOptionsDescription = co->GetString("description", String::Empty);
                CompilerOptionTabs = ReadList(co, "tabs");
                JsonValue^ opts = co->GetObject("options");
                if (opts != nullptr)
                    for each (String^ id in opts->KeyNames)
                    {
                        JsonValue^ o = opts->GetObject(id);
                        if (o == nullptr || id == "description") continue;
                        CompilerOptionDefinition^ d = gcnew CompilerOptionDefinition(id);
                        d->Tab = o->GetString("tab", String::Empty);
                        d->Label = o->GetString("label", id);
                        d->Hint = o->GetString("hint", String::Empty);
                        d->Control = o->GetString("control", "check");
                        d->Choices = ReadList(o, "choices");
                        JsonValue^ def = o->Get("default");
                        if (def != nullptr && def->Kind == JsonKind::String) d->DefaultValue = def->AsString();
                        else if (def != nullptr && def->IsObject) ReadMap(o, "default", d->Defaults);
                        d->FlagOn = o->GetString("flagOn", String::Empty);
                        d->FlagOff = o->GetString("flagOff", String::Empty);
                        d->Flag = o->GetString("flag", String::Empty);
                        d->Silent = o->Has("silent") ? o->GetString("silent", String::Empty) : nullptr;
                        d->FlagEach = o->GetString("flagEach", String::Empty);
                        d->EnvironmentVariable = o->GetString("environment", String::Empty);
                        d->Needs = o->GetString("needs", String::Empty);
                        d->Unavailable = o->GetString("unavailable", String::Empty);
                        if (!CompilerOptionTabs->Contains(d->Tab) && d->Tab->Length > 0) CompilerOptionTabs->Add(d->Tab);
                        CompilerOptions->Add(d);
                    }
            }
            ReadMap(doc, "placeholders", Placeholders);
            JsonValue^ targets = doc->GetObject("targets");
            if (targets != nullptr)
                for each (String^ arch in targets->KeyNames)
                {
                    JsonValue^ t = targets->GetObject(arch);
                    if (t == nullptr || arch == "description") continue;
                    TargetDefinition^ d = gcnew TargetDefinition(arch);
                    d->IdeTarget = t->GetString("ideTarget", String::Empty);
                    if (d->IdeTarget->Length == 0) continue;
                    d->Name = t->GetString("name", arch);
                    d->Description = t->GetString("description", String::Empty);
                    d->Host = t->GetBool("host", false);
                    d->Emulated = t->GetBool("emulated", false);
                    d->DebugInfo = t->GetString("debugInfo", String::Empty);
                    d->AssemblyExtension = t->GetString("assemblyExtension", d->AssemblyExtension);
                    d->ObjectExtension = t->GetString("objectExtension", d->ObjectExtension);
                    d->ProgramExtension = t->GetString("programExtension", d->ProgramExtension);
                    d->EmulatedProgramExtension = t->GetString("emulatedProgramExtension", d->EmulatedProgramExtension);
                    d->LibraryDirFlag = t->GetString("libraryDirFlag", d->LibraryDirFlag);
                    d->LibraryFlag = t->GetString("libraryFlag", d->LibraryFlag);
                    ReadMap(t, "roles", d->Roles);
                    d->Runtime = t->GetString("runtime", String::Empty);
                    d->LinkerCommandFile = t->GetString("linkerCommandFile", String::Empty);
                    d->Run = t->GetString("run", d->Run);
                    d->Debug = t->GetString("debug", d->Debug);
                    d->ShownRun = t->GetString("shownRun", String::Empty);
                    d->ShownEmulate = t->GetString("shownEmulate", String::Empty);
                    Targets->Add(d);
                }
            JsonValue^ tools = doc->GetObject("tools");
            if (tools != nullptr)
                for each (String^ id in tools->KeyNames)
                {
                    JsonValue^ t = tools->GetObject(id);
                    if (t == nullptr || id == "description") continue;
                    ToolDefinition^ d = gcnew ToolDefinition(id);
                    d->Name = t->GetString("name", id);
                    d->Description = t->GetString("description", String::Empty);
                    d->Role = t->GetString("role", String::Empty);
                    d->Languages = ReadList(t, "languages");
                    d->Targets = ReadList(t, "targets");
                    d->Exe = t->GetString("exe", String::Empty);
                    d->Aliases = ReadList(t, "aliases");
                    d->Words = ReadList(t, "words");
                    JsonValue^ l = t->GetObject("lookup");
                    if (l != nullptr)
                    {
                        d->Lookup->Env = l->GetString("env", String::Empty);
                        d->Lookup->InstallDir = l->GetString("installDir", String::Empty);
                        d->Lookup->Dirs = ReadList(l, "dirs");
                        d->Lookup->OnPath = l->GetBool("path", true);
                        d->Lookup->Vcvars = l->GetBool("vcvars", false);
                        d->Lookup->Description = l->GetString("description", String::Empty);
                    }
                    JsonValue^ v = t->GetObject("version");
                    d->VersionArgs = v == nullptr ? nullptr : v->GetString("args", String::Empty);
                    d->OutputEncoding = t->GetString("outputEncoding", d->OutputEncoding);
                    d->Diagnostics = t->GetString("diagnostics", d->Diagnostics);
                    ReadMap(t, "environment", d->Environment);
                    d->ArchFlag = t->GetString("archFlag", String::Empty);
                    d->IncludeFlag = t->GetString("includeFlag", String::Empty);
                    d->DefineFlag = t->GetString("defineFlag", String::Empty);
                    d->InstallIncludeDirs = ReadList(t, "installIncludeDirs");
                    d->Options = ReadList(t, "options");
                    ReadMap(t, "languageFlags", d->LanguageFlags);
                    ReadMap(t, "configFlags", d->ConfigFlags);
                    ReadMap(t, "asmSyntaxFlags", d->AsmSyntaxFlags);
                    JsonValue^ by = t->GetObject("configFlagsByTarget");
                    if (by != nullptr)
                        for each (String^ arch in by->KeyNames)
                        {
                            if (arch == "description" || by->GetObject(arch) == nullptr) continue;
                            Dictionary<String^, String^>^ m = gcnew Dictionary<String^, String^>();
                            ReadMap(by, arch, m);
                            d->ConfigFlagsByTarget[arch] = m;
                        }
                    JsonValue^ ps = t->GetObject("projectSettings");
                    if (ps != nullptr)
                        for each (String^ name in ps->KeyNames)
                        {
                            if (name == "description" || ps->GetObject(name) == nullptr) continue;
                            Dictionary<String^, String^>^ m = gcnew Dictionary<String^, String^>(StringComparer::OrdinalIgnoreCase);
                            ReadMap(ps, name, m);
                            d->ProjectSettings[name] = m;
                            d->ProjectSettingOrder->Add(name);
                        }
                    JsonValue^ ops = t->GetObject("operations");
                    if (ops != nullptr)
                        for each (String^ op in ops->KeyNames)
                        {
                            JsonValue^ o = ops->GetObject(op);
                            if (o == nullptr || op == "description") continue;
                            OperationDefinition^ od = gcnew OperationDefinition();
                            od->Id = op;
                            od->Description = o->GetString("description", String::Empty);
                            od->Args = o->GetString("args", String::Empty);
                            od->Shown = o->GetString("shown", String::Empty);
                            od->Output = o->GetString("output", String::Empty);
                            ReadMap(o, "arguments", od->Arguments);
                            d->Operations[op] = od;
                            d->OperationOrder->Add(op);
                        }
                    JsonValue^ sess = t->Get("session");
                    if (sess != nullptr && sess->Kind == JsonKind::String)
                    {
                        d->Session = gcnew SessionDefinition();
                        d->Session->Like = sess->AsString();
                    }
                    else if (sess != nullptr && sess->IsObject)
                    {
                        d->Session = gcnew SessionDefinition();
                        d->Session->Description = sess->GetString("description", String::Empty);
                        d->Session->Dialect = sess->GetString("dialect", String::Empty);
                        d->Session->Like = sess->GetString("like", String::Empty);
                        d->Session->Marker = sess->GetString("marker", String::Empty);
                        d->Session->MarkerCommand = sess->GetString("markerCommand", String::Empty);
                        d->Session->Console = sess->GetBool("console", false);
                        d->Session->AfterReady = sess->GetString("afterReady", String::Empty);
                        d->Session->Preamble = ReadList(sess, "preamble");
                        ReadMap(sess, "commands", d->Session->Commands);
                        ReadMap(sess, "environment", d->Session->Environment);
                        ReadMap(sess, "protocol", d->Session->Protocol);
                    }
                    Tools->Add(d);
                }
            // a session that starts from another tool's: that one's fields, this one's over them
            for each (ToolDefinition^ d in Tools)
            {
                if (d->Session == nullptr || d->Session->Like->Length == 0) continue;
                ToolDefinition^ from = Tool(d->Session->Like);
                if (from == nullptr || from->Session == nullptr)
                {
                    Problems->Add(FileName + ": tool '" + d->Id + "': session 'like' names a tool '" + d->Session->Like + "' with no session - the session is its own fields alone");
                    continue;
                }
                SessionDefinition^ merged = from->Session->Clone();
                SessionDefinition^ own = d->Session;
                merged->Like = own->Like;
                if (own->Description->Length > 0) merged->Description = own->Description;
                if (own->Dialect->Length > 0) merged->Dialect = own->Dialect;
                if (own->Marker->Length > 0) merged->Marker = own->Marker;
                if (own->MarkerCommand->Length > 0) merged->MarkerCommand = own->MarkerCommand;
                if (own->Console) merged->Console = true;
                if (own->AfterReady->Length > 0) merged->AfterReady = own->AfterReady;
                if (own->Preamble->Count > 0) merged->Preamble = own->Preamble;
                for each (KeyValuePair<String^, String^> kv in own->Commands) merged->Commands[kv.Key] = kv.Value;
                for each (KeyValuePair<String^, String^> kv in own->Environment) merged->Environment[kv.Key] = kv.Value;
                for each (KeyValuePair<String^, String^> kv in own->Protocol) merged->Protocol[kv.Key] = kv.Value;
                d->Session = merged;
            }
            JsonValue^ libs = doc->GetObject("libraries");
            if (libs != nullptr)
                for each (String^ id in libs->KeyNames)
                {
                    JsonValue^ l = libs->GetObject(id);
                    if (l == nullptr || id == "description") continue;
                    LibraryDefinition^ d = gcnew LibraryDefinition(id);
                    d->Name = l->GetString("name", id);
                    d->Description = l->GetString("description", String::Empty);
                    d->Kind = l->GetString("kind", String::Empty);
                    d->Target = l->GetString("target", String::Empty);
                    d->Directory = l->GetString("directory", String::Empty);
                    d->DirectoryEnv = l->GetString("directoryEnv", String::Empty);
                    d->Marker = l->GetString("marker", String::Empty);
                    ReadMap(l, "files", d->Files);
                    d->Aliases = ReadList(l, "aliases");
                    ReadMap(l, "assemblyDirectories", d->AssemblyDirectories);
                    ReadMap(l, "assemblyDirectoryEnv", d->AssemblyDirectoryEnv);
                    d->When = l->GetString("when", d->When);
                    Libraries->Add(d);
                }
            JsonValue^ cmds = doc->GetObject("linkerCommandFiles");
            if (cmds != nullptr)
                for each (String^ id in cmds->KeyNames)
                {
                    JsonValue^ c = cmds->GetObject(id);
                    if (c == nullptr || id == "description") continue;
                    LinkerCommandFileDefinition^ d = gcnew LinkerCommandFileDefinition(id);
                    d->Name = c->GetString("name", id);
                    d->Description = c->GetString("description", String::Empty);
                    d->Lines = c->GetStringList("lines");
                    LinkerCommandFiles->Add(d);
                }
            // the targets' roles must name tools that are there
            for each (TargetDefinition^ t in Targets)
                for each (KeyValuePair<String^, String^> kv in t->Roles)
                    if (!String::IsNullOrEmpty(kv.Value) && Tool(kv.Value) == nullptr)
                        Problems->Add(FileName + ": target '" + t->Arch + "': role '" + kv.Key + "' names a tool '" + kv.Value + "' that is not defined - the role is empty");
        }
    };
}}
