// =============================================================================
//  CodeTextBox.h - RichTextBox tuned for source code
//    * plain-text paste, auto-indent on Enter, Tab / Shift+Tab block indent
//    * Ctrl+Z / Ctrl+Y forwarded to GRIDE's UndoManager
//    * optional "show white space" overlay (dots for spaces, arrows for tabs)
// =============================================================================
#pragma once
#include "../Theme.h"
#include "../../Core/NativeMethods.h"

namespace Gride { namespace UI {

    public delegate void BoolHandler(bool value);
    public delegate void LineHandler(int line);   // 1-based line numbers

    public ref class CodeTextBox : public RichTextBox
    {
    public:
        CodeTextBox()
        {
            AcceptsTab = true;
            WordWrap = false;
            DetectUrls = false;
            HideSelection = false;
            ScrollBars = RichTextBoxScrollBars::Both;
            BorderStyle = WF::BorderStyle::None;
            ShowWhitespace = false;
            TabSize = 4;
            InsertSpaces = false;
            AutoIndent = true;
            EnableAutoDragDrop = false;
        }

        property bool ShowWhitespace;
        property int TabSize;
        property bool InsertSpaces;
        property bool AutoIndent;

        event EventHandler^ UndoRequested;
        event EventHandler^ RedoRequested;
        event EventHandler^ BeforeEdit;          // a good moment for an undo snapshot
        event BoolHandler^ IndentRequested;      // true = indent, false = outdent

        int CurrentLineIndex() { return GetLineFromCharIndex(SelectionStart); }

        String^ LineText(int lineIndex)
        {
            int start = GetFirstCharIndexFromLine(lineIndex);
            if (start < 0) return String::Empty;
            int next = GetFirstCharIndexFromLine(lineIndex + 1);
            String^ all = Text;
            int end = next < 0 ? all->Length : next - 1;
            if (end < start) end = start;
            return all->Substring(start, end - start);
        }

        void PastePlain()
        {
            if (ReadOnly || !Clipboard::ContainsText()) return;
            BeforeEdit(this, EventArgs::Empty);
            SelectedText = Clipboard::GetText()->Replace("\r\n", "\n");
        }

        String^ IndentUnit()
        {
            if (InsertSpaces) return gcnew String(L' ', TabSize);
            return "\t";
        }

        float CharWidth()
        {
            return TextRenderer::MeasureText("MMMMMMMMMM", this->Font, SD::Size(1000, 100), TextFormatFlags::NoPadding).Width / 10.0f;
        }

    protected:
        virtual bool ProcessCmdKey(WF::Message% msg, Keys keyData) override
        {
            if (keyData == (Keys::Control | Keys::V) || keyData == (Keys::Shift | Keys::Insert)) { PastePlain(); return true; }
            if (keyData == (Keys::Control | Keys::Z)) { UndoRequested(this, EventArgs::Empty); return true; }
            if (keyData == (Keys::Control | Keys::Y) || keyData == (Keys::Control | Keys::Shift | Keys::Z)) { RedoRequested(this, EventArgs::Empty); return true; }
            if (ReadOnly)
            {
                if (RichTextBox::ProcessCmdKey(msg, keyData)) return true;
                return IsFormattingKey(keyData);
            }

            if (keyData == (Keys::Control | Keys::X) || keyData == Keys::Delete || keyData == Keys::Back)
                BeforeEdit(this, EventArgs::Empty);

            if (keyData == Keys::Return && AutoIndent)
            {
                BeforeEdit(this, EventArgs::Empty);
                String^ line = LineText(CurrentLineIndex());
                int caretCol = SelectionStart - GetFirstCharIndexOfCurrentLine();
                String^ before = line->Substring(0, Math::Min(caretCol, line->Length));
                int n = 0;
                while (n < line->Length && (line[n] == L' ' || line[n] == L'\t')) n++;
                String^ indent = line->Substring(0, Math::Min(n, before->Length));
                if (before->TrimEnd()->EndsWith("{")) indent = indent + IndentUnit();
                SelectedText = "\n" + indent;
                return true;
            }

            if (keyData == Keys::Tab || keyData == (Keys::Shift | Keys::Tab))
            {
                bool outdent = keyData != Keys::Tab;
                bool multiLine = SelectionLength > 0 &&
                                 GetLineFromCharIndex(SelectionStart) != GetLineFromCharIndex(SelectionStart + SelectionLength);
                if (multiLine || outdent) { IndentRequested(!outdent); return true; }
                if (InsertSpaces)
                {
                    int col = SelectionStart - GetFirstCharIndexOfCurrentLine();
                    SelectedText = gcnew String(L' ', TabSize - (col % TabSize));
                    return true;
                }
            }
            if (RichTextBox::ProcessCmdKey(msg, keyData)) return true;   // menu shortcuts first
            return IsFormattingKey(keyData);                               // then swallow rich-text formatting keys
        }

        // Ctrl+B/I/U/L/R/E/J, Ctrl+1/2/5, Ctrl+=, Ctrl+Shift+</> ... would change fonts in a RichTextBox
        static bool IsFormattingKey(Keys k)
        {
            Keys code = k & Keys::KeyCode;
            bool ctrl = (k & Keys::Control) == Keys::Control;
            if (!ctrl || (k & Keys::Alt) == Keys::Alt) return false;
            return code == Keys::B || code == Keys::I || code == Keys::U || code == Keys::L || code == Keys::R ||
                   code == Keys::E || code == Keys::J || code == Keys::D1 || code == Keys::D2 || code == Keys::D5 ||
                   code == Keys::Oemplus || code == Keys::OemPeriod || code == Keys::Oemcomma ||
                   ((k & Keys::Shift) == Keys::Shift && (code == Keys::A || code == Keys::L));
        }

        virtual void WndProc(WF::Message% m) override
        {
            RichTextBox::WndProc(m);
            if (m.Msg == 0x000F && ShowWhitespace) DrawWhitespace();   // WM_PAINT
        }

    private:
        void DrawWhitespace()
        {
            if (TextLength == 0 || TextLength > 400000) return;
            int first = GetCharIndexFromPosition(SD::Point(0, 0));
            int last = GetCharIndexFromPosition(SD::Point(ClientSize.Width, ClientSize.Height));
            String^ all = Text;
            if (last >= all->Length) last = all->Length - 1;
            Graphics^ g = CreateGraphics();
            Color c = Color::FromArgb(110, ThemeManager::Current->MutedText);
            SolidBrush^ b = gcnew SolidBrush(c);
            Pen^ p = gcnew Pen(c);
            float cw = CharWidth();
            int lh = this->Font->Height;
            for (int i = first; i <= last; i++)
            {
                wchar_t ch = all[i];
                if (ch != L' ' && ch != L'\t') continue;
                SD::Point pt = GetPositionFromCharIndex(i);
                if (pt.X < 0) continue;
                if (ch == L' ') g->FillRectangle(b, pt.X + cw / 2 - 1, pt.Y + lh / 2.0f - 1, 2.0f, 2.0f);
                else
                {
                    float y = pt.Y + lh / 2.0f;
                    g->DrawLine(p, (float)pt.X + 2, y, (float)pt.X + cw * 1.5f, y);
                    g->DrawLine(p, (float)pt.X + cw * 1.5f - 3, y - 3, (float)pt.X + cw * 1.5f, y);
                    g->DrawLine(p, (float)pt.X + cw * 1.5f - 3, y + 3, (float)pt.X + cw * 1.5f, y);
                }
            }
            delete p; delete b; delete g;
        }
    };

    // -------------------------------------------------------------------------
    //  LineGutter - line numbers + breakpoint / bookmark / execution markers
    // -------------------------------------------------------------------------
    public ref class LineGutter : public Control
    {
    public:
        LineGutter(CodeTextBox^ box)
        {
            this->box = box;
            Breakpoints = gcnew SortedSet<int>();
            Bookmarks = gcnew SortedSet<int>();
            ExecLine = 0;
            ShowNumbers = true;
            SetStyle(ControlStyles::AllPaintingInWmPaint | ControlStyles::OptimizedDoubleBuffer | ControlStyles::UserPaint, true);
            Dock = DockStyle::Left;
            Width = 48;
            Cursor = Cursors::Hand;
        }

        property SortedSet<int>^ Breakpoints;   // 1-based
        property SortedSet<int>^ Bookmarks;     // 1-based
        property int ExecLine;                  // 1-based, 0 = none
        property bool ShowNumbers;

        event LineHandler^ LineClicked;

        void UpdateWidth()
        {
            int lines = box->GetLineFromCharIndex(box->TextLength) + 1;
            int digits = Math::Max(3, lines.ToString()->Length);
            int w = 22 + (ShowNumbers ? (int)(digits * box->CharWidth()) + 10 : 0);
            if (w != Width) Width = w;
            Invalidate();
        }

    protected:
        virtual void OnPaint(PaintEventArgs^ e) override
        {
            Theme^ t = ThemeManager::Current;
            Graphics^ g = e->Graphics;
            g->Clear(t->Window);
            g->SmoothingMode = SD::Drawing2D::SmoothingMode::AntiAlias;

            int lineH = box->Font->Height;
            int total = box->GetLineFromCharIndex(box->TextLength) + 1;
            int firstChar = box->GetCharIndexFromPosition(SD::Point(1, 1));
            int line = box->GetLineFromCharIndex(firstChar);
            int current = box->GetLineFromCharIndex(box->SelectionStart);
            int prevY = -lineH;
            SolidBrush^ bpBrush = gcnew SolidBrush(t->Breakpoint);
            SolidBrush^ bmBrush = gcnew SolidBrush(t->Bookmark);
            SolidBrush^ exBrush = gcnew SolidBrush(t->Accent);

            while (line < total)
            {
                int idx = box->GetFirstCharIndexFromLine(line);
                if (idx < 0) break;
                int y = box->GetPositionFromCharIndex(idx).Y;
                if (idx >= box->TextLength && line > 0) y = prevY + lineH;   // empty last line
                if (y > Height) break;
                prevY = y;
                int n = line + 1;

                if (ShowNumbers)
                {
                    SD::Rectangle r = SD::Rectangle(18, y, Width - 24, lineH);
                    TextRenderer::DrawText(g, n.ToString(), box->Font, r, line == current ? t->Text : t->LineNumber,
                        TextFormatFlags::Right | TextFormatFlags::Top | TextFormatFlags::NoPadding);
                }
                int cy = y + lineH / 2;
                if (Breakpoints->Contains(n)) g->FillEllipse(bpBrush, 3, cy - 6, 12, 12);
                if (Bookmarks->Contains(n)) g->FillRectangle(bmBrush, 1, cy - 5, 4, 10);
                if (ExecLine == n)
                {
                    g->FillPolygon(exBrush, gcnew array<SD::Point>{ SD::Point(4, cy - 6), SD::Point(14, cy), SD::Point(4, cy + 6) });
                }
                line++;
            }
            delete bpBrush; delete bmBrush; delete exBrush;
            Pen^ sep = gcnew Pen(t->Border);
            g->DrawLine(sep, Width - 3, 0, Width - 3, Height);
            delete sep;
        }

        virtual void OnMouseDown(MouseEventArgs^ e) override
        {
            Control::OnMouseDown(e);
            int idx = box->GetCharIndexFromPosition(SD::Point(1, e->Y));
            int line = box->GetLineFromCharIndex(idx) + 1;
            LineClicked(line);
        }

    private:
        CodeTextBox^ box;
    };
}}
