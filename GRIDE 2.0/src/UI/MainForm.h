#pragma once
// ============================================================================
//  GRIDE 2.0 - Main window (Phases 2, 4, 7-12)
//
//  +-------------------------------------------------------------------+
//  | Menu  /  Toolbar  (config + target combos, Build, Run, Stop)       |
//  +--------------+-------------------------------------+--------------+
//  | Solution     |  Editor tabs                        | Properties   |
//  | Explorer     |                                     |              |
//  |              +-------------------------------------+--------------+
//  |              |  Output | Error List                               |
//  +--------------+----------------------------------------------------+
//  | Status bar: message | Ln/Col | language | encoding | target|config |
//  +-------------------------------------------------------------------+
// ============================================================================
#include "OptionsDialog.h"

using namespace System::Collections::Concurrent;
using System::Threading::CancellationTokenSource;

#define GRIDE_EH(fn) gcnew EventHandler(this, &MainForm::fn)

namespace Gride
{
    public enum class NodeKind { Project, Folder, File, Info };

    public ref class NodeInfo
    {
    public:
        NodeInfo(NodeKind k, String^ p) { Kind = k; FilePath = p; }
        property NodeKind Kind;
        property String^ FilePath;
    };

    public enum class JobKind { Build, Rebuild, Clean, Run, BuildAndRun, Convert, CompileFile };

    public ref class BuildJob
    {
    public:
        property JobKind Kind;
        property GrideProject^ Project;
        property IdeSettings^ Settings;
        property ToolDefinition^ Tool;
        property String^ InputFile;
        property String^ OutputFile;
        property String^ WorkDir;
        property BuildResult^ Result;
        property int ExitCode;
        property String^ SingleFile;                      // CompileFile
        property System::Threading::CancellationToken Token;
    };

    public ref class MainForm : public Form
    {
    public:
        MainForm(array<String^>^ args)
        {
            startupArgs = args;
            String^ warning = nullptr;
            settings = IdeSettings::Load(warning);
            settingsWarning = warning;
            outQueue = gcnew ConcurrentQueue<String^>();
            msgQueue = gcnew ConcurrentQueue<BuildMessage^>();
            parser = gcnew ErrorParser();
            untitledCounter = 0;

            SuspendLayout();
            BuildLayout();
            BuildMenus();
            BuildToolbar();
            BuildStatusBar();
            // Docking: fill first, then bottom / top bars (last added docks first).
            Controls->Add(splitMain);
            Controls->Add(statusBar);
            Controls->Add(toolbar);
            Controls->Add(menu);
            MainMenuStrip = menu;
            ResumeLayout(true);

            worker = gcnew BackgroundWorker();
            worker->DoWork += gcnew DoWorkEventHandler(this, &MainForm::OnWorkerDoWork);
            worker->RunWorkerCompleted += gcnew RunWorkerCompletedEventHandler(this, &MainForm::OnWorkerCompleted);

            flushTimer = gcnew System::Windows::Forms::Timer();
            flushTimer->Interval = 100;
            flushTimer->Tick += GRIDE_EH(OnFlushTick);
            flushTimer->Start();

            findDialog = gcnew FindReplaceDialog(gcnew Func<CodeEditor^>(this, &MainForm::ActiveEditor));

            AllowDrop = true;
            DragEnter += gcnew DragEventHandler(this, &MainForm::OnDragEnter);
            DragDrop += gcnew DragEventHandler(this, &MainForm::OnDragDrop);
            FormClosing += gcnew FormClosingEventHandler(this, &MainForm::OnMainFormClosing);

            RestoreWindowBounds();
            UpdateUi();
        }

        CodeEditor^ ActiveEditor()
        {
            if (docs == nullptr || docs->SelectedTab == nullptr) return nullptr;
            return dynamic_cast<CodeEditor^>(docs->SelectedTab->Tag);
        }

    protected:
        virtual void OnShown(EventArgs^ e) override
        {
            Form::OnShown(e);
            RestoreSplitters();
            if (settingsWarning != nullptr) Ui::Error(this, settingsWarning);

            bool opened = false;
            if (startupArgs != nullptr)
            {
                for each (String^ a in startupArgs)
                {
                    if (!File::Exists(a)) continue;
                    if (IsProjectFile(a)) { OpenProject(a); opened = true; }
                    else OpenFile(a);
                }
            }
            if (!opened && settings->ReopenLastProject && !String::IsNullOrEmpty(settings->LastProject) &&
                File::Exists(settings->LastProject))
                OpenProject(settings->LastProject);
            SetStatus("Ready");
        }

        ~MainForm()
        {
            if (flushTimer != nullptr) { flushTimer->Stop(); delete flushTimer; }
        }

    private:
        // ------------------------------------------------------------ state
        IdeSettings^ settings;
        String^ settingsWarning;
        GrideProject^ project;
        DateTime projectDiskTime;           // last write time of the .pro we know about
        array<String^>^ startupArgs;
        int untitledCounter;
        bool busy;
        bool updatingCombos;
        int errorCount;
        int warningCount;

        BackgroundWorker^ worker;
        CancellationTokenSource^ cts;
        ConcurrentQueue<String^>^ outQueue;
        ConcurrentQueue<BuildMessage^>^ msgQueue;
        System::Windows::Forms::Timer^ flushTimer;
        ErrorParser^ parser;
        FindReplaceDialog^ findDialog;

        // ------------------------------------------------------------ controls
        MenuStrip^ menu;
        ToolStrip^ toolbar;
        StatusStrip^ statusBar;
        SplitContainer^ splitMain;
        SplitContainer^ splitCenter;
        SplitContainer^ splitRight;
        TreeView^ tree;
        System::Windows::Forms::ContextMenuStrip^ treeMenu;
        TabControl^ docs;
        System::Windows::Forms::ContextMenuStrip^ docMenu;
        TabPage^ docMenuPage;
        PropertyGrid^ props;
        TabControl^ bottomTabs;
        TabPage^ pageOutput;
        TabPage^ pageErrors;
        TextBox^ txtOutput;
        ListView^ lvErrors;

        ToolStripStatusLabel^ lblStatus;
        ToolStripStatusLabel^ lblPos;
        ToolStripStatusLabel^ lblLang;
        ToolStripStatusLabel^ lblEnc;
        ToolStripStatusLabel^ lblTarget;
        ToolStripComboBox^ cboConfig;
        ToolStripComboBox^ cboTarget;

        ToolStripMenuItem^ miRecentFiles;
        ToolStripMenuItem^ miRecentProjects;
        ToolStripMenuItem^ miCloseFile;
        ToolStripMenuItem^ miCloseProject;
        ToolStripMenuItem^ miSave;
        ToolStripMenuItem^ miSaveAs;
        ToolStripMenuItem^ miSaveAll;
        ToolStripMenuItem^ miUndo;
        ToolStripMenuItem^ miRedo;
        ToolStripMenuItem^ miAddNew;
        ToolStripMenuItem^ miAddExisting;
        ToolStripMenuItem^ miRemove;
        ToolStripMenuItem^ miProjProps;
        ToolStripMenuItem^ miBuild;
        ToolStripMenuItem^ miCompileFile;
        ToolStripMenuItem^ miEdit;
        bool checkingDisk;
        bool closing;
        ToolStripMenuItem^ miRebuild;
        ToolStripMenuItem^ miClean;
        ToolStripMenuItem^ miBuildStop;
        ToolStripMenuItem^ miBuildRun;
        ToolStripMenuItem^ miRun;
        ToolStripMenuItem^ miStop;
        ToolStripMenuItem^ miConvC2S;
        ToolStripMenuItem^ miConvS2C;
        ToolStripMenuItem^ miViewExplorer;
        ToolStripMenuItem^ miViewProps;
        ToolStripMenuItem^ miViewBottom;
        ToolStripButton^ tbSave;
        ToolStripButton^ tbSaveAll;
        ToolStripButton^ tbBuild;
        ToolStripButton^ tbRun;
        ToolStripButton^ tbStop;

        // tree context menu items
        ToolStripMenuItem^ tmOpen;
        ToolStripMenuItem^ tmOpenFolder;
        ToolStripMenuItem^ tmAddNew;
        ToolStripMenuItem^ tmAddExisting;
        ToolStripMenuItem^ tmRename;
        ToolStripMenuItem^ tmRemove;
        ToolStripMenuItem^ tmProps;

        // ============================================================ layout
        static Panel^ MakeToolWindow(String^ title, Control^ content)
        {
            Panel^ p = gcnew Panel();
            p->Dock = DockStyle::Fill;
            Label^ header = gcnew Label();
            header->Text = title;
            header->Dock = DockStyle::Top;
            header->Height = 22;
            header->TextAlign = ContentAlignment::MiddleLeft;
            header->Padding = System::Windows::Forms::Padding(6, 0, 0, 0);
            header->BackColor = Color::FromArgb(0, 122, 204);
            header->ForeColor = Color::White;
            content->Dock = DockStyle::Fill;
            p->Controls->Add(content);
            p->Controls->Add(header);
            return p;
        }

        void BuildLayout()
        {
            // Font first, then sizes (predictable layout at any DPI setting).
            AutoScaleMode = System::Windows::Forms::AutoScaleMode::None;
            Font = SystemFonts::MessageBoxFont;
            Text = AppInfo::Name;
            Size = System::Drawing::Size(1280, 820);
            MinimumSize = System::Drawing::Size(800, 500);
            StartPosition = FormStartPosition::CenterScreen;
            Icon = SystemIcons::Application;

            // ---- Solution Explorer
            tree = gcnew TreeView();
            tree->HideSelection = false;
            tree->ShowNodeToolTips = true;
            tree->LabelEdit = false;
            tree->AllowDrop = true;
            tree->NodeMouseDoubleClick += gcnew TreeNodeMouseClickEventHandler(this, &MainForm::OnTreeDoubleClick);
            tree->NodeMouseClick += gcnew TreeNodeMouseClickEventHandler(this, &MainForm::OnTreeClick);
            tree->AfterSelect += gcnew TreeViewEventHandler(this, &MainForm::OnTreeAfterSelect);
            tree->KeyDown += gcnew KeyEventHandler(this, &MainForm::OnTreeKeyDown);
            tree->DragEnter += gcnew DragEventHandler(this, &MainForm::OnDragEnter);
            tree->DragDrop += gcnew DragEventHandler(this, &MainForm::OnDragDrop);
            BuildTreeMenu();
            tree->ContextMenuStrip = treeMenu;

            // ---- documents
            docs = gcnew TabControl();
            docs->ShowToolTips = true;
            docs->AllowDrop = true;
            docs->SelectedIndexChanged += GRIDE_EH(OnDocChanged);
            docs->MouseUp += gcnew MouseEventHandler(this, &MainForm::OnDocsMouseUp);
            docs->DragEnter += gcnew DragEventHandler(this, &MainForm::OnDragEnter);
            docs->DragDrop += gcnew DragEventHandler(this, &MainForm::OnDragDrop);
            docMenu = gcnew System::Windows::Forms::ContextMenuStrip();
            docMenu->Items->Add("Close", nullptr, GRIDE_EH(OnDocMenuClose));
            docMenu->Items->Add("Close All But This", nullptr, GRIDE_EH(OnDocMenuCloseOthers));
            docMenu->Items->Add("Close All", nullptr, GRIDE_EH(OnDocMenuCloseAll));
            docMenu->Items->Add(gcnew ToolStripSeparator());
            docMenu->Items->Add("Copy Full Path", nullptr, GRIDE_EH(OnDocMenuCopyPath));
            docMenu->Items->Add("Open Containing Folder", nullptr, GRIDE_EH(OnDocMenuOpenFolder));

            // ---- properties
            props = gcnew PropertyGrid();
            props->ToolbarVisible = false;
            props->HelpVisible = true;
            props->PropertySort = PropertySort::Categorized;

            // ---- output + error list
            txtOutput = gcnew TextBox();
            txtOutput->Multiline = true;
            txtOutput->ReadOnly = true;
            txtOutput->WordWrap = false;
            txtOutput->ScrollBars = System::Windows::Forms::ScrollBars::Both;
            txtOutput->MaxLength = Int32::MaxValue;
            txtOutput->Font = gcnew System::Drawing::Font("Consolas", 9.0f);
            txtOutput->BackColor = Color::White;
            txtOutput->Dock = DockStyle::Fill;
            txtOutput->DoubleClick += GRIDE_EH(OnOutputDoubleClick);

            lvErrors = gcnew ListView();
            lvErrors->View = System::Windows::Forms::View::Details;
            lvErrors->FullRowSelect = true;
            lvErrors->GridLines = true;
            lvErrors->HideSelection = false;
            lvErrors->Dock = DockStyle::Fill;
            lvErrors->Columns->Add("Type", 70);
            lvErrors->Columns->Add("Code", 80);
            lvErrors->Columns->Add("Description", 560);
            lvErrors->Columns->Add("File", 200);
            lvErrors->Columns->Add("Line", 60);
            lvErrors->Columns->Add("Tool", 140);
            lvErrors->DoubleClick += GRIDE_EH(OnErrorActivate);
            lvErrors->KeyDown += gcnew KeyEventHandler(this, &MainForm::OnErrorKeyDown);

            bottomTabs = gcnew TabControl();
            bottomTabs->Dock = DockStyle::Fill;
            pageOutput = gcnew TabPage("Output");
            pageOutput->Controls->Add(txtOutput);
            pageErrors = gcnew TabPage("Error List");
            pageErrors->Controls->Add(lvErrors);
            bottomTabs->TabPages->Add(pageOutput);
            bottomTabs->TabPages->Add(pageErrors);

            // ---- splitters
            splitRight = gcnew SplitContainer();
            splitRight->Dock = DockStyle::Fill;
            splitRight->Orientation = System::Windows::Forms::Orientation::Vertical;
            splitRight->FixedPanel = System::Windows::Forms::FixedPanel::Panel2;
            docs->Dock = DockStyle::Fill;
            splitRight->Panel1->Controls->Add(docs);
            splitRight->Panel2->Controls->Add(MakeToolWindow("Properties", props));

            splitCenter = gcnew SplitContainer();
            splitCenter->Dock = DockStyle::Fill;
            splitCenter->Orientation = System::Windows::Forms::Orientation::Horizontal;
            splitCenter->FixedPanel = System::Windows::Forms::FixedPanel::Panel2;
            splitCenter->Panel1->Controls->Add(splitRight);
            splitCenter->Panel2->Controls->Add(bottomTabs);

            splitMain = gcnew SplitContainer();
            splitMain->Dock = DockStyle::Fill;
            splitMain->Orientation = System::Windows::Forms::Orientation::Vertical;
            splitMain->FixedPanel = System::Windows::Forms::FixedPanel::Panel1;
            splitMain->Panel1->Controls->Add(MakeToolWindow("Solution Explorer", tree));
            splitMain->Panel2->Controls->Add(splitCenter);
        }

        void BuildTreeMenu()
        {
            treeMenu = gcnew System::Windows::Forms::ContextMenuStrip();
            tmOpen = gcnew ToolStripMenuItem("Open", nullptr, GRIDE_EH(OnTreeOpen));
            tmOpenFolder = gcnew ToolStripMenuItem("Open Containing Folder", nullptr, GRIDE_EH(OnTreeOpenFolder));
            tmAddNew = gcnew ToolStripMenuItem("Add New File...", nullptr, GRIDE_EH(OnAddNewFile));
            tmAddExisting = gcnew ToolStripMenuItem("Add Existing File...", nullptr, GRIDE_EH(OnAddExistingFile));
            tmRename = gcnew ToolStripMenuItem("Rename...", nullptr, GRIDE_EH(OnRenameFile));
            tmRemove = gcnew ToolStripMenuItem("Remove from Project", nullptr, GRIDE_EH(OnRemoveFile));
            tmProps = gcnew ToolStripMenuItem("Properties...", nullptr, GRIDE_EH(OnProjectProperties));
            treeMenu->Items->Add(tmOpen);
            treeMenu->Items->Add(tmOpenFolder);
            treeMenu->Items->Add(gcnew ToolStripSeparator());
            treeMenu->Items->Add(tmAddNew);
            treeMenu->Items->Add(tmAddExisting);
            treeMenu->Items->Add(gcnew ToolStripSeparator());
            treeMenu->Items->Add(tmRename);
            treeMenu->Items->Add(tmRemove);
            treeMenu->Items->Add(gcnew ToolStripSeparator());
            treeMenu->Items->Add(tmProps);
            treeMenu->Opening += gcnew CancelEventHandler(this, &MainForm::OnTreeMenuOpening);
        }

        ToolStripMenuItem^ AddItem(ToolStripMenuItem^ parent, String^ text, EventHandler^ handler, Keys keys, String^ keyText)
        {
            ToolStripMenuItem^ mi = gcnew ToolStripMenuItem(text);
            if (handler != nullptr) mi->Click += handler;
            if (keys != Keys::None) mi->ShortcutKeys = keys;
            if (keyText != nullptr) mi->ShortcutKeyDisplayString = keyText;
            parent->DropDownItems->Add(mi);
            return mi;
        }

        static void AddSep(ToolStripMenuItem^ parent) { parent->DropDownItems->Add(gcnew ToolStripSeparator()); }

        void BuildMenus()
        {
            menu = gcnew MenuStrip();
            ToolStripMenuItem^ file = gcnew ToolStripMenuItem("&File");
            ToolStripMenuItem^ edit = gcnew ToolStripMenuItem("&Edit");
            miEdit = edit;
            edit->DropDownOpening += GRIDE_EH(OnEditMenuOpening);
            ToolStripMenuItem^ view = gcnew ToolStripMenuItem("&View");
            ToolStripMenuItem^ proj = gcnew ToolStripMenuItem("&Project");
            ToolStripMenuItem^ build = gcnew ToolStripMenuItem("&Build");
            ToolStripMenuItem^ debug = gcnew ToolStripMenuItem("&Debug");
            ToolStripMenuItem^ tools = gcnew ToolStripMenuItem("&Tools");
            ToolStripMenuItem^ help = gcnew ToolStripMenuItem("&Help");

            // ---- File
            AddItem(file, "New &Project...", GRIDE_EH(OnNewProject), Keys::Control | Keys::Shift | Keys::N, nullptr);
            AddItem(file, "&New File", GRIDE_EH(OnNewFile), Keys::Control | Keys::N, nullptr);
            AddSep(file);
            AddItem(file, "Open P&roject...", GRIDE_EH(OnOpenProject), Keys::Control | Keys::Shift | Keys::O, nullptr);
            AddItem(file, "&Open File...", GRIDE_EH(OnOpenFile), Keys::Control | Keys::O, nullptr);
            AddItem(file, "&Import CCS 7.4 Project (read-only)...", GRIDE_EH(OnImportCcs), Keys::None, nullptr);
            AddSep(file);
            miCloseFile = AddItem(file, "&Close File", GRIDE_EH(OnCloseFile), Keys::Control | Keys::F4, nullptr);
            miCloseProject = AddItem(file, "Close Projec&t", GRIDE_EH(OnCloseProject), Keys::None, nullptr);
            AddSep(file);
            miSave = AddItem(file, "&Save", GRIDE_EH(OnSave), Keys::Control | Keys::S, nullptr);
            miSaveAs = AddItem(file, "Save &As...", GRIDE_EH(OnSaveAs), Keys::None, nullptr);
            miSaveAll = AddItem(file, "Save A&ll", GRIDE_EH(OnSaveAll), Keys::Control | Keys::Shift | Keys::S, nullptr);
            AddSep(file);
            miRecentFiles = AddItem(file, "Recent &Files", nullptr, Keys::None, nullptr);
            miRecentFiles->DropDownItems->Add("(none)");
            miRecentFiles->DropDownOpening += GRIDE_EH(OnRecentFilesOpening);
            miRecentProjects = AddItem(file, "Recent Pro&jects", nullptr, Keys::None, nullptr);
            miRecentProjects->DropDownItems->Add("(none)");
            miRecentProjects->DropDownOpening += GRIDE_EH(OnRecentProjectsOpening);
            AddSep(file);
            AddItem(file, "E&xit", GRIDE_EH(OnExit), Keys::None, "Alt+F4");

            // ---- Edit (editor handles the clipboard keys itself; menu shows them only)
            miUndo = AddItem(edit, "&Undo", GRIDE_EH(OnUndo), Keys::None, "Ctrl+Z");
            miRedo = AddItem(edit, "&Redo", GRIDE_EH(OnRedo), Keys::None, "Ctrl+Y");
            AddSep(edit);
            AddItem(edit, "Cu&t", GRIDE_EH(OnCut), Keys::None, "Ctrl+X");
            AddItem(edit, "&Copy", GRIDE_EH(OnCopy), Keys::None, "Ctrl+C");
            AddItem(edit, "&Paste", GRIDE_EH(OnPaste), Keys::None, "Ctrl+V");
            AddItem(edit, "Select &All", GRIDE_EH(OnSelectAll), Keys::None, "Ctrl+A");
            AddSep(edit);
            AddItem(edit, "&Find...", GRIDE_EH(OnFind), Keys::Control | Keys::F, nullptr);
            AddItem(edit, "R&eplace...", GRIDE_EH(OnReplace), Keys::Control | Keys::H, nullptr);
            AddItem(edit, "Find &Next", GRIDE_EH(OnFindNext), Keys::F3, nullptr);
            AddItem(edit, "&Go To Line...", GRIDE_EH(OnGoToLine), Keys::Control | Keys::G, nullptr);
            AddItem(edit, "Find in F&iles...", GRIDE_EH(OnFindInFiles), Keys::Control | Keys::Shift | Keys::F, nullptr);
            AddSep(edit);
            AddItem(edit, "Next Document", GRIDE_EH(OnNextDocument), Keys::None, "Ctrl+Tab");
            AddItem(edit, "Previous Document", GRIDE_EH(OnPrevDocument), Keys::None, "Ctrl+Shift+Tab");

            // ---- View
            miViewExplorer = AddItem(view, "&Solution Explorer", GRIDE_EH(OnToggleExplorer), Keys::Control | Keys::Alt | Keys::L, nullptr);
            miViewProps = AddItem(view, "&Properties Window", GRIDE_EH(OnToggleProps), Keys::F4, nullptr);
            miViewBottom = AddItem(view, "Output / &Error Panel", GRIDE_EH(OnToggleBottom), Keys::Control | Keys::Alt | Keys::O, nullptr);
            AddSep(view);
            AddItem(view, "&Output", GRIDE_EH(OnShowOutput), Keys::None, nullptr);
            AddItem(view, "Error &List", GRIDE_EH(OnShowErrors), Keys::Control | Keys::Alt | Keys::E, nullptr);
            miViewExplorer->Checked = true;
            miViewProps->Checked = true;
            miViewBottom->Checked = true;

            // ---- Project
            miAddNew = AddItem(proj, "Add &New File...", GRIDE_EH(OnAddNewFile), Keys::Control | Keys::Shift | Keys::A, nullptr);
            miAddExisting = AddItem(proj, "Add &Existing File...", GRIDE_EH(OnAddExistingFile), Keys::Shift | Keys::Alt | Keys::A, nullptr);
            miRemove = AddItem(proj, "&Remove from Project", GRIDE_EH(OnRemoveFile), Keys::None, nullptr);
            AddSep(proj);
            miProjProps = AddItem(proj, "&Properties...", GRIDE_EH(OnProjectProperties), Keys::Alt | Keys::F7, nullptr);

            // ---- Build
            miBuild = AddItem(build, "&Build Project", GRIDE_EH(OnBuild), Keys::Control | Keys::Shift | Keys::B, nullptr);
            miCompileFile = AddItem(build, "Com&pile Current File", GRIDE_EH(OnCompileFile), Keys::Control | Keys::F7, nullptr);
            miRebuild = AddItem(build, "&Rebuild Project", GRIDE_EH(OnRebuild), Keys::None, nullptr);
            miClean = AddItem(build, "&Clean Project", GRIDE_EH(OnClean), Keys::None, nullptr);
            AddSep(build);
            miBuildStop = AddItem(build, "Cancel Build", GRIDE_EH(OnStop), Keys::Control | Keys::Cancel, "Ctrl+Break");

            // ---- Debug (run on target: native console, VM6747, or configured runner)
            miBuildRun = AddItem(debug, "Build and &Run", GRIDE_EH(OnBuildAndRun), Keys::F5, nullptr);
            miRun = AddItem(debug, "Run &Without Building", GRIDE_EH(OnRun), Keys::Control | Keys::F5, nullptr);
            miStop = AddItem(debug, "&Stop", GRIDE_EH(OnStop), Keys::Shift | Keys::F5, nullptr);

            // ---- Tools
            miConvC2S = AddItem(tools, "Convert C -> &Shalimar", GRIDE_EH(OnConvertCToShalimar), Keys::None, nullptr);
            miConvS2C = AddItem(tools, "Convert Shalimar -> &C", GRIDE_EH(OnConvertShalimarToC), Keys::None, nullptr);
            AddSep(tools);
            AddItem(tools, "&Options...", GRIDE_EH(OnOptions), Keys::None, nullptr);
            AddItem(tools, "&Toolchain Settings...", GRIDE_EH(OnToolchainOptions), Keys::None, nullptr);

            // ---- Help
            AddItem(help, "&Placeholder Reference", GRIDE_EH(OnPlaceholderHelp), Keys::None, nullptr);
            AddItem(help, "&Keyboard Shortcuts", GRIDE_EH(OnShortcutHelp), Keys::None, nullptr);
            AddSep(help);
            AddItem(help, "&About GRIDE", GRIDE_EH(OnAbout), Keys::None, nullptr);

            menu->Items->AddRange(gcnew array<ToolStripItem^>{ file, edit, view, proj, build, debug, tools, help });
        }

        ToolStripButton^ AddButton(String^ text, String^ tip, EventHandler^ handler)
        {
            ToolStripButton^ b = gcnew ToolStripButton(text);
            b->DisplayStyle = ToolStripItemDisplayStyle::Text;
            b->ToolTipText = tip;
            b->Click += handler;
            toolbar->Items->Add(b);
            return b;
        }

        void BuildToolbar()
        {
            toolbar = gcnew ToolStrip();
            toolbar->GripStyle = ToolStripGripStyle::Hidden;
            AddButton("New Project", "New Project (Ctrl+Shift+N)", GRIDE_EH(OnNewProject));
            AddButton("Open", "Open Project (Ctrl+Shift+O)", GRIDE_EH(OnOpenProject));
            tbSave = AddButton("Save", "Save (Ctrl+S)", GRIDE_EH(OnSave));
            tbSaveAll = AddButton("Save All", "Save All (Ctrl+Shift+S)", GRIDE_EH(OnSaveAll));
            toolbar->Items->Add(gcnew ToolStripSeparator());

            toolbar->Items->Add(gcnew ToolStripLabel("Config:"));
            cboConfig = gcnew ToolStripComboBox();
            cboConfig->DropDownStyle = ComboBoxStyle::DropDownList;
            cboConfig->Width = 100;
            cboConfig->SelectedIndexChanged += GRIDE_EH(OnConfigChanged);
            toolbar->Items->Add(cboConfig);
            toolbar->Items->Add(gcnew ToolStripLabel("Target:"));
            cboTarget = gcnew ToolStripComboBox();
            cboTarget->DropDownStyle = ComboBoxStyle::DropDownList;
            cboTarget->Width = 100;
            for each (String^ n in Names::TargetNames()) cboTarget->Items->Add(n);
            cboTarget->SelectedIndexChanged += GRIDE_EH(OnTargetChanged);
            toolbar->Items->Add(cboTarget);
            toolbar->Items->Add(gcnew ToolStripSeparator());

            tbBuild = AddButton("Build", "Build Project (Ctrl+Shift+B)", GRIDE_EH(OnBuild));
            tbRun = AddButton("Run", "Build and Run (F5)", GRIDE_EH(OnBuildAndRun));
            tbStop = AddButton("Stop", "Stop (Shift+F5)", GRIDE_EH(OnStop));
        }

        void BuildStatusBar()
        {
            statusBar = gcnew StatusStrip();
            lblStatus = gcnew ToolStripStatusLabel("Ready");
            lblStatus->Spring = true;
            lblStatus->TextAlign = ContentAlignment::MiddleLeft;
            lblPos = gcnew ToolStripStatusLabel("");
            lblLang = gcnew ToolStripStatusLabel("");
            lblEnc = gcnew ToolStripStatusLabel("");
            lblTarget = gcnew ToolStripStatusLabel("");
            lblPos->AutoSize = true;
            statusBar->Items->AddRange(gcnew array<ToolStripItem^>{ lblStatus, lblPos, lblLang, lblEnc, lblTarget });
        }

        // ============================================================ window layout persistence
        void RestoreWindowBounds()
        {
            if (settings->WinW < 400 || settings->WinH < 300) return;
            Rectangle r(settings->WinX, settings->WinY, settings->WinW, settings->WinH);
            bool visible = false;
            for each (Screen^ s in Screen::AllScreens)
                if (s->WorkingArea.IntersectsWith(r)) { visible = true; break; }
            if (!visible) return;
            StartPosition = FormStartPosition::Manual;
            Bounds = r;
            if (settings->WinMaximized) WindowState = FormWindowState::Maximized;
        }

        void RestoreSplitters()
        {
            // one try per splitter, so one bad value does not stop the others
            try { splitMain->SplitterDistance = Math::Min(settings->ExplorerWidth, Math::Max(100, splitMain->Width - 300)); }
            catch (Exception^) {}
            try { splitCenter->SplitterDistance = Math::Max(100, splitCenter->Height - settings->BottomHeight); }
            catch (Exception^) {}
            try { splitRight->SplitterDistance = Math::Max(150, splitRight->Width - settings->PropertiesWidth); }
            catch (Exception^) {}
        }

        void SaveLayout()
        {
            settings->WinMaximized = WindowState == FormWindowState::Maximized;
            Rectangle r = WindowState == FormWindowState::Normal ? Bounds : RestoreBounds;
            settings->WinX = r.X; settings->WinY = r.Y; settings->WinW = r.Width; settings->WinH = r.Height;
            if (WindowState == FormWindowState::Minimized) return;      // splitter sizes are meaningless then
            if (!splitMain->Panel1Collapsed) settings->ExplorerWidth = splitMain->SplitterDistance;
            if (!splitCenter->Panel2Collapsed) settings->BottomHeight = splitCenter->Height - splitCenter->SplitterDistance;
            if (!splitRight->Panel2Collapsed) settings->PropertiesWidth = splitRight->Width - splitRight->SplitterDistance;
        }

        void SaveSettings()
        {
            String^ err = nullptr;
            if (!settings->Save(err)) SetStatus(String::Format("Could not save settings: {0}", err));
        }

        // ============================================================ UI state
        void SetStatus(String^ s) { lblStatus->Text = s; }

        void UpdateUi()
        {
            CodeEditor^ ed = ActiveEditor();
            bool hasProject = project != nullptr;
            bool writable = hasProject && !project->IsReadOnly;
            bool canBuild = writable && !busy;

            miCloseFile->Enabled = ed != nullptr;
            miCloseProject->Enabled = hasProject && !busy;
            miSave->Enabled = ed != nullptr && !ed->IsReadOnly;
            miSaveAs->Enabled = ed != nullptr;
            miSaveAll->Enabled = docs->TabPages->Count > 0;
            tbSave->Enabled = miSave->Enabled;
            tbSaveAll->Enabled = miSaveAll->Enabled;
            miUndo->Enabled = ed != nullptr && ed->CanUndo;
            miRedo->Enabled = ed != nullptr && ed->CanRedo;
            miAddNew->Enabled = writable && !busy;
            miAddExisting->Enabled = writable && !busy;
            miRemove->Enabled = writable && !busy;
            miProjProps->Enabled = hasProject;
            miBuild->Enabled = canBuild;
            miCompileFile->Enabled = canBuild && ed != nullptr && ed->FilePath != nullptr &&
                                     TargetInfo::IsSource(Path::GetExtension(ed->FilePath), settings) &&
                                     project->ContainsFile(ed->FilePath);
            miRebuild->Enabled = canBuild;
            miClean->Enabled = canBuild;
            miBuildRun->Enabled = canBuild;
            miRun->Enabled = canBuild;
            miStop->Enabled = busy;
            miBuildStop->Enabled = busy;
            tbBuild->Enabled = canBuild;
            tbRun->Enabled = canBuild;
            tbStop->Enabled = busy;
            cboConfig->Enabled = writable && !busy;
            cboTarget->Enabled = writable && !busy;

            String^ ext = (ed != nullptr && ed->FilePath != nullptr) ? Path::GetExtension(ed->FilePath) : String::Empty;
            miConvC2S->Enabled = !busy && ed != nullptr && ed->FilePath != nullptr && String::Equals(ext, ".c", StringComparison::OrdinalIgnoreCase);
            miConvS2C->Enabled = !busy && ed != nullptr && ed->FilePath != nullptr && settings->IsShalimarExtension(ext);

            // title
            String^ title = AppInfo::Name;
            if (hasProject)
            {
                String^ ro = project->IsReadOnly ? gcnew String(" [CCS, read-only]") : String::Empty;
                title = String::Format("{0}{1} - {2}", project->Name, ro, AppInfo::Name);
            }
            if (ed != nullptr) title = String::Format("{0}{1} - {2}", ed->DisplayName, ed->IsDirty ? gcnew String("*") : String::Empty, title);
            Text = title;
            UpdateStatusLabels();
        }

        void UpdateStatusLabels()
        {
            CodeEditor^ ed = ActiveEditor();
            if (ed != nullptr)
            {
                lblPos->Text = String::Format("Ln {0}, Col {1}", ed->CurrentLine, ed->CurrentColumn);
                lblLang->Text = LanguageDefs::ModeName(ed->Mode);
                lblEnc->Text = String::Format("{0}  {1}", ed->EncodingLabel, ed->UsesCrLf ? gcnew String("CRLF") : gcnew String("LF"));
            }
            else
            {
                lblPos->Text = String::Empty;
                lblLang->Text = String::Empty;
                lblEnc->Text = String::Empty;
            }
            lblTarget->Text = project != nullptr
                ? String::Format("{0} | {1}", project->Target, project->ActiveConfig()->Name) : String::Empty;
        }

        void SetBusy(bool b)
        {
            busy = b;
            UseWaitCursor = false;
            UpdateUi();
        }

        // ============================================================ helpers
        static bool IsProjectFile(String^ path)
        {
            String^ name = Path::GetFileName(path);
            String^ ext = Path::GetExtension(path);
            return String::Equals(ext, ".pro", StringComparison::OrdinalIgnoreCase) ||
                   String::Equals(name, ".project", StringComparison::OrdinalIgnoreCase);
        }


        String^ DefaultFolder()
        {
            if (project != nullptr) return project->ProjectDir;
            return Environment::GetFolderPath(Environment::SpecialFolder::MyDocuments);
        }

        void ShowException(String^ what, Exception^ ex)
        {
            Ui::Error(this, String::Format("{0}\n\n{1}", what, ex->Message));
        }

        // ============================================================ project management
        void OpenProject(String^ path)
        {
            if (busy) { Ui::Info(this, "Please wait for the current build to finish."); return; }
            String^ full;
            try { full = Path::GetFullPath(path); }
            catch (Exception^ ex) { ShowException("Invalid project path.", ex); return; }
            if (project != nullptr && PathUtil::SamePath(project->FilePath, full)) return;
            if (!CloseProjectInternal()) return;

            GrideProject^ p = nullptr;
            try
            {
                if (String::Equals(Path::GetFileName(full), ".project", StringComparison::OrdinalIgnoreCase))
                    p = CcsImporter::Import(full);
                else
                    p = GrideProject::Load(full);
            }
            catch (Exception^ ex)
            {
                ShowException(String::Format("Could not open project:\n{0}", full), ex);
                return;
            }
            project = p;
            RememberProjectTime();
            settings->AddRecentProject(full);
            SaveSettings();
            RefreshTree();
            RefreshCombos();
            UpdateUi();
            if (project->IsReadOnly)
                SetStatus(String::Format("Imported CCS project '{0}' (read-only, {1} files)", project->Name, project->SourceFiles->Count));
            else
                SetStatus(String::Format("Opened project '{0}'", project->Name));
        }

        bool CloseProjectInternal()
        {
            if (project == nullptr) return true;
            if (!CloseAllDocuments(nullptr)) return false;
            project = nullptr;
            props->SelectedObject = nullptr;
            RefreshTree();
            RefreshCombos();
            UpdateUi();
            return true;
        }

        void SaveProject()
        {
            if (project == nullptr || project->IsReadOnly) return;
            try { project->Save(); }
            catch (Exception^ ex) { ShowException("Could not save the project file.", ex); return; }
            RememberProjectTime();
            // keep an open (unmodified) editor on the .pro file in sync
            TabPage^ t = FindTab(project->FilePath);
            CodeEditor^ ed = t != nullptr ? dynamic_cast<CodeEditor^>(t->Tag) : nullptr;
            if (ed != nullptr && !ed->IsDirty) ed->ReloadFromDisk();
        }

        void RefreshCombos()
        {
            updatingCombos = true;
            try
            {
                cboConfig->Items->Clear();
                if (project != nullptr)
                {
                    for each (BuildConfiguration^ c in project->Configurations) cboConfig->Items->Add(c->Name);
                    cboConfig->SelectedItem = project->ActiveConfig()->Name;
                    cboTarget->SelectedItem = project->Target;
                }
                else cboTarget->SelectedIndex = -1;
            }
            finally { updatingCombos = false; }
        }

        // ---- Solution Explorer
        TreeNode^ MakeFolderNode(String^ text)
        {
            TreeNode^ n = gcnew TreeNode(text);
            n->Tag = gcnew NodeInfo(NodeKind::Folder, nullptr);
            n->ForeColor = Color::FromArgb(0, 70, 140);
            return n;
        }

        void RefreshTree()
        {
            String^ selected = nullptr;
            NodeInfo^ sel = SelectedNodeInfo();
            if (sel != nullptr) selected = sel->FilePath;

            tree->BeginUpdate();
            tree->Nodes->Clear();
            if (project != nullptr)
            {
                String^ rootText = project->IsReadOnly
                    ? String::Format("{0}  (CCS 7.4, read-only)", project->Name)
                    : String::Format("{0}  [{1}, {2}]", project->Name, project->Language, project->Target);
                TreeNode^ root = gcnew TreeNode(rootText);
                root->Tag = gcnew NodeInfo(NodeKind::Project, project->FilePath);
                root->ToolTipText = project->FilePath;
                root->ForeColor = Color::FromArgb(0, 70, 140);
                TreeNode^ srcNode = MakeFolderNode("Source Files");
                TreeNode^ hdrNode = MakeFolderNode("Header Files");
                TreeNode^ otherNode = MakeFolderNode("Other Files");
                TreeNode^ toSelect = nullptr;

                for each (String^ rel in project->SourceFiles)
                {
                    String^ full = project->ResolvePath(rel);
                    String^ ext = Path::GetExtension(full);
                    TreeNode^ n = gcnew TreeNode(rel);
                    n->Tag = gcnew NodeInfo(NodeKind::File, full);
                    n->ToolTipText = full;
                    if (!File::Exists(full))
                    {
                        n->Text = String::Format("{0}  (missing)", rel);
                        n->ForeColor = Color::Gray;
                    }
                    if (TargetInfo::IsSource(ext, settings)) srcNode->Nodes->Add(n);
                    else if (TargetInfo::IsHeader(ext)) hdrNode->Nodes->Add(n);
                    else otherNode->Nodes->Add(n);
                    if (selected != nullptr && PathUtil::SamePath(selected, full)) toSelect = n;
                }
                root->Nodes->Add(srcNode);
                root->Nodes->Add(hdrNode);
                root->Nodes->Add(otherNode);

                if (project->CcsConfigurations != nullptr && project->CcsConfigurations->Count > 0)
                {
                    TreeNode^ cc = MakeFolderNode("CCS Build Configurations (read-only)");
                    for each (CcsConfiguration^ c in project->CcsConfigurations)
                    {
                        TreeNode^ cn = cc->Nodes->Add(c->Name);
                        cn->Tag = gcnew NodeInfo(NodeKind::Info, nullptr);
                        for each (String^ o in c->Options)
                        {
                            TreeNode^ on = cn->Nodes->Add(o);
                            on->Tag = gcnew NodeInfo(NodeKind::Info, nullptr);
                            on->ToolTipText = o;
                        }
                    }
                    root->Nodes->Add(cc);
                }
                tree->Nodes->Add(root);
                root->Expand();
                srcNode->Expand();
                hdrNode->Expand();
                otherNode->Expand();
                tree->SelectedNode = toSelect != nullptr ? toSelect : root;
            }
            tree->EndUpdate();
            UpdatePropertyGrid();
        }

        NodeInfo^ SelectedNodeInfo()
        {
            if (tree->SelectedNode == nullptr) return nullptr;
            return dynamic_cast<NodeInfo^>(tree->SelectedNode->Tag);
        }

        String^ SelectedFile()
        {
            NodeInfo^ ni = SelectedNodeInfo();
            return (ni != nullptr && ni->Kind == NodeKind::File) ? ni->FilePath : nullptr;
        }

        void UpdatePropertyGrid()
        {
            NodeInfo^ ni = SelectedNodeInfo();
            if (ni == nullptr || project == nullptr) { props->SelectedObject = nullptr; return; }
            if (ni->Kind == NodeKind::File)
                props->SelectedObject = gcnew FileInfoView(ni->FilePath, project, settings);
            else if (ni->Kind == NodeKind::Project)
            {
                // read-only here; edit through Project > Properties
                ProjectPropertiesView^ v = ProjectPropertiesView::FromProject(project);
                TypeDescriptor::AddAttributes(v, gcnew ReadOnlyAttribute(true));
                props->SelectedObject = v;
            }
            else props->SelectedObject = nullptr;
        }

        void OnTreeDoubleClick(Object^ sender, TreeNodeMouseClickEventArgs^ e)
        {
            NodeInfo^ ni = dynamic_cast<NodeInfo^>(e->Node->Tag);
            if (ni != nullptr && ni->Kind == NodeKind::File) OpenFile(ni->FilePath);
        }

        void OnTreeClick(Object^ sender, TreeNodeMouseClickEventArgs^ e)
        {
            if (e->Button == System::Windows::Forms::MouseButtons::Right) tree->SelectedNode = e->Node;
        }

        void OnTreeAfterSelect(Object^ sender, TreeViewEventArgs^ e) { UpdatePropertyGrid(); }

        void OnTreeKeyDown(Object^ sender, KeyEventArgs^ e)
        {
            if (e->KeyCode == Keys::Enter && SelectedFile() != nullptr) { OpenFile(SelectedFile()); e->Handled = true; }
            else if (e->KeyCode == Keys::Delete && SelectedFile() != nullptr) { OnRemoveFile(nullptr, nullptr); e->Handled = true; }
            else if (e->KeyCode == Keys::F2 && SelectedFile() != nullptr) { OnRenameFile(nullptr, nullptr); e->Handled = true; }
        }

        void OnTreeMenuOpening(Object^ sender, CancelEventArgs^ e)
        {
            NodeInfo^ ni = SelectedNodeInfo();
            bool isFile = ni != nullptr && ni->Kind == NodeKind::File;
            bool writable = project != nullptr && !project->IsReadOnly && !busy;
            tmOpen->Enabled = isFile;
            tmOpenFolder->Enabled = isFile || (ni != nullptr && ni->Kind == NodeKind::Project);
            tmAddNew->Enabled = writable;
            tmAddExisting->Enabled = writable;
            tmRename->Enabled = isFile && writable;
            tmRemove->Enabled = isFile && writable;
            tmProps->Enabled = project != nullptr;
            if (project == nullptr) e->Cancel = true;
        }

        void OnTreeOpen(Object^ sender, EventArgs^ e) { if (SelectedFile() != nullptr) OpenFile(SelectedFile()); }

        void OnTreeOpenFolder(Object^ sender, EventArgs^ e)
        {
            NodeInfo^ ni = SelectedNodeInfo();
            if (ni == nullptr || ni->FilePath == nullptr) return;
            OpenInExplorer(ni->FilePath);
        }

        void OpenInExplorer(String^ path)
        {
            try
            {
                if (File::Exists(path)) Process::Start("explorer.exe", String::Format("/select,\"{0}\"", path));
                else if (Directory::Exists(Path::GetDirectoryName(path))) Process::Start("explorer.exe", PathUtil::Quote(Path::GetDirectoryName(path)));
            }
            catch (Exception^ ex) { ShowException("Could not open Explorer.", ex); }
        }

        // ============================================================ documents
        CodeEditor^ CreateEditor()
        {
            CodeEditor^ ed = gcnew CodeEditor(settings);
            ed->Dock = DockStyle::Fill;
            ed->DirtyChanged += GRIDE_EH(OnEditorDirtyChanged);
            ed->CaretChanged += GRIDE_EH(OnEditorCaretChanged);
            return ed;
        }

        TabPage^ AddEditorTab(CodeEditor^ ed)
        {
            TabPage^ page = gcnew TabPage(ed->DisplayName);
            page->Tag = ed;
            page->ToolTipText = ed->FilePath != nullptr ? ed->FilePath : ed->DisplayName;
            page->Controls->Add(ed);
            docs->TabPages->Add(page);
            docs->SelectedTab = page;
            ed->FocusEditor();
            UpdateUi();
            return page;
        }

        TabPage^ FindTab(String^ full)
        {
            for each (TabPage^ p in docs->TabPages)
            {
                CodeEditor^ ed = dynamic_cast<CodeEditor^>(p->Tag);
                if (ed != nullptr && ed->FilePath != nullptr && PathUtil::SamePath(ed->FilePath, full)) return p;
            }
            return nullptr;
        }

        CodeEditor^ OpenFile(String^ path)
        {
            String^ full;
            try { full = Path::GetFullPath(path); }
            catch (Exception^ ex) { ShowException("Invalid file path.", ex); return nullptr; }

            TabPage^ existing = FindTab(full);
            if (existing != nullptr)
            {
                docs->SelectedTab = existing;
                CodeEditor^ ex = safe_cast<CodeEditor^>(existing->Tag);
                ex->FocusEditor();
                return ex;
            }
            if (!File::Exists(full)) { Ui::Error(this, String::Format("File not found:\n{0}", full)); return nullptr; }

            CodeEditor^ ed = CreateEditor();
            try
            {
                ed->Mode = LanguageDefs::ModeForFile(full, settings, project);
                ed->LoadFile(full);
            }
            catch (Exception^ ex)
            {
                delete ed;
                ShowException(String::Format("Could not open file:\n{0}", full), ex);
                return nullptr;
            }
            ed->IsReadOnly = project != nullptr && project->IsReadOnly && project->ContainsFile(full);
            AddEditorTab(ed);
            settings->AddRecentFile(full);
            return ed;
        }

        void UpdateTabText(CodeEditor^ ed)
        {
            for each (TabPage^ p in docs->TabPages)
            {
                if (p->Tag != ed) continue;
                String^ t = ed->DisplayName;
                if (ed->IsDirty) t = String::Concat(t, " *");
                if (ed->IsReadOnly) t = String::Concat(t, " [RO]");
                if (!String::Equals(p->Text, t)) p->Text = t;
                p->ToolTipText = ed->FilePath != nullptr ? ed->FilePath : ed->DisplayName;
            }
        }

        // Returns false if the user cancelled.
        bool SaveEditor(CodeEditor^ ed, bool saveAs)
        {
            if (ed == nullptr) return true;
            if (ed->IsReadOnly && !saveAs) return true;
            String^ path = ed->FilePath;
            if (saveAs || path == nullptr)
            {
                SaveFileDialog^ d = gcnew SaveFileDialog();
                d->Filter = SourceFilter();
                d->InitialDirectory = path != nullptr ? Path::GetDirectoryName(path) : DefaultFolder();
                d->FileName = ed->DisplayName;
                d->AddExtension = true;
                try
                {
                    if (d->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) return false;
                    path = d->FileName;
                }
                finally { delete d; }
            }
            try
            {
                bool changedPath = !PathUtil::SamePath(ed->FilePath, path);
                ed->SaveFile(path);
                if (changedPath)
                {
                    ed->IsReadOnly = false;
                    ed->Mode = LanguageDefs::ModeForFile(path, settings, project);
                }
                settings->AddRecentFile(ed->FilePath);
                UpdateTabText(ed);
                SetStatus(String::Format("Saved {0}", ed->FilePath));
                // Saving the open project's .pro from the editor: reload it.
                if (project != nullptr && PathUtil::SamePath(ed->FilePath, project->FilePath)) ReloadProjectFromDisk();
                UpdateUi();
                return true;
            }
            catch (Exception^ ex)
            {
                ShowException(String::Format("Could not save:\n{0}", path), ex);
                return false;
            }
        }

        void RememberProjectTime()
        {
            try { projectDiskTime = project != nullptr && File::Exists(project->FilePath) ? File::GetLastWriteTimeUtc(project->FilePath) : DateTime::MinValue; }
            catch (Exception^) { projectDiskTime = DateTime::MinValue; }
        }

        void ReloadProjectFromDisk()
        {
            try
            {
                GrideProject^ p = GrideProject::Load(project->FilePath);
                project = p;
                RememberProjectTime();
                RefreshTree();
                RefreshCombos();
            }
            catch (Exception^ ex) { ShowException("The project file has errors and was not reloaded.", ex); }
        }

        bool SaveAll()
        {
            for each (TabPage^ p in docs->TabPages)
            {
                CodeEditor^ ed = dynamic_cast<CodeEditor^>(p->Tag);
                if (ed != nullptr && ed->IsDirty && !ed->IsReadOnly)
                    if (!SaveEditor(ed, false)) return false;
            }
            return true;
        }

        bool CloseTab(TabPage^ page)
        {
            CodeEditor^ ed = dynamic_cast<CodeEditor^>(page->Tag);
            if (ed != nullptr && ed->IsDirty && !ed->IsReadOnly)
            {
                docs->SelectedTab = page;
                System::Windows::Forms::DialogResult r = MessageBox::Show(this,
                    String::Format("Save changes to {0}?", ed->DisplayName), AppInfo::Name,
                    MessageBoxButtons::YesNoCancel, MessageBoxIcon::Question);
                if (r == System::Windows::Forms::DialogResult::Cancel) return false;
                if (r == System::Windows::Forms::DialogResult::Yes && !SaveEditor(ed, false)) return false;
            }
            docs->TabPages->Remove(page);
            delete page;          // disposes the editor too
            UpdateUi();
            return true;
        }

        // Closes every tab except 'keep'. Returns false if cancelled.
        bool CloseAllDocuments(TabPage^ keep)
        {
            // one prompt for all modified documents
            List<CodeEditor^>^ dirty = gcnew List<CodeEditor^>();
            for each (TabPage^ p in docs->TabPages)
            {
                if (p == keep) continue;
                CodeEditor^ ed = dynamic_cast<CodeEditor^>(p->Tag);
                if (ed != nullptr && ed->IsDirty && !ed->IsReadOnly) dirty->Add(ed);
            }
            if (dirty->Count > 0)
            {
                StringBuilder^ sb = gcnew StringBuilder("Save changes to the following files?\n\n");
                for each (CodeEditor^ ed in dirty) sb->AppendLine(ed->DisplayName);
                System::Windows::Forms::DialogResult r = MessageBox::Show(this, sb->ToString(), AppInfo::Name,
                    MessageBoxButtons::YesNoCancel, MessageBoxIcon::Question);
                if (r == System::Windows::Forms::DialogResult::Cancel) return false;
                if (r == System::Windows::Forms::DialogResult::Yes)
                    for each (CodeEditor^ ed in dirty) if (!SaveEditor(ed, false)) return false;
            }
            for (int i = docs->TabPages->Count - 1; i >= 0; i--)
            {
                TabPage^ p = docs->TabPages[i];
                if (p == keep) continue;
                docs->TabPages->RemoveAt(i);
                delete p;
            }
            UpdateUi();
            return true;
        }

        String^ SourceFilter()
        {
            String^ shal = String::Join(";", settings->ShalimarExtensionList()->ConvertAll(
                gcnew Converter<String^, String^>(&MainForm::ToWildcard))->ToArray());
            return String::Format(
                "Source files (*.c;*.cpp;*.h;*.hpp;{0};*.asm;*.s)|*.c;*.cpp;*.cc;*.cxx;*.h;*.hpp;{0};*.asm;*.s;*.inc|"
                "C90 (*.c;*.h)|*.c;*.h|C++11 (*.cpp;*.hpp)|*.cpp;*.cc;*.cxx;*.hpp;*.h|Shalimar ({0})|{0}|"
                "Assembly (*.asm;*.s)|*.asm;*.s;*.inc|Linker command (*.cmd)|*.cmd|All files (*.*)|*.*", shal);
        }

        static String^ ToWildcard(String^ ext) { return String::Concat("*", ext); }

        void OnEditorDirtyChanged(Object^ sender, EventArgs^ e)
        {
            CodeEditor^ ed = dynamic_cast<CodeEditor^>(sender);
            if (ed != nullptr) UpdateTabText(ed);
            UpdateUi();
        }

        void OnEditorCaretChanged(Object^ sender, EventArgs^ e)
        {
            if (sender == ActiveEditor()) UpdateStatusLabels();
        }

        void OnDocChanged(Object^ sender, EventArgs^ e)
        {
            UpdateUi();
            CodeEditor^ ed = ActiveEditor();
            if (ed != nullptr) ed->FocusEditor();
        }

        int TabIndexAt(Point pt)
        {
            for (int i = 0; i < docs->TabCount; i++)
                if (docs->GetTabRect(i).Contains(pt)) return i;
            return -1;
        }

        void OnDocsMouseUp(Object^ sender, MouseEventArgs^ e)
        {
            int i = TabIndexAt(e->Location);
            if (i < 0) return;
            if (e->Button == System::Windows::Forms::MouseButtons::Middle) CloseTab(docs->TabPages[i]);
            else if (e->Button == System::Windows::Forms::MouseButtons::Right)
            {
                docMenuPage = docs->TabPages[i];
                docMenu->Show(docs, e->Location);
            }
        }

        void OnDocMenuClose(Object^ sender, EventArgs^ e) { if (docMenuPage != nullptr) CloseTab(docMenuPage); }
        void OnDocMenuCloseOthers(Object^ sender, EventArgs^ e) { if (docMenuPage != nullptr) CloseAllDocuments(docMenuPage); }
        void OnDocMenuCloseAll(Object^ sender, EventArgs^ e) { CloseAllDocuments(nullptr); }

        void OnDocMenuCopyPath(Object^ sender, EventArgs^ e)
        {
            if (docMenuPage == nullptr) return;
            CodeEditor^ ed = dynamic_cast<CodeEditor^>(docMenuPage->Tag);
            if (ed != nullptr && ed->FilePath != nullptr)
            {
                try { Clipboard::SetText(ed->FilePath); } catch (ExternalException^) {}
            }
        }

        void OnDocMenuOpenFolder(Object^ sender, EventArgs^ e)
        {
            if (docMenuPage == nullptr) return;
            CodeEditor^ ed = dynamic_cast<CodeEditor^>(docMenuPage->Tag);
            if (ed != nullptr && ed->FilePath != nullptr) OpenInExplorer(ed->FilePath);
        }

        // ============================================================ File menu
        void OnNewProject(Object^ sender, EventArgs^ e)
        {
            if (busy) return;
            String^ loc = project != nullptr ? Path::GetDirectoryName(project->ProjectDir)
                                             : Environment::GetFolderPath(Environment::SpecialFolder::MyDocuments);
            NewProjectDialog^ d = gcnew NewProjectDialog(loc);
            try
            {
                if (d->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) return;
                if (!CloseProjectInternal()) return;
                String^ folder = d->ProjectFolder;
                Directory::CreateDirectory(folder);
                GrideProject^ p = GrideProject::Create(d->ProjectName, folder, d->Language, d->Target);
                String^ mainFile = nullptr;
                if (d->CreateMainFile)
                {
                    mainFile = Path::Combine(folder, MainFileName(d->Language));
                    if (!File::Exists(mainFile))
                        File::WriteAllText(mainFile, MainTemplate(d->Language, Path::GetFileName(mainFile)), gcnew UTF8Encoding(false));
                    p->AddFile(mainFile);
                }
                p->Save();
                OpenProject(p->FilePath);
                if (mainFile != nullptr) OpenFile(mainFile);
            }
            catch (Exception^ ex) { ShowException("Could not create the project.", ex); }
            finally { delete d; }
        }

        String^ MainFileName(LanguageKind lang)
        {
            switch (lang)
            {
            case LanguageKind::Cpp11:    return "main.cpp";
            case LanguageKind::Shalimar: return String::Concat("main", settings->PrimaryShalimarExtension());
            default:                     return "main.c";
            }
        }

        static String^ MainTemplate(LanguageKind lang, String^ fileName)
        {
            array<String^>^ lines;
            switch (lang)
            {
            case LanguageKind::Cpp11:
                lines = gcnew array<String^>{
                    String::Format("// {0} - created by GRIDE 2.0 (C++11)", fileName),
                    "#include <iostream>", "",
                    "int main()", "{",
                    "    std::cout << \"Hello from GRIDE\" << std::endl;",
                    "    return 0;", "}", "" };
                break;
            case LanguageKind::Shalimar:
                lines = gcnew array<String^>{
                    String::Format("// {0} - created by GRIDE 2.0 (Shalimar)", fileName),
                    "// Write your Shalimar program here.", "" };
                break;
            default:
                lines = gcnew array<String^>{
                    String::Format("/* {0} - created by GRIDE 2.0 (C90) */", fileName),
                    "#include <stdio.h>", "",
                    "int main(void)", "{",
                    "    printf(\"Hello from GRIDE\\n\");",
                    "    return 0;", "}", "" };
                break;
            }
            return String::Join("\r\n", lines);
        }

        void OnNewFile(Object^ sender, EventArgs^ e)
        {
            untitledCounter++;
            LanguageKind lang = project != nullptr ? project->LanguageValue : LanguageKind::C90;
            String^ ext = lang == LanguageKind::Cpp11 ? gcnew String(".cpp")
                        : (lang == LanguageKind::Shalimar ? settings->PrimaryShalimarExtension() : gcnew String(".c"));
            CodeEditor^ ed = CreateEditor();
            ed->UntitledName = String::Format("Untitled{0}{1}", untitledCounter, ext);
            ed->Mode = LanguageDefs::ModeForFile(ed->UntitledName, settings, project);
            AddEditorTab(ed);
        }

        void OnOpenProject(Object^ sender, EventArgs^ e)
        {
            OpenFileDialog^ d = gcnew OpenFileDialog();
            d->Filter = "GRIDE project (*.pro)|*.pro|CCS 7.4 / Eclipse project (.project)|.project;*.project|All files (*.*)|*.*";
            d->InitialDirectory = DefaultFolder();
            try
            {
                if (d->ShowDialog(this) == System::Windows::Forms::DialogResult::OK) OpenProject(d->FileName);
            }
            finally { delete d; }
        }

        void OnImportCcs(Object^ sender, EventArgs^ e)
        {
            FolderBrowserDialog^ d = gcnew FolderBrowserDialog();
            d->Description = "Select the CCS 7.4 project folder (the one that contains .project and .cproject)";
            try
            {
                if (d->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) return;
                String^ dotProject = Path::Combine(d->SelectedPath, ".project");
                if (!File::Exists(dotProject)) { Ui::Error(this, "This folder has no .project file."); return; }
                OpenProject(dotProject);
            }
            finally { delete d; }
        }

        void OnOpenFile(Object^ sender, EventArgs^ e)
        {
            OpenFileDialog^ d = gcnew OpenFileDialog();
            d->Filter = SourceFilter();
            d->Multiselect = true;
            d->InitialDirectory = DefaultFolder();
            try
            {
                if (d->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) return;
                for each (String^ f in d->FileNames)
                {
                    if (IsProjectFile(f)) OpenProject(f);
                    else OpenFile(f);
                }
            }
            finally { delete d; }
        }

        void OnCloseFile(Object^ sender, EventArgs^ e) { if (docs->SelectedTab != nullptr) CloseTab(docs->SelectedTab); }

        void OnCloseProject(Object^ sender, EventArgs^ e)
        {
            if (busy) return;
            if (CloseProjectInternal())
            {
                settings->LastProject = String::Empty;
                SaveSettings();
                SetStatus("Project closed");
            }
        }

        void OnSave(Object^ sender, EventArgs^ e) { SaveEditor(ActiveEditor(), false); }
        void OnSaveAs(Object^ sender, EventArgs^ e) { SaveEditor(ActiveEditor(), true); }
        void OnSaveAll(Object^ sender, EventArgs^ e) { if (SaveAll()) SetStatus("All files saved"); }
        void OnExit(Object^ sender, EventArgs^ e) { Close(); }

        void OnRecentFilesOpening(Object^ sender, EventArgs^ e) { FillRecent(miRecentFiles, settings->RecentFiles, GRIDE_EH(OnRecentFileClick)); }
        void OnRecentProjectsOpening(Object^ sender, EventArgs^ e) { FillRecent(miRecentProjects, settings->RecentProjects, GRIDE_EH(OnRecentProjectClick)); }

        void FillRecent(ToolStripMenuItem^ parent, List<String^>^ list, EventHandler^ handler)
        {
            parent->DropDownItems->Clear();
            int n = 0;
            for each (String^ p in list)
            {
                n++;
                ToolStripMenuItem^ it = gcnew ToolStripMenuItem(String::Format("&{0}  {1}", n % 10, p));
                it->Tag = p;
                it->Click += handler;
                it->Enabled = File::Exists(p);
                parent->DropDownItems->Add(it);
            }
            if (n == 0)
            {
                ToolStripItem^ none = parent->DropDownItems->Add("(none)");
                none->Enabled = false;
            }
        }

        void OnRecentFileClick(Object^ sender, EventArgs^ e)
        {
            String^ p = safe_cast<String^>(safe_cast<ToolStripItem^>(sender)->Tag);
            if (IsProjectFile(p)) OpenProject(p); else OpenFile(p);
        }

        void OnRecentProjectClick(Object^ sender, EventArgs^ e)
        {
            OpenProject(safe_cast<String^>(safe_cast<ToolStripItem^>(sender)->Tag));
        }

        // ============================================================ Edit menu
        void OnUndo(Object^ sender, EventArgs^ e) { CodeEditor^ ed = ActiveEditor(); if (ed != nullptr) ed->Undo(); }
        void OnRedo(Object^ sender, EventArgs^ e) { CodeEditor^ ed = ActiveEditor(); if (ed != nullptr) ed->Redo(); }

        void OnCut(Object^ sender, EventArgs^ e)
        {
            CodeEditor^ ed = ActiveEditor();
            if (ed != nullptr) ed->Cut();
        }

        void OnCopy(Object^ sender, EventArgs^ e)
        {
            if (txtOutput->ContainsFocus) { txtOutput->Copy(); return; }
            CodeEditor^ ed = ActiveEditor();
            if (ed != nullptr) ed->Copy();
        }

        void OnPaste(Object^ sender, EventArgs^ e)
        {
            CodeEditor^ ed = ActiveEditor();
            if (ed != nullptr) ed->Paste();
        }

        void OnSelectAll(Object^ sender, EventArgs^ e)
        {
            if (txtOutput->ContainsFocus) { txtOutput->SelectAll(); return; }
            CodeEditor^ ed = ActiveEditor();
            if (ed != nullptr) ed->SelectAll();
        }

        void OnFind(Object^ sender, EventArgs^ e)
        {
            CodeEditor^ ed = ActiveEditor();
            findDialog->ShowFor(this, false, ed != nullptr ? ed->SelectedText : nullptr);
        }

        void OnReplace(Object^ sender, EventArgs^ e)
        {
            CodeEditor^ ed = ActiveEditor();
            findDialog->ShowFor(this, true, ed != nullptr ? ed->SelectedText : nullptr);
        }

        void OnFindNext(Object^ sender, EventArgs^ e)
        {
            if (String::IsNullOrEmpty(findDialog->SearchText)) { OnFind(sender, e); return; }
            if (!findDialog->FindNext()) SetStatus(String::Format("'{0}' was not found", findDialog->SearchText));
        }

        void OnGoToLine(Object^ sender, EventArgs^ e)
        {
            CodeEditor^ ed = ActiveEditor();
            if (ed == nullptr) return;
            String^ s = InputDialog::Ask(this, "Go To Line",
                String::Format("Line number (1 - {0}):", ed->LineCount()), Convert::ToString(ed->CurrentLine));
            if (s == nullptr) return;
            int n;
            if (Int32::TryParse(s, n) && n >= 1) ed->GoToLine(Math::Min(n, ed->LineCount()));
            else Ui::Error(this, "Please enter a valid line number.");
        }

        // ============================================================ View menu
        void OnToggleExplorer(Object^ sender, EventArgs^ e)
        {
            miViewExplorer->Checked = !miViewExplorer->Checked;
            splitMain->Panel1Collapsed = !miViewExplorer->Checked;
        }

        void OnToggleProps(Object^ sender, EventArgs^ e)
        {
            miViewProps->Checked = !miViewProps->Checked;
            splitRight->Panel2Collapsed = !miViewProps->Checked;
        }

        void OnToggleBottom(Object^ sender, EventArgs^ e)
        {
            miViewBottom->Checked = !miViewBottom->Checked;
            splitCenter->Panel2Collapsed = !miViewBottom->Checked;
        }

        void ShowBottom(TabPage^ page)
        {
            if (splitCenter->Panel2Collapsed) { splitCenter->Panel2Collapsed = false; miViewBottom->Checked = true; }
            bottomTabs->SelectedTab = page;
        }

        void OnShowOutput(Object^ sender, EventArgs^ e) { ShowBottom(pageOutput); }
        void OnShowErrors(Object^ sender, EventArgs^ e) { ShowBottom(pageErrors); }

        // ============================================================ Project menu
        void OnAddNewFile(Object^ sender, EventArgs^ e)
        {
            if (project == nullptr || project->IsReadOnly || busy) return;
            SaveFileDialog^ d = gcnew SaveFileDialog();
            d->Title = "Add New File";
            d->Filter = SourceFilter();
            d->InitialDirectory = project->ProjectDir;
            d->FileName = String::Concat("newfile", project->LanguageValue == LanguageKind::Cpp11 ? gcnew String(".cpp")
                : (project->LanguageValue == LanguageKind::Shalimar ? settings->PrimaryShalimarExtension() : gcnew String(".c")));
            d->OverwritePrompt = false;
            try
            {
                if (d->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) return;
                String^ f = d->FileName;
                if (!File::Exists(f)) File::WriteAllText(f, NewFileHeader(f), gcnew UTF8Encoding(false));
                if (!project->AddFile(f)) SetStatus("The file is already in the project");
                SaveProject();
                RefreshTree();
                OpenFile(f);
            }
            catch (Exception^ ex) { ShowException("Could not add the file.", ex); }
            finally { delete d; }
        }

        static String^ NewFileHeader(String^ path)
        {
            String^ ext = Path::GetExtension(path)->ToLowerInvariant();
            String^ name = Path::GetFileName(path);
            if (String::Equals(ext, ".c") || String::Equals(ext, ".h"))
                return String::Format("/* {0} */\r\n\r\n", name);
            if (String::Equals(ext, ".asm") || String::Equals(ext, ".s") || String::Equals(ext, ".inc"))
                return String::Format("; {0}\r\n\r\n", name);
            if (String::Equals(ext, ".cmd"))
                return String::Format("/* {0} - linker command file */\r\n\r\n", name);
            return String::Format("// {0}\r\n\r\n", name);
        }

        void OnAddExistingFile(Object^ sender, EventArgs^ e)
        {
            if (project == nullptr || project->IsReadOnly || busy) return;
            OpenFileDialog^ d = gcnew OpenFileDialog();
            d->Title = "Add Existing Files";
            d->Filter = SourceFilter();
            d->Multiselect = true;
            d->InitialDirectory = project->ProjectDir;
            try
            {
                if (d->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) return;
                int added = 0;
                for each (String^ f in d->FileNames) if (project->AddFile(f)) added++;
                SaveProject();
                RefreshTree();
                SetStatus(String::Format("{0} file(s) added", added));
            }
            finally { delete d; }
        }

        void OnRemoveFile(Object^ sender, EventArgs^ e)
        {
            if (project == nullptr || project->IsReadOnly || busy) return;
            String^ f = SelectedFile();
            if (f == nullptr)
            {
                CodeEditor^ ed = ActiveEditor();
                if (ed != nullptr && ed->FilePath != nullptr && project->ContainsFile(ed->FilePath)) f = ed->FilePath;
            }
            if (f == nullptr) { Ui::Info(this, "Select a file in Solution Explorer first."); return; }
            if (!Ui::Confirm(this, String::Format("Remove '{0}' from the project?\n(The file stays on disk.)", Path::GetFileName(f)))) return;
            project->RemoveFile(f);
            SaveProject();
            RefreshTree();
        }

        void OnRenameFile(Object^ sender, EventArgs^ e)
        {
            if (project == nullptr || project->IsReadOnly || busy) return;
            String^ f = SelectedFile();
            if (f == nullptr) return;
            String^ name = InputDialog::Ask(this, "Rename File", "New file name:", Path::GetFileName(f));
            if (name == nullptr || String::Equals(name, Path::GetFileName(f))) return;
            if (!PathUtil::IsValidFileName(name)) { Ui::Error(this, "That is not a valid file name."); return; }
            String^ target = Path::Combine(Path::GetDirectoryName(f), name);
            bool caseOnly = String::Equals(target, f, StringComparison::OrdinalIgnoreCase);
            if (File::Exists(target) && !caseOnly) { Ui::Error(this, "A file with that name already exists."); return; }

            TabPage^ tab = FindTab(f);
            CodeEditor^ ed = tab != nullptr ? safe_cast<CodeEditor^>(tab->Tag) : nullptr;
            try
            {
                if (File::Exists(f))
                {
                    if (caseOnly)
                    {
                        String^ tmp = String::Concat(f, ".renametmp");
                        File::Move(f, tmp);
                        File::Move(tmp, target);
                    }
                    else File::Move(f, target);
                }
                project->RenameFile(f, target);
                SaveProject();
                if (ed != nullptr)
                {
                    ed->FilePath = Path::GetFullPath(target);
                    ed->Mode = LanguageDefs::ModeForFile(target, settings, project);
                    UpdateTabText(ed);
                }
                RefreshTree();
                UpdateUi();
            }
            catch (Exception^ ex) { ShowException("Could not rename the file.", ex); }
        }

        void OnProjectProperties(Object^ sender, EventArgs^ e)
        {
            if (project == nullptr) return;
            ProjectPropertiesDialog^ d = gcnew ProjectPropertiesDialog(project);
            try
            {
                if (d->ShowDialog(this) != System::Windows::Forms::DialogResult::OK || project->IsReadOnly) return;
                if (busy) { Ui::Info(this, "Properties cannot be changed while a build is running."); return; }
                d->View->ApplyTo(project);
                SaveProject();
                RefreshTree();
                RefreshCombos();
                RefreshEditorModes();
                UpdateUi();
            }
            finally { delete d; }
        }

        void RefreshEditorModes()
        {
            for each (TabPage^ p in docs->TabPages)
            {
                CodeEditor^ ed = dynamic_cast<CodeEditor^>(p->Tag);
                if (ed == nullptr) continue;
                String^ name = ed->FilePath != nullptr ? ed->FilePath : ed->UntitledName;
                EditorMode m = LanguageDefs::ModeForFile(name, settings, project);
                if (m != ed->Mode) ed->Mode = m;
            }
        }

        void OnConfigChanged(Object^ sender, EventArgs^ e)
        {
            if (updatingCombos || project == nullptr || project->IsReadOnly || cboConfig->SelectedItem == nullptr) return;
            project->ActiveConfiguration = safe_cast<String^>(cboConfig->SelectedItem);
            SaveProject();
            UpdatePropertyGrid();
            UpdateUi();
        }

        void OnTargetChanged(Object^ sender, EventArgs^ e)
        {
            if (updatingCombos || project == nullptr || project->IsReadOnly || cboTarget->SelectedItem == nullptr) return;
            project->Target = safe_cast<String^>(cboTarget->SelectedItem);
            SaveProject();
            RefreshTree();
            RefreshEditorModes();
            UpdateUi();
        }

        // ============================================================ Build / Run / Convert
        bool PrepareBuild()
        {
            if (busy) return false;
            if (project == nullptr) { Ui::Info(this, "Open or create a project first."); return false; }
            if (project->IsReadOnly)
            {
                Ui::Info(this, "Imported CCS 7.4 projects are read-only in GRIDE. Build them in Code Composer Studio.");
                return false;
            }
            if (settings->SaveBeforeBuild && !SaveAll()) return false;
            return true;
        }

        void ClearBuildOutput()
        {
            String^ s;
            while (outQueue->TryDequeue(s)) {}
            BuildMessage^ m;
            while (msgQueue->TryDequeue(m)) {}
            txtOutput->Clear();
            lvErrors->Items->Clear();
            errorCount = 0;
            warningCount = 0;
            UpdateErrorTabText();
        }

        void StartJob(BuildJob^ job, bool clearOutput)
        {
            if (busy || worker->IsBusy) return;
            if (clearOutput) ClearBuildOutput();
            ShowBottom(pageOutput);
            if (cts != nullptr) delete cts;               // Dispose the previous source
            cts = gcnew CancellationTokenSource();
            job->Token = cts->Token;                      // the worker never touches cts itself
            SetBusy(true);
            worker->RunWorkerAsync(job);
        }

        BuildJob^ MakeJob(JobKind kind)
        {
            BuildJob^ job = gcnew BuildJob();
            job->Kind = kind;
            // Work on copies so the UI can never change them under the worker thread.
            job->Project = JsonClone<GrideProject>(project);
            job->Project->FilePath = project->FilePath;
            job->Project->Normalize();
            job->Settings = JsonClone<IdeSettings>(settings);
            job->Settings->Normalize();
            return job;
        }

        void StartBuild(JobKind kind)
        {
            if (!PrepareBuild()) return;
            String^ label = "Build started...";
            if (kind == JobKind::Rebuild) label = "Rebuild started...";
            else if (kind == JobKind::Clean) label = "Clean started...";
            else if (kind == JobKind::Run) label = "Running...";
            SetStatus(label);
            StartJob(MakeJob(kind), kind != JobKind::Run);
        }

        void OnBuild(Object^ sender, EventArgs^ e) { StartBuild(JobKind::Build); }

        void OnCompileFile(Object^ sender, EventArgs^ e)
        {
            CodeEditor^ ed = ActiveEditor();
            if (ed == nullptr || ed->FilePath == nullptr || project == nullptr || !project->ContainsFile(ed->FilePath))
            {
                Ui::Info(this, "Open a source file of the project first.");
                return;
            }
            if (!PrepareBuild()) return;
            if (ed->IsDirty && !SaveEditor(ed, false)) return;
            BuildJob^ job = MakeJob(JobKind::CompileFile);
            job->SingleFile = ed->FilePath;
            SetStatus(String::Format("Compiling {0}...", ed->DisplayName));
            StartJob(job, true);
        }
        void OnRebuild(Object^ sender, EventArgs^ e) { StartBuild(JobKind::Rebuild); }
        void OnClean(Object^ sender, EventArgs^ e) { StartBuild(JobKind::Clean); }
        void OnBuildAndRun(Object^ sender, EventArgs^ e) { StartBuild(JobKind::BuildAndRun); }
        void OnRun(Object^ sender, EventArgs^ e) { StartBuild(JobKind::Run); }

        void OnStop(Object^ sender, EventArgs^ e)
        {
            if (busy && cts != nullptr)
            {
                cts->Cancel();
                SetStatus("Stopping...");
            }
        }

        void StartConversion(bool toShalimar)
        {
            if (busy) return;
            CodeEditor^ ed = ActiveEditor();
            if (ed == nullptr || ed->FilePath == nullptr) { Ui::Info(this, "Open a saved source file first."); return; }
            if (ed->IsDirty && !ed->IsReadOnly && !SaveEditor(ed, false)) return;
            String^ toolId = toShalimar ? ToolIds::ConvertCToShalimar : ToolIds::ConvertShalimarToC;
            ToolDefinition^ tool = settings->GetTool(toolId);
            if (tool == nullptr || tool->ResolvedPath() == nullptr)
            {
                Ui::Info(this, "The converter is not configured. Set its path in Tools > Options > Toolchains.");
                return;
            }
            String^ outPath = Path::ChangeExtension(ed->FilePath,
                toShalimar ? settings->PrimaryShalimarExtension() : gcnew String(".c"));
            if (File::Exists(outPath) &&
                !Ui::Confirm(this, String::Format("{0} already exists. Overwrite it?", Path::GetFileName(outPath)))) return;
            TabPage^ open = FindTab(outPath);
            if (open != nullptr && !CloseTab(open)) return;

            BuildJob^ job = gcnew BuildJob();
            job->Kind = JobKind::Convert;
            job->Tool = JsonClone<ToolDefinition>(tool);
            job->InputFile = ed->FilePath;
            job->OutputFile = outPath;
            job->WorkDir = Path::GetDirectoryName(ed->FilePath);
            SetStatus("Converting...");
            StartJob(job, true);
        }

        void OnConvertCToShalimar(Object^ sender, EventArgs^ e) { StartConversion(true); }
        void OnConvertShalimarToC(Object^ sender, EventArgs^ e) { StartConversion(false); }

        // ---- worker thread (no UI access in here)
        void EnqueueOutput(String^ s) { outQueue->Enqueue(s); }
        void EnqueueMessage(BuildMessage^ m) { msgQueue->Enqueue(m); }

        void OnWorkerDoWork(Object^ sender, DoWorkEventArgs^ e)
        {
            BuildJob^ job = safe_cast<BuildJob^>(e->Argument);
            Action<String^>^ out = gcnew Action<String^>(this, &MainForm::EnqueueOutput);
            Action<BuildMessage^>^ msg = gcnew Action<BuildMessage^>(this, &MainForm::EnqueueMessage);
            System::Threading::CancellationToken token = job->Token;
            try
            {
                switch (job->Kind)
                {
                case JobKind::Convert:
                    job->ExitCode = BuildEngine::RunConversion(job->Tool, job->InputFile, job->OutputFile, job->WorkDir, out, token);
                    break;
                case JobKind::Run:
                    job->ExitCode = BuildEngine::RunOutput(job->Project, job->Settings, out, token);
                    break;
                default:
                {
                    BuildEngine^ engine = gcnew BuildEngine(job->Project, job->Settings, out, msg, token);
                    engine->SingleFile = job->SingleFile;
                    BuildAction action = BuildAction::Build;
                    if (job->Kind == JobKind::Rebuild) action = BuildAction::Rebuild;
                    else if (job->Kind == JobKind::Clean) action = BuildAction::Clean;
                    else if (job->Kind == JobKind::CompileFile) action = BuildAction::CompileFile;
                    job->Result = engine->Execute(action);
                    if (job->Kind == JobKind::BuildAndRun && job->Result->Success && !token.IsCancellationRequested)
                        job->ExitCode = BuildEngine::RunOutput(job->Project, job->Settings, out, token);
                    break;
                }
                }
            }
            catch (Exception^ ex)
            {
                out->Invoke(String::Format("Internal error: {0}", ex->ToString()));
            }
            e->Result = job;
        }

        void OnWorkerCompleted(Object^ sender, RunWorkerCompletedEventArgs^ e)
        {
            FlushQueues();
            SetBusy(false);
            if (closing) return;            // window is shutting down: no dialogs, no new tabs
            if (e->Error != nullptr)
            {
                SetStatus("Failed");
                txtOutput->AppendText(String::Format("Internal error: {0}\r\n", e->Error->Message));
                return;
            }
            BuildJob^ job = dynamic_cast<BuildJob^>(e->Result);
            if (job == nullptr) return;

            if (job->Result != nullptr)
            {
                if (job->Result->Cancelled) SetStatus("Build cancelled");
                else if (job->Result->Success) SetStatus(String::Format("Build succeeded  ({0} warning(s))", job->Result->Warnings));
                else SetStatus(String::Format("Build failed  ({0} error(s))", job->Result->Errors));
                if (errorCount > 0) ShowBottom(pageErrors);
            }
            else if (job->Kind == JobKind::Run) SetStatus(job->ExitCode == 0 ? gcnew String("Run finished") : String::Format("Run ended with code {0}", job->ExitCode));

            if (job->Kind == JobKind::Convert)
            {
                if (job->ExitCode == 0 && File::Exists(job->OutputFile))
                {
                    SetStatus("Conversion succeeded");
                    OpenFile(job->OutputFile);
                    if (project != nullptr && !project->IsReadOnly && !project->ContainsFile(job->OutputFile) &&
                        project->ContainsFile(job->InputFile) &&
                        Ui::Confirm(this, String::Format("Add {0} to the project?", Path::GetFileName(job->OutputFile))))
                    {
                        project->AddFile(job->OutputFile);
                        SaveProject();
                        RefreshTree();
                    }
                }
                else SetStatus("Conversion failed");
            }
            // files on disk may have changed (outputs, missing markers)
            if (project != nullptr) RefreshTree();
        }

        // ---- output / error list (UI thread, timer driven)
        void OnFlushTick(Object^ sender, EventArgs^ e) { FlushQueues(); }

        void FlushQueues()
        {
            StringBuilder^ sb = gcnew StringBuilder();
            String^ line;
            int lines = 0;
            while (lines < 5000 && outQueue->TryDequeue(line)) { sb->Append(line)->Append("\r\n"); lines++; }
            if (sb->Length > 0) txtOutput->AppendText(sb->ToString());

            BuildMessage^ m;
            bool any = false;
            lvErrors->BeginUpdate();
            try
            {
                while (msgQueue->TryDequeue(m)) { AddErrorItem(m); any = true; }
            }
            finally { lvErrors->EndUpdate(); }
            if (any) UpdateErrorTabText();
        }

        void AddErrorItem(BuildMessage^ m)
        {
            String^ type = "Message";
            Color color = Color::DimGray;
            if (String::Equals(m->Severity, "error")) { type = "Error"; color = Color::Firebrick; errorCount++; }
            else if (String::Equals(m->Severity, "warning")) { type = "Warning"; color = Color::DarkOrange; warningCount++; }
            String^ file = String::IsNullOrEmpty(m->FilePath) ? String::Empty : Path::GetFileName(m->FilePath);
            String^ line = m->Line > 0 ? Convert::ToString(m->Line) : String::Empty;
            ListViewItem^ it = gcnew ListViewItem(gcnew array<String^>{
                type, m->Code, m->Text, file, line, m->Tool == nullptr ? String::Empty : m->Tool });
            it->ForeColor = color;
            it->Tag = m;
            it->ToolTipText = m->FilePath;
            lvErrors->Items->Add(it);
        }

        void UpdateErrorTabText()
        {
            pageErrors->Text = String::Format("Error List ({0} errors, {1} warnings)", errorCount, warningCount);
        }

        void GoToMessage(BuildMessage^ m)
        {
            if (m == nullptr || String::IsNullOrEmpty(m->FilePath)) return;
            String^ f = m->FilePath;
            if (!File::Exists(f) && project != nullptr)
            {
                // tools sometimes print paths relative to another folder - try by file name in the project
                for each (String^ rel in project->SourceFiles)
                {
                    String^ full = project->ResolvePath(rel);
                    if (String::Equals(Path::GetFileName(full), Path::GetFileName(f), StringComparison::OrdinalIgnoreCase)) { f = full; break; }
                }
            }
            if (!File::Exists(f)) { SetStatus(String::Format("File not found: {0}", m->FilePath)); return; }
            CodeEditor^ ed = OpenFile(f);
            if (ed == nullptr || m->Line <= 0) return;
            if (m->Column > 0) ed->GoToLineColumn(m->Line, m->Column);
            else ed->SelectLine(m->Line);
        }

        void OnErrorActivate(Object^ sender, EventArgs^ e)
        {
            if (lvErrors->SelectedItems->Count == 0) return;
            GoToMessage(dynamic_cast<BuildMessage^>(lvErrors->SelectedItems[0]->Tag));
        }

        void OnErrorKeyDown(Object^ sender, KeyEventArgs^ e)
        {
            if (e->KeyCode == Keys::Enter) { OnErrorActivate(sender, e); e->Handled = true; }
        }

        void OnOutputDoubleClick(Object^ sender, EventArgs^ e)
        {
            int lineIndex = txtOutput->GetLineFromCharIndex(txtOutput->SelectionStart);
            array<String^>^ lines = txtOutput->Lines;
            if (lineIndex < 0 || lineIndex >= lines->Length) return;
            String^ baseDir = project != nullptr ? project->ProjectDir : nullptr;
            String^ text = lines[lineIndex];
            BuildMessage^ m = parser->Parse(text, (Regex^)nullptr, baseDir);
            if (m == nullptr)
            {
                // Find in Files result:  C:\path\file.c(12,5): text
                Match^ fm = Regex::Match(text, R"RX(^(?<file>(?:[A-Za-z]:)?[^(]+)\((?<line>\d+),(?<col>\d+)\):\s)RX");
                if (fm->Success)
                {
                    m = gcnew BuildMessage();
                    m->FilePath = fm->Groups["file"]->Value;
                    m->Line = Int32::Parse(fm->Groups["line"]->Value);
                    m->Column = Int32::Parse(fm->Groups["col"]->Value);
                }
            }
            if (m != nullptr) GoToMessage(m);
        }

        // ============================================================ extra editor commands
        void OnEditMenuOpening(Object^ sender, EventArgs^ e)
        {
            CodeEditor^ ed = ActiveEditor();
            miUndo->Enabled = ed != nullptr && ed->CanUndo;
            miRedo->Enabled = ed != nullptr && ed->CanRedo;
        }

        void SwitchDocument(int step)
        {
            int n = docs->TabCount;
            if (n < 2) return;
            docs->SelectedIndex = ((docs->SelectedIndex + step) % n + n) % n;
        }

        void OnNextDocument(Object^ sender, EventArgs^ e) { SwitchDocument(1); }
        void OnPrevDocument(Object^ sender, EventArgs^ e) { SwitchDocument(-1); }

        // Find in Files: searches every project file (open editors use their unsaved text).
        // Results go to the Output window as  file(line,col): text  - double-click to jump.
        void OnFindInFiles(Object^ sender, EventArgs^ e)
        {
            if (project == nullptr) { Ui::Info(this, "Open a project first."); return; }
            if (busy) { Ui::Info(this, "Please wait for the current build to finish."); return; }
            CodeEditor^ cur = ActiveEditor();
            String^ initial = cur != nullptr ? cur->SelectedText : String::Empty;
            if (initial != nullptr && initial->IndexOf(L'\n') >= 0) initial = String::Empty;
            String^ what = InputDialog::Ask(this, "Find in Files", "Find in all project files (case-insensitive):", initial);
            if (String::IsNullOrEmpty(what)) return;

            ClearBuildOutput();
            ShowBottom(pageOutput);
            StringBuilder^ sb = gcnew StringBuilder();
            sb->AppendFormat("Find all \"{0}\" in project {1}\r\n", what, project->Name);
            int hits = 0, filesHit = 0;
            for each (String^ rel in project->SourceFiles)
            {
                String^ full = project->ResolvePath(rel);
                String^ text = nullptr;
                TabPage^ t = FindTab(full);
                if (t != nullptr) text = safe_cast<CodeEditor^>(t->Tag)->GetAllText();
                else if (File::Exists(full))
                {
                    try
                    {
                        Encoding^ enc = nullptr; bool crlfDummy = true;
                        text = TextFile::Read(full, enc, crlfDummy);
                    }
                    catch (Exception^) { text = nullptr; }
                }
                if (text == nullptr) continue;
                array<String^>^ lines = text->Replace("\r\n", "\n")->Split(L'\n');
                bool any = false;
                for (int i = 0; i < lines->Length; i++)
                {
                    int col = lines[i]->IndexOf(what, StringComparison::OrdinalIgnoreCase);
                    if (col < 0) continue;
                    sb->AppendFormat("{0}({1},{2}): {3}\r\n", full, i + 1, col + 1, lines[i]->Trim());
                    hits++;
                    any = true;
                }
                if (any) filesHit++;
            }
            sb->AppendFormat("Matching lines: {0}    Matching files: {1}\r\n", hits, filesHit);
            txtOutput->AppendText(sb->ToString());
            SetStatus(String::Format("Find in Files: {0} match(es)", hits));
        }

    protected:
        // VS-like checks when the window is activated: files changed or deleted outside GRIDE.
        virtual void OnActivated(EventArgs^ e) override
        {
            Form::OnActivated(e);
            if (checkingDisk) return;
            checkingDisk = true;
            try
            {
                for each (TabPage^ p in docs->TabPages)
                {
                    CodeEditor^ ed = dynamic_cast<CodeEditor^>(p->Tag);
                    if (ed == nullptr || ed->FilePath == nullptr || !ed->ChangedOnDisk()) continue;
                    bool reload = !ed->IsDirty;
                    if (ed->IsDirty)
                    {
                        docs->SelectedTab = p;
                        reload = Ui::Confirm(this, String::Format(
                            "{0}\n\nThis file was changed outside GRIDE and also has unsaved changes here.\n"
                            "Reload it and lose your changes?", ed->FilePath));
                    }
                    if (reload)
                    {
                        try { ed->ReloadFromDisk(); }
                        catch (Exception^ ex) { ShowException("Could not reload the file.", ex); }
                    }
                    else ed->RememberDiskTime();      // don't ask again for this change
                }
                if (project != nullptr && !project->IsReadOnly && File::Exists(project->FilePath) &&
                    File::GetLastWriteTimeUtc(project->FilePath) > projectDiskTime && projectDiskTime != DateTime::MinValue)
                {
                    projectDiskTime = File::GetLastWriteTimeUtc(project->FilePath);
                    if (Ui::Confirm(this, "The project file was changed outside GRIDE. Reload it?")) ReloadProjectFromDisk();
                }
                UpdateUi();
            }
            finally { checkingDisk = false; }
        }

        virtual bool ProcessCmdKey(Message% msg, Keys keyData) override
        {
            if (keyData == (Keys::Control | Keys::Tab)) { SwitchDocument(1); return true; }
            if (keyData == (Keys::Control | Keys::Shift | Keys::Tab)) { SwitchDocument(-1); return true; }
            return Form::ProcessCmdKey(msg, keyData);
        }

    private:

        // ============================================================ Tools / Help
        void OnOptions(Object^ sender, EventArgs^ e) { ShowOptions(false); }
        void OnToolchainOptions(Object^ sender, EventArgs^ e) { ShowOptions(true); }

        void ShowOptions(bool toolsPage)
        {
            OptionsDialog^ d = gcnew OptionsDialog(settings);
            try
            {
                if (toolsPage) d->SelectToolsPage();
                if (d->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) return;
                IdeSettings^ s = d->Result;
                // keep values the dialog does not own (layout, recents)
                s->RecentFiles = settings->RecentFiles;
                s->RecentProjects = settings->RecentProjects;
                s->LastProject = settings->LastProject;
                settings = s;
                SaveSettings();
                for each (TabPage^ p in docs->TabPages)
                {
                    CodeEditor^ ed = dynamic_cast<CodeEditor^>(p->Tag);
                    if (ed != nullptr) ed->ApplySettings(settings);
                }
                RefreshEditorModes();
                RefreshTree();
                UpdateUi();
            }
            finally { delete d; }
        }

        void OnPlaceholderHelp(Object^ sender, EventArgs^ e)
        {
            Ui::Info(this,
                "Placeholders usable in tool arguments:\n\n"
                "{in}          input file\n"
                "{out}         output file\n"
                "{objs}        all object files (linker)\n"
                "{libs}        libraries, each with the tool's library switch\n"
                "{cmdfiles}    TMS6747 linker command files (*.cmd) in the project\n"
                "{runtime}     RTS6747 runtime library path\n"
                "{includes}    include folders, each with the include switch\n"
                "{defines}     defines of the active configuration\n"
                "{flags}       compiler flags of the active configuration\n"
                "{linkflags}   linker flags of the active configuration\n"
                "{target}      windows | linux | macos | tms6747\n"
                "{config}      Debug | Release | ...\n"
                "{name}        project name\n"
                "{projectdir} {outdir} {objdir} {exe} {map}");
        }

        void OnShortcutHelp(Object^ sender, EventArgs^ e)
        {
            Ui::Info(this,
                "Ctrl+Shift+N  New project          Ctrl+N  New file\n"
                "Ctrl+Shift+O  Open project         Ctrl+O  Open file\n"
                "Ctrl+S  Save    Ctrl+Shift+S  Save all    Ctrl+F4  Close file\n"
                "Ctrl+Z / Ctrl+Y  Undo / Redo\n"
                "Ctrl+F  Find    Ctrl+H  Replace    F3  Find next    Ctrl+G  Go to line\n"
                "Tab / Shift+Tab  Indent / outdent selected lines\n"
                "Ctrl+Shift+B  Build    F5  Build and run    Ctrl+F5  Run    Shift+F5  Stop\n"
                "Alt+F7  Project properties    F4  Properties window\n"
                "Middle-click a tab to close it. Double-click an error to jump to it.");
        }

        void OnAbout(Object^ sender, EventArgs^ e)
        {
            Ui::Info(this, String::Format(
                "{0}  (version {1})\n\n"
                "IDE for the C90, C++11 and Shalimar compilers.\n"
                "Targets: Windows (MASM/LINK), Linux, macOS, TMS6747 DSP (ASM6x/LNK6x/RTS6747/VM6747).\n"
                "Reads Code Composer Studio 7.4 projects (read-only).\n\n"
                "C++/CLI, .NET Framework 4.8, Windows Forms.", AppInfo::Name, AppInfo::Version));
        }

        // ============================================================ drag & drop, closing
        void OnDragEnter(Object^ sender, DragEventArgs^ e)
        {
            e->Effect = e->Data->GetDataPresent(DataFormats::FileDrop) ? DragDropEffects::Copy : DragDropEffects::None;
        }

        void OnDragDrop(Object^ sender, DragEventArgs^ e)
        {
            array<String^>^ files = dynamic_cast<array<String^>^>(e->Data->GetData(DataFormats::FileDrop));
            if (files == nullptr) return;
            for each (String^ f in files)
            {
                if (Directory::Exists(f) && File::Exists(Path::Combine(f, ".project"))) OpenProject(Path::Combine(f, ".project"));
                else if (!File::Exists(f)) continue;
                else if (IsProjectFile(f)) OpenProject(f);
                else OpenFile(f);
            }
        }

        void OnMainFormClosing(Object^ sender, FormClosingEventArgs^ e)
        {
            if (busy)
            {
                if (!Ui::Confirm(this, "A build is running. Stop it and exit?")) { e->Cancel = true; return; }
                if (cts != nullptr) cts->Cancel();
                closing = true;
                Enabled = false;                // no menu clicks while we wait
                // give the tools a moment to be killed before the window goes away
                Diagnostics::Stopwatch^ sw = Diagnostics::Stopwatch::StartNew();
                while (worker->IsBusy && sw->ElapsedMilliseconds < 8000)
                {
                    Application::DoEvents();
                    System::Threading::Thread::Sleep(50);
                }
                Enabled = true;
            }
            if (!CloseAllDocuments(nullptr)) { e->Cancel = true; closing = false; return; }
            SaveLayout();
            if (project != nullptr) settings->LastProject = project->FilePath;
            SaveSettings();
            flushTimer->Stop();
        }
    };
}

#undef GRIDE_EH
