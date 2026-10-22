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
#include "../Build/ProgramRunner.h"
#include "DebugPanes.h"
#include "HelpFiles.h"

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

    public enum class JobKind { Build, Rebuild, Clean, Run, BuildAndRun, Convert, CompileFile, BuildAndDebug };

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
        property RunHow How;                              // BuildAndRun: what runs once the build succeeded
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
            breakpoints = gcnew Dictionary<String^, SortedSet<int>^>(StringComparer::OrdinalIgnoreCase);
            debugger = nullptr;
            stoppedIn = nullptr;
            parser = gcnew ErrorParser();
            runner = gcnew ProgramRunner(gcnew Action<String^>(this, &MainForm::EnqueueOutput), gcnew Action<int, bool>(this, &MainForm::OnProgramExited));
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
            ReportToolchainProblems();

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
        ProgramRunner^ runner;              // the program running in the Output pane (RIDE's window)
        CancellationTokenSource^ verifyCts; // a Verify running on its own thread
        bool verifying;

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
        int closeHot;                   // the tab whose close cross the mouse is over, -1 none
        PropertyGrid^ props;
        TabControl^ bottomTabs;
        TabPage^ pageOutput;
        TabPage^ pageErrors;
        TextBox^ txtOutput;
        TextBox^ txtInput;                  // the running program's input line, under the Output pane
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
        ToolStripMenuItem^ miCcsMapping;
        ToolStripMenuItem^ miCompilerOptions;
        // ---- the debugger (R10): the session, the breakpoints shared with the editors and the engine, the panes
        DebugEngine^ debugger;
        Dictionary<String^, SortedSet<int>^>^ breakpoints;
        CodeEditor^ stoppedIn;                 // the editor showing the stop's arrow
        LocalsPane^ localsPane;
        WatchPane^ watchPane;
        CallStackPane^ callStackPane;
        RegistersPane^ registersPane;
        MemoryPane^ memoryPane;
        BreakpointsPane^ breakpointsPane;
        TabPage^ pageLocals;
        TabPage^ pageWatch;
        TabPage^ pageCallStack;
        TabPage^ pageRegisters;
        TabPage^ pageMemory;
        TabPage^ pageBreakpoints;
        ToolStripMenuItem^ miDebugStart;
        ToolStripMenuItem^ miToggleBreakpoint;
        ToolStripMenuItem^ miStepOver;
        ToolStripMenuItem^ miStepInto;
        ToolStripMenuItem^ miStepOut;
        ToolStripMenuItem^ miRunToCursor;
        ToolStripMenuItem^ miUpStack;
        ToolStripMenuItem^ miDownStack;
        ToolStripMenuItem^ miWatchExpression;
        ToolStripMenuItem^ miSetVariable;
        ToolStripMenuItem^ miStopDebugging;
        ToolStripMenuItem^ miDeleteBreakpoints;
        // which C++ project of a solution to open (the .sln, its projects' names) -> the name, or nullptr; nullptr: the dialog
        Func<String^, List<String^>^, String^>^ projectChooser;
        ToolStripMenuItem^ miImportCcs;
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
        ToolStripMenuItem^ miEmulate;
        ToolStripMenuItem^ miVerify;
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
            // each tab draws its own close cross on the right, a click on it closing that file (the user, 2026-10-21)
            closeHot = -1;
            docs->DrawMode = TabDrawMode::OwnerDrawFixed;
            docs->Padding = System::Drawing::Point(20, 4);
            docs->DrawItem += gcnew DrawItemEventHandler(this, &MainForm::OnDocsDrawItem);
            docs->MouseDown += gcnew MouseEventHandler(this, &MainForm::OnDocsMouseDown);
            docs->MouseMove += gcnew MouseEventHandler(this, &MainForm::OnDocsMouseMove);
            docs->MouseLeave += GRIDE_EH(OnDocsMouseLeave);
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
            // the running program's input line (RIDE's window): Enter hands the line over, Ctrl+Z ends the input
            txtInput = gcnew TextBox();
            txtInput->Dock = DockStyle::Fill;
            txtInput->Font = txtOutput->Font;
            txtInput->Enabled = false;
            txtInput->KeyDown += gcnew KeyEventHandler(this, &MainForm::OnInputKeyDown);
            Label^ inputLabel = gcnew Label();
            inputLabel->Text = "Program input:";
            inputLabel->AutoSize = false;
            inputLabel->Width = 100;
            inputLabel->Dock = DockStyle::Left;
            inputLabel->TextAlign = ContentAlignment::MiddleLeft;
            Panel^ inputPanel = gcnew Panel();
            inputPanel->Height = txtInput->PreferredHeight;
            inputPanel->Dock = DockStyle::Bottom;
            inputPanel->Controls->Add(txtInput);
            inputPanel->Controls->Add(inputLabel);
            pageOutput = gcnew TabPage("Output");
            pageOutput->Controls->Add(txtOutput);
            pageOutput->Controls->Add(inputPanel);
            pageErrors = gcnew TabPage("Error List");
            pageErrors->Controls->Add(lvErrors);
            bottomTabs->TabPages->Add(pageOutput);
            bottomTabs->TabPages->Add(pageErrors);
            // the debugger's panes (R10: RIDE's DebugView in GRIDEd's panes)
            localsPane = gcnew LocalsPane();
            localsPane->KeysLine = KeysLine();
            localsPane->SetRequested += gcnew VariableSetHandler(this, &MainForm::OnVariableSet);
            watchPane = gcnew WatchPane();
            callStackPane = gcnew CallStackPane();
            callStackPane->FrameChosen += gcnew IndexChosenHandler(this, &MainForm::LookAtFrame);
            registersPane = gcnew RegistersPane();
            memoryPane = gcnew MemoryPane();
            breakpointsPane = gcnew BreakpointsPane();
            breakpointsPane->Navigate += gcnew NavigateHandler(this, &MainForm::OnBreakpointNavigate);
            breakpointsPane->RemoveRequested += gcnew NavigateHandler(this, &MainForm::OnBreakpointRemove);
            pageLocals = AddPanePage("Locals", localsPane);
            pageWatch = AddPanePage("Watch", watchPane);
            pageCallStack = AddPanePage("Call Stack", callStackPane);
            pageRegisters = AddPanePage("Registers", registersPane);
            pageMemory = AddPanePage("Memory", memoryPane);
            pageBreakpoints = AddPanePage("Breakpoints", breakpointsPane);
            watchPane->UpdateFrom(nullptr);

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
            miImportCcs = AddItem(file, "&Import CCS Project (7.4 / 5.5, read-only)...", GRIDE_EH(OnImportCcs), Keys::None, nullptr);
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
            miCompilerOptions = AddItem(proj, "&Compiler Options...", GRIDE_EH(OnCompilerOptions), Keys::None, nullptr);
            miCcsMapping = AddItem(proj, "CCS / VS &Mapping...", GRIDE_EH(OnCcsMapping), Keys::None, nullptr);

            // ---- Build
            miBuild = AddItem(build, "&Build Project", GRIDE_EH(OnBuild), Keys::Control | Keys::Shift | Keys::B, nullptr);
            miCompileFile = AddItem(build, "Com&pile Current File", GRIDE_EH(OnCompileFile), Keys::Control | Keys::F7, nullptr);
            miRebuild = AddItem(build, "&Rebuild Project", GRIDE_EH(OnRebuild), Keys::None, nullptr);
            miClean = AddItem(build, "&Clean Project", GRIDE_EH(OnClean), Keys::None, nullptr);
            AddSep(build);
            miBuildStop = AddItem(build, "Cancel Build", GRIDE_EH(OnStop), Keys::Control | Keys::Cancel, "Ctrl+Break");

            // ---- Debug: the program runs in the Output pane (RIDE's window); on the C6747 the simulator,
            // or the emulator on the assembly; Verify runs both and compares them
            // the debugger (R10), on RIDE's keys: F8 starts and carries on, F7 steps over, F6 steps into, F9 sets a
            // breakpoint, Ctrl+Up / Ctrl+Down walk the stack
            miDebugStart = AddItem(debug, "Start / &Continue Debugging", GRIDE_EH(OnDebugStart), Keys::F8, nullptr);
            miToggleBreakpoint = AddItem(debug, "Toggle &Breakpoint", GRIDE_EH(OnToggleBreakpoint), Keys::F9, nullptr);
            miStepOver = AddItem(debug, "Step &Over", GRIDE_EH(OnStepOver), Keys::F7, nullptr);
            miStepInto = AddItem(debug, "Step &Into", GRIDE_EH(OnStepInto), Keys::F6, nullptr);
            miStepOut = AddItem(debug, "Step Ou&t", GRIDE_EH(OnStepOut), Keys::None, nullptr);
            miRunToCursor = AddItem(debug, "Run to &Cursor", GRIDE_EH(OnRunToCursor), Keys::None, nullptr);
            miUpStack = AddItem(debug, "&Up the Stack", GRIDE_EH(OnUpStack), Keys::Control | Keys::Up, nullptr);
            miDownStack = AddItem(debug, "&Down the Stack", GRIDE_EH(OnDownStack), Keys::Control | Keys::Down, nullptr);
            miWatchExpression = AddItem(debug, "&Watch Expression...", GRIDE_EH(OnWatchExpression), Keys::None, nullptr);
            miSetVariable = AddItem(debug, "Set &Variable...", GRIDE_EH(OnSetVariable), Keys::None, nullptr);
            miStopDebugging = AddItem(debug, "Stop Debu&gging", GRIDE_EH(OnStopDebugging), Keys::None, nullptr);
            miDeleteBreakpoints = AddItem(debug, "Delete All Breakpoi&nts", GRIDE_EH(OnDeleteBreakpoints), Keys::None, nullptr);
            AddSep(debug);
            miBuildRun = AddItem(debug, "Build and &Run", GRIDE_EH(OnBuildAndRun), Keys::F5, nullptr);
            miRun = AddItem(debug, "Run &Without Building", GRIDE_EH(OnRun), Keys::Control | Keys::F5, nullptr);
            String^ emulatorName = ProgramLauncher::EmulatorNameOf(nullptr);
            TargetEntry^ emulated = ProgramLauncher::EmulatedEntry();
            miEmulate = AddItem(debug, String::Concat("&Emulate on ", emulatorName), GRIDE_EH(OnEmulate), Keys::None, nullptr);
            miVerify = AddItem(debug, String::Format("&Verify ({0} against {1})", emulatorName, ProgramLauncher::ToolNameOf(emulated, "simulator")), GRIDE_EH(OnVerify), Keys::None, nullptr);
            miStop = AddItem(debug, "&Stop", GRIDE_EH(OnStop), Keys::Shift | Keys::F5, nullptr);

            // ---- Tools
            miConvC2S = AddItem(tools, "Convert C -> &Shalimar", GRIDE_EH(OnConvertCToShalimar), Keys::None, nullptr);
            miConvS2C = AddItem(tools, "Convert Shalimar -> &C", GRIDE_EH(OnConvertShalimarToC), Keys::None, nullptr);
            AddSep(tools);
            AddItem(tools, "&Options...", GRIDE_EH(OnOptions), Keys::None, nullptr);
            AddItem(tools, "&Toolchain Settings...", GRIDE_EH(OnToolchainOptions), Keys::None, nullptr);
            AddItem(tools, "Check Tool&chain", GRIDE_EH(OnCheckToolchain), Keys::None, nullptr);

            // ---- Help (R13: GRIDEd's items - this page, RIDE's manual at a chapter, the Shalimar language)
            ToolStripMenuItem^ languageRef = gcnew ToolStripMenuItem(HelpFiles::LanguageMenu);
            ToolStripMenuItem^ toolchainRef = gcnew ToolStripMenuItem(HelpFiles::ToolchainMenu);
            for each (HelpItem^ h in HelpFiles::Items())
            {
                ToolStripMenuItem^ under = h->Menu == HelpFiles::LanguageMenu ? languageRef : h->Menu == HelpFiles::ToolchainMenu ? toolchainRef : help;
                ToolStripMenuItem^ mi = AddItem(under, h->Text, GRIDE_EH(OnHelpPage), h->File == HelpFiles::ViewHelpFile ? Keys::F1 : Keys::None, nullptr);
                mi->Tag = h->File;
            }
            help->DropDownItems->Add(languageRef);
            help->DropDownItems->Add(toolchainRef);
            AddSep(help);
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
            bool canBuild = hasProject && (writable || project->IsImported) && !busy;     // a CCS or Visual Studio project builds, read-only

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
            // one program at a time; Stop ends a build or the running program (RIDE's Stop)
            bool programBusy = ProgramBusy();
            bool canRun = canBuild && !programBusy;
            bool emulatedTarget = hasProject && TargetInfo::Get(project->TargetValue)->Emulated;
            miBuildRun->Enabled = canRun;
            miRun->Enabled = canRun;
            miEmulate->Enabled = canRun && emulatedTarget;
            miVerify->Enabled = canRun && emulatedTarget;
            miStop->Enabled = busy || programBusy;
            miBuildStop->Enabled = busy || programBusy;
            tbBuild->Enabled = canBuild;
            tbRun->Enabled = canRun;
            tbStop->Enabled = busy || programBusy;
            cboConfig->Enabled = hasProject && (writable || project->IsImported) && !busy;
            miCcsMapping->Enabled = hasProject && project->IsImported;
            // the debugger's commands by the session's state (a disabled item leaves its key to the editor)
            bool debugging = debugger != nullptr && debugger->IsActive;
            bool stopped = debugging && debugger->State == DebugState::Break;
            miDebugStart->Enabled = stopped || (canRun && !debugging);
            miDebugStart->Text = stopped ? "&Continue" : "Start / &Continue Debugging";
            miToggleBreakpoint->Enabled = ed != nullptr && ed->FilePath != nullptr;
            miStepOver->Enabled = stopped;
            miStepInto->Enabled = stopped;
            miStepOut->Enabled = stopped;
            miRunToCursor->Enabled = stopped && ed != nullptr && ed->FilePath != nullptr;
            miUpStack->Enabled = stopped;
            miDownStack->Enabled = stopped && debugger->CurrentFrame > 0;
            miWatchExpression->Enabled = true;
            miSetVariable->Enabled = stopped;
            miStopDebugging->Enabled = debugging;
            miDeleteBreakpoints->Enabled = breakpoints->Count > 0;
            miCompilerOptions->Enabled = hasProject && !busy;
            miImportCcs->Enabled = settings->CcsEnabled && !busy;
            cboTarget->Enabled = writable && !busy;

            String^ ext = (ed != nullptr && ed->FilePath != nullptr) ? Path::GetExtension(ed->FilePath) : String::Empty;
            miConvC2S->Enabled = !busy && ed != nullptr && ed->FilePath != nullptr && String::Equals(ext, ".c", StringComparison::OrdinalIgnoreCase);
            miConvS2C->Enabled = !busy && ed != nullptr && ed->FilePath != nullptr && settings->IsShalimarExtension(ext);

            // title
            String^ title = AppInfo::Name;
            if (hasProject)
            {
                String^ ro = project->IsImported ? String::Format(" [{0}, read-only]", CcsLabel(project)) : project->IsReadOnly ? gcnew String(" [CCS, read-only]") : String::Empty;
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
        // "CCS 7.4" / "CCS 5.5", as the project's .ccsproject says; "Visual Studio 2022 v143" for a Visual Studio project
        static String^ CcsLabel(GrideProject^ p)
        {
            if (p != nullptr && p->IsVs) return p->Vs->Label;
            if (p == nullptr || !p->IsCcs) return "CCS";
            return p->Ccs->CcsVersion->Length == 0 ? Str("CCS 5.5") : String::Concat("CCS ", p->Ccs->CcsVersion->Length >= 3 ? p->Ccs->CcsVersion->Substring(0, 3) : p->Ccs->CcsVersion);
        }

        // Project > CCS / VS Mapping: RIDE's read-only dialog - what CCS set and what GRIDE builds with; for a Visual
        // Studio project the reading (every configuration's values, the items, what is not mapped) and GRIDE's lines
        void OnCcsMapping(Object^ sender, EventArgs^ e)
        {
            if (project != nullptr && project->IsVs)
            {
                Ui::ShowText(this, String::Format("{0} - {1} mapping", project->Name, CcsLabel(project)), VsMappingText(project)->Replace("\n", "\r\n"));
                return;
            }
            if (project == nullptr || !project->IsCcs) return;
            Ui::ShowText(this, String::Format("{0} - {1} mapping", project->Name, CcsLabel(project)),
                         project->Ccs->MappingText(BuildEngine::IsDebugConfig(project->ActiveConfig()->Name))->Replace("\n", "\r\n"));
        }

        // a Visual Studio project's mapping: GRIDE's lines, what RIDE's compilers are given, the reading
        static String^ VsMappingText(GrideProject^ p)
        {
            StringBuilder^ t = gcnew StringBuilder();
            for each (String^ l in p->Vs->ReportLines) t->Append(l)->Append("\n");
            t->Append("\nBuilt here with GRIDE's compilers for x86_64-windows into .gride\\ beside the project; c90, cpp11 and shalimar take Compiler Options' defaults (");
            ToolEntry^ c90 = ToolchainFile::Current->ToolByRole("cCompiler");
            CompilerOptionStore^ none = gcnew CompilerOptionStore();
            TargetEntry^ win = TargetInfo::Get(TargetKind::Windows)->Entry;
            t->Append(String::Format("c90 Debug {0}, Release {1}", CompilerOptionRules::Flags(none, c90, "Debug", win), CompilerOptionRules::Flags(none, c90, "Release", win)));
            t->Append("), the configuration's defines, include directories and libraries.\n\n");
            t->Append(p->Vs->Reading->MappingText());
            return t->ToString();
        }

        static bool IsProjectFile(String^ path)
        {
            String^ name = Path::GetFileName(path);
            String^ ext = Path::GetExtension(path);
            return String::Equals(ext, ".pro", StringComparison::OrdinalIgnoreCase) || CcsImporter::IsProjectFile(path) || VsImporter::IsVsFile(path);
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

            GrideProject^ p = nullptr;
            bool vs = VsImporter::IsVsFile(full);
            bool ccs = !vs && (CcsImporter::IsProjectFile(full) || Directory::Exists(full));
            if (ccs && !settings->CcsEnabled)
            {
                Ui::Error(this, String::Format("Could not open project:\n{0}\n\nOpening CCS projects is turned off under Tools > Options > GRIDE Paths.", full));
                return;
            }
            // GRIDE 2.0 holds one project: a solution with several C++ projects asks which (before anything is closed)
            String^ member = nullptr;
            if (vs && VsImporter::IsSolution(full) && File::Exists(full))
            {
                List<String^>^ names;
                try { names = VsImporter::CppProjectNames(full); }
                catch (Exception^ ex) { ShowException(String::Format("Could not open project:\n{0}", full), ex); return; }
                if (names->Count > 1)
                {
                    member = projectChooser != nullptr ? projectChooser->Invoke(full, names)
                           : Ui::Choose(this, Path::GetFileName(full), String::Format("{0} holds {1} C++ projects. GRIDE opens one - which?", Path::GetFileName(full), names->Count), names);
                    if (member == nullptr) return;
                }
            }
            if (!CloseProjectInternal()) return;
            try
            {
                if (vs)
                {
                    p = VsImporter::Import(full, member);
                    ApplyCcsState(p);
                }
                else if (ccs)
                {
                    p = CcsImporter::Import(full);
                    ApplyCcsState(p);
                }
                else
                    p = VsImporter::LoadPro(full);           // GRIDE's own project, or one standing for a Visual Studio project (R12)
            }
            catch (Exception^ ex)
            {
                ShowException(String::Format("Could not open project:\n{0}", full), ex);
                return;
            }
            project = p;
            RememberProjectTime();
            settings->AddRecentProject(p->IsVs && p->Vs->StateFile->Length == 0 ? p->FilePath : full);     // a Visual Studio project by its project file (its solution is found again); one a .pro stands for by the .pro
            SaveSettings();
            RefreshTree();
            RefreshCombos();
            UpdateUi();
            if (project->IsVs)
            {
                // what was read, what each configuration is built from, what is not mapped
                for each (String^ line in project->Vs->ReportLines) EnqueueOutput(line);
                if (project->Vs->ReportLines->Count > 0) ShowBottom(pageOutput);
                SetStatus(String::Format("Opened Visual Studio project '{0}' (read-only: built with GRIDE's toolchain into .gride, edited in Visual Studio; {1} files)", project->Name, project->SourceFiles->Count));
            }
            else if (project->IsCcs)
            {
                // RIDE's Messages lines about the reading: what is not supported, RIDE's own instead, the sources, the refusals
                for each (String^ line in project->CcsReportLines) EnqueueOutput(line);
                if (project->CcsReportLines->Count > 0) ShowBottom(pageOutput);
                SetStatus(String::Format("Opened CCS project '{0}' (read-only: built as GRIDE builds it, edited in CCS; {1} files)", project->Name, project->SourceFiles->Count));
            }
            else if (project->IsReadOnly)
                SetStatus(String::Format("Imported CCS project '{0}' (read-only, {1} files)", project->Name, project->SourceFiles->Count));
            else
                SetStatus(String::Format("Opened project '{0}'", project->Name));
            // the project's main file comes to the front (the user, 2026-10-21), chosen as GRIDE 1.0 chooses it
            String^ entry = project->FileToOpen();
            if (entry != nullptr && FindTab(entry) == nullptr) OpenFile(entry);
        }

        // RIDE's own state for a CCS project lives in the settings (ccs.projects), never in CCS's files: the
        // configuration chosen, the program's arguments, emulateOnly - what the read-only .project cannot hold;
        // a Visual Studio project's likewise, by its project file (R08)
        void ApplyCcsState(GrideProject^ p)
        {
            if (p == nullptr || !p->IsImported) return;
            if (p->IsVs && p->Vs->StateFile->Length > 0) return;          // the .pro standing for it keeps its state (R12)
            CcsProjectState^ st = p->IsVs ? settings->VsState(p->FilePath) : settings->CcsState(p->ProjectDir);
            if (st == nullptr) return;
            if (!String::IsNullOrEmpty(st->Toolchain)) p->Toolchain = st->Toolchain;
            if (p->FindConfig(st->Configuration) != nullptr) p->ActiveConfiguration = p->FindConfig(st->Configuration)->Name;
            p->ProgramArgs = st->ProgramArgs == nullptr ? nullptr : gcnew List<String^>(st->ProgramArgs);
            p->EmulateOnly = st->EmulateOnly;
            p->Normalize();
        }

        void RememberCcsState(GrideProject^ p)
        {
            if (p == nullptr || !p->IsImported) return;
            if (p->IsVs && p->Vs->StateFile->Length > 0)
            {
                // the .pro standing for it (R12); the Visual Studio files are never written
                try { VsImporter::SaveState(p); }
                catch (Exception^ ex) { ShowException(String::Format("Could not write {0}", p->Vs->StateFile), ex); }
                return;
            }
            if (p->IsVs) settings->RememberVsState(p->FilePath, p->ActiveConfiguration, p->ProgramArgs, p->EmulateOnly, p->Toolchain);
            else settings->RememberCcsState(p->ProjectDir, p->ActiveConfiguration, p->ProgramArgs, p->EmulateOnly, p->Toolchain);
            SaveSettings();
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

        // the node of a Visual Studio filter path ("Source\UI\Dialogs"), made with its parents as needed
        TreeNode^ VsFolderNode(TreeNode^ root, Dictionary<String^, TreeNode^>^ made, String^ path)
        {
            TreeNode^ at;
            if (made->TryGetValue(path, at)) return at;
            int cut = path->LastIndexOf(L'\\');
            TreeNode^ parent = cut > 0 ? VsFolderNode(root, made, path->Substring(0, cut)) : root;
            at = MakeFolderNode(cut > 0 ? path->Substring(cut + 1) : path);
            at->ToolTipText = path;
            parent->Nodes->Add(at);
            made[path] = at;
            return at;
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
                    ? String::Format("{0}  ({1}, read-only)", project->Name, CcsLabel(project))
                    : String::Format("{0}  [{1}, {2}]", project->Name, project->Language, project->Target);
                TreeNode^ root = gcnew TreeNode(rootText);
                root->Tag = gcnew NodeInfo(NodeKind::Project, project->FilePath);
                root->ToolTipText = project->FilePath;
                root->ForeColor = Color::FromArgb(0, 70, 140);
                TreeNode^ srcNode = MakeFolderNode("Source Files");
                TreeNode^ hdrNode = MakeFolderNode("Header Files");
                TreeNode^ otherNode = MakeFolderNode("Other Files");
                TreeNode^ toSelect = nullptr;
                // a Visual Studio project's files sit in its filters (its .filters, else their directories), nested at '\'
                Dictionary<String^, TreeNode^>^ vsFolders = project->IsVs ? gcnew Dictionary<String^, TreeNode^>(StringComparer::OrdinalIgnoreCase) : nullptr;
                if (vsFolders != nullptr)
                    for each (String^ f in project->Vs->Reading->Filters) VsFolderNode(root, vsFolders, f);
                // a .pro of the one format shows its groups as the folders, in its own order
                Dictionary<String^, TreeNode^>^ groupNodes = vsFolders == nullptr && project->HasGroups ? gcnew Dictionary<String^, TreeNode^>(StringComparer::OrdinalIgnoreCase) : nullptr;
                if (groupNodes != nullptr)
                    for each (String^ g in project->Groups) { groupNodes[g] = MakeFolderNode(g); root->Nodes->Add(groupNodes[g]); }

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
                    String^ vsFolder;
                    if (vsFolders != nullptr && project->Vs->FolderOf->TryGetValue(rel, vsFolder))
                    {
                        n->Text = Path::GetFileName(rel);
                        if (!File::Exists(full)) n->Text = String::Format("{0}  (missing)", n->Text);
                        bool notBuilt = project->ActiveConfig()->Excludes(full) && TargetInfo::IsSource(ext, settings);
                        if (notBuilt) { n->ForeColor = Color::Gray; n->ToolTipText = String::Format("{0} (not built in {1})", full, project->ActiveConfig()->Name); }
                        VsFolderNode(root, vsFolders, vsFolder)->Nodes->Add(n);
                    }
                    else if (groupNodes != nullptr)
                    {
                        n->Text = Path::GetFileName(rel);
                        if (!File::Exists(full)) n->Text = String::Format("{0}  (missing)", n->Text);
                        if (!project->IsBuilt(rel) && TargetInfo::IsSource(ext, settings)) { n->ForeColor = Color::Gray; n->ToolTipText = String::Format("{0} (its group is not built)", full); }
                        String^ g = project->GroupOfFile(rel);
                        TreeNode^ gn;
                        if (!groupNodes->TryGetValue(g, gn)) { gn = MakeFolderNode(g); groupNodes[g] = gn; root->Nodes->Add(gn); }
                        gn->Nodes->Add(n);
                    }
                    else if (TargetInfo::IsSource(ext, settings)) srcNode->Nodes->Add(n);
                    else if (TargetInfo::IsHeader(ext)) hdrNode->Nodes->Add(n);
                    else otherNode->Nodes->Add(n);
                    if (selected != nullptr && PathUtil::SamePath(selected, full)) toSelect = n;
                }
                if (vsFolders == nullptr && groupNodes == nullptr)
                {
                    root->Nodes->Add(srcNode);
                    root->Nodes->Add(hdrNode);
                    root->Nodes->Add(otherNode);
                }

                if (project->CcsConfigurations != nullptr && project->CcsConfigurations->Count > 0)
                {
                    TreeNode^ cc = MakeFolderNode(project->IsVs ? "Visual Studio Configurations (read-only)" : "CCS Build Configurations (read-only)");
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
            ed->BreakpointClicked += gcnew LineHandler(this, &MainForm::OnGutterBreakpoint);
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
            SortedSet<int>^ marks;
            if (breakpoints->TryGetValue(full, marks)) ed->Breakpoints = marks;
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
            if (ed != nullptr && !ed->IsReadOnly && ed->NeedsSaving)
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
                if (ed != nullptr && !ed->IsReadOnly && ed->NeedsSaving) dirty->Add(ed);
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

        // the close cross of tab i: a square at the right end of its rectangle
        System::Drawing::Rectangle TabCloseRect(int i)
        {
            System::Drawing::Rectangle r = docs->GetTabRect(i);
            int s = 14;
            return System::Drawing::Rectangle(r.Right - s - 5, r.Top + (r.Height - s) / 2, s, s);
        }

        int TabCloseAt(System::Drawing::Point pt)
        {
            for (int i = 0; i < docs->TabCount; i++)
                if (TabCloseRect(i).Contains(pt)) return i;
            return -1;
        }

        void OnDocsDrawItem(Object^ sender, DrawItemEventArgs^ e)
        {
            if (e->Index < 0 || e->Index >= docs->TabCount) return;
            TabPage^ page = docs->TabPages[e->Index];
            System::Drawing::Rectangle r = docs->GetTabRect(e->Index);
            bool selected = e->Index == docs->SelectedIndex;
            Graphics^ g = e->Graphics;
            g->FillRectangle(selected ? SystemBrushes::Window : SystemBrushes::Control, r);
            System::Drawing::Rectangle text(r.Left + 6, r.Top, r.Width - 6 - 24, r.Height);
            TextRenderer::DrawText(g, page->Text, docs->Font, text, SystemColors::ControlText,
                                   TextFormatFlags::Left | TextFormatFlags::VerticalCenter | TextFormatFlags::EndEllipsis | TextFormatFlags::NoPrefix);
            System::Drawing::Rectangle x = TabCloseRect(e->Index);
            if (e->Index == closeHot) g->FillRectangle(SystemBrushes::ControlDark, x);
            Pen^ pen = gcnew Pen(e->Index == closeHot ? SystemColors::HighlightText : (selected ? SystemColors::ControlText : SystemColors::GrayText), 1.6f);
            System::Drawing::Drawing2D::SmoothingMode was = g->SmoothingMode;
            g->SmoothingMode = System::Drawing::Drawing2D::SmoothingMode::AntiAlias;
            int m = 4;
            g->DrawLine(pen, x.Left + m, x.Top + m, x.Right - m - 1, x.Bottom - m - 1);
            g->DrawLine(pen, x.Right - m - 1, x.Top + m, x.Left + m, x.Bottom - m - 1);
            g->SmoothingMode = was;
            delete pen;
        }

        void OnDocsMouseDown(Object^ sender, MouseEventArgs^ e)
        {
            if (e->Button != System::Windows::Forms::MouseButtons::Left) return;
            int i = TabCloseAt(e->Location);
            if (i < 0) return;
            closeHot = -1;
            CloseTab(docs->TabPages[i]);
        }

        void OnDocsMouseMove(Object^ sender, MouseEventArgs^ e)
        {
            int hot = TabCloseAt(e->Location);
            if (hot != closeHot) { closeHot = hot; docs->Invalidate(); }
        }

        void OnDocsMouseLeave(Object^ sender, EventArgs^ e)
        {
            if (closeHot != -1) { closeHot = -1; docs->Invalidate(); }
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
                GrideProject^ p = GrideProject::Create(d->ProjectName, folder, d->Language, d->Target, settings);   // the settings' compiler and options
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
            {
                lines = gcnew array<String^>{
                    String::Format("// {0} - created by GRIDE 2.0 (C++11)", fileName),
                    "#include <iostream>", "",
                    "int main()", "{",
                    "    std::cout << \"Hello from GRIDE\" << std::endl;",
                    "    return 0;", "}", "" };
                break;
            }
            case LanguageKind::Shalimar:
            {
                lines = gcnew array<String^>{
                    String::Format("// {0} - created by GRIDE 2.0 (Shalimar)", fileName),
                    "// Write your Shalimar program here.", "" };
                break;
            }
            default:
            {
                lines = gcnew array<String^>{
                    String::Format("/* {0} - created by GRIDE 2.0 (C90) */", fileName),
                    "#include <stdio.h>", "",
                    "int main(void)", "{",
                    "    printf(\"Hello from GRIDE\\n\");",
                    "    return 0;", "}", "" };
                break;
            }
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
            d->Filter = "GRIDE project (*.pro)|*.pro|CCS 7.4 / 5.5 project (.project, .ccsproject, .cproject)|.project;.ccsproject;.cproject;*.project;*.ccsproject;*.cproject|"
                        "Visual Studio solution or C++ project (*.sln, *.vcxproj, *.vcproj)|*.sln;*.vcxproj;*.vcproj|All files (*.*)|*.*";
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
            d->Description = "Select a CCS 7.4 or 5.5 project folder (the one with .project, .ccsproject and .cproject)";
            try
            {
                if (d->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) return;
                if (!CcsReading::IsProject(d->SelectedPath) && !CcsWorkspace::IsWorkspace(d->SelectedPath))
                {
                    Ui::Error(this, "This folder is not a CCS project (it needs .project and .ccsproject) or a CCS workspace.");
                    return;
                }
                OpenProject(CcsReading::IsProject(d->SelectedPath) ? Path::Combine(d->SelectedPath, ".project") : d->SelectedPath);
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
                if (d->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) return;
                if (busy) { Ui::Info(this, "Properties cannot be changed while a build is running."); return; }
                if (project->IsImported && d->ImportedView != nullptr)
                {
                    d->ImportedView->ApplyTo(project);
                    RememberCcsState(project);
                    UpdatePropertyGrid();
                    UpdateUi();
                    return;
                }
                if (project->IsReadOnly) return;
                d->View->ApplyTo(project);
                SaveProject();
                RefreshTree();
                RefreshCombos();
                RefreshEditorModes();
                UpdateUi();
            }
            finally { delete d; }
        }

        // RIDE's Compiler Options of the project, per configuration; a CCS project's options are its
        // configurations' (the mapping shows them)
        void OnCompilerOptions(Object^ sender, EventArgs^ e)
        {
            if (project == nullptr) return;
            if (project->IsImported) { OnCcsMapping(sender, e); return; }
            if (project->IsReadOnly) { Ui::Info(this, "This project is read-only."); return; }
            CompilerOptionsDialog^ d = gcnew CompilerOptionsDialog(project);
            try
            {
                if (d->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) return;
                if (busy) { Ui::Info(this, "Compiler Options cannot be changed while a build is running."); return; }
                project->SetOptionStore(d->Result);
                SaveProject();
                UpdatePropertyGrid();
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
            if (updatingCombos || project == nullptr || (project->IsReadOnly && !project->IsImported) || cboConfig->SelectedItem == nullptr) return;
            project->ActiveConfiguration = safe_cast<String^>(cboConfig->SelectedItem);
            if (project->IsImported) RememberCcsState(project);
            else SaveProject();
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

        // ============================================================ the debugger (R10)
        TabPage^ AddPanePage(String^ title, Control^ pane)
        {
            TabPage^ page = gcnew TabPage(title);
            page->Controls->Add(pane);
            bottomTabs->TabPages->Add(page);
            return page;
        }

        // RIDE's keys line under the variables, from the Debug menu's keys
        static String^ KeysLine()
        {
            return "F8 carries on   F7 steps over   F6 steps into   F9 sets a breakpoint   Ctrl+Up / Ctrl+Down walks the stack   name = value below sets a variable";
        }

        // the file's breakpoints; made (and handed to its editor) when asked to
        SortedSet<int>^ BreakpointsOf(String^ file, bool make)
        {
            SortedSet<int>^ set;
            if (breakpoints->TryGetValue(file, set)) return set;
            if (!make) return nullptr;
            set = gcnew SortedSet<int>();
            breakpoints[file] = set;
            TabPage^ t = FindTab(file);
            if (t != nullptr) safe_cast<CodeEditor^>(t->Tag)->Breakpoints = set;
            return set;
        }

        // F9, or a click in the gutter: a breakpoint on the line, or none; the engine told while it is stopped
        void ToggleBreakpoint(String^ file, int line)
        {
            if (String::IsNullOrEmpty(file) || line < 1) return;
            SortedSet<int>^ set = BreakpointsOf(file, true);
            if (!set->Remove(line)) set->Add(line);
            if (set->Count == 0) breakpoints->Remove(file);
            TabPage^ t = FindTab(file);
            if (t != nullptr) { CodeEditor^ ed = safe_cast<CodeEditor^>(t->Tag); ed->Breakpoints = set->Count == 0 ? nullptr : set; ed->RefreshMarks(); }
            breakpointsPane->Fill(breakpoints);
            if (debugger != nullptr && debugger->State == DebugState::Break) debugger->BreakpointsChanged();
            SetStatus(set->Contains(line) ? String::Format("a breakpoint on {0}:{1}", Path::GetFileName(file), line) : String::Format("no breakpoint on {0}:{1}", Path::GetFileName(file), line));
            UpdateUi();
        }

        void OnToggleBreakpoint(Object^ sender, EventArgs^ e)
        {
            CodeEditor^ ed = ActiveEditor();
            if (ed == nullptr || ed->FilePath == nullptr) return;
            ToggleBreakpoint(Path::GetFullPath(ed->FilePath), ed->CurrentLine);
        }

        void OnGutterBreakpoint(int line)
        {
            CodeEditor^ ed = ActiveEditor();
            if (ed != nullptr && ed->FilePath != nullptr) ToggleBreakpoint(Path::GetFullPath(ed->FilePath), line);
        }

        void OnDeleteBreakpoints(Object^ sender, EventArgs^ e)
        {
            breakpoints->Clear();
            for each (TabPage^ p in docs->TabPages) { CodeEditor^ ed = dynamic_cast<CodeEditor^>(p->Tag); if (ed != nullptr) ed->Breakpoints = nullptr; }
            breakpointsPane->Fill(breakpoints);
            if (debugger != nullptr && debugger->State == DebugState::Break) debugger->BreakpointsChanged();
            UpdateUi();
        }

        void OnBreakpointNavigate(String^ file, int line)
        {
            CodeEditor^ ed = OpenFile(file);
            if (ed != nullptr) ed->GoToLine(line);
        }

        void OnBreakpointRemove(String^ file, int line) { ToggleBreakpoint(file, line); }

        // F8: carries on when stopped; else builds the Debug configuration and starts the debugger on it
        void OnDebugStart(Object^ sender, EventArgs^ e)
        {
            if (debugger != nullptr && debugger->State == DebugState::Break) { debugger->Continue(); return; }
            if (debugger != nullptr && debugger->IsActive) return;
            if (busy || ProgramBusy()) return;
            if (project == nullptr) { Ui::Info(this, "Open or create a project first."); return; }
            if (!BuildEngine::IsDebugConfig(project->ActiveConfig()->Name))
            {
                // RIDE's refusal before anything is built
                DebugLaunch^ l = DebuggerChoice::For(project, settings);
                ShowBottom(pageOutput);
                EnqueueOutput(l->WhyNot);
                SetStatus(l->WhyNot);
                return;
            }
            if (!PrepareBuild()) return;
            BuildJob^ job = MakeJob(JobKind::BuildAndDebug);
            SetStatus("Build started (for the debugger)...");
            StartJob(job, true);
        }

        // the engine that debugs the build (DebuggerChoice: RIDE's), its events marshalled to the window
        void StartDebugging()
        {
            DebugLaunch^ l = DebuggerChoice::For(project, settings);
            ShowBottom(pageOutput);
            if (!l->Possible)
            {
                EnqueueOutput(String::Concat("[debug] ", l->WhyNot->Length > 0 ? l->WhyNot : Str("nothing can debug this build")));
                SetStatus(l->WhyNot);
                return;
            }
            if (debugger != nullptr) { debugger->Stop(); debugger = nullptr; }
            DebugEngine^ d = l->Kind == DebuggerKind::Shalimar ? safe_cast<DebugEngine^>(gcnew ShalimarSession(l)) : safe_cast<DebugEngine^>(gcnew ProcessDebugger(l));
            d->StateChanged += gcnew DebugStateHandler(this, &MainForm::OnDebuggerState);
            d->Stopped += gcnew DebugStopHandler(this, &MainForm::OnDebuggerStopped);
            d->Output += gcnew TextLineHandler(this, &MainForm::OnDebuggerLine);
            d->ProgramOutput += gcnew TextLineHandler(this, &MainForm::EnqueueOutput);
            d->SetBreakpoints(breakpoints);
            List<String^>^ files = gcnew List<String^>();
            for each (String^ f in project->SourceFiles) files->Add(project->ResolvePath(f));
            d->SetSourceFiles(files);
            debugger = d;
            localsPane->ClearView();
            watchPane->UpdateFrom(d);
            SetStatus(String::Format("debugging with {0}", l->Backend));
            if (!d->Start(l->Program, project->TargetValue, nullptr, 0))
            {
                SetStatus("the debugger did not start - see the Output pane");
                debugger = nullptr;
                watchPane->ClearView();
            }
            UpdateUi();
        }

        void OnDebuggerLine(String^ line) { EnqueueOutput(String::Concat("[debug] ", line)); }

        void OnDebuggerState(DebugState state)
        {
            if (InvokeRequired) { BeginInvoke(gcnew DebugStateHandler(this, &MainForm::OnDebuggerState), state); return; }
            DebugEngine^ d = debugger;
            if (d == nullptr) { UpdateUi(); return; }
            if (state == DebugState::Running) ClearStopArrow();
            if (state == DebugState::Idle)
            {
                ClearStopArrow();
                localsPane->ClearView();
                callStackPane->ClearView();
                registersPane->ClearView();
                memoryPane->ClearView();
                watchPane->ClearView();
                // RIDE's end line, and where to go from there
                if (d->Exited)
                {
                    EnqueueOutput("[debug] F8 starts it again. The breakpoints are still where you put them.");
                    SetStatus(String::Format("the program returned {0}", d->ExitStatus));
                }
                else SetStatus("debugging ended");
                debugger = nullptr;
            }
            UpdateUi();
        }

        void OnDebuggerStopped(StopInfo^ info)
        {
            if (InvokeRequired) { BeginInvoke(gcnew DebugStopHandler(this, &MainForm::OnDebuggerStopped), info); return; }
            if (debugger == nullptr || info == nullptr) return;
            localsPane->NewStop();
            watchPane->NewStop();
            ShowStop(info->File, info->Line, true);
            RefreshDebugPanes();
            ShowBottom(pageLocals);
            SetStatus(info->Function->Length > 0 ? String::Format("{0}:{1} in {2}", Path::GetFileName(info->File), info->Line, info->Function)
                                                 : String::Format("{0}:{1}", Path::GetFileName(info->File), info->Line));
            UpdateUi();
        }

        // the file it stopped in opened, the caret on the line; the arrow when it is the stop itself
        void ShowStop(String^ file, int line, bool arrow)
        {
            if (String::IsNullOrEmpty(file) || !File::Exists(file) || line < 1) return;
            CodeEditor^ ed = OpenFile(file);
            if (ed == nullptr) return;
            ed->GoToLine(line);
            if (arrow)
            {
                ClearStopArrow();
                ed->StopLine = line;
                stoppedIn = ed;
            }
        }

        void ClearStopArrow()
        {
            if (stoppedIn != nullptr) stoppedIn->StopLine = 0;
            stoppedIn = nullptr;
        }

        void RefreshDebugPanes()
        {
            DebugEngine^ d = debugger;
            localsPane->UpdateFrom(d);
            watchPane->UpdateFrom(d);
            callStackPane->UpdateFrom(d);
            registersPane->UpdateFrom(d);
            memoryPane->UpdateFrom(d);
        }

        void OnStepOver(Object^ sender, EventArgs^ e) { if (debugger != nullptr && debugger->State == DebugState::Break) debugger->StepOver(); }
        void OnStepInto(Object^ sender, EventArgs^ e) { if (debugger != nullptr && debugger->State == DebugState::Break) debugger->StepInto(); }
        void OnStepOut(Object^ sender, EventArgs^ e) { if (debugger != nullptr && debugger->State == DebugState::Break) debugger->StepOut(); }

        void OnRunToCursor(Object^ sender, EventArgs^ e)
        {
            CodeEditor^ ed = ActiveEditor();
            if (debugger == nullptr || debugger->State != DebugState::Break || ed == nullptr || ed->FilePath == nullptr) return;
            debugger->RunToCursor(Path::GetFullPath(ed->FilePath), ed->CurrentLine);
        }

        void OnStopDebugging(Object^ sender, EventArgs^ e)
        {
            DebugEngine^ d = debugger;
            if (d == nullptr) return;
            d->Stop();
        }

        // Ctrl+Up / Ctrl+Down (RIDE's): the frame above or below looked at, the editor on its line, the panes
        // refreshed; the status bar says where that is (RIDE's sentences)
        void OnUpStack(Object^ sender, EventArgs^ e) { if (debugger != nullptr && debugger->State == DebugState::Break) LookAtFrame(debugger->CurrentFrame + 1); }
        void OnDownStack(Object^ sender, EventArgs^ e) { if (debugger != nullptr && debugger->State == DebugState::Break) LookAtFrame(debugger->CurrentFrame - 1); }

        void LookAtFrame(int index)
        {
            DebugEngine^ d = debugger;
            if (d == nullptr || d->State != DebugState::Break) return;
            if (index < 0) { SetStatus("this is where the program stopped - there is nothing below it"); return; }
            String^ cannot = d->WhyNoStackWalk();
            if (!String::IsNullOrEmpty(cannot) && index > 0) { SetStatus(cannot); return; }
            List<DebugFrame^>^ frames = d->CallStack();
            if (index >= frames->Count) { SetStatus("nothing called main, which is the top"); return; }
            if (index != d->CurrentFrame && !d->SelectFrame(index)) { SetStatus(String::Format("{0} cannot look at another frame", d->Backend)); return; }
            DebugFrame^ f = frames[index];
            ShowStop(f->File, f->Line, false);
            RefreshDebugPanes();
            SetStatus(index == 0 ? Str("back where it stopped")
                                 : String::Format("{0}:{1} in {2} - where the call came from", Path::GetFileName(f->File), f->Line, f->Function));
            UpdateUi();
        }

        // name = value: the engine sets it; its complaint in the Output pane
        void OnVariableSet(String^ name, String^ value)
        {
            DebugEngine^ d = debugger;
            if (d == nullptr || d->State != DebugState::Break) return;
            String^ said;
            if (!d->SetVariable(name, value, said))
            {
                EnqueueOutput(String::Format("[debug] {0} = {1}: {2}", name, value, said));
                SetStatus(said);
            }
            RefreshDebugPanes();
        }

        void OnWatchExpression(Object^ sender, EventArgs^ e)
        {
            String^ text = InputDialog::Ask(this, "Watch Expression", "An expression to watch (read again at every stop):", String::Empty);
            if (String::IsNullOrWhiteSpace(text)) return;
            watchPane->AddExpression(text);
            ShowBottom(pageWatch);
        }

        void OnSetVariable(Object^ sender, EventArgs^ e)
        {
            String^ text = InputDialog::Ask(this, "Set Variable", "name = value:", String::Empty);
            if (String::IsNullOrWhiteSpace(text)) return;
            int eq = text->IndexOf(L'=');
            if (eq <= 0) return;
            OnVariableSet(text->Substring(0, eq)->Trim(), text->Substring(eq + 1)->Trim());
        }

        // ============================================================ Build / Run / Convert
        bool PrepareBuild()
        {
            if (busy) return false;
            if (project == nullptr) { Ui::Info(this, "Open or create a project first."); return false; }
            if (project->IsReadOnly && !project->IsImported)
            {
                Ui::Info(this, "This project is read-only in GRIDE.");
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
            // Work on copies so the UI can never change them under the worker thread. A CCS or Visual Studio
            // project is read afresh for every build, as RIDE reads it (its configurations carry what a .pro cannot hold).
            if (project->IsImported)
            {
                job->Project = project->IsVs ? VsImporter::Read(project->FilePath, project->Vs->SolutionPath) : CcsImporter::Import(project->FilePath);
                job->Project->ActiveConfiguration = project->ActiveConfiguration;
                job->Project->Toolchain = project->Toolchain;
                if (project->IsVs) job->Project->Vs->StateFile = project->Vs->StateFile;
                job->Project->ProgramArgs = project->ProgramArgs == nullptr ? nullptr : gcnew List<String^>(project->ProgramArgs);
                job->Project->EmulateOnly = project->EmulateOnly;
                job->Project->Normalize();
            }
            else
            {
                job->Project = JsonClone<GrideProject>(project);
                job->Project->FilePath = project->FilePath;
                job->Project->Normalize();
            }
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
        void OnBuildAndRun(Object^ sender, EventArgs^ e) { StartRun(RunHow::Run, true); }
        void OnRun(Object^ sender, EventArgs^ e) { StartRun(RunHow::Run, false); }
        void OnEmulate(Object^ sender, EventArgs^ e) { StartRun(RunHow::Emulate, true); }
        void OnVerify(Object^ sender, EventArgs^ e) { StartRun(RunHow::Verify, true); }

        // RIDE's Stop: the build, else the program running in the Output pane and everything it started
        void OnStop(Object^ sender, EventArgs^ e)
        {
            if (busy && cts != nullptr)
            {
                cts->Cancel();
                SetStatus("Stopping...");
            }
            else if (runner->IsRunning)
            {
                SetStatus("Stopping the program...");
                runner->Stop();
            }
            else if (verifying && verifyCts != nullptr)
            {
                SetStatus("Stopping the verify...");
                verifyCts->Cancel();
            }
            else if (debugger != nullptr && debugger->IsActive) OnStopDebugging(sender, e);
        }

        bool ProgramBusy() { return runner != nullptr && (runner->IsRunning || verifying); }

        // F5, Ctrl+F5, Emulate, Verify: a run this machine cannot make is turned away before anything is
        // built (RIDE's buildAndRun); otherwise the build, then the program in the Output pane
        void StartRun(RunHow how, bool build)
        {
            if (busy || ProgramBusy()) return;
            if (project == nullptr) { Ui::Info(this, "Open or create a project first."); return; }
            String^ refused = ProgramLauncher::Refusal(project, how);
            if (refused != nullptr)
            {
                ClearBuildOutput();
                ShowBottom(pageOutput);
                EnqueueOutput(refused);
                SetStatus(refused);
                return;
            }
            if (!build)
            {
                ClearBuildOutput();
                RunChosen(how);
                return;
            }
            if (!PrepareBuild()) return;
            BuildJob^ job = MakeJob(JobKind::BuildAndRun);
            job->How = how;
            SetStatus("Build started...");
            StartJob(job, true);
        }

        // the project's program, run as RIDE's window runs one: in the Output pane with an input line and
        // "[program returned N]" at the end; on the C6747 the simulator on the .out (the emulator on the
        // assembly when the project says emulateOnly), Emulate the emulator, Verify both, compared
        void RunChosen(RunHow how)
        {
            if (project == nullptr || ProgramBusy()) return;
            ShowBottom(pageOutput);
            TargetInfo^ t = TargetInfo::Get(project->TargetValue);
            // the way before the port, kept as an option: the program in a console window of its own
            if (!settings->RunInIde && how == RunHow::Run && t->RunsNatively)
            {
                int rc = BuildEngine::RunOutput(project, settings, gcnew Action<String^>(this, &MainForm::EnqueueOutput), System::Threading::CancellationToken::None);
                SetStatus(rc == 0 ? Str("Started in a console window") : Str("The program could not be started"));
                return;
            }
            RunPlan^ plan = ProgramLauncher::Plan(project, settings, how);
            if (plan->Error != nullptr) { EnqueueOutput(plan->Error); SetStatus(plan->Error); return; }
            for each (String^ note in plan->Notes) EnqueueOutput(note);
            if (plan->How == RunHow::Verify)
            {
                if (verifyCts != nullptr) delete verifyCts;
                verifyCts = gcnew CancellationTokenSource();
                verifying = true;
                VerifyJob^ job = gcnew VerifyJob(this, plan, verifyCts->Token);
                System::Threading::Thread^ th = gcnew System::Threading::Thread(gcnew System::Threading::ThreadStart(job, &VerifyJob::Run));
                th->IsBackground = true;
                th->Start();
                SetStatus(String::Format("Verifying {0} on {1} and {2}...", plan->OutName, plan->EmulatorName, plan->SimulatorName));
                UpdateUi();
                return;
            }
            EnqueueOutput(plan->Shown);
            if (!runner->Start(plan->Exe, plan->Args, plan->WorkDir, plan->Tool, plan->CodePage)) { SetStatus("The program could not be started"); return; }
            txtInput->Enabled = true;
            txtInput->Focus();
            SetStatus(String::Format("Running {0} - type its input below the output; Stop (Shift+F5) ends it", Path::GetFileName(plan->Exe)));
            UpdateUi();
        }

        // Verify on a thread of its own: both programs captured, the lines into the Output pane
        ref class VerifyJob
        {
        public:
            VerifyJob(MainForm^ form, RunPlan^ plan, System::Threading::CancellationToken token) { this->form = form; this->plan = plan; this->token = token; }
            void Run()
            {
                int status = 2;
                try { status = ProgramRunner::Verify(plan, gcnew Action<String^>(form, &MainForm::EnqueueOutput), token); }
                catch (Exception^ ex) { form->EnqueueOutput(String::Concat("[verify] ", ex->Message)); }
                form->OnProgramExited(status, token.IsCancellationRequested);
            }
        private:
            MainForm^ form;
            RunPlan^ plan;
            System::Threading::CancellationToken token;
        };

        // the program ended (a worker thread): "[program returned N]" or "[stopped]" after its last line
        void OnProgramExited(int status, bool stopped)
        {
            EnqueueOutput(String::Empty);
            EnqueueOutput(stopped ? Str("[stopped]") : String::Format("[program returned {0}]", status));
            if (closing || IsDisposed || !IsHandleCreated) return;
            try { BeginInvoke(gcnew Action<int, bool>(this, &MainForm::ProgramEnded), gcnew array<Object^>{ status, stopped }); }
            catch (InvalidOperationException^) { }
        }

        void ProgramEnded(int status, bool stopped)
        {
            verifying = false;
            txtInput->Enabled = false;
            SetStatus(stopped ? Str("The program was stopped") : String::Format("The program returned {0}", status));
            UpdateUi();
        }

        // the input line: Enter hands the line to the program (and shows it, as a console echoes), Ctrl+Z ends its input
        void OnInputKeyDown(Object^ sender, KeyEventArgs^ e)
        {
            if (e->Control && e->KeyCode == Keys::Z)
            {
                e->SuppressKeyPress = true;
                runner->CloseInput();
                EnqueueOutput("^Z");
                txtInput->Enabled = false;
                return;
            }
            if (e->KeyCode != Keys::Enter) return;
            e->SuppressKeyPress = true;
            String^ line = txtInput->Text;
            txtInput->Clear();
            if (runner->Write(line)) EnqueueOutput(line);
            else SetStatus("No program is running to read that");
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

        // the native question (RIDE's askNative) asked from the build's thread: a Yes/No box on the window's
        bool AskFromBuild(String^ question)
        {
            if (closing || IsDisposed) return false;
            try { return safe_cast<bool>(Invoke(gcnew Func<String^, bool>(this, &MainForm::AskOnWindow), gcnew array<Object^>{ question })); }
            catch (Exception^) { return false; }
        }

        bool AskOnWindow(String^ question)
        {
            FlushQueues();                  // the build's output so far, above the question
            return !closing && Ui::Confirm(this, question);
        }

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
                    engine->Asker = gcnew Func<String^, bool>(this, &MainForm::AskFromBuild);
                    BuildAction action = BuildAction::Build;
                    if (job->Kind == JobKind::Rebuild) action = BuildAction::Rebuild;
                    else if (job->Kind == JobKind::Clean) action = BuildAction::Clean;
                    else if (job->Kind == JobKind::CompileFile) action = BuildAction::CompileFile;
                    job->Result = engine->Execute(action);      // a BuildAndRun's program runs on the window's thread afterwards (RunChosen)
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
            // the worker posts this to the thread that started it; where that thread had no window's
            // synchronization context it arrives on a pool thread, and is handed to the window's
            if (InvokeRequired)
            {
                if (!IsDisposed) BeginInvoke(gcnew RunWorkerCompletedEventHandler(this, &MainForm::OnWorkerCompleted), gcnew array<Object^>{ sender, e });
                return;
            }
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
                // RIDE puts the caret on the first error of a failed build
                if (!job->Result->Success && !job->Result->Cancelled && settings->JumpToFirstError && job->Result->FirstError != nullptr)
                {
                    BuildMessage^ first = job->Result->FirstError;
                    if (!String::IsNullOrEmpty(first->FilePath) && first->Line > 0)
                    {
                        GoToMessage(first);
                        SetStatus(String::Format("{0}:{1}: error: {2}", Path::GetFileName(first->FilePath), first->Line, first->Text));
                    }
                }
            }
            else if (job->Kind == JobKind::Run) SetStatus(job->ExitCode == 0 ? gcnew String("Run finished") : String::Format("Run ended with code {0}", job->ExitCode));
            // a successful build of Build and Run, Emulate or Verify: the program now runs in the Output pane
            if (job->Kind == JobKind::BuildAndRun && job->Result != nullptr && job->Result->Success && !job->Result->Cancelled && project != nullptr)
                RunChosen(job->How);
            // a successful build for F8: the debugger now (R10)
            if (job->Kind == JobKind::BuildAndDebug && job->Result != nullptr && job->Result->Success && !job->Result->Cancelled && project != nullptr)
                StartDebugging();

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
            String^ file = BuildMessage::Leaf(m->FilePath);
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
            if (m == nullptr) m = parser->ParseLocation(text, baseDir);      // RIDE's whereIs_: a place with no severity word
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
            String^ openAfter = nullptr;
            try
            {
                if (toolsPage) d->SelectToolsPage();
                if (d->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) return;
                IdeSettings^ s = d->Result;
                openAfter = d->OpenAfter;
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
            if (openAfter != nullptr && File::Exists(openAfter)) OpenFile(openAfter);
        }

        // what toolchain.json's reader found wrong, at start-up
        void ReportToolchainProblems()
        {
            ToolchainFile^ f = ToolchainFile::Current;
            if (f->Problems->Count == 0) return;
            StringBuilder^ sb = gcnew StringBuilder();
            for each (String^ p in f->Problems) sb->Append(p)->Append("\r\n");
            txtOutput->AppendText(sb->ToString());
        }

        // Tools > Check Toolchain: the files read again, their problems, and where every tool was found or looked for
        void OnCheckToolchain(Object^ sender, EventArgs^ e)
        {
            ToolchainFile^ f = ToolchainFile::Reload();
            settings->Normalize();
            StringBuilder^ sb = gcnew StringBuilder();
            sb->Append("------ Toolchain check ------\r\n");
            sb->AppendFormat("toolchain.json (shipped): {0}{1}\r\n", f->ShippedFile, f->ShippedFound ? "" : " - not found");
            sb->AppendFormat("toolchain.json (user): {0}{1}\r\n", f->UserFile, f->UserFileExists ? "" : " - none, the shipped values apply");
            for each (String^ p in f->Problems) sb->Append(p)->Append("\r\n");
            if (f->Problems->Count == 0) sb->Append("no problems in toolchain.json\r\n");
            for each (ToolDefinition^ t in settings->Tools)
            {
                String^ found = t->ResolvedPath();
                if (!t->IsConfigured()) sb->AppendFormat("{0}: not configured{1}\r\n", t->DisplayName, t->Defined ? "" : " - toolchain.json defines nothing for it");
                else if (found != nullptr) sb->AppendFormat("{0}: {1}{2}\r\n", t->DisplayName, found, t->Custom ? " (changed under Options)" : "");
                else sb->AppendFormat("{0}: '{1}' not found{2}\r\n", t->DisplayName, t->ExecutablePath, t->IsBareName() ? " - looked in " + t->LookedIn() : "");
            }
            txtOutput->AppendText(sb->ToString());
            UpdateUi();
        }

        // a Help item: its page from the help folders (HelpFiles), else what was looked for
        void OnHelpPage(Object^ sender, EventArgs^ e)
        {
            ToolStripItem^ item = dynamic_cast<ToolStripItem^>(sender);
            String^ name = item != nullptr ? dynamic_cast<String^>(item->Tag) : nullptr;
            if (name == nullptr) return;
            String^ f = HelpFiles::Find(name);
            if (f == nullptr)
            {
                SetStatus(String::Concat(name, " not found"));
                Ui::Info(this, HelpFiles::Missing(name, HelpFiles::ExeDirectory()));
                return;
            }
            try { HelpFiles::Open(f); SetStatus(String::Concat("Opened ", f)); }
            catch (Exception^ ex) { ShowException(String::Format("Could not open:\n{0}", f), ex); }
        }

        void OnPlaceholderHelp(Object^ sender, EventArgs^ e)
        {
            StringBuilder^ sb = gcnew StringBuilder();
            sb->Append("Placeholders in a tool's arguments. GRIDE's spelling first, then toolchain.json's where it has one; both are filled in.\r\n\r\n");
            array<String^>^ own = gcnew array<String^>{
                "{in}          input file: the source being compiled ($(Source)), the assembly being assembled ($(AsmFile)), the file converted",
                "{out}         output file: the assembly written ($(AsmFile)), the object ($(ObjFile)), the program linked ($(Output))",
                "{objs}        all object files (linker) - $(Objects)",
                "{libs}        libraries, each with the tool's library switch - $(LibFlags)",
                "{crtlibs}     the C runtime libraries the Windows link names for the configuration - $(CrtLibs)",
                "{cmdfiles}    TMS6747 linker command files (*.cmd) in the project, or the one GRIDE writes; $(LinkerCmd) is the first of them, unquoted",
                "{runtime}     RTS6x runtime library file (the Debug file for a Debug build) - $(Runtime)",
                "{runtimedir}  its directory (lnk6x -i) - $(RuntimeDir)",
                "{includes}    include folders, each with the include switch - $(IncludeFlags)",
                "{defines}     defines of the active configuration - $(DefineFlags)",
                "{configflags} the tool's flags for the configuration from toolchain.json (c90's -g -D_DEBUG=1 / -O2 -DNDEBUG=1 ...) - $(ConfigFlags)",
                "{flags}       compiler flags of the active configuration - $(CompileFlags)",
                "{linkflags}   linker flags of the active configuration - $(LinkFlags)",
                "{arch}        the target as the compilers take it: x86_64-windows | x86_64-linux | arm64-darwin | tms6747 - $(Arch); $(ArchFlag) is the tool's spelling of it",
                "{config}      the configuration's name; $(Config) is Debug or Release",
                "{name}        project name - $(ProjectName); $(Name) is the object name of the file being compiled",
                "{args}        the program's arguments (runners) - $(Arguments)",
                "{exe}         the program - $(Program)",
                "{projectdir} {outdir} {objdir} {asmdir} {map} - $(ProjectDir) $(OutDir) $(ObjDir)" };
            for each (String^ l in own) sb->Append(l)->Append("\r\n");
            ToolchainFile^ f = ToolchainFile::Current;
            if (f->PlaceholderOrder->Count > 0)
            {
                sb->Append("\r\ntoolchain.json's placeholders, as its placeholders section explains them (the ones a build fills in here are listed above; the others are empty until the revision that gives them a meaning):\r\n\r\n");
                for each (String^ k in f->PlaceholderOrder) sb->AppendFormat("{0,-24} {1}\r\n", k, f->Placeholder(k));
            }
            Ui::ShowText(this, "Placeholder Reference", sb->ToString());
        }

        void OnShortcutHelp(Object^ sender, EventArgs^ e)
        {
            Ui::Info(this,
                "Ctrl+Shift+N  New project          Ctrl+N  New file\n"
                "Ctrl+Shift+O  Open project         Ctrl+O  Open file\n"
                "Ctrl+S  Save    Ctrl+Shift+S  Save all    Ctrl+F4  Close file\n"
                "Ctrl+Z / Ctrl+Y  Undo / Redo\n"
                "Ctrl+F  Find    Ctrl+H  Replace    F3  Find next    Ctrl+G  Go to line\n"
                "Ctrl+Shift+F  Find in files    Ctrl+X / Ctrl+C / Ctrl+V / Ctrl+A  Cut / Copy / Paste / Select all\n"
                "Tab / Shift+Tab  Indent / outdent selected lines\n"
                "Ctrl+Alt+L  Solution Explorer    F4  Properties window    Ctrl+Alt+O  Output panel    Ctrl+Alt+E  Error list\n"
                "Ctrl+Shift+A  Add new file    Shift+Alt+A  Add existing file    Alt+F7  Project properties\n"
                "Ctrl+Shift+B  Build    Ctrl+F7  Compile file    Ctrl+Break  Cancel build\n"
                "F5  Build and run    Ctrl+F5  Run    Shift+F5  Stop\n"
                "F8  Start debugging / carry on    F9  Breakpoint    F7  Step over    F6  Step into\n"
                "Ctrl+Up / Ctrl+Down  While stopped: the frame that called this one / back down\n"
                "F1  Help    Alt+F4  Exit\n"
                "Middle-click a tab to close it. Double-click an error to jump to it.");
        }

        void OnAbout(Object^ sender, EventArgs^ e)
        {
            Ui::Info(this, String::Format(
                "{0}  (version {1})\n\n"
                "IDE for the C90, C++11 and Shalimar compilers.\n"
                "Targets: Windows (MASM/LINK), Linux, macOS, TMS6747 DSP (ASM6x/LNK6x/RTS6747/VM6747).\n"
                "Reads Code Composer Studio 7.4 / 5.5 projects and Visual Studio solutions (read-only).\n"
                "Debugs with GRIDE's debuggers: sim6747 and vm6747, cdb (or lldb), a Shalimar program's own session.\n"
                "Follows White's conventions; its Help is the one the three outlooks share, with the toolchain's manual (RIDE 5.17's).\n"
                "Its example is cppparse, built with GRIDE's own cpp11, masm and link.\n\n"
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
                System::Diagnostics::Stopwatch^ sw = System::Diagnostics::Stopwatch::StartNew();
                while (worker->IsBusy && sw->ElapsedMilliseconds < 8000)
                {
                    Application::DoEvents();
                    System::Threading::Thread::Sleep(50);
                }
                Enabled = true;
            }
            if (!CloseAllDocuments(nullptr)) { e->Cancel = true; closing = false; return; }
            if (debugger != nullptr) { debugger->Stop(); debugger = nullptr; }         // the debugger and its program go with the window
            // a program still running in the Output pane is stopped with the window
            if (ProgramBusy())
            {
                closing = true;
                if (verifyCts != nullptr) verifyCts->Cancel();
                runner->Stop();
                runner->WaitForExit(3000);
            }
            SaveLayout();
            if (project != nullptr) settings->LastProject = project->FilePath;
            SaveSettings();
            flushTimer->Stop();
        }
    };
}

#undef GRIDE_EH
