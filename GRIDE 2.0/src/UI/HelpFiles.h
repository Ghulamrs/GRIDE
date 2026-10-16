#pragma once
// ============================================================================
//  GRIDE 2.0 - Help (R13: GRIDEd's Help lookup and RIDE's help, as GRIDEd has them)
//    View Help (F1)        GRIDE_Help - GRIDE 2.0's own page: its example, which
//                          Help item opens which page, RIDE 5.17's Express Help
//                          with an "In GRIDE 2.0" note on each part
//    Language Reference    C90, C++11 (RIDE 5.17's Complete Manual at a chapter),
//                          Shalimar (the Shalimar language)
//    Toolchain Reference   MASM / LINK, ASM6x / LNK6x, RTS6747 (the manual's
//                          chapters), VM6747 / SIM6747 (GRIDE's own page)
//  Each is <name>.pdf / .chm / .html / .htm / .md / .txt in the first folder that
//  holds one: beside GRIDE.exe, docs and help under it, then ..\help and ..\docs
//  (an installed layout). Every build copies docs\ beside GRIDE.exe.
// ============================================================================
#include "../Common/Common.h"

namespace Gride
{
    // one Help item: the submenu it is under ("" for the Help menu itself), its text, the page's name
    public ref class HelpItem sealed
    {
    public:
        String^ Menu;
        String^ Text;
        String^ File;
        HelpItem(String^ menu, String^ text, String^ file) : Menu(menu), Text(text), File(file) {}
    };

    public ref class HelpFiles abstract sealed
    {
    public:
        literal String^ LanguageMenu = "&Language Reference";
        literal String^ ToolchainMenu = "&Toolchain Reference";
        literal String^ ViewHelpFile = "GRIDE_Help";

        static array<String^>^ Extensions() { return gcnew array<String^>{ ".pdf", ".chm", ".html", ".htm", ".md", ".txt" }; }

        // the Help menu's pages, in the menu's order (GRIDEd's)
        static List<HelpItem^>^ Items()
        {
            List<HelpItem^>^ r = gcnew List<HelpItem^>();
            r->Add(gcnew HelpItem("", "View &Help", ViewHelpFile));
            r->Add(gcnew HelpItem(LanguageMenu, "C90", "C90_Reference"));
            r->Add(gcnew HelpItem(LanguageMenu, "C++11", "Cpp11_Reference"));
            r->Add(gcnew HelpItem(LanguageMenu, "Shalimar", "Shalimar_Reference"));
            r->Add(gcnew HelpItem(ToolchainMenu, "MASM / LINK (Windows)", "MASM_LINK_Reference"));
            r->Add(gcnew HelpItem(ToolchainMenu, "ASM6x / LNK6x", "ASM6x_LNK6x_Reference"));
            r->Add(gcnew HelpItem(ToolchainMenu, "RTS6747 Runtime", "RTS6747_Reference"));
            r->Add(gcnew HelpItem(ToolchainMenu, "VM6747 / SIM6747", "VM6747_Reference"));
            return r;
        }

        // where the pages are looked for, in order (GRIDEd's, without its folder named under Options)
        static List<String^>^ Folders(String^ exeDir)
        {
            List<String^>^ dirs = gcnew List<String^>();
            String^ above = nullptr;
            try { above = Path::GetDirectoryName(exeDir); } catch (Exception^) { }
            for each (String^ d in gcnew array<String^>{ exeDir, Path::Combine(exeDir, "docs"), Path::Combine(exeDir, "help"),
                                                         above == nullptr ? nullptr : Path::Combine(above, "help"),
                                                         above == nullptr ? nullptr : Path::Combine(above, "docs") })
                if (d != nullptr && !dirs->Contains(d)) dirs->Add(d);
            return dirs;
        }

        // the first <folder>\<name><extension>, folders first, or null
        static String^ Find(String^ name, String^ exeDir)
        {
            for each (String^ dir in Folders(exeDir))
                for each (String^ ext in Extensions())
                {
                    String^ f = Path::Combine(dir, name + ext);
                    try { if (File::Exists(f)) return f; } catch (Exception^) { }
                }
            return nullptr;
        }
        static String^ Find(String^ name) { return Find(name, ExeDirectory()); }

        // what Help says when a page is not there: its name and every folder looked in
        static String^ Missing(String^ name, String^ exeDir)
        {
            return String::Concat("The help page ", name, " is not installed.\n\nPlace ", name,
                                  ".html (or .pdf / .chm / .htm / .md / .txt) in one of:\n", String::Join("\n", Folders(exeDir)),
                                  "\n\nThe build copies GRIDE's docs folder beside GRIDE.exe.");
        }

        // how a page is opened: the default program for it, unless the harness put its own here
        static Action<String^>^ Opener;
        static void Open(String^ path)
        {
            if (Opener != nullptr) Opener(path);
            else System::Diagnostics::Process::Start(path);
        }

        static String^ ExeDirectory()
        {
            try { return AppDomain::CurrentDomain->BaseDirectory->TrimEnd(L'\\'); }
            catch (Exception^) { return Environment::CurrentDirectory; }
        }
    };
}
