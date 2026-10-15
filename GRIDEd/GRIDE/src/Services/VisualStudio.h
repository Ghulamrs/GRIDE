// =============================================================================
//  VisualStudio.h - where Visual Studio's tools are (cl, ml64, Microsoft's
//  link.exe, clang) and the directories their environment needs, found without
//  cmd.exe: RIDE imports vcvars64.bat's `set` output through cmd, which a policy
//  may block (it is blocked where this was written), so the same directories are
//  taken from the installation's layout, as toolchain.json's visualStudio
//  section describes it: the newest compiler version, or the one its msvcVersion
//  pins (14.44 for VS 2022's v143) - for cl, link.exe, INCLUDE and LIB alike.
//  A setup script named under Tools > Options still wins.
// =============================================================================
#pragma once
#include "../Settings/ToolchainDefinitions.h"

namespace Gride { namespace Services {

    using namespace Gride::Core;
    using namespace Gride::Settings;

    public ref class VsInstallation
    {
    public:
        VsInstallation()
        {
            Root = String::Empty; MsvcDir = String::Empty; MsvcVersion = String::Empty; MsvcBin = String::Empty; MsvcLib = String::Empty; MsvcInclude = String::Empty;
            LlvmBin = String::Empty; KitRoot = String::Empty; KitVersion = String::Empty; KitBin = String::Empty;
            KitLibs = gcnew List<String^>(); KitIncludes = gcnew List<String^>(); Vcvars = String::Empty;
            PinnedVersion = String::Empty; Note = String::Empty;
        }
        property String^ Root;
        property String^ MsvcDir;
        property String^ MsvcVersion;
        property String^ PinnedVersion;   // the file's msvcVersion when it chose the version, else ""
        property String^ Note;            // why the pin was not followed, or ""
        property String^ MsvcBin;
        property String^ MsvcLib;
        property String^ MsvcInclude;
        property String^ LlvmBin;
        property String^ KitRoot;
        property String^ KitVersion;
        property String^ KitBin;
        property List<String^>^ KitLibs;
        property List<String^>^ KitIncludes;
        property String^ Vcvars;

        // the folders the tools live in, in the order they are searched
        List<String^>^ BinDirs()
        {
            List<String^>^ r = gcnew List<String^>();
            for each (String^ d in gcnew array<String^>{ MsvcBin, LlvmBin, KitBin })
                if (!String::IsNullOrEmpty(d) && Directory::Exists(d)) r->Add(d);
            return r;
        }

        // "INCLUDE=...", "LIB=...", "PATH=...;%PATH%" as toolchain.json's environment names them
        List<String^>^ EnvironmentLines(VisualStudioDefinition^ d)
        {
            List<String^>^ r = gcnew List<String^>();
            for each (KeyValuePair<String^, String^> kv in d->Environment)
            {
                List<String^>^ dirs = gcnew List<String^>();
                String^ name = kv.Key->ToUpperInvariant();
                if (name == "INCLUDE") { if (MsvcInclude->Length > 0) dirs->Add(MsvcInclude); dirs->AddRange(KitIncludes); }
                else if (name == "LIB") { if (MsvcLib->Length > 0) dirs->Add(MsvcLib); dirs->AddRange(KitLibs); }
                else if (name == "PATH") dirs->AddRange(BinDirs());
                if (dirs->Count == 0) continue;
                r->Add(kv.Key + "=" + String::Join(";", dirs) + ";%" + kv.Key + "%");
            }
            return r;
        }

        String^ Describe()
        {
            return "Visual Studio " + Root + " (MSVC " + MsvcVersion + (PinnedVersion->Length > 0 ? " as msvcVersion " + PinnedVersion + " asks" : "") +
                   (LlvmBin->Length > 0 ? ", clang" : "") + (KitVersion->Length > 0 ? ", Windows SDK " + KitVersion : ", no Windows SDK") + ")" +
                   (Note->Length > 0 ? " - " + Note : "");
        }
    };

    public ref class VisualStudio abstract sealed
    {
    public:
        // RIDE's settings.json "vcvars": the vcvars64.bat named under Tools > Options, whose installation
        // is taken before vswhere's (the batch file is never run - its folder says where Visual Studio is)
        static property String^ NamedVcvars
        {
            String^ get() { return namedVcvars; }
            void set(String^ v) { if (!String::Equals(namedVcvars, v)) { namedVcvars = v; Forget(); } }
        }

        // the newest installation, or null; found once per definitions
        static VsInstallation^ Find()
        {
            ToolchainDefinitions^ defs = ToolchainDefinitions::Current;
            if (found != nullptr && foundFor == defs) return found;
            if (foundFor == defs && looked) return nullptr;
            foundFor = defs; looked = true; found = nullptr;
            VisualStudioDefinition^ d = defs->VisualStudio;
            if (!d->Defined()) return nullptr;
            String^ root = RootFromNamedVcvars(d);
            if (root == nullptr) root = RootFromVswhere(d);
            if (root == nullptr) root = RootFromDefaults(d);
            if (root == nullptr) return nullptr;
            VsInstallation^ vs = gcnew VsInstallation();
            vs->Root = root;
            vs->Vcvars = d->Vcvars->Length > 0 ? Path::Combine(root, d->Vcvars) : String::Empty;
            String^ tools = Path::Combine(root, d->MsvcTools);
            // the file's msvcVersion pins a compiler (14.44: the newest 14.44.*); the newest otherwise, or when the pinned one is not there
            String^ version = d->MsvcVersion->Length > 0 ? NewestVersionUnder(tools, d->MsvcVersion) : nullptr;
            if (version != nullptr) vs->PinnedVersion = d->MsvcVersion;
            else
            {
                version = NewestVersionUnder(tools);
                if (d->MsvcVersion->Length > 0)
                    vs->Note = "msvcVersion " + d->MsvcVersion + " is not under " + tools + (version != nullptr ? " - the newest, " + version + ", is taken" : "");
            }
            if (version != nullptr)
            {
                vs->MsvcVersion = version;
                vs->MsvcDir = Path::Combine(tools, version);
                vs->MsvcBin = Path::Combine(vs->MsvcDir, d->HostBin);
                vs->MsvcLib = Path::Combine(vs->MsvcDir, d->Lib);
                vs->MsvcInclude = Path::Combine(vs->MsvcDir, d->Include);
            }
            if (d->LlvmBin->Length > 0 && Directory::Exists(Path::Combine(root, d->LlvmBin))) vs->LlvmBin = Path::Combine(root, d->LlvmBin);
            String^ kit = KitRoot(d);
            if (kit != nullptr)
            {
                vs->KitRoot = kit;
                String^ kv = NewestVersionUnder(Path::Combine(kit, d->KitVersionsUnder));
                if (kv != nullptr)
                {
                    vs->KitVersion = kv;
                    for each (String^ rel in d->KitLibDirs) AddIfThere(vs->KitLibs, Path::Combine(kit, rel->Replace("$(Version)", kv)));
                    for each (String^ rel in d->KitIncludeDirs) AddIfThere(vs->KitIncludes, Path::Combine(kit, rel->Replace("$(Version)", kv)));
                    String^ bin = Path::Combine(kit, d->KitBinDir->Replace("$(Version)", kv));
                    if (Directory::Exists(bin)) vs->KitBin = bin;
                }
            }
            found = vs;
            return vs;
        }

        static void Forget() { found = nullptr; looked = false; foundFor = nullptr; }

        // a tool of Visual Studio's by its names, or null
        static String^ FindTool(array<String^>^ names)
        {
            VsInstallation^ vs = Find();
            if (vs == nullptr) return nullptr;
            for each (String^ dir in vs->BinDirs())
                for each (String^ n in names)
                {
                    try
                    {
                        String^ full = Path::Combine(dir, n);
                        if (File::Exists(full)) return full;
                    }
                    catch (Exception^) { }
                }
            return nullptr;
        }

        // the environment lines for a tool run on this machine's host target, or an empty list
        static List<String^>^ EnvironmentLines()
        {
            VsInstallation^ vs = Find();
            return vs == nullptr ? gcnew List<String^>() : vs->EnvironmentLines(ToolchainDefinitions::Current->VisualStudio);
        }

        static String^ Where()
        {
            VsInstallation^ vs = Find();
            return vs == nullptr ? "no Visual Studio was found" : vs->Describe();
        }

    private:
        static VsInstallation^ found;
        static ToolchainDefinitions^ foundFor;
        static bool looked = false;
        static String^ namedVcvars;

        // the installation a named vcvars64.bat belongs to: the file's path without the file's
        // installation-relative name (VC\Auxiliary\Build\vcvars64.bat), else three folders up
        static String^ RootFromNamedVcvars(VisualStudioDefinition^ d)
        {
            try
            {
                if (String::IsNullOrWhiteSpace(namedVcvars)) return nullptr;
                String^ file = Path::GetFullPath(Environment::ExpandEnvironmentVariables(namedVcvars->Trim()));
                if (!File::Exists(file)) return nullptr;
                if (d->Vcvars->Length > 0 && file->EndsWith("\\" + d->Vcvars, StringComparison::OrdinalIgnoreCase))
                {
                    String^ root = file->Substring(0, file->Length - d->Vcvars->Length - 1);
                    if (Directory::Exists(Path::Combine(root, d->MsvcTools))) return root;
                }
                String^ up = Path::GetDirectoryName(file);
                for (int i = 0; i < 3 && up != nullptr; i++) up = Path::GetDirectoryName(up);
                return up != nullptr && Directory::Exists(Path::Combine(up, d->MsvcTools)) ? up : nullptr;
            }
            catch (Exception^) { return nullptr; }
        }

        static String^ RootFromVswhere(VisualStudioDefinition^ d)
        {
            try
            {
                String^ vswhere = Environment::ExpandEnvironmentVariables(d->Vswhere);
                if (vswhere->Length == 0 || vswhere->Contains("%") || !File::Exists(vswhere)) return nullptr;
                System::Diagnostics::ProcessStartInfo^ si = gcnew System::Diagnostics::ProcessStartInfo(vswhere, d->VswhereArgs);
                si->UseShellExecute = false; si->CreateNoWindow = true; si->RedirectStandardOutput = true; si->RedirectStandardInput = true;
                System::Diagnostics::Process^ p = System::Diagnostics::Process::Start(si);
                p->StandardInput->Close();
                String^ out = p->StandardOutput->ReadToEnd();
                if (!p->WaitForExit(10000)) { try { p->Kill(); } catch (Exception^) { } return nullptr; }
                for each (String^ line in out->Split(gcnew array<wchar_t>{ L'\r', L'\n' }, StringSplitOptions::RemoveEmptyEntries))
                {
                    String^ root = line->Trim();
                    if (root->Length > 0 && Directory::Exists(root)) return root;
                }
            }
            catch (Exception^) { }
            return nullptr;
        }

        static String^ RootFromDefaults(VisualStudioDefinition^ d)
        {
            for each (String^ r in d->DefaultRoots)
            {
                String^ root = Environment::ExpandEnvironmentVariables(r);
                if (root->Contains("%")) continue;
                List<String^>^ candidates = gcnew List<String^>();
                for each (String^ e in d->Editions) candidates->Add(Path::Combine(root, e));
                candidates->Add(root);
                for each (String^ c in candidates)
                    if (Directory::Exists(Path::Combine(c, d->MsvcTools))) return c;
            }
            return nullptr;
        }

        static String^ KitRoot(VisualStudioDefinition^ d)
        {
            try
            {
                if (d->KitRegistryKey->Length > 0)
                {
                    Microsoft::Win32::RegistryKey^ k = Microsoft::Win32::Registry::LocalMachine->OpenSubKey(d->KitRegistryKey);
                    if (k != nullptr)
                    {
                        String^ v = dynamic_cast<String^>(k->GetValue(d->KitRegistryValue));
                        if (v != nullptr && Directory::Exists(v)) return v->TrimEnd(L'\\');
                    }
                }
            }
            catch (Exception^) { }
            for each (String^ r in d->KitRoots)
            {
                String^ root = Environment::ExpandEnvironmentVariables(r);
                if (!root->Contains("%") && Directory::Exists(root)) return root;
            }
            return nullptr;
        }

        // the highest version-numbered folder under a directory (14.52.36725 over 14.44.35207)
        static String^ NewestVersionUnder(String^ dir) { return NewestVersionUnder(dir, nullptr); }

        // the same among the folders a version names: itself, or starting with it and a dot (14.44 -> 14.44.35207, not 14.4)
        static String^ NewestVersionUnder(String^ dir, String^ prefix)
        {
            if (String::IsNullOrEmpty(dir) || !Directory::Exists(dir)) return nullptr;
            String^ best = nullptr;
            Version^ bestVersion = nullptr;
            for each (String^ sub in Directory::GetDirectories(dir))
            {
                String^ name = Path::GetFileName(sub);
                Version^ v;
                if (!Version::TryParse(name, v)) continue;
                if (!String::IsNullOrEmpty(prefix) && String::Compare(name, prefix, true) != 0 && !name->StartsWith(prefix + ".", StringComparison::OrdinalIgnoreCase)) continue;
                if (bestVersion == nullptr || v > bestVersion) { bestVersion = v; best = name; }
            }
            return best;
        }

        static void AddIfThere(List<String^>^ into, String^ dir)
        {
            if (Directory::Exists(dir)) into->Add(dir);
        }
    };
}}
