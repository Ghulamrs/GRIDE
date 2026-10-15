// =============================================================================
//  VsSolution.h - Visual Studio solutions and C++ projects, read only
//
//    .sln      Visual Studio 2005 .. 2022 solutions: the projects, the solution folders
//              and their items, the dependencies, the configurations and platforms and
//              what each project builds for them
//    .vcxproj  Visual Studio 2010 and later (MSBuild): the configurations, the property
//              and item-definition groups with their conditions, the items (wildcards
//              expanded), the exclusions per configuration, the .filters beside it
//    .vcproj   Visual Studio 2005 / 2008 (the classic XML)
//
//  What is read is what cppparse's ProjectReader reads (the files, the include
//  directories, the definitions, the configurations, the version), and what the
//  explorer and a build need beyond that. Nothing is ever written to these files.
// =============================================================================
#pragma once
#include "../Core/PathUtil.h"

namespace Gride { namespace Model {

    using namespace System::Xml;
    using namespace System::Text::RegularExpressions;
    using namespace Gride::Core;

    // ---- one Project(...) line of a .sln, with its sections ----------------------------------
    public ref class VsSolutionEntry
    {
    public:
        VsSolutionEntry()
        {
            Name = String::Empty; Path = String::Empty; RelativePath = String::Empty; Guid = String::Empty; TypeGuid = String::Empty;
            ParentGuid = String::Empty; Kind = "other"; Dependencies = gcnew List<String^>(); Items = gcnew List<String^>();
            ConfigMap = gcnew Dictionary<String^, String^>(StringComparer::OrdinalIgnoreCase); Builds = gcnew HashSet<String^>(StringComparer::OrdinalIgnoreCase);
        }
        property String^ Name;
        property String^ Path;               // absolute; "" for a solution folder
        property String^ RelativePath;       // as the .sln wrote it
        property String^ Guid;               // without braces, upper case
        property String^ TypeGuid;
        property String^ ParentGuid;         // the solution folder it is nested in, or ""
        property String^ Kind;               // vcxproj / vcproj / folder / other
        property List<String^>^ Dependencies;    // GUIDs (ProjectDependencies)
        property List<String^>^ Items;           // a solution folder's files, absolute
        property Dictionary<String^, String^>^ ConfigMap;   // solution configuration -> the project's (ActiveCfg)
        property HashSet<String^>^ Builds;                  // the solution configurations the project builds in (Build.0)

        property bool IsFolder { bool get() { return Kind == "folder"; } }
        property bool IsCpp { bool get() { return Kind == "vcxproj" || Kind == "vcproj"; } }
    };

    // ---- the .sln ------------------------------------------------------------------------------
    public ref class VsSolution
    {
    public:
        literal String^ CppTypeGuid = "8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942";
        literal String^ FolderTypeGuid = "2150E333-8FDC-42A3-9474-1A3956D46DE8";

        VsSolution()
        {
            Path = String::Empty; Dir = String::Empty; Name = String::Empty; FormatVersion = String::Empty; VisualStudioVersion = String::Empty;
            Entries = gcnew List<VsSolutionEntry^>(); Configurations = gcnew List<String^>(); Notes = gcnew List<String^>();
        }
        property String^ Path;
        property String^ Dir;
        property String^ Name;
        property String^ FormatVersion;          // 12.00
        property String^ VisualStudioVersion;    // "# Visual Studio Version 17", "VisualStudioVersion = 17.0..."
        property List<VsSolutionEntry^>^ Entries;
        property List<String^>^ Configurations;  // "Debug|x64" ...
        property List<String^>^ Notes;

        static bool IsSolution(String^ path) { return path != nullptr && PathUtil::HasExtension(path, ".sln"); }

        // "Visual Studio 2010 (format 11.00)"
        String^ Version()
        {
            String^ v = VisualStudioVersion->Length > 0 ? VisualStudioVersion : "Visual Studio";
            return FormatVersion->Length > 0 ? v + " (format " + FormatVersion + ")" : v;
        }

        List<VsSolutionEntry^>^ CppProjects()
        {
            List<VsSolutionEntry^>^ r = gcnew List<VsSolutionEntry^>();
            for each (VsSolutionEntry^ e in Entries) if (e->IsCpp) r->Add(e);
            return r;
        }
        VsSolutionEntry^ ByGuid(String^ guid)
        {
            String^ g = CleanGuid(guid);
            for each (VsSolutionEntry^ e in Entries) if (e->Guid == g) return e;
            return nullptr;
        }
        // the solution folders an entry sits in, outermost first: "Libs/Inner"
        String^ FolderPathOf(VsSolutionEntry^ e)
        {
            List<String^>^ chain = gcnew List<String^>();
            VsSolutionEntry^ at = e;
            int guard = 0;
            while (at != nullptr && at->ParentGuid->Length > 0 && guard++ < 32)
            {
                at = ByGuid(at->ParentGuid);
                if (at != nullptr) chain->Insert(0, at->Name);
            }
            return String::Join("/", chain);
        }
        // every solution folder path, outer ones first
        List<String^>^ FolderPaths()
        {
            List<String^>^ r = gcnew List<String^>();
            for each (VsSolutionEntry^ e in Entries)
                if (e->IsFolder)
                {
                    String^ own = FolderPathOf(e);
                    r->Add(own->Length > 0 ? own + "/" + e->Name : e->Name);
                }
            return r;
        }

        static String^ CleanGuid(String^ g) { return g == nullptr ? String::Empty : g->Trim()->Trim(L'{', L'}')->ToUpperInvariant(); }

        static VsSolution^ Read(String^ path)
        {
            VsSolution^ s = gcnew VsSolution();
            s->Path = System::IO::Path::GetFullPath(path);
            s->Dir = System::IO::Path::GetDirectoryName(s->Path);
            s->Name = System::IO::Path::GetFileNameWithoutExtension(s->Path);
            array<String^>^ lines = File::ReadAllLines(s->Path);
            Regex^ projectLine = gcnew Regex("^Project\\(\"\\{?([^\"}]+)\\}?\"\\)\\s*=\\s*\"([^\"]*)\"\\s*,\\s*\"([^\"]*)\"\\s*,\\s*\"\\{?([^\"}]+)\\}?\"", RegexOptions::IgnoreCase);
            VsSolutionEntry^ current = nullptr;
            String^ section = String::Empty;      // the ProjectSection or GlobalSection being read
            for each (String^ raw in lines)
            {
                String^ line = raw->Trim();
                if (line->Length == 0) continue;
                if (line->StartsWith("Microsoft Visual Studio Solution File", StringComparison::OrdinalIgnoreCase))
                {
                    int at = line->IndexOf("Format Version", StringComparison::OrdinalIgnoreCase);
                    s->FormatVersion = at < 0 ? String::Empty : line->Substring(at + 14)->Trim();
                    continue;
                }
                if (line->StartsWith("# Visual Studio")) { if (s->VisualStudioVersion->Length == 0) s->VisualStudioVersion = line->Substring(2)->Trim(); continue; }
                if (line->StartsWith("VisualStudioVersion", StringComparison::OrdinalIgnoreCase))
                {
                    int eq = line->IndexOf(L'=');
                    if (eq > 0) s->VisualStudioVersion = (s->VisualStudioVersion->Length > 0 ? s->VisualStudioVersion + ", " : String::Empty) + "version " + line->Substring(eq + 1)->Trim();
                    continue;
                }
                Match^ m = projectLine->Match(line);
                if (m->Success)
                {
                    current = gcnew VsSolutionEntry();
                    current->TypeGuid = CleanGuid(m->Groups[1]->Value);
                    current->Name = m->Groups[2]->Value;
                    current->RelativePath = m->Groups[3]->Value;
                    current->Guid = CleanGuid(m->Groups[4]->Value);
                    if (current->TypeGuid == FolderTypeGuid) current->Kind = "folder";
                    else
                    {
                        String^ ext = System::IO::Path::GetExtension(current->RelativePath)->ToLowerInvariant();
                        current->Kind = ext == ".vcxproj" ? "vcxproj" : ext == ".vcproj" ? "vcproj" : "other";
                        current->Path = PathUtil::MakeAbsolute(s->Dir, current->RelativePath);
                        if (current->Kind == "other")
                            s->Notes->Add("skipped non-C++ project \"" + current->Name + "\" (" + current->RelativePath + ")");
                    }
                    s->Entries->Add(current);
                    continue;
                }
                if (line->StartsWith("EndProject", StringComparison::OrdinalIgnoreCase) && !line->StartsWith("EndProjectSection", StringComparison::OrdinalIgnoreCase)) { current = nullptr; section = String::Empty; continue; }
                if (line->StartsWith("ProjectSection(", StringComparison::OrdinalIgnoreCase) || line->StartsWith("GlobalSection(", StringComparison::OrdinalIgnoreCase))
                {
                    int open = line->IndexOf(L'('), close = line->IndexOf(L')');
                    section = open >= 0 && close > open ? line->Substring(open + 1, close - open - 1) : String::Empty;
                    continue;
                }
                if (line->StartsWith("EndProjectSection", StringComparison::OrdinalIgnoreCase) || line->StartsWith("EndGlobalSection", StringComparison::OrdinalIgnoreCase)) { section = String::Empty; continue; }
                if (section->Length == 0) continue;
                int eq = line->IndexOf(L'=');
                if (eq < 0) continue;
                String^ key = line->Substring(0, eq)->Trim();
                String^ value = line->Substring(eq + 1)->Trim();
                if (current != nullptr && String::Compare(section, "ProjectDependencies", true) == 0) current->Dependencies->Add(CleanGuid(key));
                else if (current != nullptr && String::Compare(section, "SolutionItems", true) == 0) current->Items->Add(PathUtil::MakeAbsolute(s->Dir, key));
                else if (String::Compare(section, "SolutionConfigurationPlatforms", true) == 0) { if (!s->Configurations->Contains(key)) s->Configurations->Add(key); }
                else if (String::Compare(section, "ProjectConfigurationPlatforms", true) == 0)
                {
                    // {guid}.Debug|x64.ActiveCfg = Debug|x64 ; {guid}.Debug|x64.Build.0 = Debug|x64
                    int dot = key->IndexOf(L'.');
                    if (dot < 0) continue;
                    VsSolutionEntry^ e = s->ByGuid(key->Substring(0, dot));
                    String^ rest = key->Substring(dot + 1);
                    if (e == nullptr) continue;
                    if (rest->EndsWith(".ActiveCfg", StringComparison::OrdinalIgnoreCase)) e->ConfigMap[rest->Substring(0, rest->Length - 10)] = value;
                    else if (rest->EndsWith(".Build.0", StringComparison::OrdinalIgnoreCase)) e->Builds->Add(rest->Substring(0, rest->Length - 8));
                }
                else if (String::Compare(section, "NestedProjects", true) == 0)
                {
                    VsSolutionEntry^ child = s->ByGuid(key);
                    if (child != nullptr) child->ParentGuid = CleanGuid(value);
                }
            }
            if (s->Entries->Count == 0) s->Notes->Add("no project in " + s->Path);
            return s;
        }
    };

    // ---- a project's item (a file) ---------------------------------------------------------------
    public ref class VsProjectItem
    {
    public:
        VsProjectItem() { Include = String::Empty; Path = String::Empty; Type = String::Empty; Filter = String::Empty; FromWildcard = false; Excluded = gcnew HashSet<String^>(StringComparer::OrdinalIgnoreCase); Settings = gcnew List<String^>(); }
        property String^ Include;        // as written (the pattern, for a wildcard item)
        property String^ Path;           // absolute
        property String^ Type;           // ClCompile, ClInclude, None, ResourceCompile, Text, ...
        property String^ Filter;         // the .filters folder, "" for the root
        property bool FromWildcard;
        property HashSet<String^>^ Excluded;   // the configurations ("Debug|x64") it is excluded from the build in; "*" for all
        property List<String^>^ Settings;      // per-file settings not mapped (PrecompiledHeader ...)

        bool ExcludedIn(String^ config) { return Excluded->Contains("*") || (config != nullptr && Excluded->Contains(config)); }
    };

    // ---- one configuration|platform of a project ----------------------------------------------------
    public ref class VsProjectConfig
    {
    public:
        VsProjectConfig(String^ name)
        {
            Name = name;
            int bar = name->IndexOf(L'|');
            Configuration = bar < 0 ? name : name->Substring(0, bar);
            Platform = bar < 0 ? String::Empty : name->Substring(bar + 1);
            IncludeDirs = gcnew List<String^>(); Defines = gcnew List<String^>(); Undefines = gcnew List<String^>();
            Libs = gcnew List<String^>(); LibDirs = gcnew List<String^>(); Notes = gcnew List<String^>();
            Optimization = String::Empty; AdditionalOptions = String::Empty; OutDir = String::Empty; IntDir = String::Empty; TargetName = String::Empty;
            ConfigurationType = String::Empty; Toolset = String::Empty; CharacterSet = String::Empty; CompileAs = String::Empty; SubSystem = String::Empty;
            ClrSupport = false; UseDebugLibraries = nullptr;
            ClSettings = gcnew Dictionary<String^, String^>(StringComparer::OrdinalIgnoreCase);
        }
        property String^ Name;               // Debug|x64
        property String^ Configuration;      // Debug
        property String^ Platform;           // x64
        property List<String^>^ IncludeDirs; // absolute, macros expanded
        property List<String^>^ Defines;
        property List<String^>^ Undefines;
        property List<String^>^ Libs;        // AdditionalDependencies
        property List<String^>^ LibDirs;     // AdditionalLibraryDirectories, absolute
        property String^ Optimization;       // Disabled / MinSpace / MaxSpeed / Full, or "" when not set
        property String^ AdditionalOptions;
        property String^ OutDir;
        property String^ IntDir;
        property String^ TargetName;
        property String^ ConfigurationType;  // Application / StaticLibrary / DynamicLibrary
        property String^ Toolset;
        property String^ CharacterSet;
        property String^ CompileAs;          // CompileAsC / CompileAsCpp / Default
        property String^ SubSystem;
        property bool ClrSupport;
        property String^ UseDebugLibraries;  // "true" / "false" / null when not said
        // the rest of the ClCompile settings, MSBuild's names and values (WarningLevel -> Level4, RuntimeLibrary ->
        // MultiThreaded ...), the last group's winning: cl takes them through toolchain.json's projectSettings
        property Dictionary<String^, String^>^ ClSettings;
        property List<String^>^ Notes;       // what was seen and not mapped, for this configuration

        property bool IsDebugLike { bool get() { return String::Compare(Configuration, "Debug", true) == 0 || (UseDebugLibraries != nullptr && String::Compare(UseDebugLibraries, "true", true) == 0); } }
    };

    // ---- the project file ------------------------------------------------------------------------------
    public ref class VsProject
    {
    public:
        VsProject()
        {
            Path = String::Empty; Dir = String::Empty; Name = String::Empty; Kind = String::Empty; Guid = String::Empty; ToolsVersion = String::Empty;
            Toolset = String::Empty; Version = String::Empty; SolutionDir = String::Empty; FiltersFile = String::Empty;
            Configurations = gcnew List<VsProjectConfig^>(); Items = gcnew List<VsProjectItem^>(); Filters = gcnew List<String^>(); Notes = gcnew List<String^>();
            References = gcnew List<String^>(); ProjectReferences = gcnew List<String^>();
        }
        property String^ Path;
        property String^ Dir;
        property String^ Name;
        property String^ Kind;               // vcxproj / vcproj
        property String^ Guid;
        property String^ ToolsVersion;
        property String^ Toolset;            // the first configuration's PlatformToolset
        property String^ Version;            // "Visual Studio 2022, toolset v143"
        property String^ SolutionDir;        // for $(SolutionDir); "" when the project was opened alone
        property String^ FiltersFile;        // the .filters read, or ""
        property List<VsProjectConfig^>^ Configurations;
        property List<VsProjectItem^>^ Items;
        property List<String^>^ Filters;     // the filter paths, as written (Source\UI)
        property List<String^>^ Notes;       // what is in the file and not mapped
        property List<String^>^ References;          // assembly references (C++/CLI)
        property List<String^>^ ProjectReferences;   // other projects referenced

        static bool IsProjectFile(String^ path)
        {
            return path != nullptr && (PathUtil::HasExtension(path, ".vcxproj") || PathUtil::HasExtension(path, ".vcproj"));
        }

        VsProjectConfig^ Config(String^ name)
        {
            for each (VsProjectConfig^ c in Configurations) if (String::Compare(c->Name, name, true) == 0) return c;
            return nullptr;
        }
        List<String^>^ Platforms()
        {
            List<String^>^ r = gcnew List<String^>();
            for each (VsProjectConfig^ c in Configurations) if (!r->Contains(c->Platform)) r->Add(c->Platform);
            return r;
        }
        List<String^>^ ConfigurationNames()
        {
            List<String^>^ r = gcnew List<String^>();
            for each (VsProjectConfig^ c in Configurations) if (!r->Contains(c->Configuration)) r->Add(c->Configuration);
            return r;
        }
        List<VsProjectItem^>^ ItemsOfType(String^ type)
        {
            List<VsProjectItem^>^ r = gcnew List<VsProjectItem^>();
            for each (VsProjectItem^ i in Items) if (String::Compare(i->Type, type, true) == 0) r->Add(i);
            return r;
        }

        // cppparse's describeToolsVersion
        static String^ DescribeToolsVersion(String^ toolsVersion, String^ toolset)
        {
            String^ vs = String::Empty;
            String^ ts = toolset == nullptr ? String::Empty : toolset->ToLowerInvariant();
            if (ts == "v90") vs = "Visual Studio 2008";
            else if (ts == "v100") vs = "Visual Studio 2010";
            else if (ts == "v110") vs = "Visual Studio 2012";
            else if (ts == "v120") vs = "Visual Studio 2013";
            else if (ts == "v140") vs = "Visual Studio 2015";
            else if (ts == "v141") vs = "Visual Studio 2017";
            else if (ts == "v142") vs = "Visual Studio 2019";
            else if (ts == "v143") vs = "Visual Studio 2022";
            else if (ts == "v145") vs = "Visual Studio 2026";
            if (vs->Length == 0)
            {
                if (toolsVersion == "4.0") vs = "Visual Studio 2010/2012";
                else if (toolsVersion == "12.0") vs = "Visual Studio 2013";
                else if (toolsVersion == "14.0") vs = "Visual Studio 2015";
                else if (toolsVersion == "15.0") vs = "Visual Studio 2017";
                else if (toolsVersion == "Current") vs = "Visual Studio 2019 or later";
                else vs = "Visual Studio (unknown version)";
            }
            String^ out = vs;
            if (!String::IsNullOrEmpty(toolsVersion)) out += ", ToolsVersion " + toolsVersion;
            if (!String::IsNullOrEmpty(toolset)) out += ", toolset " + toolset;
            return out;
        }

        // MSBuild's macros a project file uses for its own paths; an unknown one is left as written
        String^ ExpandMacros(String^ s, VsProjectConfig^ c)
        {
            if (String::IsNullOrEmpty(s)) return String::Empty;
            String^ out = s;
            out = ReplaceMacro(out, "ProjectDir", Dir + "\\");
            out = ReplaceMacro(out, "MSBuildProjectDirectory", Dir);
            out = ReplaceMacro(out, "MSBuildThisFileDirectory", Dir + "\\");
            out = ReplaceMacro(out, "ProjectName", Name);
            out = ReplaceMacro(out, "ProjectPath", Path);
            out = ReplaceMacro(out, "TargetName", c != nullptr && c->TargetName->Length > 0 && !c->TargetName->Contains("$(") ? c->TargetName : Name);
            if (SolutionDir->Length > 0)
            {
                out = ReplaceMacro(out, "SolutionDir", SolutionDir->TrimEnd(L'\\') + "\\");
                out = ReplaceMacro(out, "SolutionName", System::IO::Path::GetFileName(SolutionDir->TrimEnd(L'\\')));
            }
            if (c != nullptr)
            {
                out = ReplaceMacro(out, "Configuration", c->Configuration);
                out = ReplaceMacro(out, "ConfigurationName", c->Configuration);
                out = ReplaceMacro(out, "Platform", c->Platform);
                out = ReplaceMacro(out, "PlatformName", c->Platform);
            }
            return out;
        }
        static bool HasMacro(String^ s) { return s != nullptr && s->Contains("$("); }

        // ---- reading ----
        static VsProject^ Read(String^ path, String^ solutionDir)
        {
            VsProject^ p = gcnew VsProject();
            p->Path = System::IO::Path::GetFullPath(path);
            p->Dir = System::IO::Path::GetDirectoryName(p->Path);
            p->Name = System::IO::Path::GetFileNameWithoutExtension(p->Path);
            p->SolutionDir = String::IsNullOrWhiteSpace(solutionDir) ? String::Empty : System::IO::Path::GetFullPath(solutionDir);
            if (!File::Exists(p->Path)) throw gcnew IOException("not found: " + p->Path);
            XmlDocument^ doc = gcnew XmlDocument();
            doc->Load(p->Path);
            if (PathUtil::HasExtension(p->Path, ".vcxproj")) { p->Kind = "vcxproj"; p->ReadVcxproj(doc); }
            else if (PathUtil::HasExtension(p->Path, ".vcproj")) { p->Kind = "vcproj"; p->ReadVcproj(doc); }
            else throw gcnew IOException("not a Visual Studio C++ project: " + p->Path);
            return p;
        }

        // the dialog's text: the file, the version, every configuration's values, the items, the notes
        String^ MappingText()
        {
            StringBuilder^ t = gcnew StringBuilder();
            t->Append("Visual Studio project " + Name + ", read from " + Path + "\n");
            t->Append(Version + (Guid->Length > 0 ? "; " + Guid : String::Empty) + "\n");
            if (SolutionDir->Length > 0) t->Append("$(SolutionDir) = " + SolutionDir + "\n");
            if (FiltersFile->Length > 0) t->Append("filters: " + FiltersFile + " (" + Filters->Count + " folders)\n");
            t->Append("configurations: " + String::Join(", ", ConfigurationNames()) + "; platforms: " + String::Join(", ", Platforms()) + "\n");
            for each (VsProjectConfig^ c in Configurations)
            {
                t->Append("\n" + c->Name + ":\n");
                if (c->ConfigurationType->Length > 0) t->Append("  type " + c->ConfigurationType + (c->Toolset->Length > 0 ? ", toolset " + c->Toolset : String::Empty) + (c->ClrSupport ? ", C++/CLI" : String::Empty) + "\n");
                if (c->Defines->Count > 0) t->Append("  defines: " + String::Join(";", c->Defines) + "\n");
                if (c->Undefines->Count > 0) t->Append("  undefines: " + String::Join(";", c->Undefines) + "\n");
                if (c->IncludeDirs->Count > 0) t->Append("  include directories: " + String::Join("; ", c->IncludeDirs) + "\n");
                if (c->Optimization->Length > 0) t->Append("  optimization: " + c->Optimization + "\n");
                if (c->AdditionalOptions->Length > 0) t->Append("  additional options: " + c->AdditionalOptions + "\n");
                if (c->ClSettings->Count > 0)
                {
                    List<String^>^ said = gcnew List<String^>();
                    for each (KeyValuePair<String^, String^> kv in c->ClSettings) said->Add(kv.Key + " " + kv.Value);
                    t->Append("  cl's own settings (given to cl when it compiles the project): " + String::Join("; ", said) + "\n");
                }
                if (c->Libs->Count > 0) t->Append("  libraries: " + String::Join(";", c->Libs) + "\n");
                if (c->LibDirs->Count > 0) t->Append("  library directories: " + String::Join("; ", c->LibDirs) + "\n");
                if (c->OutDir->Length > 0) t->Append("  output: " + c->OutDir + (c->TargetName->Length > 0 ? " " + c->TargetName : String::Empty) + "\n");
                for each (String^ n in c->Notes) t->Append("  not mapped: " + n + "\n");
            }
            Dictionary<String^, int>^ counts = gcnew Dictionary<String^, int>();
            for each (VsProjectItem^ i in Items) { int n; counts->TryGetValue(i->Type, n); counts[i->Type] = n + 1; }
            List<String^>^ said = gcnew List<String^>();
            for each (KeyValuePair<String^, int> kv in counts) said->Add(kv.Value + " " + kv.Key);
            t->Append("\nitems: " + String::Join(", ", said) + "\n");
            for each (VsProjectItem^ i in Items)
                if (i->Excluded->Count > 0) t->Append("  " + i->Include + " excluded from " + (i->Excluded->Contains("*") ? "every configuration" : String::Join(", ", i->Excluded)) + "\n");
            if (Notes->Count > 0)
            {
                t->Append("\nnot mapped:\n");
                for each (String^ n in Notes) t->Append("  " + n + "\n");
            }
            t->Append("\nThese are read from the Visual Studio files and cannot be changed here - edit them in Visual Studio.\n");
            return t->ToString();
        }

    private:
        static String^ ReplaceMacro(String^ s, String^ name, String^ value)
        {
            return Regex::Replace(s, "\\$\\(" + Regex::Escape(name) + "\\)", value == nullptr ? String::Empty : value->Replace("$", "$$"), RegexOptions::IgnoreCase);
        }
        static String^ Local(XmlNode^ n) { return n == nullptr ? String::Empty : n->LocalName; }
        static String^ Attr(XmlNode^ n, String^ name)
        {
            if (n == nullptr || n->Attributes == nullptr) return String::Empty;
            XmlAttribute^ a = n->Attributes[name];
            return a == nullptr ? String::Empty : a->Value;
        }
        static List<XmlElement^>^ Elements(XmlNode^ parent)
        {
            List<XmlElement^>^ r = gcnew List<XmlElement^>();
            if (parent == nullptr) return r;
            for each (XmlNode^ c in parent->ChildNodes) { XmlElement^ e = dynamic_cast<XmlElement^>(c); if (e != nullptr) r->Add(e); }
            return r;
        }
        static XmlElement^ Child(XmlNode^ parent, String^ localName)
        {
            for each (XmlElement^ e in Elements(parent)) if (String::Compare(e->LocalName, localName, true) == 0) return e;
            return nullptr;
        }
        static String^ ChildText(XmlNode^ parent, String^ localName)
        {
            XmlElement^ e = Child(parent, localName);
            return e == nullptr ? String::Empty : e->InnerText->Trim();
        }
        // ';'-separated, blanks dropped, MSBuild's %(...) references dropped
        static List<String^>^ SplitList(String^ list)
        {
            List<String^>^ r = gcnew List<String^>();
            if (String::IsNullOrEmpty(list)) return r;
            for each (String^ s in list->Split(gcnew array<wchar_t>{ L';' }))
            {
                String^ t = s->Trim();
                if (t->Length == 0 || t->StartsWith("%(")) continue;
                r->Add(t);
            }
            return r;
        }
        static bool Appends(String^ list) { return list != nullptr && list->Contains("%("); }
        // a list value: with %(...) in it the entries are added to what earlier groups gave, else they replace it (MSBuild)
        static void ApplyList(List<String^>^ into, String^ value)
        {
            List<String^>^ items = SplitList(value);
            if (!Appends(value)) into->Clear();
            for each (String^ i in items) if (!into->Contains(i)) into->Add(i);
        }
        static String^ ItemKey(String^ include)
        {
            String^ k = include == nullptr ? String::Empty : include->Trim()->Replace(L'/', L'\\')->ToLowerInvariant();
            while (k->StartsWith(".\\")) k = k->Substring(2);
            return k;
        }

        // ---- conditions: '$(Configuration)|$(Platform)'=='Debug|x64', exists(...), and / or ----
        bool Matches(String^ condition, VsProjectConfig^ c)
        {
            if (String::IsNullOrWhiteSpace(condition)) return true;
            String^ text = condition->Trim();
            array<String^>^ ors = Regex::Split(text, "\\s+or\\s+", RegexOptions::IgnoreCase);
            if (ors->Length > 1)
            {
                for each (String^ o in ors) if (Matches(o, c)) return true;
                return false;
            }
            array<String^>^ ands = Regex::Split(text, "\\s+and\\s+", RegexOptions::IgnoreCase);
            if (ands->Length > 1)
            {
                for each (String^ a in ands) if (!Matches(a, c)) return false;
                return true;
            }
            String^ one = text->Trim()->Trim(L'(', L')')->Trim();
            bool negate = false;
            if (one->StartsWith("!")) { negate = true; one = one->Substring(1)->Trim(); }
            bool result;
            Match^ ex = Regex::Match(one, "^exists\\s*\\(\\s*'?([^')]*)'?\\s*\\)$", RegexOptions::IgnoreCase);
            Match^ cmp = Regex::Match(one, "^'(.*)'\\s*(==|!=)\\s*'(.*)'$");
            if (ex->Success)
            {
                String^ p = ExpandForCondition(ex->Groups[1]->Value, c);
                result = !HasMacro(p) && p->Length > 0 && (File::Exists(p) || Directory::Exists(p));
            }
            else if (cmp->Success)
            {
                String^ l = ExpandForCondition(cmp->Groups[1]->Value, c);
                String^ r = ExpandForCondition(cmp->Groups[3]->Value, c);
                bool same = String::Compare(l, r, true) == 0;
                result = cmp->Groups[2]->Value == "==" ? same : !same;
            }
            else if (String::Compare(one, "true", true) == 0) result = true;
            else if (String::Compare(one, "false", true) == 0) result = false;
            else
            {
                String^ note = "condition not evaluated, taken as true: " + condition;
                if (!Notes->Contains(note)) Notes->Add(note);
                result = true;
            }
            return negate ? !result : result;
        }
        // in a condition an unknown property is empty, as MSBuild has it
        String^ ExpandForCondition(String^ s, VsProjectConfig^ c)
        {
            String^ out = ExpandMacros(s, c);
            return Regex::Replace(out, "\\$\\([^)]*\\)", String::Empty);
        }

        // ---- .vcxproj ----
        void ReadVcxproj(XmlDocument^ doc)
        {
            XmlElement^ root = doc->DocumentElement;
            if (root == nullptr || String::Compare(root->LocalName, "Project", true) != 0) throw gcnew IOException(Path + " is not an MSBuild project");
            ToolsVersion = Attr(root, "ToolsVersion");
            // the configurations first, so the groups can be judged against them
            for each (XmlElement^ g in Elements(root))
                if (String::Compare(g->LocalName, "ItemGroup", true) == 0)
                    for each (XmlElement^ e in Elements(g))
                        if (String::Compare(e->LocalName, "ProjectConfiguration", true) == 0)
                        {
                            String^ name = Attr(e, "Include");
                            if (name->Length > 0 && Config(name) == nullptr) Configurations->Add(gcnew VsProjectConfig(name));
                        }
            if (Configurations->Count == 0) { Configurations->Add(gcnew VsProjectConfig("Debug|Win32")); Configurations->Add(gcnew VsProjectConfig("Release|Win32")); Notes->Add("no ProjectConfiguration in the file: Debug|Win32 and Release|Win32 assumed"); }
            // the globals and every group in document order
            for each (XmlElement^ g in Elements(root))
            {
                String^ kind = g->LocalName;
                String^ condition = Attr(g, "Condition");
                if (String::Compare(kind, "PropertyGroup", true) == 0)
                {
                    for each (VsProjectConfig^ c in Configurations)
                    {
                        if (!Matches(condition, c)) continue;
                        for each (XmlElement^ e in Elements(g))
                        {
                            String^ n = e->LocalName; String^ v = e->InnerText->Trim();
                            if (!Matches(Attr(e, "Condition"), c)) continue;
                            if (n == "ProjectGuid") Guid = VsSolution::CleanGuid(v);
                            else if (n == "ProjectName") { if (v->Length > 0) Name = v; }
                            else if (n == "RootNamespace") { if (Name->Length == 0) Name = v; }
                            else if (n == "ConfigurationType") c->ConfigurationType = v;
                            else if (n == "PlatformToolset") c->Toolset = v;
                            else if (n == "CharacterSet") c->CharacterSet = v;
                            else if (n == "CLRSupport") c->ClrSupport = String::Compare(v, "false", true) != 0 && v->Length > 0;
                            else if (n == "UseDebugLibraries") c->UseDebugLibraries = v;
                            else if (n == "OutDir") c->OutDir = v;
                            else if (n == "IntDir") c->IntDir = v;
                            else if (n == "TargetName") c->TargetName = v;
                        }
                    }
                }
                else if (String::Compare(kind, "ItemDefinitionGroup", true) == 0)
                {
                    for each (VsProjectConfig^ c in Configurations)
                    {
                        if (!Matches(condition, c)) continue;
                        for each (XmlElement^ tool in Elements(g))
                        {
                            String^ tn = tool->LocalName;
                            if (tn == "ClCompile")
                            {
                                for each (XmlElement^ e in Elements(tool))
                                {
                                    String^ n = e->LocalName; String^ v = e->InnerText->Trim();
                                    if (!Matches(Attr(e, "Condition"), c)) continue;
                                    if (n == "PreprocessorDefinitions") ApplyList(c->Defines, v);
                                    else if (n == "UndefinePreprocessorDefinitions") ApplyList(c->Undefines, v);
                                    else if (n == "AdditionalIncludeDirectories") ApplyDirs(c->IncludeDirs, v, c);
                                    else if (n == "Optimization") c->Optimization = v;
                                    else if (n == "AdditionalOptions") c->AdditionalOptions = v->Replace("%(AdditionalOptions)", String::Empty)->Trim();
                                    else if (n == "CompileAs") c->CompileAs = v;
                                    else if (n == "PrecompiledHeader") { if (v->Length > 0 && String::Compare(v, "NotUsing", true) != 0) AddNote(c->Notes, "precompiled header (" + v + ")"); }
                                    else if (n == "ForcedIncludeFiles") AddNote(c->Notes, "forced include files: " + v);
                                    else if (v->Length > 0) c->ClSettings[n] = v;
                                }
                            }
                            else if (tn == "Link" || tn == "Lib")
                            {
                                for each (XmlElement^ e in Elements(tool))
                                {
                                    String^ n = e->LocalName; String^ v = e->InnerText->Trim();
                                    if (!Matches(Attr(e, "Condition"), c)) continue;
                                    if (n == "AdditionalDependencies") ApplyList(c->Libs, v);
                                    else if (n == "AdditionalLibraryDirectories") ApplyDirs(c->LibDirs, v, c);
                                    else if (n == "SubSystem") c->SubSystem = v;
                                    else if (n == "ModuleDefinitionFile" || n == "DelayLoadDLLs" || n == "EntryPointSymbol") AddNote(c->Notes, tn + " " + n + " " + v);
                                }
                            }
                            else if (tn == "PreBuildEvent" || tn == "PreLinkEvent" || tn == "PostBuildEvent" || tn == "CustomBuildStep" || tn == "CustomBuild")
                                AddNote(c->Notes, tn + " (not run)");
                            else if (tn == "ResourceCompile" || tn == "Midl" || tn == "Manifest" || tn == "ProjectReference") AddNote(c->Notes, tn + " settings");
                        }
                    }
                }
                else if (String::Compare(kind, "ItemGroup", true) == 0)
                {
                    for each (XmlElement^ e in Elements(g))
                    {
                        String^ type = e->LocalName;
                        String^ include = Attr(e, "Include");
                        if (type == "ProjectConfiguration") continue;
                        if (type == "Reference") { References->Add(include); continue; }
                        if (type == "ProjectReference") { ProjectReferences->Add(include); continue; }
                        if (type == "PackageReference" || type == "Filter") continue;
                        if (Attr(e, "Remove")->Length > 0 || Attr(e, "Update")->Length > 0) { AddNote(Notes, type + " with Remove/Update (SDK style) not applied: " + Attr(e, "Remove") + Attr(e, "Update")); continue; }
                        if (include->Length == 0) continue;
                        AddItems(type, include, Attr(e, "Exclude"), e, condition);
                    }
                }
                else if (String::Compare(kind, "Import", true) == 0) NoteImport(Attr(g, "Project"));
                else if (String::Compare(kind, "ImportGroup", true) == 0)
                    for each (XmlElement^ e in Elements(g)) if (String::Compare(e->LocalName, "Import", true) == 0) NoteImport(Attr(e, "Project"));
                else if (String::Compare(kind, "Target", true) == 0) AddNote(Notes, "target '" + Attr(g, "Name") + "' (not run)");
            }
            Toolset = Configurations->Count > 0 ? Configurations[0]->Toolset : String::Empty;
            Version = DescribeToolsVersion(ToolsVersion, Toolset);
            for each (VsProjectConfig^ c in Configurations)
            {
                if (c->ClrSupport) AddNote(Notes, "C++/CLI (CLRSupport) in " + c->Name + " - GRIDE's compilers build native C and C++ only");
                if (c->OutDir->Length > 0) c->OutDir = ExpandMacros(c->OutDir, c);
                if (c->IntDir->Length > 0) c->IntDir = ExpandMacros(c->IntDir, c);
                if (c->TargetName->Length > 0) c->TargetName = ExpandMacros(c->TargetName, c);
            }
            if (References->Count > 0) AddNote(Notes, "assembly references: " + String::Join(", ", References));
            if (ProjectReferences->Count > 0) AddNote(Notes, "project references: " + String::Join(", ", ProjectReferences));
            ReadFilters();
        }

        void NoteImport(String^ project)
        {
            if (String::IsNullOrEmpty(project)) return;
            if (project->EndsWith(".props", StringComparison::OrdinalIgnoreCase) && !HasMacro(project)) AddNote(Notes, "property sheet not evaluated: " + project);
            else if (project->EndsWith(".props", StringComparison::OrdinalIgnoreCase) && !project->Contains("$(VCTargetsPath)") && !project->Contains("$(UserRootDir)")) AddNote(Notes, "property sheet not evaluated: " + project);
            else if (project->EndsWith(".targets", StringComparison::OrdinalIgnoreCase) && !project->Contains("$(VCTargetsPath)")) AddNote(Notes, "targets file not run: " + project);
        }
        static void AddNote(List<String^>^ notes, String^ note) { if (!notes->Contains(note)) notes->Add(note); }

        // directories: macros expanded, made absolute under the project; one with an unresolved macro is left out and said
        void ApplyDirs(List<String^>^ into, String^ value, VsProjectConfig^ c)
        {
            if (!Appends(value)) into->Clear();
            for each (String^ raw in SplitList(value))
            {
                String^ d = ExpandMacros(raw, c);
                if (HasMacro(d)) { AddNote(c->Notes, "directory with an unresolved macro left out: " + raw + (SolutionDir->Length == 0 && raw->Contains("$(SolutionDir)") ? " (open the solution to resolve $(SolutionDir))" : String::Empty)); continue; }
                String^ abs = PathUtil::MakeAbsolute(Dir, d->Trim()->TrimEnd(L'\\'));
                if (!into->Contains(abs)) into->Add(abs);
            }
        }

        // an item: a file, or a wildcard pattern expanded against the project folder (* and ? in the last part, ** for any depth)
        void AddItems(String^ type, String^ include, String^ exclude, XmlElement^ e, String^ groupCondition)
        {
            List<String^>^ paths = gcnew List<String^>();
            bool wildcard = include->IndexOfAny(gcnew array<wchar_t>{ L'*', L'?' }) >= 0;
            String^ expanded = ExpandMacros(include, nullptr);
            if (wildcard)
            {
                paths = Expand(expanded);
                if (paths->Count == 0) AddNote(Notes, type + " pattern matched nothing: " + include);
                if (!String::IsNullOrEmpty(exclude))
                    for each (String^ x in SplitList(exclude))
                        for each (String^ gone in Expand(ExpandMacros(x, nullptr))) paths->Remove(gone);
            }
            else if (HasMacro(expanded)) { AddNote(Notes, type + " item with an unresolved macro: " + include); return; }
            else paths->Add(PathUtil::MakeAbsolute(Dir, expanded));
            for each (String^ path in paths)
            {
                VsProjectItem^ item = gcnew VsProjectItem();
                item->Include = include; item->Path = path; item->Type = type; item->FromWildcard = wildcard;
                for each (XmlElement^ m in Elements(e))
                {
                    String^ n = m->LocalName;
                    if (n == "ExcludedFromBuild")
                    {
                        if (String::Compare(m->InnerText->Trim(), "true", true) != 0) continue;
                        String^ cond = Attr(m, "Condition");
                        if (cond->Length == 0) item->Excluded->Add("*");
                        else for each (VsProjectConfig^ c in Configurations) if (Matches(cond, c)) item->Excluded->Add(c->Name);
                    }
                    else if (n == "Filter") item->Filter = m->InnerText->Trim();
                    else if (n != "FileType" && n != "SubType" && !item->Settings->Contains(n)) item->Settings->Add(n);
                }
                // an item group of a condition holds its items for those configurations only
                if (groupCondition->Length > 0)
                    for each (VsProjectConfig^ c in Configurations) if (!Matches(groupCondition, c)) item->Excluded->Add(c->Name);
                Items->Add(item);
                if (item->Settings->Count > 0 && type == "ClCompile") AddNote(Notes, "per-file settings of " + include + " not mapped: " + String::Join(", ", item->Settings));
            }
        }

        List<String^>^ Expand(String^ pattern)
        {
            List<String^>^ r = gcnew List<String^>();
            try
            {
                String^ p = pattern->Replace(L'/', L'\\');
                int star2 = p->IndexOf("**");
                String^ baseRel; String^ filePattern; bool recursive = false;
                if (star2 >= 0)
                {
                    baseRel = p->Substring(0, star2)->TrimEnd(L'\\');
                    filePattern = p->Substring(star2 + 2)->TrimStart(L'\\');
                    if (filePattern->Length == 0) filePattern = "*";
                    recursive = true;
                }
                else
                {
                    int slash = p->LastIndexOf(L'\\');
                    baseRel = slash < 0 ? String::Empty : p->Substring(0, slash);
                    filePattern = slash < 0 ? p : p->Substring(slash + 1);
                }
                if (filePattern->Contains("\\")) { int slash = filePattern->LastIndexOf(L'\\'); baseRel = baseRel + "\\" + filePattern->Substring(0, slash); filePattern = filePattern->Substring(slash + 1); }
                String^ base = baseRel->Length == 0 ? Dir : PathUtil::MakeAbsolute(Dir, baseRel);
                if (!Directory::Exists(base)) return r;
                array<String^>^ found = Directory::GetFiles(base, filePattern, recursive ? SearchOption::AllDirectories : SearchOption::TopDirectoryOnly);
                Array::Sort(found, StringComparer::OrdinalIgnoreCase);
                for each (String^ f in found) r->Add(System::IO::Path::GetFullPath(f));
            }
            catch (Exception^) { }
            return r;
        }

        // the .filters beside the project: the folders, and which one each item sits in
        void ReadFilters()
        {
            String^ file = Path + ".filters";
            if (!File::Exists(file)) return;
            FiltersFile = file;
            XmlDocument^ doc = gcnew XmlDocument();
            try { doc->Load(file); }
            catch (Exception^ ex) { AddNote(Notes, "the .filters file could not be read: " + ex->Message); return; }
            Dictionary<String^, String^>^ byInclude = gcnew Dictionary<String^, String^>();
            for each (XmlElement^ g in Elements(doc->DocumentElement))
            {
                if (String::Compare(g->LocalName, "ItemGroup", true) != 0) continue;
                for each (XmlElement^ e in Elements(g))
                {
                    String^ include = Attr(e, "Include");
                    if (String::Compare(e->LocalName, "Filter", true) == 0) { if (include->Length > 0 && !Filters->Contains(include)) Filters->Add(include); continue; }
                    String^ filter = ChildText(e, "Filter");
                    if (include->Length > 0) byInclude[ItemKey(include)] = filter;
                }
            }
            for each (VsProjectItem^ i in Items)
            {
                String^ f;
                if (byInclude->TryGetValue(ItemKey(i->Include), f)) i->Filter = f;
            }
            // a filter an item names that the list does not: still a folder
            for each (VsProjectItem^ i in Items) if (i->Filter->Length > 0 && !Filters->Contains(i->Filter)) Filters->Add(i->Filter);
        }

        // ---- .vcproj (the classic XML) ----
        void ReadVcproj(XmlDocument^ doc)
        {
            XmlElement^ root = doc->DocumentElement;
            if (root == nullptr || String::Compare(root->LocalName, "VisualStudioProject", true) != 0) throw gcnew IOException(Path + " is not a Visual Studio project");
            String^ ver = Attr(root, "Version");
            if (Attr(root, "Name")->Length > 0) Name = Attr(root, "Name");
            Guid = VsSolution::CleanGuid(Attr(root, "ProjectGUID"));
            if (ver->StartsWith("9")) Version = "Visual Studio 2008";
            else if (ver->StartsWith("8")) Version = "Visual Studio 2005";
            else if (ver->StartsWith("7.1")) Version = "Visual Studio .NET 2003";
            else if (ver->StartsWith("7")) Version = "Visual Studio .NET 2002";
            else Version = "Visual Studio (classic .vcproj)";
            if (ver->Length > 0) Version += ", format " + ver;
            ToolsVersion = ver;
            XmlElement^ configs = Child(root, "Configurations");
            for each (XmlElement^ ce in Elements(configs))
            {
                if (String::Compare(ce->LocalName, "Configuration", true) != 0) continue;
                String^ name = Attr(ce, "Name");
                if (name->Length == 0) continue;
                VsProjectConfig^ c = gcnew VsProjectConfig(name);
                String^ type = Attr(ce, "ConfigurationType");
                c->ConfigurationType = type == "1" ? "Application" : type == "2" ? "DynamicLibrary" : type == "4" ? "StaticLibrary" : type;
                c->OutDir = ExpandMacros(Attr(ce, "OutputDirectory"), c);
                c->IntDir = ExpandMacros(Attr(ce, "IntermediateDirectory"), c);
                c->CharacterSet = Attr(ce, "CharacterSet") == "1" ? "Unicode" : Attr(ce, "CharacterSet") == "2" ? "MultiByte" : Attr(ce, "CharacterSet");
                if (Attr(ce, "ManagedExtensions")->Length > 0 && Attr(ce, "ManagedExtensions") != "0") c->ClrSupport = true;
                for each (XmlElement^ tool in Elements(ce))
                {
                    String^ tn = Attr(tool, "Name");
                    if (String::Compare(tn, "VCCLCompilerTool", true) == 0)
                    {
                        ApplyList(c->Defines, Attr(tool, "PreprocessorDefinitions"));
                        ApplyList(c->Undefines, Attr(tool, "UndefinePreprocessorDefinitions"));
                        ApplyDirs(c->IncludeDirs, Attr(tool, "AdditionalIncludeDirectories")->Replace(",", ";"), c);
                        String^ opt = Attr(tool, "Optimization");
                        c->Optimization = opt == "0" ? "Disabled" : opt == "1" ? "MinSpace" : opt == "2" ? "MaxSpeed" : opt == "3" ? "Full" : opt;
                        c->AdditionalOptions = Attr(tool, "AdditionalOptions");
                        String^ pch = Attr(tool, "UsePrecompiledHeader");
                        if (pch->Length > 0 && pch != "0") AddNote(c->Notes, "precompiled header");
                        String^ as = Attr(tool, "CompileAs");
                        c->CompileAs = as == "1" ? "CompileAsC" : as == "2" ? "CompileAsCpp" : String::Empty;
                    }
                    else if (String::Compare(tn, "VCLinkerTool", true) == 0)
                    {
                        // the classic file separates libraries with spaces
                        for each (String^ l in Attr(tool, "AdditionalDependencies")->Split(gcnew array<wchar_t>{ L' ', L';' }, StringSplitOptions::RemoveEmptyEntries))
                            if (!c->Libs->Contains(l)) c->Libs->Add(l);
                        ApplyDirs(c->LibDirs, Attr(tool, "AdditionalLibraryDirectories")->Replace(",", ";"), c);
                        String^ sub = Attr(tool, "SubSystem");
                        c->SubSystem = sub == "1" ? "Console" : sub == "2" ? "Windows" : sub;
                    }
                    else if (String::Compare(tn, "VCPreBuildEventTool", true) == 0 || String::Compare(tn, "VCPostBuildEventTool", true) == 0 || String::Compare(tn, "VCPreLinkEventTool", true) == 0 || String::Compare(tn, "VCCustomBuildTool", true) == 0)
                    {
                        if (Attr(tool, "CommandLine")->Length > 0) AddNote(c->Notes, tn + " (not run)");
                    }
                    else if (String::Compare(tn, "VCResourceCompilerTool", true) == 0 || String::Compare(tn, "VCMIDLTool", true) == 0) { }
                }
                Configurations->Add(c);
            }
            if (Configurations->Count == 0) { Configurations->Add(gcnew VsProjectConfig("Debug|Win32")); Configurations->Add(gcnew VsProjectConfig("Release|Win32")); Notes->Add("no Configuration in the file: Debug|Win32 and Release|Win32 assumed"); }
            XmlElement^ files = Child(root, "Files");
            if (files != nullptr) ReadVcprojFiles(files, String::Empty);
            Toolset = String::Empty;
        }

        void ReadVcprojFiles(XmlElement^ parent, String^ filter)
        {
            for each (XmlElement^ e in Elements(parent))
            {
                if (String::Compare(e->LocalName, "Filter", true) == 0)
                {
                    String^ name = Attr(e, "Name");
                    String^ path = filter->Length == 0 ? name : filter + "\\" + name;
                    if (!Filters->Contains(path)) Filters->Add(path);
                    ReadVcprojFiles(e, path);
                }
                else if (String::Compare(e->LocalName, "File", true) == 0)
                {
                    String^ rel = Attr(e, "RelativePath");
                    if (rel->Length == 0) continue;
                    VsProjectItem^ item = gcnew VsProjectItem();
                    item->Include = rel;
                    item->Path = PathUtil::MakeAbsolute(Dir, ExpandMacros(rel, nullptr));
                    item->Type = TypeByExtension(rel);
                    item->Filter = filter;
                    for each (XmlElement^ fc in Elements(e))
                    {
                        if (String::Compare(fc->LocalName, "FileConfiguration", true) != 0) continue;
                        if (String::Compare(Attr(fc, "ExcludedFromBuild"), "true", true) == 0) item->Excluded->Add(Attr(fc, "Name"));
                        for each (XmlElement^ tool in Elements(fc))
                            if (Attr(tool, "Name")->Length > 0 && tool->Attributes->Count > 1 && !item->Settings->Contains(Attr(tool, "Name"))) item->Settings->Add(Attr(tool, "Name"));
                    }
                    Items->Add(item);
                }
            }
        }

        // what a classic file's item would be in MSBuild terms: by its extension
        static String^ TypeByExtension(String^ path)
        {
            String^ e = System::IO::Path::GetExtension(path)->ToLowerInvariant();
            if (e == ".cpp" || e == ".cxx" || e == ".cc" || e == ".c" || e == ".c++") return "ClCompile";
            if (e == ".h" || e == ".hpp" || e == ".hxx" || e == ".hh" || e == ".inl" || e == ".tli" || e == ".tlh" || e == ".ipp") return "ClInclude";
            if (e == ".rc") return "ResourceCompile";
            if (e == ".idl") return "Midl";
            return "None";
        }
    };
}}
