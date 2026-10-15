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
        CodeEditor()
        {
            box = gcnew CodeTextBox();
            box->Dock = DockStyle::Fill;
            box->Tag = "keep";          // ThemeManager must not recolour the text (it would erase syntax colours)
            gutter = gcnew LineGutter(box);
            undo = gcnew UndoManager();
            highlighter = gcnew SyntaxHighlighter(nullptr);
            coloring = false;
            modified = false;
            execLine = 0;
            lang = Language::Other;
            fontName = "Consolas"; fontSize = 10.0f;

            split = gcnew SplitContainer();
            split->Dock = DockStyle::Fill;
            split->Orientation = WF::Orientation::Horizontal;
            split->Panel1Collapsed = true;
            split->SplitterWidth = 5;
            mirror = gcnew RichTextBox();
            mirror->Dock = DockStyle::Fill;
            mirror->ReadOnly = true;
            mirror->WordWrap = false;
            mirror->BorderStyle = WF::BorderStyle::None;
            mirror->Tag = "keep";
            split->Panel1->Controls->Add(mirror);
            split->Panel2->Controls->Add(box);
            split->Panel2->Controls->Add(gutter);
            Controls->Add(split);

            recolorTimer = gcnew WF::Timer();
            recolorTimer->Interval = 200;
            recolorTimer->Tick += gcnew EventHandler(this, &CodeEditor::OnRecolorTick);
            snapshotTimer = gcnew WF::Timer();
            snapshotTimer->Interval = 700;
            snapshotTimer->Tick += gcnew EventHandler(this, &CodeEditor::OnSnapshotTick);

            box->TextChanged += gcnew EventHandler(this, &CodeEditor::OnBoxTextChanged);
            box->SelectionChanged += gcnew EventHandler(this, &CodeEditor::OnBoxSelectionChanged);
            box->VScroll += gcnew EventHandler(this, &CodeEditor::OnBoxScroll);
            box->Resize += gcnew EventHandler(this, &CodeEditor::OnBoxScroll);
            box->UndoRequested += gcnew EventHandler(this, &CodeEditor::OnUndoRequested);
            box->RedoRequested += gcnew EventHandler(this, &CodeEditor::OnRedoRequested);
            box->BeforeEdit += gcnew EventHandler(this, &CodeEditor::OnBeforeEdit);
            box->IndentRequested += gcnew BoolHandler(this, &CodeEditor::IndentSelection);
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
        property bool CanUndo { bool get() { return undo->CanUndo || box->Modified; } }
        property bool CanRedo { bool get() { return undo->CanRedo; } }
        property bool HasSelection { bool get() { return box->SelectionLength > 0; } }
        property String^ SelectedText { String^ get() { return box->SelectedText; } }
        property bool IsSplit { bool get() { return !split->Panel1Collapsed; } }
        property bool ReadOnly { bool get() { return box->ReadOnly; } void set(bool v) { box->ReadOnly = v; } }

        // ---- setup ---------------------------------------------------------------------
        void SetLanguage(Language l, LanguageSettings^ settings)
        {
            lang = l;
            highlighter = gcnew SyntaxHighlighter(settings);
            commentLine = settings != nullptr ? settings->LineComment : String::Empty;
            commentStart = settings != nullptr ? settings->BlockCommentStart : String::Empty;
            commentEnd = settings != nullptr ? settings->BlockCommentEnd : String::Empty;
            HighlightAll();
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
            box->Font = gcnew SD::Font(fontName, fontSize);
            mirror->Font = box->Font;
            HighlightAll();
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
            coloring = true;
            box->Text = text->Replace("\r\n", "\n");
            coloring = false;
            HighlightAll();
            box->Select(0, 0);
            undo->Reset(box->Text, 0);
            box->Modified = false;
            Modified = false;
            gutter->UpdateWidth();
        }

        String^ GetText() { return box->Text; }

        void MarkSaved()
        {
            box->Modified = false;
            Modified = false;
        }

        // replaces the whole text but keeps undo history and caret
        void ReplaceAllText(String^ text)
        {
            Snapshot();
            int caret = box->SelectionStart;
            SD::Point scroll = NativeMethods::GetScrollPos(box->Handle);
            coloring = true;
            box->Text = text;
            coloring = false;
            HighlightAll();
            box->Select(Math::Min(caret, box->TextLength), 0);
            NativeMethods::SetScrollPos(box->Handle, scroll);
            Snapshot();
            Modified = true;
            gutter->UpdateWidth();
        }

        void HighlightAll()
        {
            if (box->TextLength > 1500000 || highlighter->IsPlain)
            {
                // very large or plain files: no colouring, keep it fast
                coloring = true;
                int s = box->SelectionStart, l = box->SelectionLength;
                box->SelectAll(); box->SelectionColor = ThemeManager::Current->EditorText;
                box->SelectionBackColor = ThemeManager::Current->Window;
                box->Select(s, l);
                coloring = false;
                ApplyTabStops();
                return;
            }
            coloring = true;
            int selStart = box->SelectionStart, selLen = box->SelectionLength;
            SD::Point scroll = NativeMethods::GetScrollPos(box->Handle);
            NativeMethods::SetRedraw(box->Handle, false);
            String^ text = box->Text;
            box->Rtf = highlighter->BuildRtf(text, ThemeManager::Current, fontName, fontSize);
            box->Select(selStart, selLen);
            NativeMethods::SetScrollPos(box->Handle, scroll);
            ApplyTabStops();
            NativeMethods::SetRedraw(box->Handle, true);
            coloring = false;
            if (execLine > 0) PaintExecLine(execLine, true);
            if (IsSplit) mirror->Rtf = box->Rtf;
        }

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

        void SelectRange(int start, int length)
        {
            box->Select(start, length);
            box->ScrollToCaret();
        }

        // ---- clipboard / undo -----------------------------------------------------------
        void Undo()
        {
            UndoState^ s = undo->Undo(box->Text, box->SelectionStart);
            if (s != nullptr) RestoreState(s);
        }
        void Redo()
        {
            UndoState^ s = undo->Redo();
            if (s != nullptr) RestoreState(s);
        }
        void Cut()       { if (box->SelectionLength > 0) { Snapshot(); box->Cut(); } }
        void Copy()      { if (box->SelectionLength > 0) box->Copy(); }
        void Paste()     { box->PastePlain(); }
        void DeleteSel() { if (box->SelectionLength > 0) { Snapshot(); box->SelectedText = String::Empty; } }
        void SelectAll() { box->SelectAll(); }

        // ---- find / replace -------------------------------------------------------------
        bool Find(String^ pattern, FindOptions^ o)
        {
            if (String::IsNullOrEmpty(pattern)) return false;
            Regex^ re;
            try { re = o->Build(pattern); } catch (ArgumentException^) { return false; }
            String^ text = box->Text;
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
                m = rl->Match(text, box->SelectionStart);
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
            if (box->SelectionLength > 0)
            {
                Match^ m = re->Match(box->SelectedText);
                if (m->Success && m->Index == 0 && m->Length == box->SelectionLength)
                {
                    Snapshot();
                    box->SelectedText = o->UseRegex ? re->Replace(box->SelectedText, replacement) : replacement;
                    ScheduleRecolor();
                }
            }
            return Find(pattern, o);
        }

        int ReplaceAll(String^ pattern, String^ replacement, FindOptions^ o)
        {
            Regex^ re;
            try { re = o->Build(pattern); } catch (ArgumentException^) { return 0; }
            String^ text = box->Text;
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
            if (box->SelectionLength == 0) return;
            Snapshot();
            int s = box->SelectionStart, l = box->SelectionLength;
            box->SelectedText = upper ? box->SelectedText->ToUpperInvariant() : box->SelectedText->ToLowerInvariant();
            box->Select(s, l);
            ScheduleRecolor();
        }

        // converts leading white space to tabs (toTabs) or to spaces
        void ConvertIndentation(bool toTabs)
        {
            array<String^>^ lines = box->Text->Split(L'\n');
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
            array<String^>^ lines = box->Text->Split(L'\n');
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

        // inserts text at the caret (snippets, templates)
        void InsertText(String^ text)
        {
            Snapshot();
            box->SelectedText = text->Replace("\r\n", "\n");
            Snapshot();
            Modified = true;
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
                mirror->Rtf = box->Rtf;
                // throws when the editor is too small for the panel minimum sizes
                try { split->SplitterDistance = Math::Max(60, split->Height / 3); }
                catch (Exception^) { }
            }
        }

        void FocusEditor() { box->Focus(); }

    private:
        CodeTextBox^ box;
        LineGutter^ gutter;
        RichTextBox^ mirror;
        SplitContainer^ split;
        UndoManager^ undo;
        SyntaxHighlighter^ highlighter;
        WF::Timer^ recolorTimer;
        WF::Timer^ snapshotTimer;
        bool coloring;
        bool modified;
        int execLine;
        Language lang;
        String^ fontName;
        float fontSize;
        String^ commentLine;
        String^ commentStart;
        String^ commentEnd;

        void Snapshot()
        {
            snapshotTimer->Stop();
            undo->Push(box->Text, box->SelectionStart);
        }

        void RestoreState(UndoState^ s)
        {
            coloring = true;
            box->Text = s->Text;
            coloring = false;
            HighlightAll();
            box->Select(Math::Min(s->Caret, box->TextLength), 0);
            box->ScrollToCaret();
            Modified = true;
            gutter->UpdateWidth();
        }

        void ApplyTabStops()
        {
            float cw = box->CharWidth();
            array<int>^ stops = gcnew array<int>(32);
            for (int i = 0; i < 32; i++) stops[i] = (int)((i + 1) * box->TabSize * cw);
            int s = box->SelectionStart, l = box->SelectionLength;
            bool old = coloring;
            coloring = true;
            box->SelectAll();
            box->SelectionTabs = stops;
            box->Select(s, l);
            coloring = old;
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
            int start = box->GetFirstCharIndexFromLine(first);
            int endNext = box->GetFirstCharIndexFromLine(last + 1);
            int end = endNext < 0 ? box->TextLength : endNext - 1;
            Snapshot();
            String^ block = String::Join("\n", lines);
            box->Select(start, end - start);
            box->SelectedText = block;
            box->Select(start, block->Length);
            Snapshot();
            Modified = true;
            ScheduleRecolor();
        }

        void PaintExecLine(int line, bool on)
        {
            int start = box->GetFirstCharIndexFromLine(line - 1);
            if (start < 0) return;
            int next = box->GetFirstCharIndexFromLine(line);
            int end = next < 0 ? box->TextLength : next;
            bool old = coloring;
            coloring = true;
            int s = box->SelectionStart, l = box->SelectionLength;
            box->Select(start, end - start);
            box->SelectionBackColor = on ? ThemeManager::Current->ExecLine : ThemeManager::Current->Window;
            box->Select(s, l);
            coloring = old;
        }

        void ScheduleRecolor() { recolorTimer->Stop(); recolorTimer->Start(); }

        // colours only the visible region (after typing or scrolling)
        void RecolorVisible()
        {
            if (highlighter->IsPlain || box->TextLength == 0 || box->TextLength > 1500000) return;
            String^ text = box->Text;
            int first = box->GetCharIndexFromPosition(SD::Point(0, 0));
            first = box->GetFirstCharIndexFromLine(box->GetLineFromCharIndex(first));
            int last = box->GetCharIndexFromPosition(SD::Point(box->ClientSize.Width, box->ClientSize.Height));
            int lastLine = box->GetLineFromCharIndex(last) + 1;
            int next = box->GetFirstCharIndexFromLine(lastLine + 1);
            last = next < 0 ? text->Length : next;
            if (first < 0) first = 0;
            if (last <= first) return;

            bool inComment = highlighter->InBlockComment(text, first);
            List<Token>^ tokens = highlighter->Tokenize(text, first, last, inComment);
            Theme^ t = ThemeManager::Current;

            coloring = true;
            int selStart = box->SelectionStart, selLen = box->SelectionLength;
            SD::Point scroll = NativeMethods::GetScrollPos(box->Handle);
            NativeMethods::SetRedraw(box->Handle, false);
            box->Select(first, last - first);
            box->SelectionColor = t->EditorText;
            for each (Token tk in tokens)
            {
                box->Select(tk.Start, tk.Length);
                box->SelectionColor = SyntaxHighlighter::ColorOf(tk.Kind, t);
            }
            box->Select(selStart, selLen);
            NativeMethods::SetScrollPos(box->Handle, scroll);
            NativeMethods::SetRedraw(box->Handle, true);
            coloring = false;
        }

        // ---- event handlers --------------------------------------------------------------
        void OnBoxTextChanged(Object^ sender, EventArgs^ e)
        {
            if (coloring) return;
            Modified = true;
            ScheduleRecolor();
            snapshotTimer->Stop(); snapshotTimer->Start();
            gutter->UpdateWidth();
        }
        void OnBoxSelectionChanged(Object^ sender, EventArgs^ e)
        {
            if (coloring) return;
            gutter->Invalidate();
            CaretMoved(this, EventArgs::Empty);
        }
        void OnBoxScroll(Object^ sender, EventArgs^ e)
        {
            gutter->Invalidate();
            if (!coloring) ScheduleRecolor();
        }
        void OnRecolorTick(Object^ sender, EventArgs^ e)
        {
            recolorTimer->Stop();
            RecolorVisible();
            if (IsSplit) mirror->Rtf = box->Rtf;
        }
        void OnSnapshotTick(Object^ sender, EventArgs^ e) { Snapshot(); }
        void OnUndoRequested(Object^ sender, EventArgs^ e) { Undo(); }
        void OnRedoRequested(Object^ sender, EventArgs^ e) { Redo(); }
        void OnBeforeEdit(Object^ sender, EventArgs^ e) { Snapshot(); }
        void OnGutterClicked(int line) { ToggleBreakpoint(line); }
    };
}}
