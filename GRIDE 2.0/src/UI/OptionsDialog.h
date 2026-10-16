#pragma once
// ============================================================================
//  GRIDE 2.0 - Tools > Options (General, Editor, Toolchains, Definitions, Languages,
//  RIDE Paths, Compiler Options)
//  Edits a copy of the settings; the caller takes Result on OK.
// ============================================================================
#include "CompilerOptionsEditor.h"

namespace Gride
{
    public ref class OptionsDialog : public Form
    {
    public:
        OptionsDialog(IdeSettings^ s)
        {
            work = JsonClone<IdeSettings>(s);
            work->Normalize();
            OpenAfter = nullptr;
            Ui::SetupDialog(this, "Options", 780, 560);

            tabs = gcnew TabControl();
            tabs->Location = Point(8, 8);
            tabs->Size = System::Drawing::Size(764, 506);
            Controls->Add(tabs);

            BuildGeneralPage();
            BuildEditorPage();
            BuildToolsPage();
            BuildDefinitionsPage();
            BuildLanguagesPage();
            BuildRidePathsPage();
            BuildCompilerOptionsPage();

            Button^ ok = Ui::AddButton(this, "OK", 610, 524, 78);
            ok->Click += gcnew EventHandler(this, &OptionsDialog::OnOk);
            Button^ cancel = Ui::AddButton(this, "Cancel", 694, 524, 78);
            cancel->DialogResult = System::Windows::Forms::DialogResult::Cancel;
            CancelButton = cancel;

            LoadValues();
        }

        property IdeSettings^ Result { IdeSettings^ get() { return work; } }
        // a file the caller opens in the editor after the dialog closes (Edit user overrides...)
        property String^ OpenAfter;

        void SelectToolsPage() { tabs->SelectedIndex = 2; }
        void SelectDefinitionsPage() { tabs->SelectedIndex = 3; }
        void SelectRidePathsPage() { tabs->SelectedIndex = 5; }
        void SelectCompilerOptionsPage() { tabs->SelectedIndex = 6; }

    private:
        IdeSettings^ work;
        TabControl^ tabs;
        // general
        CheckBox^ chkReopen;
        CheckBox^ chkSaveBeforeBuild;
        CheckBox^ chkJumpToFirstError;
        CheckBox^ chkAskNative;
        CheckBox^ chkRunInIde;
        TextBox^ txtRideRoot;
        // editor
        Label^ lblFont;
        NumericUpDown^ numTab;
        CheckBox^ chkSpaces;
        CheckBox^ chkLineNumbers;
        CheckBox^ chkAutoIndent;
        ListBox^ lstColors;
        Panel^ pnlSwatch;
        String^ fontName;
        float fontSize;
        // tools
        ListView^ lvTools;
        Label^ lblToolAbout;
        TextBox^ txtPath;
        TextBox^ txtArgs;
        TextBox^ txtInc;
        TextBox^ txtDef;
        TextBox^ txtLib;
        TextBox^ txtPattern;
        TextBox^ txtEnv;
        TextBox^ txtSetup;
        NumericUpDown^ numCodePage;
        Label^ lblToolStatus;
        Label^ lblToolSource;
        Button^ btnRestoreTool;
        ToolDefinition^ currentTool;
        bool loadingTool;
        // definitions (toolchain.json)
        Label^ lblShipped;
        Label^ lblUser;
        TextBox^ txtProblems;
        ListView^ lvDefs;
        // languages
        TextBox^ txtShalExt;
        TextBox^ txtShalKeywords;
        // RIDE paths (RIDE's settings.json: include, lib, vcvars, ti, tilib, includes, libraries, compiler, ccs)
        TextBox^ txtRideInclude;
        TextBox^ txtRideLib;
        TextBox^ txtVcvars;
        TextBox^ txtTiDir;
        TextBox^ txtTiLib;
        TextBox^ txtSharedIncludes;
        TextBox^ txtSharedLibraries;
        ComboBox^ cboDefaultCompiler;
        ComboBox^ cboC6747Debugger;
        CheckBox^ chkCcsEnabled;
        TextBox^ txtCcsRoot;
        // compiler options (the store a new project starts with)
        CompilerOptionStore^ optionStore;
        CompilerOptionsEditor^ optionsEditor;

        TabPage^ AddPage(String^ title)
        {
            TabPage^ p = gcnew TabPage(title);
            p->UseVisualStyleBackColor = true;
            tabs->TabPages->Add(p);
            return p;
        }

        // ------------------------------------------------------------ pages
        void BuildGeneralPage()
        {
            TabPage^ p = AddPage("General");
            chkReopen = Ui::AddCheck(p, "Reopen the last project at startup", 16, 16);
            chkSaveBeforeBuild = Ui::AddCheck(p, "Save all modified files before building", 16, 44);
            chkJumpToFirstError = Ui::AddCheck(p, "After a failed build, put the caret on the first error (as RIDE does)", 340, 16);
            chkAskNative = Ui::AddCheck(p, "Offer the vendor's assembler and linker when the project's own fail", 340, 44);
            chkRunInIde = Ui::AddCheck(p, "Run programs in the Output pane (off: a console window)", 340, 72);
            Ui::AddLabel(p, "RIDE installation (its root, or its bin):", 16, 84);
            txtRideRoot = Ui::AddText(p, 16, 108, 560);
            Button^ br = Ui::AddButton(p, "Browse...", 584, 106, 90);
            br->Click += gcnew EventHandler(this, &OptionsDialog::OnBrowseRideRoot);
            Label^ h = Ui::AddLabel(p, "A tool named without a path is looked for as RIDE looks: its environment variable ($C90, $VM6747 ...), beside GRIDE.exe,\n"
                                       "RIDE's installation (this folder, else the default roots toolchain.json names, with one of its markers in their bin), then PATH.", 16, 140);
            h->ForeColor = Color::DimGray;
            Label^ l = Ui::AddLabel(p, String::Format("Settings file: {0}", IdeSettings::SettingsPath()), 16, 190);
            l->ForeColor = Color::DimGray;
        }

        void BuildEditorPage()
        {
            TabPage^ p = AddPage("Editor");
            Ui::AddLabel(p, "Font:", 16, 16);
            lblFont = Ui::AddLabel(p, "", 110, 16);
            Button^ bf = Ui::AddButton(p, "Change...", 330, 12, 90);
            bf->Click += gcnew EventHandler(this, &OptionsDialog::OnFont);

            Ui::AddLabel(p, "Tab size:", 16, 52);
            numTab = gcnew NumericUpDown();
            numTab->Location = Point(110, 50);
            numTab->Width = 60;
            numTab->Minimum = Decimal(1);
            numTab->Maximum = Decimal(16);
            p->Controls->Add(numTab);
            chkSpaces = Ui::AddCheck(p, "Insert spaces instead of tabs", 190, 52);
            chkLineNumbers = Ui::AddCheck(p, "Show line numbers", 16, 84);
            chkAutoIndent = Ui::AddCheck(p, "Auto-indent new lines", 190, 84);

            Ui::AddLabel(p, "Colours:", 16, 124);
            lstColors = gcnew ListBox();
            lstColors->Location = Point(110, 124);
            lstColors->Size = System::Drawing::Size(200, 150);
            lstColors->Items->AddRange(gcnew array<Object^>{
                "Text", "Keyword", "Type", "Comment", "String", "Number", "Preprocessor", "Background" });
            lstColors->SelectedIndexChanged += gcnew EventHandler(this, &OptionsDialog::OnColorSelected);
            p->Controls->Add(lstColors);
            pnlSwatch = gcnew Panel();
            pnlSwatch->Location = Point(330, 124);
            pnlSwatch->Size = System::Drawing::Size(90, 26);
            pnlSwatch->BorderStyle = System::Windows::Forms::BorderStyle::FixedSingle;
            p->Controls->Add(pnlSwatch);
            Button^ bc = Ui::AddButton(p, "Change...", 330, 158, 90);
            bc->Click += gcnew EventHandler(this, &OptionsDialog::OnColor);
            Button^ br = Ui::AddButton(p, "Reset colours", 330, 190, 90);
            br->Click += gcnew EventHandler(this, &OptionsDialog::OnResetColors);
        }

        void BuildToolsPage()
        {
            TabPage^ p = AddPage("Toolchains");
            lvTools = gcnew ListView();
            lvTools->Location = Point(8, 8);
            lvTools->Size = System::Drawing::Size(740, 140);
            lvTools->View = System::Windows::Forms::View::Details;
            lvTools->FullRowSelect = true;
            lvTools->HideSelection = false;
            lvTools->MultiSelect = false;
            lvTools->GridLines = true;
            lvTools->Columns->Add("Tool", 170);
            lvTools->Columns->Add("Group", 70);
            lvTools->Columns->Add("Program", 240);
            lvTools->Columns->Add("Status", 90);
            lvTools->Columns->Add("Definition", 150);
            lvTools->SelectedIndexChanged += gcnew EventHandler(this, &OptionsDialog::OnToolSelected);
            p->Controls->Add(lvTools);

            lblToolAbout = gcnew Label();
            lblToolAbout->Location = Point(8, 154);
            lblToolAbout->Size = System::Drawing::Size(740, 30);
            lblToolAbout->ForeColor = Color::DimGray;
            p->Controls->Add(lblToolAbout);

            int y = 188;
            Ui::AddLabel(p, "Program / file:", 8, y);
            txtPath = Ui::AddText(p, 130, y, 520);
            Button^ bb = Ui::AddButton(p, "Browse...", 658, y - 2, 90);
            bb->Click += gcnew EventHandler(this, &OptionsDialog::OnBrowseTool);
            y += 30;
            Ui::AddLabel(p, "Arguments:", 8, y);
            txtArgs = Ui::AddText(p, 130, y, 618);
            y += 30;
            Ui::AddLabel(p, "Include switch:", 8, y);
            txtInc = Ui::AddText(p, 130, y, 60);
            Ui::AddLabel(p, "Define switch:", 200, y);
            txtDef = Ui::AddText(p, 290, y, 60);
            Ui::AddLabel(p, "Library switch:", 362, y);
            txtLib = Ui::AddText(p, 458, y, 60);
            Ui::AddLabel(p, "Output code page:", 530, y);
            numCodePage = gcnew NumericUpDown();
            numCodePage->Location = Point(648, y);
            numCodePage->Width = 100;
            numCodePage->Minimum = Decimal(0);
            numCodePage->Maximum = Decimal(65535);
            p->Controls->Add(numCodePage);
            y += 30;
            Ui::AddLabel(p, "Error regex:", 8, y);
            txtPattern = Ui::AddText(p, 130, y, 618);
            y += 30;
            Ui::AddLabel(p, "Environment:", 8, y);
            txtEnv = Ui::AddText(p, 130, y, 618);
            y += 30;
            Ui::AddLabel(p, "Setup script:", 8, y);
            txtSetup = Ui::AddText(p, 130, y, 520);
            Button^ bs = Ui::AddButton(p, "Browse...", 658, y - 2, 90);
            bs->Click += gcnew EventHandler(this, &OptionsDialog::OnBrowseSetup);
            y += 28;
            lblToolStatus = Ui::AddLabel(p, "", 8, y);
            y += 24;
            lblToolSource = Ui::AddLabel(p, "", 8, y);
            lblToolSource->ForeColor = Color::DimGray;
            btnRestoreTool = Ui::AddButton(p, "Restore from toolchain.json", 548, y - 4, 200);
            btnRestoreTool->Click += gcnew EventHandler(this, &OptionsDialog::OnRestoreTool);
            y += 28;
            Label^ help = gcnew Label();
            help->Location = Point(8, y);
            help->Size = System::Drawing::Size(740, 52);
            help->ForeColor = Color::DimGray;
            help->Text =
                "Placeholders: toolchain.json's $(Source) $(AsmFile) $(ObjFile) $(Objects) $(Output) $(Program) $(ArchFlag) $(ConfigFlags) ... or GRIDE's {in} {out} {objs} {libs} "
                "{crtlibs} {includes} {defines} {configflags} {flags} {linkflags} {arch} {config} {exe} {cmdfiles} {runtime} {runtimedir} {args} ... (Help > Placeholder Reference).\n"
                "Program: a full path, or a name looked for as RIDE looks (its $VARIABLE, beside GRIDE.exe, RIDE's installation, PATH). A tool follows toolchain.json until its program, "
                "arguments or switches are changed here. Environment: NAME=value;NAME2=value (%VAR% allowed). Code page 0 = OEM, 65001 = UTF-8. "
                "Error regex (optional) groups: file, line, col, sev, code, msg; RIDE's shapes, MSVC's, TI's and GCC's are built in.";
            p->Controls->Add(help);

            txtPath->TextChanged += gcnew EventHandler(this, &OptionsDialog::OnToolFieldChanged);
            txtArgs->TextChanged += gcnew EventHandler(this, &OptionsDialog::OnToolFieldChanged);
            txtInc->TextChanged += gcnew EventHandler(this, &OptionsDialog::OnToolFieldChanged);
            txtDef->TextChanged += gcnew EventHandler(this, &OptionsDialog::OnToolFieldChanged);
            txtLib->TextChanged += gcnew EventHandler(this, &OptionsDialog::OnToolFieldChanged);
            txtPattern->TextChanged += gcnew EventHandler(this, &OptionsDialog::OnToolFieldChanged);
            txtEnv->TextChanged += gcnew EventHandler(this, &OptionsDialog::OnToolFieldChanged);
            txtSetup->TextChanged += gcnew EventHandler(this, &OptionsDialog::OnToolFieldChanged);
            numCodePage->ValueChanged += gcnew EventHandler(this, &OptionsDialog::OnToolFieldChanged);
        }

        // toolchain.json: where the two files are, what is wrong in them, what they define
        void BuildDefinitionsPage()
        {
            TabPage^ p = AddPage("Definitions");
            Ui::AddLabel(p, "Shipped file:", 16, 14);
            lblShipped = Ui::AddLabel(p, "", 120, 14);
            Ui::AddLabel(p, "User overrides:", 16, 38);
            lblUser = Ui::AddLabel(p, "", 120, 38);
            Button^ reload = Ui::AddButton(p, "Reload", 16, 62, 90);
            reload->Click += gcnew EventHandler(this, &OptionsDialog::OnReloadDefinitions);
            Button^ edit = Ui::AddButton(p, "Edit user overrides...", 112, 62, 170);
            edit->Click += gcnew EventHandler(this, &OptionsDialog::OnEditUserFile);
            Button^ discard = Ui::AddButton(p, "Discard user overrides", 288, 62, 170);
            discard->Click += gcnew EventHandler(this, &OptionsDialog::OnDiscardUserFile);
            Ui::AddLabel(p, "Problems:", 16, 98);
            txtProblems = gcnew TextBox();
            txtProblems->Multiline = true;
            txtProblems->ReadOnly = true;
            txtProblems->ScrollBars = System::Windows::Forms::ScrollBars::Vertical;
            txtProblems->Location = Point(16, 118);
            txtProblems->Size = System::Drawing::Size(724, 84);
            p->Controls->Add(txtProblems);
            Ui::AddLabel(p, "The file's tools and libraries (a row marked 'user' has fields from the user's file):", 16, 212);
            lvDefs = gcnew ListView();
            lvDefs->Location = Point(16, 232);
            lvDefs->Size = System::Drawing::Size(724, 186);
            lvDefs->View = System::Windows::Forms::View::Details;
            lvDefs->FullRowSelect = true;
            lvDefs->GridLines = true;
            lvDefs->Columns->Add("Id", 90);
            lvDefs->Columns->Add("Name", 110);
            lvDefs->Columns->Add("Program / file", 150);
            lvDefs->Columns->Add("Role", 110);
            lvDefs->Columns->Add("Targets", 190);
            lvDefs->Columns->Add("From", 60);
            p->Controls->Add(lvDefs);
            Label^ h = gcnew Label();
            h->Location = Point(16, 424);
            h->Size = System::Drawing::Size(724, 44);
            h->ForeColor = Color::DimGray;
            h->Text = "toolchain.json defines every tool, its arguments, the libraries and where things are looked for; the user's copy overrides it field by field "
                      "(a field named there wins, every other keeps the shipped value; see docs\\TOOLCHAIN-FORMAT.md). The Toolchains page's entries are made from it.";
            p->Controls->Add(h);
        }

        void BuildLanguagesPage()
        {
            TabPage^ p = AddPage("Languages");
            Ui::AddLabel(p, "Shalimar file extensions (separated by ';'):", 16, 16);
            txtShalExt = Ui::AddText(p, 16, 40, 300);
            Ui::AddLabel(p, "Shalimar keywords (one per line or separated by spaces):", 16, 76);
            txtShalKeywords = gcnew TextBox();
            txtShalKeywords->Multiline = true;
            txtShalKeywords->ScrollBars = System::Windows::Forms::ScrollBars::Vertical;
            txtShalKeywords->Location = Point(16, 100);
            txtShalKeywords->Size = System::Drawing::Size(720, 340);
            txtShalKeywords->Font = gcnew System::Drawing::Font("Consolas", 9.0f);
            p->Controls->Add(txtShalKeywords);
            Label^ l = Ui::AddLabel(p, "C90 and C++11 keyword sets are built in. The Shalimar set is yours to define.", 16, 448);
            l->ForeColor = Color::DimGray;
        }

        static Label^ Section(Control^ parent, String^ text, int y)
        {
            Label^ l = Ui::AddLabel(parent, text, 8, y);
            l->Font = gcnew System::Drawing::Font(l->Font, FontStyle::Bold);
            return l;
        }

        static Label^ Note(Control^ parent, String^ text, int y, int h)
        {
            Label^ l = gcnew Label();
            l->Text = text;
            l->Location = Point(16, y);
            l->Size = System::Drawing::Size(716, h);
            l->ForeColor = Color::DimGray;
            parent->Controls->Add(l);
            return l;
        }

        // a labelled path box with a Browse button that fills it (a folder, or a file when filter is given)
        TextBox^ PathRow(Control^ parent, String^ label, int y, String^ filter)
        {
            Ui::AddLabel(parent, label, 16, y);
            TextBox^ t = Ui::AddText(parent, 220, y, 420);
            Button^ b = Ui::AddButton(parent, "Browse...", 648, y - 2, 84);
            b->Tag = filter == nullptr ? (Object^)t : (Object^)gcnew array<Object^>{ t, filter };
            b->Click += gcnew EventHandler(this, &OptionsDialog::OnBrowsePath);
            return t;
        }

        static TextBox^ LinesBox(Control^ parent, int x, int y, int w, int h)
        {
            TextBox^ t = gcnew TextBox();
            t->Multiline = true;
            t->ScrollBars = System::Windows::Forms::ScrollBars::Vertical;
            t->Location = Point(x, y);
            t->Size = System::Drawing::Size(w, h);
            parent->Controls->Add(t);
            return t;
        }

        static String^ JoinLines(List<String^>^ l) { return l == nullptr ? String::Empty : String::Join("\r\n", l->ToArray()); }
        static List<String^>^ SplitLines(String^ text)
        {
            List<String^>^ r = gcnew List<String^>();
            for each (String^ x in text->Split(gcnew array<wchar_t>{ L'\r', L'\n' }, StringSplitOptions::RemoveEmptyEntries))
            {
                String^ t = x->Trim();
                if (t->Length > 0 && !r->Contains(t)) r->Add(t);
            }
            return r;
        }

        // RIDE's settings.json, merged: the installation's header directories, Visual Studio, TI's compiler,
        // what every compile and link takes, the defaults, CCS
        void BuildRidePathsPage()
        {
            TabPage^ p = AddPage("RIDE Paths");
            p->AutoScroll = true;
            int y = 8;
            String^ root = IdeSettingsPaths::RideRootDir();
            Section(p, "RIDE's headers (settings.json include / lib)", y); y += 24;
            txtRideInclude = PathRow(p, "cpp11's headers (include):", y, nullptr); y += 28;
            txtRideLib = PathRow(p, "c90's headers (lib):", y, nullptr); y += 28;
            Note(p, String::Format("Every cpp11 compile is given the include directory and every c90 compile the lib directory, after the project's own. "
                                   "Relative to RIDE's installation ({0}); empty: the directory of that name there.", root == nullptr ? Str("none found") : root), y, 30); y += 38;

            Section(p, "Visual Studio (settings.json vcvars)", y); y += 24;
            txtVcvars = PathRow(p, "vcvars64.bat:", y, "Batch files (*.bat)|*.bat|All files (*.*)|*.*"); y += 28;
            Note(p, String::Format("Names the Visual Studio whose cl, ml64, link.exe and clang are used (its folder is taken; the batch file is never run). "
                                   "Empty: the newest installation vswhere finds. In force now: {0}.", VisualStudio::Where()), y, 30); y += 38;

            Section(p, "TI's C6000 compiler (settings.json ti / tilib)", y); y += 24;
            txtTiDir = PathRow(p, "TI compiler directory:", y, nullptr); y += 28;
            txtTiLib = PathRow(p, "TI library directory:", y, nullptr); y += 28;
            String^ found = TiCompiler::Detected();
            Note(p, String::Format("The directory with bin\\lnk6x (ti-cgt-c6000_x.y.z). Named, a TMS6747 build links against TI's runtime ({0} where it is, in its lib or the "
                                   "library directory) instead of RTS6x, and TI's lnk6x stands in for a linker that is not there. Empty: RTS6x beside GRIDE; the newest TI "
                                   "compiler found under {1} when RTS6x is not there ({2}).",
                                   ToolchainFile::Current->Ccs->RuntimeLibrary, String::Join(", ", ToolchainFile::Current->Ccs->TiRoots->ToArray()),
                                   found == nullptr ? Str("none found now") : String::Concat("found: ", found)), y, 44); y += 52;

            Section(p, "Shared (settings.json includes / libraries)", y); y += 24;
            Ui::AddLabel(p, "Header directories every compile searches after the project's (one per line):", 16, y);
            Ui::AddLabel(p, "Libraries every host link takes (one per line):", 388, y); y += 22;
            txtSharedIncludes = LinesBox(p, 16, y, 356, 64);
            txtSharedLibraries = LinesBox(p, 388, y, 344, 64); y += 74;

            Section(p, "Defaults", y); y += 24;
            Ui::AddLabel(p, "Compiler a new project starts with:", 16, y);
            cboDefaultCompiler = gcnew ComboBox();
            cboDefaultCompiler->DropDownStyle = ComboBoxStyle::DropDownList;
            cboDefaultCompiler->Location = Point(220, y);
            cboDefaultCompiler->Width = 120;
            for each (String^ w in ToolWords::Words()) cboDefaultCompiler->Items->Add(w);
            p->Controls->Add(cboDefaultCompiler);
            Ui::AddLabel(p, "TMS6747 debugger:", 388, y);
            cboC6747Debugger = gcnew ComboBox();
            cboC6747Debugger->DropDownStyle = ComboBoxStyle::DropDownList;
            cboC6747Debugger->Location = Point(512, y);
            cboC6747Debugger->Width = 220;
            for each (String^ id in DebuggerIds()) cboC6747Debugger->Items->Add(DebuggerName(id));
            p->Controls->Add(cboC6747Debugger);
            y += 30;
            Note(p, "auto: the simulator on this build's .out when it has a line table, else the emulator on the assembly (RIDE). "
                    "RIDE_DEBUG_SIM6747=1 / RIDE_DEBUG_VM6747=1 in the environment at start win.", y, 30); y += 38;

            Section(p, "Code Composer Studio 7.4 / 5.5 projects (settings.json ccs)", y); y += 24;
            chkCcsEnabled = Ui::AddCheck(p, "Open CCS projects (File > Open Project, File > Import CCS Project)", 16, y); y += 28;
            txtCcsRoot = PathRow(p, "CCS installation:", y, nullptr); y += 28;
            Note(p, "RIDE's ccs.root (C:\\ti\\ccsv7): ${CG_TOOL_ROOT} is the project's compiler version under its tools\\compiler. "
                    "Empty: TI's compiler directory above, named or found.", y, 30);
        }

        // the C6747 debugger choices: RIDE's own, the simulator, the emulator (stored as the tool's id; "" for auto)
        static array<String^>^ DebuggerIds() { return gcnew array<String^>{ String::Empty, ToolIds::TmsSim, ToolIds::TmsSimulator }; }
        static String^ DebuggerName(String^ id)
        {
            if (String::IsNullOrEmpty(id)) return "auto (RIDE's choice)";
            ToolEntry^ e = ToolIds::EntryOf(id);
            return e == nullptr ? id : e->Name;
        }

        // RIDE's Compiler Options dialog on the settings' store: what a new project starts with
        void BuildCompilerOptionsPage()
        {
            TabPage^ p = AddPage("Compiler Options");
            optionStore = work->OptionStore();
            TargetInfo^ host = nullptr;
            for each (TargetKind k in gcnew array<TargetKind>{ TargetKind::Windows, TargetKind::Linux, TargetKind::macOS })
                if (host == nullptr && TargetInfo::Get(k)->Host) host = TargetInfo::Get(k);
            optionsEditor = gcnew CompilerOptionsEditor(optionStore, "Debug", host == nullptr ? TargetKind::Windows : host->Kind);
            Label^ n = gcnew Label();
            n->Dock = DockStyle::Top;
            n->Height = 34;
            n->ForeColor = Color::DimGray;
            n->Text = "The Compiler Options a new project starts with (RIDE's settings.json options). A project's own are under Project > Compiler Options; "
                      "a CCS project's come from its configurations.";
            p->Controls->Add(optionsEditor);
            p->Controls->Add(n);
            optionsEditor->BringToFront();
        }

        void OnBrowsePath(Object^ sender, EventArgs^ e)
        {
            Button^ b = safe_cast<Button^>(sender);
            array<Object^>^ pair = dynamic_cast<array<Object^>^>(b->Tag);
            TextBox^ t = pair != nullptr ? safe_cast<TextBox^>(pair[0]) : safe_cast<TextBox^>(b->Tag);
            if (pair != nullptr)
            {
                OpenFileDialog^ d = gcnew OpenFileDialog();
                d->Filter = safe_cast<String^>(pair[1]);
                if (d->ShowDialog(this) == System::Windows::Forms::DialogResult::OK) t->Text = d->FileName;
                delete d;
                return;
            }
            FolderBrowserDialog^ f = gcnew FolderBrowserDialog();
            if (Directory::Exists(t->Text)) f->SelectedPath = t->Text;
            if (f->ShowDialog(this) == System::Windows::Forms::DialogResult::OK) t->Text = f->SelectedPath;
            delete f;
        }

        // ------------------------------------------------------------ load / save
        void LoadValues()
        {
            chkReopen->Checked = work->ReopenLastProject;
            chkSaveBeforeBuild->Checked = work->SaveBeforeBuild;
            chkJumpToFirstError->Checked = work->JumpToFirstError;
            chkAskNative->Checked = work->AskNativeTools;
            chkRunInIde->Checked = work->RunInIde;
            txtRideRoot->Text = work->RideRoot;
            fontName = work->EditorFontName;
            fontSize = work->EditorFontSize;
            lblFont->Text = String::Format("{0}, {1} pt", fontName, fontSize);
            numTab->Value = Decimal(work->TabSize);
            chkSpaces->Checked = work->InsertSpaces;
            chkLineNumbers->Checked = work->ShowLineNumbers;
            chkAutoIndent->Checked = work->AutoIndent;
            lstColors->SelectedIndex = 0;

            FillTools();
            FillDefinitions();

            txtShalExt->Text = work->ShalimarExtensions;
            txtShalKeywords->Text = String::Join("\r\n", work->ShalimarKeywords->ToArray());

            txtRideInclude->Text = work->RideIncludeDir;
            txtRideLib->Text = work->RideLibDir;
            txtVcvars->Text = work->Vcvars;
            txtTiDir->Text = work->TiCompilerDir;
            txtTiLib->Text = work->TiLibDir;
            txtSharedIncludes->Text = JoinLines(work->SharedIncludes);
            txtSharedLibraries->Text = JoinLines(work->SharedLibraries);
            cboDefaultCompiler->SelectedItem = work->DefaultCompiler;
            if (cboDefaultCompiler->SelectedIndex < 0 && cboDefaultCompiler->Items->Count > 0) cboDefaultCompiler->SelectedIndex = 0;
            cboC6747Debugger->SelectedIndex = Math::Max(0, Array::IndexOf(DebuggerIds(), work->C6747Debugger));
            chkCcsEnabled->Checked = work->CcsEnabled;
            txtCcsRoot->Text = work->CcsRoot;
        }

        void FillTools()
        {
            String^ selected = currentTool != nullptr ? currentTool->Id : nullptr;
            lvTools->BeginUpdate();
            lvTools->Items->Clear();
            for each (ToolDefinition^ t in work->Tools)
            {
                ListViewItem^ it = gcnew ListViewItem(gcnew array<String^>{ t->DisplayName, t->Category, t->ExecutablePath, t->StatusText(), DefinitionText(t) });
                it->Tag = t;
                lvTools->Items->Add(it);
                if (selected != nullptr && String::Equals(t->Id, selected)) it->Selected = true;
            }
            lvTools->EndUpdate();
            if (lvTools->SelectedItems->Count == 0 && lvTools->Items->Count > 0) lvTools->Items[0]->Selected = true;
            OnToolSelected(nullptr, nullptr);   // the event may not fire before the handle exists
        }

        static String^ DefinitionText(ToolDefinition^ t)
        {
            if (t->Custom) return "changed here";
            if (!t->Defined) return "not in toolchain.json";
            return t->Operation->Length > 0 ? String::Format("{0} > {1}", t->Definition, t->Operation) : t->Definition;
        }

        void FillDefinitions()
        {
            ToolchainFile^ f = ToolchainFile::Current;
            lblShipped->Text = f->ShippedFound ? f->ShippedFile : String::Format("{0} - not found", f->ShippedFile);
            lblShipped->ForeColor = f->ShippedFound ? SystemColors::ControlText : Color::Firebrick;
            lblUser->Text = f->UserFileExists ? f->UserFile : String::Format("{0} - none, the shipped values apply", f->UserFile);
            txtProblems->Text = f->Problems->Count == 0 ? Str("no problems in toolchain.json") : String::Join("\r\n", f->Problems->ToArray());
            txtProblems->ForeColor = f->Problems->Count == 0 ? Color::DarkGreen : Color::Firebrick;
            lvDefs->BeginUpdate();
            lvDefs->Items->Clear();
            for each (ToolEntry^ e in f->Tools)
                lvDefs->Items->Add(gcnew ListViewItem(gcnew array<String^>{ e->Id, e->Name, e->Exe, e->Role, String::Join(", ", e->Targets->ToArray()), f->IsOverridden("tools", e->Id) ? "user" : "shipped" }));
            for each (LibraryEntry^ l in f->Libraries)
                lvDefs->Items->Add(gcnew ListViewItem(gcnew array<String^>{ l->Id, l->Name, l->FileFor(false), String::Concat("library: ", l->Kind), l->Target, f->IsOverridden("libraries", l->Id) ? "user" : "shipped" }));
            lvDefs->EndUpdate();
        }

        void OnBrowseRideRoot(Object^ sender, EventArgs^ e)
        {
            FolderBrowserDialog^ d = gcnew FolderBrowserDialog();
            d->Description = "RIDE's installation: its root (with bin, lib and include under it), or its bin";
            if (d->ShowDialog(this) == System::Windows::Forms::DialogResult::OK) txtRideRoot->Text = d->SelectedPath;
            delete d;
        }

        void OnOk(Object^ sender, EventArgs^ e)
        {
            work->ReopenLastProject = chkReopen->Checked;
            work->SaveBeforeBuild = chkSaveBeforeBuild->Checked;
            work->JumpToFirstError = chkJumpToFirstError->Checked;
            work->AskNativeTools = chkAskNative->Checked;
            work->RunInIde = chkRunInIde->Checked;
            work->RideRoot = txtRideRoot->Text->Trim();
            work->EditorFontName = fontName;
            work->EditorFontSize = fontSize;
            work->TabSize = Decimal::ToInt32(numTab->Value);
            work->InsertSpaces = chkSpaces->Checked;
            work->ShowLineNumbers = chkLineNumbers->Checked;
            work->AutoIndent = chkAutoIndent->Checked;
            work->ShalimarExtensions = txtShalExt->Text->Trim();
            List<String^>^ kw = gcnew List<String^>();
            for each (String^ k in txtShalKeywords->Text->Split(gcnew array<wchar_t>{ L' ', L'\t', L'\r', L'\n', L',' }, StringSplitOptions::RemoveEmptyEntries))
                if (!kw->Contains(k)) kw->Add(k);
            work->ShalimarKeywords = kw;
            work->RideIncludeDir = txtRideInclude->Text->Trim();
            work->RideLibDir = txtRideLib->Text->Trim();
            work->Vcvars = txtVcvars->Text->Trim();
            work->TiCompilerDir = txtTiDir->Text->Trim();
            work->TiLibDir = txtTiLib->Text->Trim();
            work->SharedIncludes = SplitLines(txtSharedIncludes->Text);
            work->SharedLibraries = SplitLines(txtSharedLibraries->Text);
            work->DefaultCompiler = cboDefaultCompiler->SelectedItem == nullptr ? ToolWords::Auto : safe_cast<String^>(cboDefaultCompiler->SelectedItem);
            work->C6747Debugger = DebuggerIds()[Math::Max(0, cboC6747Debugger->SelectedIndex)];
            work->CcsEnabled = chkCcsEnabled->Checked;
            work->CcsRoot = txtCcsRoot->Text->Trim();
            optionsEditor->Commit();
            work->SetOptionStore(optionStore);
            work->Normalize();
            DialogResult = System::Windows::Forms::DialogResult::OK;
            Close();
        }

        // ------------------------------------------------------------ editor page handlers
        void OnFont(Object^ sender, EventArgs^ e)
        {
            FontDialog^ d = gcnew FontDialog();
            d->FixedPitchOnly = true;
            d->ShowEffects = false;
            try { d->Font = gcnew System::Drawing::Font(fontName, fontSize); } catch (Exception^) {}
            if (d->ShowDialog(this) == System::Windows::Forms::DialogResult::OK)
            {
                fontName = d->Font->Name;
                fontSize = d->Font->SizeInPoints;
                lblFont->Text = String::Format("{0}, {1} pt", fontName, fontSize);
            }
            delete d;
        }

        String^ GetColor(int i)
        {
            switch (i)
            {
            case 0: return work->ColorText;
            case 1: return work->ColorKeyword;
            case 2: return work->ColorType;
            case 3: return work->ColorComment;
            case 4: return work->ColorString;
            case 5: return work->ColorNumber;
            case 6: return work->ColorPreprocessor;
            default: return work->ColorBackground;
            }
        }

        void SetColor(int i, String^ v)
        {
            switch (i)
            {
            case 0: work->ColorText = v; break;
            case 1: work->ColorKeyword = v; break;
            case 2: work->ColorType = v; break;
            case 3: work->ColorComment = v; break;
            case 4: work->ColorString = v; break;
            case 5: work->ColorNumber = v; break;
            case 6: work->ColorPreprocessor = v; break;
            default: work->ColorBackground = v; break;
            }
        }

        static Color SafeColor(String^ html)
        {
            try { return ColorTranslator::FromHtml(html); } catch (Exception^) { return Color::Black; }
        }

        void OnColorSelected(Object^ sender, EventArgs^ e)
        {
            if (lstColors->SelectedIndex < 0) return;
            pnlSwatch->BackColor = SafeColor(GetColor(lstColors->SelectedIndex));
        }

        void OnColor(Object^ sender, EventArgs^ e)
        {
            int i = lstColors->SelectedIndex;
            if (i < 0) return;
            ColorDialog^ d = gcnew ColorDialog();
            d->FullOpen = true;
            d->Color = SafeColor(GetColor(i));
            if (d->ShowDialog(this) == System::Windows::Forms::DialogResult::OK)
            {
                SetColor(i, String::Format("#{0:X2}{1:X2}{2:X2}", d->Color.R, d->Color.G, d->Color.B));
                pnlSwatch->BackColor = d->Color;
            }
            delete d;
        }

        void OnResetColors(Object^ sender, EventArgs^ e)
        {
            IdeSettings^ d = gcnew IdeSettings();
            work->ColorText = d->ColorText; work->ColorKeyword = d->ColorKeyword; work->ColorType = d->ColorType;
            work->ColorComment = d->ColorComment; work->ColorString = d->ColorString; work->ColorNumber = d->ColorNumber;
            work->ColorPreprocessor = d->ColorPreprocessor; work->ColorBackground = d->ColorBackground;
            OnColorSelected(nullptr, nullptr);
        }

        // ------------------------------------------------------------ tools page handlers
        void OnToolSelected(Object^ sender, EventArgs^ e)
        {
            if (lvTools->SelectedItems->Count == 0) return;
            currentTool = safe_cast<ToolDefinition^>(lvTools->SelectedItems[0]->Tag);
            loadingTool = true;
            txtPath->Text = currentTool->ExecutablePath;
            txtArgs->Text = currentTool->Arguments;
            txtInc->Text = currentTool->IncludeSwitch;
            txtDef->Text = currentTool->DefineSwitch;
            txtLib->Text = currentTool->LibrarySwitch;
            txtPattern->Text = currentTool->ErrorPattern;
            txtEnv->Text = currentTool->Environment;
            txtSetup->Text = currentTool->SetupScript;
            numCodePage->Value = Decimal(Math::Min(65535, Math::Max(0, currentTool->OutputCodePage)));
            bool isRuntime = currentTool->IsRuntime;
            txtArgs->Enabled = !isRuntime;
            txtInc->Enabled = !isRuntime;
            txtDef->Enabled = !isRuntime;
            txtLib->Enabled = !isRuntime;
            txtPattern->Enabled = !isRuntime;
            txtEnv->Enabled = !isRuntime;
            txtSetup->Enabled = !isRuntime;
            numCodePage->Enabled = !isRuntime;
            lblToolAbout->Text = currentTool->About->Length > 0 ? currentTool->About
                               : currentTool->Defined ? String::Empty : Str("toolchain.json defines nothing for this entry - name the program here.");
            loadingTool = false;
            UpdateToolStatus();
            UpdateToolSource();
        }

        void OnToolFieldChanged(Object^ sender, EventArgs^ e)
        {
            if (loadingTool || currentTool == nullptr) return;
            currentTool->ExecutablePath = txtPath->Text->Trim();
            currentTool->Arguments = txtArgs->Text;
            currentTool->IncludeSwitch = txtInc->Text->Trim();
            currentTool->DefineSwitch = txtDef->Text->Trim();
            currentTool->LibrarySwitch = txtLib->Text->Trim();
            currentTool->ErrorPattern = txtPattern->Text;
            currentTool->Environment = txtEnv->Text->Trim();
            currentTool->SetupScript = txtSetup->Text->Trim();
            currentTool->OutputCodePage = Decimal::ToInt32(numCodePage->Value);
            // the program, arguments and switches are the file's: changing them here is leaving the file
            if (sender == txtPath || sender == txtArgs || sender == txtInc || sender == txtDef || sender == txtLib || sender == numCodePage)
            {
                currentTool->Custom = true;
                UpdateToolSource();
            }
            if (sender == txtPath) UpdateToolStatus();
            if (sender == txtPattern)
            {
                try
                {
                    if (txtPattern->Text->Trim()->Length > 0) gcnew Regex(txtPattern->Text);
                    txtPattern->BackColor = SystemColors::Window;
                }
                catch (ArgumentException^) { txtPattern->BackColor = Color::MistyRose; }
            }
        }

        void UpdateToolStatus()
        {
            if (currentTool == nullptr || lvTools->SelectedItems->Count == 0) return;
            ListViewItem^ it = lvTools->SelectedItems[0];
            String^ status = currentTool->StatusText();
            it->SubItems[2]->Text = currentTool->ExecutablePath;
            it->SubItems[3]->Text = status;
            String^ resolved = currentTool->ResolvedPath();
            lblToolStatus->Text = resolved != nullptr ? String::Format("Found: {0}", resolved)
                                : currentTool->IsBareName() ? String::Format("{0} - looked in {1}", status, currentTool->LookedIn()) : status;
            lblToolStatus->ForeColor = resolved != nullptr ? Color::DarkGreen : Color::Firebrick;
        }

        void UpdateToolSource()
        {
            if (currentTool == nullptr || lvTools->SelectedItems->Count == 0) return;
            lvTools->SelectedItems[0]->SubItems[4]->Text = DefinitionText(currentTool);
            if (currentTool->Custom)
                lblToolSource->Text = currentTool->Defined
                    ? String::Format("Changed here: toolchain.json's '{0}' is not followed until restored.", currentTool->Definition)
                    : Str("Changed here.");
            else if (currentTool->Defined)
                lblToolSource->Text = currentTool->Operation->Length > 0
                    ? String::Format("Follows toolchain.json: tool '{0}', operation '{1}'.", currentTool->Definition, currentTool->Operation)
                    : String::Format("Follows toolchain.json: library '{0}'.", currentTool->Definition);
            else lblToolSource->Text = "Not in toolchain.json.";
            btnRestoreTool->Enabled = currentTool->Custom;
        }

        // the file's definition back: the program, arguments and switches as toolchain.json gives them
        void OnRestoreTool(Object^ sender, EventArgs^ e)
        {
            if (currentTool == nullptr) return;
            ToolDefinition^ def = nullptr;
            for each (ToolDefinition^ d in IdeSettings::DefaultTools()) if (String::Equals(d->Id, currentTool->Id)) def = d;
            currentTool->TakeFrom(def);
            if (lvTools->SelectedItems->Count > 0) lvTools->SelectedItems[0]->SubItems[0]->Text = currentTool->DisplayName;
            OnToolSelected(nullptr, nullptr);
        }

        void OnBrowseSetup(Object^ sender, EventArgs^ e)
        {
            if (currentTool == nullptr) return;
            OpenFileDialog^ d = gcnew OpenFileDialog();
            d->Filter = "Batch files (*.bat;*.cmd)|*.bat;*.cmd|All files (*.*)|*.*";
            d->Title = "Locate the environment setup script (e.g. vcvars32.bat)";
            if (d->ShowDialog(this) == System::Windows::Forms::DialogResult::OK) txtSetup->Text = d->FileName;
            delete d;
        }

        void OnBrowseTool(Object^ sender, EventArgs^ e)
        {
            if (currentTool == nullptr) return;
            OpenFileDialog^ d = gcnew OpenFileDialog();
            bool isRuntime = currentTool->IsRuntime;
            d->Filter = isRuntime ? gcnew String("Libraries (*.lib;*.a)|*.lib;*.a|All files (*.*)|*.*")
                                  : gcnew String("Programs (*.exe;*.cmd;*.bat)|*.exe;*.cmd;*.bat|All files (*.*)|*.*");
            d->Title = String::Format("Locate {0}", currentTool->DisplayName);
            if (d->ShowDialog(this) == System::Windows::Forms::DialogResult::OK) txtPath->Text = d->FileName;
            delete d;
        }

        // ------------------------------------------------------------ definitions page handlers
        void OnReloadDefinitions(Object^ sender, EventArgs^ e)
        {
            ToolchainFile::Reload();
            work->Normalize();
            FillDefinitions();
            FillTools();
        }

        // the user's file, made with its header when it is not there yet, opened in the editor after OK
        void OnEditUserFile(Object^ sender, EventArgs^ e)
        {
            String^ path = ToolchainFile::Current->UserFile;
            if (!File::Exists(path))
            {
                String^ error;
                if (!ToolchainFile::WriteEmptyUserFile(path, error)) { Ui::Error(this, String::Format("The user's toolchain.json could not be written:\n{0}", error)); return; }
            }
            OpenAfter = path;
            OnOk(sender, e);
        }

        void OnDiscardUserFile(Object^ sender, EventArgs^ e)
        {
            ToolchainFile^ f = ToolchainFile::Current;
            if (!f->UserFileExists) { Ui::Info(this, "There is no user file: the shipped toolchain.json applies as it is."); return; }
            if (!Ui::Confirm(this, String::Format("Delete the user's overrides?\n{0}\n\nEvery tool then follows the shipped toolchain.json.", f->UserFile))) return;
            try { File::Delete(f->UserFile); }
            catch (Exception^ ex) { Ui::Error(this, String::Format("The file could not be deleted:\n{0}", ex->Message)); return; }
            OnReloadDefinitions(sender, e);
        }
    };
}
