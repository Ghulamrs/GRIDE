#pragma once
// ============================================================================
//  GRIDE 2.0 - the vendors' tools
//  Visual Studio's (cl, ml64, Microsoft's link.exe, clang) and the directories
//  their environment needs, found without cmd.exe: RIDE imports vcvars64.bat's
//  `set` output through cmd, which a policy may block (it is blocked where this
//  was written), so the same directories are taken from the installation's
//  layout, as toolchain.json's visualStudio section describes it. A setup
//  script named under Tools > Options still wins. TI's C6000 compiler
//  directory (lnk6x, the runtime) as RIDE detects it under the roots the ccs
//  section names.
// ============================================================================
#include "ToolchainFile.h"

namespace Gride
{
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
        property String^ PinnedVersion;   // the file's msvcVersion when it chose the version, else ""
        property String^ Note;            // a msvcVersion that is not there
        property String^ Root;
        property String^ MsvcDir;
        property String^ MsvcVersion;
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
        List<String^>^ EnvironmentLines(VisualStudioEntry^ d)
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
        // RIDE's settings.json "vcvars": a vcvars64.bat named under Tools > Options, whose installation
        // is taken before vswhere's (the batch file is never run - its folder says where Visual Studio is)
        static property String^ NamedVcvars
        {
            String^ get() { return namedVcvars; }
            void set(String^ v) { if (!String::Equals(namedVcvars, v)) { namedVcvars = v; Forget(); } }
        }

        // the newest installation, or null; found once per toolchain file
        static VsInstallation^ Find()
        {
            ToolchainFile^ defs = ToolchainFile::Current;
            if (found != nullptr && foundFor == defs) return found;
            if (foundFor == defs && looked) return nullptr;
            foundFor = defs; looked = true; found = nullptr;
            VisualStudioEntry^ d = defs->VisualStudio;
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
        static String^ FindTool(IEnumerable<String^>^ names)
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
            return vs == nullptr ? gcnew List<String^>() : vs->EnvironmentLines(ToolchainFile::Current->VisualStudio);
        }

        static String^ Where()
        {
            VsInstallation^ vs = Find();
            return vs == nullptr ? "no Visual Studio was found" : vs->Describe();
        }

    private:
        static VsInstallation^ found;
        static ToolchainFile^ foundFor;
        static bool looked = false;
        static String^ namedVcvars;

        // the installation a named vcvars64.bat belongs to: the file's path without the file's
        // installation-relative name (VC\Auxiliary\Build\vcvars64.bat), else three folders up
        static String^ RootFromNamedVcvars(VisualStudioEntry^ d)
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

        static String^ RootFromVswhere(VisualStudioEntry^ d)
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

        static String^ RootFromDefaults(VisualStudioEntry^ d)
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

        static String^ KitRoot(VisualStudioEntry^ d)
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

    // TI's C6000 compiler directory (the one with bin\lnk6x): the one named under Tools > Options when
    // it is there, else the newest one CCS's installer left under the roots toolchain.json's ccs
    // section names (RIDE's detectedTi: C:\ti\ti-cgt-c6000_*, C:\ti\ccs*\tools\compiler\...)
    public ref class TiCompiler abstract sealed
    {
    public:
        static property String^ NamedDir
        {
            String^ get() { return namedDir; }
            void set(String^ v) { namedDir = v; }
        }

        static bool Named()
        {
            if (String::IsNullOrWhiteSpace(namedDir)) return false;
            String^ d = Environment::ExpandEnvironmentVariables(namedDir->Trim());
            return Directory::Exists(d);
        }

        static String^ Dir()
        {
            if (Named()) return Path::GetFullPath(Environment::ExpandEnvironmentVariables(namedDir->Trim()));
            return Detected();
        }

        static String^ Bin()
        {
            String^ d = Dir();
            return d == nullptr ? nullptr : Path::Combine(d, ToolchainFile::Current->Ccs->TiBin);
        }

        static String^ Lib()
        {
            String^ d = Dir();
            return d == nullptr ? nullptr : Path::Combine(d, ToolchainFile::Current->Ccs->TiLib);
        }

        // the newest compiler directory with the native linker in its bin; looked for once per toolchain file
        static String^ Detected()
        {
            ToolchainFile^ f = ToolchainFile::Current;
            if (detectedFor == f) return detected;
            detectedFor = f; detected = nullptr;
            CcsEntry^ cd = f->Ccs;
            ToolEntry^ ld = nullptr;
            for each (TargetEntry^ t in f->Targets) if (t->Emulated && ld == nullptr) ld = f->Tool(t->ToolIdFor("nativeLinker"));
            if (ld == nullptr || cd->TiRoots->Count == 0) return nullptr;
            List<String^>^ candidates = gcnew List<String^>();
            for each (String^ r in cd->TiRoots)
            {
                String^ root = Environment::ExpandEnvironmentVariables(r);
                if (root->Contains("%") || !Directory::Exists(root)) continue;
                for each (String^ under in cd->TiUnder) CompilersUnder(root, under, cd, candidates);
            }
            String^ best = nullptr;
            Version^ bestVersion = nullptr;
            for each (String^ c in candidates)
            {
                bool hasLinker = false;
                for each (String^ n in ld->AllNames()) if (File::Exists(Path::Combine(Path::Combine(c, cd->TiBin), n))) { hasLinker = true; break; }
                if (!hasLinker) continue;
                Version^ v = VersionOf(Path::GetFileName(c));
                if (best == nullptr || (v != nullptr && (bestVersion == nullptr || v > bestVersion))) { best = c; bestVersion = v; }
            }
            detected = best;
            return best;
        }

        static void Forget() { detectedFor = nullptr; detected = nullptr; }

    private:
        static String^ namedDir;
        static String^ detected;
        static ToolchainFile^ detectedFor;

        // the compiler directories under a root at one layout ("" is the root itself; a part ending in * names any folder starting so)
        static void CompilersUnder(String^ root, String^ under, CcsEntry^ cd, List<String^>^ into)
        {
            List<String^>^ dirs = gcnew List<String^>();
            dirs->Add(root);
            try
            {
                for each (String^ part in under->Split(gcnew array<wchar_t>{ L'\\', L'/' }, StringSplitOptions::RemoveEmptyEntries))
                {
                    List<String^>^ next = gcnew List<String^>();
                    for each (String^ d in dirs)
                    {
                        if (part->EndsWith("*")) next->AddRange(Directory::GetDirectories(d, part));
                        else if (Directory::Exists(Path::Combine(d, part))) next->Add(Path::Combine(d, part));
                    }
                    dirs = next;
                }
                for each (String^ d in dirs)
                    for each (String^ sub in Directory::GetDirectories(d))
                    {
                        String^ name = Path::GetFileName(sub);
                        for each (KeyValuePair<String^, String^> kv in cd->CompilerDirs)
                            if (kv.Value->Length > 0 && name->StartsWith(kv.Value, StringComparison::OrdinalIgnoreCase)) { into->Add(sub); break; }
                    }
            }
            catch (Exception^) { }
        }

        // the version in a folder name (ti-cgt-c6000_8.3.2 -> 8.3.2; c6000_7.4.24 -> 7.4.24), read as numbers
        static Version^ VersionOf(String^ name)
        {
            int at = -1;
            for (int i = 0; i < name->Length; i++) if (Char::IsDigit(name[i]) && (i == 0 || name[i - 1] == L'_' || name[i - 1] == L'-' || name[i - 1] == L' ')) { at = i; break; }
            if (at < 0) return nullptr;
            StringBuilder^ sb = gcnew StringBuilder();
            for (int i = at; i < name->Length; i++) { wchar_t c = name[i]; if (Char::IsDigit(c) || c == L'.') sb->Append(c); else break; }
            String^ s = sb->ToString()->Trim(L'.');
            if (s->IndexOf(L'.') < 0) s = s + ".0";
            Version^ v;
            return Version::TryParse(s, v) ? v : nullptr;
        }
    };
}
