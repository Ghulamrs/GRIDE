// =============================================================================
//  ToolDialogs.h - Tools and Help dialogs
//    ConvertDialog         Tools > Convert > C <-> Shalimar
//    ToolchainCheckDialog  Tools > Toolchain Check
//    ExternalToolsDialog   Tools > External Tools
//    ShortcutsDialog       Help > Keyboard Shortcuts
//    AboutDialog           Help > About GRIDE
// =============================================================================
#pragma once
#include "OptionsDialog.h"
#include "../../Services/CodeConverter.h"

namespace Gride { namespace UI {

    using namespace Gride::Services;

    // -------------------------------------------------------------------------
    public ref class ConvertDialog : public ThemedDialog
    {
    public:
        ConvertDialog(ConverterSettings^ s, String^ input, ConvertDirection d) : ThemedDialog("Convert Source", 640, 420)
        {
            settings = s;
            OutputPath = nullptr;
            TableLayoutPanel^ grid = MakeGrid(110);
            inputField = gcnew PathField(false, false);
            inputField->Filter = "C and Shalimar sources (*.c;*.h;*.shl)|*.c;*.h;*.shl|All files (*.*)|*.*";
            inputField->Value = input == nullptr ? String::Empty : input;
            inputField->Controls[0]->TextChanged += gcnew EventHandler(this, &ConvertDialog::OnInputChanged);
            AddRow(grid, "Source file", inputField);
            FlowLayoutPanel^ dir = gcnew FlowLayoutPanel();
            dir->Height = 30;
            toShl = gcnew RadioButton(); toShl->Text = "C  ->  Shalimar"; toShl->AutoSize = true;
            toC = gcnew RadioButton(); toC->Text = "Shalimar  ->  C"; toC->AutoSize = true;
            toShl->Checked = d == ConvertDirection::CToShalimar;
            toC->Checked = d == ConvertDirection::ShalimarToC;
            toShl->CheckedChanged += gcnew EventHandler(this, &ConvertDialog::OnInputChanged);
            dir->Controls->Add(toShl); dir->Controls->Add(toC);
            AddRow(grid, "Direction", dir);
            outputBox = gcnew TextBox();
            AddRow(grid, "Output file", outputBox);
            Controls->Add(grid);

            logBox = gcnew TextBox();
            logBox->Multiline = true;
            logBox->ReadOnly = true;
            logBox->ScrollBars = WF::ScrollBars::Vertical;
            logBox->Dock = DockStyle::Fill;
            logBox->Font = gcnew SD::Font("Consolas", 9.0f);
            Panel^ host = gcnew Panel();
            host->Dock = DockStyle::Fill;
            host->Padding = WF::Padding(10);
            host->Controls->Add(logBox);
            Controls->Add(host);
            host->BringToFront();
            AddOkCancel("Convert");
            Suggest();
        }

        property String^ OutputPath;

    protected:
        virtual bool OnAccept() override
        {
            String^ src = inputField->Value;
            if (!File::Exists(src)) { logBox->Text = "Source file not found."; return false; }
            ConvertDirection d = toShl->Checked ? ConvertDirection::CToShalimar : ConvertDirection::ShalimarToC;
            CodeConverter^ c = gcnew CodeConverter(settings);
            UseWaitCursor = true;
            bool ok = c->Convert(src, outputBox->Text->Trim(), d);
            UseWaitCursor = false;
            logBox->Text = c->Log->Replace("\n", "\r\n");
            if (!ok) return false;
            OutputPath = outputBox->Text->Trim();
            return true;
        }

    private:
        ConverterSettings^ settings;
        PathField^ inputField;
        RadioButton^ toShl;
        RadioButton^ toC;
        TextBox^ outputBox;
        TextBox^ logBox;

        void Suggest()
        {
            String^ src = inputField->Value;
            if (src->Length == 0) return;
            ConvertDirection d = toShl->Checked ? ConvertDirection::CToShalimar : ConvertDirection::ShalimarToC;
            outputBox->Text = CodeConverter::SuggestOutput(src, d);
            CodeConverter^ c = gcnew CodeConverter(settings);
            String^ tool = c->ToolPath(d);
            if (File::Exists(tool)) logBox->Text = "Converter: " + tool;
            else
            {
                String^ looked = c->LookedIn(d);
                logBox->Text = "Converter not installed yet: " + tool
                    + (looked->Length > 0 ? "\r\nLooked in: " + looked : String::Empty)
                    + "\r\nPut c2s.exe beside GRIDE.exe, or set the converter in Tools > Options > Converters.";
            }
        }

        void OnInputChanged(Object^ sender, EventArgs^ e)
        {
            if (sender == inputField->Controls[0] && File::Exists(inputField->Value))
            {
                bool shl = PathUtil::HasExtension(inputField->Value, ".shl");
                toC->Checked = shl; toShl->Checked = !shl;
            }
            Suggest();
        }
    };

    // -------------------------------------------------------------------------
    public ref class ToolchainCheckDialog : public ThemedDialog
    {
    public:
        ToolchainCheckDialog(GrideSettings^ s) : ThemedDialog("Toolchain Check", 820, 520)
        {
            FormBorderStyle = WF::FormBorderStyle::Sizable;
            ListView^ list = gcnew ListView();
            list->Dock = DockStyle::Fill;
            list->View = WF::View::Details;
            list->FullRowSelect = true;
            list->Columns->Add("Tool", 150);
            list->Columns->Add("Resolved path", 300);
            list->Columns->Add("Status", 90);
            list->Columns->Add("Looked in", 260);
            list->SmallImageList = IconFactory::CreateImageList(gcnew array<String^>{ "info", "warning" });
            int found = 0, missing = 0;
            ToolchainDefinitions^ defs = ToolchainDefinitions::Current;
            ListViewGroup^ dg = gcnew ListViewGroup("toolchain.json");
            list->Groups->Add(dg);
            AddLine(list, dg, "Shipped file", defs->ShippedFound ? defs->ShippedFile : "not found: " + defs->ShippedFile, defs->ShippedFound ? "info" : "warning", defs->ShippedFound ? "Found" : "Missing", String::Empty);
            AddLine(list, dg, "Your overrides", defs->UserJson != nullptr ? defs->UserFile : "(none) " + defs->UserFile, "info", defs->UserJson != nullptr ? "In use" : "None", String::Empty);
            for each (String^ p in defs->Problems) AddLine(list, dg, "Problem", p, "warning", "Check", String::Empty);
            for each (ToolchainSettings^ tc in s->Toolchains)
            {
                ListViewGroup^ g = gcnew ListViewGroup(TargetInfo::DisplayName(tc->TargetKind));
                list->Groups->Add(g);
                AddTool(list, g, tc, "C90 compiler (c90)", tc->CompilerC90, found, missing);
                AddTool(list, g, tc, "C++11 compiler (cpp11)", tc->CompilerCpp11, found, missing);
                AddTool(list, g, tc, "Shalimar compiler", tc->CompilerShalimar, found, missing);
                if (!String::IsNullOrEmpty(tc->Assembler)) AddTool(list, g, tc, "Assembler", tc->Assembler, found, missing);
                if (!String::IsNullOrEmpty(tc->Linker)) AddTool(list, g, tc, "Linker", tc->Linker, found, missing);
                if (!String::IsNullOrEmpty(tc->DebugAssembler)) AddTool(list, g, tc, "Debug assembler (clang)", tc->DebugAssembler, found, missing);
                if (!String::IsNullOrEmpty(tc->Runtime)) AddTool(list, g, tc, "Runtime", tc->Runtime, found, missing);
                if (!String::IsNullOrEmpty(tc->RuntimeDir))
                {
                    String^ dir = tc->ResolveRuntimeDir();
                    AddPlain(list, g, "Runtime directory (RTS6x)", String::IsNullOrEmpty(dir) ? tc->RuntimeDir : Path::Combine(dir, "rts6x.lib"), found, missing);
                }
                if (!String::IsNullOrEmpty(tc->Simulator)) AddTool(list, g, tc, "Simulator (sim6747)", tc->Simulator, found, missing);
                if (!String::IsNullOrEmpty(tc->Emulator)) AddTool(list, g, tc, "Emulator (vm6747)", tc->Emulator, found, missing);
                if (!String::IsNullOrEmpty(tc->Debugger)) AddTool(list, g, tc, "Debugger", tc->Debugger, found, missing);
            }
            ListViewGroup^ cg = gcnew ListViewGroup("Converters");
            list->Groups->Add(cg);
            CodeConverter^ conv = gcnew CodeConverter(s->Converter);
            AddConverter(list, cg, conv, "C -> Shalimar", ConvertDirection::CToShalimar, found, missing);
            AddConverter(list, cg, conv, "Shalimar -> C", ConvertDirection::ShalimarToC, found, missing);

            Label^ sum = Heading(String::Format("{0} tool(s) found, {1} not installed yet", found, missing));
            Panel^ host = gcnew Panel();
            host->Dock = DockStyle::Fill;
            host->Padding = WF::Padding(10);
            host->Controls->Add(list);
            host->Controls->Add(sum);
            Controls->Add(host);
            host->BringToFront();
            Button^ close = AddButton("Close");
            close->DialogResult = WF::DialogResult::Cancel;
            CancelButton = close;
            Button^ opt = AddButton("Open Options...");
            opt->Tag = "accent";
            opt->DialogResult = WF::DialogResult::Retry;
        }

    private:
        // a tool that is not there says where it was looked for
        static void AddTool(ListView^ list, ListViewGroup^ g, ToolchainSettings^ tc, String^ name, String^ tool, int% found, int% missing)
        {
            String^ path = tc->Resolve(tool);
            bool ok = !String::IsNullOrEmpty(path) && File::Exists(path);
            AddLine(list, g, name, String::IsNullOrEmpty(path) ? "(not set)" : path, ok ? "info" : "warning", ok ? "Found" : "Not installed", ok ? String::Empty : tc->LookedIn(tool));
            if (ok) found++; else missing++;
        }
        // a converter direction: the program that would run, and where toolchain.json's c2s was looked for
        static void AddConverter(ListView^ list, ListViewGroup^ g, CodeConverter^ conv, String^ name, ConvertDirection d, int% found, int% missing)
        {
            String^ path = conv->ToolPath(d);
            bool ok = !String::IsNullOrEmpty(path) && File::Exists(path);
            AddLine(list, g, name, String::IsNullOrEmpty(path) ? "(not set)" : path, ok ? "info" : "warning", ok ? "Found" : "Not installed", ok ? String::Empty : conv->LookedIn(d));
            if (ok) found++; else missing++;
        }
        static void AddPlain(ListView^ list, ListViewGroup^ g, String^ name, String^ path, int% found, int% missing)
        {
            bool ok = !String::IsNullOrEmpty(path) && File::Exists(path);
            AddLine(list, g, name, String::IsNullOrEmpty(path) ? "(not set)" : path, ok ? "info" : "warning", ok ? "Found" : "Not installed", String::Empty);
            if (ok) found++; else missing++;
        }
        static void AddLine(ListView^ list, ListViewGroup^ g, String^ name, String^ shown, String^ icon, String^ state, String^ lookedIn)
        {
            ListViewItem^ it = gcnew ListViewItem(name, icon);
            it->SubItems->Add(shown);
            it->SubItems->Add(state);
            it->SubItems->Add(lookedIn);
            it->Group = g;
            list->Items->Add(it);
        }
    };

    // -------------------------------------------------------------------------
    public ref class ExternalToolsDialog : public ThemedDialog
    {
    public:
        ExternalToolsDialog(List<ExternalTool^>^ tools) : ThemedDialog("External Tools", 640, 420)
        {
            Tools = gcnew List<ExternalTool^>();
            for each (ExternalTool^ t in tools)
            {
                ExternalTool^ c = gcnew ExternalTool();
                c->Title = t->Title; c->Command = t->Command; c->Arguments = t->Arguments; c->InitialDir = t->InitialDir;
                Tools->Add(c);
            }
            list = gcnew ListBox();
            list->Dock = DockStyle::Left;
            list->Width = 200;
            list->IntegralHeight = false;
            list->SelectedIndexChanged += gcnew EventHandler(this, &ExternalToolsDialog::OnSelect);
            TableLayoutPanel^ grid = MakeGrid(100);
            title = gcnew TextBox(); AddRow(grid, "Title", title);
            command = gcnew PathField(false, true); AddRow(grid, "Command", command);
            args = gcnew TextBox(); AddRow(grid, "Arguments", args);
            dir = gcnew TextBox(); AddRow(grid, "Initial folder", dir);
            Label^ m = gcnew Label();
            m->Text = "Macros: $(FilePath) $(FileDir) $(FileName) $(ProjectDir) $(ProjectName) $(WorkspaceDir)";
            m->AutoSize = true; m->Tag = "muted";
            AddRow(grid, String::Empty, m);
            Panel^ host = gcnew Panel();
            host->Dock = DockStyle::Fill;
            host->Padding = WF::Padding(10);
            host->Controls->Add(grid);
            host->Controls->Add(list);
            Controls->Add(host);
            host->BringToFront();
            AddOkCancel("OK");
            Button^ remove = AddButton("Remove");
            remove->Click += gcnew EventHandler(this, &ExternalToolsDialog::OnRemove);
            Button^ addBtn = AddButton("Add");
            addBtn->Click += gcnew EventHandler(this, &ExternalToolsDialog::OnAdd);
            current = nullptr;
            Fill();
        }
        property List<ExternalTool^>^ Tools;

    protected:
        virtual bool OnAccept() override { Store(); return true; }

    private:
        ListBox^ list;
        TextBox^ title; PathField^ command; TextBox^ args; TextBox^ dir;
        ExternalTool^ current;

        void Fill()
        {
            list->Items->Clear();
            for each (ExternalTool^ t in Tools)
            {
                // ListBox::Items::Add throws on nullptr
                if (String::IsNullOrEmpty(t->Title)) list->Items->Add("(untitled)");
                else list->Items->Add(t->Title);
            }
            if (list->Items->Count > 0) list->SelectedIndex = 0;
        }
        void Store()
        {
            if (current == nullptr) return;
            current->Title = title->Text; current->Command = command->Value;
            current->Arguments = args->Text; current->InitialDir = dir->Text;
        }
        void OnSelect(Object^ sender, EventArgs^ e)
        {
            Store();
            int i = list->SelectedIndex;
            current = nullptr;
            if (i >= 0) current = Tools[i];
            if (current == nullptr) return;
            title->Text = current->Title; command->Value = current->Command;
            args->Text = current->Arguments; dir->Text = current->InitialDir;
        }
        void OnAdd(Object^ sender, EventArgs^ e)
        {
            Store();
            current = nullptr;
            ExternalTool^ t = gcnew ExternalTool();
            Tools->Add(t);
            Fill();
            list->SelectedIndex = list->Items->Count - 1;
        }
        void OnRemove(Object^ sender, EventArgs^ e)
        {
            int i = list->SelectedIndex;
            if (i < 0) return;
            current = nullptr;
            Tools->RemoveAt(i);
            Fill();
        }
    };

    // -------------------------------------------------------------------------
    public ref class ShortcutsDialog : public ThemedDialog
    {
    public:
        ShortcutsDialog(CommandRegistry^ reg) : ThemedDialog("Keyboard Shortcuts", 640, 560)
        {
            registry = reg;
            FormBorderStyle = WF::FormBorderStyle::Sizable;
            filter = gcnew TextBox();
            filter->Dock = DockStyle::Top;
            filter->TextChanged += gcnew EventHandler(this, &ShortcutsDialog::OnFilter);
            list = gcnew ListView();
            list->Dock = DockStyle::Fill;
            list->View = WF::View::Details;
            list->FullRowSelect = true;
            list->Columns->Add("Menu", 90);
            list->Columns->Add("Command", 300);
            list->Columns->Add("Shortcut", 180);
            Panel^ host = gcnew Panel();
            host->Dock = DockStyle::Fill;
            host->Padding = WF::Padding(10);
            host->Controls->Add(list);
            host->Controls->Add(filter);
            host->Controls->Add(Heading("Type to filter:"));
            Controls->Add(host);
            host->BringToFront();
            Button^ close = AddButton("Close");
            close->DialogResult = WF::DialogResult::OK;
            CancelButton = close;
            Fill();
        }
    private:
        CommandRegistry^ registry;
        TextBox^ filter;
        ListView^ list;
        void Fill()
        {
            String^ f = filter->Text->Trim()->ToLowerInvariant();
            list->BeginUpdate();
            list->Items->Clear();
            for each (Command^ c in registry->All)
            {
                if (String::IsNullOrEmpty(c->ShortcutText)) continue;
                String^ hay = (c->Category + " " + c->PlainText + " " + c->ShortcutText)->ToLowerInvariant();
                if (f->Length > 0 && !hay->Contains(f)) continue;
                ListViewItem^ it = gcnew ListViewItem(c->Category);
                it->SubItems->Add(c->PlainText);
                it->SubItems->Add(c->ShortcutText);
                list->Items->Add(it);
            }
            list->EndUpdate();
        }
        void OnFilter(Object^ sender, EventArgs^ e) { Fill(); }
    };

    // -------------------------------------------------------------------------
    public ref class AboutDialog : public ThemedDialog
    {
    public:
        // cpp11: the program the toolchain resolves to, asked for its version when the box opens, as White's
        // About asks it - a number written here would be a second copy, and the stale one
        AboutDialog(String^ settingsPath, String^ cpp11) : ThemedDialog("About GRIDE", 520, 400)
        {
            PictureBox^ logo = gcnew PictureBox();
            logo->Image = IconFactory::AppIcon()->ToBitmap();
            logo->SizeMode = PictureBoxSizeMode::CenterImage;
            logo->Size = SD::Size(64, 64);
            logo->Location = SD::Point(24, 24);
            Controls->Add(logo);

            Label^ name = gcnew Label();
            name->Text = "GRIDE";
            name->Font = gcnew SD::Font("Segoe UI Semibold", 22.0f);
            name->Location = SD::Point(100, 20);
            name->AutoSize = true;
            Controls->Add(name);

            Label^ info = gcnew Label();
            info->Location = SD::Point(102, 70);
            info->Size = SD::Size(400, 250);
            String^ said = AskVersion(cpp11);
            if (said->Length == 0) said = String::IsNullOrEmpty(cpp11) ? "not found (Tools > Toolchain Check)" : "no answer from " + cpp11;
            info->Text = String::Format(
                "Version {0}\n\n"
                "A 3-language, 4-target integrated development environment.\n\n"
                "Languages:  C90 / C++11 / Shalimar\n"
                "cpp11:      {3}\n"
                "Targets:    Windows (MASM/LINK) / Linux / macOS / TMS320C6747\n"
                "DSP tools:  ASM6x / LNK6x / RTS6747 / VM6747 / SIM6747\n"
                "Opens:      CCS 7.4 / 5.5 projects, Visual Studio solutions (read-only)\n\n"
                ".NET Framework {1}\n"
                "Settings:   {2}",
                System::Reflection::Assembly::GetExecutingAssembly()->GetName()->Version->ToString(3), Environment::Version, settingsPath, said);
            Controls->Add(info);

            Button^ ok = AddButton("OK");
            ok->DialogResult = WF::DialogResult::OK;
            ok->Tag = "accent";
            AcceptButton = ok;
            CancelButton = ok;
        }

    private:
        // the program's own words for `--version`: its banner without the copyright, then the version line cpp11
        // writes beneath it ("C++03 with C++11 extensions, Version 1.30, sealed 23-10-2026"); empty when it cannot run
        static String^ AskVersion(String^ exe)
        {
            if (String::IsNullOrEmpty(exe) || !File::Exists(exe)) return String::Empty;
            try
            {
                System::Diagnostics::ProcessStartInfo^ si = gcnew System::Diagnostics::ProcessStartInfo(exe, "--version");
                si->UseShellExecute = false; si->CreateNoWindow = true; si->RedirectStandardOutput = true; si->RedirectStandardInput = true;
                si->StandardOutputEncoding = System::Text::Encoding::UTF8;
                System::Diagnostics::Process^ p = System::Diagnostics::Process::Start(si);
                p->StandardInput->Close();
                String^ out = p->StandardOutput->ReadToEnd();
                if (!p->WaitForExit(5000)) { try { p->Kill(); } catch (Exception^) { } return String::Empty; }
                String^ banner = String::Empty;
                String^ version = String::Empty;
                for each (String^ raw in out->Split(gcnew array<wchar_t>{ L'\r', L'\n' }, StringSplitOptions::RemoveEmptyEntries))
                {
                    String^ line = raw->Trim();
                    if (banner->Length == 0) banner = line;
                    else if (version->Length == 0 && line->StartsWith("Version ")) version = line;
                }
                int owner = banner->IndexOf(" - ");
                if (banner->Length > 0 && banner[0] == L'\u00A9' && owner > 0) banner = banner->Substring(owner + 3);
                return version->Length == 0 ? banner : banner + ", " + version;
            }
            catch (Exception^) { return String::Empty; }
        }
    };
}}
