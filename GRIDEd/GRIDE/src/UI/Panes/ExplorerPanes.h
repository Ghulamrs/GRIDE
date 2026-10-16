// =============================================================================
//  ExplorerPanes.h - the left-hand panes
//    ProjectExplorerPane  workspace > projects > folders > files (+ CCS 7.4)
//    ClassViewPane        functions / types / macros found in project sources
//    FilesPane            plain file-system view of the workspace folder
// =============================================================================
#pragma once
#include "../Docking/DockArea.h"
#include "../../Model/Workspace.h"
#include "../../Settings/GrideSettings.h"

namespace Gride { namespace UI {

    using namespace Gride::Core;
    using namespace Gride::Model;
    using namespace Gride::Settings;

    public delegate void NavigateHandler(String^ file, int line);
    public delegate void NavigateColumnHandler(String^ file, int line, int column);
    public delegate void FileActivatedHandler(String^ file, GrideProject^ project);
    public delegate void RenameHandler(GrideProject^ project, String^ oldPath, String^ newName);

    // SolutionFolder / SolutionItem: a Visual Studio solution's folders and the files they hold, which belong to no project
    public enum class NodeKind { Workspace, Project, Folder, File, SolutionFolder, SolutionItem };

    public ref class NodeInfo
    {
    public:
        NodeInfo(NodeKind k, GrideProject^ p, String^ path, String^ folder) { Kind = k; Project = p; FilePath = path; Folder = folder; }
        property NodeKind Kind;
        property GrideProject^ Project;
        property String^ FilePath;
        property String^ Folder;
        property String^ Key
        {
            String^ get()
            {
                String^ pn = Project != nullptr ? Project->Name : String::Empty;
                return String::Format("{0}|{1}|{2}|{3}", (int)Kind, pn, Folder, FilePath);
            }
        }
    };

    // -------------------------------------------------------------------------
    public ref class FileIcons abstract sealed
    {
    public:
        static array<String^>^ Names()
        {
            return gcnew array<String^>{ "workspace", "project", "projectcpp", "projectshl", "projectccs", "folder",
                                          "filec", "filecpp", "fileshl", "fileh", "fileasm", "filecmd", "file",
                                          "class", "struct", "function", "variable" };
        }
        static String^ ForFile(String^ path)
        {
            String^ e = Path::GetExtension(path)->ToLowerInvariant();
            if (e == ".c") return "filec";
            if (e == ".cpp" || e == ".cc" || e == ".cxx") return "filecpp";
            if (e == ".shl") return "fileshl";
            if (e == ".h" || e == ".hpp" || e == ".hh" || e == ".inc") return "fileh";
            if (e == ".asm" || e == ".s" || e == ".sa") return "fileasm";
            if (e == ".cmd" || e == ".ld") return "filecmd";
            return "file";
        }
        static String^ ForProject(GrideProject^ p)
        {
            if (p->IsCcs) return "projectccs";
            if (p->Lang == Language::Cpp11) return "projectcpp";
            if (p->Lang == Language::Shalimar) return "projectshl";
            return "project";
        }
    };

    // -------------------------------------------------------------------------
    public ref class ProjectExplorerPane : public ToolPane
    {
    public:
        ProjectExplorerPane() : ToolPane("ProjectExplorer", "Project Explorer", "workspace")
        {
            tree = gcnew TreeView();
            tree->Dock = DockStyle::Fill;
            tree->HideSelection = false;
            tree->ShowLines = false;
            tree->FullRowSelect = true;
            tree->ItemHeight = 22;
            tree->LabelEdit = true;
            tree->ImageList = IconFactory::CreateImageList(FileIcons::Names());
            tree->NodeMouseDoubleClick += gcnew TreeNodeMouseClickEventHandler(this, &ProjectExplorerPane::OnDoubleClick);
            tree->NodeMouseClick += gcnew TreeNodeMouseClickEventHandler(this, &ProjectExplorerPane::OnNodeClick);
            tree->AfterSelect += gcnew TreeViewEventHandler(this, &ProjectExplorerPane::OnAfterSelect);
            tree->BeforeLabelEdit += gcnew NodeLabelEditEventHandler(this, &ProjectExplorerPane::OnBeforeLabelEdit);
            tree->AfterLabelEdit += gcnew NodeLabelEditEventHandler(this, &ProjectExplorerPane::OnAfterLabelEdit);
            tree->KeyDown += gcnew KeyEventHandler(this, &ProjectExplorerPane::OnKeyDown);
            Controls->Add(tree);
            workspace = nullptr;
        }

        event FileActivatedHandler^ FileActivated;
        event EventHandler^ SelectionChanged;
        event RenameHandler^ RenameRequested;
        event EventHandler^ RemoveRequested;    // Del pressed on a file while the pane floats

        // context menus are created by MainForm from its command registry
        property System::Windows::Forms::ContextMenuStrip^ WorkspaceMenu;
        property System::Windows::Forms::ContextMenuStrip^ ProjectMenu;
        property System::Windows::Forms::ContextMenuStrip^ FolderMenu;
        property System::Windows::Forms::ContextMenuStrip^ FileMenu;

        property NodeInfo^ Selected
        {
            NodeInfo^ get() { if (tree->SelectedNode == nullptr) return nullptr; return dynamic_cast<NodeInfo^>(tree->SelectedNode->Tag); }
        }
        property GrideProject^ SelectedProject
        {
            GrideProject^ get() { NodeInfo^ n = Selected; if (n == nullptr) return nullptr; return n->Project; }
        }
        property String^ SelectedFile
        {
            String^ get() { NodeInfo^ n = Selected; if (n != nullptr && n->Kind == NodeKind::File) return n->FilePath; return nullptr; }
        }
        property bool HasFocus { bool get() { return tree->Focused; } }
        // a file name is being edited in place (the edit box is a child of the tree)
        property bool IsEditingLabel { bool get() { return tree->ContainsFocus && !tree->Focused; } }

        void SetWorkspace(Workspace^ w) { workspace = w; RefreshTree(); }

        void RefreshTree()
        {
            HashSet<String^>^ expanded = gcnew HashSet<String^>();
            String^ selectedKey = nullptr;
            if (Selected != nullptr) selectedKey = Selected->Key;
            CollectExpanded(tree->Nodes, expanded);
            bool first = tree->Nodes->Count == 0;

            tree->BeginUpdate();
            tree->Nodes->Clear();
            if (workspace != nullptr)
            {
                String^ plural = "s";
                if (workspace->Projects->Count == 1) plural = String::Empty;
                TreeNode^ root = MakeNode(String::Format("Workspace '{0}' ({1} project{2})", workspace->Name,
                    workspace->Projects->Count, plural),
                    "workspace", gcnew NodeInfo(NodeKind::Workspace, nullptr, workspace->FilePath, nullptr));
                tree->Nodes->Add(root);
                // a Visual Studio solution's folders, nested as the .sln nests them, with their items
                Dictionary<String^, TreeNode^>^ solutionFolders = gcnew Dictionary<String^, TreeNode^>(StringComparer::OrdinalIgnoreCase);
                for each (String^ path in workspace->SolutionFolders) SolutionFolderNode(root, path, solutionFolders);
                for each (KeyValuePair<String^, List<String^>^> kv in workspace->SolutionItems)
                {
                    TreeNode^ fn = SolutionFolderNode(root, kv.Key, solutionFolders);
                    for each (String^ abs in kv.Value)
                    {
                        String^ text = Path::GetFileName(abs);
                        if (!File::Exists(abs)) text = text + "  (missing)";
                        fn->Nodes->Add(MakeNode(text, FileIcons::ForFile(abs), gcnew NodeInfo(NodeKind::SolutionItem, nullptr, abs, kv.Key)));
                    }
                }
                for each (GrideProject^ p in workspace->Projects)
                {
                    String^ label = p->Name + "   [" + LanguageInfo::Name(p->Lang) + "]";
                    if (p->IsCcs) label = p->Name + "   [" + (p->Template != nullptr && p->Template->StartsWith("CCS 5.5") ? "CCS 5.5" : "CCS 7.4") + ", read-only]";
                    else if (p->IsVs) label = p->Name + "   [" + (p->Vs != nullptr && p->Vs->Version->Length > 0 ? p->Vs->Version->Split(L',')[0] : "Visual Studio") +
                                              (p->Vs != nullptr && p->Vs->Toolset->Length > 0 ? " " + p->Vs->Toolset : String::Empty) + ", read-only]";
                    TreeNode^ pn = MakeNode(label, FileIcons::ForProject(p), gcnew NodeInfo(NodeKind::Project, p, p->FilePath, nullptr));
                    if (String::Compare(p->Name, workspace->StartupName, true) == 0)
                    {
                        // one bold font for the life of the pane, not a new GDI font per refresh
                        if (boldFont == nullptr || !boldFont->FontFamily->Equals(tree->Font->FontFamily) || boldFont->Size != tree->Font->Size)
                            boldFont = gcnew SD::Font(tree->Font, FontStyle::Bold);
                        pn->NodeFont = boldFont;
                    }
                    TreeNode^ under = root;
                    if (p->IsVs && p->Vs != nullptr && p->Vs->SolutionFolder->Length > 0) under = SolutionFolderNode(root, p->Vs->SolutionFolder, solutionFolders);
                    under->Nodes->Add(pn);
                    for each (ProjectFolder^ f in p->Folders)
                    {
                        // a folder that names its compiler says so, as RIDE's pane names a group's toolchain
                        String^ folderText = f->Name;
                        String^ own;
                        if (p->FolderToolchains->TryGetValue(f->Name, own) && !String::IsNullOrWhiteSpace(own))
                            folderText = f->Name + "  (" + Gride::Services::RideToolchain::Name(Gride::Services::RideToolchain::FromWord(own)) + ")";
                        TreeNode^ fn = MakeNode(folderText, "folder", gcnew NodeInfo(NodeKind::Folder, p, nullptr, f->Name));
                        pn->Nodes->Add(fn);
                        List<String^>^ files = gcnew List<String^>(f->Files);
                        files->Sort(StringComparer::OrdinalIgnoreCase);
                        for each (String^ rel in files)
                        {
                            String^ abs = p->ToAbsolute(rel);
                            String^ text = Path::GetFileName(rel);
                            if (p->IsExcluded(abs)) text = text + "  (excluded)";
                            if (!File::Exists(abs)) text = text + "  (missing)";
                            fn->Nodes->Add(MakeNode(text, FileIcons::ForFile(rel), gcnew NodeInfo(NodeKind::File, p, abs, f->Name)));
                        }
                    }
                }
                root->Expand();
                if (first) for each (TreeNode^ pn in root->Nodes) pn->Expand();
                if (first) for each (KeyValuePair<String^, TreeNode^> kv in solutionFolders) kv.Value->Expand();
                RestoreExpanded(tree->Nodes, expanded, selectedKey);
            }
            tree->EndUpdate();
        }

        // the node of a solution folder path ("Libs/Inner"), made with its parents when missing
        TreeNode^ SolutionFolderNode(TreeNode^ root, String^ path, Dictionary<String^, TreeNode^>^ made)
        {
            TreeNode^ found;
            if (made->TryGetValue(path, found)) return found;
            int slash = path->LastIndexOf(L'/');
            TreeNode^ parent = slash > 0 ? SolutionFolderNode(root, path->Substring(0, slash), made) : root;
            String^ leaf = slash > 0 ? path->Substring(slash + 1) : path;
            TreeNode^ n = MakeNode(leaf + "   [solution folder]", "folder", gcnew NodeInfo(NodeKind::SolutionFolder, nullptr, nullptr, path));
            parent->Nodes->Add(n);
            made[path] = n;
            return n;
        }

        // selects the node of a file (used by "Track active item")
        void Reveal(String^ path)
        {
            TreeNode^ n = FindFileNode(tree->Nodes, path);
            if (n != nullptr) { tree->SelectedNode = n; n->EnsureVisible(); }
        }

        void BeginRename()
        {
            if (tree->SelectedNode != nullptr && SelectedFile != nullptr) tree->SelectedNode->BeginEdit();
        }

    private:
        TreeView^ tree;
        Workspace^ workspace;
        SD::Font^ boldFont;

        TreeNode^ MakeNode(String^ text, String^ icon, NodeInfo^ info)
        {
            TreeNode^ n = gcnew TreeNode(text);
            n->ImageKey = icon;
            n->SelectedImageKey = icon;
            n->Tag = info;
            return n;
        }

        void CollectExpanded(TreeNodeCollection^ nodes, HashSet<String^>^ keys)
        {
            for each (TreeNode^ n in nodes)
            {
                NodeInfo^ i = dynamic_cast<NodeInfo^>(n->Tag);
                if (n->IsExpanded && i != nullptr) keys->Add(i->Key);
                CollectExpanded(n->Nodes, keys);
            }
        }

        void RestoreExpanded(TreeNodeCollection^ nodes, HashSet<String^>^ keys, String^ selectedKey)
        {
            for each (TreeNode^ n in nodes)
            {
                NodeInfo^ i = dynamic_cast<NodeInfo^>(n->Tag);
                if (i != nullptr && keys->Contains(i->Key)) n->Expand();
                if (i != nullptr && selectedKey != nullptr && i->Key == selectedKey) tree->SelectedNode = n;
                RestoreExpanded(n->Nodes, keys, selectedKey);
            }
        }

        TreeNode^ FindFileNode(TreeNodeCollection^ nodes, String^ path)
        {
            for each (TreeNode^ n in nodes)
            {
                NodeInfo^ i = dynamic_cast<NodeInfo^>(n->Tag);
                if (i != nullptr && i->Kind == NodeKind::File && PathUtil::SamePath(i->FilePath, path)) return n;
                TreeNode^ r = FindFileNode(n->Nodes, path);
                if (r != nullptr) return r;
            }
            return nullptr;
        }

        void OnDoubleClick(Object^ sender, TreeNodeMouseClickEventArgs^ e)
        {
            NodeInfo^ i = dynamic_cast<NodeInfo^>(e->Node->Tag);
            if (i != nullptr && (i->Kind == NodeKind::File || i->Kind == NodeKind::SolutionItem)) FileActivated(i->FilePath, i->Project);
        }

        void OnNodeClick(Object^ sender, TreeNodeMouseClickEventArgs^ e)
        {
            if (e->Button != WF::MouseButtons::Right) return;
            tree->SelectedNode = e->Node;
            NodeInfo^ i = dynamic_cast<NodeInfo^>(e->Node->Tag);
            if (i == nullptr) return;
            System::Windows::Forms::ContextMenuStrip^ m = nullptr;
            switch (i->Kind)
            {
            case NodeKind::Workspace: m = WorkspaceMenu; break;
            case NodeKind::Project:   m = ProjectMenu; break;
            case NodeKind::Folder:    m = FolderMenu; break;
            case NodeKind::SolutionFolder:
            case NodeKind::SolutionItem: m = WorkspaceMenu; break;
            default:                  m = FileMenu; break;
            }
            if (m != nullptr) m->Show(tree, e->Location);
        }

        void OnAfterSelect(Object^ sender, TreeViewEventArgs^ e) { SelectionChanged(this, EventArgs::Empty); }

        void OnKeyDown(Object^ sender, KeyEventArgs^ e)
        {
            NodeInfo^ sel = Selected;
            if (e->KeyCode == Keys::Enter && sel != nullptr && sel->Kind == NodeKind::SolutionItem) { FileActivated(sel->FilePath, nullptr); e->Handled = true; return; }
            if (e->KeyCode == Keys::Enter && SelectedFile != nullptr) { FileActivated(SelectedFile, SelectedProject); e->Handled = true; }
            if (e->KeyCode == Keys::F2) { BeginRename(); e->Handled = true; }
            if (e->KeyData == Keys::Delete && SelectedFile != nullptr) { RemoveRequested(this, EventArgs::Empty); e->Handled = true; }
        }

        void OnBeforeLabelEdit(Object^ sender, NodeLabelEditEventArgs^ e)
        {
            NodeInfo^ i = dynamic_cast<NodeInfo^>(e->Node->Tag);
            if (i == nullptr || i->Kind != NodeKind::File || i->Project == nullptr || i->Project->IsReadOnly) { e->CancelEdit = true; return; }
            e->Node->Text = Path::GetFileName(i->FilePath);
        }

        void OnAfterLabelEdit(Object^ sender, NodeLabelEditEventArgs^ e)
        {
            NodeInfo^ i = dynamic_cast<NodeInfo^>(e->Node->Tag);
            e->CancelEdit = true;              // the tree is rebuilt after the rename
            pendingRename = nullptr;
            pendingName = nullptr;
            if (i != nullptr && !String::IsNullOrEmpty(e->Label) && e->Label != Path::GetFileName(i->FilePath))
            {
                pendingRename = i;
                pendingName = e->Label->Trim();
            }
            // never rebuild the tree (or show message boxes) inside the label-edit
            // notification - the native control still owns the edited node
            BeginInvoke(gcnew MethodInvoker(this, &ProjectExplorerPane::FinishRename));
        }

        NodeInfo^ pendingRename;
        String^ pendingName;

        void FinishRename()
        {
            NodeInfo^ i = pendingRename;
            String^ name = pendingName;
            pendingRename = nullptr;
            pendingName = nullptr;
            if (i == nullptr || String::IsNullOrEmpty(name)) { RefreshTree(); return; }
            RenameRequested(i->Project, i->FilePath, name);
        }
    };

    // -------------------------------------------------------------------------
    //  ClassViewPane - lightweight symbol scan (regular expressions)
    // -------------------------------------------------------------------------
    public ref class ClassViewPane : public ToolPane
    {
    public:
        ClassViewPane() : ToolPane("ClassView", "Class View", "class")
        {
            tree = gcnew TreeView();
            tree->Dock = DockStyle::Fill;
            tree->ShowLines = false;
            tree->FullRowSelect = true;
            tree->ItemHeight = 22;
            tree->ImageList = IconFactory::CreateImageList(FileIcons::Names());
            tree->NodeMouseDoubleClick += gcnew TreeNodeMouseClickEventHandler(this, &ClassViewPane::OnDoubleClick);
            Controls->Add(tree);
        }

        event NavigateHandler^ Navigate;

        void Scan(Workspace^ w)
        {
            // a rescan (after a save) keeps what was expanded; the first one expands the projects
            HashSet<String^>^ open = gcnew HashSet<String^>();
            bool first = tree->Nodes->Count == 0;
            CollectExpanded(tree->Nodes, open);
            tree->BeginUpdate();
            tree->Nodes->Clear();
            if (w != nullptr)
            {
                Regex^ fn = gcnew Regex("^(?:static\\s+|extern\\s+|inline\\s+|const\\s+|unsigned\\s+|signed\\s+)*[A-Za-z_][\\w]*[\\s\\*&]+(?<name>[A-Za-z_]\\w*(::\\w+)?)\\s*\\([^;]*$");
                Regex^ ty = gcnew Regex("^\\s*(typedef\\s+)?(?<kind>struct|class|union|enum)\\s+(?<name>[A-Za-z_]\\w*)");
                Regex^ td = gcnew Regex("^\\s*}\\s*(?<name>[A-Za-z_]\\w*)\\s*;");
                // Shalimar: fun <results> = name(params) {
                Regex^ sf = gcnew Regex("^\\s*fun\\s*<[^>]*>\\s*=\\s*(?<name>[A-Za-z_]\\w*)\\s*\\(");
                Regex^ mc = gcnew Regex("^\\s*#\\s*define\\s+(?<name>[A-Za-z_]\\w*)");
                for each (GrideProject^ p in w->Projects)
                {
                    TreeNode^ pn = gcnew TreeNode(p->Name);
                    pn->ImageKey = FileIcons::ForProject(p); pn->SelectedImageKey = pn->ImageKey;
                    TreeNode^ fNode = Group(pn, "Functions", "function");
                    TreeNode^ tNode = Group(pn, "Types", "struct");
                    TreeNode^ mNode = Group(pn, "Macros", "variable");
                    for each (String^ f in p->AllFiles())
                    {
                        if (!File::Exists(f)) continue;
                        String^ ext = Path::GetExtension(f)->ToLowerInvariant();
                        if (ext != ".c" && ext != ".cpp" && ext != ".h" && ext != ".hpp" && ext != ".shl" && ext != ".cc") continue;
                        array<String^>^ lines;
                        try { lines = File::ReadAllLines(f); } catch (Exception^) { continue; }
                        for (int i = 0; i < lines->Length; i++)
                        {
                            String^ l = lines[i];
                            Match^ m = mc->Match(l);
                            if (m->Success) { Leaf(mNode, m->Groups["name"]->Value, "variable", f, i + 1); continue; }
                            m = ty->Match(l);
                            if (m->Success)
                            {
                                String^ icon = "struct";
                                if (m->Groups["kind"]->Value == "class") icon = "class";
                                Leaf(tNode, m->Groups["kind"]->Value + " " + m->Groups["name"]->Value, icon, f, i + 1);
                                continue;
                            }
                            m = td->Match(l);
                            if (m->Success) { Leaf(tNode, "typedef " + m->Groups["name"]->Value, "struct", f, i + 1); continue; }
                            m = sf->Match(l);
                            if (m->Success) { Leaf(fNode, m->Groups["name"]->Value + "()", "function", f, i + 1); continue; }
                            if (l->Length > 0 && !Char::IsWhiteSpace(l[0]) && !l->StartsWith("#") && !l->StartsWith("/") && !l->StartsWith("*"))
                            {
                                m = fn->Match(l);
                                if (m->Success && !l->TrimEnd()->EndsWith(";") && m->Groups["name"]->Value != "if" && m->Groups["name"]->Value != "while")
                                    Leaf(fNode, m->Groups["name"]->Value + "()", "function", f, i + 1);
                            }
                        }
                    }
                    // a group with nothing found is left out, so an empty one does not look like a placeholder
                    for each (TreeNode^ g in gcnew array<TreeNode^>{ fNode, tNode, mNode })
                        if (g->Nodes->Count == 0) g->Remove();
                    if (pn->Nodes->Count == 0) pn->Nodes->Add(gcnew TreeNode("(no symbols found)"));
                    tree->Nodes->Add(pn);
                    if (first) pn->Expand();
                }
            }
            if (!first) RestoreExpanded(tree->Nodes, open);
            tree->EndUpdate();
        }

    private:
        TreeView^ tree;

        static void CollectExpanded(TreeNodeCollection^ nodes, HashSet<String^>^ open)
        {
            for each (TreeNode^ n in nodes)
                if (n->IsExpanded) { open->Add(n->FullPath); CollectExpanded(n->Nodes, open); }
        }

        static void RestoreExpanded(TreeNodeCollection^ nodes, HashSet<String^>^ open)
        {
            for each (TreeNode^ n in nodes)
                if (open->Contains(n->FullPath)) { n->Expand(); RestoreExpanded(n->Nodes, open); }
        }

        TreeNode^ Group(TreeNode^ parent, String^ text, String^ icon)
        {
            TreeNode^ n = gcnew TreeNode(text);
            n->ImageKey = icon; n->SelectedImageKey = icon;
            parent->Nodes->Add(n);
            return n;
        }

        void Leaf(TreeNode^ parent, String^ text, String^ icon, String^ file, int line)
        {
            TreeNode^ n = gcnew TreeNode(text + "    " + Path::GetFileName(file) + ":" + line.ToString());
            n->ImageKey = icon; n->SelectedImageKey = icon;
            n->Tag = gcnew Tuple<String^, int>(file, line);
            parent->Nodes->Add(n);
        }

        void OnDoubleClick(Object^ sender, TreeNodeMouseClickEventArgs^ e)
        {
            Tuple<String^, int>^ t = dynamic_cast<Tuple<String^, int>^>(e->Node->Tag);
            if (t != nullptr) Navigate(t->Item1, t->Item2);
        }
    };

    // -------------------------------------------------------------------------
    //  FilesPane - the workspace folder as it is on disk
    // -------------------------------------------------------------------------
    public ref class FilesPane : public ToolPane
    {
    public:
        FilesPane() : ToolPane("Files", "Files", "folder")
        {
            tree = gcnew TreeView();
            tree->Dock = DockStyle::Fill;
            tree->ShowLines = false;
            tree->ItemHeight = 22;
            tree->ImageList = IconFactory::CreateImageList(FileIcons::Names());
            tree->BeforeExpand += gcnew TreeViewCancelEventHandler(this, &FilesPane::OnBeforeExpand);
            tree->NodeMouseDoubleClick += gcnew TreeNodeMouseClickEventHandler(this, &FilesPane::OnDoubleClick);
            Controls->Add(tree);
        }

        event FileActivatedHandler^ FileActivated;

        void SetRoot(String^ dir)
        {
            tree->Nodes->Clear();
            if (String::IsNullOrEmpty(dir) || !Directory::Exists(dir)) return;
            TreeNode^ root = FolderNode(dir);
            root->Text = dir;
            tree->Nodes->Add(root);
            root->Expand();
        }

    private:
        TreeView^ tree;

        TreeNode^ FolderNode(String^ dir)
        {
            TreeNode^ n = gcnew TreeNode(Path::GetFileName(dir));
            n->ImageKey = "folder"; n->SelectedImageKey = "folder";
            n->Tag = dir;
            n->Nodes->Add(gcnew TreeNode("..."));   // placeholder until expanded
            return n;
        }

        void OnBeforeExpand(Object^ sender, TreeViewCancelEventArgs^ e)
        {
            String^ dir = dynamic_cast<String^>(e->Node->Tag);
            if (dir == nullptr || !Directory::Exists(dir)) return;
            if (e->Node->Nodes->Count == 1 && e->Node->Nodes[0]->Tag == nullptr)
            {
                e->Node->Nodes->Clear();
                try
                {
                    for each (String^ d in Directory::GetDirectories(dir))
                    {
                        String^ name = Path::GetFileName(d);
                        if (name->StartsWith(".")) continue;
                        e->Node->Nodes->Add(FolderNode(d));
                    }
                    for each (String^ f in Directory::GetFiles(dir))
                    {
                        TreeNode^ n = gcnew TreeNode(Path::GetFileName(f));
                        n->ImageKey = FileIcons::ForFile(f); n->SelectedImageKey = n->ImageKey;
                        n->Tag = gcnew FileInfo(f);
                        e->Node->Nodes->Add(n);
                    }
                }
                catch (Exception^) { }
            }
        }

        void OnDoubleClick(Object^ sender, TreeNodeMouseClickEventArgs^ e)
        {
            FileInfo^ fi = dynamic_cast<FileInfo^>(e->Node->Tag);
            if (fi != nullptr) FileActivated(fi->FullName, nullptr);
        }
    };
}}
