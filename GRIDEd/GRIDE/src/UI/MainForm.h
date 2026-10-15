// =============================================================================
//  MainForm.h - the GRIDE main window
//
//  +--------------------------------------------------------------------+
//  | Menu: File Edit View Project Build Debug Tools Window Help         |
//  | Toolbars: Standard | Build | Debug | Text Editor                   |
//  +-----------+------------------------------------+-------------------+
//  | leftArea  |  DocumentManager (tabbed editors)  |  rightArea        |
//  | Explorer  |                                    |  Properties       |
//  | ClassView +------------------------------------+  Breakpoints      |
//  | Files     |  bottomArea: Output, Error List,   |                   |
//  |           |  Find Results, Watch, Registers... |                   |
//  +-----------+------------------------------------+-------------------+
//  | Status bar                                                         |
//  +--------------------------------------------------------------------+
//
//  The implementation is split by menu:
//    MainForm.cpp           construction, layout, theme, status, keys
//    MainForm.Commands.cpp  command table, menus, toolbars, context menus
//    MainForm.File.cpp      File + Project menus (workspace, projects, files)
//    MainForm.Edit.cpp      Edit + View + Window + Help menus
//    MainForm.Build.cpp     Build + Debug + Tools menus
// =============================================================================
#pragma once
#include "Commands.h"
#include "PropertyObjects.h"
#include "Editor/DocumentManager.h"
#include "Docking/DockArea.h"
#include "Panes/ExplorerPanes.h"
#include "Panes/OutputPanes.h"
#include "Panes/DebugPanes.h"
#include "Dialogs/DialogBase.h"
#include "Dialogs/ProjectDialogs.h"
#include "Dialogs/OptionsDialog.h"
#include "Dialogs/EditDialogs.h"
#include "Dialogs/ToolDialogs.h"
#include "../Services/BuildEngine.h"
#include "../Services/ProgramRunner.h"
#include "../Services/Debugger.h"
#include "../Services/GdbDebugger.h"
#include "../Services/ShalimarSession.h"
#include "../Services/CodeConverter.h"
#include "../Services/ProjectTemplates.h"

namespace Gride { namespace UI {

    using namespace Gride::Core;
    using namespace Gride::Model;
    using namespace Gride::Settings;
    using namespace Gride::Services;

    // ---- small helpers that let one command object drive a pane / toolbar ----
    public ref class PaneToggle
    {
    public:
        PaneToggle(ToolPane^ p, bool showOnly) { pane = p; this->showOnly = showOnly; }
        void Run()
        {
            DockArea^ a = pane->Home;
            if (!showOnly && a->IsShown(pane) && a->ActivePane == pane && !pane->IsFloating) a->HidePane(pane);
            else a->ShowPane(pane);
        }
        bool IsShown() { return pane->Home->IsShown(pane); }
    private:
        ToolPane^ pane;
        bool showOnly;
    };

    public ref class ToolbarToggle
    {
    public:
        ToolbarToggle(ToolStrip^ t) { bar = t; }
        void Run() { bar->Visible = !bar->Visible; }
        bool IsShown() { return bar->Visible; }
    private:
        ToolStrip^ bar;
    };

    // a command that passes a fixed value to a handler (themes, help pages ...)
    public ref class ValueCommand
    {
    public:
        ValueCommand(String^ v, Action<String^>^ run, Func<String^>^ current) { value = v; runner = run; getter = current; }
        void Run() { runner->Invoke(value); }
        bool IsCurrent() { return getter != nullptr && String::Compare(getter->Invoke(), value, true) == 0; }
    private:
        String^ value;
        Action<String^>^ runner;
        Func<String^>^ getter;
    };

    // what a build was started for: nothing more, a run, the debugger, RIDE's Emulate on vm6747, RIDE's Verify
    public enum class AfterBuild { Nothing, Run, Debug, Emulate, Verify };

    // =========================================================================
    public ref class MainForm : public Form
    {
    public:
        MainForm(array<String^>^ args);

    protected:
        virtual void OnShown(EventArgs^ e) override;
        virtual void OnFormClosing(FormClosingEventArgs^ e) override;
        virtual void OnActivated(EventArgs^ e) override;
        virtual bool ProcessCmdKey(WF::Message% msg, Keys keyData) override;

    public:
        // a running program's lines and its end (worker threads; also the Verify job's way in)
        void OnRunOutput(String^ line);
        void OnRunExited(int status, bool stopped);

    private:
        // ---------------------------------------------------------------- state
        array<String^>^ startArgs;
        GrideSettings^ settings;
        Workspace^ workspace;
        Dictionary<String^, SortedSet<int>^>^ looseBreakpoints;   // used when no workspace is open
        BuildEngine^ engine;
        ProgramRunner^ runner;          // the program of Start Without Debugging / Emulate, running in the Output pane
        DebugEngine^ debugger;
        CommandRegistry^ commands;
        AfterBuild afterBuild;
        Keys pendingChord;
        bool fullScreen;
        FormWindowState savedState;
        bool autoHidden;
        bool updatingCombos;
        bool checkingDisk;

        // ---------------------------------------------------------------- layout
        MenuStrip^ menu;
        FlowLayoutPanel^ toolbarHost;
        ToolStrip^ tbStandard;
        ToolStrip^ tbBuild;
        ToolStrip^ tbDebug;
        ToolStrip^ tbEditor;
        ToolStripComboBox^ cbConfig;
        ToolStripComboBox^ cbTarget;
        ToolStripComboBox^ cbLanguage;
        ToolStripButton^ btnStart;
        StatusStrip^ status;
        ToolStripStatusLabel^ stMessage;
        ToolStripStatusLabel^ stLine;
        ToolStripStatusLabel^ stLang;
        ToolStripStatusLabel^ stTarget;
        ToolStripStatusLabel^ stConfig;
        ToolStripStatusLabel^ stEncoding;
        ToolStripStatusLabel^ stEol;
        SplitContainer^ splitMain;      // left | rest
        SplitContainer^ splitRight;     // center | right
        SplitContainer^ splitCenter;    // documents / bottom
        DockArea^ leftArea;
        DockArea^ rightArea;
        DockArea^ bottomArea;
        DocumentManager^ docs;
        WF::Timer^ idleTimer;

        // ---------------------------------------------------------------- panes
        ProjectExplorerPane^ explorer;
        ClassViewPane^ classView;
        FilesPane^ filesPane;
        PropertiesPane^ properties;
        BreakpointsPane^ bpPane;
        OutputPane^ output;
        ErrorListPane^ errorList;
        FindResultsPane^ findResults;
        TerminalPane^ terminal;
        WatchPane^ watch;
        LocalsPane^ locals;
        CallStackPane^ callStack;
        RegistersPane^ registers;
        MemoryPane^ memory;
        DisassemblyPane^ disasm;
        List<ToolPane^>^ allPanes;
        List<DebugPaneBase^>^ debugPanes;
        FindReplaceDialog^ findDialog;

        // dynamic menus
        ToolStripMenuItem^ miRecentFiles;
        ToolStripMenuItem^ miRecentWorkspaces;
        ToolStripMenuItem^ miWindow;
        ToolStripMenuItem^ miTools;
        ToolStripMenuItem^ miBuild;

        // printing
        System::Drawing::Printing::PrintDocument^ printDoc;
        array<String^>^ printLines;
        int printIndex;
        int printPage;

        // ======================================================= MainForm.cpp
        void CreateLayout();
        void CreatePanes();
        void RestoreWindowBounds();
        void ApplyLayoutSizes();
        void ResetLayout();
        void StoreLayout();
        void ApplyTheme();
        void ApplySettings();
        void SaveSettings();
        void UpdateTitle();
        void UpdateStatus();
        void UpdateCombos();
        void SetStatus(String^ text);
        void RefreshMenuTree(ToolStripItemCollection^ items);
        void OnIdleTick(Object^ sender, EventArgs^ e);
        void OnAreaVisibilityChanged(Object^ sender, EventArgs^ e);
        void OnDragEnter(Object^ sender, DragEventArgs^ e);
        void OnDragDrop(Object^ sender, DragEventArgs^ e);
        void CheckFilesChangedOnDisk();
        Dictionary<String^, SortedSet<int>^>^ BreakpointMap();
        Command^ FocusCommand(Keys keyData, array<String^>^ ids);

        // ============================================== MainForm.Commands.cpp
        void RegisterCommands();
        void BuildMenus();
        void BuildToolbars();
        void BuildContextMenus();
        System::Windows::Forms::ContextMenuStrip^ MakeEditorMenu();
        ToolStripMenuItem^ TopMenu(String^ text);
        ToolStripMenuItem^ SubMenu(ToolStripMenuItem^ parent, String^ text);
        void AddItems(ToolStripMenuItem^ parent, array<String^>^ ids);
        System::Windows::Forms::ContextMenuStrip^ MakeContextMenu(array<String^>^ ids);
        Command^ Cmd(String^ id, String^ text, String^ keys, String^ icon, Action^ handler);
        void OnMenuOpening(Object^ sender, EventArgs^ e);
        void OnContextOpening(Object^ sender, System::ComponentModel::CancelEventArgs^ e);
        void OnFileMenuOpening(Object^ sender, EventArgs^ e);
        void OnWindowMenuOpening(Object^ sender, EventArgs^ e);
        void OnToolsMenuOpening(Object^ sender, EventArgs^ e);
        void OnBuildMenuOpening(Object^ sender, EventArgs^ e);
        void OnRecentFileClick(Object^ sender, EventArgs^ e);
        void OnRecentWorkspaceClick(Object^ sender, EventArgs^ e);
        void OnWindowDocClick(Object^ sender, EventArgs^ e);
        void OnExternalToolClick(Object^ sender, EventArgs^ e);
        void OnConfigChanged(Object^ sender, EventArgs^ e);
        void OnTargetChanged(Object^ sender, EventArgs^ e);
        void OnLanguageChanged(Object^ sender, EventArgs^ e);
        // CanExecute tests
        bool HasWorkspace();
        bool HasDocument();
        bool HasProject();
        bool HasWritableProject();
        bool HasSelectedFile();
        bool CanBuild();
        bool IsBuilding();
        bool IsDebugging();
        bool IsInBreak();
        bool CanStartDebug();
        bool IsLineNumbers();
        bool IsWhitespace();
        bool IsStatusBar();
        bool IsSplit();
        bool IsFullScreen();
        bool IsSelectedExcluded();
        String^ CurrentTheme();

        // ================================================== MainForm.File.cpp
        GrideProject^ ContextProject();
        void OpenPath(String^ path);
        void SetWorkspace(Workspace^ w);
        bool CloseWorkspace();
        void SaveWorkspace();
        EditorDocument^ OpenDocument(String^ path, int line);
        void AddRecentFile(String^ path);
        void AddRecentWorkspace(String^ path);
        void RefreshProjectViews();
        void OnExplorerFileActivated(String^ file, GrideProject^ project);
        void OnExplorerSelection(Object^ sender, EventArgs^ e);
        void OnRenameRequested(GrideProject^ project, String^ oldPath, String^ newName);
        void OnExplorerRemove(Object^ sender, EventArgs^ e);
        void OnNavigate(String^ file, int line);
        void OnNavigateTo(String^ file, int line, int column);
        void OnStartPageCommand(String^ id);
        void OnStartPageRecent(String^ path);
        void OnDocumentActivated(Object^ sender, EventArgs^ e);
        void OnDocumentCaret(Object^ sender, EventArgs^ e);
        void OnDocumentDirty(Object^ sender, EventArgs^ e);
        void OnPrintPage(Object^ sender, System::Drawing::Printing::PrintPageEventArgs^ e);
        // File menu
        void FileNewProject();
        void FileNewWorkspace();
        void FileNewFile();
        void FileOpenWorkspace();
        void FileOpenFile();
        void FileOpenCcs();
        void FileClose();
        void FileCloseWorkspace();
        void FileSave();
        void FileSaveAs();
        void FileSaveAll();
        void FileImportFolder();
        void FileExportZip();
        void FilePageSetup();
        void FilePrint();
        void FileExit();
        void FileOpenSelected();
        // Project menu
        void ProjectAddNewItem();
        void ProjectAddExisting();
        void ProjectNewFolder();
        void ProjectRemoveItem();
        void ProjectRename();
        void ProjectExclude();
        void ProjectAddProject();
        void ProjectRemoveProject();
        void ProjectSetStartup();
        void ProjectDependencies();
        void ProjectBuildOrder();
        void ProjectRefreshCcs();
        void ProjectCcsMapping();
        void ReportCcsProjects();
        bool HasCcsProject();
        bool HasImportedProject();
        void ApplyCcsStates();
        void RememberCcsState(GrideProject^ p);
        void KeepImportedState(GrideProject^ p);
        bool CcsEnabled();
        void ProjectOpenFolder();
        void ProjectShowProperties();
        void ProjectCompilerOptions();
        void ShowProjectProperties(String^ tab);

        // ================================================== MainForm.Edit.cpp
        TextBoxBase^ FocusedTextBox();
        CodeEditor^ ActiveEditor();
        void EditUndo();
        void EditRedo();
        void EditCut();
        void EditCopy();
        void EditPaste();
        void EditDelete();
        void EditSelectAll();
        void EditFind();
        void EditReplace();
        void EditFindInFiles();
        void EditGoToLine();
        void EditComment();
        void EditUncomment();
        void EditFormat();
        void EditUpper();
        void EditLower();
        void EditTabify();
        void EditIndent();
        void EditOutdent();
        void EditToggleBookmark();
        void EditNextBookmark();
        void EditPrevBookmark();
        void EditClearBookmarks();
        void OnFindRequested(FindRequest^ r);
        List<String^>^ FilesInScope(FindScope scope);
        // View
        void ViewStatusBar();
        void ViewFullScreen();
        void ViewZoomIn();
        void ViewZoomOut();
        void ViewZoomReset();
        void ViewLineNumbers();
        void ViewWhitespace();
        void SetTheme(String^ name);
        // Window
        void WindowSplit();
        void WindowFloat();
        void WindowDock();
        void WindowAutoHide();
        void WindowPinTab();
        void WindowCloseAll();
        void WindowCloseAllButThis();
        void WindowResetLayout();
        void WindowSaveLayout();
        void WindowList();
        // Help
        void HelpView();
        void HelpUserGuide();
        void OpenHelpFile(String^ baseName);
        List<String^>^ HelpFolders();
        String^ FindHelpFile(String^ baseName, array<String^>^ extensions);
        void HelpShortcuts();
        void HelpReleaseNotes();
        void HelpAbout();

        // ================================================= MainForm.Build.cpp
        bool StartBuild(BuildAction action, List<GrideProject^>^ projects, String^ singleFile);
        void BuildWorkspace();
        void RebuildWorkspace();
        void CleanWorkspace();
        void BuildProject();
        void RebuildProject();
        void CleanProject();
        void CompileFile();
        void AssembleOnly();
        void LinkOnly();
        void BatchBuild();
        void ConfigurationManager();
        void CancelBuild();
        void OnEngineOutput(String^ line);
        void OnEngineMessage(BuildMessage^ m);
        void OnEngineFinished(BuildSummary^ s);
        void AppendBuildOutput(String^ line);
        void AddBuildMessage(BuildMessage^ m);
        void BuildFinishedUI(BuildSummary^ s);
        bool OnEngineQuestion(String^ question);
        bool AskYesNo(String^ question);
        String^ CurrentConfig();
        Target CurrentTarget();
        // Debug
        void DebugStart();
        void DebugStartNoDebug();
        void DebugStop();
        void DebugRestart();
        void DebugBreakAll();
        void DebugStepInto();
        void DebugStepOver();
        void DebugStepOut();
        void DebugRunToCursor();
        void DebugUpStack();
        void DebugDownStack();
        void DebugAddWatch();
        void DebugSetVariable();
        void LookAtFrame(int index);
        void OnFrameChosen(int index);
        void OnVariableSet(String^ name, String^ value);
        void ApplyKeySet();
        String^ KeysLine();
        void DebugToggleBreakpoint();
        void DebugDeleteAllBreakpoints();
        void DebugConnect();
        void DebugLoadProgram();
        void LaunchDebugger(String^ program);
        void AttachDebugger(DebugEngine^ engine);
        String^ AssemblyProgramFor(GrideProject^ p, BuildConfiguration^ cfg, ToolchainSettings^ tc);
        void OnDebugProgramOutput(String^ line);
        void RunProgram();
        void RunChosen(AfterBuild how);
        void RunExternal(GrideProject^ p, ToolchainSettings^ tc, String^ program);
        void DebugEmulate();
        void DebugVerify();
        void StopProgram();
        bool IsRunning();
        void RunExitedUI(Object^ status, Object^ stopped);
        void OnOutputInput(String^ line);
        bool FindEntryPoint(GrideProject^ p, String^% file, int% line);
        void OnDebugState(DebugState s);
        void OnDebugStopped(StopInfo^ info);
        void OnDebugOutput(String^ line);
        void DebugStateUI(Object^ state);
        void DebugStoppedUI(StopInfo^ info);
        void RefreshDebugPanes();
        void ClearExecutionMarkers();
        void OnBreakpointToggled(EditorDocument^ doc, int line);
        void OnBreakpointRemove(String^ file, int line);
        void RefreshBreakpointsPane();
        // Tools
        void ToolsConvert(ConvertDirection d);
        void ToolsCToShalimar();
        void ToolsShalimarToC();
        void ToolsToolchainCheck();
        void ToolsCommandPrompt();
        void ToolsExternalTools();
        void RunExternalTool(ExternalTool^ t);
        void ToolsSnippets();
        void ToolsKeyboard();
        void ToolsOptions();
        void ShowOptions(String^ page);
        String^ ToolchainPath();
    };
}}
