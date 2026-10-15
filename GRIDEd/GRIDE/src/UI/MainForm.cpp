// =============================================================================
//  MainForm.cpp - construction, layout, theme, status bar, keyboard chords
// =============================================================================
#include "../pch.h"
#include "MainForm.h"

namespace Gride { namespace UI {

    MainForm::MainForm(array<String^>^ args)
    {
        startArgs = args;
        String^ warning;
        settings = SettingsManager::Load(warning);
        workspace = nullptr;
        looseBreakpoints = gcnew Dictionary<String^, SortedSet<int>^>(StringComparer::OrdinalIgnoreCase);
        afterBuild = AfterBuild::Nothing;
        pendingChord = Keys::None;
        fullScreen = false;
        autoHidden = false;
        updatingCombos = false;
        checkingDisk = false;

        Theme^ theme = Theme::ByName(settings->Env->Theme);
        theme->ApplyOverrides(settings->Env->SyntaxColors);
        ThemeManager::Current = theme;

        engine = gcnew BuildEngine(settings);
        engine->Output += gcnew TextLineHandler(this, &MainForm::OnEngineOutput);
        engine->Message += gcnew BuildMessageHandler(this, &MainForm::OnEngineMessage);
        engine->Finished += gcnew BuildFinishedHandler(this, &MainForm::OnEngineFinished);

        // swap PlaceholderDebugger for the real VM6747 / native debugger later
        debugger = gcnew PlaceholderDebugger();
        debugger->StateChanged += gcnew DebugStateHandler(this, &MainForm::OnDebugState);
        debugger->Stopped += gcnew DebugStopHandler(this, &MainForm::OnDebugStopped);
        debugger->Output += gcnew TextLineHandler(this, &MainForm::OnDebugOutput);

        Text = "GRIDE";
        Icon = IconFactory::AppIcon();
        Font = gcnew SD::Font("Segoe UI", 9.0f);
        StartPosition = FormStartPosition::Manual;
        LayoutSettings^ l = settings->Layout;
        Bounds = SD::Rectangle(l->Left, l->Top, Math::Max(800, l->Width), Math::Max(560, l->Height));
        if (l->Maximized) WindowState = FormWindowState::Maximized;
        AllowDrop = true;
        DragEnter += gcnew DragEventHandler(this, &MainForm::OnDragEnter);
        DragDrop += gcnew DragEventHandler(this, &MainForm::OnDragDrop);

        commands = gcnew CommandRegistry();
        SuspendLayout();
        CreateLayout();
        CreatePanes();
        RegisterCommands();
        commands->ApplyBindings(settings->KeyBindings);
        BuildMenus();
        BuildToolbars();
        BuildContextMenus();
        ResumeLayout(true);

        findDialog = gcnew FindReplaceDialog();
        findDialog->Requested += gcnew FindRequestHandler(this, &MainForm::OnFindRequested);

        printDoc = gcnew System::Drawing::Printing::PrintDocument();
        printDoc->PrintPage += gcnew System::Drawing::Printing::PrintPageEventHandler(this, &MainForm::OnPrintPage);

        idleTimer = gcnew WF::Timer();
        idleTimer->Interval = 250;
        idleTimer->Tick += gcnew EventHandler(this, &MainForm::OnIdleTick);
        idleTimer->Start();

        ApplyTheme();
        UpdateCombos();
        UpdateStatus();
        UpdateTitle();
        if (warning != nullptr) output->Append("Tools", warning);
        output->Append("Tools", "Settings: " + settings->FilePath);
    }

    // =========================================================================
    //  layout
    // =========================================================================
    void MainForm::CreateLayout()
    {
        menu = gcnew MenuStrip();
        menu->Padding = WF::Padding(6, 2, 0, 2);
        MainMenuStrip = menu;

        toolbarHost = gcnew FlowLayoutPanel();
        toolbarHost->Dock = DockStyle::Top;
        toolbarHost->AutoSize = true;
        toolbarHost->WrapContents = true;
        toolbarHost->Padding = WF::Padding(4, 1, 4, 1);

        status = gcnew StatusStrip();
        status->SizingGrip = true;
        stMessage = gcnew ToolStripStatusLabel("Ready");
        stMessage->Spring = true;
        stMessage->TextAlign = ContentAlignment::MiddleLeft;
        stLine = gcnew ToolStripStatusLabel();
        stLang = gcnew ToolStripStatusLabel();
        stTarget = gcnew ToolStripStatusLabel();
        stConfig = gcnew ToolStripStatusLabel();
        stEncoding = gcnew ToolStripStatusLabel();
        stEol = gcnew ToolStripStatusLabel();
        status->Items->AddRange(gcnew array<ToolStripItem^>{ stMessage, stLine, stLang, stTarget, stConfig, stEncoding, stEol });
        for each (ToolStripItem^ i in status->Items) i->Margin = WF::Padding(8, 3, 8, 2);

        splitMain = gcnew SplitContainer();
        splitMain->Size = SD::Size(1400, 800);
        splitMain->Dock = DockStyle::Fill;
        splitMain->FixedPanel = FixedPanel::Panel1;
        splitMain->SplitterWidth = 4;
        splitMain->SplitterDistance = 270;

        splitRight = gcnew SplitContainer();
        splitRight->Size = SD::Size(1120, 800);
        splitRight->Dock = DockStyle::Fill;
        splitRight->FixedPanel = FixedPanel::Panel2;
        splitRight->SplitterWidth = 4;
        splitRight->SplitterDistance = 850;
        splitMain->Panel2->Controls->Add(splitRight);

        splitCenter = gcnew SplitContainer();
        splitCenter->Size = SD::Size(850, 800);
        splitCenter->Dock = DockStyle::Fill;
        splitCenter->Orientation = WF::Orientation::Horizontal;
        splitCenter->FixedPanel = FixedPanel::Panel2;
        splitCenter->SplitterWidth = 4;
        splitCenter->SplitterDistance = 560;
        splitRight->Panel1->Controls->Add(splitCenter);

        docs = gcnew DocumentManager();
        docs->Dock = DockStyle::Fill;
        docs->AppSettings = settings;
        docs->ActiveChanged += gcnew EventHandler(this, &MainForm::OnDocumentActivated);
        docs->CaretMoved += gcnew EventHandler(this, &MainForm::OnDocumentCaret);
        docs->DirtyChanged += gcnew EventHandler(this, &MainForm::OnDocumentDirty);
        docs->BreakpointToggled += gcnew DocumentLineHandler(this, &MainForm::OnBreakpointToggled);
        docs->Start->CommandRequested += gcnew PathHandler(this, &MainForm::OnStartPageCommand);
        docs->Start->RecentRequested += gcnew PathHandler(this, &MainForm::OnStartPageRecent);
        splitCenter->Panel1->Controls->Add(docs);

        leftArea = gcnew DockArea("left");
        rightArea = gcnew DockArea("right");
        bottomArea = gcnew DockArea("bottom");
        for each (DockArea^ a in gcnew array<DockArea^>{ leftArea, rightArea, bottomArea })
        {
            a->Dock = DockStyle::Fill;
            a->VisibleCountChanged += gcnew EventHandler(this, &MainForm::OnAreaVisibilityChanged);
        }
        splitMain->Panel1->Controls->Add(leftArea);
        splitRight->Panel2->Controls->Add(rightArea);
        splitCenter->Panel2->Controls->Add(bottomArea);

        // docking order: Fill first, then top bars, then the status bar
        Controls->Add(splitMain);
        Controls->Add(toolbarHost);
        Controls->Add(menu);
        Controls->Add(status);
    }

    void MainForm::CreatePanes()
    {
        allPanes = gcnew List<ToolPane^>();
        debugPanes = gcnew List<DebugPaneBase^>();

        explorer = gcnew ProjectExplorerPane();
        explorer->FileActivated += gcnew FileActivatedHandler(this, &MainForm::OnExplorerFileActivated);
        explorer->SelectionChanged += gcnew EventHandler(this, &MainForm::OnExplorerSelection);
        explorer->RenameRequested += gcnew RenameHandler(this, &MainForm::OnRenameRequested);
        classView = gcnew ClassViewPane();
        classView->Navigate += gcnew NavigateHandler(this, &MainForm::OnNavigate);
        filesPane = gcnew FilesPane();
        filesPane->FileActivated += gcnew FileActivatedHandler(this, &MainForm::OnExplorerFileActivated);
        leftArea->AddPane(explorer, false);
        leftArea->AddPane(classView, false);
        leftArea->AddPane(filesPane, false);
        leftArea->ShowPane(explorer);

        properties = gcnew PropertiesPane();
        bpPane = gcnew BreakpointsPane();
        bpPane->Navigate += gcnew NavigateHandler(this, &MainForm::OnNavigate);
        bpPane->RemoveRequested += gcnew NavigateHandler(this, &MainForm::OnBreakpointRemove);
        rightArea->AddPane(properties, false);
        rightArea->AddPane(bpPane, false);
        rightArea->ShowPane(properties);

        output = gcnew OutputPane();
        output->Navigate += gcnew NavigateHandler(this, &MainForm::OnNavigate);
        errorList = gcnew ErrorListPane();
        errorList->Navigate += gcnew NavigateHandler(this, &MainForm::OnNavigate);
        findResults = gcnew FindResultsPane();
        findResults->Navigate += gcnew NavigateHandler(this, &MainForm::OnNavigate);
        terminal = gcnew TerminalPane();
        watch = gcnew WatchPane();
        locals = gcnew LocalsPane();
        callStack = gcnew CallStackPane();
        callStack->Navigate += gcnew NavigateHandler(this, &MainForm::OnNavigate);
        registers = gcnew RegistersPane();
        memory = gcnew MemoryPane();
        disasm = gcnew DisassemblyPane();
        bottomArea->AddPane(output, false);
        bottomArea->AddPane(errorList, false);
        bottomArea->AddPane(findResults, false);
        bottomArea->AddPane(watch, false);
        bottomArea->AddPane(locals, true);
        bottomArea->AddPane(callStack, true);
        bottomArea->AddPane(registers, false);
        bottomArea->AddPane(memory, false);
        bottomArea->AddPane(disasm, true);
        bottomArea->AddPane(terminal, false);
        bottomArea->ShowPane(output);

        for each (DockArea^ a in gcnew array<DockArea^>{ leftArea, rightArea, bottomArea })
            allPanes->AddRange(a->Panes);
        debugPanes->AddRange(gcnew array<DebugPaneBase^>{ watch, locals, callStack, registers, memory, disasm });

        for each (ToolPane^ p in allPanes)
            if (settings->Layout->HiddenPanes->Contains(p->Key)) p->Home->HidePane(p);
        terminal->ExtraPath = ToolchainPath();
    }

    void MainForm::ApplyLayoutSizes()
    {
        LayoutSettings^ l = settings->Layout;
        try
        {
            splitMain->SplitterDistance = Math::Max(120, Math::Min(l->LeftWidth, splitMain->Width - 300));
            splitRight->SplitterDistance = Math::Max(300, splitRight->Width - l->RightWidth);
            splitCenter->SplitterDistance = Math::Max(150, splitCenter->Height - l->BottomHeight);
        }
        catch (Exception^) { }
        OnAreaVisibilityChanged(nullptr, EventArgs::Empty);
        tbStandard->Visible = !l->HiddenToolbars->Contains("Standard");
        tbBuild->Visible = !l->HiddenToolbars->Contains("Build");
        tbDebug->Visible = !l->HiddenToolbars->Contains("Debug");
        tbEditor->Visible = !l->HiddenToolbars->Contains("Text Editor");
        status->Visible = l->StatusBar;
    }

    void MainForm::StoreLayout()
    {
        LayoutSettings^ l = settings->Layout;
        l->Maximized = WindowState == FormWindowState::Maximized;
        SD::Rectangle b = WindowState == FormWindowState::Normal ? Bounds : RestoreBounds;
        l->Left = b.Left; l->Top = b.Top; l->Width = b.Width; l->Height = b.Height;
        if (!splitMain->Panel1Collapsed) l->LeftWidth = splitMain->SplitterDistance;
        if (!splitRight->Panel2Collapsed) l->RightWidth = splitRight->Width - splitRight->SplitterDistance;
        if (!splitCenter->Panel2Collapsed) l->BottomHeight = splitCenter->Height - splitCenter->SplitterDistance;
        l->HiddenPanes->Clear();
        for each (ToolPane^ p in allPanes)
            if (!p->Home->IsShown(p) && p != locals && p != callStack && p != disasm) l->HiddenPanes->Add(p->Key);
        l->HiddenToolbars->Clear();
        if (!tbStandard->Visible) l->HiddenToolbars->Add("Standard");
        if (!tbBuild->Visible) l->HiddenToolbars->Add("Build");
        if (!tbDebug->Visible) l->HiddenToolbars->Add("Debug");
        if (!tbEditor->Visible) l->HiddenToolbars->Add("Text Editor");
        l->StatusBar = status->Visible;
    }

    void MainForm::ResetLayout()
    {
        LayoutSettings^ fresh = gcnew LayoutSettings();
        settings->Layout->LeftWidth = fresh->LeftWidth;
        settings->Layout->RightWidth = fresh->RightWidth;
        settings->Layout->BottomHeight = fresh->BottomHeight;
        settings->Layout->HiddenPanes->Clear();
        settings->Layout->HiddenToolbars = fresh->HiddenToolbars;
        settings->Layout->StatusBar = true;
        for each (DockArea^ a in gcnew array<DockArea^>{ leftArea, rightArea, bottomArea }) a->DockAll();
        for each (ToolPane^ p in allPanes)
        {
            if (p == locals || p == callStack || p == disasm) p->Home->HidePane(p);
            else p->Home->ShowPane(p);
        }
        leftArea->ShowPane(explorer);
        rightArea->ShowPane(properties);
        bottomArea->ShowPane(output);
        autoHidden = false;
        ApplyLayoutSizes();
    }

    void MainForm::OnAreaVisibilityChanged(Object^ sender, EventArgs^ e)
    {
        if (autoHidden) return;
        splitMain->Panel1Collapsed = !leftArea->HasVisiblePanes;
        splitRight->Panel2Collapsed = !rightArea->HasVisiblePanes;
        splitCenter->Panel2Collapsed = !bottomArea->HasVisiblePanes;
    }

    // =========================================================================
    //  theme / settings
    // =========================================================================
    void MainForm::ApplyTheme()
    {
        Theme^ t = Theme::ByName(settings->Env->Theme);
        t->ApplyOverrides(settings->Env->SyntaxColors);
        ThemeManager::Current = t;
        IconFactory::Clear();
        ThemeManager::Apply(this);
        commands->UpdateItems(menu->Items);
        for each (ToolStrip^ ts in gcnew array<ToolStrip^>{ tbStandard, tbBuild, tbDebug, tbEditor })
        {
            commands->UpdateItems(ts->Items);
            ThemeManager::Apply(ts);
        }
        splitMain->BackColor = t->Border;
        splitRight->BackColor = t->Border;
        splitCenter->BackColor = t->Border;
        toolbarHost->BackColor = t->Chrome;
        menu->BackColor = t->Chrome;
        leftArea->ApplyTheme();
        rightArea->ApplyTheme();
        bottomArea->ApplyTheme();
        docs->ApplyTheme();
        docs->Start->Build(settings->Recent->Workspaces);
        if (findDialog != nullptr) ThemeManager::Apply(findDialog);
        Invalidate(true);
    }

    void MainForm::ApplySettings()
    {
        engine->CurrentSettings = settings;
        docs->AppSettings = settings;
        commands->ApplyBindings(settings->KeyBindings);
        ApplyTheme();
        docs->ApplySettings();
        terminal->ExtraPath = ToolchainPath();
        UpdateStatus();
    }

    void MainForm::SaveSettings()
    {
        StoreLayout();
        settings->LastWorkspace = (workspace != nullptr && !workspace->IsTemporary) ? workspace->FilePath : String::Empty;
        SettingsManager::Save(settings);
    }

    // =========================================================================
    //  status, title, combos
    // =========================================================================
    void MainForm::SetStatus(String^ text) { stMessage->Text = text; }

    void MainForm::UpdateTitle()
    {
        String^ t = "GRIDE";
        if (workspace != nullptr) t = workspace->Name + " - " + t;
        EditorDocument^ d = docs->Active;
        if (d != nullptr)
        {
            String^ mark = String::Empty;
            if (d->IsDirty) mark = " *";
            t = d->Title + mark + " - " + t;
        }
        if (debugger->IsActive) t = t + "  (Debugging)";
        if (Text != t) Text = t;
    }

    void MainForm::UpdateStatus()
    {
        EditorDocument^ d = docs->Active;
        if (d != nullptr)
        {
            stLine->Text = String::Format("Ln {0}, Col {1}", d->Editor->CurrentLine, d->Editor->CurrentColumn);
            stLang->Text = LanguageInfo::Name(d->Lang);
            stEncoding->Text = d->EncodingName;
            stEol->Text = d->LineEnding;
        }
        else
        {
            stLine->Text = String::Empty;
            stLang->Text = safe_cast<String^>(cbLanguage->SelectedItem);
            stEncoding->Text = String::Empty;
            stEol->Text = String::Empty;
        }
        stTarget->Text = TargetInfo::Name(CurrentTarget());
        stConfig->Text = CurrentConfig();
        if (btnStart != nullptr)
        {
            String^ s = "Start";
            if (CurrentTarget() == Target::C6747) s = "Start (VM6747)";
            if (IsInBreak()) s = "Continue";
            if (btnStart->Text != s) btnStart->Text = s;
        }
    }

    void MainForm::UpdateCombos()
    {
        updatingCombos = true;
        if (workspace != nullptr)
        {
            cbConfig->SelectedItem = workspace->ActiveConfig;
            cbTarget->SelectedIndex = (int)workspace->ActiveTarget;
        }
        EditorDocument^ d = docs->Active;
        if (d != nullptr && LanguageInfo::IsCompiled(d->Lang)) cbLanguage->SelectedIndex = (int)d->Lang;
        updatingCombos = false;
    }

    // refresh Enabled/Checked of every command item, menus included, so that
    // shortcuts of items that just became available work immediately
    void MainForm::RefreshMenuTree(ToolStripItemCollection^ items)
    {
        commands->Refresh(items);
        for each (ToolStripItem^ it in items)
        {
            ToolStripMenuItem^ mi = dynamic_cast<ToolStripMenuItem^>(it);
            if (mi != nullptr && mi->HasDropDownItems) RefreshMenuTree(mi->DropDownItems);
        }
    }

    void MainForm::OnIdleTick(Object^ sender, EventArgs^ e)
    {
        RefreshMenuTree(menu->Items);
        for each (ToolStrip^ ts in gcnew array<ToolStrip^>{ tbStandard, tbBuild, tbDebug, tbEditor })
            if (ts->Visible) commands->Refresh(ts->Items);
        cbConfig->Enabled = !engine->IsBuilding;
        cbTarget->Enabled = !engine->IsBuilding;
        UpdateTitle();
        if (btnStart != nullptr) UpdateStatus();
    }

    // =========================================================================
    //  form events
    // =========================================================================
    void MainForm::OnShown(EventArgs^ e)
    {
        Form::OnShown(e);
        ApplyLayoutSizes();
        bool opened = false;
        if (startArgs != nullptr)
            for each (String^ a in startArgs)
                if (File::Exists(a) || Directory::Exists(a)) { OpenPath(a); opened = true; }
        if (!opened && settings->Env->ReopenLastWorkspace && File::Exists(settings->LastWorkspace))
        {
            try { OpenPath(settings->LastWorkspace); }
            catch (Exception^ ex) { output->Append("Tools", "Could not reopen last workspace: " + ex->Message); }
        }
        SetStatus("Ready");
    }

    void MainForm::OnFormClosing(FormClosingEventArgs^ e)
    {
        if (engine->IsBuilding)
        {
            if (MessageBox::Show(this, "A build is running. Stop it and exit?", "GRIDE", MessageBoxButtons::YesNo, MessageBoxIcon::Question)
                != WF::DialogResult::Yes) { e->Cancel = true; return; }
            engine->Cancel();
        }
        if (!docs->CloseAll(nullptr)) { e->Cancel = true; return; }
        if (debugger->IsActive) debugger->Stop();
        SaveWorkspace();
        SaveSettings();
        terminal->StopShell();
        idleTimer->Stop();
        Form::OnFormClosing(e);
    }

    void MainForm::OnActivated(EventArgs^ e)
    {
        Form::OnActivated(e);
        CheckFilesChangedOnDisk();
    }

    void MainForm::CheckFilesChangedOnDisk()
    {
        if (checkingDisk || docs == nullptr) return;
        checkingDisk = true;
        for each (EditorDocument^ d in gcnew List<EditorDocument^>(docs->Documents))
        {
            if (!d->ChangedOnDisk()) continue;
            String^ q = d->FilePath + "\n\nThis file has been changed outside GRIDE.";
            if (d->IsDirty) q = q + "\nYou also have unsaved changes - reloading will discard them.";
            q = q + "\n\nReload it?";
            if (MessageBox::Show(this, q, "GRIDE", MessageBoxButtons::YesNo, MessageBoxIcon::Question) == WF::DialogResult::Yes)
                d->Reload();
        }
        checkingDisk = false;
    }

    void MainForm::OnDragEnter(Object^ sender, DragEventArgs^ e)
    {
        if (e->Data->GetDataPresent(DataFormats::FileDrop)) e->Effect = DragDropEffects::Copy;
    }

    void MainForm::OnDragDrop(Object^ sender, DragEventArgs^ e)
    {
        array<String^>^ files = safe_cast<array<String^>^>(e->Data->GetData(DataFormats::FileDrop));
        for each (String^ f in files) OpenPath(f);
    }

    // =========================================================================
    //  keyboard: two-key chords (Ctrl+K,C ...) like Visual Studio
    // =========================================================================
    bool MainForm::ProcessCmdKey(WF::Message% msg, Keys keyData)
    {
        Keys code = keyData & Keys::KeyCode;
        bool modifierOnly = code == Keys::ControlKey || code == Keys::ShiftKey || code == Keys::Menu;
        if (pendingChord != Keys::None && !modifierOnly)
        {
            Keys first = pendingChord;
            pendingChord = Keys::None;
            Command^ c = commands->FindChord(first, keyData);
            if (c != nullptr) { SetStatus("Ready"); c->Execute(); return true; }
            SetStatus(String::Format("The key combination ({0}, {1}) is not a command.", KeyText::Format(first), KeyText::Format(keyData)));
            return true;
        }
        if (!modifierOnly && commands->IsChordStart(keyData))
        {
            pendingChord = keyData;
            SetStatus(String::Format("({0}) was pressed. Waiting for second key of chord...", KeyText::Format(keyData)));
            return true;
        }
        return Form::ProcessCmdKey(msg, keyData);
    }

    // breakpoints live in the workspace (saved in the .grws) or in a loose map
    Dictionary<String^, SortedSet<int>^>^ MainForm::BreakpointMap()
    {
        return workspace != nullptr ? workspace->Breakpoints : looseBreakpoints;
    }

    // folders of every configured tool, prepended to PATH for terminals
    String^ MainForm::ToolchainPath()
    {
        List<String^>^ dirs = gcnew List<String^>();
        for each (ToolchainSettings^ tc in settings->Toolchains)
        {
            String^ d = String::Empty;
            if (!String::IsNullOrEmpty(tc->ToolDir)) d = Environment::ExpandEnvironmentVariables(tc->ToolDir);
            if (!String::IsNullOrEmpty(d) && Directory::Exists(d) && !dirs->Contains(d)) dirs->Add(d);
            for each (String^ tool in gcnew array<String^>{ tc->Assembler, tc->Linker })
            {
                String^ full = tc->Resolve(tool);
                if (String::IsNullOrEmpty(full)) continue;
                String^ fd = Path::GetDirectoryName(full);
                if (!String::IsNullOrEmpty(fd) && Directory::Exists(fd) && !dirs->Contains(fd)) dirs->Add(fd);
            }
        }
        return String::Join(";", dirs);
    }
}}
