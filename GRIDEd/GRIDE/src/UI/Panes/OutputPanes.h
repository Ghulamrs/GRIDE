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
            // the input line of a program running inside the IDE (RIDE's window has one): shown while one runs
            input = gcnew TextBox();
            input->Dock = DockStyle::Bottom;
            input->Font = PaneUtil::MonoFont();
            input->Visible = false;
            input->KeyDown += gcnew KeyEventHandler(this, &OutputPane::OnInputKey);
            Controls->Add(box);
            Controls->Add(input);
            Controls->Add(bar);
        }

        event NavigateHandler^ Navigate;
        event NavigateColumnHandler^ NavigateTo;     // with the column, when the line names one
        event TextLineHandler^ InputEntered;         // a line typed for the running program

        property String^ CurrentSource { String^ get() { return safe_cast<String^>(source->SelectedItem); } }

        // the input line is there while a program runs; it takes the focus when shown
        property bool InputEnabled
        {
            bool get() { return input->Visible; }
            void set(bool on)
            {
                input->Visible = on;
                if (on) { input->Clear(); input->Focus(); }
            }
        }

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
        TextBox^ input;

        // Enter hands the line to the program and shows it, as a console echoes what is typed
        void OnInputKey(Object^ sender, KeyEventArgs^ e)
        {
            if (e->KeyCode != Keys::Enter) return;
            e->SuppressKeyPress = true;
            String^ text = input->Text;
            input->Clear();
            Append(CurrentSource, text);
            InputEntered(text);
        }

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

        // a double-click goes to the place the line names: a diagnostic, or any "file:line:col:" /
        // "file(line,col):" line, as Enter on RIDE's Console does
        void OnDoubleClick(Object^ sender, MouseEventArgs^ e)
        {
            int line = box->GetLineFromCharIndex(box->GetCharIndexFromPosition(e->Location));
            if (line < 0 || line >= box->Lines->Length) return;
            BuildMessage^ m = BuildMessageParser::TryParse(box->Lines[line], nullptr);
            if (m == nullptr || m->Line <= 0) m = BuildMessageParser::TryParseLocation(box->Lines[line], nullptr);
            if (m == nullptr || m->Line <= 0) return;
            NavigateTo(m->File, m->Line, m->Column);
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
            errors = warnings = infos = 0;
            UpdateCounts();
        }

        event NavigateHandler^ Navigate;
        event NavigateColumnHandler^ NavigateTo;

        property int ErrorCount { int get() { return errors; } }
        property int WarningCount { int get() { return warnings; } }

        // the first error listed, where a failed build takes the caret
        BuildMessage^ FirstError()
        {
            for each (BuildMessage^ m in all)
                if (m->Sev == Severity::Error && m->Line > 0 && !String::IsNullOrEmpty(m->File)) return m;
            return nullptr;
        }

        void Clear() { all->Clear(); list->Items->Clear(); errors = warnings = infos = 0; UpdateCounts(); }

        void Add(BuildMessage^ m)
        {
            all->Add(m);
            if (m->Sev == Severity::Error) errors++;
            else if (m->Sev == Severity::Warning) warnings++;
            else infos++;
            if (IsShown(m)) list->Items->Add(MakeItem(m));
            UpdateCounts();
        }

    private:
        List<BuildMessage^>^ all;
        ListView^ list;
        ToolStripButton^ btnErrors;
        ToolStripButton^ btnWarnings;
        ToolStripButton^ btnInfos;
        int errors, warnings, infos;     // kept while adding, not recounted per message

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

        void UpdateCounts()
        {
            btnErrors->Text = String::Format("{0} Errors", errors);
            btnWarnings->Text = String::Format("{0} Warnings", warnings);
            btnInfos->Text = String::Format("{0} Messages", infos);
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
            if (!String::IsNullOrEmpty(m->File)) NavigateTo(m->File, m->Line, m->Column);
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
            Write("GRIDE terminal - type a command below and press Enter. cmd.exe is started on first use, "
                  "with the toolchain folders on PATH.\n");
        }

        property String^ ExtraPath;          // toolchain folders added to PATH
        property String^ WorkingDirectory;

        // started on the first command, not when the pane is shown: cmd.exe may be
        // blocked by policy, and most sessions never use the terminal
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
            Process^ p = gcnew Process();
            p->StartInfo = si;
            p->EnableRaisingEvents = true;
            p->OutputDataReceived += gcnew DataReceivedEventHandler(this, &TerminalPane::OnData);
            p->ErrorDataReceived += gcnew DataReceivedEventHandler(this, &TerminalPane::OnData);
            p->Exited += gcnew EventHandler(this, &TerminalPane::OnExited);
            try
            {
                p->Start();
                p->BeginOutputReadLine();
                p->BeginErrorReadLine();
                proc = p;
                started = DateTime::Now;
            }
            catch (Exception^ ex)
            {
                Write("Cannot start cmd.exe: " + ex->Message + "\nThe terminal is not available on this computer; "
                      "build and run commands of GRIDE do not need it.\n");
                proc = nullptr;
            }
        }

        void StopShell()
        {
            Process^ p = proc;
            proc = nullptr;
            if (p == nullptr) return;
            try
            {
                p->EnableRaisingEvents = false;
                if (!p->HasExited) p->Kill();
                p->Close();
            }
            catch (Exception^) { }
        }

    protected:
        ~TerminalPane() { StopShell(); }

    private:
        Process^ proc;
        DateTime started;
        RichTextBox^ output;
        TextBox^ input;

        void Write(String^ s)
        {
            output->AppendText(s);
            output->ScrollToCaret();
        }

        void Post(String^ s)
        {
            if (IsDisposed || !IsHandleCreated) return;
            try { BeginInvoke(gcnew Action<String^>(this, &TerminalPane::Write), s); }
            catch (Exception^) { }
        }

        void OnData(Object^ sender, DataReceivedEventArgs^ e)
        {
            if (e->Data != nullptr) Post(e->Data + "\n");
        }

        void OnExited(Object^ sender, EventArgs^ e)
        {
            Process^ p = safe_cast<Process^>(sender);
            int code = 0;
            try { code = p->ExitCode; } catch (Exception^) { }
            String^ s = String::Format("[cmd.exe ended, exit code {0}]\n", code);
            if ((DateTime::Now - started).TotalSeconds < 3)
                s = s + "cmd.exe stopped right after starting - it may be disabled by a policy on this computer.\n";
            Post(s);
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

        void OnRestart(Object^ sender, EventArgs^ e) { output->Clear(); StopShell(); StartShell(); }
        void OnClear(Object^ sender, EventArgs^ e) { output->Clear(); }
        void OnVisibleChanged(Object^ sender, EventArgs^ e)
        {
            if (Visible) input->Focus();
        }
    };
}}
