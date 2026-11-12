#pragma once

// common\AboutBox.h - the About box the three outlooks share, and the one way they ask the toolchain what it is.
//
// **Why it is shared.** Black, Grey and White each had an About box of their own, saying different things in
// different words: one showed the copyright, one a build date, one four compilers, the others cpp11 alone.
//
// **Four groups** (the user's layout, 2026-11-12): the compilers first; the other tools next, the assemblers and
// linkers; then the runtime libraries and what debugs, a group of their own; the editors' versions at the end.
// Every line is one short string - a name, a number and what it is in four words:
//     masm 1.0 - x64 assembler, MASM syntax
// No line of the first three groups is written here. Every program of the toolchain answers `--version` with such a
// line first, and every runtime library holds one (RTS6x's __rts6x_version, grt's __grt_version, shmrt's
// shm_runtime_version); the box asks and reads as it opens, so it says what this installation holds, and a
// missing, old or foreign program shows as one. The editors' lines are Editors.h's, which each editor takes its
// own number from.
//
// An outlook fills an AboutInfo - which outlook it is, how it finds a program - and calls AboutBox::Open.
// C++/CLI, header only: each outlook includes it by relative path. Every name is qualified, because the three
// outlooks open different namespaces around it and White has <windows.h> in the same translation unit. Plain
// ASCII throughout: the three compile it in different code pages.

#include "Editors.h"

namespace GrideAbout {

    // ------------------------------------------------------------------ one component's answer
    public ref class VersionAnswer
    {
    public:
        System::String^ Name;      // as asked for: "cpp11", "RTS6x"
        System::String^ File;      // the file that answered, or was looked for
        System::String^ Version;   // "1.66"; nullptr when there is none to report
        System::String^ What;      // "C++03 with C++11 extensions"
        System::String^ Problem;   // why there is no version, in a few words

        // as the box shows it: "cpp11 1.66 - C++03 with C++11 extensions", "masm - not found beside GRIDE"
        System::String^ Line()
        {
            if (Version == nullptr) return Name + " - " + (Problem == nullptr ? "no version to report" : Problem);
            return Name + " " + Version + " - " + What;
        }
    };

    // ------------------------------------------------------------------ asking
    public ref class VersionProbe abstract sealed
    {
    public:
        // `name x.y - what it is`, the name in any case; fills Version and What
        static bool Read(System::String^ name, System::String^ line, VersionAnswer^ a)
        {
            if (System::String::IsNullOrEmpty(line)) return false;
            System::Text::RegularExpressions::Match^ m = System::Text::RegularExpressions::Regex::Match(line->Trim(),
                "^" + System::Text::RegularExpressions::Regex::Escape(name) + " (?<v>\\d+(?:\\.\\d+)+) - (?<w>.+)$",
                System::Text::RegularExpressions::RegexOptions::IgnoreCase);
            if (!m->Success) return false;
            a->Version = m->Groups["v"]->Value;
            a->What = m->Groups["w"]->Value->Trim();
            return true;
        }

        // The programs, each run with --version: all started, then all read, so eleven take the time of one.
        // files[i] is the program a build would run, or nullptr when the outlook did not find it.
        static array<VersionAnswer^>^ AskPrograms(array<System::String^>^ names, array<System::String^>^ files)
        {
            int n = names->Length;
            array<VersionAnswer^>^ said = gcnew array<VersionAnswer^>(n);
            array<System::Diagnostics::Process^>^ run = gcnew array<System::Diagnostics::Process^>(n);
            array<System::Threading::Tasks::Task<System::String^>^>^ outText = gcnew array<System::Threading::Tasks::Task<System::String^>^>(n);
            array<System::Threading::Tasks::Task<System::String^>^>^ errText = gcnew array<System::Threading::Tasks::Task<System::String^>^>(n);
            for (int i = 0; i < n; i++)
            {
                VersionAnswer^ a = gcnew VersionAnswer();
                a->Name = names[i];
                a->File = files[i];
                said[i] = a;
                if (System::String::IsNullOrEmpty(files[i]) || !System::IO::File::Exists(files[i])) { a->Problem = "not found beside GRIDE"; continue; }
                try
                {
                    System::Diagnostics::ProcessStartInfo^ si = gcnew System::Diagnostics::ProcessStartInfo(files[i], "--version");
                    si->UseShellExecute = false;
                    si->CreateNoWindow = true;
                    si->RedirectStandardInput = true;
                    si->RedirectStandardOutput = true;
                    si->RedirectStandardError = true;
                    si->StandardOutputEncoding = System::Text::Encoding::UTF8;
                    si->StandardErrorEncoding = System::Text::Encoding::UTF8;
                    run[i] = System::Diagnostics::Process::Start(si);
                    run[i]->StandardInput->Close();
                    outText[i] = run[i]->StandardOutput->ReadToEndAsync();
                    errText[i] = run[i]->StandardError->ReadToEndAsync();
                }
                catch (System::Exception^) { run[i] = nullptr; a->Problem = "found, cannot be run"; }
            }
            // one allowance for them all: a program that does not know --version may wait for something
            System::DateTime until = System::DateTime::UtcNow.AddSeconds(5);
            for (int i = 0; i < n; i++)
            {
                if (run[i] == nullptr) continue;
                VersionAnswer^ a = said[i];
                try
                {
                    int left = static_cast<int>((until - System::DateTime::UtcNow).TotalMilliseconds);
                    if (!run[i]->WaitForExit(left > 50 ? left : 50))
                    {
                        try { run[i]->Kill(); } catch (System::Exception^) { }
                        a->Problem = "gave no answer";
                        continue;
                    }
                    System::String^ text = outText[i]->Wait(1000) ? outText[i]->Result : System::String::Empty;
                    System::String^ first = nullptr;
                    for each (System::String^ raw in text->Split(gcnew array<wchar_t>{ L'\r', L'\n' }, System::StringSplitOptions::RemoveEmptyEntries))
                        if (raw->Trim()->Length > 0) { first = raw->Trim(); break; }
                    if (Read(a->Name, first, a)) continue;
                    // not the shape: an earlier release of the program, or somebody else's program of the same name
                    a->Problem = "no version, not this release's";
                }
                catch (System::Exception^) { a->Problem = "found, cannot be asked"; }
                finally { delete run[i]; }
            }
            return said;
        }

        // A runtime library: the string `name x.y - what it is` among its bytes.
        static VersionAnswer^ AskLibrary(System::String^ name, System::String^ file)
        {
            VersionAnswer^ a = gcnew VersionAnswer();
            a->Name = name;
            a->File = file;
            if (System::String::IsNullOrEmpty(file) || !System::IO::File::Exists(file)) { a->Problem = "not found beside GRIDE"; return a; }
            try
            {
                // one byte, one character: the library is searched as it lies on the disk
                System::String^ text = System::Text::Encoding::GetEncoding(28591)->GetString(System::IO::File::ReadAllBytes(file));
                System::String^ lead = name + " ";
                for (int at = text->IndexOf(lead, System::StringComparison::Ordinal); at >= 0; at = text->IndexOf(lead, at + 1, System::StringComparison::Ordinal))
                {
                    if (at > 0 && (System::Char::IsLetterOrDigit(text[at - 1]) || text[at - 1] == L'_')) continue;
                    int end = at;
                    while (end < text->Length && end - at < 200 && text[end] >= L' ' && text[end] < 127) end++;
                    if (Read(name, text->Substring(at, end - at), a)) return a;
                }
                a->Problem = "no version, not this release's";
            }
            catch (System::Exception^) { a->Problem = "found, cannot be read"; }
            return a;
        }
    };

    // ------------------------------------------------------------------ what an outlook says of itself
    // "cpp11.exe" -> the program a build would run, or nullptr; an outlook's own search, so that the box asks the
    // program its builds use and not merely the one beside it
    public delegate System::String^ ProgramFinder(System::String^ fileName);

    public ref class AboutInfo
    {
    public:
        System::String^ Outlook;        // "Black", "Grey" or "White": the editor that is running
        ProgramFinder^ Find;            // nullptr: beside this program
        System::Drawing::Icon^ Icon;    // the outlook's own; nullptr: none shown
        // The box's colours come from the outlook itself: its window's background, the text on it, and the colour it
        // marks things with (the button that closes the box wears it). Each may be left empty: the system's
        // background, text by the background's brightness, a plain button.
        System::Drawing::Color Back, Fore, Accent, AccentFore;
    };

    // one of the four groups: its heading and its lines, one of which may be this editor's own
    public ref class AboutPart
    {
    public:
        System::String^ Title;
        System::Collections::Generic::List<System::String^>^ Lines;
        int Own;    // the line that is the running editor's, or -1
        AboutPart(System::String^ title) { Title = title; Lines = gcnew System::Collections::Generic::List<System::String^>(); Own = -1; }
    };

    // ------------------------------------------------------------------ what the box says
    public ref class AboutText
    {
    public:
        System::String^ Product;     // "GRIDE"
        System::String^ Outlook;     // "Black outlook", with ", Setup 1.6.3" where a Setup installed it
        System::String^ Owner;
        System::Collections::Generic::List<AboutPart^>^ Parts;   // Compilers; Other tools; Runtime, library, debug; Editors

        // the folder this program runs from: the toolchain and lib\ are beside it
        static System::String^ Here() { return System::Windows::Forms::Application::StartupPath; }

        // "GRIDE Setup 1.6.3" is the first line of setup.txt, which Setup writes beside the program it installs
        static System::String^ SetupLine()
        {
            try
            {
                System::String^ file = System::IO::Path::Combine(Here(), "setup.txt");
                if (!System::IO::File::Exists(file)) return nullptr;
                array<System::String^>^ lines = System::IO::File::ReadAllLines(file);
                if (lines->Length == 0) return nullptr;
                System::Text::RegularExpressions::Match^ m = System::Text::RegularExpressions::Regex::Match(lines[0], "^GRIDE Setup (\\d+(?:\\.\\d+)+)");
                return m->Success ? "Setup " + m->Groups[1]->Value : nullptr;
            }
            catch (System::Exception^) { return nullptr; }
        }

        static AboutText^ Gather(AboutInfo^ info)
        {
            AboutText^ t = gcnew AboutText();
            t->Product = "GRIDE";
            System::String^ setup = SetupLine();
            t->Outlook = info->Outlook + " outlook" + (setup == nullptr ? System::String::Empty : ", " + setup);
            // the copyright sign by its number, 169: this file is read in whatever code page each outlook compiles in
            t->Owner = System::Char::ToString(static_cast<wchar_t>(169)) + "2026 G. R. Akhtar, Islamabad, Pakistan";
            t->Parts = gcnew System::Collections::Generic::List<AboutPart^>();

            // the eleven programs: four compilers, the two assemblers and two linkers, the three that debug
            array<System::String^>^ names = gcnew array<System::String^>{
                "cpp11", "c90", "shalimar", "c2s", "masm", "link", "asm6x", "lnk6x", "gride-dbg", "sim6747", "vm6747" };
            array<System::String^>^ files = gcnew array<System::String^>(names->Length);
            for (int i = 0; i < names->Length; i++)
            {
                System::String^ exe = names[i] + ".exe";
                System::String^ found = nullptr;
                try { if (info->Find != nullptr) found = info->Find(exe); } catch (System::Exception^) { found = nullptr; }
                if (System::String::IsNullOrEmpty(found) || !System::IO::File::Exists(found))
                {
                    System::String^ beside = System::IO::Path::Combine(Here(), exe);
                    if (System::IO::File::Exists(beside)) found = beside;
                }
                files[i] = found;
            }
            array<VersionAnswer^>^ programs = VersionProbe::AskPrograms(names, files);

            AboutPart^ compilers = gcnew AboutPart("Compilers");
            for (int i = 0; i < 4; i++) compilers->Lines->Add(programs[i]->Line());
            t->Parts->Add(compilers);

            // the other tools: what turns a compiler's assembly into a program, for Windows and for the C6747
            AboutPart^ tools = gcnew AboutPart("Other tools");
            for (int i = 4; i < 8; i++) tools->Lines->Add(programs[i]->Line());
            t->Parts->Add(tools);

            // what a program runs on and is debugged with, a group of its own (the user's, 2026-11-12): the four
            // runtime libraries beside this program, each read for its own line, then the debugger for Windows and
            // the simulator and the emulator, which run and debug a C6747 program
            AboutPart^ runtime = gcnew AboutPart("Runtime, library, debug");
            System::String^ win = System::IO::Path::Combine(Here(), "lib\\x86_64-windows");
            System::String^ tms = System::IO::Path::Combine(Here(), "lib\\tms6747");
            VersionAnswer^ shmrt = VersionProbe::AskLibrary("shmrt", System::IO::Path::Combine(win, "shmrt.lib"));
            if (shmrt->Version == nullptr) shmrt = VersionProbe::AskLibrary("shmrt", System::IO::Path::Combine(tms, "shmrt6x.lib"));
            runtime->Lines->Add(VersionProbe::AskLibrary("grt", System::IO::Path::Combine(win, "grtc.lib"))->Line());
            runtime->Lines->Add(VersionProbe::AskLibrary("RTS6x", System::IO::Path::Combine(tms, "rts6x.lib"))->Line());
            runtime->Lines->Add(VersionProbe::AskLibrary("printf6x", System::IO::Path::Combine(tms, "printf6x.lib"))->Line());
            runtime->Lines->Add(shmrt->Line());
            for (int i = 8; i < programs->Length; i++) runtime->Lines->Add(programs[i]->Line());
            t->Parts->Add(runtime);

            // the editors: the three outlooks of this release (Editors.h), the running one marked
            AboutPart^ editors = gcnew AboutPart("Editors");
            array<System::String^>^ outlooks = gcnew array<System::String^>{ "Black", "Grey", "White" };
            array<System::String^>^ lines = gcnew array<System::String^>{
                "Black " GRIDE_BLACK_VERSION " - " GRIDE_BLACK_WHAT,
                "Grey " GRIDE_GREY_VERSION " - " GRIDE_GREY_WHAT,
                "White " GRIDE_WHITE_VERSION " - " GRIDE_WHITE_WHAT };
            for (int i = 0; i < outlooks->Length; i++)
            {
                if (System::String::Compare(outlooks[i], info->Outlook, System::StringComparison::OrdinalIgnoreCase) == 0) editors->Own = i;
                editors->Lines->Add(lines[i]);
            }
            t->Parts->Add(editors);
            return t;
        }

        // the same as plain text, for the Copy button and for a report: the running editor's line begins with ">"
        System::String^ Plain()
        {
            System::Text::StringBuilder^ sb = gcnew System::Text::StringBuilder();
            sb->Append(Product)->Append(" - ")->Append(Outlook)->Append("\r\n")->Append(Owner)->Append("\r\n");
            for each (AboutPart^ part in Parts)
            {
                sb->Append("\r\n")->Append(part->Title)->Append("\r\n");
                for (int i = 0; i < part->Lines->Count; i++) sb->Append(i == part->Own ? "> " : "  ")->Append(part->Lines[i])->Append("\r\n");
            }
            return sb->ToString();
        }
    };

    // ------------------------------------------------------------------ the box
    public ref class AboutBox : public System::Windows::Forms::Form
    {
    public:
        AboutBox(AboutInfo^ info)
        {
            said = AboutText::Gather(info);

            Text = "About GRIDE";
            FormBorderStyle = System::Windows::Forms::FormBorderStyle::FixedDialog;
            StartPosition = System::Windows::Forms::FormStartPosition::CenterParent;
            MinimizeBox = false;
            MaximizeBox = false;
            ShowInTaskbar = false;
            Font = gcnew System::Drawing::Font("Segoe UI", 9.0f);
            AutoSize = true;
            AutoSizeMode = System::Windows::Forms::AutoSizeMode::GrowAndShrink;
            // The outlook's own colours (below); every panel and line takes them from the box. An outlook that gives
            // no background gets the system's, and text is light on a dark background and dark on a light one.
            back = Mix(info->Back.IsEmpty ? System::Drawing::SystemColors::Control : info->Back, System::Drawing::Color::Black, 0);
            fore = !info->Fore.IsEmpty ? Mix(info->Fore, System::Drawing::Color::Black, 0)
                 : back.GetBrightness() < 0.5f ? Shade(214, 217, 222) : Shade(30, 30, 30);
            accent = info->Accent;
            accentFore = !info->AccentFore.IsEmpty ? info->AccentFore
                       : accent.GetBrightness() < 0.5f ? Shade(255, 255, 255) : Shade(21, 23, 26);
            BackColor = back;
            ForeColor = fore;

            System::Windows::Forms::TableLayoutPanel^ root = gcnew System::Windows::Forms::TableLayoutPanel();
            root->AutoSize = true;
            root->AutoSizeMode = System::Windows::Forms::AutoSizeMode::GrowAndShrink;
            root->ColumnCount = 1;
            root->Location = System::Drawing::Point(0, 0);
            root->Margin = System::Windows::Forms::Padding(0);

            // the head: the outlook's icon, the product, which outlook and which Setup, whose it is
            System::Windows::Forms::TableLayoutPanel^ head = Grid(2);
            head->Padding = System::Windows::Forms::Padding(20, 16, 28, 0);
            if (info->Icon != nullptr)
            {
                System::Windows::Forms::PictureBox^ logo = gcnew System::Windows::Forms::PictureBox();
                logo->Image = (gcnew System::Drawing::Icon(info->Icon, 48, 48))->ToBitmap();
                logo->SizeMode = System::Windows::Forms::PictureBoxSizeMode::CenterImage;
                logo->Size = System::Drawing::Size(56, 56);
                logo->Margin = System::Windows::Forms::Padding(0, 4, 16, 0);
                head->Controls->Add(logo, 0, 0);
                head->SetRowSpan(logo, 3);
            }
            System::Windows::Forms::Label^ product = Line(said->Product);
            product->Font = gcnew System::Drawing::Font("Segoe UI Semibold", 20.0f);
            product->Margin = System::Windows::Forms::Padding(0);
            head->Controls->Add(product, 1, 0);
            head->Controls->Add(Line(said->Outlook), 1, 1);
            head->Controls->Add(Line(said->Owner), 1, 2);
            root->Controls->Add(head, 0, 0);

            // The four groups two by two, read as a page is: the compilers and the other tools above, the runtime
            // group and the editors below - so the box lies landscape, as a version window is expected to (the
            // user's, 2026-11-12), and the editors still come last. Each group is a heading and its lines.
            System::Windows::Forms::TableLayoutPanel^ body = Grid(2);
            body->Padding = System::Windows::Forms::Padding(20, 4, 0, 10);
            int cell = 0;
            for each (AboutPart^ part in said->Parts)
            {
                System::Windows::Forms::TableLayoutPanel^ group = Grid(1);
                group->Margin = System::Windows::Forms::Padding(0, 0, 28, 0);
                int at = 0;
                System::Windows::Forms::Label^ title = Line(part->Title);
                title->Font = gcnew System::Drawing::Font(Font, System::Drawing::FontStyle::Bold);
                title->Margin = System::Windows::Forms::Padding(0, 10, 0, 2);
                group->Controls->Add(title, 0, at++);
                for (int i = 0; i < part->Lines->Count; i++)
                {
                    System::Windows::Forms::Label^ line = Line(part->Lines[i]);
                    line->Margin = System::Windows::Forms::Padding(16, 1, 0, 1);
                    // the running editor's own line stands out among the three
                    if (i == part->Own) line->Font = gcnew System::Drawing::Font(Font, System::Drawing::FontStyle::Bold);
                    group->Controls->Add(line, 0, at++);
                }
                body->Controls->Add(group, cell % 2, cell / 2);
                cell++;
            }
            root->Controls->Add(body, 0, 1);

            System::Windows::Forms::FlowLayoutPanel^ buttons = gcnew System::Windows::Forms::FlowLayoutPanel();
            buttons->FlowDirection = System::Windows::Forms::FlowDirection::RightToLeft;
            buttons->AutoSize = true;
            buttons->AutoSizeMode = System::Windows::Forms::AutoSizeMode::GrowAndShrink;
            buttons->Dock = System::Windows::Forms::DockStyle::Fill;
            buttons->Padding = System::Windows::Forms::Padding(12, 8, 12, 10);
            buttons->Margin = System::Windows::Forms::Padding(0);
            buttons->BackColor = Mix(back, fore, 6);   // the strip the buttons stand on, a shade off the background
            System::Windows::Forms::Button^ ok = Push("OK", true);
            ok->DialogResult = System::Windows::Forms::DialogResult::OK;
            System::Windows::Forms::Button^ copy = Push("Copy", false);
            copy->Click += gcnew System::EventHandler(this, &AboutBox::OnCopy);
            buttons->Controls->Add(ok);
            buttons->Controls->Add(copy);
            root->Controls->Add(buttons, 0, 2);

            Controls->Add(root);
            AcceptButton = ok;
            CancelButton = ok;
        }

        // the box, shown over its owner and gone when it is closed
        static void Open(System::Windows::Forms::IWin32Window^ owner, AboutInfo^ info)
        {
            AboutBox^ box = gcnew AboutBox(info);
            try { box->ShowDialog(owner); }
            finally { delete box; }
        }

    private:
        AboutText^ said;

        // **The colours are the outlook's own** (the user's, 2026-11-12: the background of About comes from the
        // outlook itself). The outlook gives its background, and may give its text and accent colours; what lies
        // between them - the strip the buttons stand on, a button's edge, the colour under the mouse - is mixed here
        // from the two, so the box sits right on a black, a grey and a white window alike.
        System::Drawing::Color back, fore, accent, accentFore;

        static System::Drawing::Color Shade(int r, int g, int b) { return System::Drawing::Color::FromArgb(r, g, b); }

        // `from` moved `percent` of the way to `to`
        static System::Drawing::Color Mix(System::Drawing::Color from, System::Drawing::Color to, int percent)
        {
            return System::Drawing::Color::FromArgb(from.R + (to.R - from.R) * percent / 100,
                                                    from.G + (to.G - from.G) * percent / 100,
                                                    from.B + (to.B - from.B) * percent / 100);
        }

        static System::Windows::Forms::TableLayoutPanel^ Grid(int columns)
        {
            System::Windows::Forms::TableLayoutPanel^ g = gcnew System::Windows::Forms::TableLayoutPanel();
            g->AutoSize = true;
            g->AutoSizeMode = System::Windows::Forms::AutoSizeMode::GrowAndShrink;
            g->ColumnCount = columns;
            g->Margin = System::Windows::Forms::Padding(0);
            return g;
        }

        // a line of text as wide as it needs
        static System::Windows::Forms::Label^ Line(System::String^ text)
        {
            System::Windows::Forms::Label^ l = gcnew System::Windows::Forms::Label();
            l->Text = text;
            l->AutoSize = true;
            l->UseMnemonic = false;
            l->MaximumSize = System::Drawing::Size(620, 0);
            l->Margin = System::Windows::Forms::Padding(0, 3, 0, 3);
            return l;
        }

        // a flat button in the box's colours; the one that closes it wears the outlook's accent where it gave one
        System::Windows::Forms::Button^ Push(System::String^ text, bool closes)
        {
            System::Windows::Forms::Button^ b = gcnew System::Windows::Forms::Button();
            b->Text = text;
            b->Size = System::Drawing::Size(88, 28);
            b->Margin = System::Windows::Forms::Padding(4);
            b->UseVisualStyleBackColor = false;
            b->FlatStyle = System::Windows::Forms::FlatStyle::Flat;
            if (closes && !accent.IsEmpty)
            {
                b->BackColor = accent;
                b->ForeColor = accentFore;
                b->FlatAppearance->BorderColor = accent;
                b->FlatAppearance->MouseOverBackColor = Mix(accent, back, 20);
            }
            else
            {
                b->BackColor = Mix(back, fore, 6);
                b->ForeColor = fore;
                b->FlatAppearance->BorderColor = Mix(back, fore, 30);
                b->FlatAppearance->MouseOverBackColor = Mix(back, fore, 16);
            }
            return b;
        }

        void OnCopy(System::Object^ sender, System::EventArgs^ e)
        {
            try { System::Windows::Forms::Clipboard::SetText(said->Plain()); }
            catch (System::Exception^) { }
        }
    };
}
