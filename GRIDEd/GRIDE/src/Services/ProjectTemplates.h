// =============================================================================
//  ProjectTemplates.h - starter files for File > New > Project and
//  Project > Add New Item. Content is plain text; edit freely.
// =============================================================================
#pragma once
#include "../Model/GrideProject.h"

namespace Gride { namespace Services {

    using namespace Gride::Core;
    using namespace Gride::Model;

    public ref class ItemTemplate
    {
    public:
        ItemTemplate(String^ name, String^ ext, String^ folder) { Name = name; Extension = ext; Folder = folder; }
        property String^ Name;
        property String^ Extension;
        property String^ Folder;
        virtual String^ ToString() override { return Name + "  (" + Extension + ")"; }
    };

    public ref class ProjectTemplates abstract sealed
    {
    public:
        // template names shown in the New Project dialog, per language
        static array<String^>^ For(Language l)
        {
            if (l == Language::Shalimar)
                return gcnew array<String^>{ "Shalimar Program", "Shalimar Module", "From C Source", "Empty Project" };
            return gcnew array<String^>{ "Console Application", "DSP Application (C6747)", "Static Library", "Empty Project" };
        }

        static String^ Describe(String^ t)
        {
            if (t == "Console Application") return "Host program for Windows / Linux / macOS.";
            if (t == "DSP Application (C6747)") return "Bare-metal main + RTS6747 and a C6747 linker command file.";
            if (t == "Static Library") return "Reusable library; objects are kept, no link step.";
            if (t == "Shalimar Program") return "Shalimar micro-compiler program for any target.";
            if (t == "Shalimar Module") return "Reusable Shalimar unit.";
            if (t == "From C Source") return "Start empty, then use Tools > Convert > C to Shalimar.";
            return "Only the .pro file and the src / inc / lib folders.";
        }

        static array<ItemTemplate^>^ Items()
        {
            return gcnew array<ItemTemplate^>{
                gcnew ItemTemplate("C90 source file", ".c", "src"),
                gcnew ItemTemplate("C90 header file", ".h", "inc"),
                gcnew ItemTemplate("C++11 source file", ".cpp", "src"),
                gcnew ItemTemplate("C++11 header file", ".hpp", "inc"),
                gcnew ItemTemplate("Shalimar source file", ".shl", "src"),
                gcnew ItemTemplate("Assembly file", ".asm", "src"),
                gcnew ItemTemplate("Linker command file", ".cmd", "linker"),
                gcnew ItemTemplate("Text file", ".txt", "doc") };
        }

        // writes the starter files of a new project and adds them to it
        static void Populate(GrideProject^ p)
        {
            String^ t = p->Template;
            String^ dir = p->ProjectDir;
            if (t == "Empty Project" || t == "From C Source") { p->Save(); return; }

            if (p->Lang == Language::Shalimar)
            {
                String^ name = "main.shl";
                if (t == "Shalimar Module") name = "module.shl";
                Write(p, Path::Combine(dir, "src\\" + name), ShalimarStarter(p->Name, t == "Shalimar Module"), "src");
            }
            else
            {
                bool cpp = p->Lang == Language::Cpp11;
                String^ hdrExt = ".h";
                if (cpp) hdrExt = ".hpp";
                String^ srcExt = ".c";
                if (cpp) srcExt = ".cpp";
                String^ baseName = p->Name->ToLowerInvariant();
                Write(p, Path::Combine(dir, "inc\\" + baseName + hdrExt), Header(p->Name), "inc");
                if (t == "Static Library")
                    Write(p, Path::Combine(dir, "src\\" + baseName + srcExt), LibrarySource(p->Name, baseName + hdrExt), "src");
                else
                    Write(p, Path::Combine(dir, "src\\main" + srcExt), MainSource(p, t == "DSP Application (C6747)", baseName + hdrExt), "src");
                if (t == "DSP Application (C6747)" || p->Targets->Contains(Target::C6747))
                    Write(p, Path::Combine(dir, "c6747.cmd"), LinkerCmd(), "linker");
            }
            p->Save();
        }

        // starter content for Project > Add New Item
        static String^ ItemContent(String^ fileName, Language projectLang)
        {
            String^ ext = Path::GetExtension(fileName)->ToLowerInvariant();
            String^ stem = Path::GetFileNameWithoutExtension(fileName);
            String^ guard = Regex::Replace(stem->ToUpperInvariant(), "[^A-Z0-9]", "_") + "_H";
            if (ext == ".h" || ext == ".hpp")
                return "#ifndef " + guard + "\r\n#define " + guard + "\r\n\r\n\r\n#endif /* " + guard + " */\r\n";
            if (ext == ".c")   return "/* " + fileName + " */\r\n\r\n";
            if (ext == ".cpp") return "// " + fileName + "\r\n\r\n";
            if (ext == ".shl") return "// " + fileName + " - Shalimar source\r\n\r\n";
            if (ext == ".asm") return "; " + fileName + "\r\n\r\n        .text\r\n\r\n";
            if (ext == ".cmd") return LinkerCmd();
            return String::Empty;
        }

    private:
        static void Write(GrideProject^ p, String^ path, String^ content, String^ folder)
        {
            PathUtil::EnsureDirectory(Path::GetDirectoryName(path));
            if (!File::Exists(path)) File::WriteAllText(path, content, gcnew UTF8Encoding(false));
            p->AddFile(path, folder);
        }

        static String^ Header(String^ name)
        {
            String^ g = Regex::Replace(name->ToUpperInvariant(), "[^A-Z0-9]", "_") + "_H";
            return "#ifndef " + g + "\r\n#define " + g + "\r\n\r\n/* " + name + " public interface */\r\n\r\n#endif /* " + g + " */\r\n";
        }

        static String^ MainSource(GrideProject^ p, bool dsp, String^ header)
        {
            bool cpp = p->Lang == Language::Cpp11;
            StringBuilder^ sb = gcnew StringBuilder();
            if (cpp) sb->Append("// main.cpp - " + p->Name + " (C++11)\r\n");
            else sb->Append("/* main.c - " + p->Name + " (C90) */\r\n");
            sb->Append("#include \"" + header + "\"\r\n");
            if (!dsp) sb->Append("#include <stdio.h>\r\n");
            sb->Append("\r\n");
            if (dsp)
            {
                sb->Append("int main(void)\r\n{\r\n");
                if (cpp) sb->Append("    // device initialisation goes here\r\n");
                else sb->Append("    /* device initialisation goes here */\r\n");
                sb->Append("    for (;;)\r\n    {\r\n    }\r\n}\r\n");
            }
            else
            {
                sb->Append("int main(void)\r\n{\r\n");
                sb->Append("    printf(\"Hello from " + p->Name + "\\n\");\r\n");
                sb->Append("    return 0;\r\n}\r\n");
            }
            return sb->ToString();
        }

        static String^ LibrarySource(String^ name, String^ header)
        {
            return "/* " + name + " library */\r\n#include \"" + header + "\"\r\n\r\n";
        }

        static String^ ShalimarStarter(String^ name, bool module)
        {
            String^ kind = "program";
            if (module) kind = "module";
            return "// " + name + " - Shalimar " + kind + "\r\n// [write Shalimar code here]\r\n\r\n";
        }

        // C6747: 256 KB L2 RAM at 0x11800000, 128 KB shared RAM at 0x80000000,
        // external SDRAM at 0xC0000000. Adjust to your board.
        static String^ LinkerCmd()
        {
            return
                "/* c6747.cmd - linker command file for TMS320C6747 (adjust to your board) */\r\n"
                "-stack 0x2000\r\n"
                "-heap  0x2000\r\n\r\n"
                "MEMORY\r\n{\r\n"
                "    L2RAM:   o = 0x11800000  l = 0x00040000   /* 256 KB internal L2 */\r\n"
                "    SHRAM:   o = 0x80000000  l = 0x00020000   /* 128 KB shared RAM  */\r\n"
                "    SDRAM:   o = 0xC0000000  l = 0x04000000   /* external SDRAM     */\r\n"
                "}\r\n\r\n"
                "SECTIONS\r\n{\r\n"
                "    .vectors  > L2RAM\r\n"
                "    .text     > L2RAM\r\n"
                "    .stack    > L2RAM\r\n"
                "    .bss      > L2RAM\r\n"
                "    .cinit    > L2RAM\r\n"
                "    .const    > L2RAM\r\n"
                "    .data     > L2RAM\r\n"
                "    .switch   > L2RAM\r\n"
                "    .sysmem   > SDRAM\r\n"
                "    .far      > SDRAM\r\n"
                "    .cio      > L2RAM\r\n"
                "}\r\n";
        }
    };
}}
