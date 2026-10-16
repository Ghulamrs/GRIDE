#pragma once
// ============================================================================
//  GRIDE 2.0 - Dialogs: input box, new project, find/replace, project
//  properties (PropertyGrid views)
// ============================================================================
#include "../Editor/CodeEditor.h"
#include "../Import/CcsImporter.h"
#include "../Import/VsImporter.h"

using namespace System::ComponentModel;

namespace Gride
{
    // ------------------------------------------------------------------ helpers
    public ref class Ui abstract sealed
    {
    public:
        static Label^ AddLabel(Control^ parent, String^ text, int x, int y)
        {
            Label^ l = gcnew Label();
            l->Text = text;
            l->AutoSize = true;
            l->Location = Point(x, y + 3);
            parent->Controls->Add(l);
            return l;
        }

        static TextBox^ AddText(Control^ parent, int x, int y, int w)
        {
            TextBox^ t = gcnew TextBox();
            t->Location = Point(x, y);
            t->Width = w;
            parent->Controls->Add(t);
            return t;
        }

        static Button^ AddButton(Control^ parent, String^ text, int x, int y, int w)
        {
            Button^ b = gcnew Button();
            b->Text = text;
            b->Location = Point(x, y);
            b->Size = System::Drawing::Size(w, 26);
            parent->Controls->Add(b);
            return b;
        }

        static CheckBox^ AddCheck(Control^ parent, String^ text, int x, int y)
        {
            CheckBox^ c = gcnew CheckBox();
            c->Text = text;
            c->AutoSize = true;
            c->Location = Point(x, y);
            parent->Controls->Add(c);
            return c;
        }

        static void SetupDialog(Form^ f, String^ title, int w, int h)
        {
            // Font first, then sizes; no auto-scaling surprises after the layout is set.
            f->AutoScaleMode = System::Windows::Forms::AutoScaleMode::None;
            f->Font = SystemFonts::MessageBoxFont;
            f->Text = title;
            f->FormBorderStyle = System::Windows::Forms::FormBorderStyle::FixedDialog;
            f->MaximizeBox = false;
            f->MinimizeBox = false;
            f->ShowInTaskbar = false;
            f->StartPosition = FormStartPosition::CenterParent;
            f->ClientSize = System::Drawing::Size(w, h);
        }

        static void Error(IWin32Window^ owner, String^ text)
        {
            MessageBox::Show(owner, text, AppInfo::Name, MessageBoxButtons::OK, MessageBoxIcon::Error);
        }

        static void Info(IWin32Window^ owner, String^ text)
        {
            MessageBox::Show(owner, text, AppInfo::Name, MessageBoxButtons::OK, MessageBoxIcon::Information);
        }

        static bool Confirm(IWin32Window^ owner, String^ text)
        {
            return MessageBox::Show(owner, text, AppInfo::Name, MessageBoxButtons::YesNo, MessageBoxIcon::Question)
                   == System::Windows::Forms::DialogResult::Yes;
        }

        // one of a list (a solution's C++ projects): the one chosen, or nullptr
        static String^ Choose(IWin32Window^ owner, String^ title, String^ prompt, List<String^>^ items)
        {
            Form^ f = gcnew Form();
            SetupDialog(f, title, 420, 300);
            Label^ l = AddLabel(f, prompt, 12, 10);
            l->MaximumSize = System::Drawing::Size(396, 0);
            ListBox^ list = gcnew ListBox();
            list->Location = Point(12, 40);
            list->Size = System::Drawing::Size(396, 186);
            for each (String^ i in items) list->Items->Add(i);
            if (list->Items->Count > 0) list->SelectedIndex = 0;
            f->Controls->Add(list);
            Button^ ok = AddButton(f, "Open", 252, 236, 75);
            ok->DialogResult = System::Windows::Forms::DialogResult::OK;
            Button^ cancel = AddButton(f, "Cancel", 333, 236, 75);
            cancel->DialogResult = System::Windows::Forms::DialogResult::Cancel;
            f->AcceptButton = ok;
            f->CancelButton = cancel;
            try
            {
                bool picked = f->ShowDialog(owner) == System::Windows::Forms::DialogResult::OK;
                return picked && list->SelectedItem != nullptr ? safe_cast<String^>(list->SelectedItem) : nullptr;
            }
            finally { delete f; }
        }

        // a read-only text window for a long reference (Help > Placeholder Reference)
        static void ShowText(IWin32Window^ owner, String^ title, String^ text)
        {
            Form^ f = gcnew Form();
            SetupDialog(f, title, 920, 560);
            f->FormBorderStyle = System::Windows::Forms::FormBorderStyle::Sizable;
            f->MaximizeBox = true;
            TextBox^ t = gcnew TextBox();
            t->Multiline = true;
            t->ReadOnly = true;
            t->WordWrap = false;
            t->ScrollBars = System::Windows::Forms::ScrollBars::Both;
            t->Font = gcnew System::Drawing::Font("Consolas", 9.0f);
            t->BackColor = SystemColors::Window;
            t->Text = text;
            t->TabStop = false;
            t->Dock = DockStyle::Fill;
            f->Controls->Add(t);
            Button^ close = gcnew Button();
            close->Text = "Close";
            close->DialogResult = System::Windows::Forms::DialogResult::Cancel;
            close->Dock = DockStyle::Bottom;
            close->Height = 30;
            f->Controls->Add(close);
            f->CancelButton = close;
            try { f->ShowDialog(owner); }
            finally { delete f; }
        }
    };

    // ------------------------------------------------------------------ input box
    public ref class InputDialog : public Form
    {
    public:
        InputDialog(String^ title, String^ prompt, String^ initial)
        {
            Ui::SetupDialog(this, title, 420, 110);
            Ui::AddLabel(this, prompt, 12, 12);
            txt = Ui::AddText(this, 12, 38, 396);
            txt->Text = initial == nullptr ? String::Empty : initial;
            Button^ ok = Ui::AddButton(this, "OK", 252, 74, 75);
            ok->DialogResult = System::Windows::Forms::DialogResult::OK;
            Button^ cancel = Ui::AddButton(this, "Cancel", 333, 74, 75);
            cancel->DialogResult = System::Windows::Forms::DialogResult::Cancel;
            AcceptButton = ok;
            CancelButton = cancel;
        }

        property String^ Value { String^ get() { return txt->Text->Trim(); } }

        static String^ Ask(IWin32Window^ owner, String^ title, String^ prompt, String^ initial)
        {
            InputDialog^ d = gcnew InputDialog(title, prompt, initial);
            try
            {
                if (d->ShowDialog(owner) == System::Windows::Forms::DialogResult::OK) return d->Value;
                return nullptr;
            }
            finally { delete d; }
        }

    protected:
        virtual void OnShown(EventArgs^ e) override
        {
            Form::OnShown(e);
            txt->Focus();
            txt->SelectAll();
        }

    private:
        TextBox^ txt;
    };

    // ------------------------------------------------------------------ new project
    public ref class NewProjectDialog : public Form
    {
    public:
        NewProjectDialog(String^ defaultLocation)
        {
            Ui::SetupDialog(this, "New Project", 520, 230);
            Ui::AddLabel(this, "Name:", 12, 14);
            txtName = Ui::AddText(this, 110, 12, 398);
            txtName->Text = "MyProject";
            Ui::AddLabel(this, "Location:", 12, 46);
            txtLocation = Ui::AddText(this, 110, 44, 310);
            txtLocation->Text = defaultLocation;
            Button^ browse = Ui::AddButton(this, "Browse...", 428, 42, 80);
            browse->Click += gcnew EventHandler(this, &NewProjectDialog::OnBrowse);

            Ui::AddLabel(this, "Language:", 12, 80);
            cboLanguage = gcnew ComboBox();
            cboLanguage->DropDownStyle = ComboBoxStyle::DropDownList;
            cboLanguage->Location = Point(110, 78);
            cboLanguage->Width = 150;
            for each (String^ n in Names::LanguageNames()) cboLanguage->Items->Add(n);
            cboLanguage->SelectedIndex = 0;
            Controls->Add(cboLanguage);

            Ui::AddLabel(this, "Target:", 280, 80);
            cboTarget = gcnew ComboBox();
            cboTarget->DropDownStyle = ComboBoxStyle::DropDownList;
            cboTarget->Location = Point(340, 78);
            cboTarget->Width = 168;
            for each (String^ n in Names::TargetNames()) cboTarget->Items->Add(n);
            cboTarget->SelectedIndex = 0;
            Controls->Add(cboTarget);

            chkMain = Ui::AddCheck(this, "Create a main source file", 110, 114);
            chkMain->Checked = true;
            chkFolder = Ui::AddCheck(this, "Create a folder for the project", 110, 140);
            chkFolder->Checked = true;

            Button^ ok = Ui::AddButton(this, "Create", 352, 190, 75);
            ok->Click += gcnew EventHandler(this, &NewProjectDialog::OnOk);
            Button^ cancel = Ui::AddButton(this, "Cancel", 433, 190, 75);
            cancel->DialogResult = System::Windows::Forms::DialogResult::Cancel;
            AcceptButton = ok;
            CancelButton = cancel;
        }

        property String^ ProjectName   { String^ get() { return txtName->Text->Trim(); } }
        property LanguageKind Language { LanguageKind get() { return Names::ParseLanguage(safe_cast<String^>(cboLanguage->SelectedItem)); } }
        property TargetKind Target     { TargetKind get() { return Names::ParseTarget(safe_cast<String^>(cboTarget->SelectedItem)); } }
        property bool CreateMainFile   { bool get() { return chkMain->Checked; } }
        property String^ ProjectFolder
        {
            String^ get()
            {
                String^ loc = txtLocation->Text->Trim();
                return chkFolder->Checked ? Path::Combine(loc, ProjectName) : loc;
            }
        }

    private:
        TextBox^ txtName;
        TextBox^ txtLocation;
        ComboBox^ cboLanguage;
        ComboBox^ cboTarget;
        CheckBox^ chkMain;
        CheckBox^ chkFolder;

        void OnBrowse(Object^ sender, EventArgs^ e)
        {
            FolderBrowserDialog^ d = gcnew FolderBrowserDialog();
            d->Description = "Choose the folder that will contain the project";
            d->SelectedPath = txtLocation->Text;
            if (d->ShowDialog(this) == System::Windows::Forms::DialogResult::OK) txtLocation->Text = d->SelectedPath;
            delete d;
        }

        void OnOk(Object^ sender, EventArgs^ e)
        {
            if (!PathUtil::IsValidFileName(ProjectName))
            {
                Ui::Error(this, "Please enter a valid project name (no \\ / : * ? \" < > |).");
                return;
            }
            String^ loc = txtLocation->Text->Trim();
            try
            {
                if (String::IsNullOrEmpty(loc) || !Path::IsPathRooted(loc)) throw gcnew ArgumentException();
                Path::GetFullPath(loc);
            }
            catch (Exception^)
            {
                Ui::Error(this, "Please enter a valid full location path.");
                return;
            }
            String^ proFile = Path::Combine(ProjectFolder, String::Concat(ProjectName, ".pro"));
            if (File::Exists(proFile))
            {
                Ui::Error(this, String::Format("A project already exists at:\n{0}", proFile));
                return;
            }
            DialogResult = System::Windows::Forms::DialogResult::OK;
            Close();
        }
    };

    // ------------------------------------------------------------------ find / replace
    public ref class FindReplaceDialog : public Form
    {
    public:
        FindReplaceDialog(Func<CodeEditor^>^ getEditor)
        {
            this->getEditor = getEditor;
            Ui::SetupDialog(this, "Find and Replace", 470, 170);
            FormBorderStyle = System::Windows::Forms::FormBorderStyle::FixedToolWindow;
            StartPosition = FormStartPosition::Manual;

            Ui::AddLabel(this, "Find what:", 10, 12);
            txtFind = Ui::AddText(this, 100, 10, 260);
            lblReplace = Ui::AddLabel(this, "Replace with:", 10, 42);
            txtReplace = Ui::AddText(this, 100, 40, 260);
            chkCase = Ui::AddCheck(this, "Match case", 100, 72);
            chkWord = Ui::AddCheck(this, "Whole word", 200, 72);
            chkUp = Ui::AddCheck(this, "Search up", 300, 72);
            lblInfo = Ui::AddLabel(this, "", 10, 136);
            lblInfo->ForeColor = Color::DimGray;

            btnNext = Ui::AddButton(this, "Find Next", 372, 8, 90);
            btnNext->Click += gcnew EventHandler(this, &FindReplaceDialog::OnFindNext);
            btnReplace = Ui::AddButton(this, "Replace", 372, 38, 90);
            btnReplace->Click += gcnew EventHandler(this, &FindReplaceDialog::OnReplace);
            btnAll = Ui::AddButton(this, "Replace All", 372, 68, 90);
            btnAll->Click += gcnew EventHandler(this, &FindReplaceDialog::OnReplaceAll);
            Button^ close = Ui::AddButton(this, "Close", 372, 132, 90);
            close->Click += gcnew EventHandler(this, &FindReplaceDialog::OnCloseClick);
            AcceptButton = btnNext;
            CancelButton = close;
        }

        property String^ SearchText { String^ get() { return txtFind->Text; } }

        void ShowFor(IWin32Window^ owner, bool replace, String^ initial)
        {
            lblReplace->Visible = replace;
            txtReplace->Visible = replace;
            btnReplace->Visible = replace;
            btnAll->Visible = replace;
            Text = replace ? Str("Replace") : Str("Find");
            if (!String::IsNullOrEmpty(initial) && initial->IndexOf(L'\n') < 0) txtFind->Text = initial;
            lblInfo->Text = String::Empty;
            if (!Visible)
            {
                Form^ of = dynamic_cast<Form^>(owner);
                if (of != nullptr) Location = Point(of->Right - Width - 40, of->Top + 120);
                Show(owner);
            }
            Activate();
            txtFind->Focus();
            txtFind->SelectAll();
        }

        bool FindNext()
        {
            CodeEditor^ ed = getEditor->Invoke();
            if (ed == nullptr || String::IsNullOrEmpty(txtFind->Text)) return false;
            bool found = ed->FindNext(txtFind->Text, chkCase->Checked, chkWord->Checked, chkUp->Checked);
            lblInfo->Text = found ? String::Empty : String::Format("'{0}' was not found.", txtFind->Text);
            return found;
        }

    protected:
        virtual void OnFormClosing(FormClosingEventArgs^ e) override
        {
            if (e->CloseReason == System::Windows::Forms::CloseReason::UserClosing) { e->Cancel = true; Hide(); }
            Form::OnFormClosing(e);
        }

    private:
        Func<CodeEditor^>^ getEditor;
        TextBox^ txtFind;
        TextBox^ txtReplace;
        Label^ lblReplace;
        Label^ lblInfo;
        CheckBox^ chkCase;
        CheckBox^ chkWord;
        CheckBox^ chkUp;
        Button^ btnNext;
        Button^ btnReplace;
        Button^ btnAll;

        void OnFindNext(Object^ sender, EventArgs^ e) { FindNext(); }

        void OnReplace(Object^ sender, EventArgs^ e)
        {
            CodeEditor^ ed = getEditor->Invoke();
            if (ed == nullptr || String::IsNullOrEmpty(txtFind->Text)) return;
            if (ed->IsReadOnly) { lblInfo->Text = "The document is read-only."; return; }
            bool found = ed->ReplaceCurrent(txtFind->Text, txtReplace->Text, chkCase->Checked, chkWord->Checked, chkUp->Checked);
            lblInfo->Text = found ? String::Empty : Str("No more matches.");
        }

        void OnReplaceAll(Object^ sender, EventArgs^ e)
        {
            CodeEditor^ ed = getEditor->Invoke();
            if (ed == nullptr || String::IsNullOrEmpty(txtFind->Text)) return;
            if (ed->IsReadOnly) { lblInfo->Text = "The document is read-only."; return; }
            int n = ed->ReplaceAll(txtFind->Text, txtReplace->Text, chkCase->Checked, chkWord->Checked);
            lblInfo->Text = String::Format("{0} occurrence(s) replaced.", n);
        }

        void OnCloseClick(Object^ sender, EventArgs^ e) { Hide(); }
    };

    // ------------------------------------------------------------------ property views
    [DefaultProperty("Name")]
    public ref class ProjectPropertiesView
    {
    public:
        [Category("General"), Description("Project name.")]
        property String^ Name;
        [Category("General"), Description("Default language for new files (C90, C++11 or Shalimar). Each file is compiled by the compiler matching its extension.")]
        property LanguageKind Language;
        [Category("General"), Description("Target platform: Windows (MASM/LINK), Linux, macOS, or TMS6747 DSP (ASM6x/LNK6x/RTS6747).")]
        property TargetKind Target;
        [Category("General"), Description("RIDE's toolchain word: auto (each file's own compiler), c90, cpp11 or shalimar (cc1, cxx1 and shc are the same). A compiler named for a file it does not take refuses the build.")]
        property String^ Toolchain;
        [Category("General"), Description("RIDE's emulateOnly (TMS6747): the build stops at the assembly, which vm6747 runs - no asm6x, no lnk6x, no .out.")]
        property bool EmulateOnly;
        [Category("Output"), Description("Output file name without extension.")]
        property String^ OutputName;
        [Category("Run"), Description("The program's arguments as a command line: separated by spaces, one that holds a space in double quotes. Each reaches the program whole. The C6747 simulator takes none - Emulate hands them to the program.")]
        property String^ ProgramArguments;
        [Category("Paths"), Description("Include folders, separated by ';'. Relative to the project folder.")]
        property String^ IncludePaths;
        [Category("Paths"), Description("Libraries to link, separated by ';'.")]
        property String^ Libraries;
        [Category("Active Configuration"), ReadOnly(true), Description("Change the active configuration from the toolbar.")]
        property String^ Configuration;
        [Category("Active Configuration"), Description("Preprocessor defines, separated by ';'.")]
        property String^ Defines;
        [Category("Active Configuration"), Description("Extra compiler flags ({flags}).")]
        property String^ CompilerFlags;
        [Category("Active Configuration"), Description("Extra assembler flags ({asmflags}).")]
        property String^ AssemblerFlags;
        [Category("Active Configuration"), Description("Extra linker flags ({linkflags}).")]
        property String^ LinkerFlags;
        [Category("Active Configuration"), Description("Output folder. You can use $(Target), $(Config) and $(Name).")]
        property String^ OutputDirectory;

        static ProjectPropertiesView^ FromProject(GrideProject^ p)
        {
            ProjectPropertiesView^ v = gcnew ProjectPropertiesView();
            BuildConfiguration^ c = p->ActiveConfig();
            v->Name = p->Name;
            v->Language = p->LanguageValue;
            v->Target = p->TargetValue;
            v->Toolchain = p->Toolchain;
            v->EmulateOnly = p->EmulateOnly;
            v->OutputName = p->OutputName;
            v->ProgramArguments = PathUtil::JoinArguments(p->Arguments());
            v->IncludePaths = PathUtil::JoinList(p->IncludePaths);
            v->Libraries = PathUtil::JoinList(p->Libraries);
            v->Configuration = c->Name;
            v->Defines = PathUtil::JoinList(c->Defines);
            v->CompilerFlags = c->Flags;
            v->AssemblerFlags = c->AsmFlags;
            v->LinkerFlags = c->LinkFlags;
            v->OutputDirectory = c->OutputDir;
            return v;
        }

        void ApplyTo(GrideProject^ p)
        {
            if (!String::IsNullOrWhiteSpace(Name)) p->Name = Name->Trim();
            p->Language = Names::LanguageName(Language);
            p->Target = Names::TargetName(Target);
            p->Toolchain = ToolWords::Canonical(Toolchain);
            p->EmulateOnly = EmulateOnly;
            p->OutputName = String::IsNullOrWhiteSpace(OutputName) ? p->Name : OutputName->Trim();
            p->ProgramArgs = PathUtil::SplitArguments(ProgramArguments);
            p->IncludePaths = PathUtil::SplitList(IncludePaths);
            p->Libraries = PathUtil::SplitList(Libraries);
            BuildConfiguration^ c = p->ActiveConfig();
            c->Defines = PathUtil::SplitList(Defines);
            c->Flags = CompilerFlags == nullptr ? String::Empty : CompilerFlags->Trim();
            c->AsmFlags = AssemblerFlags == nullptr ? String::Empty : AssemblerFlags->Trim();
            c->LinkFlags = LinkerFlags == nullptr ? String::Empty : LinkerFlags->Trim();
            c->OutputDir = OutputDirectory;
            p->Normalize();
        }
    };

    // a project read from another IDE (CCS, Visual Studio): read-only but for the run - the program's
    // arguments and RIDE's emulateOnly, kept in GRIDE's settings (RIDE's ccs.projects; a Visual Studio
    // project's by its project file), never written into the other IDE's files
    [DefaultProperty("ProgramArguments")]
    public ref class ImportedProjectPropertiesView
    {
    public:
        [Category("Project"), ReadOnly(true), Description("The project's name.")]
        property String^ Name;
        [Category("Project"), ReadOnly(true), Description("Where the project was read from: a CCS project's folder, a Visual Studio project's file. GRIDE writes nothing into its files.")]
        property String^ ReadFrom;
        [Category("Project"), ReadOnly(true), Description("The configuration built. Change it from the toolbar.")]
        property String^ Configuration;
        [Category("Build"), Description("The compiler: auto (RIDE's, by each file's language: c90, cpp11, shalimar), c90, cpp11, or msvc - Visual Studio's own cl (cl is the same word), which a Visual Studio project is written for. Kept with GRIDE's state for this project (its .pro, else the settings), never in its files.")]
        property String^ Toolchain;
        [Category("Run"), Description("The program's arguments as a command line: separated by spaces, one that holds a space in double quotes. Kept with GRIDE's state for this project (its .pro, else the settings), not in its files.")]
        property String^ ProgramArguments;
        [Category("Run"), Description("RIDE's emulateOnly (a TMS6747 build): the build stops at the assembly, which vm6747 runs - no asm6x, no lnk6x, no .out. Kept with GRIDE's state for this project.")]
        property bool EmulateOnly;

        static ImportedProjectPropertiesView^ FromProject(GrideProject^ p)
        {
            ImportedProjectPropertiesView^ v = gcnew ImportedProjectPropertiesView();
            v->Name = p->Name;
            v->ReadFrom = p->IsVs ? (p->Vs->StateFile->Length > 0 ? String::Format("{0} (through {1})", p->FilePath, p->Vs->StateFile) : p->FilePath) : p->ProjectDir;
            v->Toolchain = p->Toolchain;
            v->Configuration = p->ActiveConfig()->Name;
            v->ProgramArguments = PathUtil::JoinArguments(p->Arguments());
            v->EmulateOnly = p->EmulateOnly;
            return v;
        }

        void ApplyTo(GrideProject^ p)
        {
            p->ProgramArgs = PathUtil::SplitArguments(ProgramArguments);
            p->EmulateOnly = EmulateOnly;
            p->Toolchain = ToolWords::Canonical(Toolchain);
            p->Normalize();
        }
    };

    public ref class FileInfoView
    {
    public:
        FileInfoView(String^ path, GrideProject^ project, IdeSettings^ s)
        {
            fileName = Path::GetFileName(path);
            fullPath = path;
            relative = project != nullptr ? project->RelativePath(path) : path;
            exists = File::Exists(path);
            if (exists)
            {
                FileInfo^ fi = gcnew FileInfo(path);
                size = fi->Length;
                modified = fi->LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss");
            }
            else modified = "(missing)";
            String^ ext = Path::GetExtension(path);
            String^ comp = TargetInfo::CompilerFor(ext, s);
            if (comp != nullptr)                                   kind = "Source (compiled)";
            else if (TargetInfo::IsAssemblySource(ext))            kind = "Assembly source";
            else if (TargetInfo::IsHeader(ext))                    kind = "Header";
            else if (project != nullptr && TargetInfo::IsLinkerCommandFile(ext, project->TargetValue)) kind = "Linker command file";
            else                                                   kind = "Other (not built)";
            language = LanguageDefs::ModeName(LanguageDefs::ModeForFile(path, s, project));
        }

        [Category("File"), ReadOnly(true)] property String^ FileName { String^ get() { return fileName; } }
        [Category("File"), ReadOnly(true)] property String^ FullPath { String^ get() { return fullPath; } }
        [Category("File"), ReadOnly(true)] property String^ RelativePath { String^ get() { return relative; } }
        [Category("File"), ReadOnly(true)] property bool Exists { bool get() { return exists; } }
        [Category("File"), ReadOnly(true)] property Int64 SizeBytes { Int64 get() { return size; } }
        [Category("File"), ReadOnly(true)] property String^ Modified { String^ get() { return modified; } }
        [Category("Build"), ReadOnly(true)] property String^ ItemType { String^ get() { return kind; } }
        [Category("Build"), ReadOnly(true)] property String^ Language { String^ get() { return language; } }

    private:
        String^ fileName; String^ fullPath; String^ relative; bool exists; Int64 size;
        String^ modified; String^ kind; String^ language;
    };

    // ------------------------------------------------------------------ project properties
    public ref class ProjectPropertiesDialog : public Form
    {
    public:
        ProjectPropertiesDialog(GrideProject^ project)
        {
            Ui::SetupDialog(this, String::Format("{0} - Project Properties", project->Name), 560, 480);
            FormBorderStyle = System::Windows::Forms::FormBorderStyle::Sizable;
            MinimumSize = System::Drawing::Size(420, 360);
            view = ProjectPropertiesView::FromProject(project);
            ccsView = project->IsImported ? ImportedProjectPropertiesView::FromProject(project) : nullptr;
            bool editable = !project->IsReadOnly || ccsView != nullptr;     // an imported project's run settings are GRIDE's
            if (project->IsReadOnly) TypeDescriptor::AddAttributes(view, gcnew ReadOnlyAttribute(true));

            grid = gcnew PropertyGrid();
            grid->Dock = DockStyle::Fill;
            grid->ToolbarVisible = false;
            grid->PropertySort = PropertySort::Categorized;
            grid->SelectedObject = ccsView != nullptr ? safe_cast<Object^>(ccsView) : safe_cast<Object^>(view);

            // Buttons in a right-to-left flow panel: they always stay visible at the right.
            FlowLayoutPanel^ bottom = gcnew FlowLayoutPanel();
            bottom->Dock = DockStyle::Bottom;
            bottom->Height = 44;
            bottom->FlowDirection = FlowDirection::RightToLeft;
            bottom->Padding = System::Windows::Forms::Padding(8, 8, 8, 8);
            Button^ cancel = gcnew Button();
            cancel->Text = editable ? Str("Cancel") : Str("Close");
            cancel->Size = System::Drawing::Size(80, 26);
            cancel->DialogResult = System::Windows::Forms::DialogResult::Cancel;
            Button^ ok = gcnew Button();
            ok->Text = "OK";
            ok->Size = System::Drawing::Size(80, 26);
            ok->DialogResult = System::Windows::Forms::DialogResult::OK;
            ok->Visible = editable;
            bottom->Controls->Add(cancel);
            bottom->Controls->Add(ok);

            Controls->Add(grid);
            Controls->Add(bottom);
            AcceptButton = ok;
            CancelButton = cancel;
        }

        property ProjectPropertiesView^ View { ProjectPropertiesView^ get() { return view; } }
        property ImportedProjectPropertiesView^ ImportedView { ImportedProjectPropertiesView^ get() { return ccsView; } }

    private:
        ProjectPropertiesView^ view;
        ImportedProjectPropertiesView^ ccsView;
        PropertyGrid^ grid;
    };
}
