// =============================================================================
//  EditDialogs.h - editing dialogs
//    FindReplaceDialog  Find / Replace / Find in Files (modeless)
//    GoToLineDialog     Edit > Go To Line
//    WindowsDialog      Window > Windows...
//    SnippetsDialog     Tools > Code Snippets Manager
// =============================================================================
#pragma once
#include "DialogBase.h"
#include "../Editor/CodeEditor.h"

namespace Gride { namespace UI {

    public enum class FindAction { FindNext, FindPrevious, Replace, ReplaceAll, FindAll };
    public enum class FindScope { CurrentDocument, OpenDocuments, Workspace };

    public ref class FindRequest
    {
    public:
        property String^ Pattern;
        property String^ Replacement;
        property FindOptions^ Options;
        property FindScope Scope;
        property FindAction Action;
    };

    public delegate void FindRequestHandler(FindRequest^ r);

    // -------------------------------------------------------------------------
    public ref class FindReplaceDialog : public ThemedDialog
    {
    public:
        FindReplaceDialog() : ThemedDialog("Find and Replace", 520, 300)
        {
            TableLayoutPanel^ grid = MakeGrid(100);
            findBox = gcnew ComboBox();
            AddRow(grid, "Find what", findBox);
            replaceBox = gcnew ComboBox();
            AddRow(grid, "Replace with", replaceBox);
            scopeBox = gcnew ComboBox();
            scopeBox->DropDownStyle = ComboBoxStyle::DropDownList;
            scopeBox->Items->AddRange(gcnew array<Object^>{ "Current document", "All open documents", "Entire workspace" });
            scopeBox->SelectedIndex = 0;
            AddRow(grid, "Look in", scopeBox);
            FlowLayoutPanel^ opts = gcnew FlowLayoutPanel();
            opts->Height = 30;
            matchCase = MakeCheck("Match case", opts);
            wholeWord = MakeCheck("Match whole word", opts);
            useRegex = MakeCheck("Use regular expressions", opts);
            AddRow(grid, "Options", opts);
            status = gcnew Label();
            status->Dock = DockStyle::Fill;
            status->Tag = "muted";
            AddRow(grid, String::Empty, status);
            Controls->Add(grid);

            Button^ close = AddButton("Close");
            close->Click += gcnew EventHandler(this, &FindReplaceDialog::OnClose);
            CancelButton = close;
            btnFindAll = AddAction("Find All", FindAction::FindAll);
            btnReplaceAll = AddAction("Replace All", FindAction::ReplaceAll);
            btnReplace = AddAction("Replace", FindAction::Replace);
            AddAction("Find Previous", FindAction::FindPrevious);
            btnNext = AddAction("Find Next", FindAction::FindNext);
            btnNext->Tag = "accent";
            AcceptButton = btnNext;
            FormBorderStyle = WF::FormBorderStyle::FixedToolWindow;
        }

        event FindRequestHandler^ Requested;

        // mode: 0 = find, 1 = replace, 2 = find in files
        void ShowMode(IWin32Window^ owner, int mode, String^ initial)
        {
            bool replace = mode == 1;
            replaceBox->Enabled = replace;
            btnReplace->Visible = replace;
            btnReplaceAll->Visible = replace;
            btnFindAll->Visible = true;
            if (mode == 2) { scopeBox->SelectedIndex = 2; Text = "Find in Files"; }
            else if (replace) Text = "Replace";
            else Text = "Find";
            if (!String::IsNullOrEmpty(initial) && !initial->Contains("\n")) findBox->Text = initial;
            status->Text = String::Empty;
            if (!Visible) Show(owner);
            Activate();
            findBox->Focus();
            findBox->SelectAll();
        }

        void SetStatus(String^ s) { status->Text = s; }

    protected:
        virtual void OnFormClosing(FormClosingEventArgs^ e) override
        {
            // keep the dialog (and its history) alive - just hide it
            if (e->CloseReason == WF::CloseReason::UserClosing) { e->Cancel = true; Hide(); }
            ThemedDialog::OnFormClosing(e);
        }

    private:
        ComboBox^ findBox;
        ComboBox^ replaceBox;
        ComboBox^ scopeBox;
        CheckBox^ matchCase;
        CheckBox^ wholeWord;
        CheckBox^ useRegex;
        Label^ status;
        Button^ btnNext;
        Button^ btnReplace;
        Button^ btnReplaceAll;
        Button^ btnFindAll;

        static CheckBox^ MakeCheck(String^ text, FlowLayoutPanel^ p)
        {
            CheckBox^ c = gcnew CheckBox();
            c->Text = text;
            c->AutoSize = true;
            c->Margin = WF::Padding(0, 4, 14, 0);
            p->Controls->Add(c);
            return c;
        }

        Button^ AddAction(String^ text, FindAction a)
        {
            Button^ b = AddButton(text);
            b->Name = ((int)a).ToString();
            b->Click += gcnew EventHandler(this, &FindReplaceDialog::OnAction);
            return b;
        }

        static void Remember(ComboBox^ c)
        {
            String^ t = c->Text;
            if (String::IsNullOrEmpty(t)) return;
            c->Items->Remove(t);
            c->Items->Insert(0, t);
            c->Text = t;
        }

        void OnAction(Object^ sender, EventArgs^ e)
        {
            if (findBox->Text->Length == 0) { status->Text = "Type something to find."; return; }
            Remember(findBox);
            Remember(replaceBox);
            FindRequest^ r = gcnew FindRequest();
            r->Pattern = findBox->Text;
            r->Replacement = replaceBox->Text;
            r->Options = gcnew FindOptions();
            r->Options->MatchCase = matchCase->Checked;
            r->Options->WholeWord = wholeWord->Checked;
            r->Options->UseRegex = useRegex->Checked;
            r->Action = (FindAction)Int32::Parse(safe_cast<Button^>(sender)->Name);
            r->Options->Forward = r->Action != FindAction::FindPrevious;
            r->Scope = (FindScope)scopeBox->SelectedIndex;
            Requested(r);
        }

        void OnClose(Object^ sender, EventArgs^ e) { Hide(); }
    };

    // -------------------------------------------------------------------------
    public ref class GoToLineDialog : public ThemedDialog
    {
    public:
        GoToLineDialog(int current, int max) : ThemedDialog("Go To Line", 340, 130)
        {
            TableLayoutPanel^ grid = MakeGrid(150);
            number = gcnew NumericUpDown();
            number->Minimum = Decimal(1);
            number->Maximum = Decimal(Math::Max(1, max));
            number->Value = Decimal(Math::Min(Math::Max(1, current), Math::Max(1, max)));
            AddRow(grid, String::Format("Line number (1 - {0})", max), number);
            Controls->Add(grid);
            AddOkCancel("OK");
        }
        property int Line { int get() { return Decimal::ToInt32(number->Value); } }
    protected:
        virtual void OnShown(EventArgs^ e) override
        {
            ThemedDialog::OnShown(e);
            number->Select(0, number->Text->Length);
            number->Focus();
        }
    private:
        NumericUpDown^ number;
    };

    // -------------------------------------------------------------------------
    public ref class WindowsDialog : public ThemedDialog
    {
    public:
        WindowsDialog(List<String^>^ titles, List<String^>^ paths) : ThemedDialog("Windows", 640, 420)
        {
            Action = String::Empty;
            Selected = gcnew List<int>();
            list = gcnew ListView();
            list->Dock = DockStyle::Fill;
            list->View = WF::View::Details;
            list->FullRowSelect = true;
            list->Columns->Add("Name", 180);
            list->Columns->Add("Path", 420);
            for (int i = 0; i < titles->Count; i++)
            {
                ListViewItem^ it = gcnew ListViewItem(titles[i]);
                it->SubItems->Add(paths[i]);
                list->Items->Add(it);
            }
            Panel^ host = gcnew Panel();
            host->Dock = DockStyle::Fill;
            host->Padding = WF::Padding(10);
            host->Controls->Add(list);
            Controls->Add(host);
            host->BringToFront();
            Button^ ok = AddButton("Close");
            ok->DialogResult = WF::DialogResult::Cancel;
            CancelButton = ok;
            AddAct("Close Window(s)");
            AddAct("Save");
            Button^ act = AddAct("Activate");
            act->Tag = "accent";
            list->DoubleClick += gcnew EventHandler(this, &WindowsDialog::OnDouble);
        }
        property String^ Action;
        property List<int>^ Selected;
    private:
        ListView^ list;
        Button^ AddAct(String^ text)
        {
            Button^ b = AddButton(text);
            b->Click += gcnew EventHandler(this, &WindowsDialog::OnAct);
            return b;
        }
        void Finish(String^ a)
        {
            Selected->Clear();
            for each (int i in list->SelectedIndices) Selected->Add(i);
            if (Selected->Count == 0) return;
            Action = a;
            DialogResult = WF::DialogResult::OK;
            Close();
        }
        void OnAct(Object^ sender, EventArgs^ e) { Finish(safe_cast<Button^>(sender)->Text); }
        void OnDouble(Object^ sender, EventArgs^ e) { Finish("Activate"); }
    };

    // -------------------------------------------------------------------------
    public ref class SnippetsDialog : public ThemedDialog
    {
    public:
        SnippetsDialog(Dictionary<String^, String^>^ snippets, bool canInsert) : ThemedDialog("Code Snippets Manager", 760, 480)
        {
            Snippets = gcnew Dictionary<String^, String^>(snippets);
            InsertText = nullptr;
            FormBorderStyle = WF::FormBorderStyle::Sizable;

            list = gcnew ListBox();
            list->Dock = DockStyle::Left;
            list->Width = 230;
            list->IntegralHeight = false;
            list->SelectedIndexChanged += gcnew EventHandler(this, &SnippetsDialog::OnSelect);
            editor = gcnew TextBox();
            editor->Multiline = true;
            editor->AcceptsTab = true;
            editor->AcceptsReturn = true;
            editor->ScrollBars = WF::ScrollBars::Both;
            editor->WordWrap = false;
            editor->Dock = DockStyle::Fill;
            editor->Font = gcnew SD::Font("Consolas", 10.0f);
            editor->TextChanged += gcnew EventHandler(this, &SnippetsDialog::OnEdit);
            Panel^ host = gcnew Panel();
            host->Dock = DockStyle::Fill;
            host->Padding = WF::Padding(10);
            host->Controls->Add(editor);
            host->Controls->Add(list);
            Controls->Add(host);
            host->BringToFront();

            AddOkCancel("Save");
            Button^ del = AddButton("Delete");
            del->Click += gcnew EventHandler(this, &SnippetsDialog::OnDelete);
            Button^ addBtn = AddButton("New...");
            addBtn->Click += gcnew EventHandler(this, &SnippetsDialog::OnNew);
            if (canInsert)
            {
                Button^ ins = AddButton("Insert");
                ins->Click += gcnew EventHandler(this, &SnippetsDialog::OnInsert);
            }
            Fill();
        }
        property Dictionary<String^, String^>^ Snippets;
        property String^ InsertText;

    private:
        ListBox^ list;
        TextBox^ editor;
        bool loading;

        void Fill()
        {
            list->Items->Clear();
            List<String^>^ keys = gcnew List<String^>(Snippets->Keys);
            keys->Sort();
            for each (String^ k in keys) list->Items->Add(k);
            if (list->Items->Count > 0) list->SelectedIndex = 0;
        }
        String^ Current() { return safe_cast<String^>(list->SelectedItem); }
        void OnSelect(Object^ sender, EventArgs^ e)
        {
            String^ k = Current();
            if (k == nullptr) return;
            loading = true;
            editor->Text = Snippets[k]->Replace("\n", "\r\n");
            loading = false;
        }
        void OnEdit(Object^ sender, EventArgs^ e)
        {
            String^ k = Current();
            if (!loading && k != nullptr) Snippets[k] = editor->Text->Replace("\r\n", "\n");
        }
        void OnDelete(Object^ sender, EventArgs^ e)
        {
            String^ k = Current();
            if (k != nullptr) { Snippets->Remove(k); Fill(); }
        }
        void OnNew(Object^ sender, EventArgs^ e)
        {
            String^ name = String::Format("snippet {0}", Snippets->Count + 1);
            Snippets[name] = String::Empty;
            Fill();
            list->SelectedItem = name;
            editor->Focus();
        }
        void OnInsert(Object^ sender, EventArgs^ e)
        {
            String^ k = Current();
            if (k == nullptr) return;
            InsertText = Snippets[k];
            DialogResult = WF::DialogResult::OK;
            Close();
        }
    };
}}
