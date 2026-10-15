// =============================================================================
//  CcsProjectReader.h - read-only import of Code Composer Studio 7.4 projects
//
//  CCS 7.x is Eclipse based. A project folder holds three XML files:
//     .project      Eclipse: project name, linked resources
//     .cproject     CDT: build configurations, include paths, defines, libs
//     .ccsproject   TI: device variant, code-generation tools version
//  GRIDE reads them into an in-memory GrideProject (Kind = "ccs7",
//  IsReadOnly = true). Nothing is ever written back to the CCS files.
// =============================================================================
#pragma once
#include "GrideProject.h"

namespace Gride { namespace Model {

    using namespace System::Xml;
    using namespace Gride::Core;

    public ref class CcsProjectReader abstract sealed
    {
    public:
        // pathOrFolder may be the project folder, its .project or its .ccsproject
        static String^ ResolveFolder(String^ pathOrFolder)
        {
            if (String::IsNullOrEmpty(pathOrFolder)) return nullptr;
            if (Directory::Exists(pathOrFolder)) return Path::GetFullPath(pathOrFolder);
            if (File::Exists(pathOrFolder)) return Path::GetDirectoryName(Path::GetFullPath(pathOrFolder));
            return nullptr;
        }

        static bool IsCcsProject(String^ pathOrFolder)
        {
            String^ dir = ResolveFolder(pathOrFolder);
            if (dir == nullptr) return false;
            return File::Exists(Path::Combine(dir, ".project")) &&
                   (File::Exists(Path::Combine(dir, ".ccsproject")) || File::Exists(Path::Combine(dir, ".cproject")));
        }

        // cgToolRoot = value of ${CG_TOOL_ROOT} from GRIDE settings (may be empty)
        static GrideProject^ Read(String^ pathOrFolder, String^ cgToolRoot)
        {
            String^ dir = ResolveFolder(pathOrFolder);
            if (dir == nullptr || !IsCcsProject(dir))
                throw gcnew IOException("Not a Code Composer Studio project: " + pathOrFolder);

            GrideProject^ p = gcnew GrideProject();
            p->Kind = "ccs7";
            p->IsReadOnly = true;
            p->FilePath = Path::Combine(dir, ".project");
            p->Name = Path::GetFileName(dir);
            p->Template = "CCS 7.4 project";
            p->Targets->Add(Target::C6747);
            p->Ccs = gcnew CcsInfo();

            List<String^>^ files = gcnew List<String^>();
            ReadProjectFile(p, Path::Combine(dir, ".project"), files);
            ReadCcsProjectFile(p, Path::Combine(dir, ".ccsproject"));
            ReadCProjectFile(p, Path::Combine(dir, ".cproject"), cgToolRoot);
            ScanFolder(dir, dir, files, 0);

            // group files by their folder relative to the project
            bool anyCpp = false;
            for each (String^ abs in files)
            {
                String^ rel = PathUtil::MakeRelative(dir, abs);
                String^ folder = Path::GetDirectoryName(rel->Replace(L'/', L'\\'));
                if (String::IsNullOrEmpty(folder)) folder = "(project root)";
                if (Path::IsPathRooted(rel)) folder = "(linked)";
                folder = folder->Replace(L'\\', L'/');
                ProjectFolder^ f = p->GetOrAddFolder(folder);
                if (!f->Files->Contains(rel)) f->Files->Add(rel);
                if (PathUtil::HasExtension(abs, ".cpp")) anyCpp = true;
            }
            p->Lang = anyCpp ? Language::Cpp11 : Language::C90;

            if (p->Configurations->Count == 0)
            {
                p->Configurations->Add(gcnew BuildConfiguration("Debug", Target::C6747));
                p->Configurations->Add(gcnew BuildConfiguration("Release", Target::C6747));
                for each (BuildConfiguration^ c in p->Configurations)
                {
                    c->OutDir = ".gride/out/" + c->Config;
                    c->ObjDir = ".gride/obj/" + c->Config;
                }
            }
            if (!String::IsNullOrEmpty(p->Ccs->LinkerCommandFile))
                for each (BuildConfiguration^ c in p->Configurations)
                    c->LinkerCmd = p->Ccs->LinkerCommandFile;

            p->IsDirty = false;
            return p;
        }

    private:
        static String^ Attr(XmlNode^ n, String^ name)
        {
            if (n == nullptr || n->Attributes == nullptr) return nullptr;
            XmlNode^ a = n->Attributes->GetNamedItem(name);
            if (a == nullptr) return nullptr;
            return a->Value;
        }

        static XmlDocument^ LoadXml(String^ file)
        {
            if (!File::Exists(file)) return nullptr;
            try
            {
                XmlDocument^ d = gcnew XmlDocument();
                d->Load(file);
                return d;
            }
            catch (Exception^) { return nullptr; }
        }

        // ---- .project : name + linked resources ---------------------------------
        static void ReadProjectFile(GrideProject^ p, String^ file, List<String^>^ files)
        {
            XmlDocument^ d = LoadXml(file);
            if (d == nullptr) return;
            XmlNode^ name = d->SelectSingleNode("/projectDescription/name");
            if (name != nullptr && !String::IsNullOrEmpty(name->InnerText)) p->Name = name->InnerText->Trim();

            String^ dir = Path::GetDirectoryName(file);
            for each (XmlNode^ link in d->SelectNodes("/projectDescription/linkedResources/link"))
            {
                XmlNode^ type = link->SelectSingleNode("type");
                XmlNode^ loc = link->SelectSingleNode("location");
                if (loc == nullptr) loc = link->SelectSingleNode("locationURI");
                if (loc == nullptr) continue;
                String^ path = ResolveLocation(loc->InnerText->Trim(), dir);
                if (path == nullptr) continue;
                if (type != nullptr && type->InnerText->Trim() == "2")
                {
                    if (Directory::Exists(path)) ScanFolder(path, path, files, 0);
                }
                else if (File::Exists(path) && !files->Contains(path)) files->Add(path);
            }
        }

        // "PARENT-2-PROJECT_LOC/common/x.c", "PROJECT_LOC/x.c", "C:/abs/x.c"
        static String^ ResolveLocation(String^ loc, String^ projDir)
        {
            if (String::IsNullOrEmpty(loc)) return nullptr;
            loc = loc->Replace("file:/", "");
            Match^ m = Regex::Match(loc, "^PARENT-(\\d+)-PROJECT_LOC/(.*)$");
            if (m->Success)
            {
                int up = Int32::Parse(m->Groups[1]->Value);
                String^ baseDir = projDir;
                for (int i = 0; i < up && baseDir != nullptr; i++) baseDir = Path::GetDirectoryName(baseDir);
                if (baseDir == nullptr) return nullptr;
                return PathUtil::MakeAbsolute(baseDir, m->Groups[2]->Value);
            }
            if (loc->StartsWith("PROJECT_LOC/")) return PathUtil::MakeAbsolute(projDir, loc->Substring(12));
            if (loc->Contains("$") || loc->Contains("_LOC")) return nullptr;   // unknown variable
            return PathUtil::MakeAbsolute(projDir, loc);
        }

        // ---- .ccsproject : device + tools version ----------------------------------
        static void ReadCcsProjectFile(GrideProject^ p, String^ file)
        {
            XmlDocument^ d = LoadXml(file);
            if (d == nullptr) return;
            String^ v;
            v = Attr(d->SelectSingleNode("//deviceVariant"), "value");      if (v != nullptr) p->Ccs->DeviceVariant = v;
            v = Attr(d->SelectSingleNode("//deviceFamily"), "value");       if (v != nullptr) p->Ccs->DeviceFamily = v;
            v = Attr(d->SelectSingleNode("//codegenToolVersion"), "value"); if (v != nullptr) p->Ccs->CodegenVersion = v;
            v = Attr(d->SelectSingleNode("//outputFormat"), "value");       if (v != nullptr) p->Ccs->OutputFormat = v;
            v = Attr(d->SelectSingleNode("//linkerCommandFile"), "value");  if (v != nullptr) p->Ccs->LinkerCommandFile = v;
        }

        // ---- .cproject : configurations ---------------------------------------------
        static void ReadCProjectFile(GrideProject^ p, String^ file, String^ cgRoot)
        {
            XmlDocument^ d = LoadXml(file);
            if (d == nullptr) return;
            String^ dir = Path::GetDirectoryName(file);

            for each (XmlNode^ cc in d->SelectNodes("//cconfiguration"))
            {
                XmlNode^ cfgNode = cc->SelectSingleNode(".//configuration");
                String^ cfgName = Attr(cfgNode, "name");
                if (String::IsNullOrEmpty(cfgName)) continue;

                BuildConfiguration^ c = gcnew BuildConfiguration(cfgName, Target::C6747);
                c->Defines->Clear(); c->IncludeDirs->Clear(); c->Libs->Clear(); c->LibDirs->Clear();
                // GRIDE builds into .gride\... so CCS's own <Config> folder is never touched
                c->OutDir = ".gride/out/" + cfgName;
                c->ObjDir = ".gride/obj/" + cfgName;

                for each (XmlNode^ opt in cc->SelectNodes(".//option"))
                {
                    String^ id = Attr(opt, "superClass");
                    if (id == nullptr) id = Attr(opt, "id");
                    if (id == nullptr) continue;
                    String^ ID = id->ToUpperInvariant();
                    List<String^>^ values = OptionValues(opt, dir, cgRoot, p->Name);

                    if (ID->Contains("INCLUDE_PATH"))            c->IncludeDirs->AddRange(values);
                    else if (ID->Contains("UNDEFINE"))           { }
                    else if (ID->Contains("DEFINE"))             c->Defines->AddRange(values);
                    else if (ID->Contains("OPT_LEVEL"))          c->Optimise = OptLevel(Attr(opt, "value"));
                    else if (ID->Contains("LINKER") && ID->Contains("SEARCH_PATH")) c->LibDirs->AddRange(values);
                    else if (ID->Contains("LINKER") && ID->Contains("LIBRARY"))     c->Libs->AddRange(values);
                    else if (ID->Contains("LINKER") && ID->Contains("OUTPUT_FILE") && values->Count > 0)
                        c->OutputName = Path::GetFileNameWithoutExtension(values[0]);
                }
                p->Configurations->Add(c);
            }
        }

        static List<String^>^ OptionValues(XmlNode^ opt, String^ projDir, String^ cgRoot, String^ projName)
        {
            List<String^>^ r = gcnew List<String^>();
            String^ single = Attr(opt, "value");
            if (!String::IsNullOrEmpty(single) && !single->StartsWith("com.ti")) r->Add(Expand(single, projDir, cgRoot, projName));
            for each (XmlNode^ lv in opt->SelectNodes("listOptionValue"))
            {
                String^ v = Attr(lv, "value");
                if (!String::IsNullOrEmpty(v)) r->Add(Expand(v, projDir, cgRoot, projName));
            }
            return r;
        }

        static String^ Expand(String^ v, String^ projDir, String^ cgRoot, String^ projName)
        {
            v = v->Trim()->Trim(L'"');
            v = v->Replace("${PROJECT_ROOT}", projDir)->Replace("${PROJECT_LOC}", projDir);
            v = v->Replace("${ProjName}", projName);
            if (!String::IsNullOrEmpty(cgRoot)) v = v->Replace("${CG_TOOL_ROOT}", cgRoot);
            if (Path::IsPathRooted(v) && !v->Contains("${"))
            {
                String^ rel = PathUtil::MakeRelative(projDir, v);
                if (!rel->StartsWith("..")) return rel;   // inside the project: keep it relative
            }
            return v;
        }

        // "com.ti...OPT_LEVEL.2" -> "O2", "...OPT_LEVEL.off" -> "O0"
        static String^ OptLevel(String^ v)
        {
            if (String::IsNullOrEmpty(v)) return "O0";
            int dot = v->LastIndexOf(L'.');
            String^ last = dot >= 0 ? v->Substring(dot + 1) : v;
            if (last->Length == 1 && Char::IsDigit(last[0])) return "O" + last;
            return "O0";
        }

        // ---- folder scan -----------------------------------------------------------
        static bool IsInteresting(String^ file)
        {
            String^ e = Path::GetExtension(file)->ToLowerInvariant();
            return e == ".c" || e == ".cpp" || e == ".h" || e == ".hpp" || e == ".asm" || e == ".s" ||
                   e == ".sa" || e == ".cmd" || e == ".inc" || e == ".ccxml" || e == ".shl" || e == ".cfg";
        }

        static bool IsSkippedFolder(String^ name)
        {
            String^ n = name->ToLowerInvariant();
            return n->StartsWith(".") || n == "debug" || n == "release" || n == "out" || n == "obj";
        }

        static void ScanFolder(String^ root, String^ dir, List<String^>^ files, int depth)
        {
            if (depth > 8) return;
            try
            {
                for each (String^ f in Directory::GetFiles(dir))
                    if (IsInteresting(f) && !files->Contains(f)) files->Add(f);
                for each (String^ sub in Directory::GetDirectories(dir))
                    if (!IsSkippedFolder(Path::GetFileName(sub))) ScanFolder(root, sub, files, depth + 1);
            }
            catch (UnauthorizedAccessException^) { }
        }
    };
}}
