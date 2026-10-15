#pragma once
// ============================================================================
//  GRIDE 2.0 - Code editor control (Phase 5)
//  RichTextBox + line-number gutter, syntax colouring of the visible region,
//  own delta-based undo/redo (colouring never pollutes it), plain-text
//  clipboard, smart Tab / Shift+Tab, auto-indent, word delete, context menu,
//  find / replace / go to line, change-on-disk detection support.
// ============================================================================
#include "Syntax.h"

using namespace System::Drawing;
using namespace System::Windows::Forms;

namespace Gride
{
    // One undo step: at Start, Removed was replaced by Inserted.
    public ref class EditDelta
    {
    public:
        int Start;
        String^ Removed;
        String^ Inserted;
        int CaretBefore;
        int CaretAfter;
    };

    public ref class GutterPanel : public Panel
    {
    public:
        GutterPanel()
        {
            SetStyle(ControlStyles::AllPaintingInWmPaint | ControlStyles::OptimizedDoubleBuffer |
                     ControlStyles::UserPaint | ControlStyles::ResizeRedraw, true);
        }
    };

    public ref class EditorTextBox : public RichTextBox
    {
    public:
        event EventHandler^ Scrolled;
        Func<Keys, bool>^ CommandHandler;

    protected:
        virtual void OnHandleCreated(EventArgs^ e) override
        {
            RichTextBox::OnHandleCreated(e);
            // Our own undo is used; stop RichEdit recording every colour change.
            NativeMethods::SendMessage(Handle, NativeMethods::EM_SETUNDOLIMIT, IntPtr::Zero, IntPtr::Zero);
        }

        virtual bool ProcessCmdKey(Message% msg, Keys keyData) override
        {
            if (CommandHandler != nullptr && CommandHandler->Invoke(keyData)) return true;
            return RichTextBox::ProcessCmdKey(msg, keyData);
        }

        virtual void WndProc(Message% m) override
        {
            RichTextBox::WndProc(m);
            if (m.Msg == NativeMethods::WM_VSCROLL || m.Msg == NativeMethods::WM_HSCROLL ||
                m.Msg == NativeMethods::WM_MOUSEWHEEL)
                Scrolled(this, EventArgs::Empty);
        }
    };

    public ref class CodeEditor : public UserControl
    {
    public:
        event EventHandler^ DirtyChanged;
        event EventHandler^ CaretChanged;

        literal int MaxHighlightLength = 4 * 1024 * 1024;   // skip colouring of giant files
        literal int MaxUndo = 500;

        CodeEditor(IdeSettings^ s)
        {
            settings = s;
            undoStack = gcnew List<EditDelta^>();
            redoStack = gcnew List<EditDelta^>();
            encoding = gcnew UTF8Encoding(false);
            crlf = true;
            savedText = String::Empty;
            committedText = String::Empty;
            mode = EditorMode::PlainText;
            untitledName = "Untitled";
            gutterDigits = 0;
            lastFirstVisible = -1;
            diskTime = DateTime::MinValue;

            box = gcnew EditorTextBox();
            box->Dock = DockStyle::Fill;
            box->BorderStyle = System::Windows::Forms::BorderStyle::None;
            box->AcceptsTab = true;
            box->WordWrap = false;
            box->DetectUrls = false;
            box->HideSelection = false;
            box->ScrollBars = RichTextBoxScrollBars::Both;
            box->ShortcutsEnabled = false;      // we handle every shortcut (keeps rich formatting out)
            box->EnableAutoDragDrop = false;
            box->MaxLength = Int32::MaxValue;
            box->CommandHandler = gcnew Func<Keys, bool>(this, &CodeEditor::HandleKey);
            box->TextChanged += gcnew EventHandler(this, &CodeEditor::OnBoxTextChanged);
            box->SelectionChanged += gcnew EventHandler(this, &CodeEditor::OnBoxSelectionChanged);
            box->Scrolled += gcnew EventHandler(this, &CodeEditor::OnBoxScrolled);
            box->VScroll += gcnew EventHandler(this, &CodeEditor::OnBoxScrolled);
            box->Resize += gcnew EventHandler(this, &CodeEditor::OnBoxScrolled);
            BuildContextMenu();

            gutter = gcnew GutterPanel();
            gutter->Dock = DockStyle::Left;
            gutter->Width = 44;
            gutter->Paint += gcnew PaintEventHandler(this, &CodeEditor::OnGutterPaint);
            gutter->MouseDown += gcnew MouseEventHandler(this, &CodeEditor::OnGutterMouseDown);

            // Fill control first, Left control second (docking runs in reverse order).
            Controls->Add(box);
            Controls->Add(gutter);

            highlightTimer = gcnew System::Windows::Forms::Timer();
            highlightTimer->Interval = 60;
            highlightTimer->Tick += gcnew EventHandler(this, &CodeEditor::OnHighlightTick);

            burstTimer = gcnew System::Windows::Forms::Timer();
            burstTimer->Interval = 700;
            burstTimer->Tick += gcnew EventHandler(this, &CodeEditor::OnBurstTick);

            ApplySettings(s);
        }

        // ------------------------------------------------------------ properties
        property String^ FilePath
        {
            String^ get() { return filePath; }
            void set(String^ v) { filePath = v; RememberDiskTime(); }
        }

        property String^ UntitledName
        {
            String^ get() { return untitledName; }
            void set(String^ v) { untitledName = v; }
        }

        property String^ DisplayName
        {
            String^ get() { return String::IsNullOrEmpty(filePath) ? untitledName : Path::GetFileName(filePath); }
        }

        property bool IsDirty { bool get() { return dirty; } }

        property bool IsReadOnly
        {
            bool get() { return box->ReadOnly; }
            void set(bool v)
            {
                box->ReadOnly = v;
                // RichTextBox greys itself when read-only; keep the theme, tint slightly.
                box->BackColor = v ? Color::FromArgb(248, 248, 248) : background;
            }
        }

        property EditorMode Mode
        {
            EditorMode get() { return mode; }
            void set(EditorMode m)
            {
                mode = m;
                highlighter = gcnew SyntaxHighlighter(m, settings);
                FullRecolor();
            }
        }

        property int CurrentLine   { int get() { return box->GetLineFromCharIndex(box->SelectionStart) + 1; } }
        property int CurrentColumn
        {
            int get()
            {
                int line = box->GetLineFromCharIndex(box->SelectionStart);
                int start = box->GetFirstCharIndexFromLine(line);
                return box->SelectionStart - (start < 0 ? 0 : start) + 1;
            }
        }
        property String^ EncodingLabel { String^ get() { return TextFile::EncodingLabel(encoding); } }
        property bool UsesCrLf { bool get() { return crlf; } }
        property String^ SelectedText { String^ get() { return box->SelectedText; } }
        property bool CanUndo { bool get() { return !box->ReadOnly && (inBurst || undoStack->Count > 0); } }
        property bool CanRedo { bool get() { return !box->ReadOnly && redoStack->Count > 0; } }

        // true when the file on disk is newer than what we loaded / saved
        bool ChangedOnDisk()
        {
            if (String::IsNullOrEmpty(filePath) || diskTime == DateTime::MinValue) return false;
            try { return File::Exists(filePath) && File::GetLastWriteTimeUtc(filePath) != diskTime; }
            catch (Exception^) { return false; }
        }

        bool DeletedOnDisk() { return !String::IsNullOrEmpty(filePath) && diskTime != DateTime::MinValue && !File::Exists(filePath); }

        void RememberDiskTime()
        {
            try { diskTime = (!String::IsNullOrEmpty(filePath) && File::Exists(filePath)) ? File::GetLastWriteTimeUtc(filePath) : DateTime::MinValue; }
            catch (Exception^) { diskTime = DateTime::MinValue; }
        }

        // ------------------------------------------------------------ settings
        void ApplySettings(IdeSettings^ s)
        {
            settings = s;
            System::Drawing::Font^ f;
            try { f = gcnew System::Drawing::Font(s->EditorFontName, s->EditorFontSize); }
            catch (Exception^) { f = gcnew System::Drawing::Font(FontFamily::GenericMonospace, 10.0f); }

            palette = gcnew array<Color>(7);
            palette[(int)TokenKind::Text]         = ParseColor(s->ColorText, Color::Black);
            palette[(int)TokenKind::Keyword]      = ParseColor(s->ColorKeyword, Color::Blue);
            palette[(int)TokenKind::Type]         = ParseColor(s->ColorType, Color::Teal);
            palette[(int)TokenKind::Comment]      = ParseColor(s->ColorComment, Color::Green);
            palette[(int)TokenKind::StringLit]    = ParseColor(s->ColorString, Color::Brown);
            palette[(int)TokenKind::Number]       = ParseColor(s->ColorNumber, Color::DarkGreen);
            palette[(int)TokenKind::Preprocessor] = ParseColor(s->ColorPreprocessor, Color::Gray);
            background = ParseColor(s->ColorBackground, Color::White);

            System::Drawing::Font^ old = editorFont;
            editorFont = f;
            box->Font = f;
            gutter->Font = f;
            if (old != nullptr) delete old;               // free the previous GDI font
            box->BackColor = box->ReadOnly ? Color::FromArgb(248, 248, 248) : background;
            box->ForeColor = palette[0];
            gutter->BackColor = Color::FromArgb(240, 240, 240);
            gutter->Visible = s->ShowLineNumbers;
            gutterDigits = 0;
            UpdateGutterWidth();

            highlighter = gcnew SyntaxHighlighter(mode, settings);
            FullRecolor();
        }

        // ------------------------------------------------------------ file I/O
        void LoadFile(String^ path)
        {
            String^ full = Path::GetFullPath(path);
            Encoding^ enc = nullptr;
            bool isCrLf = true;
            String^ t = TextFile::Read(full, enc, isCrLf);

            suppress = true;
            box->Text = t->Replace("\r\n", "\n")->Replace("\r", "\n");
            suppress = false;

            encoding = enc;
            crlf = isCrLf;
            filePath = full;
            RememberDiskTime();
            savedText = box->Text;
            committedText = box->Text;
            undoStack->Clear();
            redoStack->Clear();
            inBurst = false;
            box->Select(0, 0);
            committedCaret = 0;
            SetDirty(false);
            gutterDigits = 0;
            UpdateGutterWidth();
            FullRecolor();
        }

        // Reload after an outside change, keeping the caret line. Undo history is cleared.
        void ReloadFromDisk()
        {
            if (String::IsNullOrEmpty(filePath)) return;
            int line = CurrentLine;
            LoadFile(filePath);
            GoToLine(Math::Min(line, LineCount()));
        }

        void SaveFile(String^ path)
        {
            burstTimer->Stop();
            CommitBurst();
            String^ full = Path::GetFullPath(path);
            TextFile::Write(full, box->Text, encoding, crlf);
            filePath = full;
            RememberDiskTime();
            savedText = box->Text;
            SetDirty(false);
        }

        // ------------------------------------------------------------ editing commands
        void Undo()
        {
            if (box->ReadOnly) return;
            burstTimer->Stop();
            CommitBurst();
            if (undoStack->Count == 0) return;
            EditDelta^ d = undoStack[undoStack->Count - 1];
            undoStack->RemoveAt(undoStack->Count - 1);
            ApplyDelta(d->Start, d->Inserted->Length, d->Removed, d->CaretBefore);
            redoStack->Add(d);
        }

        void Redo()
        {
            if (box->ReadOnly) return;
            burstTimer->Stop();
            CommitBurst();
            if (redoStack->Count == 0) return;
            EditDelta^ d = redoStack[redoStack->Count - 1];
            redoStack->RemoveAt(redoStack->Count - 1);
            ApplyDelta(d->Start, d->Removed->Length, d->Inserted, d->CaretAfter);
            undoStack->Add(d);
        }

        void Copy()
        {
            if (box->SelectionLength == 0) return;
            try { Clipboard::SetText(box->SelectedText->Replace("\n", "\r\n")); }
            catch (ExternalException^) {}
        }

        void Cut()
        {
            if (box->SelectionLength == 0) return;
            Copy();
            if (box->ReadOnly) return;
            AtomicEdit(String::Empty);
        }

        void Paste()
        {
            if (box->ReadOnly) return;
            try
            {
                if (!Clipboard::ContainsText()) return;
                String^ t = Clipboard::GetText()->Replace("\r\n", "\n")->Replace("\r", "\n");
                AtomicEdit(t);
            }
            catch (ExternalException^) {}
        }

        void SelectAll() { box->SelectAll(); }
        void FocusEditor() { box->Focus(); }

        void GoToLine(int line1) { GoToLineColumn(line1, 1); }

        void GoToLineColumn(int line1, int col1)
        {
            int idx = box->GetFirstCharIndexFromLine(Math::Max(0, line1 - 1));
            if (idx < 0) idx = box->TextLength;
            int next = box->GetFirstCharIndexFromLine(Math::Max(0, line1));
            int lineEnd = next < 0 ? box->TextLength : next - 1;
            idx = Math::Min(idx + Math::Max(0, col1 - 1), Math::Max(idx, lineEnd));
            box->Select(idx, 0);
            box->ScrollToCaret();
            box->Focus();
        }

        void SelectLine(int line1)
        {
            int idx = box->GetFirstCharIndexFromLine(Math::Max(0, line1 - 1));
            if (idx < 0) { GoToLine(line1); return; }
            int next = box->GetFirstCharIndexFromLine(line1);
            int end = next < 0 ? box->TextLength : next - 1;
            box->Select(idx, Math::Max(0, end - idx));
            box->ScrollToCaret();
            box->Focus();
        }

        int LineCount() { return box->GetLineFromCharIndex(box->TextLength) + 1; }

        String^ GetAllText() { return box->Text; }

        bool FindNext(String^ what, bool matchCase, bool wholeWord, bool up)
        {
            if (String::IsNullOrEmpty(what) || box->TextLength == 0) return false;
            RichTextBoxFinds opts = RichTextBoxFinds::None;
            if (matchCase) opts = opts | RichTextBoxFinds::MatchCase;
            if (wholeWord) opts = opts | RichTextBoxFinds::WholeWord;
            int idx = -1;
            if (!up)
            {
                int from = box->SelectionStart + box->SelectionLength;
                if (from < box->TextLength) idx = box->Find(what, from, box->TextLength, opts);
                if (idx < 0) idx = box->Find(what, 0, box->TextLength, opts);        // wrap around
            }
            else
            {
                opts = opts | RichTextBoxFinds::Reverse;
                int to = box->SelectionStart;
                if (to > 0) idx = box->Find(what, 0, to, opts);
                if (idx < 0) idx = box->Find(what, 0, box->TextLength, opts);
            }
            if (idx >= 0)
            {
                box->Select(idx, what->Length);
                box->ScrollToCaret();
            }
            return idx >= 0;
        }

        bool ReplaceCurrent(String^ what, String^ with, bool matchCase, bool wholeWord, bool up)
        {
            if (box->ReadOnly) return false;
            StringComparison cmp = matchCase ? StringComparison::Ordinal : StringComparison::OrdinalIgnoreCase;
            if (box->SelectionLength > 0 && String::Equals(box->SelectedText, what, cmp))
                AtomicEdit(with == nullptr ? String::Empty : with->Replace("\r\n", "\n"));
            return FindNext(what, matchCase, wholeWord, up);
        }

        int ReplaceAll(String^ what, String^ with, bool matchCase, bool wholeWord)
        {
            if (box->ReadOnly || String::IsNullOrEmpty(what)) return 0;
            String^ pat = Regex::Escape(what);
            if (wholeWord) pat = String::Concat("\\b", pat, "\\b");
            RegexOptions o = RegexOptions::CultureInvariant;
            if (!matchCase) o = o | RegexOptions::IgnoreCase;
            Regex^ r = gcnew Regex(pat, o);
            String^ text = box->Text;
            int count = r->Matches(text)->Count;
            if (count == 0) return 0;
            String^ repl = with == nullptr ? String::Empty : with->Replace("\r\n", "\n")->Replace("$", "$$");
            ReplaceWholeText(r->Replace(text, repl));
            return count;
        }

    protected:
        ~CodeEditor()
        {
            if (highlightTimer != nullptr) { highlightTimer->Stop(); delete highlightTimer; highlightTimer = nullptr; }
            if (burstTimer != nullptr)     { burstTimer->Stop(); delete burstTimer; burstTimer = nullptr; }
            // editorFont is left to the GC: child controls still reference it during base disposal.
        }

    private:
        IdeSettings^ settings;
        EditorTextBox^ box;
        GutterPanel^ gutter;
        System::Windows::Forms::Timer^ highlightTimer;
        System::Windows::Forms::Timer^ burstTimer;
        SyntaxHighlighter^ highlighter;
        EditorMode mode;
        array<Color>^ palette;
        Color background;
        System::Drawing::Font^ editorFont;

        String^ filePath;
        String^ untitledName;
        Encoding^ encoding;
        bool crlf;
        String^ savedText;
        bool dirty;
        bool suppress;          // ignore TextChanged caused by our own Text assignments
        bool colouring;         // ignore Selection/Scroll events caused by colouring
        DateTime diskTime;

        List<EditDelta^>^ undoStack;
        List<EditDelta^>^ redoStack;
        String^ committedText;  // document text at the end of the last edit burst
        int committedCaret;
        bool inBurst;
        int gutterDigits;
        int lastFirstVisible;
        bool needFullRecolor;   // colouring requested before the window handle existed

        // ------------------------------------------------------------ undo machinery
        void PushUndo(EditDelta^ d)
        {
            undoStack->Add(d);
            if (undoStack->Count > MaxUndo) undoStack->RemoveRange(0, undoStack->Count - MaxUndo);
        }

        // Closes the current typing burst and records it as one delta.
        void CommitBurst()
        {
            if (!inBurst) return;
            inBurst = false;
            String^ now = box->Text;
            String^ before = committedText;
            int pre = 0;
            int maxPre = Math::Min(before->Length, now->Length);
            while (pre < maxPre && before[pre] == now[pre]) pre++;
            int suf = 0;
            while (suf < before->Length - pre && suf < now->Length - pre &&
                   before[before->Length - 1 - suf] == now[now->Length - 1 - suf]) suf++;
            if (pre != before->Length || pre != now->Length)
            {
                EditDelta^ d = gcnew EditDelta();
                d->Start = pre;
                d->Removed = before->Substring(pre, before->Length - pre - suf);
                d->Inserted = now->Substring(pre, now->Length - pre - suf);
                d->CaretBefore = committedCaret;
                d->CaretAfter = box->SelectionStart;
                PushUndo(d);
            }
            committedText = now;
            committedCaret = box->SelectionStart;
            SetDirty(!String::Equals(now, savedText));
        }

        // Replace [start, start+length) with text, without recording undo.
        void ApplyDelta(int start, int length, String^ text, int caret)
        {
            suppress = true;
            try
            {
                box->Select(start, length);
                box->SelectedText = text;
            }
            finally { suppress = false; }
            committedText = box->Text;
            box->Select(Math::Min(Math::Max(0, caret), box->TextLength), 0);
            committedCaret = box->SelectionStart;
            box->ScrollToCaret();
            SetDirty(!String::Equals(committedText, savedText));
            UpdateGutterWidth();
            gutter->Invalidate();
            ScheduleHighlight();
        }

        // Replace the selection as one undo step.
        void AtomicEdit(String^ newSelectedText)
        {
            burstTimer->Stop();
            CommitBurst();
            box->SelectedText = newSelectedText;     // fires TextChanged -> opens a burst
            burstTimer->Stop();
            CommitBurst();
        }

        void ReplaceWholeText(String^ newText)
        {
            burstTimer->Stop();
            CommitBurst();
            int caret = box->SelectionStart;
            box->Text = newText;                     // TextChanged opens a burst
            burstTimer->Stop();
            CommitBurst();
            box->Select(Math::Min(caret, box->TextLength), 0);
            box->ScrollToCaret();
            FullRecolor();
        }

        void SetDirty(bool d)
        {
            if (d == dirty) return;
            dirty = d;
            DirtyChanged(this, EventArgs::Empty);
        }

        // ------------------------------------------------------------ events
        void OnBoxTextChanged(Object^ sender, EventArgs^ e)
        {
            if (suppress || colouring) return;
            if (!inBurst)
            {
                redoStack->Clear();
                inBurst = true;
            }
            burstTimer->Stop();
            burstTimer->Start();
            SetDirty(true);
            UpdateGutterWidth();
            ScheduleHighlight();
            gutter->Invalidate();
        }

        void OnBurstTick(Object^ sender, EventArgs^ e)
        {
            burstTimer->Stop();
            CommitBurst();
        }

        void OnBoxSelectionChanged(Object^ sender, EventArgs^ e)
        {
            if (colouring) return;
            if (!inBurst && !suppress) committedCaret = box->SelectionStart;   // caret Undo returns to
            gutter->Invalidate();
            // Re-colour only when the caret move scrolled the view (keyboard paging).
            int first = box->IsHandleCreated
                ? NativeMethods::SendMessage(box->Handle, NativeMethods::EM_GETFIRSTVISIBLELINE, IntPtr::Zero, IntPtr::Zero).ToInt32()
                : 0;
            if (first != lastFirstVisible) { lastFirstVisible = first; ScheduleHighlight(); }
            CaretChanged(this, EventArgs::Empty);
        }

        void OnBoxScrolled(Object^ sender, EventArgs^ e)
        {
            if (colouring) return;
            gutter->Invalidate();
            ScheduleHighlight();
        }

        void OnHighlightTick(Object^ sender, EventArgs^ e)
        {
            highlightTimer->Stop();
            // Never move the selection while the user is selecting (mouse drag or a live
            // keyboard selection): restoring it would lose its direction. Try again later.
            if (Control::MouseButtons != System::Windows::Forms::MouseButtons::None ||
                (box->SelectionLength > 0 && (Control::ModifierKeys & Keys::Shift) == Keys::Shift))
            {
                highlightTimer->Start();
                return;
            }
            if (needFullRecolor && box->IsHandleCreated) FullRecolor();   // first show after load
            else HighlightVisible();
        }

        void OnGutterMouseDown(Object^ sender, MouseEventArgs^ e)
        {
            int idx = box->GetCharIndexFromPosition(Point(2, e->Y));
            SelectLine(box->GetLineFromCharIndex(idx) + 1);
        }

        // ------------------------------------------------------------ context menu
        void BuildContextMenu()
        {
            System::Windows::Forms::ContextMenuStrip^ cm = gcnew System::Windows::Forms::ContextMenuStrip();
            cmUndo = AddMenu(cm, "Undo", "Ctrl+Z", gcnew EventHandler(this, &CodeEditor::OnMenuUndo));
            cmRedo = AddMenu(cm, "Redo", "Ctrl+Y", gcnew EventHandler(this, &CodeEditor::OnMenuRedo));
            cm->Items->Add(gcnew ToolStripSeparator());
            cmCut = AddMenu(cm, "Cut", "Ctrl+X", gcnew EventHandler(this, &CodeEditor::OnMenuCut));
            cmCopy = AddMenu(cm, "Copy", "Ctrl+C", gcnew EventHandler(this, &CodeEditor::OnMenuCopy));
            cmPaste = AddMenu(cm, "Paste", "Ctrl+V", gcnew EventHandler(this, &CodeEditor::OnMenuPaste));
            cm->Items->Add(gcnew ToolStripSeparator());
            AddMenu(cm, "Select All", "Ctrl+A", gcnew EventHandler(this, &CodeEditor::OnMenuSelectAll));
            cm->Opening += gcnew System::ComponentModel::CancelEventHandler(this, &CodeEditor::OnMenuOpening);
            box->ContextMenuStrip = cm;
        }

        static ToolStripMenuItem^ AddMenu(System::Windows::Forms::ContextMenuStrip^ cm, String^ text, String^ keys, EventHandler^ h)
        {
            ToolStripMenuItem^ mi = gcnew ToolStripMenuItem(text, nullptr, h);
            mi->ShortcutKeyDisplayString = keys;
            cm->Items->Add(mi);
            return mi;
        }

        ToolStripMenuItem^ cmUndo;
        ToolStripMenuItem^ cmRedo;
        ToolStripMenuItem^ cmCut;
        ToolStripMenuItem^ cmCopy;
        ToolStripMenuItem^ cmPaste;

        void OnMenuOpening(Object^ sender, System::ComponentModel::CancelEventArgs^ e)
        {
            bool sel = box->SelectionLength > 0;
            cmUndo->Enabled = CanUndo;
            cmRedo->Enabled = CanRedo;
            cmCut->Enabled = sel && !box->ReadOnly;
            cmCopy->Enabled = sel;
            bool hasText = false;
            try { hasText = Clipboard::ContainsText(); } catch (ExternalException^) {}
            cmPaste->Enabled = !box->ReadOnly && hasText;
        }

        void OnMenuUndo(Object^ s, EventArgs^ e) { Undo(); }
        void OnMenuRedo(Object^ s, EventArgs^ e) { Redo(); }
        void OnMenuCut(Object^ s, EventArgs^ e) { Cut(); }
        void OnMenuCopy(Object^ s, EventArgs^ e) { Copy(); }
        void OnMenuPaste(Object^ s, EventArgs^ e) { Paste(); }
        void OnMenuSelectAll(Object^ s, EventArgs^ e) { SelectAll(); }

        // ------------------------------------------------------------ key handling
        bool HandleKey(Keys k)
        {
            if (k == (Keys::Control | Keys::Z)) { Undo(); return true; }
            if (k == (Keys::Control | Keys::Y) || k == (Keys::Control | Keys::Shift | Keys::Z)) { Redo(); return true; }
            if (k == (Keys::Control | Keys::X) || k == (Keys::Shift | Keys::Delete)) { Cut(); return true; }
            if (k == (Keys::Control | Keys::C) || k == (Keys::Control | Keys::Insert)) { Copy(); return true; }
            if (k == (Keys::Control | Keys::V) || k == (Keys::Shift | Keys::Insert)) { Paste(); return true; }
            if (k == (Keys::Control | Keys::A)) { box->SelectAll(); return true; }
            if (box->ReadOnly) return false;
            if (k == (Keys::Control | Keys::Back)) { DeleteWord(true); return true; }
            if (k == (Keys::Control | Keys::Delete)) { DeleteWord(false); return true; }
            if (k == Keys::Tab) return IndentOrTab(false);
            if (k == (Keys::Shift | Keys::Tab)) return IndentOrTab(true);
            if (k == Keys::Enter && settings->AutoIndent) { NewLineWithIndent(); return true; }
            return false;
        }

        static int CharClass(wchar_t c)
        {
            if (c == L'\n') return 0;
            if (Char::IsWhiteSpace(c)) return 1;
            if (Char::IsLetterOrDigit(c) || c == L'_') return 2;
            return 3;
        }

        // Ctrl+Backspace / Ctrl+Delete: delete to the previous / next word boundary.
        void DeleteWord(bool backwards)
        {
            if (box->SelectionLength > 0) { AtomicEdit(String::Empty); return; }
            String^ t = box->Text;
            int caret = box->SelectionStart;
            int p = caret;
            if (backwards)
            {
                if (p == 0) return;
                while (p > 0 && CharClass(t[p - 1]) == 1) p--;
                if (p > 0)
                {
                    int cls = CharClass(t[p - 1]);
                    if (cls == 0) p--;
                    else while (p > 0 && CharClass(t[p - 1]) == cls) p--;
                }
                box->Select(p, caret - p);
            }
            else
            {
                if (p >= t->Length) return;
                int cls = CharClass(t[p]);
                if (cls == 0) p++;
                else while (p < t->Length && CharClass(t[p]) == cls) p++;
                while (p < t->Length && CharClass(t[p]) == 1) p++;
                box->Select(caret, p - caret);
            }
            AtomicEdit(String::Empty);
        }

        String^ IndentUnit()
        {
            return settings->InsertSpaces ? gcnew String(L' ', settings->TabSize) : Str("\t");
        }

        bool IndentOrTab(bool outdent)
        {
            int selStart = box->SelectionStart;
            int selLen = box->SelectionLength;
            int l1 = box->GetLineFromCharIndex(selStart);
            int l2 = box->GetLineFromCharIndex(selStart + selLen);

            if (!outdent && (selLen == 0 || l1 == l2))
            {
                if (settings->InsertSpaces)
                {
                    int lineStart = box->GetFirstCharIndexFromLine(l1);
                    int col = selStart - lineStart;
                    int n = settings->TabSize - (col % settings->TabSize);
                    AtomicEdit(gcnew String(L' ', n));
                }
                else AtomicEdit("\t");
                return true;
            }

            // Block indent / outdent of whole lines
            if (selLen > 0 && l2 > l1 && selStart + selLen == box->GetFirstCharIndexFromLine(l2)) l2--;
            int blockStart = box->GetFirstCharIndexFromLine(l1);
            int nextLine = box->GetFirstCharIndexFromLine(l2 + 1);
            int blockEnd = nextLine < 0 ? box->TextLength : nextLine - 1;   // exclude final '\n'
            if (blockEnd < blockStart) blockEnd = blockStart;

            String^ block = box->Text->Substring(blockStart, blockEnd - blockStart);
            array<String^>^ lines = block->Split(L'\n');
            String^ unit = IndentUnit();
            for (int i = 0; i < lines->Length; i++)
            {
                if (!outdent)
                {
                    if (lines[i]->Length > 0) lines[i] = String::Concat(unit, lines[i]);
                }
                else if (lines[i]->StartsWith("\t"))
                {
                    lines[i] = lines[i]->Substring(1);
                }
                else
                {
                    int n = 0;
                    while (n < settings->TabSize && n < lines[i]->Length && lines[i][n] == L' ') n++;
                    lines[i] = lines[i]->Substring(n);
                }
            }
            String^ nb = String::Join("\n", lines);
            box->Select(blockStart, blockEnd - blockStart);
            AtomicEdit(nb);
            box->Select(blockStart, nb->Length);
            return true;
        }

        void NewLineWithIndent()
        {
            String^ text = box->Text;
            int caret = box->SelectionStart;
            int line = box->GetLineFromCharIndex(caret);
            int ls = box->GetFirstCharIndexFromLine(line);
            if (ls < 0) ls = 0;
            int i = ls;
            while (i < text->Length && i < caret && (text[i] == L' ' || text[i] == L'\t')) i++;
            String^ indent = text->Substring(ls, i - ls);

            int p = caret - 1;
            while (p >= ls && (text[p] == L' ' || text[p] == L'\t')) p--;
            if (p >= ls && text[p] == L'{' && LanguageDefs::IsCLike(mode)) indent = String::Concat(indent, IndentUnit());

            AtomicEdit(String::Concat("\n", indent));
            box->ScrollToCaret();
        }

        // ------------------------------------------------------------ colouring
        static Color ParseColor(String^ html, Color fallback)
        {
            try
            {
                if (String::IsNullOrWhiteSpace(html)) return fallback;
                return ColorTranslator::FromHtml(html->Trim());
            }
            catch (Exception^) { return fallback; }
        }

        void ScheduleHighlight()
        {
            if (highlightTimer == nullptr) return;
            highlightTimer->Stop();
            highlightTimer->Start();
        }

        void Freeze(bool frozen)
        {
            if (!box->IsHandleCreated) return;
            NativeMethods::SendMessage(box->Handle, NativeMethods::WM_SETREDRAW, IntPtr(frozen ? 0 : 1), IntPtr::Zero);
            if (!frozen) box->Invalidate();
        }

        int FirstVisibleLine()
        {
            return NativeMethods::SendMessage(box->Handle, NativeMethods::EM_GETFIRSTVISIBLELINE, IntPtr::Zero, IntPtr::Zero).ToInt32();
        }

        // EM_GETSCROLLPOS is 16-bit limited (jumps in files > ~4000 lines), so the
        // vertical position is restored by line with EM_LINESCROLL; X comes from SCROLLPOS.
        void SaveView(IntPtr scrollBuf, int% firstLine)
        {
            NativeMethods::SendMessage(box->Handle, NativeMethods::EM_GETSCROLLPOS, IntPtr::Zero, scrollBuf);
            firstLine = FirstVisibleLine();
        }

        void RestoreView(IntPtr scrollBuf, int firstLine)
        {
            Marshal::WriteInt32(scrollBuf, 4, 0);          // y = 0: vertical handled below
            NativeMethods::SendMessage(box->Handle, NativeMethods::EM_SETSCROLLPOS, IntPtr::Zero, scrollBuf);
            int now = FirstVisibleLine();
            if (now != firstLine)
                NativeMethods::SendMessage(box->Handle, NativeMethods::EM_LINESCROLL, IntPtr::Zero, IntPtr(firstLine - now));
        }

        // Tab stops that follow the TabSize setting (RichEdit allows 32 stops).
        void ApplyTabStops()
        {
            int charWidth = TextRenderer::MeasureText("MMMMMMMMMM", box->Font).Width / 10;
            if (charWidth <= 0) charWidth = 8;
            array<int>^ tabs = gcnew array<int>(32);
            for (int i = 0; i < 32; i++) tabs[i] = (i + 1) * settings->TabSize * charWidth;
            box->SelectionTabs = tabs;
        }

        // Reset everything to the default colour and tab stops, then colour what is visible.
        void FullRecolor()
        {
            if (!box->IsHandleCreated) { needFullRecolor = true; ScheduleHighlight(); return; }
            needFullRecolor = false;
            if (box->TextLength > MaxHighlightLength) return;
            int selStart = box->SelectionStart, selLen = box->SelectionLength;
            IntPtr scroll = Marshal::AllocHGlobal(8);
            int firstLine = 0;
            try
            {
                colouring = true;
                SaveView(scroll, firstLine);
                Freeze(true);
                box->SelectAll();
                box->SelectionColor = palette[0];
                ApplyTabStops();
                box->Select(selStart, selLen);
                RestoreView(scroll, firstLine);
            }
            finally
            {
                Freeze(false);
                colouring = false;
                Marshal::FreeHGlobal(scroll);
            }
            HighlightVisible();
        }

        void HighlightVisible()
        {
            if (highlighter == nullptr || !highlighter->Enabled) return;
            if (box->TextLength == 0 || box->TextLength > MaxHighlightLength) return;
            if (!box->IsHandleCreated) return;

            String^ text = box->Text;
            int firstChar = box->GetCharIndexFromPosition(Point(1, 1));
            int lastChar = box->GetCharIndexFromPosition(Point(1, Math::Max(1, box->ClientSize.Height - 1)));
            int firstLine = Math::Max(0, box->GetLineFromCharIndex(firstChar) - 1);
            int lastLine = box->GetLineFromCharIndex(lastChar) + 2;

            int visStart = box->GetFirstCharIndexFromLine(firstLine);
            if (visStart < 0) visStart = 0;
            int visEnd = box->GetFirstCharIndexFromLine(lastLine);
            if (visEnd < 0 || visEnd > text->Length) visEnd = text->Length;
            if (visEnd <= visStart) return;

            // If the visible part starts inside a /* ... */ comment, tokenize from its opening.
            int tokStart = visStart;
            if (highlighter->HasBlockComments && visStart > 0)
            {
                int open = text->LastIndexOf("/*", visStart - 1, StringComparison::Ordinal);
                int close = text->LastIndexOf("*/", visStart - 1, StringComparison::Ordinal);
                if (open >= 0 && open > close)
                {
                    int ls = text->LastIndexOf(L'\n', open);
                    tokStart = ls < 0 ? 0 : ls + 1;
                }
            }

            List<TokenSpan>^ spans = highlighter->Tokenize(text->Substring(tokStart, visEnd - tokStart), tokStart);

            int selStart = box->SelectionStart, selLen = box->SelectionLength;
            IntPtr scroll = Marshal::AllocHGlobal(8);
            int viewLine = 0;
            try
            {
                colouring = true;
                SaveView(scroll, viewLine);
                Freeze(true);
                box->Select(visStart, visEnd - visStart);
                box->SelectionColor = palette[0];
                for each (TokenSpan sp in spans)
                {
                    int s = Math::Max(sp.Start, visStart);
                    int e = Math::Min(sp.Start + sp.Length, visEnd);
                    if (e <= s) continue;
                    box->Select(s, e - s);
                    box->SelectionColor = palette[(int)sp.Kind];
                }
                box->Select(selStart, selLen);
                RestoreView(scroll, viewLine);
            }
            finally
            {
                Freeze(false);
                colouring = false;
                Marshal::FreeHGlobal(scroll);
            }
            lastFirstVisible = FirstVisibleLine();
            gutter->Invalidate();
        }

        // ------------------------------------------------------------ gutter
        void UpdateGutterWidth()
        {
            int digits = Math::Max(3, Convert::ToString(LineCount())->Length);
            if (digits == gutterDigits) return;
            gutterDigits = digits;
            int w = TextRenderer::MeasureText(gcnew String(L'9', digits), gutter->Font).Width + 14;
            gutter->Width = w;
        }

        void OnGutterPaint(Object^ sender, PaintEventArgs^ e)
        {
            e->Graphics->Clear(gutter->BackColor);
            int h = box->ClientSize.Height;
            int total = LineCount();
            int firstChar = box->GetCharIndexFromPosition(Point(1, 1));
            int line = box->GetLineFromCharIndex(firstChar);
            int caretLine = box->GetLineFromCharIndex(box->SelectionStart);
            int lineHeight = box->Font->Height;
            int prevY = -lineHeight;

            SolidBrush^ normal = gcnew SolidBrush(Color::FromArgb(43, 145, 175));
            SolidBrush^ current = gcnew SolidBrush(Color::Black);
            StringFormat^ sf = gcnew StringFormat();
            sf->Alignment = StringAlignment::Far;
            try
            {
                for (; line < total; line++)
                {
                    int idx = box->GetFirstCharIndexFromLine(line);
                    if (idx < 0) break;
                    int y = box->GetPositionFromCharIndex(idx).Y;
                    if (line > 0 && y <= prevY) y = prevY + lineHeight;   // empty last line
                    if (y > h) break;
                    prevY = y;
                    RectangleF r(0.0f, (float)y, (float)(gutter->Width - 6), (float)lineHeight);
                    e->Graphics->DrawString(Convert::ToString(line + 1), gutter->Font,
                                            line == caretLine ? current : normal, r, sf);
                }
                e->Graphics->DrawLine(Pens::LightGray, gutter->Width - 1, 0, gutter->Width - 1, gutter->Height);
            }
            finally
            {
                delete normal; delete current; delete sf;
            }
        }
    };
}
