// =============================================================================
//  CcsReading.h - a CCS 7.4 or 5.5 project for the TMS320C6747, read as RIDE's
//  src/ccs reads it (ride-port\RIDE-conventions.md, 7): TI's own option
//  definitions as data (CcsOptionDefs), the three files read afresh and nothing
//  written back (CcsReading), the workspace a project is registered in
//  (CcsWorkspace). What RIDE's toolchain can honour is mapped; the rest is named,
//  never dropped in silence. Where RIDE refuses a project outright (not a C674x,
//  COFF, big-endian) GRIDEd reads on and says so in a note, so the project still
//  opens; the build then stands on what was read.
// =============================================================================
#pragma once
#include "../Core/PathUtil.h"
#include "../Settings/ToolchainDefinitions.h"

namespace Gride { namespace Model {

    using namespace System::Xml;
    using namespace Gride::Core;
    using namespace Gride::Settings;

    // ---- TI's option definitions, one row per option (ccsoptions) -----------------------------
    public ref class CcsOptionDef
    {
    public:
        CcsOptionDef() { Id = String::Empty; ValueType = String::Empty; Command = String::Empty; DefaultValue = String::Empty; SuperClass = String::Empty; Enums = gcnew List<KeyValuePair<String^, String^>>(); DefaultEnum = String::Empty; }
        property String^ Id;            // the tail after the version prefix: compilerID.OPT_LEVEL
        property String^ ValueType;     // string, boolean, enumerated, stringList, definedSymbols, includePath, libPaths, libs
        property String^ Command;       // --heap_size=, -O, --define=
        property String^ DefaultValue;  // a plain default, or an enumerated one's full id
        property String^ SuperClass;    // OPT_LEVEL.release's is OPT_LEVEL
        property List<KeyValuePair<String^, String^>>^ Enums;   // value suffix -> flag
        property String^ DefaultEnum;   // the suffix marked (default), or ""
    };

    public ref class CcsOptionDefs
    {
    public:
        CcsOptionDefs() { defs = gcnew Dictionary<String^, CcsOptionDef^>(); File = String::Empty; }

        // a directory named outright (tests, a suite); empty: looked for as DefinitionsDir says
        static property String^ NamedDir;

        property String^ File;
        property bool Loaded { bool get() { return defs->Count > 0; } }

        // lib\ccs beside the program, above it, under the installation's root, or the checkout's docs
        static String^ DefinitionsDir()
        {
            CcsDefinition^ d = ToolchainDefinitions::Current->Ccs;
            if (!String::IsNullOrEmpty(NamedDir)) return Holds(NamedDir, d) ? NamedDir : String::Empty;
            String^ program = ToolchainDefinitions::ExeDirectory();
            List<String^>^ tried = gcnew List<String^>();
            if (program != nullptr)
            {
                tried->Add(Path::Combine(program, d->DefinitionsDir));
                String^ up = program;
                for (int i = 0; i < 4 && up != nullptr; i++)
                {
                    up = Path::GetDirectoryName(up);
                    if (up == nullptr) break;
                    tried->Add(Path::Combine(up, d->DefinitionsDir));
                    tried->Add(Path::Combine(Path::Combine(up, "GRIDE"), d->DefinitionsDir));
                }
            }
            String^ root = ToolchainSettingsRoot();
            if (root != nullptr) { tried->Add(Path::Combine(root, d->DefinitionsDir)); tried->Add(Path::Combine(Path::GetDirectoryName(root), d->DefinitionsDir)); }
            for each (String^ t in tried) if (Holds(t, d)) return t;
            return String::Empty;
        }

        // the file for a compiler version: its major's; false, with a reason, when it is not there
        bool Load(String^ version, String^% why)
        {
            defs->Clear(); File = String::Empty; why = String::Empty;
            CcsDefinition^ d = ToolchainDefinitions::Current->Ccs;
            String^ dir = DefinitionsDir();
            if (dir->Length == 0) { why = "TI's option definitions are not here - " + d->DefinitionsDir + " beside the program, or docs/ccs-reference/ti-option-definitions"; return false; }
            String^ major = MajorOf(version);
            String^ name;
            if (!d->Definitions->TryGetValue(major, name))
            {
                // the nearest major: 8 and up take the newest file, anything older the oldest
                String^ best = nullptr; int bestMajor = -1;
                int wanted = 0; Int32::TryParse(major, wanted);
                for each (KeyValuePair<String^, String^> kv in d->Definitions)
                {
                    int m; if (!Int32::TryParse(kv.Key, m)) continue;
                    if (best == nullptr || (m <= wanted && m > bestMajor) || (bestMajor > wanted && m < bestMajor)) { best = kv.Value; bestMajor = m; }
                }
                name = best;
            }
            if (String::IsNullOrEmpty(name)) { why = "no option definition file is named for compiler version " + version; return false; }
            String^ file = Path::Combine(dir, name);
            if (!System::IO::File::Exists(file)) { why = "cannot read " + file; return false; }
            array<String^>^ lines = System::IO::File::ReadAllLines(file);
            for (int i = 1; i < lines->Length; i++)
            {
                array<String^>^ cols = lines[i]->Split(L'\t');
                if (cols->Length < 7) continue;
                CcsOptionDef^ def = gcnew CcsOptionDef();
                def->Id = IdTail(cols[1]->Trim());
                def->ValueType = cols[2]->Trim();
                def->Command = cols[3]->Trim();
                def->DefaultValue = cols[5]->Trim();
                def->SuperClass = IdTail(cols[6]->Trim());
                if (cols->Length > 7)
                    for each (String^ raw in cols[7]->Split(L'|'))
                    {
                        String^ one = raw->Trim();
                        if (one->Length == 0) continue;
                        bool isDefault = false;
                        String^ mark = " (default)";
                        if (one->EndsWith(mark)) { one = one->Substring(0, one->Length - mark->Length); isDefault = true; }
                        int eq = one->IndexOf(L'=');
                        String^ suffix = eq < 0 ? one : one->Substring(0, eq);
                        String^ flag = eq < 0 ? String::Empty : one->Substring(eq + 1);
                        def->Enums->Add(KeyValuePair<String^, String^>(suffix, flag));
                        if (isDefault) def->DefaultEnum = suffix;
                    }
                // the first row of a tail wins: an instance (compilerRelease's OPT_LEVEL.release) has its own id
                if (!defs->ContainsKey(def->Id)) defs[def->Id] = def;
            }
            File = file;
            if (defs->Count == 0) { why = file + " holds no definitions"; return false; }
            return true;
        }

        CcsOptionDef^ Find(String^ tail)
        {
            CcsOptionDef^ d;
            return defs->TryGetValue(tail, d) ? d : nullptr;
        }

        // the command-line spelling of a stored value, from the definition: a boolean's command, a
        // string's command and value, an enumerated value's own flag, a list's command per item
        String^ Spell(String^ tail, String^ value, List<String^>^ items)
        {
            CcsOptionDef^ def = Find(tail);
            // an instance carries its superClass's shape: OPT_LEVEL.release spells as OPT_LEVEL does
            CcsOptionDef^ shape = def != nullptr && def->ValueType->Length == 0 && def->SuperClass->Length > 0 ? Find(def->SuperClass) : def;
            if (shape == nullptr)
            {
                String^ said = tail;
                if (!String::IsNullOrEmpty(value)) said += "=" + value;
                for (int i = 0; i < items->Count; i++) said += (i > 0 ? "," : "=") + items[i];
                return said;
            }
            if (shape->ValueType == "boolean") return value == "true" ? shape->Command : shape->Command + "=false";
            if (shape->ValueType == "enumerated")
            {
                String^ suffix = EnumSuffix(String::Empty, value);
                for each (KeyValuePair<String^, String^> e in shape->Enums)
                    if (e.Key == suffix) return e.Value->Length == 0 ? shape->Command : e.Value;
                return shape->Command + "=" + suffix;
            }
            if (shape->ValueType == "string") return shape->Command + (value == nullptr ? String::Empty : value);
            StringBuilder^ said = gcnew StringBuilder();
            for (int i = 0; i < items->Count; i++) { if (i > 0) said->Append(L' '); said->Append(shape->Command)->Append(items[i]); }
            return said->Length == 0 ? shape->Command : said->ToString();
        }

        // the enumerated default's suffix, or the plain defaultValue, for an option the project left unstored
        String^ DefaultOf(String^ tail)
        {
            CcsOptionDef^ def = Find(tail);
            if (def == nullptr) return String::Empty;
            if (def->DefaultValue->Length > 0)
            {
                CcsOptionDef^ shape = def->ValueType->Length > 0 ? def : Find(def->SuperClass);
                if (shape != nullptr && shape->ValueType == "enumerated") return EnumSuffix(String::Empty, def->DefaultValue);
                return def->DefaultValue;
            }
            if (def->DefaultEnum->Length > 0) return def->DefaultEnum;
            if (def->ValueType->Length == 0 && def->SuperClass->Length > 0) return DefaultOf(def->SuperClass);
            return String::Empty;
        }

        // "com.ti.ccstudio.buildDefinitions.C6000_8.2.compilerID.OPT_LEVEL" -> "compilerID.OPT_LEVEL"
        static String^ IdTail(String^ fullId)
        {
            if (fullId == nullptr) return String::Empty;
            String^ prefix = "com.ti.ccstudio.buildDefinitions.";
            if (!fullId->StartsWith(prefix)) return fullId;
            String^ rest = fullId->Substring(prefix->Length);
            // past "C6000_8.2." - the version carries a dot of its own - or "core."; what follows is compilerID.X, linkerID.X
            if (rest->StartsWith("C6000_"))
            {
                int at = 6;
                while (at < rest->Length && (Char::IsDigit(rest[at]) || rest[at] == L'.')) at++;
                return rest->Substring(at);
            }
            return rest;
        }

        // an enumerated value's id -> its suffix after the option's own id
        static String^ EnumSuffix(String^ optionId, String^ valueId)
        {
            if (valueId == nullptr) return String::Empty;
            if (optionId->Length > 0 && valueId->StartsWith(optionId) && valueId->Length > optionId->Length && valueId[optionId->Length] == L'.')
                return valueId->Substring(optionId->Length + 1);
            int dot = valueId->LastIndexOf(L'.');
            return dot < 0 ? valueId : valueId->Substring(dot + 1);
        }

        static String^ MajorOf(String^ version)
        {
            if (String::IsNullOrEmpty(version)) return String::Empty;
            int dot = version->IndexOf(L'.');
            return dot < 0 ? version->Trim() : version->Substring(0, dot)->Trim();
        }

    private:
        Dictionary<String^, CcsOptionDef^>^ defs;

        static bool Holds(String^ dir, CcsDefinition^ d)
        {
            if (String::IsNullOrEmpty(dir) || !Directory::Exists(dir)) return false;
            for each (KeyValuePair<String^, String^> kv in d->Definitions)
                if (!System::IO::File::Exists(Path::Combine(dir, kv.Value))) return false;
            return d->Definitions->Count > 0;
        }

        // the installation's bin, from the settings, for lib\ccs beside it
        static String^ ToolchainSettingsRoot()
        {
            try
            {
                InstallationDefinition^ inst = ToolchainDefinitions::Current->Installation;
                for each (String^ r in inst->DefaultRoots)
                {
                    String^ x = Environment::ExpandEnvironmentVariables(r);
                    if (!x->Contains("%") && Directory::Exists(x)) return x;
                }
            }
            catch (Exception^) { }
            return nullptr;
        }
    };

    // ---- the link of one configuration, as lnk6x is handed it (TiLink) -----------------------
    public ref class CcsLink
    {
    public:
        CcsLink() { Given = false; Heap = String::Empty; Stack = String::Empty; SearchPaths = gcnew List<String^>(); Libraries = gcnew List<String^>(); RomModel = -1; CmdFiles = gcnew List<String^>(); }
        property bool Given;
        property String^ Heap;
        property String^ Stack;
        property List<String^>^ SearchPaths;
        property List<String^>^ Libraries;
        property int RomModel;          // 1 --rom_model, 0 --ram_model, -1 unsaid
        property List<String^>^ CmdFiles;
    };

    // ---- one configuration, mapped (Config) ----------------------------------------------------
    public ref class CcsConfig
    {
    public:
        CcsConfig()
        {
            Name = String::Empty; IsDebug = true; Opt = String::Empty; Debug = false; DebugSaid = false;
            Defines = gcnew List<String^>(); Undefines = gcnew List<String^>(); Includes = gcnew List<String^>(); NoCompress = false;
            Link = gcnew CcsLink(); Sources = gcnew List<String^>(); Excluded = gcnew List<String^>();
            Unsupported = gcnew List<String^>(); OwnInstead = gcnew List<String^>(); Mapped = gcnew List<String^>();
            Prebuild = String::Empty; Postbuild = String::Empty;
        }
        property String^ Name;                 // CCS's: Debug, Release
        property bool IsDebug;                 // RIDE's
        property String^ Opt;                  // -O0, -O1 or -O2
        property bool Debug;                   // -g (DWARF)
        property bool DebugSaid;
        property List<String^>^ Defines;       // NAME or NAME=value, CCS's outer quotes taken off
        property List<String^>^ Undefines;
        property List<String^>^ Includes;      // absolute, macros resolved, ${CG_TOOL_ROOT}/include left out
        property bool NoCompress;
        property CcsLink^ Link;
        property List<String^>^ Sources;       // absolute, exclusions applied, .cmd files apart
        property List<String^>^ Excluded;      // project-relative, as .cproject spells them
        property List<String^>^ Unsupported;   // as cl6x would have been given them
        property List<String^>^ OwnInstead;
        property List<String^>^ Mapped;        // "cl6x -O3 -> cpp11 -O2", for the read-only dialog
        property String^ Prebuild;
        property String^ Postbuild;
    };

    // ---- a workspace's registered project (ccsworkspace) --------------------------------------
    public ref class CcsWorkspaceMember
    {
    public:
        CcsWorkspaceMember() { Name = String::Empty; Location = String::Empty; Inside = false; }
        property String^ Name;
        property String^ Location;      // absolute, slashes one way
        property bool Inside;           // in the workspace folder rather than imported from elsewhere
    };

    public ref class CcsWorkspace
    {
    public:
        CcsWorkspace()
        {
            Dir = String::Empty; Projects = gcnew List<CcsWorkspaceMember^>(); Notes = gcnew List<String^>();
            PathVariables = gcnew Dictionary<String^, String^>(); Macros = gcnew Dictionary<String^, String^>();
        }
        property String^ Dir;
        property List<CcsWorkspaceMember^>^ Projects;
        property List<String^>^ Notes;
        property Dictionary<String^, String^>^ PathVariables;
        property Dictionary<String^, String^>^ Macros;

        literal String^ Resources = ".metadata/.plugins/org.eclipse.core.resources";
        literal String^ RuntimeSettings = ".metadata/.plugins/org.eclipse.core.runtime/.settings";

        CcsWorkspaceMember^ Project(String^ name)
        {
            for each (CcsWorkspaceMember^ m in Projects) if (m->Name == name) return m;
            return nullptr;
        }

        // a folder with .metadata/.plugins/org.eclipse.core.resources is a workspace
        static bool IsWorkspace(String^ dir)
        {
            return !String::IsNullOrEmpty(dir) && Directory::Exists(Path::Combine(dir, Resources->Replace(L'/', L'\\')));
        }

        // the registry, the path variables and the build macros; null when the folder is not a workspace
        static CcsWorkspace^ Read(String^ where, String^% error)
        {
            error = String::Empty;
            String^ dir = WithSlashes(Path::GetFullPath(where))->TrimEnd(L'/');
            if (!IsWorkspace(dir)) { error = dir + " is not a CCS workspace (no .metadata)"; return nullptr; }
            CcsWorkspace^ out = gcnew CcsWorkspace();
            out->Dir = dir;
            String^ registry = Path::Combine(dir, Resources + "/.projects");
            if (Directory::Exists(registry))
                for each (String^ sub in Directory::GetDirectories(registry))
                {
                    String^ name = Path::GetFileName(sub);
                    if (name->Length == 0 || name->StartsWith(".")) continue;
                    CcsWorkspaceMember^ member = gcnew CcsWorkspaceMember();
                    member->Name = name;
                    String^ record = Path::Combine(sub, ".location");
                    if (System::IO::File::Exists(record))
                    {
                        member->Location = LocationRecord(System::IO::File::ReadAllBytes(record));
                        if (member->Location->Length == 0) { out->Notes->Add(name + ": its .location could not be read"); continue; }
                    }
                    else { member->Location = dir + "/" + name; member->Inside = true; }
                    if (!Directory::Exists(member->Location)) { out->Notes->Add(name + " is registered at " + member->Location + ", which is not on this machine"); continue; }
                    if (!IsProjectDir(member->Location)) continue;   // RemoteSystemsTempFiles and other non-CCS projects
                    out->Projects->Add(member);
                }
            out->Projects->Sort(gcnew Comparison<CcsWorkspaceMember^>(&CcsWorkspace::ByName));
            String^ settingsDir = Path::Combine(dir, RuntimeSettings);
            String^ resources = Path::Combine(settingsDir, "org.eclipse.core.resources.prefs");
            if (System::IO::File::Exists(resources))
                for each (KeyValuePair<String^, String^> kv in Properties(System::IO::File::ReadAllText(resources)))
                    if (kv.Key->StartsWith("pathvariable.")) out->PathVariables[kv.Key->Substring(13)] = PathOfUri(kv.Value);
            String^ cdt = Path::Combine(settingsDir, "org.eclipse.cdt.core.prefs");
            if (System::IO::File::Exists(cdt))
            {
                Dictionary<String^, String^>^ prefs = Properties(System::IO::File::ReadAllText(cdt));
                String^ xml;
                if (prefs->TryGetValue("macros/workspace", xml))
                {
                    try
                    {
                        XmlDocument^ d = gcnew XmlDocument();
                        d->LoadXml(xml);
                        for each (XmlNode^ m in d->SelectNodes("//stringMacro"))
                            out->Macros[Attr(m, "name")] = Attr(m, "value");
                    }
                    catch (Exception^ ex) { out->Notes->Add("the workspace's build macros could not be read: " + ex->Message); }
                }
            }
            return out;
        }

        // the workspace a project folder is registered in, by looking in its parent: "" when there is none
        static String^ WorkspaceOf(String^ projectDir)
        {
            String^ dir = WithSlashes(Path::GetFullPath(projectDir))->TrimEnd(L'/');
            String^ parent = Path::GetDirectoryName(dir);
            if (parent == nullptr || !IsWorkspace(parent)) return String::Empty;
            String^ error;
            CcsWorkspace^ ws = Read(parent, error);
            if (ws == nullptr) return String::Empty;
            for each (CcsWorkspaceMember^ m in ws->Projects)
                if (PathUtil::SamePath(m->Location, dir)) return ws->Dir;
            return String::Empty;
        }

        static bool IsProjectDir(String^ dir)
        {
            return Directory::Exists(dir) && System::IO::File::Exists(Path::Combine(dir, ".project")) && System::IO::File::Exists(Path::Combine(dir, ".ccsproject"));
        }

        static String^ WithSlashes(String^ p) { return p == nullptr ? String::Empty : p->Replace(L'\\', L'/'); }

        // a file: URI as Eclipse writes it - file:/C:/x%20y - to a path; anything else as it is
        static String^ PathOfUri(String^ uri)
        {
            if (uri == nullptr || !uri->StartsWith("file:")) return uri == nullptr ? String::Empty : uri;
            String^ text = uri->Substring(5);
            StringBuilder^ out = gcnew StringBuilder();
            for (int i = 0; i < text->Length; i++)
            {
                if (text[i] == L'%' && i + 2 < text->Length && Uri::IsHexDigit(text[i + 1]) && Uri::IsHexDigit(text[i + 2]))
                {
                    out->Append((wchar_t)Convert::ToInt32(text->Substring(i + 1, 2), 16));
                    i += 2;
                }
                else out->Append(text[i]);
            }
            String^ s = out->ToString();
            while (s->Length > 1 && s[0] == L'/' && s[1] == L'/') s = s->Substring(1);
            if (s->Length > 2 && s[0] == L'/' && s[2] == L':') s = s->Substring(1);
            return WithSlashes(s);
        }

        // .location: a 16-byte chunk marker, then Java's writeUTF of the location - a big-endian length and the bytes
        static String^ LocationRecord(array<Byte>^ bytes)
        {
            if (bytes == nullptr || bytes->Length < 18) return String::Empty;
            int length = (bytes[16] << 8) | bytes[17];
            if (length == 0 || 18 + length > bytes->Length) return String::Empty;
            String^ text = Encoding::UTF8->GetString(bytes, 18, length);
            if (text->StartsWith("URI//")) return PathOfUri(text->Substring(5));
            return WithSlashes(text);
        }

        // a Java properties file, as Eclipse's preference store writes one: key=value, the escapes undone
        static Dictionary<String^, String^>^ Properties(String^ text)
        {
            Dictionary<String^, String^>^ out = gcnew Dictionary<String^, String^>();
            List<String^>^ lines = gcnew List<String^>();
            StringBuilder^ line = gcnew StringBuilder();
            for (int i = 0; i <= text->Length; i++)
            {
                if (i == text->Length || text[i] == L'\n')
                {
                    String^ l = line->ToString()->TrimEnd(L'\r');
                    int slashes = 0;
                    while (slashes < l->Length && l[l->Length - 1 - slashes] == L'\\') slashes++;
                    if (slashes % 2 == 1 && i < text->Length) { line->Length = 0; line->Append(l->Substring(0, l->Length - 1)); continue; }
                    lines->Add(l);
                    line->Length = 0;
                }
                else line->Append(text[i]);
            }
            for each (String^ raw in lines)
            {
                String^ t = raw->TrimStart(L' ', L'\t');
                if (t->Length == 0 || t[0] == L'#' || t[0] == L'!') continue;
                StringBuilder^ key = gcnew StringBuilder(); StringBuilder^ value = gcnew StringBuilder();
                bool inKey = true;
                for (int i = 0; i < t->Length; i++)
                {
                    wchar_t c = t[i];
                    StringBuilder^ into = inKey ? key : value;
                    if (c == L'\\' && i + 1 < t->Length)
                    {
                        wchar_t e = t[++i];
                        if (e == L'n') into->Append(L'\n');
                        else if (e == L'r') into->Append(L'\r');
                        else if (e == L't') into->Append(L'\t');
                        else if (e == L'u' && i + 4 < t->Length) { into->Append((wchar_t)Convert::ToInt32(t->Substring(i + 1, 4), 16)); i += 4; }
                        else into->Append(e);
                        continue;
                    }
                    if (inKey && (c == L'=' || c == L':')) { inKey = false; continue; }
                    into->Append(c);
                }
                out[key->ToString()] = value->ToString();
            }
            return out;
        }

        static String^ Attr(XmlNode^ n, String^ name)
        {
            if (n == nullptr || n->Attributes == nullptr) return String::Empty;
            XmlNode^ a = n->Attributes->GetNamedItem(name);
            return a == nullptr ? String::Empty : a->Value;
        }

    private:
        static int ByName(CcsWorkspaceMember^ a, CcsWorkspaceMember^ b) { return String::CompareOrdinal(a->Name, b->Name); }
    };

    // ---- the project (Reading) -----------------------------------------------------------------
    public ref class CcsReading
    {
    public:
        CcsReading()
        {
            Dir = String::Empty; Name = String::Empty; Device = String::Empty; Family = String::Empty; CcsVersion = String::Empty;
            CgtVersion = String::Empty; CgToolRoot = String::Empty; Elf = true;
            Configs = gcnew List<CcsConfig^>(); Linked = gcnew List<String^>(); NotBuilt = gcnew List<String^>();
            Headers = gcnew List<String^>(); Notes = gcnew List<String^>(); Refusals = gcnew List<String^>();
            Macros = gcnew Dictionary<String^, String^>(); Workspace = String::Empty;
            PathVariables = gcnew Dictionary<String^, String^>(); Projects = gcnew Dictionary<String^, String^>();
            References = gcnew List<String^>(); DefinitionsFile = String::Empty;
        }
        property String^ Dir;             // the project folder, absolute, slashes one way
        property String^ Name;
        property String^ Device;          // TMS320C67XX.TMS320C6747
        property String^ Family;          // C6000
        property String^ CcsVersion;      // 7.4.0, or "" for 5.5
        property String^ CgtVersion;      // 8.2.2, 7.4.4
        property String^ CgToolRoot;      // ${CG_TOOL_ROOT} as resolved here, or ""
        property bool Elf;
        property List<CcsConfig^>^ Configs;     // Debug first where there is one, then Release
        property List<String^>^ Linked;         // linked resources' absolute paths, files only
        property List<String^>^ NotBuilt;       // sources of a kind RIDE does not build: .asm, .sa, .lib, .obj
        property List<String^>^ Headers;        // the folder's own headers, relative
        property List<String^>^ Notes;
        property List<String^>^ Refusals;       // what RIDE refuses the project for; GRIDEd reads on
        property Dictionary<String^, String^>^ Macros;
        property String^ Workspace;
        property Dictionary<String^, String^>^ PathVariables;
        property Dictionary<String^, String^>^ Projects;
        property List<String^>^ References;
        property String^ DefinitionsFile;

        CcsConfig^ Config(bool debug)
        {
            for each (CcsConfig^ c in Configs) if (c->IsDebug == debug) return c;
            return nullptr;
        }

        static bool IsProject(String^ dir) { return CcsWorkspace::IsProjectDir(dir); }
        static bool IsProjectFile(String^ file)
        {
            String^ leaf = Path::GetFileName(file);
            return leaf == ".project" || leaf == ".ccsproject" || leaf == ".cproject";
        }

        // a directory named as TI names its compiler directories (ti-cgt-c6000_8.3.2, c6000_7.4.24), or holding bin\lnk6x
        static bool IsCompilerDir(String^ dir)
        {
            CcsDefinition^ cd = ToolchainDefinitions::Current->Ccs;
            String^ leaf = Path::GetFileName(dir->TrimEnd(L'/', L'\\'));
            for each (KeyValuePair<String^, String^> kv in cd->CompilerDirs)
                if (kv.Value->Length > 0 && leaf->StartsWith(kv.Value, StringComparison::OrdinalIgnoreCase)) return true;
            try
            {
                String^ bin = String::IsNullOrEmpty(cd->TiBin) ? dir : Path::Combine(dir, cd->TiBin);
                ToolDefinition^ ld = nullptr;
                for each (TargetDefinition^ t in ToolchainDefinitions::Current->Targets)
                    if (t->Emulated) { ld = ToolchainDefinitions::Current->Tool(t->ToolIdFor("nativeLinker")); break; }
                if (ld != nullptr && Directory::Exists(bin))
                    for each (String^ n in ld->AllNames()) if (System::IO::File::Exists(Path::Combine(bin, n))) return true;
            }
            catch (Exception^) { }
            return false;
        }

        // ${CG_TOOL_ROOT}: under the CCS root, the compiler directory of that version; a compiler directory named outright as it is; "" for none
        static String^ CgToolRootFor(String^ ccsRoot, String^ version)
        {
            if (String::IsNullOrWhiteSpace(ccsRoot)) return String::Empty;
            String^ root = CcsWorkspace::WithSlashes(ccsRoot->Trim())->TrimEnd(L'/');
            // the compiler directory itself: GRIDEd's setting before the port, or TI's compiler directory
            // named outright (RIDE's "ti"), told by its place, its name or the linker in its bin
            if (root->Contains("/tools/compiler/") || IsCompilerDir(root)) return root;
            String^ compilers = root + "/tools/compiler";
            String^ found = nullptr;
            if (Directory::Exists(compilers))
                for each (String^ sub in Directory::GetDirectories(compilers))
                {
                    String^ name = Path::GetFileName(sub);
                    if (name->EndsWith("_" + version)) return compilers + "/" + name;
                    if (found == nullptr && name->Contains(version)) found = compilers + "/" + name;
                }
            if (found != nullptr) return found;
            String^ prefix;
            if (!ToolchainDefinitions::Current->Ccs->CompilerDirs->TryGetValue(CcsOptionDefs::MajorOf(version), prefix))
            {
                int major = 0; Int32::TryParse(CcsOptionDefs::MajorOf(version), major);
                prefix = major >= 8 ? "ti-cgt-c6000_" : "c6000_";
            }
            return compilers + "/" + prefix + version;
        }

        // ${ProjName}, ${PROJECT_ROOT}, ${PROJECT_LOC}, ${CG_TOOL_ROOT}, ${workspace_loc:/P/x}, a build variable; a macro nobody knows is left as written
        String^ ResolveMacros(String^ text)
        {
            if (String::IsNullOrEmpty(text)) return String::Empty;
            StringBuilder^ out = gcnew StringBuilder();
            int i = 0;
            while (i < text->Length)
            {
                if (text[i] != L'$' || i + 1 >= text->Length || text[i + 1] != L'{') { out->Append(text[i++]); continue; }
                int close = text->IndexOf(L'}', i);
                if (close < 0) { out->Append(text->Substring(i)); break; }
                String^ macro = text->Substring(i + 2, close - i - 2);
                String^ value = nullptr;
                if (macro == "ProjName") value = Name;
                else if (macro == "PROJECT_ROOT" || macro == "PROJECT_LOC") value = Dir;
                else if (macro == "CG_TOOL_ROOT") value = CgToolRoot;
                else if (macro == "CG_TOOL_CL") value = "\"" + CgToolRoot + "/bin/cl6x\"";
                else if (macro == "CG_CLEAN_CMD") value = "DEL /F";
                else if (macro == "BuildArtifactFileName") value = Name + ".out";
                else if (macro == "BuildDirectory") value = Dir;
                else if (macro == "workspace_loc") value = WorkspaceDir();
                else if (macro->StartsWith("workspace_loc:"))
                {
                    String^ rest = macro->Substring(14)->TrimStart(L'/');
                    int slash = rest->IndexOf(L'/');
                    String^ first = slash < 0 ? rest : rest->Substring(0, slash);
                    String^ member;
                    if (Projects->TryGetValue(first, member)) value = slash < 0 ? member : member + "/" + rest->Substring(slash + 1);
                    else value = WorkspaceDir() + "/" + rest;
                }
                else { String^ v; if (Macros->TryGetValue(macro, v)) value = v; }
                if (value == nullptr) { out->Append(text->Substring(i, close - i + 1)); i = close + 1; continue; }
                out->Append(value);
                i = close + 1;
            }
            return out->ToString();
        }

        // WORKSPACE_LOC: the workspace the project was opened from, else the folder's parent
        String^ WorkspaceDir()
        {
            if (Workspace->Length > 0) return Workspace;
            String^ parent = Path::GetDirectoryName(Dir);
            return parent == nullptr ? Dir : CcsWorkspace::WithSlashes(parent);
        }

        // Reads the three files. Throws only when a file is missing or will not parse; RIDE's refusals
        // (another device, COFF, big-endian, no configuration) are notes in Refusals and the reading goes on.
        static CcsReading^ Read(String^ where, String^ ccsRootSetting, CcsWorkspace^ workspace)
        {
            CcsReading^ out = gcnew CcsReading();
            String^ dir = CcsWorkspace::WithSlashes(Path::GetFullPath(where));
            if (System::IO::File::Exists(dir) && IsProjectFile(dir)) dir = CcsWorkspace::WithSlashes(Path::GetDirectoryName(dir));
            dir = dir->TrimEnd(L'/');
            if (!IsProject(dir)) throw gcnew IOException(dir + " is not a CCS project (no .project and .ccsproject)");
            out->Dir = dir;
            if (workspace != nullptr)
            {
                out->Workspace = workspace->Dir;
                for each (KeyValuePair<String^, String^> kv in workspace->PathVariables) out->PathVariables[kv.Key] = kv.Value;
                for each (CcsWorkspaceMember^ m in workspace->Projects) out->Projects[m->Name] = m->Location;
            }
            XmlDocument^ project = LoadXml(Path::Combine(dir, ".project"));
            XmlDocument^ ccsproject = LoadXml(Path::Combine(dir, ".ccsproject"));
            String^ cprojectFile = Path::Combine(dir, ".cproject");
            if (!System::IO::File::Exists(cprojectFile)) throw gcnew IOException(dir + ": a CCS project needs a .cproject beside .project");
            XmlDocument^ cproject = LoadXml(cprojectFile);
            CcsDefinition^ cd = ToolchainDefinitions::Current->Ccs;

            XmlNode^ nameNode = project->SelectSingleNode("/projectDescription/name");
            out->Name = nameNode != nullptr ? nameNode->InnerText->Trim() : String::Empty;
            if (out->Name->Length == 0) out->Name = Path::GetFileName(dir);
            for each (XmlNode^ r in project->SelectNodes("/projectDescription/projects/project"))
                if (r->InnerText->Trim()->Length > 0) out->References->Add(r->InnerText->Trim());
            for each (String^ r in out->References)
                out->Notes->Add("depends on project " + r + ", which GRIDE does not build - build it in CCS, or open it in GRIDE first");

            out->Device = Attr(ccsproject->SelectSingleNode("//deviceVariant"), "value");
            out->Family = Attr(ccsproject->SelectSingleNode("//deviceFamily"), "value");
            out->CgtVersion = Attr(ccsproject->SelectSingleNode("//codegenToolVersion"), "value");
            out->CcsVersion = Attr(ccsproject->SelectSingleNode("//ccsVersion"), "value");
            String^ elf = Attr(ccsproject->SelectSingleNode("//isElfFormat"), "value");
            out->Elf = elf != "false";
            String^ endianness = Attr(ccsproject->SelectSingleNode("//deviceEndianness"), "value");

            // only the C674x: the C6747 is what the toolchain is built for; GRIDEd notes and reads on
            if (out->Family != cd->DeviceFamily || !out->Device->Contains(cd->DeviceVariant))
                out->Refusals->Add(out->Name + " is for " + (out->Device->Length == 0 ? "an unnamed device" : out->Device) +
                                   (out->Family->Length == 0 ? String::Empty : " (" + out->Family + ")") + ", and GRIDE builds only C6000 C674x/C6747 projects");
            if (!out->Elf) out->Refusals->Add(out->Name + " builds COFF, and GRIDE's C6000 toolchain makes ELF only");
            if (endianness == "big") out->Refusals->Add(out->Name + " is big-endian, and GRIDE's C6000 toolchain is little-endian only");

            out->CgToolRoot = CgToolRootFor(ccsRootSetting, out->CgtVersion);
            CcsOptionDefs^ defs = gcnew CcsOptionDefs();
            String^ why;
            if (!defs->Load(out->CgtVersion, why)) out->Notes->Add(why + "; options CCS left unstored take GRIDE's defaults");
            out->DefinitionsFile = defs->File;

            // .project: the linked resources - a file, or a folder walked like the project's own
            List<KeyValuePair<String^, String^>>^ linkedFiles = gcnew List<KeyValuePair<String^, String^>>();   // project-relative name, absolute
            for each (XmlNode^ link in project->SelectNodes("//link"))
            {
                XmlNode^ linkName = link->SelectSingleNode("name");
                XmlNode^ type = link->SelectSingleNode("type");
                String^ location = out->LocationOf(link);
                if (linkName == nullptr || location->Length == 0) continue;
                if (type != nullptr && type->InnerText->Trim() == "2")
                {
                    List<String^>^ under = gcnew List<String^>();
                    Walk(location, String::Empty, gcnew List<String^>(), under);
                    for each (String^ u in under) linkedFiles->Add(KeyValuePair<String^, String^>(linkName->InnerText->Trim() + "/" + u, location + "/" + u));
                }
                else linkedFiles->Add(KeyValuePair<String^, String^>(linkName->InnerText->Trim(), location));
            }
            for each (KeyValuePair<String^, String^> lf in linkedFiles) out->Linked->Add(lf.Value);

            // .cproject: one cconfiguration per build configuration
            List<String^>^ configNames = gcnew List<String^>();
            for each (XmlNode^ cc in cproject->SelectNodes("//cconfiguration"))
            {
                XmlNode^ configuration = cc->SelectSingleNode(".//configuration");
                if (configuration == nullptr) continue;
                CcsConfig^ config = gcnew CcsConfig();
                config->Name = Attr(configuration, "name");
                configNames->Add(config->Name);
                String^ parent = Attr(configuration, "parent");
                if (parent->EndsWith(".Debug") || (parent->Length == 0 && config->Name == "Debug")) config->IsDebug = true;
                else if (parent->EndsWith(".Release") || (parent->Length == 0 && config->Name == "Release")) config->IsDebug = false;
                else { out->Notes->Add("configuration " + config->Name + " is neither Debug nor Release, and GRIDE builds those two"); continue; }
                if (out->Config(config->IsDebug) != nullptr) { out->Notes->Add("configuration " + config->Name + " is a second " + (config->IsDebug ? "Debug" : "Release") + " one, and the first is taken"); continue; }

                for each (XmlNode^ m in cc->SelectNodes(".//stringMacro"))
                    if (!out->Macros->ContainsKey(Attr(m, "name"))) out->Macros[Attr(m, "name")] = Attr(m, "value");
                if (workspace != nullptr)
                    for each (KeyValuePair<String^, String^> kv in workspace->Macros)
                        if (!out->Macros->ContainsKey(kv.Key)) out->Macros[kv.Key] = kv.Value;
                config->Prebuild = out->ResolveMacros(Attr(configuration, "prebuildStep"));
                config->Postbuild = out->ResolveMacros(Attr(configuration, "postbuildStep"));

                XmlNode^ toolChain = configuration->SelectSingleNode(".//toolChain");
                if (toolChain == nullptr) { out->Notes->Add("configuration " + config->Name + " has no toolChain"); continue; }
                List<CcsStored^>^ chain = OptionsOf(toolChain);
                CcsStored^ tags = Stored(chain, "core.OPT_TAGS");
                if (tags != nullptr)
                    for each (String^ tag in tags->Items)
                    {
                        if (tag == "OUTPUT_FORMAT=COFF") out->Refusals->Add(out->Name + " (" + config->Name + ") builds COFF, and GRIDE's C6000 toolchain makes ELF only");
                        else if (tag == "DEVICE_ENDIANNESS=big") out->Refusals->Add(out->Name + " (" + config->Name + ") is big-endian, and GRIDE's C6000 toolchain is little-endian only");
                        else if (tag->StartsWith("OUTPUT_TYPE=") && tag != "OUTPUT_TYPE=executable")
                            out->Notes->Add(config->Name + " builds " + tag->Substring(12) + ", and GRIDE builds an executable of it");
                    }
                XmlNode^ compilerTool = ToolNamed(toolChain, ".compiler");
                XmlNode^ linkerTool = ToolNamed(toolChain, ".linker");
                List<CcsStored^>^ compiler = compilerTool != nullptr ? OptionsOf(compilerTool) : gcnew List<CcsStored^>();
                List<CcsStored^>^ linker = linkerTool != nullptr ? OptionsOf(linkerTool) : gcnew List<CcsStored^>();
                CcsStored^ silicon = Stored(compiler, "compilerID.SILICON_VERSION");
                if (silicon != nullptr)
                {
                    // CCS stores the core as a string; a project written with it enumerated (as one of the CCS test projects is) means its suffix
                    String^ core = silicon->Value->StartsWith("com.ti.") ? CcsOptionDefs::EnumSuffix(String::Empty, silicon->Value) : silicon->Value;
                    if (core != cd->SiliconVersion)
                        out->Refusals->Add(out->Name + " (" + config->Name + ") is built for -mv" + core + ", and GRIDE's C6000 toolchain is the C674x's (-mv" + cd->SiliconVersion + ")");
                }
                CcsStored^ abi = Stored(compiler, "compilerID.ABI");
                if (abi != nullptr && CcsOptionDefs::EnumSuffix(String::Empty, abi->Value) == "coffabi")
                    out->Refusals->Add(out->Name + " (" + config->Name + ") uses the COFF ABI, and GRIDE's C6000 toolchain makes ELF only");
                CcsStored^ big = Stored(compiler, "compilerID.BIG_ENDIAN");
                if (big != nullptr && big->Value == "true")
                    out->Refusals->Add(out->Name + " (" + config->Name + ") is big-endian, and GRIDE's C6000 toolchain is little-endian only");

                out->MapConfig(compiler, linker, defs, config);
                for each (XmlNode^ fi in configuration->SelectNodes(".//fileInfo")) PerFileOptions(fi, compiler, defs, config);
                for each (XmlNode^ entry in configuration->SelectNodes(".//sourceEntries/entry"))
                    for each (String^ n in Attr(entry, "excluding")->Split(gcnew array<wchar_t>{ L'|' }, StringSplitOptions::RemoveEmptyEntries))
                        config->Excluded->Add(CcsWorkspace::WithSlashes(n));
                out->Configs->Add(config);
            }
            if (out->Configs->Count == 0) out->Refusals->Add(out->Name + ": no Debug or Release configuration in .cproject");
            out->Configs->Sort(gcnew Comparison<CcsConfig^>(&CcsReading::DebugFirst));

            // the sources: every file in the folder by extension, plus the linked ones, minus each configuration's exclusions
            List<String^>^ under = gcnew List<String^>();
            Walk(dir, String::Empty, configNames, under);
            List<KeyValuePair<String^, String^>>^ candidates = gcnew List<KeyValuePair<String^, String^>>();
            for each (String^ u in under) candidates->Add(KeyValuePair<String^, String^>(u, dir + "/" + u));
            for each (KeyValuePair<String^, String^> lf in linkedFiles) candidates->Add(lf);
            for (int i = 0; i < candidates->Count; i++)
            {
                String^ rel = candidates[i].Key; String^ abs = candidates[i].Value;
                int kind = KindOf(rel);
                if (kind == KindNotBuilt) { AddUnique(out->NotBuilt, rel); continue; }
                if (kind == KindHeader) { if (i < under->Count) AddUnique(out->Headers, rel); continue; }
                if (kind == KindOther) continue;
                for each (CcsConfig^ config in out->Configs)
                {
                    if (ExcludedBy(rel, config->Excluded)) continue;
                    if (kind == KindCommand) AddUnique(config->Link->CmdFiles, abs);
                    else AddUnique(config->Sources, abs);
                }
            }
            return out;
        }

        // "CCS project K6747c (Release): 2 options not supported, using RIDE's defaults: ...; RIDE's own instead of: ..."
        String^ Report(bool debug)
        {
            CcsConfig^ config = Config(debug);
            String^ head = "CCS project " + Name;
            if (config == nullptr) return head + ": no " + (debug ? "Debug" : "Release") + " configuration in it";
            head += " (" + config->Name + "): ";
            String^ said = config->Unsupported->Count == 0 ? "no unsupported options"
                         : String::Format("{0} {1} not supported, using GRIDE's defaults: {2}", config->Unsupported->Count,
                                          config->Unsupported->Count == 1 ? "option" : "options", String::Join(", ", config->Unsupported));
            if (config->OwnInstead->Count > 0) said += "; GRIDE's own instead of: " + String::Join(", ", config->OwnInstead);
            return head + said;
        }

        // the line about the sources: linked files not on this machine, kinds RIDE does not build, the notes
        String^ SourceReport()
        {
            List<String^>^ gone = gcnew List<String^>();
            for each (String^ l in Linked) if (!System::IO::File::Exists(l)) gone->Add(l);
            String^ said = String::Empty;
            if (gone->Count > 0) said += "linked " + (gone->Count == 1 ? "file" : "files") + " not on this machine: " + String::Join(", ", gone);
            if (NotBuilt->Count > 0) said += (said->Length == 0 ? String::Empty : "; ") + "not built by GRIDE: " + String::Join(", ", NotBuilt);
            for each (String^ n in Notes) said += (said->Length == 0 ? String::Empty : "; ") + n;
            return said->Length == 0 ? said : "CCS project " + Name + ": " + said;
        }

        // the dialog's text for a configuration: every mapped option, then the two lists, then "edit in CCS"
        String^ MappingText(bool debug)
        {
            CcsConfig^ config = Config(debug);
            StringBuilder^ text = gcnew StringBuilder();
            text->Append("CCS project " + Name + ", read from " + Dir + "\n");
            text->Append("device " + Device + ", compiler " + CgtVersion + (CcsVersion->Length == 0 ? " (CCS 5.5)" : " (CCS " + CcsVersion + ")") + "\n");
            for each (String^ r in Refusals) text->Append("GRIDE would refuse this project: " + r + "\n");
            if (config == nullptr) return text->ToString() + "no " + (debug ? "Debug" : "Release") + " configuration\n";
            text->Append("\n" + config->Name + " - what CCS set, and what GRIDE builds with:\n");
            for each (String^ m in config->Mapped) text->Append("  " + m + "\n");
            if (config->Link->CmdFiles->Count > 0) text->Append("  linker command file: " + String::Join(", ", config->Link->CmdFiles) + "\n");
            if (config->Unsupported->Count > 0)
            {
                text->Append("\nnot supported, GRIDE's defaults instead:\n");
                for each (String^ u in config->Unsupported) text->Append("  " + u + "\n");
            }
            if (config->OwnInstead->Count > 0)
            {
                text->Append("\nRIDE's own instead of:\n");
                for each (String^ o in config->OwnInstead) text->Append("  " + o + "\n");
            }
            if (config->Excluded->Count > 0) text->Append("\nexcluded from the build: " + String::Join(", ", config->Excluded) + "\n");
            text->Append("\nThese are read from the CCS project and cannot be changed here - edit them in CCS.\n");
            return text->ToString();
        }

    private:
        // one option as .cproject stores it: its definition's id tail, a value, or list items
        ref class CcsStored
        {
        public:
            CcsStored() { Tail = String::Empty; Value = String::Empty; Items = gcnew List<String^>(); }
            property String^ Tail;
            property String^ Value;
            property List<String^>^ Items;
        };

        literal int KindCompiled = 0;
        literal int KindCommand = 1;
        literal int KindNotBuilt = 2;
        literal int KindHeader = 3;
        literal int KindOther = 4;
        literal String^ CgToolRootMacro = "${CG_TOOL_ROOT}";

        static int DebugFirst(CcsConfig^ a, CcsConfig^ b) { return (a->IsDebug ? 0 : 1) - (b->IsDebug ? 0 : 1); }

        static XmlDocument^ LoadXml(String^ file)
        {
            XmlDocument^ d = gcnew XmlDocument();
            try { d->Load(file); }
            catch (Exception^ ex) { throw gcnew IOException(file + ": " + ex->Message); }
            return d;
        }
        static String^ Attr(XmlNode^ n, String^ name) { return CcsWorkspace::Attr(n, name); }
        static String^ Unquoted(String^ s)
        {
            return s != nullptr && s->Length >= 2 && s[0] == L'"' && s[s->Length - 1] == L'"' ? s->Substring(1, s->Length - 2) : (s == nullptr ? String::Empty : s);
        }
        static bool Rooted(String^ p) { return !String::IsNullOrEmpty(p) && (p[0] == L'/' || p[0] == L'\\' || (p->Length > 1 && p[1] == L':')); }
        static void AddUnique(List<String^>^ into, String^ one) { if (!into->Contains(one)) into->Add(one); }

        // what CCS's makefile would take a file for, by its extension (FORMAT.md, "Source files")
        static int KindOf(String^ name)
        {
            String^ ext = Path::GetExtension(name);
            if (ext->Length > 0) ext = ext->Substring(1);
            if (ext == "c" || ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "C" || ext == "c++") return KindCompiled;
            if (ext == "cmd") return KindCommand;
            if (ext == "asm" || ext == "s" || ext == "sa" || ext == "lib" || ext == "a" || ext == "obj") return KindNotBuilt;
            if (ext == "h" || ext == "hpp" || ext == "hh" || ext == "hxx") return KindHeader;
            return KindOther;
        }

        static List<CcsStored^>^ OptionsOf(XmlNode^ tool)
        {
            List<CcsStored^>^ out = gcnew List<CcsStored^>();
            for each (XmlNode^ o in tool->SelectNodes("option"))
            {
                CcsStored^ one = gcnew CcsStored();
                String^ sc = Attr(o, "superClass");
                one->Tail = CcsOptionDefs::IdTail(sc->Length > 0 ? sc : Attr(o, "id"));
                one->Value = Attr(o, "value");
                for each (XmlNode^ item in o->SelectNodes("listOptionValue")) one->Items->Add(Attr(item, "value"));
                out->Add(one);
            }
            return out;
        }
        static CcsStored^ Stored(List<CcsStored^>^ list, String^ tail)
        {
            for each (CcsStored^ s in list) if (s->Tail == tail) return s;
            return nullptr;
        }
        // the linker tool is the one whose superClass says so; the compiler the same. A hand-made project
        // (as one of the CCS test projects is) may leave superClass out: then the id, else the tool's name, says it
        static XmlNode^ ToolNamed(XmlNode^ toolChain, String^ word)
        {
            for each (XmlNode^ t in toolChain->SelectNodes("tool"))
                if (Attr(t, "superClass")->Contains(word)) return t;
            for each (XmlNode^ t in toolChain->SelectNodes("tool"))
                if (Attr(t, "superClass")->Length == 0 && Attr(t, "id")->Contains(word)) return t;
            String^ bare = word->TrimStart(L'.');
            for each (XmlNode^ t in toolChain->SelectNodes("tool"))
                if (Attr(t, "superClass")->Length == 0 && Attr(t, "name")->IndexOf(bare, StringComparison::OrdinalIgnoreCase) >= 0) return t;
            return nullptr;
        }

        // the files under a folder, recursively, in name order; the build directories and the dot directories skipped
        static void Walk(String^ dir, String^ relative, List<String^>^ skip, List<String^>^ into)
        {
            if (!Directory::Exists(dir)) return;
            List<String^>^ names = gcnew List<String^>();
            Dictionary<String^, bool>^ isDir = gcnew Dictionary<String^, bool>();
            try
            {
                for each (String^ f in Directory::GetFiles(dir)) { names->Add(Path::GetFileName(f)); isDir[Path::GetFileName(f)] = false; }
                for each (String^ d in Directory::GetDirectories(dir)) { names->Add(Path::GetFileName(d)); isDir[Path::GetFileName(d)] = true; }
            }
            catch (Exception^) { return; }
            names->Sort(StringComparer::Ordinal);
            for each (String^ name in names)
            {
                if (name->Length == 0 || name[0] == L'.') continue;
                String^ rel = relative->Length == 0 ? name : relative + "/" + name;
                if (isDir[name])
                {
                    if (relative->Length == 0 && skip->Contains(name)) continue;
                    if (name->EndsWith(".vm")) continue;
                    Walk(Path::Combine(dir, name), rel, skip, into);
                }
                else into->Add(rel);
            }
        }

        static bool ExcludedBy(String^ relative, List<String^>^ excluding)
        {
            for each (String^ one in excluding)
            {
                if (one->Length == 0) continue;
                if (relative == one) return true;
                if (relative->Length > one->Length && relative->StartsWith(one) && relative[one->Length] == L'/') return true;
            }
            return false;
        }

        // Eclipse's locationURI: PROJECT_LOC/x, PARENT-n-PROJECT_LOC/x, WORKSPACE_LOC/x, a path variable, file:/...
        String^ LocationOf(XmlNode^ link)
        {
            XmlNode^ location = link->SelectSingleNode("location");
            if (location != nullptr && location->InnerText->Trim()->Length > 0) return CcsWorkspace::WithSlashes(location->InnerText->Trim());
            XmlNode^ uri = link->SelectSingleNode("locationURI");
            if (uri == nullptr) return String::Empty;
            String^ text = uri->InnerText->Trim();
            if (text->StartsWith("file:/")) return CcsWorkspace::PathOfUri(text);
            int slash = text->IndexOf(L'/');
            String^ variable = slash < 0 ? text : text->Substring(0, slash);
            String^ rest = slash < 0 ? String::Empty : text->Substring(slash + 1);
            String^ base;
            if (variable == "PROJECT_LOC") base = Dir;
            else if (variable == "WORKSPACE_LOC") base = WorkspaceDir();
            else if (variable->StartsWith("PARENT-") && variable->EndsWith("-PROJECT_LOC"))
            {
                int up = 0; Int32::TryParse(variable->Substring(7, variable->Length - 7 - 12), up);
                base = Dir;
                for (int i = 0; i < up && base != nullptr; i++) { String^ p = Path::GetDirectoryName(base); base = p == nullptr ? base : CcsWorkspace::WithSlashes(p); }
            }
            else
            {
                String^ v;
                if (!PathVariables->TryGetValue(variable, v)) return CcsWorkspace::WithSlashes(text);
                base = v;
            }
            return rest->Length == 0 ? base : base + "/" + rest;
        }

        // the mapping of one configuration's stored options onto RIDE's toolchain: every stored option
        // lands in exactly one of mapped, RIDE's own instead, unsupported
        void MapConfig(List<CcsStored^>^ compiler, List<CcsStored^>^ linker, CcsOptionDefs^ defs, CcsConfig^ out)
        {
            CcsDefinition^ cd = ToolchainDefinitions::Current->Ccs;
            bool release = !out->IsDebug;
            for each (CcsStored^ o in compiler)
            {
                String^ t = o->Tail;
                String^ flag = defs->Spell(t, o->Value, o->Items);
                if (t == "compilerID.OPT_LEVEL" || t == "compilerID.OPT_LEVEL.release")
                {
                    String^ level = CcsOptionDefs::EnumSuffix(String::Empty, o->Value);
                    if (level == "off" || level == "0") out->Opt = "-O0";
                    else if (level == "1") out->Opt = "-O1";
                    else if (level == "2" || level == "3") out->Opt = "-O2";
                    else { out->Unsupported->Add(flag); continue; }
                    out->Mapped->Add(flag + " -> " + out->Opt + (level == "3" ? " (cpp11 and c90 have no -O3)" : String::Empty));
                }
                else if (t == "compilerID.DEBUGGING_MODEL")
                {
                    out->Debug = CcsOptionDefs::EnumSuffix(String::Empty, o->Value) == "SYMDEBUG__DWARF";
                    out->DebugSaid = true;
                    out->Mapped->Add(flag + " -> " + (out->Debug ? "-g" : "no -g"));
                }
                else if (t == "compilerID.DEFINE")
                {
                    for each (String^ i in o->Items) out->Defines->Add(Unquoted(i));
                    out->Mapped->Add(flag + " -> -D" + String::Join(" -D", out->Defines));
                }
                else if (t == "compilerID.UNDEFINE")
                {
                    for each (String^ i in o->Items) out->Undefines->Add(Unquoted(i));
                    out->Mapped->Add(flag + " -> -U" + String::Join(" -U", out->Undefines));
                }
                else if (t == "compilerID.INCLUDE_PATH")
                {
                    List<String^>^ dropped = gcnew List<String^>();
                    for each (String^ i in o->Items)
                    {
                        String^ raw = Unquoted(i);
                        if (raw->StartsWith(CgToolRootMacro)) { dropped->Add(raw); continue; }
                        String^ d = CcsWorkspace::WithSlashes(ResolveMacros(raw));
                        if (!Rooted(d)) d = Dir + "/" + d;
                        AddUnique(out->Includes, d);
                    }
                    out->Mapped->Add(flag + " -> -I" + String::Join(" -I", out->Includes) +
                                     (dropped->Count == 0 ? String::Empty : " (" + String::Join(", ", dropped) + " is the compiler's own: GRIDE's headers instead)"));
                }
                else if (t == "compilerID.NO_COMPRESS")
                {
                    out->NoCompress = o->Value == "true";
                    out->Mapped->Add(flag + " -> " + (out->NoCompress ? "--no_compress" : "compact instructions kept"));
                }
                else if (t == "compilerID.SILICON_VERSION" || t == "compilerID.ABI" || t == "compilerID.EXCEPTIONS" || t == "compilerID.RTTI" ||
                         t->StartsWith("compilerID.DIAG_") || t == "compilerID.DISPLAY_ERROR_NUMBER" || t->StartsWith("compilerID.PREPROC_") ||
                         t == "compilerID.OBJ_DIRECTORY" || t == "compilerID.OBJ_EXTENSION" || t == "compilerID.ASM_DIRECTORY" ||
                         t == "compilerID.TEMP_DIRECTORY" || t == "compilerID.OUTPUT_FILE" || t == "compilerID.LIST_DIRECTORY")
                    out->OwnInstead->Add(flag);
                else out->Unsupported->Add(flag);
            }
            if (Stored(compiler, "compilerID.OPT_LEVEL") == nullptr && Stored(compiler, "compilerID.OPT_LEVEL.release") == nullptr)
            {
                String^ level = defs->DefaultOf(release ? "compilerID.OPT_LEVEL.release" : "compilerID.OPT_LEVEL");
                if (level == "off" || level == "0") out->Opt = "-O0";
                else if (level == "1") out->Opt = "-O1";
                else if (level == "2" || level == "3") out->Opt = "-O2";
                out->Mapped->Add(level->Length == 0 ? "optimization not set in CCS -> GRIDE's default" : "-O" + level + " (CCS's default for " + out->Name + ") -> " + out->Opt);
            }

            out->Link->Given = true;
            for each (CcsStored^ o in linker)
            {
                String^ t = o->Tail;
                String^ flag = defs->Spell(t, o->Value, o->Items);
                if (t == "linkerID.HEAP_SIZE") { out->Link->Heap = Unquoted(o->Value); out->Mapped->Add(flag + " -> lnk6x " + flag); }
                else if (t == "linkerID.STACK_SIZE") { out->Link->Stack = Unquoted(o->Value); out->Mapped->Add(flag + " -> lnk6x " + flag); }
                else if (t == "linkerID.SEARCH_PATH")
                {
                    List<String^>^ dropped = gcnew List<String^>();
                    for each (String^ i in o->Items)
                    {
                        String^ raw = Unquoted(i);
                        if (raw->StartsWith(CgToolRootMacro)) { dropped->Add(raw); continue; }
                        String^ d = CcsWorkspace::WithSlashes(ResolveMacros(raw));
                        if (!Rooted(d)) d = Dir + "/" + d;
                        AddUnique(out->Link->SearchPaths, d);
                    }
                    out->Mapped->Add(flag + " -> -i " + String::Join(" -i ", out->Link->SearchPaths) +
                                     (dropped->Count == 0 ? String::Empty : " (" + String::Join(", ", dropped) + " is the compiler's own: GRIDE's runtime directory instead)"));
                }
                else if (t == "linkerID.LIBRARY")
                {
                    List<String^>^ said = gcnew List<String^>();
                    for each (String^ i in o->Items)
                    {
                        String^ lib = Unquoted(i);
                        if (lib == cd->RuntimeIndex) { said->Add(lib + " -> " + cd->RuntimeLibrary + " (TI's index, which LNK6x does not read)"); lib = cd->RuntimeLibrary; }
                        else said->Add(lib);
                        AddUnique(out->Link->Libraries, lib);
                    }
                    out->Mapped->Add(flag + " -> -l " + String::Join(", -l ", said));
                }
                else if (t == "linkerID.INITIALIZATION_MODEL")
                {
                    out->Link->RomModel = CcsOptionDefs::EnumSuffix(String::Empty, o->Value) == "RAM_MODEL" ? 0 : 1;
                    out->Mapped->Add(flag + " -> lnk6x " + flag);
                }
                else if (t == "linkerID.Z" || t == "linkerID.MAP_FILE" || t == "linkerID.OUTPUT_FILE" || t == "linkerID.XML_LINK_INFO" ||
                         t->StartsWith("linkerID.DIAG_") || t == "linkerID.DISPLAY_ERROR_NUMBER" || t == "linkerID.REREAD_LIBS" || t == "linkerID.WARN_SECTIONS")
                    out->OwnInstead->Add(flag);
                else out->Unsupported->Add(flag);
            }
            if (Stored(linker, "linkerID.INITIALIZATION_MODEL") == nullptr)
            {
                String^ model = defs->DefaultOf("linkerID.INITIALIZATION_MODEL");
                if (model == "RAM_MODEL") { out->Link->RomModel = 0; out->Mapped->Add("--ram_model (CCS's default) -> lnk6x --ram_model"); }
                else if (model == "ROM_MODEL") { out->Link->RomModel = 1; out->Mapped->Add("--rom_model (CCS's default) -> lnk6x --rom_model"); }
            }
        }

        // a file's own options (fileInfo, CCS 7.4): what differs from the folder's is named as unsupported
        static void PerFileOptions(XmlNode^ fileInfo, List<CcsStored^>^ folder, CcsOptionDefs^ defs, CcsConfig^ out)
        {
            List<String^>^ differ = gcnew List<String^>();
            for each (XmlNode^ tool in fileInfo->SelectNodes("tool"))
                for each (CcsStored^ own in OptionsOf(tool))
                {
                    CcsStored^ base = Stored(folder, own->Tail);
                    if (base != nullptr && base->Value == own->Value && SameList(base->Items, own->Items)) continue;
                    differ->Add(defs->Spell(own->Tail, own->Value, own->Items));
                }
            if (differ->Count > 0) out->Unsupported->Add(Attr(fileInfo, "resourcePath") + ": " + String::Join(", ", differ) + " (per-file options)");
        }

        static bool SameList(List<String^>^ a, List<String^>^ b)
        {
            if (a->Count != b->Count) return false;
            for (int i = 0; i < a->Count; i++) if (a[i] != b[i]) return false;
            return true;
        }
    };
}}
