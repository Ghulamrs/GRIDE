// =============================================================================
//  OutputPanes.h - the bottom panes
//    OutputPane       build / debug / tools text, coloured per line
//    ErrorListPane    parsed errors and warnings, filterable
//    FindResultsPane  results of Edit > Find in Files
//    TerminalPane     cmd.exe with the toolchain folders on PATH
// =============================================================================
#pragma once
#include "ExplorerPanes.h"
#include "../../Services/BuildMessage.h"

namespace Gride { namespace UI {

    using namespace Gride::Services;
    using namespace System::Diagnostics;

    // helper used by several panes
    public ref class PaneUtil abstract sealed
    {
    public:
        static ListView^ MakeList(array<String^>^ columns, array<int>^ widths)
        {
            ListView^ lv = gcnew ListView();
            lv->Dock = DockStyle::Fill;
            lv->View = WF::View::Details;
            lv->FullRowSelect = true;
            lv->HideSelection = false;
            lv->GridLines = false;
            lv->HeaderStyle = ColumnHeaderStyle::Nonclickable;
            for (int i = 0; i < columns->Length; i++) lv->Columns->Add(columns[i], widths[i]);
            return lv;
        }

        static ToolStrip^ MakeBar()
        {
            ToolStrip^ ts = gcnew ToolStrip();
            ts->GripStyle = ToolStripGripStyle::Hidden;
            ts->Dock = DockStyle::Top;
            ts->Padding = WF::Padding(4, 0, 4, 0);
            return ts;
        }

        static SD::Font^ MonoFont() { return gcnew SD::Font("Consolas", 9.0f); }
    };

    // -------------------------------------------------------------------------
    public ref class OutputPane : public ToolPane
    {
    public:
        OutputPane() : ToolPane("Output", "Output", "output")
        {
            lines = gcnew Dictionary<String^, List<String^>^>();
            ToolStrip^ bar = PaneUtil::MakeBar();
            bar->Items->Add(gcnew ToolStripLabel("Show output from:"));
            source = gcnew ToolStripComboBox();
            source->DropDownStyle = ComboBoxStyle::DropDownList;
            source->Items->AddRange(gcnew array<Object^>{ "Build", "Debug", "Tools" });
            source->SelectedIndex = 0;
            source->SelectedIndexChanged += gcnew EventHandler(this, &OutputPane::OnSourceChanged);
            bar->Items->Add(source);
            ToolStripButton^ clear = gcnew ToolStripButton("Clear");
            clear->Image = IconFactory::Get("clean");
            clear->Click += gcnew EventHandler(this, &OutputPane::OnClear);
            bar->Items->Add(clear);

            box = gcnew RichTextBox();
            box->Dock = DockStyle::Fill;
            box->ReadOnly = true;
            box->WordWrap = false;
            box->BorderStyle = WF::BorderStyle::None;
            box->Font = PaneUtil::MonoFont();
            box->DetectUrls = false;
            box->MouseDoubleClick += gcnew MouseEventHandler(this, &OutputPane::OnDoubleClick);
            Controls->Add(box);
            Controls->Add(bar);
        }

        event NavigateHandler^ Navigate;

        property String^ CurrentSource { String^ get() { return safe_cast<String^>(source->SelectedItem); } }

        void Clear(String^ src)
        {
            List<String^>^ l = Get(src);
            l->Clear();
            if (src == CurrentSource) box->Clear();
        }

        void ShowSource(String^ src)
        {
            int i = source->Items->IndexOf(src);
            if (i >= 0) source->SelectedIndex = i;
        }

        // UI thread only
        void Append(String^ src, String^ text)
        {
            List<String^>^ l = Get(src);
            l->Add(text);
            if (l->Count > 20000) l->RemoveRange(0, 5000);
            if (src == CurrentSource) AppendLine(text);
        }

        virtual void ApplyTheme() override
        {
            ToolPane::ApplyTheme();
            Redraw();
        }

    private:
        Dictionary<String^, List<String^>^>^ lines;
        ToolStripComboBox^ source;
        RichTextBox^ box;

        List<String^>^ Get(String^ src)
        {
            List<String^>^ l;
            if (!lines->TryGetValue(src, l)) { l = gcnew List<String^>(); lines[src] = l; }
            return l;
        }

        Color ColorFor(String^ s)
        {
            Theme^ t = ThemeManager::Current;
            String^ low = s->ToLowerInvariant();
            if (low->StartsWith("------") || low->StartsWith("======")) return low->Contains(" 0 failed") ? t->SuccessText : t->Accent;
            if (Regex::IsMatch(low, "\\berror\\b|fatal")) return t->ErrorText;
            if (Regex::IsMatch(low, "\\bwarning\\b")) return t->WarningText;
            if (low->StartsWith("[")) return t->Text;
            return t->Text;
        }

        void AppendLine(String^ text)
        {
            box->SelectionStart = box->TextLength;
            box->SelectionLength = 0;
            box->SelectionColor = ColorFor(text);
            box->AppendText(text + "\n");
            box->ScrollToCaret();
        }

        void Redraw()
        {
            box->Clear();
            box->BackColor = ThemeManager::Current->Window;
            for each (String^ s in Get(CurrentSource)) AppendLine(s);
        }

        void OnSourceChanged(Object^ sender, EventArgs^ e) { Redraw(); }
        void OnClear(Object^ sender, EventArgs^ e) { Clear(CurrentSource); }

        void OnDoubleClick(Object^ sender, MouseEventArgs^ e)
        {
            int line = box->GetLineFromCharIndex(box->GetCharIndexFromPosition(e->Location));
            if (line < 0 || line >= box->Lines->Length) return;
            BuildMessage^ m = BuildMessageParser::TryParse(box->Lines[line], nullptr);
            if (m != nullptr && File::Exists(m->File)) Navigate(m->File, m->Line);
        }
    };

    // -------------------------------------------------------------------------
    public ref class ErrorListPane : public ToolPane
    {
    public:
        ErrorListPane() : ToolPane("ErrorList", "Error List", "error")
        {
            all = gcnew List<BuildMessage^>();
            ToolStrip^ bar = PaneUtil::MakeBar();
            btnErrors = MakeToggle(bar, "error");
            btnWarnings = MakeToggle(bar, "warning");
            btnInfos = MakeToggle(bar, "info");
            ToolStripButton^ clear = gcnew ToolStripButton("Clear");
            clear->Click += gcnew EventHandler(this, &ErrorListPane::OnClear);
            bar->Items->Add(gcnew ToolStripSeparator());
            bar->Items->Add(clear);

            list = PaneUtil::MakeList(gcnew array<String^>{ "", "Code", "Description", "Project", "File", "Line" },
                                      gcnew array<int>{ 26, 70, 520, 110, 160, 50 });
            list->SmallImageList = IconFactory::CreateImageList(gcnew array<String^>{ "error", "warning", "info" });
            list->DoubleClick += gcnew EventHandler(this, &ErrorListPane::OnDoubleClick);
            Controls->Add(list);
            Controls->Add(bar);
            UpdateCounts();
        }

        event NavigateHandler^ Navigate;

        property int ErrorCount { int get() { return Count(Severity::Error); } }
        property int WarningCount { int get() { return Count(Severity::Warning); } }

        void Clear() { all->Clear(); list->Items->Clear(); UpdateCounts(); }

        void Add(BuildMessage^ m)
        {
            all->Add(m);
            if (IsShown(m)) list->Items->Add(MakeItem(m));
            UpdateCounts();
        }

    private:
        List<BuildMessage^>^ all;
        ListView^ list;
        ToolStripButton^ btnErrors;
        ToolStripButton^ btnWarnings;
        ToolStripButton^ btnInfos;

        ToolStripButton^ MakeToggle(ToolStrip^ bar, String^ icon)
        {
            ToolStripButton^ b = gcnew ToolStripButton();
            b->Image = IconFactory::Get(icon);
            b->CheckOnClick = true;
            b->Checked = true;
            b->CheckedChanged += gcnew EventHandler(this, &ErrorListPane::OnFilterChanged);
            bar->Items->Add(b);
            return b;
        }

        int Count(Severity s)
        {
            int n = 0;
            for each (BuildMessage^ m in all) if (m->Sev == s) n++;
            return n;
        }

        void UpdateCounts()
        {
            btnErrors->Text = String::Format("{0} Errors", Count(Severity::Error));
            btnWarnings->Text = String::Format("{0} Warnings", Count(Severity::Warning));
            btnInfos->Text = String::Format("{0} Messages", Count(Severity::Info));
        }

        bool IsShown(BuildMessage^ m)
        {
            if (m->Sev == Severity::Error) return btnErrors->Checked;
            if (m->Sev == Severity::Warning) return btnWarnings->Checked;
            return btnInfos->Checked;
        }

        ListViewItem^ MakeItem(BuildMessage^ m)
        {
            ListViewItem^ it = gcnew ListViewItem(String::Empty);
            if (m->Sev == Severity::Error) it->ImageKey = "error";
            else if (m->Sev == Severity::Warning) it->ImageKey = "warning";
            else it->ImageKey = "info";
            it->SubItems->Add(m->Code);
            it->SubItems->Add(m->Text);
            it->SubItems->Add(m->Project);
            it->SubItems->Add(String::IsNullOrEmpty(m->File) ? String::Empty : Path::GetFileName(m->File));
            it->SubItems->Add(m->Line > 0 ? m->Line.ToString() : String::Empty);
            it->Tag = m;
            return it;
        }

        void OnFilterChanged(Object^ sender, EventArgs^ e)
        {
            list->BeginUpdate();
            list->Items->Clear();
            for each (BuildMessage^ m in all) if (IsShown(m)) list->Items->Add(MakeItem(m));
            list->EndUpdate();
        }

        void OnClear(Object^ sender, EventArgs^ e) { Clear(); }

        void OnDoubleClick(Object^ sender, EventArgs^ e)
        {
            if (list->SelectedItems->Count == 0) return;
            BuildMessage^ m = safe_cast<BuildMessage^>(list->SelectedItems[0]->Tag);
            if (!String::IsNullOrEmpty(m->File) && File::Exists(m->File)) Navigate(m->File, m->Line);
        }
    };

    // -------------------------------------------------------------------------
    public ref class FindResultsPane : public ToolPane
    {
    public:
        FindResultsPane() : ToolPane("FindResults", "Find Results", "find")
        {
            header = gcnew Label();
            header->Dock = DockStyle::Top;
            header->Height = 24;
            header->TextAlign = ContentAlignment::MiddleLeft;
            header->Padding = WF::Padding(6, 0, 0, 0);
            list = PaneUtil::MakeList(gcnew array<String^>{ "File", "Line", "Text" }, gcnew array<int>{ 220, 60, 700 });
            list->Font = PaneUtil::MonoFont();
            list->DoubleClick += gcnew EventHandler(this, &FindResultsPane::OnDoubleClick);
            Controls->Add(list);
            Controls->Add(header);
        }

        event NavigateHandler^ Navigate;

        void Begin(String^ what)
        {
            list->Items->Clear();
            header->Text = "Find all \"" + what + "\"";
            list->BeginUpdate();
        }
        void Add(String^ file, int line, String^ text)
        {
            ListViewItem^ it = gcnew ListViewItem(file);
            it->SubItems->Add(line.ToString());
            it->SubItems->Add(text->Trim());
            it->Tag = gcnew Tuple<String^, int>(file, line);
            list->Items->Add(it);
        }
        void End(int files)
        {
            list->EndUpdate();
            header->Text = header->Text + String::Format("   -   {0} match(es) in {1} file(s)", list->Items->Count, files);
        }

    private:
        Label^ header;
        ListView^ list;

        void OnDoubleClick(Object^ sender, EventArgs^ e)
        {
            if (list->SelectedItems->Count == 0) return;
            Tuple<String^, int>^ t = safe_cast<Tuple<String^, int>^>(list->SelectedItems[0]->Tag);
            Navigate(t->Item1, t->Item2);
        }
    };

    // -------------------------------------------------------------------------
    public ref class TerminalPane : public ToolPane
    {
    public:
        TerminalPane() : ToolPane("Terminal", "Terminal", "terminal")
        {
            proc = nullptr;
            ExtraPath = String::Empty;
            WorkingDirectory = Environment::GetFolderPath(Environment::SpecialFolder::MyDocuments);
            output = gcnew RichTextBox();
            output->Dock = DockStyle::Fill;
            output->ReadOnly = true;
            output->BorderStyle = WF::BorderStyle::None;
            output->Font = PaneUtil::MonoFont();
            output->WordWrap = false;
            input = gcnew TextBox();
            input->Dock = DockStyle::Bottom;
            input->Font = PaneUtil::MonoFont();
            input->KeyDown += gcnew KeyEventHandler(this, &TerminalPane::OnInputKey);
            ToolStrip^ bar = PaneUtil::MakeBar();
            ToolStripButton^ restart = gcnew ToolStripButton("Restart");
            restart->Image = IconFactory::Get("restart");
            restart->Click += gcnew EventHandler(this, &TerminalPane::OnRestart);
            bar->Items->Add(restart);
            ToolStripButton^ clear = gcnew ToolStripButton("Clear");
            clear->Click += gcnew EventHandler(this, &TerminalPane::OnClear);
            bar->Items->Add(clear);
            Controls->Add(output);
            Controls->Add(input);
            Controls->Add(bar);
            VisibleChanged += gcnew EventHandler(this, &TerminalPane::OnVisibleChanged);
        }

        property String^ ExtraPath;          // toolchain folders added to PATH
        property String^ WorkingDirectory;

        void StartShell()
        {
            StopShell();
            ProcessStartInfo^ si = gcnew ProcessStartInfo("cmd.exe", "/Q");
            si->UseShellExecute = false;
            si->RedirectStandardInput = true;
            si->RedirectStandardOutput = true;
            si->RedirectStandardError = true;
            si->CreateNoWindow = true;
            if (Directory::Exists(WorkingDirectory)) si->WorkingDirectory = WorkingDirectory;
            if (!String::IsNullOrEmpty(ExtraPath))
                si->EnvironmentVariables["PATH"] = ExtraPath + ";" + si->EnvironmentVariables["PATH"];
            proc = gcnew Process();
            proc->StartInfo = si;
            proc->OutputDataReceived += gcnew DataReceivedEventHandler(this, &TerminalPane::OnData);
            proc->ErrorDataReceived += gcnew DataReceivedEventHandler(this, &TerminalPane::OnData);
            try
            {
                proc->Start();
                proc->BeginOutputReadLine();
                proc->BeginErrorReadLine();
                Write("GRIDE terminal - toolchain folders are on PATH. Type commands below.\n");
            }
            catch (Exception^ ex) { Write("Cannot start cmd.exe: " + ex->Message + "\n"); proc = nullptr; }
        }

        void StopShell()
        {
            if (proc != nullptr)
            {
                try { if (!proc->HasExited) proc->Kill(); } catch (Exception^) { }
                proc = nullptr;
            }
        }

    protected:
        ~TerminalPane() { StopShell(); }

    private:
        Process^ proc;
        RichTextBox^ output;
        TextBox^ input;

        void Write(String^ s)
        {
            output->AppendText(s);
            output->ScrollToCaret();
        }

        void OnData(Object^ sender, DataReceivedEventArgs^ e)
        {
            if (e->Data == nullptr || IsDisposed) return;
            try { BeginInvoke(gcnew Action<String^>(this, &TerminalPane::Write), e->Data + "\n"); }
            catch (Exception^) { }
        }

        void OnInputKey(Object^ sender, KeyEventArgs^ e)
        {
            if (e->KeyCode != Keys::Enter) return;
            e->SuppressKeyPress = true;
            if (proc == nullptr || proc->HasExited) StartShell();
            if (proc == nullptr) return;       // cmd.exe could not be started (message already shown)
            Write("> " + input->Text + "\n");
            try { proc->StandardInput->WriteLine(input->Text); }
            catch (Exception^ ex) { Write("Terminal error: " + ex->Message + "\n"); }
            input->Clear();
        }

        void OnRestart(Object^ sender, EventArgs^ e) { output->Clear(); StartShell(); }
        void OnClear(Object^ sender, EventArgs^ e) { output->Clear(); }
        void OnVisibleChanged(Object^ sender, EventArgs^ e)
        {
            if (Visible && proc == nullptr) StartShell();
            if (Visible) input->Focus();
        }
    };
}}
