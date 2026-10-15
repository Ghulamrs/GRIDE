// =============================================================================
//  MainForm.Edit.cpp - Edit, View, Window and Help menus
// =============================================================================
#include "../pch.h"
#include "MainForm.h"

namespace Gride { namespace UI {

    using namespace Gride::Native;

    // the text box that has keyboard focus (also inside panes / dialogs), or null
    TextBoxBase^ MainForm::FocusedTextBox()
    {
        IntPtr h = NativeMethods::FocusedHandle();
        if (h == IntPtr::Zero) return nullptr;
        return dynamic_cast<TextBoxBase^>(Control::FromHandle(h));
    }

    CodeEditor^ MainForm::ActiveEditor()
    {
        EditorDocument^ d = docs->Active;
        if (d == nullptr) return nullptr;
        return d->Editor;
    }

    // =========================================================================
    //  Edit: clipboard and undo go to the focused control
    //  (the code editor, or any text box in a pane)
    // =========================================================================
    void MainForm::EditUndo()
    {
        TextBoxBase^ tb = FocusedTextBox();
        CodeEditor^ ed = ActiveEditor();
        if (ed != nullptr && (tb == nullptr || tb == ed->Box)) ed->Undo();
        else if (tb != nullptr) tb->Undo();
    }

    void MainForm::EditRedo()
    {
        CodeEditor^ ed = ActiveEditor();
        if (ed != nullptr) ed->Redo();
    }

    void MainForm::EditCut()
    {
        TextBoxBase^ tb = FocusedTextBox();
        CodeEditor^ ed = ActiveEditor();
        if (ed != nullptr && (tb == nullptr || tb == ed->Box)) ed->Cut();
        else if (tb != nullptr) tb->Cut();
    }

    void MainForm::EditCopy()
    {
        TextBoxBase^ tb = FocusedTextBox();
        CodeEditor^ ed = ActiveEditor();
        if (ed != nullptr && (tb == nullptr || tb == ed->Box)) ed->Copy();
        else if (tb != nullptr) tb->Copy();
    }

    void MainForm::EditPaste()
    {
        TextBoxBase^ tb = FocusedTextBox();
        CodeEditor^ ed = ActiveEditor();
        if (ed != nullptr && (tb == nullptr || tb == ed->Box)) ed->Paste();
        else if (tb != nullptr) tb->Paste();
    }

    void MainForm::EditDelete()
    {
        TextBoxBase^ tb = FocusedTextBox();
        CodeEditor^ ed = ActiveEditor();
        if (ed != nullptr && (tb == nullptr || tb == ed->Box)) ed->DeleteSel();
        else if (tb != nullptr && tb->SelectionLength > 0 && !tb->ReadOnly) tb->SelectedText = String::Empty;
    }

    void MainForm::EditSelectAll()
    {
        TextBoxBase^ tb = FocusedTextBox();
        CodeEditor^ ed = ActiveEditor();
        if (ed != nullptr && (tb == nullptr || tb == ed->Box)) ed->SelectAll();
        else if (tb != nullptr) tb->SelectAll();
    }

    // =========================================================================
    //  Find / Replace / Find in Files
    // =========================================================================
    void MainForm::EditFind()
    {
        CodeEditor^ ed = ActiveEditor();
        findDialog->ShowMode(this, 0, ed != nullptr ? ed->SelectedText : String::Empty);
    }

    void MainForm::EditReplace()
    {
        CodeEditor^ ed = ActiveEditor();
        findDialog->ShowMode(this, 1, ed != nullptr ? ed->SelectedText : String::Empty);
    }

    void MainForm::EditFindInFiles()
    {
        CodeEditor^ ed = ActiveEditor();
        findDialog->ShowMode(this, 2, ed != nullptr ? ed->SelectedText : String::Empty);
    }

    List<String^>^ MainForm::FilesInScope(FindScope scope)
    {
        List<String^>^ files = gcnew List<String^>();
        if (scope == FindScope::CurrentDocument)
        {
            EditorDocument^ d = docs->Active;
            if (d != nullptr && !d->IsUntitled) files->Add(d->FilePath);
        }
        else if (scope == FindScope::OpenDocuments)
        {
            for each (EditorDocument^ d in docs->Documents) if (!d->IsUntitled) files->Add(d->FilePath);
        }
        else if (workspace != nullptr)
        {
            for each (GrideProject^ p in workspace->Projects)
                for each (String^ f in p->AllFiles())
                    if (File::Exists(f) && !files->Contains(f)) files->Add(f);
        }
        return files;
    }

    void MainForm::OnFindRequested(FindRequest^ r)
    {
        CodeEditor^ ed = ActiveEditor();
        switch (r->Action)
        {
        case FindAction::FindNext:
        case FindAction::FindPrevious:
            if (ed == nullptr) { findDialog->SetStatus("No document is open."); return; }
            if (ed->Find(r->Pattern, r->Options)) findDialog->SetStatus(String::Empty);
            else findDialog->SetStatus("The text was not found.");
            return;

        case FindAction::Replace:
            if (ed == nullptr) return;
            if (ed->Replace(r->Pattern, r->Replacement, r->Options)) findDialog->SetStatus(String::Empty);
            else findDialog->SetStatus("No more matches.");
            return;

        case FindAction::ReplaceAll:
        {
            int total = 0, filesChanged = 0;
            if (r->Scope == FindScope::CurrentDocument)
            {
                if (ed != nullptr) total = ed->ReplaceAll(r->Pattern, r->Replacement, r->Options);
                filesChanged = total > 0 ? 1 : 0;
            }
            else
            {
                List<String^>^ files = FilesInScope(r->Scope);
                if (r->Scope == FindScope::Workspace &&
                    MessageBox::Show(findDialog, String::Format("Replace in all {0} workspace file(s)? Files that are not open are changed on disk.", files->Count),
                                     "Replace All", MessageBoxButtons::OKCancel, MessageBoxIcon::Warning) != WF::DialogResult::OK) return;
                Regex^ re;
                try { re = r->Options->Build(r->Pattern); } catch (ArgumentException^ ex) { findDialog->SetStatus(ex->Message); return; }
                for each (String^ f in files)
                {
                    EditorDocument^ d = docs->Find(f);
                    int n = 0;
                    if (d != nullptr) n = d->Editor->ReplaceAll(r->Pattern, r->Replacement, r->Options);
                    else
                    {
                        String^ text = File::ReadAllText(f);
                        n = re->Matches(text)->Count;
                        if (n > 0)
                        {
                            String^ repl = r->Options->UseRegex ? r->Replacement : r->Replacement->Replace("$", "$$");
                            File::WriteAllText(f, re->Replace(text, repl));
                        }
                    }
                    if (n > 0) { total += n; filesChanged++; }
                }
            }
            findDialog->SetStatus(String::Format("{0} occurrence(s) replaced in {1} file(s).", total, filesChanged));
            return;
        }

        case FindAction::FindAll:
        {
            Regex^ re;
            try { re = r->Options->Build(r->Pattern); } catch (ArgumentException^ ex) { findDialog->SetStatus(ex->Message); return; }
            List<String^>^ files = FilesInScope(r->Scope);
            findResults->Begin(r->Pattern);
            int filesWithHits = 0;
            for each (String^ f in files)
            {
                EditorDocument^ d = docs->Find(f);
                array<String^>^ lines;
                try { lines = d != nullptr ? d->Editor->GetText()->Split(L'\n') : File::ReadAllLines(f); }
                catch (Exception^) { continue; }
                bool hit = false;
                for (int i = 0; i < lines->Length; i++)
                    if (re->IsMatch(lines[i])) { findResults->Add(f, i + 1, lines[i]); hit = true; }
                if (hit) filesWithHits++;
            }
            findResults->End(filesWithHits);
            bottomArea->ShowPane(findResults);
            findDialog->SetStatus(String::Format("Searched {0} file(s).", files->Count));
            return;
        }
        }
    }

    void MainForm::EditGoToLine()
    {
        CodeEditor^ ed = ActiveEditor();
        if (ed == nullptr) return;
        GoToLineDialog^ dlg = gcnew GoToLineDialog(ed->CurrentLine, ed->LineCount);
        if (dlg->ShowDialog(this) == WF::DialogResult::OK) ed->GoToLine(dlg->Line);
    }

    // ---- Advanced ------------------------------------------------------------
    void MainForm::EditComment()   { CodeEditor^ ed = ActiveEditor(); if (ed != nullptr) ed->CommentSelection(true); }
    void MainForm::EditUncomment() { CodeEditor^ ed = ActiveEditor(); if (ed != nullptr) ed->CommentSelection(false); }
    void MainForm::EditFormat()    { CodeEditor^ ed = ActiveEditor(); if (ed != nullptr) ed->FormatDocument(); }
    void MainForm::EditUpper()     { CodeEditor^ ed = ActiveEditor(); if (ed != nullptr) ed->ChangeCase(true); }
    void MainForm::EditLower()     { CodeEditor^ ed = ActiveEditor(); if (ed != nullptr) ed->ChangeCase(false); }
    void MainForm::EditIndent()    { CodeEditor^ ed = ActiveEditor(); if (ed != nullptr) ed->IndentSelection(true); }
    void MainForm::EditOutdent()   { CodeEditor^ ed = ActiveEditor(); if (ed != nullptr) ed->IndentSelection(false); }

    // converts the indentation to the style chosen in Options (tabs or spaces)
    void MainForm::EditTabify()
    {
        CodeEditor^ ed = ActiveEditor();
        if (ed == nullptr) return;
        ed->ConvertIndentation(!settings->Env->InsertSpaces);
        if (settings->Env->InsertSpaces) SetStatus("Indentation converted to spaces");
        else SetStatus("Indentation converted to tabs");
    }

    // ---- Bookmarks -------------------------------------------------------------
    void MainForm::EditToggleBookmark() { CodeEditor^ ed = ActiveEditor(); if (ed != nullptr) ed->ToggleBookmark(); }
    void MainForm::EditNextBookmark()   { CodeEditor^ ed = ActiveEditor(); if (ed != nullptr) ed->NextBookmark(true); }
    void MainForm::EditPrevBookmark()   { CodeEditor^ ed = ActiveEditor(); if (ed != nullptr) ed->NextBookmark(false); }
    void MainForm::EditClearBookmarks() { CodeEditor^ ed = ActiveEditor(); if (ed != nullptr) ed->ClearBookmarks(); }

    // =========================================================================
    //  View
    // =========================================================================
    void MainForm::ViewStatusBar() { status->Visible = !status->Visible; }

    void MainForm::ViewFullScreen()
    {
        fullScreen = !fullScreen;
        if (fullScreen)
        {
            savedState = WindowState;
            FormBorderStyle = WF::FormBorderStyle::None;
            WindowState = FormWindowState::Normal;
            WindowState = FormWindowState::Maximized;
        }
        else
        {
            FormBorderStyle = WF::FormBorderStyle::Sizable;
            WindowState = savedState;
        }
    }

    void MainForm::ViewZoomIn()    { for each (EditorDocument^ d in docs->Documents) d->Editor->Zoom(1.0f); }
    void MainForm::ViewZoomOut()   { for each (EditorDocument^ d in docs->Documents) d->Editor->Zoom(-1.0f); }
    void MainForm::ViewZoomReset() { for each (EditorDocument^ d in docs->Documents) d->Editor->ResetZoom(settings->Env->FontSize); }

    void MainForm::ViewLineNumbers()
    {
        settings->Env->LineNumbers = !settings->Env->LineNumbers;
        for each (EditorDocument^ d in docs->Documents) d->Editor->SetLineNumbers(settings->Env->LineNumbers);
    }

    void MainForm::ViewWhitespace()
    {
        settings->Env->ShowWhitespace = !settings->Env->ShowWhitespace;
        for each (EditorDocument^ d in docs->Documents) d->Editor->SetShowWhitespace(settings->Env->ShowWhitespace);
    }

    void MainForm::SetTheme(String^ name)
    {
        settings->Env->Theme = name;
        ApplyTheme();
        SettingsManager::Save(settings);
    }

    // =========================================================================
    //  Window
    // =========================================================================
    void MainForm::WindowSplit() { CodeEditor^ ed = ActiveEditor(); if (ed != nullptr) ed->ToggleSplit(); }

    // floats the active pane of the area that has the focus (bottom area otherwise)
    void MainForm::WindowFloat()
    {
        DockArea^ area = bottomArea;
        for each (DockArea^ a in gcnew array<DockArea^>{ leftArea, rightArea, bottomArea })
            if (a->ContainsFocus) area = a;
        if (area->ActivePane != nullptr) area->FloatPane(area->ActivePane);
    }

    void MainForm::WindowDock()
    {
        for each (DockArea^ a in gcnew array<DockArea^>{ leftArea, rightArea, bottomArea }) a->DockAll();
    }

    // collapses (or restores) every tool area to give the editor the whole window
    void MainForm::WindowAutoHide()
    {
        autoHidden = !autoHidden;
        if (autoHidden)
        {
            splitMain->Panel1Collapsed = true;
            splitRight->Panel2Collapsed = true;
            splitCenter->Panel2Collapsed = true;
        }
        else OnAreaVisibilityChanged(nullptr, EventArgs::Empty);
    }

    void MainForm::WindowPinTab() { docs->TogglePin(docs->Active); }
    void MainForm::WindowCloseAll() { docs->CloseAll(nullptr); }
    void MainForm::WindowCloseAllButThis() { docs->CloseAll(docs->Active); }

    void MainForm::WindowResetLayout() { ResetLayout(); }

    void MainForm::WindowSaveLayout()
    {
        StoreLayout();
        SettingsManager::Save(settings);
        SetStatus("Window layout saved");
    }

    void MainForm::WindowList()
    {
        List<String^>^ titles = gcnew List<String^>();
        List<String^>^ paths = gcnew List<String^>();
        for each (EditorDocument^ d in docs->Documents)
        {
            titles->Add(d->Title);
            if (d->IsUntitled) paths->Add(String::Empty); else paths->Add(d->FilePath);
        }
        WindowsDialog^ dlg = gcnew WindowsDialog(titles, paths);
        if (dlg->ShowDialog(this) != WF::DialogResult::OK) return;
        List<EditorDocument^>^ chosen = gcnew List<EditorDocument^>();
        for each (int i in dlg->Selected) chosen->Add(docs->Documents[i]);
        for each (EditorDocument^ d in chosen)
        {
            if (dlg->Action == "Activate") { docs->Activate(d); break; }
            if (dlg->Action == "Save") docs->Save(d);
            if (dlg->Action->StartsWith("Close")) docs->Close(d);
        }
    }

    // =========================================================================
    //  Help
    // =========================================================================
    void MainForm::HelpView() { HelpUserGuide(); }

    void MainForm::HelpUserGuide() { OpenHelpFile("GRIDE_User_Guide"); }

    // opens <HelpDirectory>\<baseName>.(pdf|chm|html|htm|md|txt)
    void MainForm::OpenHelpFile(String^ baseName)
    {
        String^ dir = Environment::ExpandEnvironmentVariables(settings->Env->HelpDirectory);
        for each (String^ ext in gcnew array<String^>{ ".pdf", ".chm", ".html", ".htm", ".md", ".txt" })
        {
            String^ f = Path::Combine(dir, baseName + ext);
            if (File::Exists(f))
            {
                try { System::Diagnostics::Process::Start(f); }
                catch (Exception^ ex) { MessageBox::Show(this, ex->Message, "Help"); }
                return;
            }
        }
        MessageBox::Show(this, "The reference document is not installed yet.\n\nPlace " + baseName +
                         ".pdf (or .chm / .html / .md / .txt) in:\n" + dir +
                         "\n\nThe folder can be changed in Tools > Options > Projects.", "Help",
                         MessageBoxButtons::OK, MessageBoxIcon::Information);
    }

    void MainForm::HelpShortcuts()
    {
        ShortcutsDialog^ dlg = gcnew ShortcutsDialog(commands);
        dlg->ShowDialog(this);
    }

    void MainForm::HelpReleaseNotes()
    {
        String^ f = Path::Combine(Environment::ExpandEnvironmentVariables(settings->Env->HelpDirectory), "ReleaseNotes.txt");
        String^ text;
        if (File::Exists(f)) text = File::ReadAllText(f);
        else text =
            "GRIDE 1.0\r\n"
            "=========\r\n\r\n"
            "* VS-style shell: File, Edit, View, Project, Build, Debug, Tools, Window, Help\r\n"
            "* Languages C90, C++11, Shalimar; targets Windows, Linux, macOS, TMS320C6747\r\n"
            "* JSON project (.pro), workspace (.grws) and settings (gride.settings.json) files\r\n"
            "* Read-only import of Code Composer Studio 7.4 projects\r\n"
            "* Template-driven build engine (compile -> assemble -> link) with error parsing\r\n"
            "* C <-> Shalimar conversion through configurable converter tools\r\n"
            "* Placeholder debugger with breakpoints, stepping, registers, memory, call stack\r\n"
            "* Dark / Light / Blue themes, dockable and floatable panes, keyboard customisation\r\n";
        TextDialog^ dlg = gcnew TextDialog("Release Notes", text);
        dlg->ShowDialog(this);
    }

    void MainForm::HelpAbout()
    {
        AboutDialog^ dlg = gcnew AboutDialog(settings->FilePath);
        dlg->ShowDialog(this);
    }
}}
