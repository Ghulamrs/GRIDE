// =============================================================================
//  OptionsDialog.h - Tools > Options  (everything in settings.json)
//
//    Environment  > General | Fonts and Colors | Keyboard
//    Toolchains   > Windows | Linux | macOS | TMS320C6747
//    Languages    > C90 | C++11 | Shalimar | Assembly
//    Converters   > C <-> Shalimar
//    Projects     > Defaults and Locations | RIDE Paths | Compiler Options
//
//  The dialog edits a CLONE of the settings; Result holds it after OK.
// =============================================================================
#pragma once
#include "DialogBase.h"
#include "CompilerOptionsEditor.h"
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
            jumpFirst = Check("Go to the first error when a build fails", env->JumpToFirstError); Row(String::Empty, jumpFirst);
            askNative = Check("Offer Visual Studio's (or TI's) assembler and linker when the project's own fail", env->AskNativeTools); Row(String::Empty, askNative);
            runInIde = Check("Run programs in the Output pane, with an input line (off: an external console window)", env->RunInIde); Row(String::Empty, runInIde);
            buildJobs = gcnew NumericUpDown(); buildJobs->Minimum = Decimal(0); buildJobs->Maximum = Decimal(64); buildJobs->Value = Decimal(Math::Min(64, Math::Max(0, env->BuildJobs)));
            Row("Files compiled at once", buildJobs);
            Label^ jobsNote = gcnew Label();
            jobsNote->Text = String::Format("0: one per processor ({0} here); 1: one file at a time. The Output pane and the Error List keep the project's order.", Environment::ProcessorCount);
            jobsNote->AutoSize = true; jobsNote->Tag = "muted"; jobsNote->MaximumSize = SD::Size(520, 0);
            Row(String::Empty, jobsNote);
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
            env->JumpToFirstError = jumpFirst->Checked;
            env->AskNativeTools = askNative->Checked;
            env->RunInIde = runInIde->Checked;
            env->BuildJobs = Decimal::ToInt32(buildJobs->Value);
        }
    private:
        EnvironmentSettings^ env;
        ComboBox^ theme;
        NumericUpDown^ tabSize; NumericUpDown^ buildJobs;
        CheckBox^ insertSpaces; CheckBox^ lineNumbers; CheckBox^ whitespace; CheckBox^ reopen; CheckBox^ saveBefore; CheckBox^ jumpFirst; CheckBox^ askNative; CheckBox^ runInIde;
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
            // the key set the bindings go over: Visual Studio's, or RIDE's
            set = gcnew ComboBox();
            set->DropDownStyle = ComboBoxStyle::DropDownList;
            set->Items->Add(KeySets::Describe(KeySets::VisualStudio));
            set->Items->Add(KeySets::Describe(KeySets::Ride));
            set->SelectedIndex = KeySets::IsRide(s->KeySet) ? 1 : 0;
            set->SelectedIndexChanged += gcnew EventHandler(this, &KeyboardPage::OnSet);
            Row("Key set", set);
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
        virtual void Commit() override
        {
            settings->KeyBindings = bindings;
            settings->KeySet = set->SelectedIndex == 1 ? KeySets::Ride : KeySets::VisualStudio;
        }

    private:
        GrideSettings^ settings;
        CommandRegistry^ registry;
        Dictionary<String^, String^>^ bindings;
        ComboBox^ set;
        ListView^ list;
        TextBox^ press;

        // the user's binding, else the set's, else the command's own
        String^ ShortcutOf(Command^ c)
        {
            String^ v;
            if (bindings->TryGetValue(c->Id, v)) return v;
            if (KeySets::For(set->SelectedIndex == 1 ? KeySets::Ride : KeySets::VisualStudio)->TryGetValue(c->Id, v)) return v;
            return c->DefaultShortcut;
        }
        void OnSet(Object^ sender, EventArgs^ e) { Fill(); }
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
            tc->CompilerC90 = c90->Value; tc->CompilerCpp11 = cpp11->Value; tc->CompilerShalimar = shl->Value; tc->CompilerCl = cl->Value;
            tc->Assembler = asmb->Value; tc->Linker = lnk->Value; tc->Runtime = rts->Value; tc->Simulator = sim->Value;
            tc->DebugAssembler = dasm->Value; tc->DebugLinker = dlnk->Value; tc->RuntimeDir = rtsDir->Value;
            tc->Emulator = emu->Value; tc->Debugger = dbg->Value;
            tc->IncludeDirs = FromLines(inc); tc->LibDirs = FromLines(lib);
            tc->CompileCmd = cmdCompile->Text; tc->CompileCmdCl = cmdCompileCl->Text; tc->CompileCmdShalimar = cmdCompileShl->Text; tc->AssembleCmd = cmdAsm->Text; tc->LinkCmd = cmdLink->Text;
            tc->DebugAssembleCmd = cmdDebugAsm->Text; tc->DebugLinkCmd = cmdDebugLink->Text;
            tc->RunCmd = cmdRun->Text; tc->EmulateCmd = cmdEmulate->Text; tc->DebugCmd = cmdDebug->Text;
            tc->IncludeFlag = fInc->Text; tc->DefineFlag = fDef->Text; tc->LibDirFlag = fLibDir->Text; tc->LibFlag = fLib->Text;
            tc->AsmExt = fAsmExt->Text; tc->ObjExt = fObjExt->Text;
            tc->SetupScript = setup->Value;
            tc->EnvironmentVars = EnvLines(env);
            tc->OutputCodePage = Decimal::ToInt32(codePage->Value);
            tc->ErrorPattern = errPattern->Text->Trim();
        }

    private:
        ToolchainSettings^ tc;
        List<PathField^>^ fields;
        PathField^ toolDir; PathField^ c90; PathField^ cpp11; PathField^ shl; PathField^ cl; PathField^ asmb; PathField^ lnk; PathField^ rts; PathField^ sim;
        PathField^ dasm; PathField^ dlnk; PathField^ rtsDir; PathField^ emu; PathField^ dbg;
        TextBox^ inc; TextBox^ lib;
        TextBox^ cmdCompile; TextBox^ cmdCompileCl; TextBox^ cmdCompileShl; TextBox^ cmdAsm; TextBox^ cmdLink; TextBox^ cmdDebugAsm; TextBox^ cmdDebugLink;
        TextBox^ cmdRun; TextBox^ cmdEmulate; TextBox^ cmdDebug;
        TextBox^ fInc; TextBox^ fDef; TextBox^ fLibDir; TextBox^ fLib; TextBox^ fAsmExt; TextBox^ fObjExt;
        PathField^ setup; TextBox^ env; NumericUpDown^ codePage; TextBox^ errPattern;

        // one NAME=value per line; unlike FromLines, ';' is kept (PATH=a;b;%PATH%)
        static List<String^>^ EnvLines(TextBox^ t)
        {
            List<String^>^ r = gcnew List<String^>();
            for each (String^ s in t->Text->Split(gcnew array<wchar_t>{ L'\r', L'\n' }, StringSplitOptions::RemoveEmptyEntries))
                if (s->Trim()->Length > 0) r->Add(s->Trim());
            return r;
        }

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
            Label^ names = gcnew Label();
            names->Text = ProgramsNote();
            names->Tag = "muted";
            names->AutoSize = true;
            names->MaximumSize = SD::Size(560, 0);
            Row(String::Empty, names);
            c90 = Tool("C90 compiler (c90)", tc->CompilerC90, false);
            cpp11 = Tool("C++11 compiler (cpp11)", tc->CompilerCpp11, false);
            shl = Tool("Shalimar compiler", tc->CompilerShalimar, false);
            cl = Tool("Host compiler (cl)", tc->CompilerCl, false);
            asmb = Tool("Assembler", tc->Assembler, false);
            lnk = Tool("Linker", tc->Linker, false);
            dasm = Tool("Debug assembler (clang)", tc->DebugAssembler, false);
            dlnk = Tool("Debug linker (link.exe)", tc->DebugLinker, false);
            rts = Tool("Runtime library", tc->Runtime, false);
            rtsDir = Tool("Runtime directory (RTS6x)", tc->RuntimeDir, true);
            sim = Tool("Simulator (sim6747)", tc->Simulator, false);
            emu = Tool("Emulator (vm6747)", tc->Emulator, false);
            dbg = Tool("Debugger", tc->Debugger, false);
            Section("Search paths (one per line)");
            inc = Lines(tc->IncludeDirs, 60); Row("Include directories", inc);
            lib = Lines(tc->LibDirs, 60); Row("Library directories", lib);
            Section("Command templates");
            cmdCompile = Mono(tc->CompileCmd); Row("Compile", cmdCompile);
            cmdCompileCl = Mono(tc->CompileCmdCl); Row("Compile with cl", cmdCompileCl);
            cmdCompileShl = Mono(tc->CompileCmdShalimar); Row("Compile Shalimar", cmdCompileShl);
            cmdAsm = Mono(tc->AssembleCmd); Row("Assemble", cmdAsm);
            cmdLink = Mono(tc->LinkCmd); Row("Link", cmdLink);
            cmdDebugAsm = Mono(tc->DebugAssembleCmd); Row("Debug assemble", cmdDebugAsm);
            cmdDebugLink = Mono(tc->DebugLinkCmd); Row("Debug link", cmdDebugLink);
            cmdRun = Mono(tc->RunCmd); Row("Run", cmdRun);
            cmdEmulate = Mono(tc->EmulateCmd); Row("Emulate", cmdEmulate);
            cmdDebug = Mono(tc->DebugCmd); Row("Debug", cmdDebug);
            Label^ macros = gcnew Label();
            macros->Text = "Macros: $(Compiler) $(Assembler) $(Linker) $(DebugAssembler) $(DebugLinker) $(Runtime) $(RuntimeDir) $(Simulator) "
                           "$(Emulator) $(Debugger) $(ToolDir) $(Source) $(Sources) $(Name) $(AsmFile) $(AsmFiles) $(ObjFile) $(Objects) $(Output) $(Program) "
                           "$(ObjDir) $(OutDir) $(Arch) $(ArchFlag) $(ConfigFlags) $(AsmSyntaxFlag) $(LangFlags) $(IncludeFlags) $(DefineFlags) "
                           "$(LibDirFlags) $(LibFlags) $(CrtLibs) $(Optimise) $(CompileFlags) $(LinkFlags) $(LinkerCmd) $(Config) $(Target) "
                           "$(ProjectDir) $(ProjectName)";
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
            Section("Tool environment (optional)");
            setup = gcnew PathField(false, true);
            setup->Filter = "Batch files (*.bat;*.cmd)|*.bat;*.cmd|All files (*.*)|*.*";
            setup->Value = tc->SetupScript;
            Row("Setup script", setup);
            Label^ setupNote = gcnew Label();
            setupNote->Text = "e.g. vcvars64.bat for MASM / LINK. When set, each tool runs through cmd.exe after this script "
                              "(tools may then be found on the PATH it sets). Leave empty to start the tools directly.";
            setupNote->Tag = "muted";
            setupNote->AutoSize = true;
            setupNote->MaximumSize = SD::Size(560, 0);
            Row(String::Empty, setupNote);
            env = Lines(tc->EnvironmentVars, 60); Row("Environment (NAME=value)", env);
            codePage = gcnew NumericUpDown(); codePage->Minimum = Decimal(0); codePage->Maximum = Decimal(65535);
            codePage->Value = Decimal(Math::Max(0, Math::Min(65535, tc->OutputCodePage)));
            Row("Output code page (0 = OEM)", codePage);
            errPattern = Mono(tc->ErrorPattern); Row("Error pattern (regex)", errPattern);
            Label^ patNote = gcnew Label();
            patNote->Text = "Extra error format, tried before the built-in ones. Named groups: file, line, col, sev, code, msg.";
            patNote->Tag = "muted";
            patNote->AutoSize = true;
            patNote->MaximumSize = SD::Size(560, 0);
            Row(String::Empty, patNote);
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

        // the target's programs as toolchain.json names them, and where a bare name is looked for
        String^ ProgramsNote()
        {
            ToolchainDefinitions^ defs = ToolchainDefinitions::Current;
            TargetDefinition^ td = defs->TargetOf(tc->TargetKind);
            List<String^>^ parts = gcnew List<String^>();
            List<String^>^ vars = gcnew List<String^>();
            if (td != nullptr)
                for each (KeyValuePair<String^, String^> kv in td->Roles)
                {
                    ToolDefinition^ t = defs->Tool(kv.Value);
                    if (t == nullptr) continue;
                    String^ names = t->Exe + (t->Aliases->Count > 0 ? "; " + String::Join(", ", t->Aliases) : String::Empty);
                    parts->Add(kv.Key + ": " + t->Name + " (" + names + ")");
                    if (t->Lookup->Env->Length > 0 && !vars->Contains("$" + t->Lookup->Env)) vars->Add("$" + t->Lookup->Env);
                }
            return "Programs (toolchain.json): " + (parts->Count > 0 ? String::Join("; ", parts) : "none defined for this target") +
                   ". A name without a folder is looked for in its environment variable" + (vars->Count > 0 ? " (" + String::Join(", ", vars) + ")" : String::Empty) +
                   ", the tool folder, beside GRIDE.exe, in the installation (Tools > Options > Projects) and on PATH. "
                   "The names, aliases and argument templates themselves are edited under Toolchains > Definitions.";
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
            m->Text = "Leave the tools and the template empty to use toolchain.json's converter (c2s.exe,\r\n"
                      "--to-shalimar / --to-c), found beside GRIDE.exe or as its lookup says.\r\n"
                      "Template macros: $(Tool) $(Input) $(Output)";
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
            Section("The toolchain's installation");
            ride = gcnew PathField(true, true); ride->Value = e->RideRoot; Row("Installation root or bin", ride);
            Label^ rideNote = gcnew Label();
            rideNote->Text = "Where c90, cpp11, shalimar, masm, link, asm6x, lnk6x, vm6747, sim6747 and lib\\rts6x-tms6747 are looked for when a "
                             "toolchain names a tool without a folder. Empty: a RIDE installation under %ProgramFiles%\\RIDE\\bin, else beside GRIDE.exe, then PATH.";
            rideNote->AutoSize = true; rideNote->Tag = "muted"; rideNote->MaximumSize = SD::Size(520, 0);
            Row(String::Empty, rideNote);
            Section("Code Composer Studio 7.4 / 5.5 projects");
            ccsEnabled = Check("Open Code Composer Studio projects (File > Open > CCS Project)", e->CcsEnabled); Row(String::Empty, ccsEnabled);
            ccsRoot = gcnew PathField(true, true); ccsRoot->Value = e->CcsRoot; Row("CCS installation", ccsRoot);
            Label^ rootNote = gcnew Label();
            rootNote->Text = "White's ccs.root (C:\\ti\\ccsv7): ${CG_TOOL_ROOT} is the project's compiler version under its tools\\compiler. "
                             "Empty: TI's compiler directory (GRIDE Paths), else the directory below.";
            rootNote->AutoSize = true; rootNote->Tag = "muted"; rootNote->MaximumSize = SD::Size(520, 0);
            Row(String::Empty, rootNote);
            cg = gcnew PathField(true, true); cg->Value = e->CcsToolRoot; Row("${CG_TOOL_ROOT}", cg);
            Label^ n = gcnew Label();
            n->Text = "A compiler directory named outright, used to expand ${CG_TOOL_ROOT} in include paths read from .cproject files when no CCS installation is named.";
            n->AutoSize = true; n->Tag = "muted"; n->MaximumSize = SD::Size(520, 0);
            Row(String::Empty, n);
            Section("Help");
            help = gcnew PathField(true, true); help->Value = e->HelpDirectory; Row("Help / reference folder", help);
            loc->UpdateStatus(); ride->UpdateStatus(); ccsRoot->UpdateStatus(); cg->UpdateStatus(); help->UpdateStatus();
        }
        virtual void Commit() override
        {
            env->DefaultProjectLocation = loc->Value;
            env->RideRoot = ride->Value;
            ToolchainSettings::RideRoot = env->RideRoot;
            env->CcsEnabled = ccsEnabled->Checked;
            env->CcsRoot = ccsRoot->Value;
            env->CcsToolRoot = cg->Value;
            env->HelpDirectory = help->Value;
        }
    private:
        EnvironmentSettings^ env;
        PathField^ loc; PathField^ ride; PathField^ ccsRoot; PathField^ cg; PathField^ help;
        CheckBox^ ccsEnabled;
    };

    // ---- Projects > RIDE Paths: RIDE's settings.json, the keys that are paths and choices ----------------
    public ref class RidePathsPage : public OptionsPage
    {
    public:
        RidePathsPage(GrideSettings^ s)
        {
            env = s->Env;
            String^ root = RideToolchain::InstallRoot(ToolchainSettings::RideRoot);
            Section("The installation's headers (settings.json include / lib)");
            include = gcnew PathField(true, true); include->Value = env->RideIncludeDir; include->BaseDir = root; Row("cpp11's headers (include)", include);
            lib = gcnew PathField(true, true); lib->Value = env->RideLibDir; lib->BaseDir = root; Row("c90's headers (lib)", lib);
            Label^ hdrNote = gcnew Label();
            hdrNote->Text = "Every cpp11 compile is given the include directory and every c90 compile the lib directory, after the project's own. "
                            "Relative to the toolchain's installation" + (root == nullptr ? " (none found)" : " (" + root + ")") + "; empty: the directory of that name there.";
            hdrNote->AutoSize = true; hdrNote->Tag = "muted"; hdrNote->MaximumSize = SD::Size(520, 0);
            Row(String::Empty, hdrNote);
            Section("Visual Studio (settings.json vcvars)");
            vcvars = gcnew PathField(false, true); vcvars->Value = env->Vcvars; vcvars->Filter = "Batch files (*.bat)|*.bat|All files (*.*)|*.*"; Row("vcvars64.bat", vcvars);
            Label^ vsNote = gcnew Label();
            vsNote->Text = "Names the Visual Studio whose cl, ml64, link.exe and clang are used (its folder is taken; the batch file is never run). Empty: the newest installation vswhere finds.";
            vsNote->AutoSize = true; vsNote->Tag = "muted"; vsNote->MaximumSize = SD::Size(520, 0);
            Row(String::Empty, vsNote);
            Section("TI's C6000 compiler (settings.json ti / tilib)");
            ti = gcnew PathField(true, true); ti->Value = env->TiCompilerDir; Row("TI compiler directory", ti);
            tilib = gcnew PathField(true, true); tilib->Value = env->TiLibDir; Row("TI library directory", tilib);
            Label^ tiNote = gcnew Label();
            tiNote->Text = "The directory with bin\\lnk6x (ti-cgt-c6000_x.y.z). Named, a TMS320C6747 build links against TI's runtime (rts6740_elf_eh.lib where it is, in lib or in the "
                           "library directory) instead of RTS6x, and TI's lnk6x stands in for a linker that is not there. Empty: RTS6x beside GRIDE; the newest CCS compiler found under " +
                           String::Join(", ", ToolchainDefinitions::Current->Ccs->TiRoots) + " when RTS6x is not there" + (RideToolchain::DetectedTiDir() == nullptr ? " (none found now)" : " (found: " + RideToolchain::DetectedTiDir() + ")") + ".";
            tiNote->AutoSize = true; tiNote->Tag = "muted"; tiNote->MaximumSize = SD::Size(520, 0);
            Row(String::Empty, tiNote);
            Section("Shared (settings.json includes / libraries)");
            includes = Lines(env->SharedIncludes, 60); Row("Header directories every compile searches after the project's own (one per line)", includes);
            libraries = Lines(env->SharedLibraries, 60); Row("Libraries every host link takes (one per line)", libraries);
            Section("Defaults");
            compiler = gcnew ComboBox();
            compiler->DropDownStyle = ComboBoxStyle::DropDownList;
            compiler->Items->Add("auto");
            for each (RideTool t in RideToolchain::Compilers()) compiler->Items->Add(RideToolchain::Word(t));
            compiler->SelectedItem = env->DefaultCompiler;
            if (compiler->SelectedIndex < 0) compiler->SelectedIndex = 0;
            Row("Compiler a new project starts with", compiler);
            c6747 = gcnew ComboBox();
            c6747->DropDownStyle = ComboBoxStyle::DropDownList;
            c6747->Items->AddRange(gcnew array<Object^>{ "auto", "sim6747", "vm6747" });
            c6747->SelectedItem = env->C6747Debugger;
            if (c6747->SelectedIndex < 0) c6747->SelectedIndex = 0;
            Row("TMS320C6747 debugger", c6747);
            Label^ dbgNote = gcnew Label();
            dbgNote->Text = "auto: sim6747 on this build's .out when it has a line table, else vm6747 on the assembly (White's rule). GRIDE_DEBUG_SIM6747=1 / GRIDE_DEBUG_VM6747=1 in the environment at start win.";
            dbgNote->AutoSize = true; dbgNote->Tag = "muted"; dbgNote->MaximumSize = SD::Size(520, 0);
            Row(String::Empty, dbgNote);
            windows = gcnew ComboBox();
            windows->DropDownStyle = ComboBoxStyle::DropDownList;
            windows->Items->AddRange(gcnew array<Object^>{ "auto", "gride-dbg", "cdb" });
            windows->SelectedItem = env->EffectiveWindowsDebugger();
            if (windows->SelectedIndex < 0) windows->SelectedIndex = 0;
            Row("Windows debugger", windows);
            Label^ winNote = gcnew Label();
            winNote->Text = "auto: gride-dbg, GRIDE's own debugger, when it is beside GRIDE, else cdb (Debugging Tools for Windows), else lldb. "
                            "Either named stands in for the other where that one is not there; the Output pane says which.";
            winNote->AutoSize = true; winNote->Tag = "muted"; winNote->MaximumSize = SD::Size(520, 0);
            Row(String::Empty, winNote);
            runtime = gcnew ComboBox();
            runtime->DropDownStyle = ComboBoxStyle::DropDownList;
            runtime->Items->AddRange(gcnew array<Object^>{ "automatic", "GRIDE's runtime", "Microsoft's CRT" });   // settings.json's auto, grt, msvc
            String^ now = env->WindowsRuntime == nullptr ? "auto" : env->WindowsRuntime->Trim()->ToLowerInvariant();
            runtime->SelectedIndex = now == "grt" ? 1 : now == "msvc" ? 2 : 0;
            Row("Windows C runtime", runtime);
            Label^ rtNote = gcnew Label();
            rtNote->Text = "GRIDE's runtime: lib\\grtstart, grtcompat, grtcxx, grtc and grtwin.lib and GRIDE's own kernel32.lib - a program needs no Visual Studio and no Windows SDK to link, "
                           "and kernel32.dll alone to run. Microsoft's CRT: Visual Studio's libcmt and the SDK's UCRT, found through LIB. "
                           "Automatic: Microsoft's only where Visual Studio is found and the build has cl's objects. GRIDE_WINDOWS_RUNTIME=grt / msvc in the environment at start wins.";
            rtNote->AutoSize = true; rtNote->Tag = "muted"; rtNote->MaximumSize = SD::Size(520, 0);
            Row(String::Empty, rtNote);
            include->UpdateStatus(); lib->UpdateStatus(); vcvars->UpdateStatus(); ti->UpdateStatus(); tilib->UpdateStatus();
        }
        virtual void Commit() override
        {
            env->RideIncludeDir = include->Value;
            env->RideLibDir = lib->Value;
            env->Vcvars = vcvars->Value;
            env->TiCompilerDir = ti->Value;
            env->TiLibDir = tilib->Value;
            env->SharedIncludes = FromLines(includes);
            env->SharedLibraries = FromLines(libraries);
            env->DefaultCompiler = safe_cast<String^>(compiler->SelectedItem);
            env->C6747Debugger = safe_cast<String^>(c6747->SelectedItem);
            env->WindowsDebugger = safe_cast<String^>(windows->SelectedItem);
            env->WindowsRuntime = runtime->SelectedIndex == 1 ? "grt" : runtime->SelectedIndex == 2 ? "msvc" : "auto";
        }
    private:
        EnvironmentSettings^ env;
        PathField^ include; PathField^ lib; PathField^ vcvars; PathField^ ti; PathField^ tilib;
        TextBox^ includes; TextBox^ libraries;
        ComboBox^ compiler; ComboBox^ c6747; ComboBox^ windows; ComboBox^ runtime;
    };

    // ---- Projects > Compiler Options: RIDE's installation-level store (settings.json options) -----------
    public ref class CompilerOptionsPage : public OptionsPage
    {
    public:
        CompilerOptionsPage(GrideSettings^ s)
        {
            Label^ n = gcnew Label();
            n->Text = "The Compiler Options as the installation sets them: what a new project starts with. A project's own are under Project > Compiler Options; a build takes the project's.";
            n->AutoSize = true; n->Tag = "muted"; n->MaximumSize = SD::Size(700, 0);
            grid->ColumnStyles[0]->Width = 0;
            Row(String::Empty, n);
            TargetDefinition^ host = ToolchainDefinitions::Current->HostTarget();
            Target t = host == nullptr ? Target::Windows : TargetInfo::Parse(host->IdeTarget);
            editor = gcnew CompilerOptionsEditor(s->CompilerOptions, "Debug", t, false);
            Controls->Add(editor);
            editor->BringToFront();
        }
        virtual void Commit() override { editor->Commit(); }
    private:
        CompilerOptionsEditor^ editor;
    };

    // ---- Toolchains > Definitions (toolchain.json) -----------------------------------------
    // Edits the user's overrides of the shipped toolchain file: a tool's program name, aliases,
    // words, lookup, flags and operations, a library's files and directories. Only what differs
    // from the shipped file is written to the user's file; Restore removes the tool's entry.
    public ref class ToolchainDefinitionsPage : public OptionsPage
    {
    public:
        ToolchainDefinitionsPage(GrideSettings^ s)
        {
            work = s;
            defs = ToolchainDefinitions::Current;
            toolView = gcnew Dictionary<String^, JsonValue^>();
            libView = gcnew Dictionary<String^, JsonValue^>();
            touched = gcnew HashSet<String^>();
            currentTool = nullptr; currentLib = nullptr;
            Build();
        }

        virtual void Commit() override
        {
            CaptureTool(); CaptureLib();
            if (touched->Count == 0) return;
            for each (String^ key in touched)
            {
                String^ section = key->Substring(0, key->IndexOf(L':'));
                String^ id = key->Substring(key->IndexOf(L':') + 1);
                JsonValue^ shipped = defs->ShippedEntry(section, id);
                JsonValue^ view = section == "tools" ? toolView[id] : libView[id];
                JsonValue^ ov = defs->IsOverridden(section, id) ? ToolchainDefinitions::Clone(defs->UserEntry(section, id)) : JsonValue::MakeObject();
                if (section == "tools") ApplyToolDiff(ov, view, shipped); else ApplyLibDiff(ov, view, shipped);
                defs->SetUserEntry(section, id, ov);
            }
            String^ err;
            if (!defs->SaveUser(err))
            {
                MessageBox::Show(this, "The toolchain overrides could not be saved:\n" + err, "GRIDE", MessageBoxButtons::OK, MessageBoxIcon::Warning);
                return;
            }
            // the settings' defaults follow the new definitions; what the user set stays
            Dictionary<Target, ToolchainSettings^>^ before = gcnew Dictionary<Target, ToolchainSettings^>();
            for each (Target t in TargetInfo::All()) before[t] = gcnew ToolchainSettings(t);
            ToolchainDefinitions::Reload();
            for each (ToolchainSettings^ tc in work->Toolchains) tc->Rebase(before[tc->TargetKind]);
            defs = ToolchainDefinitions::Current;
            touched->Clear();
        }

    private:
        GrideSettings^ work;
        ToolchainDefinitions^ defs;
        Dictionary<String^, JsonValue^>^ toolView;      // what the page shows per tool (the merged entry, edited)
        Dictionary<String^, JsonValue^>^ libView;
        HashSet<String^>^ touched;                      // "tools:id" / "libraries:id"
        String^ currentTool; String^ currentLib;
        ComboBox^ toolBox; Label^ toolDesc; Label^ toolState;
        TextBox^ exe; TextBox^ aliases; TextBox^ words; TextBox^ envVar; TextBox^ installDir; CheckBox^ onPath; CheckBox^ vcvars;
        TextBox^ encoding; TextBox^ diagnostics; TextBox^ archFlag; TextBox^ includeFlag; TextBox^ defineFlag; TextBox^ installIncludes;
        TextBox^ debugFlags; TextBox^ releaseFlags; TextBox^ codeviewFlag; TextBox^ namedAsmFlag; TextBox^ versionArgs;
        DataGridView^ ops;
        ComboBox^ libBox; Label^ libDesc; Label^ libState;
        TextBox^ libDir; TextBox^ libEnv; TextBox^ libMarker; TextBox^ libDebug; TextBox^ libRelease; TextBox^ libAliases; TextBox^ libAsmDebug; TextBox^ libAsmRelease; TextBox^ libWhen;

        static String^ Joined(JsonValue^ o, String^ key) { return o == nullptr ? String::Empty : String::Join("; ", o->GetStringList(key)); }
        static List<String^>^ Split(String^ s)
        {
            List<String^>^ r = gcnew List<String^>();
            for each (String^ x in s->Split(gcnew array<wchar_t>{ L';' }, StringSplitOptions::RemoveEmptyEntries)) if (x->Trim()->Length > 0) r->Add(x->Trim());
            return r;
        }
        static String^ Nested(JsonValue^ o, String^ key, String^ sub)
        {
            JsonValue^ n = o == nullptr ? nullptr : o->GetObject(key);
            return n == nullptr ? String::Empty : n->GetString(sub, String::Empty);
        }
        static bool NestedBool(JsonValue^ o, String^ key, String^ sub, bool def)
        {
            JsonValue^ n = o == nullptr ? nullptr : o->GetObject(key);
            return n == nullptr ? def : n->GetBool(sub, def);
        }
        static JsonValue^ Sub(JsonValue^ o, String^ key)
        {
            JsonValue^ n = o->GetObject(key);
            if (n == nullptr) { n = JsonValue::MakeObject(); o->SetValue(key, n); }
            return n;
        }
        static bool Same(JsonValue^ a, JsonValue^ b)
        {
            return String::Equals(a == nullptr ? "null" : JsonWriter::Write(a), b == nullptr ? "null" : JsonWriter::Write(b));
        }
        // a scalar or list at [key] (or [key][sub]): set in the override when it differs from the shipped value, removed when not
        static void DiffAt(JsonValue^ ov, JsonValue^ view, JsonValue^ shipped, String^ key, String^ sub)
        {
            JsonValue^ v = view == nullptr ? nullptr : (sub == nullptr ? view->Get(key) : (view->GetObject(key) == nullptr ? nullptr : view->GetObject(key)->Get(sub)));
            JsonValue^ s = shipped == nullptr ? nullptr : (sub == nullptr ? shipped->Get(key) : (shipped->GetObject(key) == nullptr ? nullptr : shipped->GetObject(key)->Get(sub)));
            bool differs = !Same(v, s);
            if (sub == nullptr)
            {
                if (differs) ov->SetValue(key, ToolchainDefinitions::Clone(v)); else ov->Remove(key);
                return;
            }
            JsonValue^ n = ov->GetObject(key);
            if (differs) { if (n == nullptr) { n = JsonValue::MakeObject(); ov->SetValue(key, n); } n->SetValue(sub, ToolchainDefinitions::Clone(v)); }
            else if (n != nullptr) { n->Remove(sub); if (n->Count == 0) ov->Remove(key); }
        }
        static void ApplyToolDiff(JsonValue^ ov, JsonValue^ view, JsonValue^ shipped)
        {
            for each (String^ k in gcnew array<String^>{ "exe", "aliases", "words", "outputEncoding", "diagnostics", "archFlag", "includeFlag", "defineFlag", "installIncludeDirs" })
                DiffAt(ov, view, shipped, k, nullptr);
            for each (String^ k in gcnew array<String^>{ "env", "installDir", "path", "vcvars" }) DiffAt(ov, view, shipped, "lookup", k);
            for each (String^ k in gcnew array<String^>{ "Debug", "Release" }) DiffAt(ov, view, shipped, "configFlags", k);
            for each (String^ k in gcnew array<String^>{ "codeview", "assemblerNamed" }) DiffAt(ov, view, shipped, "asmSyntaxFlags", k);
            DiffAt(ov, view, shipped, "version", "args");
            JsonValue^ vops = view->GetObject("operations");
            JsonValue^ sops = shipped == nullptr ? nullptr : shipped->GetObject("operations");
            if (vops != nullptr)
                for each (String^ op in vops->KeyNames)
                {
                    JsonValue^ vo = vops->GetObject(op);
                    JsonValue^ so = sops == nullptr ? nullptr : sops->GetObject(op);
                    if (vo == nullptr) continue;
                    JsonValue^ oops = ov->GetObject("operations");
                    if (!Same(vo->Get("args"), so == nullptr ? nullptr : so->Get("args")))
                    {
                        if (oops == nullptr) { oops = JsonValue::MakeObject(); ov->SetValue("operations", oops); }
                        JsonValue^ oo = oops->GetObject(op);
                        if (oo == nullptr) { oo = JsonValue::MakeObject(); oops->SetValue(op, oo); }
                        oo->SetString("args", vo->GetString("args", String::Empty));
                    }
                    else if (oops != nullptr && oops->GetObject(op) != nullptr)
                    {
                        oops->GetObject(op)->Remove("args");
                        if (oops->GetObject(op)->Count == 0) oops->Remove(op);
                        if (oops->Count == 0) ov->Remove("operations");
                    }
                }
        }
        static void ApplyLibDiff(JsonValue^ ov, JsonValue^ view, JsonValue^ shipped)
        {
            for each (String^ k in gcnew array<String^>{ "directory", "directoryEnv", "marker", "aliases", "when" }) DiffAt(ov, view, shipped, k, nullptr);
            for each (String^ k in gcnew array<String^>{ "Debug", "Release" })
            {
                DiffAt(ov, view, shipped, "files", k);
                DiffAt(ov, view, shipped, "assemblyDirectories", k);
            }
        }

        JsonValue^ ViewOf(String^ section, String^ id)
        {
            Dictionary<String^, JsonValue^>^ map = section == "tools" ? toolView : libView;
            JsonValue^ v;
            if (!map->TryGetValue(id, v))
            {
                JsonValue^ m = defs->MergedEntry(section, id);
                v = m == nullptr ? JsonValue::MakeObject() : ToolchainDefinitions::Clone(m);
                map[id] = v;
            }
            return v;
        }

        void Build()
        {
            Section("toolchain.json");
            Label^ files = gcnew Label();
            files->Text = "Shipped: " + (defs->ShippedFound ? defs->ShippedFile : "not found (" + defs->ShippedFile + ")") + "\r\nYour overrides: " + defs->UserFile +
                          (defs->UserJson != nullptr ? "" : " (none yet)") +
                          (defs->Problems->Count > 0 ? "\r\n" + String::Join("\r\n", defs->Problems) : "\r\nThe file reads clean.");
            files->Tag = "muted"; files->AutoSize = true; files->MaximumSize = SD::Size(560, 0);
            Row(String::Empty, files);

            Section("Tools");
            toolBox = gcnew ComboBox();
            toolBox->DropDownStyle = ComboBoxStyle::DropDownList;
            for each (ToolDefinition^ t in defs->Tools) toolBox->Items->Add(t->Id + "  -  " + t->Name + "  (" + t->Role + ")");
            toolBox->SelectedIndexChanged += gcnew EventHandler(this, &ToolchainDefinitionsPage::OnToolChanged);
            Row("Tool", toolBox);
            toolDesc = gcnew Label(); toolDesc->Tag = "muted"; toolDesc->AutoSize = true; toolDesc->MaximumSize = SD::Size(560, 0);
            Row(String::Empty, toolDesc);
            toolState = gcnew Label(); toolState->AutoSize = true; toolState->Tag = "muted";
            Row("Source", toolState);
            exe = Mono(String::Empty); Row("Program (exe)", exe);
            aliases = Mono(String::Empty); Row("Aliases (;)", aliases);
            words = Mono(String::Empty); Row("Project words (;)", words);
            envVar = Mono(String::Empty); Row("Environment variable", envVar);
            installDir = Mono(String::Empty); Row("Installation sub-folder", installDir);
            onPath = Check("Also looked for on PATH", true); Row(String::Empty, onPath);
            vcvars = Check("Reached through vcvars64.bat (Visual Studio's)", false); Row(String::Empty, vcvars);
            versionArgs = Mono(String::Empty); Row("Version query", versionArgs);
            encoding = Mono(String::Empty); Row("Output encoding", encoding);
            diagnostics = Mono(String::Empty); Row("Diagnostics shape", diagnostics);
            archFlag = Mono(String::Empty); Row("Target flag", archFlag);
            includeFlag = Mono(String::Empty); Row("Include flag", includeFlag);
            defineFlag = Mono(String::Empty); Row("Define flag", defineFlag);
            installIncludes = Mono(String::Empty); Row("Installation includes (;)", installIncludes);
            debugFlags = Mono(String::Empty); Row("Debug flags", debugFlags);
            releaseFlags = Mono(String::Empty); Row("Release flags", releaseFlags);
            codeviewFlag = Mono(String::Empty); Row("CodeView syntax flag", codeviewFlag);
            namedAsmFlag = Mono(String::Empty); Row("Named-assembler syntax flag", namedAsmFlag);
            ops = gcnew DataGridView();
            ops->Height = 150;
            ops->AllowUserToAddRows = false; ops->AllowUserToDeleteRows = false; ops->RowHeadersVisible = false;
            ops->AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode::Fill;
            DataGridViewTextBoxColumn^ c1 = gcnew DataGridViewTextBoxColumn(); c1->HeaderText = "Operation"; c1->ReadOnly = true; c1->FillWeight = 22;
            DataGridViewTextBoxColumn^ c2 = gcnew DataGridViewTextBoxColumn(); c2->HeaderText = "Arguments"; c2->FillWeight = 50;
            DataGridViewTextBoxColumn^ c3 = gcnew DataGridViewTextBoxColumn(); c3->HeaderText = "What it does"; c3->ReadOnly = true; c3->FillWeight = 28;
            ops->Columns->Add(c1); ops->Columns->Add(c2); ops->Columns->Add(c3);
            Row("Operations", ops);
            Label^ ph = gcnew Label();
            ph->Text = "Placeholders: " + String::Join(" ", defs->Placeholders->Keys) + " - each is explained in toolchain.json and TOOLCHAIN-FORMAT.md.";
            ph->Tag = "muted"; ph->AutoSize = true; ph->MaximumSize = SD::Size(560, 0);
            Row(String::Empty, ph);
            Button^ restoreTool = gcnew Button();
            restoreTool->Text = "Restore shipped defaults (this tool)";
            restoreTool->Click += gcnew EventHandler(this, &ToolchainDefinitionsPage::OnRestoreTool);
            Row(String::Empty, restoreTool);

            Section("Libraries");
            libBox = gcnew ComboBox();
            libBox->DropDownStyle = ComboBoxStyle::DropDownList;
            for each (LibraryDefinition^ l in defs->Libraries) libBox->Items->Add(l->Id + "  -  " + l->Name + "  (" + l->Kind + ", " + l->Target + ")");
            libBox->SelectedIndexChanged += gcnew EventHandler(this, &ToolchainDefinitionsPage::OnLibChanged);
            Row("Library", libBox);
            libDesc = gcnew Label(); libDesc->Tag = "muted"; libDesc->AutoSize = true; libDesc->MaximumSize = SD::Size(560, 0);
            Row(String::Empty, libDesc);
            libState = gcnew Label(); libState->AutoSize = true; libState->Tag = "muted";
            Row("Source", libState);
            libDir = Mono(String::Empty); Row("Directory", libDir);
            libEnv = Mono(String::Empty); Row("Directory variable", libEnv);
            libMarker = Mono(String::Empty); Row("Marker file", libMarker);
            libDebug = Mono(String::Empty); Row("Debug file(s)", libDebug);
            libRelease = Mono(String::Empty); Row("Release file(s)", libRelease);
            libAliases = Mono(String::Empty); Row("Project aliases (;)", libAliases);
            libAsmDebug = Mono(String::Empty); Row("Debug assembly directory", libAsmDebug);
            libAsmRelease = Mono(String::Empty); Row("Release assembly directory", libAsmRelease);
            libWhen = Mono(String::Empty); Row("Linked when", libWhen);
            Button^ restoreLib = gcnew Button();
            restoreLib->Text = "Restore shipped defaults (this library)";
            restoreLib->Click += gcnew EventHandler(this, &ToolchainDefinitionsPage::OnRestoreLib);
            Row(String::Empty, restoreLib);

            if (toolBox->Items->Count > 0) toolBox->SelectedIndex = 0;
            if (libBox->Items->Count > 0) libBox->SelectedIndex = 0;
            ThemeManager::Apply(this);
        }

        String^ SelectedId(ComboBox^ box)
        {
            String^ s = dynamic_cast<String^>(box->SelectedItem);
            if (s == nullptr) return nullptr;
            int at = s->IndexOf("  -  ");
            return at < 0 ? s : s->Substring(0, at);
        }

        void OnToolChanged(Object^ sender, EventArgs^ e)
        {
            CaptureTool();
            currentTool = SelectedId(toolBox);
            ShowTool();
        }
        void OnLibChanged(Object^ sender, EventArgs^ e)
        {
            CaptureLib();
            currentLib = SelectedId(libBox);
            ShowLib();
        }

        void ShowTool()
        {
            if (currentTool == nullptr) return;
            JsonValue^ v = ViewOf("tools", currentTool);
            toolDesc->Text = v->GetString("description", String::Empty);
            toolState->Text = touched->Contains("tools:" + currentTool) ? "changed here (not saved yet)" : defs->IsOverridden("tools", currentTool) ? "your file overrides the shipped entry" : "as shipped";
            exe->Text = v->GetString("exe", String::Empty);
            aliases->Text = Joined(v, "aliases");
            words->Text = Joined(v, "words");
            envVar->Text = Nested(v, "lookup", "env");
            installDir->Text = Nested(v, "lookup", "installDir");
            onPath->Checked = NestedBool(v, "lookup", "path", true);
            vcvars->Checked = NestedBool(v, "lookup", "vcvars", false);
            versionArgs->Text = Nested(v, "version", "args");
            encoding->Text = v->GetString("outputEncoding", String::Empty);
            diagnostics->Text = v->GetString("diagnostics", String::Empty);
            archFlag->Text = v->GetString("archFlag", String::Empty);
            includeFlag->Text = v->GetString("includeFlag", String::Empty);
            defineFlag->Text = v->GetString("defineFlag", String::Empty);
            installIncludes->Text = Joined(v, "installIncludeDirs");
            debugFlags->Text = Nested(v, "configFlags", "Debug");
            releaseFlags->Text = Nested(v, "configFlags", "Release");
            codeviewFlag->Text = Nested(v, "asmSyntaxFlags", "codeview");
            namedAsmFlag->Text = Nested(v, "asmSyntaxFlags", "assemblerNamed");
            ops->Rows->Clear();
            JsonValue^ o = v->GetObject("operations");
            if (o != nullptr)
                for each (String^ op in o->KeyNames)
                {
                    JsonValue^ od = o->GetObject(op);
                    if (od == nullptr) continue;
                    ops->Rows->Add(op, od->GetString("args", String::Empty), od->GetString("description", String::Empty));
                }
        }

        // the fields back into the view; a change marks the tool
        void CaptureTool()
        {
            if (currentTool == nullptr) return;
            JsonValue^ v = ViewOf("tools", currentTool);
            String^ before = JsonWriter::Write(v);
            v->SetString("exe", exe->Text->Trim());
            v->SetList("aliases", Split(aliases->Text));
            v->SetList("words", Split(words->Text));
            JsonValue^ l = Sub(v, "lookup");
            l->SetString("env", envVar->Text->Trim()); l->SetString("installDir", installDir->Text->Trim());
            l->SetBool("path", onPath->Checked); l->SetBool("vcvars", vcvars->Checked);
            Sub(v, "version")->SetString("args", versionArgs->Text->Trim());
            v->SetString("outputEncoding", encoding->Text->Trim());
            v->SetString("diagnostics", diagnostics->Text->Trim());
            v->SetString("archFlag", archFlag->Text->Trim());
            v->SetString("includeFlag", includeFlag->Text->Trim());
            v->SetString("defineFlag", defineFlag->Text->Trim());
            v->SetList("installIncludeDirs", Split(installIncludes->Text));
            JsonValue^ cf = Sub(v, "configFlags");
            cf->SetString("Debug", debugFlags->Text->Trim()); cf->SetString("Release", releaseFlags->Text->Trim());
            JsonValue^ af = Sub(v, "asmSyntaxFlags");
            af->SetString("codeview", codeviewFlag->Text->Trim()); af->SetString("assemblerNamed", namedAsmFlag->Text->Trim());
            ops->EndEdit();
            JsonValue^ o = Sub(v, "operations");
            for each (DataGridViewRow^ row in ops->Rows)
            {
                String^ op = dynamic_cast<String^>(row->Cells[0]->Value);
                String^ args = dynamic_cast<String^>(row->Cells[1]->Value);
                if (op == nullptr) continue;
                Sub(o, op)->SetString("args", args == nullptr ? String::Empty : args->Trim());
            }
            if (!String::Equals(before, JsonWriter::Write(v))) touched->Add("tools:" + currentTool);
        }

        void ShowLib()
        {
            if (currentLib == nullptr) return;
            JsonValue^ v = ViewOf("libraries", currentLib);
            libDesc->Text = v->GetString("description", String::Empty);
            libState->Text = touched->Contains("libraries:" + currentLib) ? "changed here (not saved yet)" : defs->IsOverridden("libraries", currentLib) ? "your file overrides the shipped entry" : "as shipped";
            libDir->Text = v->GetString("directory", String::Empty);
            libEnv->Text = v->GetString("directoryEnv", String::Empty);
            libMarker->Text = v->GetString("marker", String::Empty);
            libDebug->Text = Nested(v, "files", "Debug");
            libRelease->Text = Nested(v, "files", "Release");
            libAliases->Text = Joined(v, "aliases");
            libAsmDebug->Text = Nested(v, "assemblyDirectories", "Debug");
            libAsmRelease->Text = Nested(v, "assemblyDirectories", "Release");
            libWhen->Text = v->GetString("when", String::Empty);
        }

        void CaptureLib()
        {
            if (currentLib == nullptr) return;
            JsonValue^ v = ViewOf("libraries", currentLib);
            String^ before = JsonWriter::Write(v);
            v->SetString("directory", libDir->Text->Trim());
            v->SetString("directoryEnv", libEnv->Text->Trim());
            v->SetString("marker", libMarker->Text->Trim());
            JsonValue^ f = Sub(v, "files");
            f->SetString("Debug", libDebug->Text->Trim()); f->SetString("Release", libRelease->Text->Trim());
            v->SetList("aliases", Split(libAliases->Text));
            if (libAsmDebug->Text->Trim()->Length > 0 || libAsmRelease->Text->Trim()->Length > 0 || v->Has("assemblyDirectories"))
            {
                JsonValue^ a = Sub(v, "assemblyDirectories");
                a->SetString("Debug", libAsmDebug->Text->Trim()); a->SetString("Release", libAsmRelease->Text->Trim());
            }
            v->SetString("when", libWhen->Text->Trim());
            if (!String::Equals(before, JsonWriter::Write(v))) touched->Add("libraries:" + currentLib);
        }

        void OnRestoreTool(Object^ sender, EventArgs^ e)
        {
            if (currentTool == nullptr) return;
            JsonValue^ shipped = defs->ShippedEntry("tools", currentTool);
            toolView[currentTool] = shipped == nullptr ? JsonValue::MakeObject() : ToolchainDefinitions::Clone(shipped);
            touched->Add("tools:" + currentTool);
            ShowTool();
            toolState->Text = "shipped defaults (saved with OK)";
        }
        void OnRestoreLib(Object^ sender, EventArgs^ e)
        {
            if (currentLib == nullptr) return;
            JsonValue^ shipped = defs->ShippedEntry("libraries", currentLib);
            libView[currentLib] = shipped == nullptr ? JsonValue::MakeObject() : ToolchainDefinitions::Clone(shipped);
            touched->Add("libraries:" + currentLib);
            ShowLib();
            libState->Text = "shipped defaults (saved with OK)";
        }
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
            tcNode->Nodes->Add("Definitions (toolchain.json)")->Name = "TCDEF";
            TreeNode^ lgNode = tree->Nodes->Add("Languages");
            for each (LanguageSettings^ l in work->Languages)
                if (l->Lang != Language::LinkerCmd) lgNode->Nodes->Add(l->Name)->Name = "LG:" + l->Name;
            tree->Nodes->Add("Converters")->Nodes->Add("C <-> Shalimar")->Name = "Converters";
            TreeNode^ prNode = tree->Nodes->Add("Projects");
            prNode->Nodes->Add("Defaults and Locations")->Name = "Projects";
            prNode->Nodes->Add("GRIDE Paths")->Name = "RidePaths";
            prNode->Nodes->Add("Compiler Options")->Name = "CompilerOptions";
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
            // the definitions page last: its reload rebases the toolchain pages' untouched values
            for each (KeyValuePair<String^, OptionsPage^> kv in pages) if (kv.Key != "TCDEF") kv.Value->Commit();
            OptionsPage^ defs;
            if (pages->TryGetValue("TCDEF", defs)) defs->Commit();
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
            if (key == "RidePaths") return gcnew RidePathsPage(work);
            if (key == "CompilerOptions") return gcnew CompilerOptionsPage(work);
            if (key == "TCDEF") return gcnew ToolchainDefinitionsPage(work);
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
