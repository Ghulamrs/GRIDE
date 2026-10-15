// =============================================================================
//  OptionsDialog.h - Tools > Options  (everything in gride.settings.json)
//
//    Environment  > General | Fonts and Colors | Keyboard
//    Toolchains   > Windows | Linux | macOS | TMS320C6747
//    Languages    > C90 | C++11 | Shalimar | Assembly
//    Converters   > C <-> Shalimar
//    Projects     > Defaults and Locations
//
//  The dialog edits a CLONE of the settings; Result holds it after OK.
// =============================================================================
#pragma once
#include "DialogBase.h"
#include "../Commands.h"
#include "../../Settings/GrideSettings.h"

namespace Gride { namespace UI {

    using namespace Gride::Core;
    using namespace Gride::Settings;

    // ---- a text box + browse button + status label -------------------------------
    public ref class PathField : public Panel
    {
    public:
        PathField(bool folder, bool showStatus)
        {
            isFolder = folder;
            Height = 28;
            BaseDir = String::Empty;
            box = gcnew TextBox();
            box->Dock = DockStyle::Fill;
            box->TextChanged += gcnew EventHandler(this, &PathField::OnChanged);
            Button^ b = gcnew Button();
            b->Text = "...";
            b->Width = 34;
            b->Dock = DockStyle::Right;
            b->Click += gcnew EventHandler(this, &PathField::OnBrowse);
            status = gcnew Label();
            status->Width = showStatus ? 112 : 0;
            status->Dock = DockStyle::Right;
            status->TextAlign = ContentAlignment::MiddleLeft;
            status->Tag = "keep";
            status->Padding = WF::Padding(6, 0, 0, 0);
            Controls->Add(box);
            Controls->Add(b);
            Controls->Add(status);
        }

        property String^ Value { String^ get() { return box->Text->Trim(); } void set(String^ v) { box->Text = v; } }
        property String^ BaseDir;      // relative names are resolved against this
        property String^ Filter;

        void UpdateStatus()
        {
            if (status->Width == 0) return;
            String^ v = Environment::ExpandEnvironmentVariables(Value);
            if (v->Length == 0) { status->Text = "(not set)"; status->ForeColor = ThemeManager::Current->MutedText; return; }
            String^ full = v;
            try { if (!Path::IsPathRooted(v) && !String::IsNullOrEmpty(BaseDir)) full = Path::Combine(Environment::ExpandEnvironmentVariables(BaseDir), v); }
            catch (Exception^) { }
            bool ok = isFolder ? Directory::Exists(full) : File::Exists(full);
            if (ok) { status->Text = gcnew String(L'\x2713', 1) + " Found"; status->ForeColor = ThemeManager::Current->SuccessText; }
            else { status->Text = "! Not installed"; status->ForeColor = ThemeManager::Current->WarningText; }
        }

    private:
        bool isFolder;
        TextBox^ box;
        Label^ status;

        void OnChanged(Object^ sender, EventArgs^ e) { UpdateStatus(); }

        void OnBrowse(Object^ sender, EventArgs^ e)
        {
            if (isFolder)
            {
                FolderBrowserDialog^ d = gcnew FolderBrowserDialog();
                if (Directory::Exists(Value)) d->SelectedPath = Value;
                if (d->ShowDialog(FindForm()) == WF::DialogResult::OK) Value = d->SelectedPath;
            }
            else
            {
                OpenFileDialog^ d = gcnew OpenFileDialog();
                if (String::IsNullOrEmpty(Filter)) d->Filter = "Programs (*.exe)|*.exe|All files (*.*)|*.*";
                else d->Filter = Filter;
                if (d->ShowDialog(FindForm()) == WF::DialogResult::OK)
                {
                    String^ f = d->FileName;
                    // keep it short when the file is inside the base folder
                    if (!String::IsNullOrEmpty(BaseDir) && PathUtil::SamePath(Path::GetDirectoryName(f), BaseDir)) f = Path::GetFileName(f);
                    Value = f;
                }
            }
        }
    };

    // ---- base class of every page --------------------------------------------------
    public ref class OptionsPage abstract : public Panel
    {
    public:
        OptionsPage()
        {
            Dock = DockStyle::Fill;
            AutoScroll = true;
            grid = gcnew TableLayoutPanel();
            grid->ColumnCount = 2;
            grid->ColumnStyles->Add(gcnew ColumnStyle(SizeType::Absolute, 170));
            grid->ColumnStyles->Add(gcnew ColumnStyle(SizeType::Percent, 100));
            grid->Dock = DockStyle::Top;
            grid->AutoSize = true;
            grid->Padding = WF::Padding(4, 4, 12, 12);
            Controls->Add(grid);
        }
        virtual void Commit() abstract;

    protected:
        TableLayoutPanel^ grid;

        void Row(String^ label, Control^ c)
        {
            Label^ l = gcnew Label();
            l->Text = label;
            l->Dock = DockStyle::Fill;
            l->TextAlign = ContentAlignment::MiddleLeft;
            c->Dock = DockStyle::Fill;
            c->Margin = WF::Padding(3, 3, 3, 3);
            grid->RowCount++;
            grid->RowStyles->Add(gcnew RowStyle(SizeType::AutoSize));
            grid->Controls->Add(l, 0, grid->RowCount - 1);
            grid->Controls->Add(c, 1, grid->RowCount - 1);
        }

        void Section(String^ text)
        {
            Label^ l = gcnew Label();
            l->Text = text;
            l->Font = gcnew SD::Font("Segoe UI Semibold", 10.0f);
            l->Height = 30;
            l->Dock = DockStyle::Fill;
            l->TextAlign = ContentAlignment::BottomLeft;
            grid->RowCount++;
            grid->RowStyles->Add(gcnew RowStyle(SizeType::AutoSize));
            grid->Controls->Add(l, 0, grid->RowCount - 1);
            grid->SetColumnSpan(l, 2);
        }

        static TextBox^ Text1(String^ v) { TextBox^ t = gcnew TextBox(); t->Text = v; return t; }
        static TextBox^ Mono(String^ v)
        {
            TextBox^ t = gcnew TextBox(); t->Text = v; t->Font = gcnew SD::Font("Consolas", 9.0f); return t;
        }
        static TextBox^ Lines(List<String^>^ items, int height)
        {
            TextBox^ t = gcnew TextBox();
            t->Multiline = true;
            t->ScrollBars = WF::ScrollBars::Vertical;
            t->Height = height;
            t->Font = gcnew SD::Font("Consolas", 9.0f);
            t->Text = String::Join("\r\n", items);
            return t;
        }
        static List<String^>^ FromLines(TextBox^ t)
        {
            List<String^>^ r = gcnew List<String^>();
            for each (String^ s in t->Text->Split(gcnew array<wchar_t>{ L'\r', L'\n', L';' }, StringSplitOptions::RemoveEmptyEntries))
                if (s->Trim()->Length > 0) r->Add(s->Trim());
            return r;
        }
        static CheckBox^ Check(String^ text, bool v)
        {
            CheckBox^ c = gcnew CheckBox(); c->Text = text; c->Checked = v; c->AutoSize = true; return c;
        }
    };

    // ---- Environment > General ---------------------------------------------------------
    public ref class GeneralPage : public OptionsPage
    {
    public:
        GeneralPage(GrideSettings^ s)
        {
            env = s->Env;
            Section("Appearance");
            theme = gcnew ComboBox();
            theme->DropDownStyle = ComboBoxStyle::DropDownList;
            for each (String^ n in Theme::Names()) theme->Items->Add(n);
            theme->SelectedItem = env->Theme;
            if (theme->SelectedIndex < 0) theme->SelectedIndex = 0;
            Row("Colour theme", theme);
            Section("Text editor");
            tabSize = gcnew NumericUpDown(); tabSize->Minimum = Decimal(1); tabSize->Maximum = Decimal(16); tabSize->Value = Decimal(Math::Min(16, Math::Max(1, env->TabSize)));
            Row("Tab size", tabSize);
            insertSpaces = Check("Insert spaces instead of tabs", env->InsertSpaces); Row(String::Empty, insertSpaces);
            lineNumbers = Check("Show line numbers", env->LineNumbers); Row(String::Empty, lineNumbers);
            whitespace = Check("Show white space", env->ShowWhitespace); Row(String::Empty, whitespace);
            Section("Start-up and build");
            reopen = Check("Reopen the last workspace at start-up", env->ReopenLastWorkspace); Row(String::Empty, reopen);
            saveBefore = Check("Save all files before building", env->SaveBeforeBuild); Row(String::Empty, saveBefore);
            Section("Settings file");
            TextBox^ path = Text1(s->FilePath);
            path->ReadOnly = true;
            String^ fileKind = "User (%APPDATA%)";
            if (s->IsPortable) fileKind = "Portable (next to exe)";
            Row(fileKind, path);
        }
        virtual void Commit() override
        {
            env->Theme = safe_cast<String^>(theme->SelectedItem);
            env->TabSize = Decimal::ToInt32(tabSize->Value);
            env->InsertSpaces = insertSpaces->Checked;
            env->LineNumbers = lineNumbers->Checked;
            env->ShowWhitespace = whitespace->Checked;
            env->ReopenLastWorkspace = reopen->Checked;
            env->SaveBeforeBuild = saveBefore->Checked;
        }
    private:
        EnvironmentSettings^ env;
        ComboBox^ theme;
        NumericUpDown^ tabSize;
        CheckBox^ insertSpaces; CheckBox^ lineNumbers; CheckBox^ whitespace; CheckBox^ reopen; CheckBox^ saveBefore;
    };

    // ---- Environment > Fonts and Colors ----------------------------------------------------
    public ref class FontsColorsPage : public OptionsPage
    {
    public:
        FontsColorsPage(GrideSettings^ s)
        {
            env = s->Env;
            colors = env->SyntaxColors != nullptr ? gcnew Dictionary<String^, String^>(env->SyntaxColors) : gcnew Dictionary<String^, String^>();
            font = gcnew ComboBox();
            for each (FontFamily^ f in FontFamily::Families) font->Items->Add(f->Name);
            font->Text = env->FontName;
            Row("Editor font", font);
            size = gcnew NumericUpDown(); size->DecimalPlaces = 1; size->Minimum = Decimal(6); size->Maximum = Decimal(40);
            size->Increment = Decimal(0.5); size->Value = Decimal(Math::Min(40.0f, Math::Max(6.0f, env->FontSize)));
            Row("Size (pt)", size);
            Section("Syntax colours (override the theme)");
            swatches = gcnew List<Button^>();
            for each (String^ token in Theme::TokenNames())
            {
                Button^ b = gcnew Button();
                b->Tag = "keep";
                b->Name = token;
                b->FlatStyle = WF::FlatStyle::Flat;
                b->Height = 26;
                b->Click += gcnew EventHandler(this, &FontsColorsPage::OnPick);
                swatches->Add(b);
                Row(token, b);
            }
            Button^ reset = gcnew Button();
            reset->Text = "Reset to theme defaults";
            reset->Click += gcnew EventHandler(this, &FontsColorsPage::OnReset);
            Row(String::Empty, reset);
            UpdateSwatches();
        }
        virtual void Commit() override
        {
            if (font->Text->Trim()->Length > 0) env->FontName = font->Text->Trim();
            env->FontSize = (float)Decimal::ToDouble(size->Value);
            env->SyntaxColors = colors;
        }
    private:
        EnvironmentSettings^ env;
        Dictionary<String^, String^>^ colors;
        ComboBox^ font;
        NumericUpDown^ size;
        List<Button^>^ swatches;

        Color Effective(String^ token)
        {
            String^ hex;
            if (colors->TryGetValue(token, hex) && !String::IsNullOrEmpty(hex))
            {
                try { return Theme::Hex(hex); } catch (Exception^) { }
            }
            return Theme::ByName(env->Theme)->GetToken(token);
        }
        void UpdateSwatches()
        {
            for each (Button^ b in swatches)
            {
                Color c = Effective(b->Name);
                b->BackColor = Theme::ByName(env->Theme)->Window;
                b->ForeColor = c;
                b->Text = String::Format("{0}   #{1:X2}{2:X2}{3:X2}", b->Name, c.R, c.G, c.B);
            }
        }
        void OnPick(Object^ sender, EventArgs^ e)
        {
            Button^ b = safe_cast<Button^>(sender);
            ColorDialog^ d = gcnew ColorDialog();
            d->Color = Effective(b->Name);
            d->FullOpen = true;
            if (d->ShowDialog(FindForm()) != WF::DialogResult::OK) return;
            colors[b->Name] = String::Format("#{0:X2}{1:X2}{2:X2}", d->Color.R, d->Color.G, d->Color.B);
            UpdateSwatches();
        }
        void OnReset(Object^ sender, EventArgs^ e) { colors->Clear(); UpdateSwatches(); }
    };

    // ---- Environment > Keyboard ---------------------------------------------------------
    public ref class KeyboardPage : public OptionsPage
    {
    public:
        KeyboardPage(GrideSettings^ s, CommandRegistry^ reg)
        {
            settings = s; registry = reg;
            bindings = gcnew Dictionary<String^, String^>(s->KeyBindings);
            list = gcnew ListView();
            list->View = WF::View::Details;
            list->FullRowSelect = true;
            list->HideSelection = false;
            list->Height = 300;
            list->Columns->Add("Menu", 80);
            list->Columns->Add("Command", 250);
            list->Columns->Add("Shortcut", 140);
            list->SelectedIndexChanged += gcnew EventHandler(this, &KeyboardPage::OnSelect);
            Row("Commands", list);
            press = gcnew TextBox();
            press->ReadOnly = true;
            press->KeyDown += gcnew KeyEventHandler(this, &KeyboardPage::OnPress);
            Row("Press new shortcut", press);
            FlowLayoutPanel^ btns = gcnew FlowLayoutPanel();
            btns->Height = 36;
            Button^ assign = gcnew Button(); assign->Text = "Assign"; assign->Click += gcnew EventHandler(this, &KeyboardPage::OnAssign);
            Button^ remove = gcnew Button(); remove->Text = "Remove"; remove->Click += gcnew EventHandler(this, &KeyboardPage::OnRemove);
            Button^ reset = gcnew Button(); reset->Text = "Reset All"; reset->Click += gcnew EventHandler(this, &KeyboardPage::OnResetAll);
            btns->Controls->Add(assign); btns->Controls->Add(remove); btns->Controls->Add(reset);
            Row(String::Empty, btns);
            Fill();
        }
        virtual void Commit() override { settings->KeyBindings = bindings; }

    private:
        GrideSettings^ settings;
        CommandRegistry^ registry;
        Dictionary<String^, String^>^ bindings;
        ListView^ list;
        TextBox^ press;

        String^ ShortcutOf(Command^ c)
        {
            String^ v;
            if (bindings->TryGetValue(c->Id, v)) return v;
            return c->DefaultShortcut;
        }
        void Fill()
        {
            list->BeginUpdate();
            list->Items->Clear();
            for each (Command^ c in registry->All)
            {
                ListViewItem^ it = gcnew ListViewItem(c->Category);
                it->SubItems->Add(c->PlainText);
                it->SubItems->Add(ShortcutOf(c));
                it->Tag = c;
                list->Items->Add(it);
            }
            list->EndUpdate();
        }
        Command^ Current()
        {
            if (list->SelectedItems->Count == 0) return nullptr;
            return safe_cast<Command^>(list->SelectedItems[0]->Tag);
        }
        void OnSelect(Object^ sender, EventArgs^ e) { Command^ c = Current(); if (c != nullptr) press->Text = ShortcutOf(c); }
        void OnPress(Object^ sender, KeyEventArgs^ e)
        {
            e->SuppressKeyPress = true;
            Keys k = e->KeyData;
            Keys code = k & Keys::KeyCode;
            if (code == Keys::ControlKey || code == Keys::ShiftKey || code == Keys::Menu) return;
            press->Text = KeyText::Format(k);
        }
        void OnAssign(Object^ sender, EventArgs^ e)
        {
            Command^ c = Current();
            if (c == nullptr || press->Text->Length == 0) return;
            for each (Command^ o in registry->All)
                if (o != c && String::Compare(ShortcutOf(o), press->Text, true) == 0)
                {
                    if (MessageBox::Show(FindForm(), press->Text + " is used by '" + o->PlainText + "'. Assign it anyway?",
                                         "Keyboard", MessageBoxButtons::YesNo) != WF::DialogResult::Yes) return;
                    bindings[o->Id] = String::Empty;
                }
            bindings[c->Id] = press->Text;
            Fill();
        }
        void OnRemove(Object^ sender, EventArgs^ e)
        {
            Command^ c = Current();
            if (c != nullptr) { bindings[c->Id] = String::Empty; Fill(); }
        }
        void OnResetAll(Object^ sender, EventArgs^ e) { bindings->Clear(); Fill(); }
    };

    // ---- Toolchains > <target> ------------------------------------------------------------
    public ref class ToolchainPage : public OptionsPage
    {
    public:
        ToolchainPage(ToolchainSettings^ tc)
        {
            this->tc = tc;
            fields = gcnew List<PathField^>();
            Build();
        }
        virtual void Commit() override
        {
            tc->ToolDir = toolDir->Value;
            tc->CompilerC90 = c90->Value; tc->CompilerCpp11 = cpp11->Value; tc->CompilerShalimar = shl->Value;
            tc->Assembler = asmb->Value; tc->Linker = lnk->Value; tc->Runtime = rts->Value; tc->Simulator = sim->Value;
            tc->IncludeDirs = FromLines(inc); tc->LibDirs = FromLines(lib);
            tc->CompileCmd = cmdCompile->Text; tc->AssembleCmd = cmdAsm->Text; tc->LinkCmd = cmdLink->Text;
            tc->RunCmd = cmdRun->Text; tc->DebugCmd = cmdDebug->Text;
            tc->IncludeFlag = fInc->Text; tc->DefineFlag = fDef->Text; tc->LibDirFlag = fLibDir->Text; tc->LibFlag = fLib->Text;
            tc->AsmExt = fAsmExt->Text; tc->ObjExt = fObjExt->Text;
        }

    private:
        ToolchainSettings^ tc;
        List<PathField^>^ fields;
        PathField^ toolDir; PathField^ c90; PathField^ cpp11; PathField^ shl; PathField^ asmb; PathField^ lnk; PathField^ rts; PathField^ sim;
        TextBox^ inc; TextBox^ lib;
        TextBox^ cmdCompile; TextBox^ cmdAsm; TextBox^ cmdLink; TextBox^ cmdRun; TextBox^ cmdDebug;
        TextBox^ fInc; TextBox^ fDef; TextBox^ fLibDir; TextBox^ fLib; TextBox^ fAsmExt; TextBox^ fObjExt;

        PathField^ Tool(String^ label, String^ value, bool folder)
        {
            PathField^ f = gcnew PathField(folder, true);
            f->Value = value;
            if (!folder) { f->BaseDir = tc->ToolDir; fields->Add(f); }
            if (label->Contains("Runtime")) f->Filter = "Libraries (*.lib;*.a)|*.lib;*.a|All files (*.*)|*.*";
            Row(label, f);
            f->UpdateStatus();
            return f;
        }

        void Build()
        {
            grid->Controls->Clear(); grid->RowStyles->Clear(); grid->RowCount = 0;
            fields->Clear();
            Section("Tools");
            toolDir = Tool("Tool folder", tc->ToolDir, true);
            toolDir->Controls[0]->TextChanged += gcnew EventHandler(this, &ToolchainPage::OnToolDirChanged);
            c90 = Tool("C90 compiler", tc->CompilerC90, false);
            cpp11 = Tool("C++11 compiler", tc->CompilerCpp11, false);
            shl = Tool("Shalimar compiler", tc->CompilerShalimar, false);
            asmb = Tool("Assembler", tc->Assembler, false);
            lnk = Tool("Linker", tc->Linker, false);
            rts = Tool("Runtime library", tc->Runtime, false);
            sim = Tool("Simulator / runner", tc->Simulator, false);
            Section("Search paths (one per line)");
            inc = Lines(tc->IncludeDirs, 60); Row("Include directories", inc);
            lib = Lines(tc->LibDirs, 60); Row("Library directories", lib);
            Section("Command templates");
            cmdCompile = Mono(tc->CompileCmd); Row("Compile", cmdCompile);
            cmdAsm = Mono(tc->AssembleCmd); Row("Assemble", cmdAsm);
            cmdLink = Mono(tc->LinkCmd); Row("Link", cmdLink);
            cmdRun = Mono(tc->RunCmd); Row("Run", cmdRun);
            cmdDebug = Mono(tc->DebugCmd); Row("Debug", cmdDebug);
            Label^ macros = gcnew Label();
            macros->Text = "Macros: $(Compiler) $(Assembler) $(Linker) $(Runtime) $(Simulator) $(ToolDir) $(Source) $(Name) "
                           "$(AsmFile) $(ObjFile) $(Objects) $(Output) $(Program) $(ObjDir) $(OutDir) $(IncludeFlags) "
                           "$(DefineFlags) $(LibDirFlags) $(LibFlags) $(Optimise) $(CompileFlags) $(LinkFlags) $(LinkerCmd) "
                           "$(Config) $(Target) $(ProjectDir) $(ProjectName)";
            macros->Tag = "muted";
            macros->AutoSize = true;
            macros->MaximumSize = SD::Size(560, 0);
            Row(String::Empty, macros);
            Section("Flag spelling");
            fInc = Mono(tc->IncludeFlag); Row("Include flag", fInc);
            fDef = Mono(tc->DefineFlag); Row("Define flag", fDef);
            fLibDir = Mono(tc->LibDirFlag); Row("Library dir flag", fLibDir);
            fLib = Mono(tc->LibFlag); Row("Library flag", fLib);
            fAsmExt = Mono(tc->AsmExt); Row("Assembly extension", fAsmExt);
            fObjExt = Mono(tc->ObjExt); Row("Object extension", fObjExt);
            Button^ reset = gcnew Button();
            reset->Text = "Reset this target to defaults";
            reset->Click += gcnew EventHandler(this, &ToolchainPage::OnReset);
            Row(String::Empty, reset);
            ThemeManager::Apply(this);
            for each (PathField^ f in fields) f->UpdateStatus();
            toolDir->UpdateStatus();
        }

        void OnToolDirChanged(Object^ sender, EventArgs^ e)
        {
            for each (PathField^ f in fields) { f->BaseDir = toolDir->Value; f->UpdateStatus(); }
        }

        void OnReset(Object^ sender, EventArgs^ e)
        {
            tc->ApplyDefaults();
            Build();
        }
    };

    // ---- Languages > <language> -------------------------------------------------------------
    public ref class LanguagePage : public OptionsPage
    {
    public:
        LanguagePage(LanguageSettings^ l)
        {
            lang = l;
            ext = Text1(String::Join(";", l->Extensions)); Row("File extensions", ext);
            lineC = Mono(l->LineComment); Row("Line comment", lineC);
            blockS = Mono(l->BlockCommentStart); Row("Block comment start", blockS);
            blockE = Mono(l->BlockCommentEnd); Row("Block comment end", blockE);
            pre = Check("Highlight preprocessor directives (#include ...)", l->Preprocessor); Row(String::Empty, pre);
            kw = Lines(l->Keywords, 150); Row("Keywords", kw);
            ty = Lines(l->Types, 90); Row("Type names", ty);
            if (l->Lang == Language::Shalimar)
            {
                Label^ n = gcnew Label();
                n->Text = "The Shalimar word list is a placeholder (C-like). Replace it with the real Shalimar keywords.";
                n->AutoSize = true; n->MaximumSize = SD::Size(520, 0); n->Tag = "muted";
                Row(String::Empty, n);
            }
        }
        virtual void Commit() override
        {
            lang->Extensions = FromLines(ext);
            lang->LineComment = lineC->Text->Trim();
            lang->BlockCommentStart = blockS->Text->Trim();
            lang->BlockCommentEnd = blockE->Text->Trim();
            lang->Preprocessor = pre->Checked;
            lang->Keywords = FromLines(kw);
            lang->Types = FromLines(ty);
        }
    private:
        LanguageSettings^ lang;
        TextBox^ ext; TextBox^ lineC; TextBox^ blockS; TextBox^ blockE; TextBox^ kw; TextBox^ ty;
        CheckBox^ pre;
    };

    // ---- Converters -------------------------------------------------------------------------
    public ref class ConvertersPage : public OptionsPage
    {
    public:
        ConvertersPage(ConverterSettings^ c)
        {
            conv = c;
            toShl = gcnew PathField(false, true); toShl->Value = c->CToShalimar; Row("C -> Shalimar tool", toShl);
            toC = gcnew PathField(false, true); toC->Value = c->ShalimarToC; Row("Shalimar -> C tool", toC);
            cmd = Mono(c->CommandTemplate); Row("Command template", cmd);
            Label^ m = gcnew Label();
            m->Text = "Macros: $(Tool) $(Input) $(Output)";
            m->AutoSize = true; m->Tag = "muted";
            Row(String::Empty, m);
            open = Check("Open the converted file in the editor", c->OpenResult); Row(String::Empty, open);
            toShl->UpdateStatus(); toC->UpdateStatus();
        }
        virtual void Commit() override
        {
            conv->CToShalimar = toShl->Value;
            conv->ShalimarToC = toC->Value;
            conv->CommandTemplate = cmd->Text;
            conv->OpenResult = open->Checked;
        }
    private:
        ConverterSettings^ conv;
        PathField^ toShl; PathField^ toC;
        TextBox^ cmd; CheckBox^ open;
    };

    // ---- Projects ----------------------------------------------------------------------------
    public ref class ProjectsPage : public OptionsPage
    {
    public:
        ProjectsPage(EnvironmentSettings^ e)
        {
            env = e;
            loc = gcnew PathField(true, true); loc->Value = e->DefaultProjectLocation; Row("Default project location", loc);
            Section("Code Composer Studio 7.4 import");
            cg = gcnew PathField(true, true); cg->Value = e->CcsToolRoot; Row("${CG_TOOL_ROOT}", cg);
            Label^ n = gcnew Label();
            n->Text = "Used to expand ${CG_TOOL_ROOT} in include paths read from .cproject files.";
            n->AutoSize = true; n->Tag = "muted";
            Row(String::Empty, n);
            Section("Help");
            help = gcnew PathField(true, true); help->Value = e->HelpDirectory; Row("Help / reference folder", help);
            loc->UpdateStatus(); cg->UpdateStatus(); help->UpdateStatus();
        }
        virtual void Commit() override
        {
            env->DefaultProjectLocation = loc->Value;
            env->CcsToolRoot = cg->Value;
            env->HelpDirectory = help->Value;
        }
    private:
        EnvironmentSettings^ env;
        PathField^ loc; PathField^ cg; PathField^ help;
    };

    // =========================================================================
    public ref class OptionsDialog : public ThemedDialog
    {
    public:
        OptionsDialog(GrideSettings^ current, CommandRegistry^ registry, String^ startPage)
            : ThemedDialog("Options", 980, 660)
        {
            work = current->Clone();
            this->registry = registry;
            pages = gcnew Dictionary<String^, OptionsPage^>();
            FormBorderStyle = WF::FormBorderStyle::Sizable;
            MinimumSize = SD::Size(760, 480);

            tree = gcnew TreeView();
            tree->Dock = DockStyle::Fill;
            tree->HideSelection = false;
            tree->ShowLines = false;
            tree->ItemHeight = 24;
            TreeNode^ envNode = tree->Nodes->Add("Environment");
            envNode->Nodes->Add("General")->Name = "General";
            envNode->Nodes->Add("Fonts and Colors")->Name = "Fonts";
            envNode->Nodes->Add("Keyboard")->Name = "Keyboard";
            TreeNode^ tcNode = tree->Nodes->Add("Toolchains");
            for each (Target t in TargetInfo::All()) tcNode->Nodes->Add(TargetInfo::DisplayName(t))->Name = "TC:" + TargetInfo::Name(t);
            TreeNode^ lgNode = tree->Nodes->Add("Languages");
            for each (LanguageSettings^ l in work->Languages)
                if (l->Lang != Language::LinkerCmd) lgNode->Nodes->Add(l->Name)->Name = "LG:" + l->Name;
            tree->Nodes->Add("Converters")->Nodes->Add("C <-> Shalimar")->Name = "Converters";
            tree->Nodes->Add("Projects")->Nodes->Add("Defaults and Locations")->Name = "Projects";
            tree->ExpandAll();
            tree->AfterSelect += gcnew TreeViewEventHandler(this, &OptionsDialog::OnSelect);

            host = gcnew Panel();
            host->Dock = DockStyle::Fill;
            host->Padding = WF::Padding(12, 4, 4, 4);
            title = Heading(String::Empty);
            title->Font = gcnew SD::Font("Segoe UI Semibold", 12.0f);

            SplitContainer^ split = gcnew SplitContainer();
            split->Size = SD::Size(980, 600);
            split->Dock = DockStyle::Fill;
            split->SplitterDistance = 230;
            split->FixedPanel = WF::FixedPanel::Panel1;
            split->Panel1->Padding = WF::Padding(8);
            split->Panel1->Controls->Add(tree);
            split->Panel2->Controls->Add(host);
            split->Panel2->Controls->Add(title);
            Controls->Add(split);
            split->BringToFront();
            AddOkCancel("OK");

            String^ startKey = startPage;
            if (String::IsNullOrEmpty(startKey)) startKey = "General";
            array<TreeNode^>^ found = tree->Nodes->Find(startKey, true);
            tree->SelectedNode = found->Length > 0 ? found[0] : envNode->Nodes[0];
        }

        property GrideSettings^ Result { GrideSettings^ get() { return work; } }

    protected:
        virtual bool OnAccept() override
        {
            for each (OptionsPage^ p in pages->Values) p->Commit();
            return true;
        }

    private:
        GrideSettings^ work;
        CommandRegistry^ registry;
        Dictionary<String^, OptionsPage^>^ pages;
        TreeView^ tree;
        Panel^ host;
        Label^ title;

        OptionsPage^ Create(String^ key)
        {
            if (key == "General") return gcnew GeneralPage(work);
            if (key == "Fonts") return gcnew FontsColorsPage(work);
            if (key == "Keyboard") return gcnew KeyboardPage(work, registry);
            if (key == "Converters") return gcnew ConvertersPage(work->Converter);
            if (key == "Projects") return gcnew ProjectsPage(work->Env);
            if (key->StartsWith("TC:")) return gcnew ToolchainPage(work->Toolchain(TargetInfo::Parse(key->Substring(3))));
            if (key->StartsWith("LG:")) return gcnew LanguagePage(work->LanguageOf(LanguageInfo::Parse(key->Substring(3))));
            return nullptr;
        }

        void OnSelect(Object^ sender, TreeViewEventArgs^ e)
        {
            String^ key = e->Node->Name;
            if (String::IsNullOrEmpty(key))
            {
                if (e->Node->Nodes->Count > 0) tree->SelectedNode = e->Node->Nodes[0];
                return;
            }
            OptionsPage^ p;
            if (!pages->TryGetValue(key, p))
            {
                p = Create(key);
                if (p == nullptr) return;
                pages[key] = p;
                ThemeManager::Apply(p);
            }
            host->Controls->Clear();
            host->Controls->Add(p);
            title->Text = (e->Node->Parent != nullptr ? e->Node->Parent->Text + "  >  " : String::Empty) + e->Node->Text;
        }
    };
}}
