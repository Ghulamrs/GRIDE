// =============================================================================
//  CodeTextBox.h - RichTextBox tuned for source code
//    * plain-text paste, auto-indent on Enter, Tab / Shift+Tab block indent
//    * Ctrl+Z / Ctrl+Y / Ctrl+X forwarded to CodeEditor (its own delta undo);
//      the RichEdit undo buffer is switched off
//    * optional "show white space" overlay (dots for spaces, arrows for tabs)
//    * folding: the { } blocks of the text and the folded ones (CodeEditor hides
//      their lines with CFE_HIDDEN); "..." is drawn after a folded block's '{' line
// =============================================================================
#pragma once
#include "../Theme.h"
#include "../../Core/NativeMethods.h"

namespace Gride { namespace UI {

    using namespace Gride::Native;

    public delegate void BoolHandler(bool value);
    public delegate void LineHandler(int line);   // 1-based line numbers
    public delegate void IndexHandler(int index); // a character index of the text

    // A folded block: its '{', and the hidden run from the '\n' ending the '{' line up to
    // the '\n' ending the line before the '}', which stays visible. RichEdit puts the caret
    // and typed text beside a hidden run, never in it.
    public value struct FoldSpan
    {
        int Open;
        int Start;
        int End;
    };

    public ref class CodeTextBox : public RichTextBox
    {
    public:
        literal int MaxFoldLength = 1500000;      // larger texts are not folded

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
            lineStarts = nullptr;
            linesFor = nullptr;
            hidden = gcnew List<FoldSpan>();
            hiddenFor = nullptr;
            foldsFor = nullptr;
            foldOpen = gcnew List<int>();
            foldStart = gcnew List<int>();
            foldEnd = gcnew List<int>();
            foldCloseLine = gcnew List<int>();
            headFold = nullptr;
            guide = nullptr;
        }

        property bool ShowWhitespace;
        property int TabSize;
        property bool InsertSpaces;
        property bool AutoIndent;
        property bool Recolouring;      // set by CodeEditor while it changes colours (selection moves are not the user's)
        // the language's comments, which the fold scan skips
        property String^ LineComment;
        property String^ BlockCommentStart;
        property String^ BlockCommentEnd;

        event EventHandler^ UndoRequested;
        event EventHandler^ RedoRequested;
        event EventHandler^ CutRequested;        // Ctrl+X / Shift+Del: cut as one undo step
        event EventHandler^ BeforeEdit;          // ends the current typing burst (caret jump, paste ...)
        event EventHandler^ AfterEdit;           // a programmatic edit is complete: record it as one undo step
        event BoolHandler^ IndentRequested;      // true = indent, false = outdent
        event IndexHandler^ FoldMarkClicked;     // the "..." of a folded block, by its '{'
        event IndexHandler^ FoldEditing;         // a key is about to edit at a folded block's edge, by its '{'

        // selection just before the last key / paste was handled, i.e. before the
        // edit it caused (CodeEditor restores it on Undo)
        property int KeySelStart { int get() { return keySelStart; } }
        property int KeySelLength { int get() { return keySelLength; } }

        // the text, read from the control again only after it changed; while a block is folded
        // Text (a stream-out) leaves its hidden lines out, and they are read with EM_GETTEXTEX
        property String^ CachedText
        {
            String^ get()
            {
                int len = TextLength;
                if (cachedText == nullptr || len != cachedLength)
                {
                    cachedText = hidden->Count > 0 && IsHandleCreated ? NativeMethods::GetFullText(Handle, len)->Replace(L'\r', L'\n') : Text;
                    cachedLength = len;
                }
                return cachedText;
            }
        }

        // the selected text whole: SelectedText and RichEdit's own copy leave hidden lines out
        String^ SelectionWhole()
        {
            String^ all = CachedText;
            int s = Math::Max(0, Math::Min(SelectionStart, all->Length));
            return all->Substring(s, Math::Max(0, Math::Min(SelectionLength, all->Length - s)));
        }

        bool SelectionHasHidden()
        {
            int a = SelectionStart, b = a + SelectionLength;
            for each (FoldSpan s in hidden) if (a < s.End && b > s.Start) return true;
            return false;
        }

        void CopyWhole()
        {
            if (SelectionLength == 0) return;
            if (!SelectionHasHidden()) { Copy(); return; }
            try { Clipboard::SetText(SelectionWhole()->Replace("\n", "\r\n")); }
            catch (System::Runtime::InteropServices::ExternalException^) { }   // clipboard held by another program
        }

        // ---- lines of the file (0-based) ---------------------------------------------------
        // RichEdit counts the lines it shows, and a folded block's lines are not shown: while
        // a block is folded the lines are counted in the text itself
        int SourceLineFromChar(int index)
        {
            if (hidden->Count == 0) return GetLineFromCharIndex(index);
            EnsureLines();
            int lo = 0, hi = lineStarts->Length - 1;
            while (lo < hi)
            {
                int mid = (lo + hi + 1) / 2;
                if (lineStarts[mid] <= index) lo = mid; else hi = mid - 1;
            }
            return lo;
        }

        // the first character of a line, -1 past the last one
        int SourceLineStart(int line)
        {
            if (hidden->Count == 0) return GetFirstCharIndexFromLine(line);
            EnsureLines();
            return line >= 0 && line < lineStarts->Length ? lineStarts[line] : -1;
        }

        int SourceLineCount()
        {
            if (hidden->Count == 0) return GetLineFromCharIndex(TextLength) + 1;
            EnsureLines();
            return lineStarts->Length;
        }

        int CurrentLineIndex() { return SourceLineFromChar(SelectionStart); }

        String^ LineText(int lineIndex)
        {
            int start = SourceLineStart(lineIndex);
            if (start < 0) return String::Empty;
            int next = SourceLineStart(lineIndex + 1);
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
            GuardFold();
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

        // ---- folding ---------------------------------------------------------------------
        // the { } blocks of `text` that have a line between the '{' line and the '}' line;
        // comments, strings, character literals and preprocessor lines are skipped
        void UpdateFolds(String^ text)
        {
            if (Object::ReferenceEquals(text, foldsFor)) return;
            foldsFor = text;
            foldOpen->Clear(); foldStart->Clear(); foldEnd->Clear(); foldCloseLine->Clear();
            List<int>^ starts = gcnew List<int>();
            starts->Add(0);
            for (int i = 0; i < text->Length; i++) if (text[i] == L'\n') starts->Add(i + 1);
            headFold = gcnew array<int>(starts->Count);
            for (int k = 0; k < headFold->Length; k++) headFold[k] = -1;
            guide = gcnew array<Byte>(starts->Count);
            if (text->Length > MaxFoldLength) return;

            List<int>^ opens = gcnew List<int>();
            List<int>^ closes = gcnew List<int>();
            ScanBraces(text, opens, closes);
            array<int>^ o = opens->ToArray();
            array<int>^ c = closes->ToArray();
            Array::Sort(o, c);
            array<int>^ depth = gcnew array<int>(starts->Count + 1);
            for (int k = 0; k < o->Length; k++)
            {
                int head = LineOf(starts, o[k]), close = LineOf(starts, c[k]);
                if (close < head + 2) continue;
                if (headFold[head] < 0) headFold[head] = foldOpen->Count;
                foldOpen->Add(o[k]);
                foldStart->Add(starts[head + 1] - 1);
                foldEnd->Add(starts[close] - 1);
                foldCloseLine->Add(close);
                depth[head + 1]++;
                depth[close]--;
                guide[close] = (Byte)(guide[close] | 2);
            }
            int d = 0;
            for (int k = 0; k < guide->Length; k++)
            {
                d += depth[k];
                if (d > 0) guide[k] = (Byte)(guide[k] | 1);
            }
        }

        // the comments changed: the blocks are found again on the next UpdateFolds
        void ForgetFolds() { foldsFor = nullptr; }

        // the fold whose '{' is the first foldable one on a line, or -1
        int FoldAtLine(int line) { return headFold != nullptr && line >= 0 && line < headFold->Length ? headFold[line] : -1; }
        // 1: the line lies inside a block, 2: a block's '}' is on it
        int FoldGuide(int line) { return guide != nullptr && line >= 0 && line < guide->Length ? guide[line] : 0; }
        void GetFold(int fold, int% open, int% start, int% end, int% closeLine)
        {
            open = foldOpen[fold]; start = foldStart[fold]; end = foldEnd[fold]; closeLine = foldCloseLine[fold];
        }

        property bool HasHidden { bool get() { return hidden->Count > 0; } }
        property List<FoldSpan>^ Hidden { List<FoldSpan>^ get() { return hidden; } }

        bool IsFolded(int open)
        {
            for each (FoldSpan s in hidden) if (s.Open == open) return true;
            return false;
        }

        bool IsHiddenChar(int index)
        {
            for each (FoldSpan s in hidden) if (index >= s.Start && index < s.End) return true;
            return false;
        }

        void AddHidden(int open, int start, int end)
        {
            FoldSpan s;
            s.Open = open; s.Start = start; s.End = end;
            hidden->Add(s);
            hiddenFor = CachedText;
        }

        void RemoveHidden(int open)
        {
            for (int k = hidden->Count - 1; k >= 0; k--) if (hidden[k].Open == open) hidden->RemoveAt(k);
            if (hidden->Count == 0) hiddenFor = nullptr;
        }

        // the whole text was replaced: nothing is hidden any more
        void ForgetHidden() { hidden->Clear(); hiddenFor = nullptr; }

        // After the text changed: a fold the edit reached (anywhere from its '{' to the visible
        // '\n' after its hidden run) or that is no longer a block of the new text is taken out
        // and returned in the new text's positions, to be shown again; the others move with it.
        List<FoldSpan>^ TakeStaleFolds()
        {
            List<FoldSpan>^ stale = gcnew List<FoldSpan>();
            String^ now = CachedText;
            String^ before = hiddenFor;
            hiddenFor = now;
            if (hidden->Count == 0 || before == nullptr || Object::ReferenceEquals(now, before) || String::Equals(now, before))
                return stale;
            int pre = 0;
            int maxPre = Math::Min(before->Length, now->Length);
            while (pre < maxPre && before[pre] == now[pre]) pre++;
            int suf = 0;
            while (suf < before->Length - pre && suf < now->Length - pre &&
                   before[before->Length - 1 - suf] == now[now->Length - 1 - suf]) suf++;
            int oldEnd = before->Length - suf, newEnd = now->Length - suf, delta = now->Length - before->Length;
            List<FoldSpan>^ kept = gcnew List<FoldSpan>();
            for each (FoldSpan s in hidden)
            {
                if (pre <= s.End && oldEnd >= s.Open)
                {
                    FoldSpan r = s;
                    r.Start = Math::Min(s.Start, pre);
                    r.End = s.End >= oldEnd ? s.End + delta : newEnd;
                    r.Start = Math::Max(0, Math::Min(r.Start, now->Length));
                    r.End = Math::Max(r.Start, Math::Min(r.End, now->Length));
                    stale->Add(r);
                    continue;
                }
                FoldSpan m = s;
                if (oldEnd < s.Open) { m.Open += delta; m.Start += delta; m.End += delta; }
                kept->Add(m);
            }
            hidden = kept;
            UpdateFolds(now);
            for (int k = hidden->Count - 1; k >= 0; k--)
            {
                FoldSpan s = hidden[k];
                int f = foldOpen->BinarySearch(s.Open);
                if (f < 0 || foldStart[f] != s.Start || foldEnd[f] != s.End) { stale->Add(s); hidden->RemoveAt(k); }
            }
            if (hidden->Count == 0) hiddenFor = nullptr;
            return stale;
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
            if (ShowWhitespace || hidden->Count > 0) Invalidate();
        }

        virtual void OnSelectionChanged(EventArgs^ e) override
        {
            RichTextBox::OnSelectionChanged(e);
            if (ShowWhitespace && IsHandleCreated && !Recolouring) InvalidateSelectionBand();
        }

        virtual void OnMouseDown(MouseEventArgs^ e) override
        {
            if (hidden->Count > 0 && e->Button == WF::MouseButtons::Left)
                for each (FoldSpan s in hidden)
                    if (!IsHiddenChar(s.End) && FoldMarkRect(s).Contains(e->Location))
                    {
                        FoldMarkClicked(s.Open);
                        return;
                    }
            BeforeEdit(this, EventArgs::Empty);        // a click moves the caret: the next typing is a new undo step
            RichTextBox::OnMouseDown(e);
        }

        virtual bool ProcessCmdKey(WF::Message% msg, Keys keyData) override
        {
            if (msg.Msg == 0x0100)                              // WM_KEYDOWN (Enter, Delete ... edit here)
            {
                Keys code = keyData & Keys::KeyCode;
                if (code == Keys::Return || code == Keys::Delete || code == Keys::Back || code == Keys::Tab) GuardFold();
                RememberKeySelection();
            }
            if (keyData == (Keys::Control | Keys::V) || keyData == (Keys::Shift | Keys::Insert)) { PastePlain(); return true; }
            if ((keyData == (Keys::Control | Keys::C) || keyData == (Keys::Control | Keys::Insert)) && SelectionHasHidden()) { CopyWhole(); return true; }
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
                int caretCol = SelectionStart - SourceLineStart(CurrentLineIndex());
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
                                 SourceLineFromChar(SelectionStart) != SourceLineFromChar(SelectionStart + SelectionLength);
                if (multiLine || outdent) { IndentRequested(!outdent); return true; }
                if (InsertSpaces)
                {
                    int col = SelectionStart - SourceLineStart(CurrentLineIndex());
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
            if (m.Msg == 0x000F && (ShowWhitespace || hidden->Count > 0) && IsHandleCreated)      // WM_PAINT
            {
                SD::Rectangle update = NativeMethods::GetUpdateRect(Handle);
                RichTextBox::WndProc(m);
                if (!update.IsEmpty)
                {
                    if (ShowWhitespace) DrawWhitespace(update);
                    if (hidden->Count > 0) DrawFoldMarks(update);
                }
                return;
            }
            if (m.Msg == 0x0102 || m.Msg == 0x0286)             // WM_CHAR, WM_IME_CHAR
            {
                int ch = (int)m.WParam;
                if (ch >= 0x20 || ch == 0x08 || ch == 0x09 || ch == 0x0D) GuardFold();
                RememberKeySelection();
            }
            RichTextBox::WndProc(m);
        }

    private:
        String^ cachedText;
        int cachedLength;
        float charWidth;
        int selTop, selBottom;      // pixel band of the selection last time (white space overlay)
        int keySelStart, keySelLength;
        array<int>^ lineStarts;     // where each line of linesFor begins
        String^ linesFor;
        List<FoldSpan>^ hidden;     // the folded blocks
        String^ hiddenFor;          // the text their positions are in
        String^ foldsFor;           // the text the blocks below were found in
        List<int>^ foldOpen;        // per block, in '{' order: the '{', the run it hides, its '}' line
        List<int>^ foldStart;
        List<int>^ foldEnd;
        List<int>^ foldCloseLine;
        array<int>^ headFold;       // per line: its first block, -1 for none
        array<Byte>^ guide;         // per line: FoldGuide

        void EnsureLines()
        {
            String^ all = CachedText;
            if (Object::ReferenceEquals(all, linesFor) && lineStarts != nullptr) return;
            int count = 1;
            for (int i = 0; i < all->Length; i++) if (all[i] == L'\n') count++;
            lineStarts = gcnew array<int>(count);
            int k = 1;
            for (int i = 0; i < all->Length; i++) if (all[i] == L'\n') lineStarts[k++] = i + 1;
            linesFor = all;
        }

        static int LineOf(List<int>^ starts, int index)
        {
            int lo = 0, hi = starts->Count - 1;
            while (lo < hi)
            {
                int mid = (lo + hi + 1) / 2;
                if (starts[mid] <= index) lo = mid; else hi = mid - 1;
            }
            return lo;
        }

        static bool StartsAt(String^ text, int i, String^ token)
        {
            return !String::IsNullOrEmpty(token) && String::CompareOrdinal(text, i, token, 0, token->Length) == 0;
        }

        // every matched { } pair, by position; an unmatched '}' is passed over
        void ScanBraces(String^ text, List<int>^ opens, List<int>^ closes)
        {
            Stack<int>^ open = gcnew Stack<int>();
            String^ lc = LineComment;
            int n = text->Length;
            bool lineStart = true;
            for (int i = 0; i < n; i++)
            {
                wchar_t c = text[i];
                if (c == L'\n') { lineStart = true; continue; }
                if (lineStart && (c == L' ' || c == L'\t')) continue;
                bool first = lineStart;
                lineStart = false;
                if (first && c == L'#' && lc != "#")
                {
                    // a preprocessor line, continued by a '\' at its end
                    while (i + 1 < n && (text[i + 1] != L'\n' || text[i] == L'\\')) i++;
                    continue;
                }
                if (StartsAt(text, i, BlockCommentStart))
                {
                    int end = String::IsNullOrEmpty(BlockCommentEnd) ? -1
                            : text->IndexOf(BlockCommentEnd, i + BlockCommentStart->Length, StringComparison::Ordinal);
                    i = end < 0 ? n : end + BlockCommentEnd->Length - 1;
                    continue;
                }
                if (StartsAt(text, i, lc))
                {
                    while (i + 1 < n && text[i + 1] != L'\n') i++;
                    continue;
                }
                if (c == L'"' && i > 0 && text[i - 1] == L'R')
                {
                    // R"delim( ... )delim"
                    int paren = text->IndexOf(L'(', i + 1);
                    if (paren > 0 && paren - i - 1 <= 16)
                    {
                        String^ close = ")" + text->Substring(i + 1, paren - i - 1) + "\"";
                        int end = text->IndexOf(close, paren + 1, StringComparison::Ordinal);
                        i = end < 0 ? n : end + close->Length - 1;
                        continue;
                    }
                }
                if (c == L'"' || c == L'\'')
                {
                    while (i + 1 < n && text[i + 1] != L'\n')
                    {
                        i++;
                        if (text[i] == L'\\') { if (i + 1 < n && text[i + 1] != L'\n') i++; continue; }
                        if (text[i] == c) break;
                    }
                    continue;
                }
                if (c == L'{') open->Push(i);
                else if (c == L'}' && open->Count > 0) { opens->Add(open->Pop()); closes->Add(i); }
            }
        }

        // a key is about to edit where a folded block's edge is: CodeEditor shows the block first
        void GuardFold()
        {
            if (hidden->Count == 0 || ReadOnly) return;
            int a = SelectionStart, b = a + SelectionLength;
            for each (FoldSpan s in hidden)
                if ((a >= s.Start && a <= s.End) || (b >= s.Start && b <= s.End))
                {
                    FoldEditing(s.Open);
                    return;
                }
        }

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
                    if (hidden->Count > 0 && IsHiddenChar(i)) continue;
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

        // the "..." box after the '{' line of a folded block: where its visible '\n' is
        SD::Rectangle FoldMarkRect(FoldSpan s)
        {
            SD::Point p = GetPositionFromCharIndex(s.End);
            float cw = CharWidth();
            return SD::Rectangle(p.X + (int)(cw / 2), p.Y + 1, (int)(cw * 3) + 4, this->Font->Height - 2);
        }

        void DrawFoldMarks(SD::Rectangle clip)
        {
            Graphics^ g = Graphics::FromHwnd(Handle);
            Theme^ t = ThemeManager::Current;
            Pen^ p = gcnew Pen(t->MutedText);
            try
            {
                g->SetClip(clip);
                for each (FoldSpan s in hidden)
                {
                    if (IsHiddenChar(s.End)) continue;             // inside another folded block
                    SD::Rectangle r = FoldMarkRect(s);
                    if (!r.IntersectsWith(clip)) continue;
                    g->DrawRectangle(p, r);
                    TextRenderer::DrawText(g, "...", this->Font, r, t->MutedText,
                        TextFormatFlags::HorizontalCenter | TextFormatFlags::VerticalCenter | TextFormatFlags::NoPadding);
                }
            }
            finally { delete p; delete g; }
        }
    };

    // -------------------------------------------------------------------------
    //  LineGutter - line numbers + breakpoint / bookmark / execution markers,
    //  and a column of fold boxes for the { } blocks
    // -------------------------------------------------------------------------
    public ref class LineGutter : public Control
    {
    public:
        literal int FoldColumn = 12;

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
        event LineHandler^ FoldClicked;         // the fold box of the block whose '{' is on that line

        void UpdateWidth()
        {
            int lines = box->SourceLineCount();
            int digits = Math::Max(3, lines.ToString()->Length);
            int w = 22 + FoldColumn + (ShowNumbers ? (int)(digits * box->CharWidth()) + 10 : 0);
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
            int total = box->GetLineFromCharIndex(box->TextLength) + 1;     // the lines shown
            int firstChar = box->GetCharIndexFromPosition(SD::Point(1, 1));
            int line = box->GetLineFromCharIndex(firstChar);
            int current = box->SourceLineFromChar(box->SelectionStart);
            int prevY = -lineH;
            int fx = FoldX();
            int noTick = -1;            // the '}' line of a folded block shown just above
            SolidBrush^ bpBrush = gcnew SolidBrush(t->Breakpoint);
            SolidBrush^ bmBrush = gcnew SolidBrush(t->Bookmark);
            SolidBrush^ exBrush = gcnew SolidBrush(t->Accent);
            SolidBrush^ boxBrush = gcnew SolidBrush(t->Window);
            Pen^ foldPen = gcnew Pen(t->LineNumber);

            while (line < total)
            {
                int idx = box->GetFirstCharIndexFromLine(line);
                if (idx < 0) break;
                int y = box->GetPositionFromCharIndex(idx).Y;
                if (idx >= box->TextLength && line > 0) y = prevY + lineH;   // empty last line
                if (y > Height) break;
                prevY = y;
                int src = box->SourceLineFromChar(idx);
                int n = src + 1;

                if (ShowNumbers)
                {
                    SD::Rectangle r = SD::Rectangle(18, y, Width - 24 - FoldColumn, lineH);
                    TextRenderer::DrawText(g, n.ToString(), box->Font, r, src == current ? t->Text : t->LineNumber,
                        TextFormatFlags::Right | TextFormatFlags::Top | TextFormatFlags::NoPadding);
                }
                int cy = y + lineH / 2;
                if (Breakpoints->Contains(n)) g->FillEllipse(bpBrush, 3, cy - 6, 12, 12);
                if (Bookmarks->Contains(n)) g->FillRectangle(bmBrush, 1, cy - 5, 4, 10);
                if (ExecLine == n)
                {
                    g->FillPolygon(exBrush, gcnew array<SD::Point>{ SD::Point(4, cy - 6), SD::Point(14, cy), SD::Point(4, cy + 6) });
                }

                int guide = box->FoldGuide(src);
                if ((guide & 1) != 0) g->DrawLine(foldPen, fx, y, fx, y + lineH);
                if ((guide & 2) != 0 && src != noTick)
                {
                    g->DrawLine(foldPen, fx, y, fx, cy);
                    g->DrawLine(foldPen, fx, cy, fx + 4, cy);
                }
                int fold = box->FoldAtLine(src);
                if (fold >= 0)
                {
                    int open, start, end, closeLine;
                    box->GetFold(fold, open, start, end, closeLine);
                    bool folded = box->IsFolded(open);
                    if (folded) noTick = closeLine;
                    else g->DrawLine(foldPen, fx, cy + 4, fx, y + lineH);
                    g->FillRectangle(boxBrush, fx - 4, cy - 4, 8, 8);
                    g->DrawRectangle(foldPen, fx - 4, cy - 4, 8, 8);
                    g->DrawLine(foldPen, fx - 2, cy, fx + 2, cy);
                    if (folded) g->DrawLine(foldPen, fx, cy - 2, fx, cy + 2);
                }
                line++;
            }
            delete bpBrush; delete bmBrush; delete exBrush; delete boxBrush; delete foldPen;
            Pen^ sep = gcnew Pen(t->Border);
            g->DrawLine(sep, Width - 3, 0, Width - 3, Height);
            delete sep;
        }

        virtual void OnMouseDown(MouseEventArgs^ e) override
        {
            Control::OnMouseDown(e);
            int idx = box->GetCharIndexFromPosition(SD::Point(1, e->Y));
            int src = box->SourceLineFromChar(idx);
            if (e->X >= FoldX() - FoldColumn / 2)
            {
                if (box->FoldAtLine(src) >= 0) FoldClicked(src + 1);
                return;
            }
            LineClicked(src + 1);
        }

    private:
        CodeTextBox^ box;

        int FoldX() { return Width - 4 - FoldColumn / 2; }
    };
}}
