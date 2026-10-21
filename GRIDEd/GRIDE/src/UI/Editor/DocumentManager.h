// =============================================================================
//  DocumentManager.h - the tabbed document area in the middle of the window
//  Opens, activates, saves and closes EditorDocuments; shows the StartPage
//  when nothing is open.
// =============================================================================
#pragma once
#include "EditorDocument.h"
#include "../Controls/StartPage.h"
#include "../IconFactory.h"

namespace Gride { namespace UI {

    using namespace Gride::Settings;

    public delegate void DocumentLineHandler(EditorDocument^ doc, int line);

    public ref class DocumentManager : public UserControl
    {
    public:
        DocumentManager()
        {
            docs = gcnew List<EditorDocument^>();
            settings = nullptr;
            tabs = gcnew TabStrip(TabStripStyle::Documents);
            host = gcnew Panel();
            host->Dock = DockStyle::Fill;
            start = gcnew StartPage();
            host->Controls->Add(start);
            Controls->Add(host);
            Controls->Add(tabs);

            tabs->SelectedChanged += gcnew EventHandler(this, &DocumentManager::OnTabSelected);
            tabs->CloseRequested += gcnew TabEventHandler(this, &DocumentManager::OnTabCloseRequested);
            tabs->TabRightClicked += gcnew TabEventHandler(this, &DocumentManager::OnTabRightClicked);
            BuildTabMenu();
        }

        event EventHandler^ ActiveChanged;
        event EventHandler^ CaretMoved;
        event EventHandler^ DirtyChanged;
        event DocumentLineHandler^ BreakpointToggled;

        // creates the right-click menu of each new editor (MainForm builds it from its commands)
        property Func<System::Windows::Forms::ContextMenuStrip^>^ EditorMenuFactory;

        property StartPage^ Start { StartPage^ get() { return start; } }
        property List<EditorDocument^>^ Documents { List<EditorDocument^>^ get() { return docs; } }
        property GrideSettings^ AppSettings { GrideSettings^ get() { return settings; } void set(GrideSettings^ s) { settings = s; } }

        property EditorDocument^ Active
        {
            EditorDocument^ get()
            {
                TabItem^ t = tabs->SelectedItem;
                if (t == nullptr) return nullptr;
                return safe_cast<EditorDocument^>(t->Tag);
            }
        }

        EditorDocument^ Find(String^ path)
        {
            for each (EditorDocument^ d in docs)
                if (!d->IsUntitled && PathUtil::SamePath(d->FilePath, path)) return d;
            return nullptr;
        }

        // opens (or activates) a file; projectLang decides the language of .h files
        EditorDocument^ Open(String^ path, GrideProject^ project)
        {
            EditorDocument^ d = Find(path);
            if (d != nullptr) { Activate(d); return d; }
            d = gcnew EditorDocument();
            d->Project = project;
            Language pl = project != nullptr ? project->Lang : Language::C90;
            d->Lang = settings->DetectLanguage(path, pl);
            Attach(d);
            d->Editor->ApplyEnvironment(settings->Env);
            d->Editor->SetLanguage(d->Lang, settings->LanguageOf(d->Lang));
            try { d->Load(path); }
            catch (Exception^)
            {
                // unreadable file: drop the half-opened tab, let the caller report the error
                d->Editor->MarkSaved();
                Close(d);
                throw;
            }
            RefreshTab(d);   // the tab was made before Load named the file
            Activate(d);
            return d;
        }

        EditorDocument^ NewDocument(Language lang)
        {
            EditorDocument^ d = gcnew EditorDocument();
            d->Lang = lang;
            Attach(d);
            d->Editor->ApplyEnvironment(settings->Env);
            d->Editor->SetLanguage(lang, settings->LanguageOf(lang));
            d->Editor->LoadText(String::Empty);
            Activate(d);
            return d;
        }

        void Activate(EditorDocument^ d)
        {
            int i = tabs->Items->IndexOf(d->Tab);
            if (i >= 0) tabs->SelectedIndex = i;
            ShowActive();
            d->Editor->FocusEditor();
        }

        // ---- save -----------------------------------------------------------------
        bool Save(EditorDocument^ d)
        {
            if (d == nullptr) return true;
            if (d->IsUntitled) return SaveAs(d);
            try { d->Save(); }
            catch (Exception^ ex)
            {
                MessageBox::Show("Could not save " + d->FilePath + "\n" + ex->Message, "GRIDE", MessageBoxButtons::OK, MessageBoxIcon::Error);
                return false;
            }
            RefreshTab(d);
            return true;
        }

        bool SaveAs(EditorDocument^ d)
        {
            SaveFileDialog^ dlg = gcnew SaveFileDialog();
            dlg->FileName = d->Title;
            dlg->Filter = FileFilter();
            if (!d->IsUntitled) dlg->InitialDirectory = Path::GetDirectoryName(d->FilePath);
            if (dlg->ShowDialog(this) != WF::DialogResult::OK) return false;
            try { d->SaveAs(dlg->FileName); }
            catch (Exception^ ex)
            {
                MessageBox::Show(ex->Message, "GRIDE", MessageBoxButtons::OK, MessageBoxIcon::Error);
                return false;
            }
            // language may change with the new extension
            Language nl = settings->DetectLanguage(d->FilePath, d->Project != nullptr ? d->Project->Lang : Language::C90);
            if (nl != d->Lang) { d->Lang = nl; d->Editor->SetLanguage(nl, settings->LanguageOf(nl)); }
            RefreshTab(d);
            return true;
        }

        void SaveAll()
        {
            for each (EditorDocument^ d in gcnew List<EditorDocument^>(docs))
                if (d->IsDirty) Save(d);
        }

        bool AnyDirty()
        {
            for each (EditorDocument^ d in docs) if (d->IsDirty) return true;
            return false;
        }

        // ---- close ----------------------------------------------------------------
        // returns false when the user pressed Cancel
        bool Close(EditorDocument^ d)
        {
            if (d == nullptr) return true;
            if (d->IsDirty)
            {
                WF::DialogResult r = MessageBox::Show("Save changes to " + d->Title + "?", "GRIDE",
                    MessageBoxButtons::YesNoCancel, MessageBoxIcon::Question);
                if (r == WF::DialogResult::Cancel) return false;
                if (r == WF::DialogResult::Yes && !Save(d)) return false;
            }
            docs->Remove(d);
            host->Controls->Remove(d->Editor);
            tabs->Remove(d->Tab);
            delete d->Editor;
            ShowActive();
            return true;
        }

        bool CloseAll(EditorDocument^ except)
        {
            for each (EditorDocument^ d in gcnew List<EditorDocument^>(docs))
            {
                if (d == except || (except != nullptr && d->Tab->Pinned)) continue;
                if (!Close(d)) return false;
            }
            return true;
        }

        // ---- settings / theme -----------------------------------------------------
        void ApplySettings()
        {
            for each (EditorDocument^ d in docs)
            {
                d->Editor->ApplyEnvironment(settings->Env);
                d->Editor->SetLanguage(d->Lang, settings->LanguageOf(d->Lang));
            }
        }

        void ApplyTheme()
        {
            for each (EditorDocument^ d in docs) d->Editor->ApplyTheme();
            host->BackColor = ThemeManager::Current->Window;
            tabs->Invalidate();
            ThemeManager::Apply(tabsMenu);
        }

        void RefreshTab(EditorDocument^ d)
        {
            if (d == nullptr || d->Tab == nullptr) return;
            d->Tab->Text = d->Title;
            d->Tab->Dirty = d->IsDirty;
            d->Tab->ToolTip = d->IsUntitled ? d->Title : d->FilePath;
            tabs->Invalidate();
        }

        void TogglePin(EditorDocument^ d)
        {
            if (d != nullptr) tabs->Pin(d->Tab, !d->Tab->Pinned);
        }

        static String^ FileFilter()
        {
            return "Source files (*.c;*.cpp;*.shl;*.h;*.hpp;*.asm)|*.c;*.cpp;*.cc;*.shl;*.h;*.hpp;*.asm;*.s;*.sa|"
                   "C90 (*.c;*.h)|*.c;*.h|C++11 (*.cpp;*.hpp)|*.cpp;*.cc;*.cxx;*.hpp|Shalimar (*.shl)|*.shl|"
                   "Assembly (*.asm;*.s)|*.asm;*.s;*.sa|Linker command (*.cmd)|*.cmd|All files (*.*)|*.*";
        }

    private:
        List<EditorDocument^>^ docs;
        GrideSettings^ settings;
        TabStrip^ tabs;
        Panel^ host;
        StartPage^ start;
        System::Windows::Forms::ContextMenuStrip^ tabsMenu;
        EditorDocument^ menuDoc;

        void Attach(EditorDocument^ d)
        {
            docs->Add(d);
            d->Tab = tabs->Add(d->Title, d);
            d->Tab->ToolTip = d->IsUntitled ? d->Title : d->FilePath;
            d->Editor->Visible = false;
            if (EditorMenuFactory != nullptr) d->Editor->SetContextMenu(EditorMenuFactory->Invoke());
            host->Controls->Add(d->Editor);
            d->Editor->ModifiedChanged += gcnew EventHandler(this, &DocumentManager::OnEditorModified);
            d->Editor->CaretMoved += gcnew EventHandler(this, &DocumentManager::OnEditorCaret);
            d->Editor->BreakpointToggled += gcnew LineHandler(this, &DocumentManager::OnEditorBreakpoint);
        }

        void ShowActive()
        {
            EditorDocument^ a = Active;
            for each (EditorDocument^ d in docs)
                d->Editor->Visible = (d == a);
            start->Visible = docs->Count == 0;
            if (a != nullptr) a->Editor->BringToFront();
            ActiveChanged(this, EventArgs::Empty);
        }

        EditorDocument^ DocOf(Object^ editor)
        {
            for each (EditorDocument^ d in docs) if (Object::ReferenceEquals(d->Editor, editor)) return d;
            return nullptr;
        }

        void BuildTabMenu()
        {
            tabsMenu = gcnew System::Windows::Forms::ContextMenuStrip();
            AddMenu("Save", "save");
            AddMenu("Close", "close");
            AddMenu("Close All But This", nullptr);
            AddMenu("Close All Documents", nullptr);
            tabsMenu->Items->Add(gcnew ToolStripSeparator());
            AddMenu("Pin / Unpin Tab", nullptr);
            AddMenu("Copy Full Path", "copy");
            AddMenu("Open Containing Folder", "open");
        }

        void AddMenu(String^ text, String^ icon)
        {
            ToolStripMenuItem^ mi = gcnew ToolStripMenuItem(text);
            if (icon != nullptr) mi->Image = IconFactory::Get(icon);
            mi->Click += gcnew EventHandler(this, &DocumentManager::OnTabMenu);
            tabsMenu->Items->Add(mi);
        }

        void OnTabMenu(Object^ sender, EventArgs^ e)
        {
            String^ what = safe_cast<ToolStripMenuItem^>(sender)->Text;
            EditorDocument^ d = menuDoc;
            if (d == nullptr) return;
            if (what == "Save") Save(d);
            else if (what == "Close") Close(d);
            else if (what == "Close All But This") CloseAll(d);
            else if (what == "Close All Documents") CloseAll(nullptr);
            else if (what == "Pin / Unpin Tab") TogglePin(d);
            else if (what == "Copy Full Path" && !d->IsUntitled) Clipboard::SetText(d->FilePath);
            else if (what == "Open Containing Folder" && !d->IsUntitled)
                System::Diagnostics::Process::Start("explorer.exe", "/select,\"" + d->FilePath + "\"");
        }

        void OnTabSelected(Object^ sender, EventArgs^ e) { ShowActive(); }

        void OnTabCloseRequested(TabItem^ t) { Close(safe_cast<EditorDocument^>(t->Tag)); }

        void OnTabRightClicked(TabItem^ t)
        {
            menuDoc = safe_cast<EditorDocument^>(t->Tag);
            tabsMenu->Show(WF::Cursor::Position);
        }

        void OnEditorModified(Object^ sender, EventArgs^ e)
        {
            EditorDocument^ d = DocOf(sender);
            if (d != nullptr) { d->Tab->Dirty = d->IsDirty; tabs->Invalidate(); }
            // the sender is the document, so a listener can tell a save from an edit
            DirtyChanged(d != nullptr ? safe_cast<Object^>(d) : this, EventArgs::Empty);
        }

        void OnEditorCaret(Object^ sender, EventArgs^ e) { CaretMoved(this, EventArgs::Empty); }

        void OnEditorBreakpoint(int line)
        {
            BreakpointToggled(Active, line);
        }
    };
}}
