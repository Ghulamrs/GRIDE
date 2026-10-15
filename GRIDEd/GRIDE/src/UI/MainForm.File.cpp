// =============================================================================
//  MainForm.File.cpp - File and Project menus: workspaces, projects, files
// =============================================================================
#include "../pch.h"
#include "MainForm.h"

namespace Gride { namespace UI {

    using namespace System::IO::Compression;

    // the project the Project / Build commands act on:
    // Project Explorer selection, else the active document's project, else the startup project
    GrideProject^ MainForm::ContextProject()
    {
        if (workspace == nullptr) return nullptr;
        GrideProject^ p = explorer->SelectedProject;
        if (p != nullptr) return p;
        EditorDocument^ d = docs->Active;
        if (d != nullptr && d->Project != nullptr) return d->Project;
        return workspace->StartupProject;
    }

    // =========================================================================
    //  opening workspaces / projects / files
    // =========================================================================
    void MainForm::OpenPath(String^ path)
    {
        if (String::IsNullOrEmpty(path)) return;
        String^ ext = Path::GetExtension(path)->ToLowerInvariant();
        try
        {
            if (ext == ".grws")
            {
                if (!CloseWorkspace()) return;
                SetWorkspace(Workspace::Load(path, settings->Env->CcsToolRoot));
                AddRecentWorkspace(path);
            }
            else if (ext == ".pro")
            {
                if (!CloseWorkspace()) return;
                SetWorkspace(Workspace::ForSingleProject(GrideProject::Load(path)));
                AddRecentWorkspace(path);
            }
            else if (CcsProjectReader::IsCcsProject(path) && (Directory::Exists(path) || ext == ".project" || ext == ".ccsproject" || ext == ".cproject"))
            {
                if (!CloseWorkspace()) return;
                SetWorkspace(Workspace::ForSingleProject(CcsProjectReader::Read(path, settings->Env->CcsToolRoot)));
                AddRecentWorkspace(CcsProjectReader::ResolveFolder(path));
            }
            else if (File::Exists(path)) OpenDocument(path, 0);
        }
        catch (Exception^ ex)
        {
            MessageBox::Show(this, "Could not open " + path + "\n\n" + ex->Message, "GRIDE", MessageBoxButtons::OK, MessageBoxIcon::Error);
        }
    }

    void MainForm::SetWorkspace(Workspace^ w)
    {
        workspace = w;
        if (w != nullptr)
        {
            for each (String^ err in w->LoadErrors) output->Append("Tools", "Workspace: " + err);
            if (w->LoadErrors->Count > 0) bottomArea->ShowPane(output);
            for each (EditorDocument^ d in docs->Documents)
                if (!d->IsUntitled) d->Project = w->FindProjectForFile(d->FilePath);
            // share the saved breakpoints with open editors
            for each (EditorDocument^ d in docs->Documents)
            {
                if (d->IsUntitled) continue;
                SortedSet<int>^ bp;
                if (!w->Breakpoints->TryGetValue(d->FilePath, bp)) { bp = gcnew SortedSet<int>(); w->Breakpoints[d->FilePath] = bp; }
                d->Editor->ShareBreakpoints(bp);
            }
            SetStatus(String::Format("Opened workspace '{0}' with {1} project(s)", w->Name, w->Projects->Count));
        }
        if (w != nullptr) filesPane->SetRoot(w->Directory);
        else filesPane->SetRoot(nullptr);
        RefreshProjectViews();
        UpdateCombos();
        UpdateStatus();
        RefreshBreakpointsPane();
        if (w != nullptr) properties->SetObject(gcnew WorkspaceProperties(w));
        else properties->SetObject(nullptr);
    }

    bool MainForm::CloseWorkspace()
    {
        if (workspace == nullptr) return true;
        if (debugger->IsActive) debugger->Stop();
        // close documents that belong to the workspace's projects
        for each (EditorDocument^ d in gcnew List<EditorDocument^>(docs->Documents))
            if (d->Project != nullptr && !docs->Close(d)) return false;
        SaveWorkspace();
        workspace = nullptr;
        SetWorkspace(nullptr);
        return true;
    }

    void MainForm::SaveWorkspace()
    {
        if (workspace == nullptr) return;
        try
        {
            workspace->SaveProjects();
            if (!workspace->IsTemporary) workspace->Save();
        }
        catch (Exception^ ex) { output->Append("Tools", "Could not save workspace: " + ex->Message); }
    }

    EditorDocument^ MainForm::OpenDocument(String^ path, int line)
    {
        if (!File::Exists(path))
        {
            MessageBox::Show(this, "File not found:\n" + path, "GRIDE", MessageBoxButtons::OK, MessageBoxIcon::Warning);
            return nullptr;
        }
        GrideProject^ p = nullptr;
        if (workspace != nullptr) p = workspace->FindProjectForFile(path);
        EditorDocument^ d = docs->Find(path);
        bool fresh = d == nullptr;
        try { d = docs->Open(path, p); }
        catch (Exception^ ex)
        {
            MessageBox::Show(this, "Could not open " + path + "\n\n" + ex->Message, "GRIDE", MessageBoxButtons::OK, MessageBoxIcon::Error);
            return nullptr;
        }
        if (fresh)
        {
            Dictionary<String^, SortedSet<int>^>^ map = BreakpointMap();
            SortedSet<int>^ bp;
            if (!map->TryGetValue(d->FilePath, bp)) { bp = gcnew SortedSet<int>(); map[d->FilePath] = bp; }
            d->Editor->ShareBreakpoints(bp);
            AddRecentFile(d->FilePath);
        }
        if (line > 0) d->Editor->GoToLine(line);
        return d;
    }

    void MainForm::AddRecentFile(String^ path) { RecentList::Push(settings->Recent->Files, path); }

    void MainForm::AddRecentWorkspace(String^ path)
    {
        RecentList::Push(settings->Recent->Workspaces, path);
        docs->Start->Build(settings->Recent->Workspaces);
    }

    void MainForm::RefreshProjectViews()
    {
        explorer->SetWorkspace(workspace);
        classView->Scan(workspace);
        UpdateTitle();
    }

    // =========================================================================
    //  pane / document events
    // =========================================================================
    void MainForm::OnExplorerFileActivated(String^ file, GrideProject^ project) { OpenDocument(file, 0); }

    void MainForm::OnExplorerSelection(Object^ sender, EventArgs^ e)
    {
        NodeInfo^ n = explorer->Selected;
        if (n == nullptr) return;
        switch (n->Kind)
        {
        case NodeKind::Workspace: properties->SetObject(gcnew WorkspaceProperties(workspace)); break;
        case NodeKind::Project:   properties->SetObject(gcnew ProjectProperties(n->Project)); break;
        case NodeKind::File:      properties->SetObject(gcnew FileProperties(n->FilePath, n->Project, settings)); break;
        default:                  properties->SetObject(gcnew ProjectProperties(n->Project)); break;
        }
    }

    void MainForm::OnNavigate(String^ file, int line) { OpenDocument(file, line); }

    void MainForm::OnStartPageCommand(String^ id) { commands->Execute(id); }
    void MainForm::OnStartPageRecent(String^ path) { OpenPath(path); }

    void MainForm::OnDocumentActivated(Object^ sender, EventArgs^ e)
    {
        EditorDocument^ d = docs->Active;
        if (d != nullptr && !d->IsUntitled && !explorer->HasFocus)
            properties->SetObject(gcnew FileProperties(d->FilePath, d->Project, settings));
        UpdateCombos();
        UpdateStatus();
        UpdateTitle();
    }

    void MainForm::OnDocumentCaret(Object^ sender, EventArgs^ e) { UpdateStatus(); }
    void MainForm::OnDocumentDirty(Object^ sender, EventArgs^ e) { UpdateTitle(); }

    void MainForm::OnRenameRequested(GrideProject^ project, String^ oldPath, String^ newName)
    {
        if (newName->IndexOfAny(Path::GetInvalidFileNameChars()) >= 0)
        {
            MessageBox::Show(this, "The name contains characters that are not allowed.", "Rename");
            explorer->RefreshTree();
            return;
        }
        String^ newPath = Path::Combine(Path::GetDirectoryName(oldPath), newName);
        try
        {
            if (File::Exists(oldPath)) File::Move(oldPath, newPath);
            project->RenameFile(oldPath, newPath);
            project->Save();
            EditorDocument^ d = docs->Find(oldPath);
            if (d != nullptr) { d->FilePath = newPath; docs->RefreshTab(d); }
            Dictionary<String^, SortedSet<int>^>^ map = BreakpointMap();
            SortedSet<int>^ bp;
            if (map->TryGetValue(oldPath, bp)) { map->Remove(oldPath); map[newPath] = bp; }
        }
        catch (Exception^ ex) { MessageBox::Show(this, ex->Message, "Rename", MessageBoxButtons::OK, MessageBoxIcon::Error); }
        RefreshProjectViews();
    }

    // =========================================================================
    //  File menu
    // =========================================================================
    void MainForm::FileNewProject()
    {
        NewProjectDialog^ dlg = gcnew NewProjectDialog(settings->Env->DefaultProjectLocation, workspace != nullptr);
        if (dlg->ShowDialog(this) != WF::DialogResult::OK) return;
        if (dlg->ImportCcs) { FileOpenCcs(); return; }

        GrideProject^ p = GrideProject::CreateNew(dlg->ProjectLocation, dlg->ProjectName, dlg->Lang, dlg->TemplateName, dlg->Targets);
        ProjectTemplates::Populate(p);
        settings->Env->DefaultProjectLocation = dlg->ProjectLocation;

        if (workspace == nullptr || dlg->NewWorkspace)
        {
            if (!CloseWorkspace()) return;
            Workspace^ w = Workspace::ForSingleProject(p);
            if (p->Targets->Count > 0) w->ActiveTarget = p->Targets[0];
            w->Save();                                  // NavFilter\NavFilter.grws
            SetWorkspace(w);
            AddRecentWorkspace(w->FilePath);
        }
        else
        {
            workspace->AddProject(p);
            SaveWorkspace();
            RefreshProjectViews();
        }
        output->Append("Tools", "Created project " + p->FilePath);
        for each (String^ f in p->AllFiles())
            if (Path::GetFileNameWithoutExtension(f)->StartsWith("main") || p->AllFiles()->Count == 1) { OpenDocument(f, 0); break; }
    }

    void MainForm::FileNewWorkspace()
    {
        SaveFileDialog^ dlg = gcnew SaveFileDialog();
        dlg->Title = "New Workspace";
        dlg->Filter = "GRIDE workspace (*.grws)|*.grws";
        dlg->InitialDirectory = settings->Env->DefaultProjectLocation;
        dlg->FileName = "Workspace.grws";
        if (dlg->ShowDialog(this) != WF::DialogResult::OK) return;
        if (!CloseWorkspace()) return;
        Workspace^ w = gcnew Workspace();
        w->FilePath = dlg->FileName;
        w->Name = Path::GetFileNameWithoutExtension(dlg->FileName);
        w->Save();
        SetWorkspace(w);
        AddRecentWorkspace(w->FilePath);
    }

    void MainForm::FileNewFile()
    {
        Language l = (Language)Math::Max(0, cbLanguage->SelectedIndex);
        EditorDocument^ d = docs->NewDocument(l);
        d->Editor->ShareBreakpoints(gcnew SortedSet<int>());
    }

    void MainForm::FileOpenWorkspace()
    {
        OpenFileDialog^ dlg = gcnew OpenFileDialog();
        dlg->Title = "Open Project / Workspace";
        dlg->Filter = "GRIDE workspace or project (*.grws;*.pro)|*.grws;*.pro|CCS 7.4 project (.project;.ccsproject)|.project;.ccsproject|All files (*.*)|*.*";
        if (dlg->ShowDialog(this) == WF::DialogResult::OK) OpenPath(dlg->FileName);
    }

    void MainForm::FileOpenFile()
    {
        OpenFileDialog^ dlg = gcnew OpenFileDialog();
        dlg->Title = "Open File";
        dlg->Multiselect = true;
        dlg->Filter = DocumentManager::FileFilter();
        if (workspace != nullptr) dlg->InitialDirectory = workspace->Directory;
        if (dlg->ShowDialog(this) != WF::DialogResult::OK) return;
        for each (String^ f in dlg->FileNames) OpenDocument(f, 0);
    }

    void MainForm::FileOpenCcs()
    {
        FolderBrowserDialog^ dlg = gcnew FolderBrowserDialog();
        dlg->Description = "Select the Code Composer Studio 7.4 project folder (the one holding .project / .ccsproject)";
        if (dlg->ShowDialog(this) != WF::DialogResult::OK) return;
        if (!CcsProjectReader::IsCcsProject(dlg->SelectedPath))
        {
            MessageBox::Show(this, "No .project / .ccsproject found in\n" + dlg->SelectedPath, "Import CCS 7.4 Project",
                             MessageBoxButtons::OK, MessageBoxIcon::Warning);
            return;
        }
        if (workspace != nullptr && !workspace->IsTemporary &&
            MessageBox::Show(this, "Add the CCS project to the current workspace?\n(No = open it on its own)", "Import CCS 7.4 Project",
                             MessageBoxButtons::YesNo, MessageBoxIcon::Question) == WF::DialogResult::Yes)
        {
            try
            {
                GrideProject^ p = CcsProjectReader::Read(dlg->SelectedPath, settings->Env->CcsToolRoot);
                workspace->AddProject(p);
                SaveWorkspace();
                RefreshProjectViews();
                output->Append("Tools", "Imported CCS project " + p->Name + " (read-only)");
            }
            catch (Exception^ ex) { MessageBox::Show(this, ex->Message, "Import CCS 7.4 Project", MessageBoxButtons::OK, MessageBoxIcon::Error); }
            return;
        }
        OpenPath(dlg->SelectedPath);
    }

    void MainForm::FileClose() { docs->Close(docs->Active); }

    void MainForm::FileCloseWorkspace() { CloseWorkspace(); }

    void MainForm::FileSave()
    {
        EditorDocument^ d = docs->Active;
        if (d != nullptr && docs->Save(d))
        {
            AddRecentFile(d->FilePath);
            SetStatus("Saved " + d->FilePath);
        }
    }

    void MainForm::FileSaveAs()
    {
        EditorDocument^ d = docs->Active;
        if (d != nullptr && docs->SaveAs(d)) AddRecentFile(d->FilePath);
    }

    void MainForm::FileSaveAll()
    {
        docs->SaveAll();
        SaveWorkspace();
        SettingsManager::Save(settings);
        SetStatus("All files saved");
    }

    // creates a .pro inside an existing folder of sources
    void MainForm::FileImportFolder()
    {
        FolderBrowserDialog^ dlg = gcnew FolderBrowserDialog();
        dlg->Description = "Select a folder with existing C90 / C++11 / Shalimar sources";
        if (dlg->ShowDialog(this) != WF::DialogResult::OK) return;
        String^ dir = dlg->SelectedPath;
        String^ name = Path::GetFileName(dir)->Replace(" ", "_");
        String^ pro = Path::Combine(dir, name + ".pro");
        if (File::Exists(pro)) { OpenPath(pro); return; }

        GrideProject^ p = gcnew GrideProject();
        p->FilePath = pro;
        p->Name = name;
        p->Template = "Imported";
        p->Targets->Add(Target::Windows);
        p->Targets->Add(Target::C6747);
        int c = 0, cpp = 0, shl = 0;
        for each (String^ f in Directory::GetFiles(dir, "*.*", SearchOption::AllDirectories))
        {
            String^ e = Path::GetExtension(f)->ToLowerInvariant();
            if (e != ".c" && e != ".h" && e != ".cpp" && e != ".hpp" && e != ".shl" && e != ".asm" && e != ".cmd") continue;
            String^ rel = PathUtil::MakeRelative(dir, f);
            if (rel->StartsWith("out/") || rel->StartsWith("obj/") || rel->StartsWith(".")) continue;
            String^ folder = Path::GetDirectoryName(rel->Replace(L'/', L'\\'))->Replace(L'\\', L'/');
            if (String::IsNullOrEmpty(folder)) folder = GrideProject::DefaultFolderFor(f);
            p->AddFile(f, folder);
            if (e == ".c") c++; else if (e == ".cpp") cpp++; else if (e == ".shl") shl++;
        }
        p->Lang = cpp > c && cpp >= shl ? Language::Cpp11 : (shl > c ? Language::Shalimar : Language::C90);
        for each (Target t in p->Targets)
        {
            p->Configurations->Add(gcnew BuildConfiguration("Debug", t));
            p->Configurations->Add(gcnew BuildConfiguration("Release", t));
        }
        p->Save();
        output->Append("Tools", String::Format("Created {0} with {1} file(s)", pro, p->AllFiles()->Count));
        if (workspace != nullptr && !workspace->IsTemporary)
        {
            workspace->AddProject(p);
            SaveWorkspace();
            RefreshProjectViews();
        }
        else OpenPath(pro);
    }

    void MainForm::FileExportZip()
    {
        GrideProject^ p = ContextProject();
        if (p == nullptr) return;
        SaveFileDialog^ dlg = gcnew SaveFileDialog();
        dlg->Filter = "ZIP archive (*.zip)|*.zip";
        dlg->FileName = p->Name + ".zip";
        if (dlg->ShowDialog(this) != WF::DialogResult::OK) return;
        try
        {
            if (File::Exists(dlg->FileName)) File::Delete(dlg->FileName);
            ZipArchive^ zip = ZipFile::Open(dlg->FileName, ZipArchiveMode::Create);
            int n = 0;
            try
            {
                if (!p->IsCcs && File::Exists(p->FilePath))
                {
                    ZipFileExtensions::CreateEntryFromFile(zip, p->FilePath, p->Name + "/" + Path::GetFileName(p->FilePath));
                    n++;
                }
                for each (String^ f in p->AllFiles())
                {
                    if (!File::Exists(f)) continue;
                    String^ rel = p->ToRelative(f);
                    if (Path::IsPathRooted(rel)) rel = "external/" + Path::GetFileName(f);
                    ZipFileExtensions::CreateEntryFromFile(zip, f, p->Name + "/" + rel);
                    n++;
                }
            }
            finally { delete zip; }     // always release the .zip file
            SetStatus(String::Format("Exported {0} file(s) to {1}", n, dlg->FileName));
        }
        catch (Exception^ ex) { MessageBox::Show(this, ex->Message, "Export", MessageBoxButtons::OK, MessageBoxIcon::Error); }
    }

    void MainForm::FilePageSetup()
    {
        PageSetupDialog^ dlg = gcnew PageSetupDialog();
        dlg->Document = printDoc;
        dlg->ShowDialog(this);
    }

    void MainForm::FilePrint()
    {
        EditorDocument^ d = docs->Active;
        if (d == nullptr) return;
        PrintDialog^ dlg = gcnew PrintDialog();
        dlg->Document = printDoc;
        dlg->UseEXDialog = true;
        if (dlg->ShowDialog(this) != WF::DialogResult::OK) return;
        printLines = d->Editor->GetText()->Split(L'\n');
        printIndex = 0;
        printPage = 1;
        printDoc->DocumentName = d->Title;
        try { printDoc->Print(); }
        catch (Exception^ ex) { MessageBox::Show(this, ex->Message, "Print", MessageBoxButtons::OK, MessageBoxIcon::Error); }
    }

    void MainForm::OnPrintPage(Object^ sender, System::Drawing::Printing::PrintPageEventArgs^ e)
    {
        SD::Font^ f = gcnew SD::Font("Consolas", 9.0f);
        SD::Font^ hf = gcnew SD::Font("Segoe UI", 8.0f, FontStyle::Bold);
        float y = (float)e->MarginBounds.Top;
        float lh = f->GetHeight(e->Graphics);
        e->Graphics->DrawString(String::Format("{0}    page {1}", printDoc->DocumentName, printPage), hf, Brushes::Black,
                                (float)e->MarginBounds.Left, y - 2 * lh);
        while (printIndex < printLines->Length && y + lh < e->MarginBounds.Bottom)
        {
            String^ text = String::Format("{0,5}  {1}", printIndex + 1, printLines[printIndex]->Replace("\t", "    "));
            e->Graphics->DrawString(text, f, Brushes::Black, (float)e->MarginBounds.Left, y);
            y += lh;
            printIndex++;
        }
        printPage++;
        e->HasMorePages = printIndex < printLines->Length;
    }

    void MainForm::FileExit() { Close(); }

    void MainForm::FileOpenSelected()
    {
        String^ f = explorer->SelectedFile;
        if (f != nullptr) OpenDocument(f, 0);
    }

    // =========================================================================
    //  Project menu
    // =========================================================================
    void MainForm::ProjectAddNewItem()
    {
        GrideProject^ p = ContextProject();
        if (p == nullptr || p->IsReadOnly) return;
        NewItemDialog^ dlg = gcnew NewItemDialog(p);
        if (dlg->ShowDialog(this) != WF::DialogResult::OK) return;
        String^ path = dlg->FilePath;
        try
        {
            PathUtil::EnsureDirectory(Path::GetDirectoryName(path));
            if (!File::Exists(path)) File::WriteAllText(path, ProjectTemplates::ItemContent(Path::GetFileName(path), p->Lang), gcnew UTF8Encoding(false));
            p->AddFile(path, dlg->Folder);
            p->Save();
        }
        catch (Exception^ ex) { MessageBox::Show(this, ex->Message, "Add New Item", MessageBoxButtons::OK, MessageBoxIcon::Error); return; }
        RefreshProjectViews();
        explorer->Reveal(path);
        OpenDocument(path, 0);
    }

    void MainForm::ProjectAddExisting()
    {
        GrideProject^ p = ContextProject();
        if (p == nullptr || p->IsReadOnly) return;
        OpenFileDialog^ dlg = gcnew OpenFileDialog();
        dlg->Title = "Add Existing Item - " + p->Name;
        dlg->Multiselect = true;
        dlg->InitialDirectory = p->ProjectDir;
        dlg->Filter = DocumentManager::FileFilter();
        if (dlg->ShowDialog(this) != WF::DialogResult::OK) return;
        NodeInfo^ n = explorer->Selected;
        String^ folder = nullptr;
        if (n != nullptr && n->Kind == NodeKind::Folder) folder = n->Folder;
        int added = 0;
        for each (String^ f in dlg->FileNames) if (p->AddFile(f, folder)) added++;
        p->Save();
        RefreshProjectViews();
        SetStatus(String::Format("Added {0} file(s) to {1}", added, p->Name));
    }

    void MainForm::ProjectNewFolder()
    {
        GrideProject^ p = ContextProject();
        if (p == nullptr || p->IsReadOnly) return;
        InputDialog^ dlg = gcnew InputDialog("New Folder", "Folder name", "NewFolder");
        if (dlg->ShowDialog(this) != WF::DialogResult::OK) return;
        String^ name = dlg->Value->Replace(L'\\', L'/');
        p->GetOrAddFolder(name);
        try { PathUtil::EnsureDirectory(p->ToAbsolute(name)); } catch (Exception^) { }
        p->Save();
        RefreshProjectViews();
    }

    void MainForm::ProjectRemoveItem()
    {
        String^ f = explorer->SelectedFile;
        GrideProject^ p = explorer->SelectedProject;
        if (f == nullptr || p == nullptr) return;
        if (p->IsReadOnly) { MessageBox::Show(this, "CCS projects are read-only in GRIDE.", "Remove"); return; }
        if (MessageBox::Show(this, "Remove " + Path::GetFileName(f) + " from " + p->Name + "?\n(The file stays on disk.)", "Remove",
                             MessageBoxButtons::OKCancel, MessageBoxIcon::Question) != WF::DialogResult::OK) return;
        p->RemoveFile(f);
        p->Save();
        RefreshProjectViews();
    }

    void MainForm::ProjectRename() { explorer->BeginRename(); }

    void MainForm::ProjectExclude()
    {
        String^ f = explorer->SelectedFile;
        GrideProject^ p = explorer->SelectedProject;
        if (f == nullptr || p == nullptr || p->IsReadOnly) return;
        p->SetExcluded(f, !p->IsExcluded(f));
        p->Save();
        RefreshProjectViews();
    }

    void MainForm::ProjectAddProject()
    {
        OpenFileDialog^ dlg = gcnew OpenFileDialog();
        dlg->Title = "Add Project to Workspace";
        dlg->Filter = "GRIDE project (*.pro)|*.pro|CCS 7.4 project (.project;.ccsproject)|.project;.ccsproject";
        if (dlg->ShowDialog(this) != WF::DialogResult::OK) return;
        try
        {
            GrideProject^ p = PathUtil::HasExtension(dlg->FileName, ".pro") ? GrideProject::Load(dlg->FileName)
                                                                            : CcsProjectReader::Read(dlg->FileName, settings->Env->CcsToolRoot);
            if (workspace == nullptr) { SetWorkspace(Workspace::ForSingleProject(p)); return; }
            if (!workspace->AddProject(p)) { MessageBox::Show(this, "That project is already in the workspace.", "GRIDE"); return; }
            SaveWorkspace();
            RefreshProjectViews();
        }
        catch (Exception^ ex) { MessageBox::Show(this, ex->Message, "Add Project", MessageBoxButtons::OK, MessageBoxIcon::Error); }
    }

    void MainForm::ProjectRemoveProject()
    {
        GrideProject^ p = ContextProject();
        if (p == nullptr) return;
        if (MessageBox::Show(this, "Remove project '" + p->Name + "' from the workspace?\n(Its files stay on disk.)", "GRIDE",
                             MessageBoxButtons::OKCancel, MessageBoxIcon::Question) != WF::DialogResult::OK) return;
        for each (EditorDocument^ d in gcnew List<EditorDocument^>(docs->Documents))
            if (d->Project == p && !docs->Close(d)) return;
        workspace->RemoveProject(p);
        SaveWorkspace();
        RefreshProjectViews();
    }

    void MainForm::ProjectSetStartup()
    {
        GrideProject^ p = ContextProject();
        if (p == nullptr) return;
        workspace->StartupName = p->Name;
        workspace->IsDirty = true;
        SaveWorkspace();
        RefreshProjectViews();
    }

    void MainForm::ProjectDependencies()
    {
        DependenciesDialog^ dlg = gcnew DependenciesDialog(workspace, ContextProject());
        if (dlg->ShowDialog(this) == WF::DialogResult::OK) RefreshProjectViews();
    }

    void MainForm::ProjectBuildOrder()
    {
        StringBuilder^ sb = gcnew StringBuilder();
        sb->AppendLine("Projects are built in this order (dependencies first):");
        sb->AppendLine();
        int i = 1;
        for each (GrideProject^ p in workspace->BuildOrder())
        {
            sb->AppendFormat("{0,3}. {1}", i++, p->Name);
            if (p->DependsOn->Count > 0) sb->Append("    (depends on " + String::Join(", ", p->DependsOn) + ")");
            sb->AppendLine();
        }
        TextDialog^ dlg = gcnew TextDialog("Build Order", sb->ToString());
        dlg->ShowDialog(this);
    }

    void MainForm::ProjectRefreshCcs()
    {
        if (workspace == nullptr) return;
        int refreshed = 0;
        for (int i = 0; i < workspace->Projects->Count; i++)
        {
            GrideProject^ p = workspace->Projects[i];
            if (!p->IsCcs) continue;
            try
            {
                workspace->Projects[i] = CcsProjectReader::Read(p->ProjectDir, settings->Env->CcsToolRoot);
                refreshed++;
            }
            catch (Exception^ ex) { output->Append("Tools", "Refresh " + p->Name + ": " + ex->Message); }
        }
        for each (EditorDocument^ d in docs->Documents)
            if (!d->IsUntitled) d->Project = workspace->FindProjectForFile(d->FilePath);
        RefreshProjectViews();
        SetStatus(String::Format("Refreshed {0} CCS project(s)", refreshed));
    }

    void MainForm::ProjectOpenFolder()
    {
        String^ target = explorer->SelectedFile;
        if (target == nullptr)
        {
            GrideProject^ p = ContextProject();
            if (p != nullptr) target = p->ProjectDir;
            else if (workspace != nullptr) target = workspace->Directory;
        }
        if (target == nullptr) return;
        if (File::Exists(target)) System::Diagnostics::Process::Start("explorer.exe", "/select,\"" + target + "\"");
        else if (Directory::Exists(target)) System::Diagnostics::Process::Start("explorer.exe", "\"" + target + "\"");
    }

    void MainForm::ProjectShowProperties()
    {
        GrideProject^ p = ContextProject();
        if (p == nullptr) return;
        ProjectPropertiesDialog^ dlg = gcnew ProjectPropertiesDialog(p, workspace, CurrentConfig(), CurrentTarget());
        if (dlg->ShowDialog(this) == WF::DialogResult::OK) RefreshProjectViews();
    }
}}
