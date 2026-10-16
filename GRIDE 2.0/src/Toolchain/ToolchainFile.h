#pragma once
// ============================================================================
//  GRIDE 2.0 - toolchain.json
//  Every tool, its arguments, the libraries and where things are looked for
//  come from the shipped file beside GRIDE.exe, with the user's file laid over
//  it field by field. No tool name stays in the code: IdeSettings' defaults,
//  the target table and the build engine ask here. The format is written up
//  in docs\TOOLCHAIN-FORMAT.md. GRIDEd reads the same file with the same
//  reader; the two IDEs share it.
// ============================================================================
#include "../Common/Json.h"

namespace Gride
{
    // one thing a tool does: the argument template and what each argument means
    public ref class ToolOperation
    {
    public:
        ToolOperation() { Id = String::Empty; Description = String::Empty; Args = String::Empty; Shown = String::Empty; Output = String::Empty; Arguments = gcnew Dictionary<String^, String^>(); }
        property String^ Id;
        property String^ Description;
        property String^ Args;
        property String^ Shown;
        property String^ Output;
        property Dictionary<String^, String^>^ Arguments;
    };

    public ref class ToolLookup
    {
    public:
        ToolLookup() { Env = String::Empty; InstallDir = String::Empty; OnPath = true; Vcvars = false; Description = String::Empty; }
        property String^ Env;
        property String^ InstallDir;
        property bool OnPath;
        property bool Vcvars;
        property String^ Description;
    };

    // how a debugger is driven on its standard input (the debugger revision reads it); for shalimar,
    // whose Debug build stops itself, the program's own session
    public ref class ToolSession
    {
    public:
        ToolSession()
        {
            Description = String::Empty; Dialect = String::Empty; Like = String::Empty; Marker = String::Empty; MarkerCommand = String::Empty;
            Console = false; AfterReady = String::Empty;
            Preamble = gcnew List<String^>(); Commands = gcnew Dictionary<String^, String^>();
            Environment = gcnew Dictionary<String^, String^>(); Protocol = gcnew Dictionary<String^, String^>();
        }
        property String^ Description;
        property String^ Dialect;
        property String^ Like;
        property String^ Marker;
        property String^ MarkerCommand;
        property bool Console;
        property String^ AfterReady;
        property List<String^>^ Preamble;
        property Dictionary<String^, String^>^ Commands;
        property Dictionary<String^, String^>^ Environment;
        property Dictionary<String^, String^>^ Protocol;

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
        ToolSession^ Clone()
        {
            ToolSession^ c = gcnew ToolSession();
            c->Description = Description; c->Dialect = Dialect; c->Like = Like; c->Marker = Marker; c->MarkerCommand = MarkerCommand;
            c->Console = Console; c->AfterReady = AfterReady;
            c->Preamble = gcnew List<String^>(Preamble);
            for each (KeyValuePair<String^, String^> kv in Commands) c->Commands[kv.Key] = kv.Value;
            for each (KeyValuePair<String^, String^> kv in Environment) c->Environment[kv.Key] = kv.Value;
            for each (KeyValuePair<String^, String^> kv in Protocol) c->Protocol[kv.Key] = kv.Value;
            return c;
        }
    };

    // a tool as the file defines it (the settings' ToolDefinition is the flat entry made from it)
    public ref class ToolEntry
    {
    public:
        ToolEntry(String^ id)
        {
            Id = id; Name = id; Description = String::Empty; Role = String::Empty;
            Languages = gcnew List<String^>(); Targets = gcnew List<String^>();
            Exe = String::Empty; Aliases = gcnew List<String^>(); Words = gcnew List<String^>();
            Lookup = gcnew ToolLookup(); VersionArgs = nullptr;
            OutputEncoding = "utf-8"; Diagnostics = "none";
            Environment = gcnew Dictionary<String^, String^>();
            ArchFlag = String::Empty; IncludeFlag = String::Empty; DefineFlag = String::Empty;
            InstallIncludeDirs = gcnew List<String^>();
            LanguageFlags = gcnew Dictionary<String^, String^>();
            ConfigFlags = gcnew Dictionary<String^, String^>();
            ConfigFlagsByTarget = gcnew Dictionary<String^, Dictionary<String^, String^>^>();
            AsmSyntaxFlags = gcnew Dictionary<String^, String^>();
            Operations = gcnew Dictionary<String^, ToolOperation^>();
            OperationOrder = gcnew List<String^>();
            Options = gcnew List<String^>();
            ProjectSettings = gcnew Dictionary<String^, Dictionary<String^, String^>^>(StringComparer::OrdinalIgnoreCase);
            ProjectSettingOrder = gcnew List<String^>();
            Session = nullptr;
        }
        property String^ Id;
        property String^ Name;
        property String^ Description;
        property String^ Role;
        property List<String^>^ Languages;
        property List<String^>^ Targets;
        property String^ Exe;
        property List<String^>^ Aliases;
        property List<String^>^ Words;
        property ToolLookup^ Lookup;
        property String^ VersionArgs;
        property String^ OutputEncoding;
        property String^ Diagnostics;
        property Dictionary<String^, String^>^ Environment;
        property String^ ArchFlag;
        property String^ IncludeFlag;
        property String^ DefineFlag;
        property List<String^>^ InstallIncludeDirs;
        property Dictionary<String^, String^>^ LanguageFlags;
        property Dictionary<String^, String^>^ ConfigFlags;
        property Dictionary<String^, Dictionary<String^, String^>^>^ ConfigFlagsByTarget;
        property Dictionary<String^, String^>^ AsmSyntaxFlags;
        property Dictionary<String^, ToolOperation^>^ Operations;
        property List<String^>^ OperationOrder;
        property List<String^>^ Options;
        // a Visual Studio project's own compiler settings and the switch the tool takes for each (cl's, R12 of
        // GRIDE 2.0 / GRIDEd's R13): MSBuild's ClCompile name -> value -> switch, "*" standing for any other value
        // ($(Value) filled in), names and values in any case; written in the order of ProjectSettingOrder
        property Dictionary<String^, Dictionary<String^, String^>^>^ ProjectSettings;
        property List<String^>^ ProjectSettingOrder;

        // the switch a setting's value maps to; "" when it is read and gives nothing; nullptr when the table does not name it
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
        property ToolSession^ Session;

        bool TakesOption(String^ id) { return Options->Contains(id); }

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

        ToolOperation^ Operation(String^ id)
        {
            ToolOperation^ op;
            return id != nullptr && Operations->TryGetValue(id, op) ? op : nullptr;
        }
        String^ OperationArgs(String^ id)
        {
            ToolOperation^ op = Operation(id);
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

    public ref class LibraryEntry
    {
    public:
        LibraryEntry(String^ id)
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
        property String^ Kind;
        property String^ Target;
        property String^ Directory;
        property String^ DirectoryEnv;
        property String^ Marker;
        property Dictionary<String^, String^>^ Files;
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

    public ref class TargetEntry
    {
    public:
        TargetEntry(String^ arch)
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
        property Dictionary<String^, String^>^ Roles;
        property String^ Runtime;
        property String^ LinkerCommandFile;
        property String^ Run;
        property String^ Debug;
        property String^ ShownRun;
        property String^ ShownEmulate;

        String^ ToolIdFor(String^ role)
        {
            String^ v;
            return Roles->TryGetValue(role, v) ? v : String::Empty;
        }
    };

    public ref class LinkerCommandFileEntry
    {
    public:
        LinkerCommandFileEntry(String^ id) { Id = id; Name = id; Description = String::Empty; Lines = gcnew List<String^>(); }
        property String^ Id;
        property String^ Name;
        property String^ Description;
        property List<String^>^ Lines;
        String^ Text() { return String::Join("\n", Lines) + "\n"; }
    };

    public ref class InstallationEntry
    {
    public:
        InstallationEntry()
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

    // where Visual Studio's tools and the Windows SDK are (the build engine's revision finds them without cmd.exe)
    public ref class VisualStudioEntry
    {
    public:
        VisualStudioEntry()
        {
            Description = String::Empty; Vswhere = String::Empty; VswhereArgs = String::Empty;
            DefaultRoots = gcnew List<String^>(); Editions = gcnew List<String^>(); Vcvars = String::Empty;
            MsvcTools = String::Empty; MsvcVersion = String::Empty; HostBin = String::Empty; Lib = String::Empty; Include = String::Empty; LlvmBin = String::Empty;
            KitRegistryKey = String::Empty; KitRegistryValue = String::Empty; KitRoots = gcnew List<String^>(); KitVersionsUnder = String::Empty;
            KitLibDirs = gcnew List<String^>(); KitIncludeDirs = gcnew List<String^>(); KitBinDir = String::Empty;
            Environment = gcnew Dictionary<String^, String^>();
        }
        property String^ Description;
        property String^ Vswhere;
        property String^ VswhereArgs;
        property List<String^>^ DefaultRoots;
        property List<String^>^ Editions;
        property String^ Vcvars;
        property String^ MsvcTools;
        property String^ MsvcVersion;       // the compiler version under msvcTools taken ("14.44": the newest 14.44.*), "" the newest
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
    // becomes on the command line, and when it applies (the settings revision draws it)
    public ref class CompilerOptionEntry
    {
    public:
        CompilerOptionEntry(String^ id)
        {
            Id = id; Tab = String::Empty; Label = id; Hint = String::Empty; Control = "check";
            Choices = gcnew List<String^>(); Defaults = gcnew Dictionary<String^, String^>(); DefaultValue = nullptr;
            FlagOn = String::Empty; FlagOff = String::Empty; Flag = String::Empty; Silent = nullptr; FlagEach = String::Empty;
            EnvironmentVariable = String::Empty; Needs = String::Empty; Unavailable = String::Empty;
        }
        property String^ Id;
        property String^ Tab;
        property String^ Label;
        property String^ Hint;
        property String^ Control;
        property List<String^>^ Choices;
        property Dictionary<String^, String^>^ Defaults;
        property String^ DefaultValue;
        property String^ FlagOn;
        property String^ FlagOff;
        property String^ Flag;
        property String^ Silent;
        property String^ FlagEach;
        property String^ EnvironmentVariable;
        property String^ Needs;
        property String^ Unavailable;

        bool IsCheck() { return String::Compare(Control, "check", true) == 0; }
        bool IsChoice() { return String::Compare(Control, "choice", true) == 0; }
        bool IsText() { return String::Compare(Control, "text", true) == 0; }

        // the default of a configuration: the one named for it, else the one for all, else the control's own
        String^ DefaultFor(String^ config)
        {
            String^ v;
            if (config != nullptr && Defaults->TryGetValue(config, v)) return v;
            if (DefaultValue != nullptr) return DefaultValue;
            if (IsChoice()) return Choices->Count > 0 ? Choices[0] : String::Empty;
            return IsCheck() ? Str("0") : String::Empty;
        }
        String^ SilentChoice() { return Silent != nullptr ? Silent : Choices->Count > 0 ? Choices[0] : String::Empty; }
        // the compiler word in front of the dot (c90.opt -> c90); general.* belongs to no one compiler
        String^ ToolWord()
        {
            int dot = Id->IndexOf(L'.');
            return dot > 0 ? Id->Substring(0, dot) : String::Empty;
        }
    };

    // reading CCS projects: TI's option definitions, which projects the toolchain builds, TI's runtime names (the CCS revision)
    public ref class CcsEntry
    {
    public:
        CcsEntry()
        {
            Description = String::Empty; DefinitionsDir = String::Empty; Definitions = gcnew Dictionary<String^, String^>();
            DeviceFamily = String::Empty; DeviceVariant = String::Empty; SiliconVersion = String::Empty;
            CompilerDirs = gcnew Dictionary<String^, String^>(); RuntimeIndex = String::Empty; RuntimeLibrary = String::Empty; RuntimeFallback = String::Empty;
            TiRoots = gcnew List<String^>(); TiUnder = gcnew List<String^>(); TiBin = String::Empty; TiLib = String::Empty;
        }
        property String^ Description;
        property String^ DefinitionsDir;
        property Dictionary<String^, String^>^ Definitions;
        property String^ DeviceFamily;
        property String^ DeviceVariant;
        property String^ SiliconVersion;
        property Dictionary<String^, String^>^ CompilerDirs;
        property String^ RuntimeIndex;
        property String^ RuntimeLibrary;
        property String^ RuntimeFallback;
        property List<String^>^ TiRoots;
        property List<String^>^ TiUnder;
        property String^ TiBin;
        property String^ TiLib;
        bool Defined() { return Definitions->Count > 0; }
    };

    public ref class ToolchainFile
    {
    public:
        literal String^ FileName = "toolchain.json";
        literal String^ UserFileNameBeside = "toolchain.user.json";
        literal String^ EnvironmentOverride = "GRIDE_TOOLCHAIN";

        // the definitions in force; loaded from the shipped and user files on first use
        static property ToolchainFile^ Current
        {
            ToolchainFile^ get()
            {
                if (current == nullptr) current = Load(ShippedPath(), UserPath());
                return current;
            }
            void set(ToolchainFile^ d) { current = d; }
        }

        // the shipped file: $GRIDE_TOOLCHAIN, beside the exe, else a source layout above an x64\<config> folder
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

        // the user's overrides: toolchain.user.json beside the exe when it is there, else %APPDATA%\GRIDE\toolchain.json
        static String^ UserPath()
        {
            String^ dir = ExeDirectory();
            if (dir != nullptr)
            {
                String^ beside = Path::Combine(dir, UserFileNameBeside);
                if (File::Exists(beside)) return beside;
            }
            return Path::Combine(System::Environment::GetFolderPath(System::Environment::SpecialFolder::ApplicationData), "GRIDE\\" + FileName);
        }

        static String^ ExeDirectory()
        {
            try { return AppDomain::CurrentDomain->BaseDirectory->TrimEnd(L'\\'); }
            catch (Exception^) { return nullptr; }
        }

        // reads both files; never throws - every fault is a line in Problems and the entry falls back
        static ToolchainFile^ Load(String^ shippedPath, String^ userPath)
        {
            ToolchainFile^ d = gcnew ToolchainFile();
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

        // reads the files again. The user's file is looked for again when it was in one of its two places, so a
        // toolchain.user.json made beside the exe while GRIDE runs is read (and one removed gives way to
        // %APPDATA%\GRIDE\toolchain.json); a file named outright (Load) stays the one read
        static ToolchainFile^ Reload()
        {
            ToolchainFile^ was = current;
            String^ user = was == nullptr || IsUserFilePlace(was->UserFile) ? UserPath() : was->UserFile;
            current = Load(was == nullptr ? ShippedPath() : was->ShippedFile, user);
            return current;
        }

        // toolchain.user.json beside the exe or %APPDATA%\GRIDE\toolchain.json (empty: none named, looked for too)
        static bool IsUserFilePlace(String^ path)
        {
            if (String::IsNullOrEmpty(path)) return true;
            String^ dir = ExeDirectory();
            array<String^>^ places = gcnew array<String^>{
                dir == nullptr ? nullptr : Path::Combine(dir, UserFileNameBeside),
                Path::Combine(System::Environment::GetFolderPath(System::Environment::SpecialFolder::ApplicationData), "GRIDE\\" + FileName) };
            for each (String^ place in places)
            {
                if (place == nullptr) continue;
                try { if (String::Equals(Path::GetFullPath(place), Path::GetFullPath(path), StringComparison::OrdinalIgnoreCase)) return true; }
                catch (Exception^) { }
            }
            return false;
        }

        property String^ ShippedFile;
        property String^ UserFile;
        property JsonValue^ ShippedJson;
        property JsonValue^ UserJson;
        property JsonValue^ MergedJson;
        property List<String^>^ Problems;
        property int Format;
        property String^ Description;
        property InstallationEntry^ Installation;
        property VisualStudioEntry^ VisualStudio;
        property CcsEntry^ Ccs;
        property List<TargetEntry^>^ Targets;
        property List<ToolEntry^>^ Tools;
        property List<LibraryEntry^>^ Libraries;
        property List<LinkerCommandFileEntry^>^ LinkerCommandFiles;
        property Dictionary<String^, String^>^ Placeholders;
        property List<String^>^ PlaceholderOrder;
        property bool ShippedFound;
        property String^ CompilerOptionsDescription;
        property List<String^>^ CompilerOptionTabs;
        property List<CompilerOptionEntry^>^ CompilerOptions;

        property bool UserFileExists { bool get() { return UserFile->Length > 0 && File::Exists(UserFile); } }

        CompilerOptionEntry^ CompilerOption(String^ id)
        {
            if (id == nullptr) return nullptr;
            for each (CompilerOptionEntry^ o in CompilerOptions) if (o->Id == id) return o;
            return nullptr;
        }
        List<CompilerOptionEntry^>^ CompilerOptionsOf(String^ tab)
        {
            List<CompilerOptionEntry^>^ r = gcnew List<CompilerOptionEntry^>();
            for each (CompilerOptionEntry^ o in CompilerOptions) if (String::Compare(o->Tab, tab, true) == 0) r->Add(o);
            return r;
        }
        ToolEntry^ ToolOfOption(String^ id)
        {
            CompilerOptionEntry^ o = CompilerOption(id);
            ToolEntry^ t = o == nullptr ? nullptr : ToolByWord(o->ToolWord());
            if (t != nullptr) return t;
            for each (ToolEntry^ d in Tools) if (d->TakesOption(id)) return d;
            return nullptr;
        }

        TargetEntry^ TargetByArch(String^ arch)
        {
            if (arch == nullptr) return nullptr;
            for each (TargetEntry^ t in Targets) if (String::Compare(t->Arch, arch->Trim(), true) == 0) return t;
            return nullptr;
        }
        TargetEntry^ TargetByIde(String^ ideName)
        {
            if (ideName == nullptr) return nullptr;
            for each (TargetEntry^ t in Targets) if (String::Compare(t->IdeTarget, ideName, true) == 0) return t;
            return nullptr;
        }
        TargetEntry^ HostTarget()
        {
            for each (TargetEntry^ t in Targets) if (t->Host) return t;
            return nullptr;
        }
        ToolEntry^ Tool(String^ id)
        {
            if (id == nullptr) return nullptr;
            for each (ToolEntry^ t in Tools) if (t->Id == id) return t;
            return nullptr;
        }
        // the first tool of a role (the compilers are the same on every target)
        ToolEntry^ ToolByRole(String^ role)
        {
            if (role == nullptr) return nullptr;
            for each (ToolEntry^ t in Tools) if (t->Role == role) return t;
            return nullptr;
        }
        ToolEntry^ ToolForArch(String^ arch, String^ role)
        {
            TargetEntry^ t = TargetByArch(arch);
            return t == nullptr ? nullptr : Tool(t->ToolIdFor(role));
        }
        // the tool a project file's word names
        ToolEntry^ ToolByWord(String^ word)
        {
            if (String::IsNullOrWhiteSpace(word)) return nullptr;
            String^ w = word->Trim();
            for each (ToolEntry^ t in Tools)
                for each (String^ x in t->Words) if (String::Compare(x, w, true) == 0) return t;
            return nullptr;
        }
        // the tool a program name (exe or alias, with or without a folder) belongs to
        ToolEntry^ ToolByName(String^ exeOrPath)
        {
            for each (ToolEntry^ t in Tools) if (t->HasName(exeOrPath)) return t;
            return nullptr;
        }
        ToolEntry^ CompilerOf(String^ languageWord)
        {
            for each (String^ role in gcnew array<String^>{ "cCompiler", "cppCompiler", "shalimarCompiler" })
            {
                ToolEntry^ t = ToolByRole(role);
                if (t != nullptr && t->CompilesLanguage(languageWord)) return t;
            }
            return nullptr;
        }
        LibraryEntry^ Library(String^ id)
        {
            if (String::IsNullOrEmpty(id)) return nullptr;
            for each (LibraryEntry^ l in Libraries) if (l->Id == id) return l;
            return nullptr;
        }
        LibraryEntry^ LibraryByKind(String^ arch, String^ kind)
        {
            for each (LibraryEntry^ l in Libraries)
                if (l->Kind == kind && (String::IsNullOrEmpty(arch) || String::Compare(l->Target, arch, true) == 0)) return l;
            return nullptr;
        }
        LinkerCommandFileEntry^ LinkerCommandFile(String^ id)
        {
            if (String::IsNullOrEmpty(id)) return nullptr;
            for each (LinkerCommandFileEntry^ f in LinkerCommandFiles) if (f->Id == id) return f;
            return nullptr;
        }
        String^ Placeholder(String^ name)
        {
            String^ v;
            return Placeholders->TryGetValue(name, v) ? v : String::Empty;
        }

        // ---- the user's overrides
        bool IsOverridden(String^ section, String^ id)
        {
            if (UserJson == nullptr) return false;
            JsonValue^ s = UserJson->GetObject(section);
            return s != nullptr && s->Has(id);
        }
        JsonValue^ UserEntry(String^ section, String^ id)
        {
            if (UserJson == nullptr) UserJson = JsonValue::MakeObject();
            JsonValue^ s = UserJson->GetObject(section);
            if (s == nullptr) { s = JsonValue::MakeObject(); UserJson->SetValue(section, s); }
            JsonValue^ e = s->GetObject(id);
            if (e == nullptr) { e = JsonValue::MakeObject(); s->SetValue(id, e); }
            return e;
        }
        // replaces the user's entry with the fields given (nothing means: as shipped)
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

        // writes the user's file (removes it when nothing is overridden)
        bool SaveUser(String^% error)
        {
            error = nullptr;
            try
            {
                if (String::IsNullOrEmpty(UserFile)) { error = "no user file is named"; return false; }
                if (UserJson == nullptr || UserJson->Count == 0 || OnlyHeader(UserJson))
                {
                    if (File::Exists(UserFile)) File::Delete(UserFile);
                }
                else
                {
                    if (!UserJson->Has("format")) UserJson->SetNumber("format", Format > 0 ? Format : 1);
                    if (!UserJson->Has("description")) UserJson->SetString("description", UserHeader);
                    JsonWriter::WriteFile(UserFile, UserJson);
                }
                return true;
            }
            catch (Exception^ ex) { error = ex->Message; return false; }
        }

        // an empty user file to start from (Tools > Options > Definitions > Edit user overrides)
        literal String^ UserHeader = "the user's overrides of the shipped toolchain.json: a field named here wins, every other field keeps the shipped value";
        static bool WriteEmptyUserFile(String^ path, String^% error)
        {
            error = nullptr;
            try
            {
                JsonValue^ v = JsonValue::MakeObject();
                v->SetNumber("format", 1);
                v->SetString("description", UserHeader);
                JsonWriter::WriteFile(path, v);
                return true;
            }
            catch (Exception^ ex) { error = ex->Message; return false; }
        }

        static array<String^>^ Roles()
        {
            return gcnew array<String^>{ "cCompiler", "cppCompiler", "shalimarCompiler", "hostCompiler", "hostCxx", "assembler", "linker",
                                         "debugAssembler", "debugLinker", "nativeAssembler", "nativeLinker", "simulator", "emulator", "debugger", "fallbackDebugger", "converter" };
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
        static ToolchainFile^ current;

        ToolchainFile()
        {
            Problems = gcnew List<String^>();
            Format = 0; Description = String::Empty;
            Installation = gcnew InstallationEntry();
            VisualStudio = gcnew VisualStudioEntry();
            Ccs = gcnew CcsEntry();
            Targets = gcnew List<TargetEntry^>();
            Tools = gcnew List<ToolEntry^>();
            Libraries = gcnew List<LibraryEntry^>();
            LinkerCommandFiles = gcnew List<LinkerCommandFileEntry^>();
            Placeholders = gcnew Dictionary<String^, String^>();
            PlaceholderOrder = gcnew List<String^>();
            ShippedFound = false;
            ShippedFile = String::Empty; UserFile = String::Empty;
            CompilerOptionsDescription = String::Empty;
            CompilerOptionTabs = gcnew List<String^>();
            CompilerOptions = gcnew List<CompilerOptionEntry^>();
        }

        static bool OnlyHeader(JsonValue^ v)
        {
            for each (String^ k in v->KeyNames) if (k != "format" && k != "description") return false;
            return true;
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
        // value (or the default) stands in
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

        // ---- the typed model from the merged document
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
                        CompilerOptionEntry^ d = gcnew CompilerOptionEntry(id);
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
            JsonValue^ ph = doc->GetObject("placeholders");
            if (ph != nullptr)
                for each (String^ k in ph->KeyNames)
                {
                    if (k == "description") continue;
                    Placeholders[k] = ph->GetString(k, String::Empty);
                    PlaceholderOrder->Add(k);
                }
            JsonValue^ targets = doc->GetObject("targets");
            if (targets != nullptr)
                for each (String^ arch in targets->KeyNames)
                {
                    JsonValue^ t = targets->GetObject(arch);
                    if (t == nullptr || arch == "description") continue;
                    TargetEntry^ d = gcnew TargetEntry(arch);
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
                    ToolEntry^ d = gcnew ToolEntry(id);
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
                            ToolOperation^ od = gcnew ToolOperation();
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
                        d->Session = gcnew ToolSession();
                        d->Session->Like = sess->AsString();
                    }
                    else if (sess != nullptr && sess->IsObject)
                    {
                        d->Session = gcnew ToolSession();
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
            for each (ToolEntry^ d in Tools)
            {
                if (d->Session == nullptr || d->Session->Like->Length == 0) continue;
                ToolEntry^ from = Tool(d->Session->Like);
                if (from == nullptr || from->Session == nullptr)
                {
                    Problems->Add(FileName + ": tool '" + d->Id + "': session 'like' names a tool '" + d->Session->Like + "' with no session - the session is its own fields alone");
                    continue;
                }
                ToolSession^ merged = from->Session->Clone();
                ToolSession^ own = d->Session;
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
                    LibraryEntry^ d = gcnew LibraryEntry(id);
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
                    LinkerCommandFileEntry^ d = gcnew LinkerCommandFileEntry(id);
                    d->Name = c->GetString("name", id);
                    d->Description = c->GetString("description", String::Empty);
                    d->Lines = c->GetStringList("lines");
                    LinkerCommandFiles->Add(d);
                }
            // the targets' roles must name tools that are there
            for each (TargetEntry^ t in Targets)
                for each (KeyValuePair<String^, String^> kv in t->Roles)
                    if (!String::IsNullOrEmpty(kv.Value) && Tool(kv.Value) == nullptr)
                        Problems->Add(FileName + ": target '" + t->Arch + "': role '" + kv.Key + "' names a tool '" + kv.Value + "' that is not defined - the role is empty");
        }
    };
}
