// =============================================================================
//  CodeTextBox.h - RichTextBox tuned for source code
//    * plain-text paste, auto-indent on Enter, Tab / Shift+Tab block indent
//    * Ctrl+Z / Ctrl+Y / Ctrl+X forwarded to CodeEditor (its own delta undo);
//      the RichEdit undo buffer is switched off
//    * optional "show white space" overlay (dots for spaces, arrows for tabs)
// =============================================================================
#pragma once
#include "../Theme.h"
#include "../../Core/NativeMethods.h"

namespace Gride { namespace UI {

    using namespace Gride::Native;

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
            cachedText = nullptr;
            cachedLength = -1;
            charWidth = 0.0f;
            selTop = selBottom = 0;
        }

        property bool ShowWhitespace;
        property int TabSize;
        property bool InsertSpaces;
        property bool AutoIndent;
        property bool Recolouring;      // set by CodeEditor while it changes colours (selection moves are not the user's)

        event EventHandler^ UndoRequested;
        event EventHandler^ RedoRequested;
        event EventHandler^ CutRequested;        // Ctrl+X / Shift+Del: cut as one undo step
        event EventHandler^ BeforeEdit;          // ends the current typing burst (caret jump, paste ...)
        event EventHandler^ AfterEdit;           // a programmatic edit is complete: record it as one undo step
        event BoolHandler^ IndentRequested;      // true = indent, false = outdent

        // selection just before the last key / paste was handled, i.e. before the
        // edit it caused (CodeEditor restores it on Undo)
        property int KeySelStart { int get() { return keySelStart; } }
        property int KeySelLength { int get() { return keySelLength; } }

        // the text, read from the control again only after it changed
        property String^ CachedText
        {
            String^ get()
            {
                int len = TextLength;
                if (cachedText == nullptr || len != cachedLength) { cachedText = Text; cachedLength = len; }
                return cachedText;
            }
        }

        int CurrentLineIndex() { return GetLineFromCharIndex(SelectionStart); }

        String^ LineText(int lineIndex)
        {
            int start = GetFirstCharIndexFromLine(lineIndex);
            if (start < 0) return String::Empty;
            int next = GetFirstCharIndexFromLine(lineIndex + 1);
            String^ all = CachedText;
            if (start > all->Length) return String::Empty;
            int end = next < 0 ? all->Length : Math::Min(all->Length, next - 1);
            if (end < start) end = start;
            return all->Substring(start, end - start);
        }

        void PastePlain()
        {
            if (ReadOnly) return;
            String^ clip = nullptr;
            try { if (Clipboard::ContainsText()) clip = Clipboard::GetText(); }
            catch (System::Runtime::InteropServices::ExternalException^) { }   // clipboard held by another program
            if (clip == nullptr) return;
            BeforeEdit(this, EventArgs::Empty);
            RememberKeySelection();
            SelectedText = clip->Replace("\r\n", "\n");
            AfterEdit(this, EventArgs::Empty);
        }

        String^ IndentUnit()
        {
            if (InsertSpaces) return gcnew String(L' ', TabSize);
            return "\t";
        }

        // RichTextBox's own version reads the whole Text (a full stream-out) on every call
        // just to range-check the index; the gutter and the white space overlay call it
        // once per line / character while painting
        virtual SD::Point GetPositionFromCharIndex(int index) override
        {
            if (!IsHandleCreated || index < 0 || index > TextLength) return SD::Point::Empty;
            return NativeMethods::PosFromChar(Handle, index);
        }

        float CharWidth()
        {
            if (charWidth <= 0.0f)
                charWidth = TextRenderer::MeasureText("MMMMMMMMMM", this->Font, SD::Size(1000, 100), TextFormatFlags::NoPadding).Width / 10.0f;
            return charWidth;
        }

    protected:
        virtual void OnHandleCreated(EventArgs^ e) override
        {
            RichTextBox::OnHandleCreated(e);
            NativeMethods::SetUndoLimit(Handle, 0);
        }

        virtual void OnFontChanged(EventArgs^ e) override
        {
            charWidth = 0.0f;
            RichTextBox::OnFontChanged(e);
        }

        virtual void OnTextChanged(EventArgs^ e) override
        {
            cachedText = nullptr;
            RichTextBox::OnTextChanged(e);
            // RichEdit redraws typed text directly, outside WM_PAINT, which wipes the overlay
            if (ShowWhitespace) Invalidate();
        }

        virtual void OnSelectionChanged(EventArgs^ e) override
        {
            RichTextBox::OnSelectionChanged(e);
            if (ShowWhitespace && IsHandleCreated && !Recolouring) InvalidateSelectionBand();
        }

        virtual void OnMouseDown(MouseEventArgs^ e) override
        {
            BeforeEdit(this, EventArgs::Empty);        // a click moves the caret: the next typing is a new undo step
            RichTextBox::OnMouseDown(e);
        }

        virtual bool ProcessCmdKey(WF::Message% msg, Keys keyData) override
        {
            if (msg.Msg == 0x0100) RememberKeySelection();      // WM_KEYDOWN (Enter, Delete ... edit here)
            if (keyData == (Keys::Control | Keys::V) || keyData == (Keys::Shift | Keys::Insert)) { PastePlain(); return true; }
            if (keyData == (Keys::Control | Keys::Z)) { UndoRequested(this, EventArgs::Empty); return true; }
            if (keyData == (Keys::Control | Keys::Y) || keyData == (Keys::Control | Keys::Shift | Keys::Z)) { RedoRequested(this, EventArgs::Empty); return true; }
            if (IsNavigationKey(keyData)) BeforeEdit(this, EventArgs::Empty);
            if (ReadOnly)
            {
                if (RichTextBox::ProcessCmdKey(msg, keyData)) return true;
                return IsFormattingKey(keyData);
            }

            if (keyData == (Keys::Control | Keys::X) || keyData == (Keys::Shift | Keys::Delete))
            {
                CutRequested(this, EventArgs::Empty);
                return true;
            }

            // Enter, Backspace and Delete are part of the typing burst like any other key
            if (keyData == Keys::Return && AutoIndent)
            {
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

        static bool IsNavigationKey(Keys k)
        {
            Keys code = k & Keys::KeyCode;
            return code == Keys::Up || code == Keys::Down || code == Keys::Left || code == Keys::Right ||
                   code == Keys::Home || code == Keys::End || code == Keys::PageUp || code == Keys::PageDown;
        }

        // The overlay is drawn while handling WM_PAINT: the update region is read
        // before RichEdit validates it, RichEdit paints, then the marks are drawn
        // clipped to that region, so nothing outside it is touched (no flicker).
        virtual void WndProc(WF::Message% m) override
        {
            if (m.Msg == 0x000F && ShowWhitespace && IsHandleCreated)      // WM_PAINT
            {
                SD::Rectangle update = NativeMethods::GetUpdateRect(Handle);
                RichTextBox::WndProc(m);
                if (!update.IsEmpty) DrawWhitespace(update);
                return;
            }
            if (m.Msg == 0x0102 || m.Msg == 0x0286) RememberKeySelection();   // WM_CHAR, WM_IME_CHAR
            RichTextBox::WndProc(m);
        }

    private:
        String^ cachedText;
        int cachedLength;
        float charWidth;
        int selTop, selBottom;      // pixel band of the selection last time (white space overlay)
        int keySelStart, keySelLength;

        void RememberKeySelection()
        {
            if (!IsHandleCreated) return;
            keySelStart = SelectionStart;
            keySelLength = SelectionLength;
        }

        void InvalidateSelectionBand()
        {
            int lh = this->Font->Height;
            int y1 = GetPositionFromCharIndex(SelectionStart).Y;
            int y2 = SelectionLength > 0 ? GetPositionFromCharIndex(SelectionStart + SelectionLength).Y : y1;
            int top = Math::Min(y1, y2), bottom = Math::Max(y1, y2) + lh;
            int t = Math::Min(top, selTop), b = Math::Max(bottom, selBottom);
            selTop = top; selBottom = bottom;
            Invalidate(SD::Rectangle(0, t, ClientSize.Width, Math::Max(lh, b - t)));
        }

        void DrawWhitespace(SD::Rectangle clip)
        {
            int length = TextLength;
            if (length == 0 || length > 400000) return;
            String^ all = CachedText;
            int first = GetCharIndexFromPosition(SD::Point(0, Math::Max(0, clip.Top)));
            first = GetFirstCharIndexFromLine(GetLineFromCharIndex(first));
            int last = GetCharIndexFromPosition(SD::Point(ClientSize.Width, Math::Min(ClientSize.Height, clip.Bottom)));
            int nextLine = GetFirstCharIndexFromLine(GetLineFromCharIndex(last) + 1);
            last = nextLine < 0 ? all->Length - 1 : nextLine - 1;
            if (first < 0) first = 0;
            if (last >= all->Length) last = all->Length - 1;
            if (last < first) return;

            Graphics^ g = Graphics::FromHwnd(Handle);
            Color c = Color::FromArgb(110, ThemeManager::Current->MutedText);
            SolidBrush^ b = gcnew SolidBrush(c);
            Pen^ p = gcnew Pen(c);
            try
            {
                g->SetClip(clip);
                float cw = CharWidth();
                int lh = this->Font->Height;
                for (int i = first; i <= last; i++)
                {
                    wchar_t ch = all[i];
                    if (ch != L' ' && ch != L'\t') continue;
                    SD::Point pt = GetPositionFromCharIndex(i);
                    if (pt.X < 0 || pt.Y + lh < clip.Top || pt.Y > clip.Bottom) continue;
                    if (ch == L' ') g->FillRectangle(b, pt.X + cw / 2 - 1, pt.Y + lh / 2.0f - 1, 2.0f, 2.0f);
                    else
                    {
                        float y = pt.Y + lh / 2.0f;
                        g->DrawLine(p, (float)pt.X + 2, y, (float)pt.X + cw * 1.5f, y);
                        g->DrawLine(p, (float)pt.X + cw * 1.5f - 3, y - 3, (float)pt.X + cw * 1.5f, y);
                        g->DrawLine(p, (float)pt.X + cw * 1.5f - 3, y + 3, (float)pt.X + cw * 1.5f, y);
                    }
                }
            }
            finally { delete p; delete b; delete g; }
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
