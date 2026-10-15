// =============================================================================
//  CodeEditor.h - the editor control shown in each document tab
//
//     +--------+-------------------------------------------+
//     | gutter |  CodeTextBox (syntax coloured)            |
//     +--------+-------------------------------------------+
//  Optional split view (Window > Split Editor) shows a read-only mirror above.
//
//  Responsibilities: colouring, undo, find/replace, comment/uncomment,
//  indent, format, case change, bookmarks, breakpoints, execution marker.
//
//  The text is set once as plain text; only the visible lines are coloured
//  (again after scrolling or editing). Undo records one EditDelta per typing
//  burst or per programmatic edit, so undo/redo never reloads the document.
// =============================================================================
#pragma once
#include "SyntaxHighlighter.h"
#include "UndoManager.h"
#include "CodeTextBox.h"

namespace Gride { namespace UI {

    using namespace Gride::Core;
    using namespace Gride::Settings;
    using namespace Gride::Native;

    public ref class FindOptions
    {
    public:
        FindOptions() { MatchCase = false; WholeWord = false; UseRegex = false; Forward = true; }
        property bool MatchCase;
        property bool WholeWord;
        property bool UseRegex;
        property bool Forward;

        Regex^ Build(String^ pattern)
        {
            String^ p = UseRegex ? pattern : Regex::Escape(pattern);
            if (WholeWord) p = "\\b" + p + "\\b";
            RegexOptions o = RegexOptions::Multiline;
            if (!MatchCase) o = o | RegexOptions::IgnoreCase;
            return gcnew Regex(p, o);
        }
    };

    public ref class CodeEditor : public UserControl
    {
    public:
        literal int MaxColorLength = 1500000;     // larger files are shown without colours
        literal int CommentLookBack = 100000;     // how far back an open block comment is searched

        CodeEditor()
        {
            box = gcnew CodeTextBox();
            box->Dock = DockStyle::Fill;
            box->Tag = "keep";          // ThemeManager must not recolour the text (it would erase syntax colours)
            gutter = gcnew LineGutter(box);
            undo = gcnew UndoManager();
            highlighter = gcnew SyntaxHighlighter(nullptr);
            coloring = false;
            suppress = false;
            closed = false;
            inBurst = false;
            inAtomic = false;
            needFullRecolor = false;
            modified = false;
            execLine = 0;
            lang = Language::Other;
            fontName = "Consolas"; fontSize = 10.0f;
            editorFont = nullptr;
            menu = nullptr;
            committedText = String::Empty;
            textVersion = 0;
            mirrorVersion = -1;
            lastFirstVisible = -1;
            lastMirrorFirst = -1;

            split = gcnew SplitContainer();
            split->Dock = DockStyle::Fill;
            split->Orientation = WF::Orientation::Horizontal;
            split->Panel1Collapsed = true;
            split->SplitterWidth = 5;
            mirror = gcnew RichTextBox();
            mirror->Dock = DockStyle::Fill;
            mirror->ReadOnly = true;
            mirror->WordWrap = false;
            mirror->DetectUrls = false;
            mirror->HideSelection = false;
            mirror->BorderStyle = WF::BorderStyle::None;
            mirror->Tag = "keep";
            split->Panel1->Controls->Add(mirror);
            split->Panel2->Controls->Add(box);
            split->Panel2->Controls->Add(gutter);
            Controls->Add(split);

            recolorTimer = gcnew WF::Timer();
            recolorTimer->Interval = 100;
            recolorTimer->Tick += gcnew EventHandler(this, &CodeEditor::OnRecolorTick);
            burstTimer = gcnew WF::Timer();
            burstTimer->Interval = 700;
            burstTimer->Tick += gcnew EventHandler(this, &CodeEditor::OnBurstTick);

            box->TextChanged += gcnew EventHandler(this, &CodeEditor::OnBoxTextChanged);
            box->SelectionChanged += gcnew EventHandler(this, &CodeEditor::OnBoxSelectionChanged);
            box->VScroll += gcnew EventHandler(this, &CodeEditor::OnBoxScroll);
            box->Resize += gcnew EventHandler(this, &CodeEditor::OnBoxScroll);
            box->UndoRequested += gcnew EventHandler(this, &CodeEditor::OnUndoRequested);
            box->RedoRequested += gcnew EventHandler(this, &CodeEditor::OnRedoRequested);
            box->CutRequested += gcnew EventHandler(this, &CodeEditor::OnCutRequested);
            box->BeforeEdit += gcnew EventHandler(this, &CodeEditor::OnBeforeEdit);
            box->AfterEdit += gcnew EventHandler(this, &CodeEditor::OnAfterEdit);
            box->IndentRequested += gcnew BoolHandler(this, &CodeEditor::IndentSelection);
            box->HandleCreated += gcnew EventHandler(this, &CodeEditor::OnBoxHandleCreated);
            mirror->HandleCreated += gcnew EventHandler(this, &CodeEditor::OnMirrorHandleCreated);
            mirror->VScroll += gcnew EventHandler(this, &CodeEditor::OnMirrorScroll);
            mirror->SelectionChanged += gcnew EventHandler(this, &CodeEditor::OnMirrorScroll);
            mirror->Resize += gcnew EventHandler(this, &CodeEditor::OnMirrorScroll);
            gutter->LineClicked += gcnew LineHandler(this, &CodeEditor::OnGutterClicked);
        }

        // ---- events ---------------------------------------------------------------
        event EventHandler^ ModifiedChanged;
        event EventHandler^ CaretMoved;
        event LineHandler^ BreakpointToggled;

        // ---- properties -------------------------------------------------------------
        property CodeTextBox^ Box { CodeTextBox^ get() { return box; } }
        property Language Lang { Language get() { return lang; } }
        property bool Modified
        {
            bool get() { return modified; }
            void set(bool v) { if (v != modified) { modified = v; ModifiedChanged(this, EventArgs::Empty); } }
        }
        property int CurrentLine { int get() { return box->GetLineFromCharIndex(box->SelectionStart) + 1; } }
        property int CurrentColumn { int get() { return box->SelectionStart - box->GetFirstCharIndexOfCurrentLine() + 1; } }
        property int LineCount { int get() { return box->GetLineFromCharIndex(box->TextLength) + 1; } }
        property SortedSet<int>^ Breakpoints { SortedSet<int>^ get() { return gutter->Breakpoints; } }
        property SortedSet<int>^ Bookmarks { SortedSet<int>^ get() { return gutter->Bookmarks; } }
        property bool CanUndo { bool get() { return inBurst || undo->CanUndo; } }
        property bool CanRedo { bool get() { return !inBurst && undo->CanRedo; } }
        property bool HasSelection { bool get() { return box->SelectionLength > 0; } }
        property String^ SelectedText { String^ get() { return box->SelectedText; } }
        property bool IsSplit { bool get() { return !split->Panel1Collapsed; } }
        property bool ReadOnly { bool get() { return box->ReadOnly; } void set(bool v) { box->ReadOnly = v; } }

        // the editor's right-click menu; it is disposed together with the editor
        void SetContextMenu(System::Windows::Forms::ContextMenuStrip^ m)
        {
            if (menu != nullptr && menu != m) delete menu;
            menu = m;
            box->ContextMenuStrip = m;
        }

        // ---- setup ---------------------------------------------------------------------
        void SetLanguage(Language l, LanguageSettings^ settings)
        {
            lang = l;
            highlighter = gcnew SyntaxHighlighter(settings);
            commentLine = settings != nullptr ? settings->LineComment : String::Empty;
            commentStart = settings != nullptr ? settings->BlockCommentStart : String::Empty;
            commentEnd = settings != nullptr ? settings->BlockCommentEnd : String::Empty;
            FullRecolor();
        }

        void ApplyEnvironment(EnvironmentSettings^ env)
        {
            fontName = env->FontName;
            fontSize = env->FontSize;
            box->TabSize = Math::Max(1, env->TabSize);
            box->InsertSpaces = env->InsertSpaces;
            box->ShowWhitespace = env->ShowWhitespace;
            gutter->ShowNumbers = env->LineNumbers;
            ApplyTheme();
        }

        void ApplyTheme()
        {
            Theme^ t = ThemeManager::Current;
            box->BackColor = t->Window; box->ForeColor = t->EditorText;
            mirror->BackColor = t->Window; mirror->ForeColor = t->EditorText;
            split->BackColor = t->Border;
            SetEditorFont();
            mirrorVersion = -1;
            FullRecolor();
            gutter->UpdateWidth();
        }

        void Zoom(float delta)
        {
            fontSize = Math::Max(6.0f, Math::Min(40.0f, fontSize + delta));
            ApplyTheme();
        }
        void ResetZoom(float size) { fontSize = size; ApplyTheme(); }
        property float FontSize { float get() { return fontSize; } }

        void SetShowWhitespace(bool on) { box->ShowWhitespace = on; box->Invalidate(); }
        void SetLineNumbers(bool on) { gutter->ShowNumbers = on; gutter->UpdateWidth(); }

        // ---- text ------------------------------------------------------------------------
        void LoadText(String^ text)
        {
            burstTimer->Stop();
            inBurst = false;
            suppress = true;
            try { box->Text = text->Replace("\r\n", "\n"); }
            finally { suppress = false; }
            committedText = box->CachedText;      // RichEdit may normalise line breaks: keep what it holds
            textVersion++;
            undo->Reset();
            box->Select(0, 0);
            Modified = false;
            gutter->UpdateWidth();
            FullRecolor();
        }

        String^ GetText() { return box->CachedText; }

        void MarkSaved()
        {
            CloseBurst();
            undo->MarkSaved();
            Modified = false;
        }

        // replaces the whole text as one undo step, keeping caret and view
        void ReplaceAllText(String^ text)
        {
            int caret = box->SelectionStart;
            SD::Point pos; int first;
            SaveView(box, pos, first);
            BeginEdit();
            box->Text = text;
            EndEdit();
            box->Select(Math::Min(caret, box->TextLength), 0);
            RestoreView(box, pos, first);
            gutter->UpdateWidth();
            FullRecolor();                          // the new text has no colours and no tab stops yet
        }

        // resets colours and tab stops of the whole text, then colours what is visible
        void HighlightAll() { FullRecolor(); }

        // ---- navigation --------------------------------------------------------------------
        void GoToLine(int line)
        {
            if (line < 1) line = 1;
            int idx = box->GetFirstCharIndexFromLine(line - 1);
            if (idx < 0) idx = box->TextLength;
            box->Select(idx, 0);
            box->ScrollToCaret();
            box->Focus();
        }

        // the place a compiler named: line and column, both 1-based; a column past the line's end
        // lands at its end, and column 0 or 1 at its start
        void GoTo(int line, int column)
        {
            if (line < 1) line = 1;
            int idx = box->GetFirstCharIndexFromLine(line - 1);
            if (idx < 0) { GoToLine(line); return; }
            int length = box->Lines->Length > line - 1 ? box->Lines[line - 1]->Length : 0;
            int offset = Math::Max(0, Math::Min(column - 1, length));
            box->Select(idx + offset, 0);
            box->ScrollToCaret();
            box->Focus();
        }

        void SelectRange(int start, int length)
        {
            box->Select(start, length);
            box->ScrollToCaret();
        }

        // ---- clipboard / undo -----------------------------------------------------------
        void Undo()
        {
            if (box->ReadOnly) return;
            CloseBurst();
            EditDelta^ d = undo->TakeUndo();
            if (d != nullptr) ApplyDelta(d->Start, d->Inserted->Length, d->Removed, d->SelStartBefore, d->SelLengthBefore, d->FirstLineBefore);
        }
        void Redo()
        {
            if (box->ReadOnly) return;
            CloseBurst();
            EditDelta^ d = undo->TakeRedo();
            if (d != nullptr) ApplyDelta(d->Start, d->Removed->Length, d->Inserted, d->CaretAfter, 0, -1);
        }
        void Cut()
        {
            if (box->SelectionLength == 0) return;
            box->Copy();
            if (box->ReadOnly) return;
            BeginEdit();
            box->SelectedText = String::Empty;
            EndEdit();
        }
        void Copy()      { if (box->SelectionLength > 0) box->Copy(); }
        void Paste()     { box->PastePlain(); }
        void DeleteSel()
        {
            if (box->SelectionLength == 0 || box->ReadOnly) return;
            BeginEdit();
            box->SelectedText = String::Empty;
            EndEdit();
        }
        void SelectAll() { box->SelectAll(); }

        // ---- find / replace -------------------------------------------------------------
        bool Find(String^ pattern, FindOptions^ o)
        {
            if (String::IsNullOrEmpty(pattern)) return false;
            Regex^ re;
            try { re = o->Build(pattern); } catch (ArgumentException^) { return false; }
            String^ text = box->CachedText;
            Match^ m;
            if (o->Forward)
            {
                int from = box->SelectionStart + box->SelectionLength;
                m = re->Match(text, Math::Min(from, text->Length));
                if (!m->Success) m = re->Match(text, 0);                       // wrap around
            }
            else
            {
                Regex^ rl = gcnew Regex(re->ToString(), re->Options | RegexOptions::RightToLeft);
                m = rl->Match(text, Math::Min(box->SelectionStart, text->Length));
                if (!m->Success) m = rl->Match(text, text->Length);
            }
            if (!m->Success) return false;
            SelectRange(m->Index, m->Length);
            return true;
        }

        // replaces the current match (if selected) and moves to the next one
        bool Replace(String^ pattern, String^ replacement, FindOptions^ o)
        {
            Regex^ re;
            try { re = o->Build(pattern); } catch (ArgumentException^) { return false; }
            if (box->SelectionLength > 0 && !box->ReadOnly)
            {
                Match^ m = re->Match(box->SelectedText);
                if (m->Success && m->Index == 0 && m->Length == box->SelectionLength)
                {
                    BeginEdit();
                    box->SelectedText = o->UseRegex ? re->Replace(box->SelectedText, replacement) : replacement;
                    EndEdit();
                    ScheduleRecolor();
                }
            }
            return Find(pattern, o);
        }

        int ReplaceAll(String^ pattern, String^ replacement, FindOptions^ o)
        {
            Regex^ re;
            try { re = o->Build(pattern); } catch (ArgumentException^) { return 0; }
            String^ text = box->CachedText;
            int count = re->Matches(text)->Count;
            if (count == 0) return 0;
            String^ repl = o->UseRegex ? replacement : replacement->Replace("$", "$$");
            ReplaceAllText(re->Replace(text, repl));
            return count;
        }

        // ---- line based editing -----------------------------------------------------------
        void CommentSelection(bool comment)
        {
            int first, last;
            SelectedLineRange(first, last);
            List<String^>^ lines = GetLines(first, last);
            bool useLine = !String::IsNullOrEmpty(commentLine);
            if (!useLine && String::IsNullOrEmpty(commentStart)) return;
            int minIndent = Int32::MaxValue;
            for each (String^ l in lines)
                if (l->Trim()->Length > 0) minIndent = Math::Min(minIndent, l->Length - l->TrimStart()->Length);
            if (minIndent == Int32::MaxValue) minIndent = 0;

            for (int i = 0; i < lines->Count; i++)
            {
                String^ l = lines[i];
                if (l->Trim()->Length == 0) continue;
                if (comment)
                {
                    if (useLine) lines[i] = l->Insert(minIndent, commentLine + " ");
                    else lines[i] = l->Substring(0, minIndent) + commentStart + " " + l->Substring(minIndent) + " " + commentEnd;
                }
                else
                {
                    String^ body = l->TrimStart();
                    String^ ind = l->Substring(0, l->Length - body->Length);
                    if (useLine && body->StartsWith(commentLine))
                    {
                        body = body->Substring(commentLine->Length);
                        if (body->StartsWith(" ")) body = body->Substring(1);
                    }
                    else if (!String::IsNullOrEmpty(commentStart) && body->StartsWith(commentStart) && body->TrimEnd()->EndsWith(commentEnd))
                    {
                        body = body->TrimEnd();
                        body = body->Substring(commentStart->Length, body->Length - commentStart->Length - commentEnd->Length);
                        if (body->StartsWith(" ")) body = body->Substring(1);
                        if (body->EndsWith(" ")) body = body->Substring(0, body->Length - 1);
                    }
                    lines[i] = ind + body;
                }
            }
            ReplaceLines(first, last, lines);
        }

        void IndentSelection(bool indent)
        {
            int first, last;
            SelectedLineRange(first, last);
            List<String^>^ lines = GetLines(first, last);
            String^ unit = box->IndentUnit();
            for (int i = 0; i < lines->Count; i++)
            {
                String^ l = lines[i];
                if (indent) { if (l->Length > 0) lines[i] = unit + l; }
                else if (l->StartsWith("\t")) lines[i] = l->Substring(1);
                else
                {
                    int n = 0;
                    while (n < box->TabSize && n < l->Length && l[n] == L' ') n++;
                    lines[i] = l->Substring(n);
                }
            }
            ReplaceLines(first, last, lines);
        }

        void ChangeCase(bool upper)
        {
            if (box->SelectionLength == 0 || box->ReadOnly) return;
            int s = box->SelectionStart, l = box->SelectionLength;
            BeginEdit();
            box->SelectedText = upper ? box->SelectedText->ToUpperInvariant() : box->SelectedText->ToLowerInvariant();
            box->Select(s, l);
            EndEdit();
            ScheduleRecolor();
        }

        // converts leading white space to tabs (toTabs) or to spaces
        void ConvertIndentation(bool toTabs)
        {
            array<String^>^ lines = box->CachedText->Split(L'\n');
            int ts = box->TabSize;
            for (int i = 0; i < lines->Length; i++)
            {
                String^ l = lines[i];
                int n = 0, col = 0;
                while (n < l->Length && (l[n] == L' ' || l[n] == L'\t')) { col = l[n] == L'\t' ? (col / ts + 1) * ts : col + 1; n++; }
                String^ lead = toTabs ? gcnew String(L'\t', col / ts) + gcnew String(L' ', col % ts) : gcnew String(L' ', col);
                lines[i] = lead + l->Substring(n);
            }
            ReplaceAllText(String::Join("\n", lines));
        }

        // re-indents the document by brace depth ( { } ), preprocessor lines at column 0
        void FormatDocument()
        {
            array<String^>^ lines = box->CachedText->Split(L'\n');
            String^ unit = box->IndentUnit();
            int depth = 0;
            bool inComment = false;
            for (int i = 0; i < lines->Length; i++)
            {
                String^ body = lines[i]->Trim();
                if (inComment)
                {
                    if (body->Contains("*/")) inComment = false;
                    continue;               // keep comment blocks as they are
                }
                if (body->StartsWith("/*") && !body->Contains("*/")) inComment = true;
                if (body->Length == 0) { lines[i] = String::Empty; continue; }
                if (body->StartsWith("#")) { lines[i] = body; continue; }
                int d = depth;
                if (body->StartsWith("}")) d = Math::Max(0, d - 1);
                StringBuilder^ sb = gcnew StringBuilder();
                for (int k = 0; k < d; k++) sb->Append(unit);
                sb->Append(body);
                lines[i] = sb->ToString();
                depth = Math::Max(0, depth + CountBraces(body));
            }
            ReplaceAllText(String::Join("\n", lines));
        }

        // ---- bookmarks / breakpoints / execution marker ------------------------------
        void ToggleBookmark()
        {
            int l = CurrentLine;
            if (!Bookmarks->Remove(l)) Bookmarks->Add(l);
            gutter->Invalidate();
        }
        void NextBookmark(bool forward)
        {
            if (Bookmarks->Count == 0) return;
            int cur = CurrentLine;
            int target = -1;
            if (forward)
            {
                for each (int b in Bookmarks) if (b > cur) { target = b; break; }
                if (target < 0) target = Bookmarks->Min;
            }
            else
            {
                for each (int b in Bookmarks) if (b < cur) target = b;
                if (target < 0) target = Bookmarks->Max;
            }
            GoToLine(target);
        }
        void ClearBookmarks() { Bookmarks->Clear(); gutter->Invalidate(); }

        void ToggleBreakpoint(int line)
        {
            if (line <= 0) line = CurrentLine;
            if (!Breakpoints->Remove(line)) Breakpoints->Add(line);
            gutter->Invalidate();
            BreakpointToggled(line);
        }
        void ClearBreakpoints() { Breakpoints->Clear(); gutter->Invalidate(); }

        // the IDE keeps one breakpoint set per file and shares it with the editor
        void ShareBreakpoints(SortedSet<int>^ bp) { gutter->Breakpoints = bp; gutter->Invalidate(); }
        void RefreshGutter() { gutter->Invalidate(); }

        // inserts text at the caret (snippets, templates) as one undo step
        void InsertText(String^ text)
        {
            if (box->ReadOnly) return;
            BeginEdit();
            box->SelectedText = text->Replace("\r\n", "\n");
            EndEdit();
            ScheduleRecolor();
            box->Focus();
        }

        // line = 0 removes the marker
        void SetExecutionLine(int line)
        {
            if (execLine > 0) PaintExecLine(execLine, false);
            execLine = line;
            gutter->ExecLine = line;
            if (line > 0) { PaintExecLine(line, true); GoToLine(line); }
            gutter->Invalidate();
        }

        // ---- split view ------------------------------------------------------------------
        void ToggleSplit()
        {
            split->Panel1Collapsed = !split->Panel1Collapsed;
            if (IsSplit)
            {
                mirrorVersion = -1;
                // throws when the editor is too small for the panel minimum sizes
                try { split->SplitterDistance = Math::Max(60, split->Height / 3); }
                catch (Exception^) { }
                ScheduleRecolor();
            }
        }

        void FocusEditor() { box->Focus(); }

    protected:
        // Stops the timers before the controls go away: DocumentManager deletes the
        // editor while a tick may be pending, which would then run on disposed controls.
        ~CodeEditor()
        {
            closed = true;
            if (recolorTimer != nullptr) { recolorTimer->Stop(); delete recolorTimer; recolorTimer = nullptr; }
            if (burstTimer != nullptr) { burstTimer->Stop(); delete burstTimer; burstTimer = nullptr; }
            if (menu != nullptr) { box->ContextMenuStrip = nullptr; delete menu; menu = nullptr; }
            // the text boxes use editorFont until they are disposed, so they go first
            delete split;
            if (editorFont != nullptr) { delete editorFont; editorFont = nullptr; }
        }

    private:
        CodeTextBox^ box;
        LineGutter^ gutter;
        RichTextBox^ mirror;
        SplitContainer^ split;
        UndoManager^ undo;
        SyntaxHighlighter^ highlighter;
        WF::Timer^ recolorTimer;
        WF::Timer^ burstTimer;
        System::Windows::Forms::ContextMenuStrip^ menu;
        SD::Font^ editorFont;
        bool coloring;          // colours / selection are being changed by the editor itself
        bool suppress;          // text is being changed by Undo / Redo / LoadText: not a user edit
        bool closed;
        bool modified;
        bool needFullRecolor;   // colouring was asked for before the window handle existed
        int execLine;
        Language lang;
        String^ fontName;
        float fontSize;
        String^ commentLine;
        String^ commentStart;
        String^ commentEnd;
        int textVersion;        // incremented on every text change
        int mirrorVersion;      // textVersion the split-view mirror shows
        int lastFirstVisible;
        int lastMirrorFirst;

        // undo bookkeeping: everything typed between two pauses is one "burst"
        String^ committedText;  // text at the end of the last burst / recorded edit
        bool inBurst;
        bool inAtomic;          // between BeginEdit and EndEdit
        bool burstAtomic;
        int burstSelStart, burstSelLength, burstFirstLine;
        int atomicSelStart, atomicSelLength, atomicFirstLine;

        // ---- undo machinery ---------------------------------------------------------------
        void CloseBurst()
        {
            if (burstTimer != nullptr) burstTimer->Stop();
            CommitBurst();
        }

        // records the text changed since the last commit as one delta
        void CommitBurst()
        {
            if (!inBurst) return;
            inBurst = false;
            String^ now = box->CachedText;
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
                d->FirstLineBefore = burstFirstLine;
                int caret = box->SelectionStart;
                if (burstAtomic)
                {
                    d->SelStartBefore = burstSelStart;
                    d->SelLengthBefore = burstSelLength;
                    d->CaretAfter = caret;
                }
                else
                {
                    // the selection seen before the first key of the burst is only
                    // trusted when it lies inside the text the burst replaced
                    bool selOk = burstSelStart >= pre && burstSelStart + burstSelLength <= pre + d->Removed->Length;
                    d->SelStartBefore = selOk ? burstSelStart : pre;
                    d->SelLengthBefore = selOk ? burstSelLength : d->Removed->Length;
                    d->CaretAfter = (caret >= pre && caret <= pre + d->Inserted->Length) ? caret : pre + d->Inserted->Length;
                }
                undo->Push(d);
            }
            committedText = now;
            Modified = !undo->AtSavePoint;
        }

        // a programmatic change follows: close the burst and remember the selection
        void BeginEdit()
        {
            CloseBurst();
            atomicSelStart = box->SelectionStart;
            atomicSelLength = box->SelectionLength;
            atomicFirstLine = FirstVisibleLine(box);
            inAtomic = true;
        }

        // the programmatic change is complete: record it as one undo step
        void EndEdit()
        {
            inAtomic = false;
            CloseBurst();
        }

        // replaces [start, start+length) with text without recording undo, then
        // restores the selection and (if still showing the caret) the view
        void ApplyDelta(int start, int length, String^ text, int selStart, int selLength, int firstLine)
        {
            SD::Point pos; int viewLine;
            SaveView(box, pos, viewLine);
            suppress = true;
            coloring = true;
            NativeMethods::SetRedraw(box->Handle, false);
            try
            {
                box->Select(start, length);
                box->SelectedText = text;
                committedText = box->CachedText;
                textVersion++;
                int len = box->TextLength;
                selStart = Math::Max(0, Math::Min(selStart, len));
                box->Select(selStart, Math::Max(0, Math::Min(selLength, len - selStart)));
                RestoreView(box, pos, firstLine >= 0 ? firstLine : viewLine);
            }
            finally
            {
                suppress = false;
                coloring = false;
                NativeMethods::SetRedraw(box->Handle, true);
            }
            if (!CaretVisible()) box->ScrollToCaret();
            Modified = !undo->AtSavePoint;
            gutter->UpdateWidth();
            gutter->Invalidate();
            lastFirstVisible = -1;
            ScheduleRecolor();
            CaretMoved(this, EventArgs::Empty);
        }

        bool CaretVisible()
        {
            int caretLine = box->GetLineFromCharIndex(box->SelectionStart);
            int first = FirstVisibleLine(box);
            int rows = Math::Max(1, box->ClientSize.Height / Math::Max(1, box->Font->Height));
            return caretLine >= first && caretLine < first + rows;
        }

        // ---- view / colouring helpers ---------------------------------------------------------
        static int FirstVisibleLine(RichTextBox^ t)
        {
            return t->IsHandleCreated ? NativeMethods::FirstVisibleLine(t->Handle) : 0;
        }

        // EM_GETSCROLLPOS is 16-bit inside RichEdit (wrong in files over ~4000 lines),
        // so the vertical position is put back by line with EM_LINESCROLL
        static void SaveView(RichTextBox^ t, SD::Point% pos, int% firstLine)
        {
            pos = NativeMethods::GetScrollPos(t->Handle);
            firstLine = NativeMethods::FirstVisibleLine(t->Handle);
        }

        static void RestoreView(RichTextBox^ t, SD::Point pos, int firstLine)
        {
            NativeMethods::SetScrollPos(t->Handle, pos);
            NativeMethods::LineScroll(t->Handle, firstLine - NativeMethods::FirstVisibleLine(t->Handle));
        }

        array<int>^ TabStops()
        {
            float cw = box->CharWidth();
            array<int>^ stops = gcnew array<int>(32);
            for (int i = 0; i < 32; i++) stops[i] = (int)((i + 1) * box->TabSize * cw);
            return stops;
        }

        void SetEditorFont()
        {
            if (editorFont != nullptr && editorFont->Name == fontName && editorFont->Size == fontSize) return;
            SD::Font^ f;
            try { f = gcnew SD::Font(fontName, fontSize); }
            catch (ArgumentException^) { f = gcnew SD::Font(FontFamily::GenericMonospace, fontSize); }
            SD::Font^ old = editorFont;
            editorFont = f;
            box->Font = f;
            mirror->Font = f;
            if (old != nullptr) delete old;         // no control refers to it any more
        }

        // default colour and tab stops for the whole text (after load, theme, language
        // or font changes), then the visible lines are coloured
        void FullRecolor()
        {
            if (closed) return;
            if (!box->IsHandleCreated) { needFullRecolor = true; return; }     // done in OnBoxHandleCreated
            needFullRecolor = false;
            Theme^ t = ThemeManager::Current;
            int selStart = box->SelectionStart, selLen = box->SelectionLength;
            SD::Point pos; int first;
            SaveView(box, pos, first);
            coloring = true;
            box->Recolouring = true;
            NativeMethods::SetRedraw(box->Handle, false);
            try
            {
                box->SelectAll();
                box->SelectionColor = t->EditorText;
                box->SelectionBackColor = t->Window;
                box->SelectionTabs = TabStops();
                box->Select(selStart, selLen);
                RestoreView(box, pos, first);
            }
            finally
            {
                NativeMethods::SetRedraw(box->Handle, true);
                box->Recolouring = false;
                coloring = false;
            }
            if (execLine > 0) PaintExecLine(execLine, true);
            RecolorVisible();
        }

        void PaintExecLine(int line, bool on)
        {
            int start = box->GetFirstCharIndexFromLine(line - 1);
            if (start < 0) return;
            int next = box->GetFirstCharIndexFromLine(line);
            int end = next < 0 ? box->TextLength : next;
            bool old = coloring;
            coloring = true;
            box->Recolouring = true;
            int s = box->SelectionStart, l = box->SelectionLength;
            box->Select(start, end - start);
            box->SelectionBackColor = on ? ThemeManager::Current->ExecLine : ThemeManager::Current->Window;
            box->Select(s, l);
            box->Recolouring = false;
            coloring = old;
        }

        void ScheduleRecolor()
        {
            if (closed || recolorTimer == nullptr) return;
            recolorTimer->Stop();
            recolorTimer->Start();
        }

        void RecolorVisible()
        {
            if (closed || !box->IsHandleCreated) return;
            String^ text = box->CachedText;
            coloring = true;
            box->Recolouring = true;
            try { ColorVisible(box, text); }
            finally { box->Recolouring = false; coloring = false; }
            lastFirstVisible = FirstVisibleLine(box);
            gutter->Invalidate();
            if (IsSplit && mirror->IsHandleCreated)
            {
                // the selection moves below scroll the mirror for a moment: no new recolour from that
                coloring = true;
                try
                {
                    if (mirrorVersion != textVersion) SyncMirror(text);
                    ColorVisible(mirror, text);
                }
                finally { coloring = false; }
                lastMirrorFirst = FirstVisibleLine(mirror);
            }
        }

        // colours the visible lines of target (the editor or the split-view mirror)
        void ColorVisible(RichTextBox^ target, String^ text)
        {
            if (highlighter->IsPlain || text->Length == 0 || text->Length > MaxColorLength) return;
            int firstChar = target->GetCharIndexFromPosition(SD::Point(1, 1));
            int lastChar = target->GetCharIndexFromPosition(SD::Point(1, Math::Max(1, target->ClientSize.Height - 1)));
            int firstLine = Math::Max(0, target->GetLineFromCharIndex(firstChar) - 1);
            int lastLine = target->GetLineFromCharIndex(lastChar) + 2;
            int visStart = target->GetFirstCharIndexFromLine(firstLine);
            if (visStart < 0 || visStart > text->Length) visStart = 0;
            int visEnd = target->GetFirstCharIndexFromLine(lastLine);
            if (visEnd < 0 || visEnd > text->Length) visEnd = text->Length;
            if (visEnd <= visStart) return;

            // open block comment: scan a bounded stretch before the visible part only
            int scanFrom = Math::Max(0, visStart - CommentLookBack);
            if (scanFrom > 0)
            {
                int nl = text->LastIndexOf(L'\n', scanFrom);
                scanFrom = nl < 0 ? 0 : nl + 1;
            }
            bool inComment = highlighter->InBlockComment(text, scanFrom, visStart);
            List<Token>^ tokens = highlighter->Tokenize(text, visStart, visEnd, inComment);
            Theme^ t = ThemeManager::Current;

            int selStart = target->SelectionStart, selLen = target->SelectionLength;
            SD::Point pos; int first;
            SaveView(target, pos, first);
            NativeMethods::SetRedraw(target->Handle, false);
            try
            {
                target->Select(visStart, visEnd - visStart);
                target->SelectionColor = t->EditorText;
                for each (Token tk in tokens)
                {
                    int s = Math::Max(tk.Start, visStart);
                    int e = Math::Min(tk.Start + tk.Length, visEnd);
                    if (e <= s) continue;
                    target->Select(s, e - s);
                    target->SelectionColor = SyntaxHighlighter::ColorOf(tk.Kind, t);
                }
                target->Select(selStart, selLen);
                RestoreView(target, pos, first);
            }
            finally { NativeMethods::SetRedraw(target->Handle, true); }
        }

        // the mirror gets the plain text again only after the text changed
        void SyncMirror(String^ text)
        {
            SD::Point pos; int first;
            SaveView(mirror, pos, first);
            int sel = mirror->SelectionStart;
            NativeMethods::SetRedraw(mirror->Handle, false);
            try
            {
                mirror->Text = text;
                mirror->SelectAll();
                mirror->SelectionColor = ThemeManager::Current->EditorText;
                mirror->SelectionTabs = TabStops();
                mirror->Select(Math::Min(sel, mirror->TextLength), 0);
                RestoreView(mirror, pos, first);
            }
            finally { NativeMethods::SetRedraw(mirror->Handle, true); }
            mirrorVersion = textVersion;
        }

        static int CountBraces(String^ s)
        {
            int n = 0;
            bool inStr = false;
            wchar_t q = L'"';
            for (int i = 0; i < s->Length; i++)
            {
                wchar_t c = s[i];
                if (inStr) { if (c == L'\\') i++; else if (c == q) inStr = false; continue; }
                if (c == L'"' || c == L'\'') { inStr = true; q = c; continue; }
                if (c == L'/' && i + 1 < s->Length && (s[i + 1] == L'/' || s[i + 1] == L'*')) break;
                if (c == L'{') n++;
                else if (c == L'}') n--;
            }
            return n;
        }

        void SelectedLineRange(int% first, int% last)
        {
            int s = box->SelectionStart, e = s + box->SelectionLength;
            first = box->GetLineFromCharIndex(s);
            last = box->GetLineFromCharIndex(e);
            if (last > first && box->SelectionLength > 0 && e == box->GetFirstCharIndexFromLine(last)) last--;
        }

        List<String^>^ GetLines(int first, int last)
        {
            List<String^>^ r = gcnew List<String^>();
            for (int i = first; i <= last; i++) r->Add(box->LineText(i));
            return r;
        }

        void ReplaceLines(int first, int last, List<String^>^ lines)
        {
            if (box->ReadOnly) return;
            int start = box->GetFirstCharIndexFromLine(first);
            int endNext = box->GetFirstCharIndexFromLine(last + 1);
            int end = endNext < 0 ? box->TextLength : endNext - 1;
            String^ block = String::Join("\n", lines);
            BeginEdit();
            box->Select(start, end - start);
            box->SelectedText = block;
            box->Select(start, block->Length);
            EndEdit();
            ScheduleRecolor();
        }

        // ---- event handlers --------------------------------------------------------------
        void OnBoxTextChanged(Object^ sender, EventArgs^ e)
        {
            if (coloring || suppress || closed) return;
            if (!inBurst)
            {
                inBurst = true;
                undo->DiscardRedo();
                burstAtomic = inAtomic;
                if (inAtomic)
                {
                    burstSelStart = atomicSelStart; burstSelLength = atomicSelLength; burstFirstLine = atomicFirstLine;
                }
                else
                {
                    burstSelStart = box->KeySelStart; burstSelLength = box->KeySelLength; burstFirstLine = FirstVisibleLine(box);
                }
            }
            textVersion++;
            Modified = true;
            if (!inAtomic) { burstTimer->Stop(); burstTimer->Start(); }
            ScheduleRecolor();
            gutter->UpdateWidth();
        }
        void OnBoxSelectionChanged(Object^ sender, EventArgs^ e)
        {
            if (coloring || closed) return;
            gutter->Invalidate();
            // keyboard paging scrolls without a scroll notification
            int first = FirstVisibleLine(box);
            if (first != lastFirstVisible) { lastFirstVisible = first; ScheduleRecolor(); }
            CaretMoved(this, EventArgs::Empty);
        }
        void OnBoxScroll(Object^ sender, EventArgs^ e)
        {
            if (closed) return;
            gutter->Invalidate();
            if (!coloring) ScheduleRecolor();
        }
        void OnMirrorScroll(Object^ sender, EventArgs^ e)
        {
            if (closed || coloring || !IsSplit || !mirror->IsHandleCreated) return;
            if (FirstVisibleLine(mirror) != lastMirrorFirst) ScheduleRecolor();
        }
        void OnMirrorHandleCreated(Object^ sender, EventArgs^ e) { NativeMethods::SetUndoLimit(mirror->Handle, 0); }
        void OnBoxHandleCreated(Object^ sender, EventArgs^ e) { if (needFullRecolor) ScheduleRecolor(); }
        void OnRecolorTick(Object^ sender, EventArgs^ e)
        {
            recolorTimer->Stop();
            if (closed) return;
            // moving the selection back while the user drags or extends it would lose its direction
            if (Control::MouseButtons != WF::MouseButtons::None ||
                (box->SelectionLength > 0 && (Control::ModifierKeys & Keys::Shift) == Keys::Shift))
            {
                recolorTimer->Start();
                return;
            }
            if (needFullRecolor) FullRecolor();
            else RecolorVisible();
        }
        void OnBurstTick(Object^ sender, EventArgs^ e) { burstTimer->Stop(); if (!closed) CommitBurst(); }
        void OnUndoRequested(Object^ sender, EventArgs^ e) { Undo(); }
        void OnRedoRequested(Object^ sender, EventArgs^ e) { Redo(); }
        void OnCutRequested(Object^ sender, EventArgs^ e) { Cut(); }
        void OnBeforeEdit(Object^ sender, EventArgs^ e) { if (!closed && !inAtomic) CloseBurst(); }
        void OnAfterEdit(Object^ sender, EventArgs^ e) { if (!closed && !inAtomic) CloseBurst(); }
        void OnGutterClicked(int line) { ToggleBreakpoint(line); }
    };
}}
