#pragma once
// ============================================================================
//  GRIDE 2.0 - Code Composer Studio 7.4 project import (Phase 11)
//  Reads the Eclipse ".project" and ".cproject" XML files and turns them into
//  a READ-ONLY GrideProject: files can be viewed, nothing can be saved/built.
// ============================================================================
#include "../Toolchain/Toolchain.h"

using namespace System::Xml;

namespace Gride
{
    public ref class CcsImporter abstract sealed
    {
    public:
        // path = the ".project" file (or the folder that contains it)
        static GrideProject^ Import(String^ path)
        {
            String^ dotProject = Directory::Exists(path) ? Path::Combine(path, ".project") : Path::GetFullPath(path);
            if (!File::Exists(dotProject))
                throw gcnew FileNotFoundException("No CCS/Eclipse .project file found.", dotProject);
            String^ dir = Path::GetDirectoryName(dotProject);

            XmlDocument^ doc = gcnew XmlDocument();
            doc->XmlResolver = nullptr;              // never fetch external DTDs
            doc->Load(dotProject);
            XmlNode^ nameNode = doc->SelectSingleNode("/projectDescription/name");
            String^ name = nameNode != nullptr ? nameNode->InnerText->Trim() : Path::GetFileName(dir);
            if (String::IsNullOrWhiteSpace(name)) name = Path::GetFileName(dir);

            GrideProject^ p = gcnew GrideProject();
            p->Name = name;
            p->OutputName = name;
            p->Target = Names::TargetName(TargetKind::TMS6747);
            p->FilePath = dotProject;
            p->Normalize();
            p->IsReadOnly = true;

            // ---- files inside the project folder
            List<String^>^ files = gcnew List<String^>();
            CollectFiles(dir, dir, files, 0);
            bool hasCpp = false;

            // ---- linked resources (files that live outside the folder)
            //      <location>C:/x/a.c</location>  or  <locationURI>PARENT-1-PROJECT_LOC/common/a.c</locationURI>
            Dictionary<String^, String^>^ vars = ReadVariables(doc, dir);
            for each (XmlNode^ link in doc->SelectNodes("/projectDescription/linkedResources/link"))
            {
                XmlNode^ typeNode = link->SelectSingleNode("type");
                if (typeNode != nullptr && String::Equals(typeNode->InnerText->Trim(), "2"))
                {
                    // type 2 = linked folder: add its files
                    String^ folder = ResolveLocation(link, dir, vars);
                    if (folder != nullptr && Directory::Exists(folder)) CollectFiles(folder, folder, files, 0);
                    continue;
                }
                String^ l = ResolveLocation(link, dir, vars);
                try
                {
                    if (l != nullptr && File::Exists(l) && IsWanted(Path::GetExtension(l)))
                        files->Add(Path::GetFullPath(l));
                }
                catch (ArgumentException^) {}
            }

            for each (String^ f in files)
            {
                if (p->AddFile(f))
                {
                    String^ e = Path::GetExtension(f)->ToLowerInvariant();
                    if (String::Equals(e, ".cpp") || String::Equals(e, ".cc") || String::Equals(e, ".cxx")) hasCpp = true;
                }
            }
            p->Language = Names::LanguageName(hasCpp ? LanguageKind::Cpp11 : LanguageKind::C90);

            // ---- build configurations from .cproject (display only)
            String^ cproject = Path::Combine(dir, ".cproject");
            if (File::Exists(cproject))
            {
                try { ReadConfigurations(cproject, p->CcsConfigurations); }
                catch (Exception^ ex)
                {
                    CcsConfiguration^ c = gcnew CcsConfiguration();
                    c->Name = "(could not read .cproject)";
                    c->Options->Add(ex->Message);
                    p->CcsConfigurations->Add(c);
                }
            }
            return p;
        }

    private:
        // Eclipse path variables: PROJECT_LOC, PARENT_LOC, WORKSPACE_LOC + <variableList> entries
        static Dictionary<String^, String^>^ ReadVariables(XmlDocument^ doc, String^ projectDir)
        {
            Dictionary<String^, String^>^ v = gcnew Dictionary<String^, String^>(StringComparer::OrdinalIgnoreCase);
            v["PROJECT_LOC"] = projectDir;
            String^ parent = Path::GetDirectoryName(projectDir);
            v["PARENT_LOC"] = parent != nullptr ? parent : projectDir;
            v["WORKSPACE_LOC"] = parent != nullptr ? parent : projectDir;   // best guess: projects usually sit in the workspace
            for each (XmlNode^ n in doc->SelectNodes("/projectDescription/variableList/variable"))
            {
                XmlNode^ name = n->SelectSingleNode("name");
                XmlNode^ value = n->SelectSingleNode("value");
                if (name == nullptr || value == nullptr) continue;
                String^ val = value->InnerText->Trim();
                if (val->StartsWith("file:/", StringComparison::OrdinalIgnoreCase)) val = val->Substring(6)->TrimStart(L'/');
                v[name->InnerText->Trim()] = ExpandUri(Uri::UnescapeDataString(val), projectDir, v);
            }
            return v;
        }

        // "PARENT-2-PROJECT_LOC/a/b.c" -> two folders above the project + a/b.c ; "VAR/x" -> value of VAR + x
        static String^ ExpandUri(String^ uri, String^ projectDir, Dictionary<String^, String^>^ vars)
        {
            String^ u = uri->Replace(L'/', L'\\');
            Match^ m = Regex::Match(u, R"RX(^PARENT-(\d+)-([A-Za-z_][\w]*)(.*)$)RX");
            if (m->Success)
            {
                String^ basePath = nullptr;
                if (!vars->TryGetValue(m->Groups[2]->Value, basePath)) return nullptr;
                int up = Int32::Parse(m->Groups[1]->Value);
                for (int i = 0; i < up && basePath != nullptr; i++) basePath = Path::GetDirectoryName(basePath);
                if (basePath == nullptr) return nullptr;
                return Path::GetFullPath(String::Concat(basePath, m->Groups[3]->Value));
            }
            Match^ v = Regex::Match(u, R"RX(^\$?\{?([A-Za-z_][\w]*)\}?(\\.*)?$)RX");
            String^ value = nullptr;
            if (v->Success && vars->TryGetValue(v->Groups[1]->Value, value))
                return Path::GetFullPath(String::Concat(value, v->Groups[2]->Value));
            if (Path::IsPathRooted(u)) return Path::GetFullPath(u);
            return nullptr;
        }

        static String^ ResolveLocation(XmlNode^ link, String^ projectDir, Dictionary<String^, String^>^ vars)
        {
            try
            {
                XmlNode^ loc = link->SelectSingleNode("location");
                if (loc != nullptr)
                {
                    String^ l = loc->InnerText->Trim()->Replace(L'/', L'\\');
                    return Path::IsPathRooted(l) ? Path::GetFullPath(l) : nullptr;
                }
                XmlNode^ uri = link->SelectSingleNode("locationURI");
                if (uri != nullptr)
                {
                    String^ u = Uri::UnescapeDataString(uri->InnerText->Trim());
                    if (u->StartsWith("file:/", StringComparison::OrdinalIgnoreCase)) u = u->Substring(6)->TrimStart(L'/');
                    return ExpandUri(u, projectDir, vars);
                }
            }
            catch (Exception^) {}
            return nullptr;
        }

        static bool IsWanted(String^ ext)
        {
            if (String::IsNullOrEmpty(ext)) return false;
            String^ e = ext->ToLowerInvariant();
            array<String^>^ wanted = gcnew array<String^>{
                ".c", ".h", ".cpp", ".cc", ".cxx", ".hpp", ".asm", ".s", ".sa", ".inc", ".cmd",
                ".cfg", ".tcf", ".ccxml", ".opt", ".gel" };
            return Array::IndexOf(wanted, e) >= 0;
        }

        static bool IsSkippedFolder(String^ name)
        {
            array<String^>^ skip = gcnew array<String^>{
                "debug", "release", ".settings", ".launches", ".git", ".svn", ".metadata", "build" };
            return Array::IndexOf(skip, name->ToLowerInvariant()) >= 0;
        }

        static void CollectFiles(String^ root, String^ dir, List<String^>^ files, int depth)
        {
            if (depth > 12) return;
            try
            {
                array<String^>^ fs = Directory::GetFiles(dir);
                Array::Sort(fs, StringComparer::OrdinalIgnoreCase);
                for each (String^ f in fs)
                    if (IsWanted(Path::GetExtension(f))) files->Add(f);
                array<String^>^ ds = Directory::GetDirectories(dir);
                Array::Sort(ds, StringComparer::OrdinalIgnoreCase);
                for each (String^ d in ds)
                    if (!IsSkippedFolder(Path::GetFileName(d))) CollectFiles(root, d, files, depth + 1);
            }
            catch (UnauthorizedAccessException^) {}
            catch (IOException^) {}
        }

        static String^ ShortName(String^ id)
        {
            if (String::IsNullOrEmpty(id)) return String::Empty;
            // "com.ti.ccstudio.buildDefinitions.C6000_7.4.compilerID.OPT_LEVEL.123" -> "OPT_LEVEL"
            array<String^>^ parts = id->Split(L'.');
            for (int i = parts->Length - 1; i >= 0; i--)
            {
                int n;
                if (!Int32::TryParse(parts[i], n) && parts[i]->Length > 1) return parts[i];
            }
            return id;
        }

        static void ReadConfigurations(String^ cproject, List<CcsConfiguration^>^ list)
        {
            XmlDocument^ doc = gcnew XmlDocument();
            doc->XmlResolver = nullptr;
            doc->Load(cproject);
            HashSet<String^>^ seen = gcnew HashSet<String^>(StringComparer::OrdinalIgnoreCase);
            for each (XmlNode^ cfgNode in doc->SelectNodes("//configuration[@name]"))
            {
                XmlElement^ cfg = dynamic_cast<XmlElement^>(cfgNode);
                if (cfg == nullptr) continue;
                String^ cname = cfg->GetAttribute("name");
                if (String::IsNullOrWhiteSpace(cname) || !seen->Add(cname)) continue;

                CcsConfiguration^ c = gcnew CcsConfiguration();
                c->Name = cname;
                String^ art = cfg->GetAttribute("artifactName");
                String^ artExt = cfg->GetAttribute("artifactExtension");
                if (!String::IsNullOrEmpty(art))
                    c->Options->Add(String::Format("Output = {0}{1}{2}", art, String::IsNullOrEmpty(artExt) ? String::Empty : Str("."), artExt));

                for each (XmlNode^ optNode in cfg->SelectNodes(".//option"))
                {
                    XmlElement^ opt = dynamic_cast<XmlElement^>(optNode);
                    if (opt == nullptr) continue;
                    String^ key = opt->GetAttribute("name");
                    if (String::IsNullOrWhiteSpace(key)) key = ShortName(opt->GetAttribute("superClass"));
                    String^ val = opt->GetAttribute("value");
                    if (String::IsNullOrEmpty(val))
                    {
                        List<String^>^ vals = gcnew List<String^>();
                        for each (XmlNode^ lv in opt->SelectNodes("listOptionValue"))
                        {
                            XmlElement^ lve = dynamic_cast<XmlElement^>(lv);
                            if (lve != nullptr && !String::IsNullOrEmpty(lve->GetAttribute("value")))
                                vals->Add(lve->GetAttribute("value"));
                        }
                        val = String::Join("; ", vals->ToArray());
                    }
                    if (String::IsNullOrWhiteSpace(val) || String::IsNullOrWhiteSpace(key)) continue;
                    c->Options->Add(String::Format("{0} = {1}", key->Trim(), val->Trim()));
                }
                list->Add(c);
            }
        }
    };
}
