#pragma once
// ============================================================================
//  GRIDE 2.0 - Tools > Options (General, Editor, Toolchains, Languages)
//  Edits a copy of the settings; the caller takes Result on OK.
// ============================================================================
#include "Dialogs.h"

namespace Gride
{
    public ref class OptionsDialog : public Form
    {
    public:
        OptionsDialog(IdeSettings^ s)
        {
            work = JsonClone<IdeSettings>(s);
            work->Normalize();
            Ui::SetupDialog(this, "Options", 780, 560);

            tabs = gcnew TabControl();
            tabs->Location = Point(8, 8);
            tabs->Size = System::Drawing::Size(764, 506);
            Controls->Add(tabs);

            BuildGeneralPage();
            BuildEditorPage();
            BuildToolsPage();
            BuildLanguagesPage();

            Button^ ok = Ui::AddButton(this, "OK", 610, 524, 78);
            ok->Click += gcnew EventHandler(this, &OptionsDialog::OnOk);
            Button^ cancel = Ui::AddButton(this, "Cancel", 694, 524, 78);
            cancel->DialogResult = System::Windows::Forms::DialogResult::Cancel;
            CancelButton = cancel;

            LoadValues();
        }

        property IdeSettings^ Result { IdeSettings^ get() { return work; } }

        void SelectToolsPage() { tabs->SelectedIndex = 2; }

    private:
        IdeSettings^ work;
        TabControl^ tabs;
        // general
        CheckBox^ chkReopen;
        CheckBox^ chkSaveBeforeBuild;
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
        ToolDefinition^ currentTool;
        bool loadingTool;
        // languages
        TextBox^ txtShalExt;
        TextBox^ txtShalKeywords;

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
            Label^ l = Ui::AddLabel(p, String::Format("Settings file: {0}", IdeSettings::SettingsPath()), 16, 84);
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
            lvTools->Size = System::Drawing::Size(740, 180);
            lvTools->View = System::Windows::Forms::View::Details;
            lvTools->FullRowSelect = true;
            lvTools->HideSelection = false;
            lvTools->MultiSelect = false;
            lvTools->GridLines = true;
            lvTools->Columns->Add("Tool", 200);
            lvTools->Columns->Add("Group", 80);
            lvTools->Columns->Add("Path", 340);
            lvTools->Columns->Add("Status", 100);
            lvTools->SelectedIndexChanged += gcnew EventHandler(this, &OptionsDialog::OnToolSelected);
            p->Controls->Add(lvTools);

            int y = 198;
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
            lblToolStatus = Ui::AddLabel(p, "", 130, y);
            y += 22;
            Label^ help = gcnew Label();
            help->Location = Point(8, y);
            help->Size = System::Drawing::Size(740, 78);
            help->ForeColor = Color::DimGray;
            help->Text =
                "Placeholders: {in} {out} {objs} {libs} {includes} {defines} {flags} {asmflags} {linkflags} {target} {config} "
                "{name} {projectdir} {outdir} {objdir} {exe} {map} {cmdfiles} {runtime}   (Help > Placeholder Reference)\n"
                "Program: full path or a name on PATH. For RTS6747 give the runtime library file. For x64 use ml64.exe.\n"
                "MASM/LINK need the Visual Studio environment: set Setup script to vcvars32.bat (or vcvars64.bat).\n"
                "Environment: NAME=value;NAME2=value (%VAR% allowed). Code page 0 = OEM console default, 65001 = UTF-8.\n"
                "Error regex (optional) groups: file, line, col, sev, code, msg. MSVC/MASM, TI and GCC formats are built in.";
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

        // ------------------------------------------------------------ load / save
        void LoadValues()
        {
            chkReopen->Checked = work->ReopenLastProject;
            chkSaveBeforeBuild->Checked = work->SaveBeforeBuild;
            fontName = work->EditorFontName;
            fontSize = work->EditorFontSize;
            lblFont->Text = String::Format("{0}, {1} pt", fontName, fontSize);
            numTab->Value = Decimal(work->TabSize);
            chkSpaces->Checked = work->InsertSpaces;
            chkLineNumbers->Checked = work->ShowLineNumbers;
            chkAutoIndent->Checked = work->AutoIndent;
            lstColors->SelectedIndex = 0;

            lvTools->BeginUpdate();
            lvTools->Items->Clear();
            for each (ToolDefinition^ t in work->Tools)
            {
                ListViewItem^ it = gcnew ListViewItem(gcnew array<String^>{ t->DisplayName, t->Category, t->ExecutablePath, t->StatusText() });
                it->Tag = t;
                lvTools->Items->Add(it);
            }
            lvTools->EndUpdate();
            if (lvTools->Items->Count > 0)
            {
                lvTools->Items[0]->Selected = true;
                OnToolSelected(nullptr, nullptr);   // the event may not fire before the handle exists
            }

            txtShalExt->Text = work->ShalimarExtensions;
            txtShalKeywords->Text = String::Join("\r\n", work->ShalimarKeywords->ToArray());
        }

        void OnOk(Object^ sender, EventArgs^ e)
        {
            work->ReopenLastProject = chkReopen->Checked;
            work->SaveBeforeBuild = chkSaveBeforeBuild->Checked;
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
            bool isRuntime = String::Equals(currentTool->Category, "Runtime");
            txtArgs->Enabled = !isRuntime;
            txtInc->Enabled = !isRuntime;
            txtDef->Enabled = !isRuntime;
            txtLib->Enabled = !isRuntime;
            txtPattern->Enabled = !isRuntime;
            txtEnv->Enabled = !isRuntime;
            txtSetup->Enabled = !isRuntime;
            numCodePage->Enabled = !isRuntime;
            loadingTool = false;
            UpdateToolStatus();
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
            lblToolStatus->Text = resolved != nullptr ? String::Format("Found: {0}", resolved) : status;
            lblToolStatus->ForeColor = resolved != nullptr ? Color::DarkGreen : Color::Firebrick;
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
            bool isRuntime = String::Equals(currentTool->Category, "Runtime");
            d->Filter = isRuntime ? gcnew String("Libraries (*.lib;*.a)|*.lib;*.a|All files (*.*)|*.*")
                                  : gcnew String("Programs (*.exe;*.cmd;*.bat)|*.exe;*.cmd;*.bat|All files (*.*)|*.*");
            d->Title = String::Format("Locate {0}", currentTool->DisplayName);
            if (d->ShowDialog(this) == System::Windows::Forms::DialogResult::OK) txtPath->Text = d->FileName;
            delete d;
        }
    };
}
