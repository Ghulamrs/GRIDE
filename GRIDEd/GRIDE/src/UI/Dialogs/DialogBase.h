// =============================================================================
//  DialogBase.h - common base for every GRIDE dialog
//    ThemedDialog   fixed dialog, themed, OK/Cancel bar, layout helpers
//    TextDialog     read-only text viewer (release notes, logs ...)
// =============================================================================
#pragma once
#include "../Theme.h"
#include "../IconFactory.h"

namespace Gride { namespace UI {

    public ref class ThemedDialog : public Form
    {
    public:
        ThemedDialog(String^ title, int width, int height)
        {
            Text = title;
            ClientSize = SD::Size(width, height);
            FormBorderStyle = WF::FormBorderStyle::FixedDialog;
            StartPosition = FormStartPosition::CenterParent;
            MinimizeBox = false;
            MaximizeBox = false;
            ShowInTaskbar = false;
            Font = gcnew SD::Font("Segoe UI", 9.0f);
            KeyPreview = true;

            buttons = gcnew FlowLayoutPanel();
            buttons->Dock = DockStyle::Bottom;
            buttons->FlowDirection = FlowDirection::RightToLeft;
            buttons->Height = 48;
            buttons->Padding = WF::Padding(8, 8, 8, 8);
            buttons->Tag = "header";
            Controls->Add(buttons);
            okButton = nullptr;
            cancelButton = nullptr;
        }

    protected:
        FlowLayoutPanel^ buttons;
        Button^ okButton;
        Button^ cancelButton;

        virtual void OnLoad(EventArgs^ e) override
        {
            ThemeManager::Apply(this);
            Form::OnLoad(e);
        }

        // adds OK (accent) + Cancel; the OK click calls OnAccept()
        void AddOkCancel(String^ okText)
        {
            cancelButton = AddButton("Cancel");
            cancelButton->DialogResult = WF::DialogResult::Cancel;
            CancelButton = cancelButton;
            okButton = AddButton(okText);
            okButton->Tag = "accent";
            okButton->Click += gcnew EventHandler(this, &ThemedDialog::OnOkClick);
            AcceptButton = okButton;
        }

        Button^ AddButton(String^ text)
        {
            Button^ b = gcnew Button();
            b->Text = text;
            b->AutoSize = true;
            b->MinimumSize = SD::Size(92, 30);
            b->Margin = WF::Padding(6, 0, 0, 0);
            buttons->Controls->Add(b);
            return b;
        }

        // override to validate; return false to keep the dialog open
        virtual bool OnAccept() { return true; }

        // ---- layout helpers ------------------------------------------------------
        static TableLayoutPanel^ MakeGrid(int labelWidth)
        {
            TableLayoutPanel^ t = gcnew TableLayoutPanel();
            t->ColumnCount = 2;
            t->ColumnStyles->Add(gcnew ColumnStyle(SizeType::Absolute, (float)labelWidth));
            t->ColumnStyles->Add(gcnew ColumnStyle(SizeType::Percent, 100.0f));
            t->AutoSize = true;
            t->Dock = DockStyle::Top;
            t->Padding = WF::Padding(4);
            return t;
        }

        static void AddRow(TableLayoutPanel^ t, String^ label, Control^ c)
        {
            Label^ l = gcnew Label();
            l->Text = label;
            l->AutoSize = false;
            l->Dock = DockStyle::Fill;
            l->TextAlign = ContentAlignment::MiddleLeft;
            c->Dock = DockStyle::Fill;
            c->Margin = WF::Padding(3, 4, 3, 4);
            t->RowCount++;
            t->RowStyles->Add(gcnew RowStyle(SizeType::AutoSize));
            t->Controls->Add(l, 0, t->RowCount - 1);
            t->Controls->Add(c, 1, t->RowCount - 1);
        }

        // text box + "..." browse button in one row
        static Panel^ BrowseRow(TextBox^ box, Button^% browse)
        {
            Panel^ p = gcnew Panel();
            p->Height = 28;
            browse = gcnew Button();
            browse->Text = "...";
            browse->Width = 34;
            browse->Dock = DockStyle::Right;
            box->Dock = DockStyle::Fill;
            p->Controls->Add(box);
            p->Controls->Add(browse);
            return p;
        }

        static Label^ Heading(String^ text)
        {
            Label^ l = gcnew Label();
            l->Text = text;
            l->Font = gcnew SD::Font("Segoe UI Semibold", 10.5f);
            l->AutoSize = false;
            l->Height = 30;
            l->Dock = DockStyle::Top;
            l->TextAlign = ContentAlignment::BottomLeft;
            return l;
        }

    private:
        void OnOkClick(Object^ sender, EventArgs^ e)
        {
            if (OnAccept())
            {
                DialogResult = WF::DialogResult::OK;
                Close();
            }
        }
    };

    // -------------------------------------------------------------------------
    public ref class InputDialog : public ThemedDialog
    {
    public:
        InputDialog(String^ title, String^ prompt, String^ initial) : ThemedDialog(title, 420, 130)
        {
            TableLayoutPanel^ grid = MakeGrid(110);
            box = gcnew TextBox();
            box->Text = initial;
            AddRow(grid, prompt, box);
            Controls->Add(grid);
            AddOkCancel("OK");
        }
        property String^ Value { String^ get() { return box->Text->Trim(); } }
    protected:
        virtual bool OnAccept() override { return Value->Length > 0; }
    private:
        TextBox^ box;
    };

    // -------------------------------------------------------------------------
    public ref class TextDialog : public ThemedDialog
    {
    public:
        TextDialog(String^ title, String^ text) : ThemedDialog(title, 720, 480)
        {
            FormBorderStyle = WF::FormBorderStyle::Sizable;
            MinimumSize = SD::Size(400, 300);
            RichTextBox^ box = gcnew RichTextBox();
            box->Dock = DockStyle::Fill;
            box->ReadOnly = true;
            box->BorderStyle = WF::BorderStyle::None;
            box->Font = gcnew SD::Font("Consolas", 9.5f);
            box->Text = text;
            Controls->Add(box);
            box->BringToFront();
            Button^ close = AddButton("Close");
            close->DialogResult = WF::DialogResult::OK;
            AcceptButton = close;
            CancelButton = close;
        }
    };
}}
