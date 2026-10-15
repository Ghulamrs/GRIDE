// =============================================================================
//  ProjectDialogs.h - project related dialogs
//    NewProjectDialog          File > New > Project
//    NewItemDialog             Project > Add New Item
//    ProjectPropertiesDialog   Project > Properties (per Config|Target)
//    ConfigurationManagerDialog Build > Configuration Manager
//    DependenciesDialog        Project > Project Dependencies
//    BatchBuildDialog          Build > Batch Build
// =============================================================================
#pragma once
#include "DialogBase.h"
#include "../PropertyObjects.h"
#include "../../Services/ProjectTemplates.h"
#include "../../Services/BuildEngine.h"      // BuildAction (BatchBuildDialog)

namespace Gride { namespace UI {

    using namespace Gride::Core;
    using namespace Gride::Model;
    using namespace Gride::Services;

    // -------------------------------------------------------------------------
    public ref class NewProjectDialog : public ThemedDialog
    {
    public:
        NewProjectDialog(String^ defaultLocation, bool haveWorkspace) : ThemedDialog("New Project", 880, 600)
        {
            Targets = gcnew List<Target>();
            ImportCcs = false;

            // ---- three columns: language | templates | folder layout ----
            TableLayoutPanel^ cols = gcnew TableLayoutPanel();
            cols->Dock = DockStyle::Top;
            cols->Height = 330;
            cols->ColumnCount = 3;
            cols->ColumnStyles->Add(gcnew ColumnStyle(SizeType::Absolute, 190));
            cols->ColumnStyles->Add(gcnew ColumnStyle(SizeType::Percent, 100));
            cols->ColumnStyles->Add(gcnew ColumnStyle(SizeType::Absolute, 260));
            cols->Padding = WF::Padding(10, 10, 10, 0);

            languages = gcnew ListBox();
            languages->Dock = DockStyle::Fill;
            languages->IntegralHeight = false;
            languages->ItemHeight = 22;
            languages->Items->AddRange(gcnew array<Object^>{ "C90", "C++11", "Shalimar", "Import CCS 7.4..." });
            languages->SelectedIndexChanged += gcnew EventHandler(this, &NewProjectDialog::OnLanguageChanged);

            templates = gcnew ListBox();
            templates->Dock = DockStyle::Fill;
            templates->IntegralHeight = false;
            templates->SelectedIndexChanged += gcnew EventHandler(this, &NewProjectDialog::OnTemplateChanged);

            description = gcnew Label();
            description->Dock = DockStyle::Bottom;
            description->Height = 44;
            description->Tag = "muted";

            Panel^ mid = gcnew Panel();
            mid->Dock = DockStyle::Fill;
            mid->Controls->Add(templates);
            mid->Controls->Add(description);

            layout = gcnew Label();
            layout->Dock = DockStyle::Fill;
            layout->Font = gcnew SD::Font("Consolas", 9.0f);
            layout->Padding = WF::Padding(8);

            cols->Controls->Add(Captioned("Language", languages), 0, 0);
            cols->Controls->Add(Captioned("Templates", mid), 1, 0);
            cols->Controls->Add(Captioned("Folder layout created", layout), 2, 0);

            // ---- fields ----
            TableLayoutPanel^ grid = MakeGrid(120);
            grid->Padding = WF::Padding(14, 10, 14, 4);
            nameBox = gcnew TextBox();
            nameBox->Text = "NavFilter";
            nameBox->TextChanged += gcnew EventHandler(this, &NewProjectDialog::OnTemplateChanged);
            AddRow(grid, "Project name", nameBox);
            locationBox = gcnew TextBox();
            locationBox->Text = defaultLocation;
            Button^ browse;
            Panel^ row = BrowseRow(locationBox, browse);
            browse->Click += gcnew EventHandler(this, &NewProjectDialog::OnBrowse);
            AddRow(grid, "Location", row);
            workspaceMode = gcnew ComboBox();
            workspaceMode->DropDownStyle = ComboBoxStyle::DropDownList;
            if (haveWorkspace) workspaceMode->Items->Add("Add to current workspace");
            workspaceMode->Items->Add("Create new workspace");
            workspaceMode->SelectedIndex = 0;
            AddRow(grid, "Workspace", workspaceMode);
            FlowLayoutPanel^ tg = gcnew FlowLayoutPanel();
            tg->Height = 30;
            targetBoxes = gcnew List<CheckBox^>();
            for each (Target t in TargetInfo::All())
            {
                CheckBox^ cb = gcnew CheckBox();
                cb->Text = TargetInfo::Name(t);
                cb->Tag = t;
                cb->AutoSize = true;
                cb->Checked = t == Target::Windows || t == Target::C6747;
                cb->Margin = WF::Padding(0, 4, 18, 0);
                targetBoxes->Add(cb);
                tg->Controls->Add(cb);
            }
            AddRow(grid, "Targets", tg);

            Controls->Add(grid);
            Controls->Add(cols);
            AddOkCancel("Create");
            languages->SelectedIndex = 0;
        }

        // ---- results ----
        property String^ ProjectName { String^ get() { return nameBox->Text->Trim(); } }
        property String^ ProjectLocation { String^ get() { return locationBox->Text->Trim(); } }
        property Language Lang;
        property String^ TemplateName;
        property List<Target>^ Targets;
        property bool NewWorkspace { bool get() { return safe_cast<String^>(workspaceMode->SelectedItem)->StartsWith("Create"); } }
        property bool ImportCcs;

    protected:
        virtual bool OnAccept() override
        {
            if (ImportCcs) return true;
            if (ProjectName->Length == 0 || ProjectName->IndexOfAny(Path::GetInvalidFileNameChars()) >= 0 || ProjectName->Contains(" "))
            {
                MessageBox::Show(this, "Please enter a project name without spaces or special characters.", Text);
                return false;
            }
            if (ProjectLocation->Length == 0) { MessageBox::Show(this, "Please choose a location.", Text); return false; }
            if (File::Exists(Path::Combine(Path::Combine(ProjectLocation, ProjectName), ProjectName + ".pro")))
            {
                MessageBox::Show(this, "A project with this name already exists in that location.", Text);
                return false;
            }
            Targets->Clear();
            for each (CheckBox^ cb in targetBoxes) if (cb->Checked) Targets->Add(safe_cast<Target>(cb->Tag));
            if (Targets->Count == 0) { MessageBox::Show(this, "Select at least one target.", Text); return false; }
            TemplateName = safe_cast<String^>(templates->SelectedItem);
            return true;
        }

    private:
        ListBox^ languages;
        ListBox^ templates;
        Label^ description;
        Label^ layout;
        TextBox^ nameBox;
        TextBox^ locationBox;
        ComboBox^ workspaceMode;
        List<CheckBox^>^ targetBoxes;

        static Control^ Captioned(String^ caption, Control^ c)
        {
            Panel^ p = gcnew Panel();
            p->Dock = DockStyle::Fill;
            p->Padding = WF::Padding(4);
            Label^ l = gcnew Label();
            l->Text = caption->ToUpperInvariant();
            l->Dock = DockStyle::Top;
            l->Height = 22;
            l->Tag = "muted";
            l->Font = gcnew SD::Font("Segoe UI", 8.0f, FontStyle::Bold);
            c->Dock = DockStyle::Fill;
            p->Controls->Add(c);
            p->Controls->Add(l);
            return p;
        }

        void OnLanguageChanged(Object^ sender, EventArgs^ e)
        {
            int i = languages->SelectedIndex;
            ImportCcs = i == 3;
            templates->Items->Clear();
            if (ImportCcs)
            {
                templates->Items->Add("Open CCS 7.4 project (read-only)");
                okButton->Text = "Choose...";
            }
            else
            {
                Lang = i == 1 ? Language::Cpp11 : (i == 2 ? Language::Shalimar : Language::C90);
                for each (String^ t in ProjectTemplates::For(Lang)) templates->Items->Add(t);
                if (okButton != nullptr) okButton->Text = "Create";
            }
            templates->SelectedIndex = 0;
        }

        void OnTemplateChanged(Object^ sender, EventArgs^ e)
        {
            String^ t = safe_cast<String^>(templates->SelectedItem);
            if (t == nullptr) return;
            if (ImportCcs)
            {
                description->Text = "Reads .project / .cproject / .ccsproject. Files are shown and can be edited; the CCS project files are never changed.";
                layout->Text = "<ccs project>\\\n+-- .project\n+-- .cproject\n+-- .ccsproject\n`-- *.c *.h *.cmd";
                return;
            }
            description->Text = ProjectTemplates::Describe(t);
            String^ n = ProjectName;
            if (n->Length == 0) n = "Project";
            String^ ext = ".c";
            if (Lang == Language::Cpp11) ext = ".cpp";
            if (Lang == Language::Shalimar) ext = ".shl";
            StringBuilder^ sb = gcnew StringBuilder();
            sb->Append(n + "\\\n");
            sb->Append("+-- " + n + ".pro\n");
            sb->Append("+-- src\\\n");
            if (t != "Empty Project" && t != "From C Source") sb->Append("|   `-- main" + ext + "\n");
            sb->Append("+-- inc\\\n+-- lib\\\n");
            if (t->Contains("DSP")) sb->Append("+-- c6747.cmd\n");
            sb->Append("`-- out\\<cfg>\\<target>\\\n\n");
            sb->Append("Paths inside the .pro are\nrelative to the .pro folder.");
            layout->Text = sb->ToString();
        }

        void OnBrowse(Object^ sender, EventArgs^ e)
        {
            FolderBrowserDialog^ d = gcnew FolderBrowserDialog();
            d->SelectedPath = locationBox->Text;
            if (d->ShowDialog(this) == WF::DialogResult::OK) locationBox->Text = d->SelectedPath;
        }
    };

    // -------------------------------------------------------------------------
    public ref class NewItemDialog : public ThemedDialog
    {
    public:
        NewItemDialog(GrideProject^ p) : ThemedDialog("Add New Item - " + p->Name, 620, 420)
        {
            project = p;
            list = gcnew ListBox();
            list->Dock = DockStyle::Fill;
            list->IntegralHeight = false;
            list->ItemHeight = 22;
            for each (ItemTemplate^ t in ProjectTemplates::Items()) list->Items->Add(t);
            list->SelectedIndexChanged += gcnew EventHandler(this, &NewItemDialog::OnTemplate);

            TableLayoutPanel^ grid = MakeGrid(90);
            grid->Dock = DockStyle::Bottom;
            nameBox = gcnew TextBox();
            AddRow(grid, "Name", nameBox);
            folderBox = gcnew ComboBox();
            for each (ProjectFolder^ f in p->Folders) folderBox->Items->Add(f->Name);
            AddRow(grid, "Folder", folderBox);

            Panel^ host = gcnew Panel();
            host->Dock = DockStyle::Fill;
            host->Padding = WF::Padding(10);
            host->Controls->Add(list);
            Controls->Add(host);
            Controls->Add(grid);
            host->BringToFront();
            AddOkCancel("Add");
            int first = 0;
            if (p->Lang == Language::Cpp11) first = 2;
            if (p->Lang == Language::Shalimar) first = 4;
            if (first < list->Items->Count) list->SelectedIndex = first;
            else if (list->Items->Count > 0) list->SelectedIndex = 0;
        }

        property String^ FilePath;
        property String^ Folder { String^ get() { return folderBox->Text->Trim(); } }

    protected:
        virtual bool OnAccept() override
        {
            ItemTemplate^ t = safe_cast<ItemTemplate^>(list->SelectedItem);
            if (t == nullptr) { MessageBox::Show(this, "Please select an item template.", Text); return false; }
            String^ name = nameBox->Text->Trim();
            if (name->Length == 0 || name->IndexOfAny(Path::GetInvalidFileNameChars()) >= 0)
            {
                MessageBox::Show(this, "Please enter a valid file name.", Text);
                return false;
            }
            if (!Path::HasExtension(name)) name = name + t->Extension;
            String^ folder = Folder->Length > 0 ? Folder : t->Folder;
            String^ dir = project->ProjectDir;
            if (folder != "linker" && folder != "doc") dir = Path::Combine(dir, folder);
            FilePath = Path::Combine(dir, name);
            if (File::Exists(FilePath) && project->ContainsFile(FilePath))
            {
                MessageBox::Show(this, "This file is already part of the project.", Text);
                return false;
            }
            return true;
        }

    private:
        GrideProject^ project;
        ListBox^ list;
        TextBox^ nameBox;
        ComboBox^ folderBox;

        void OnTemplate(Object^ sender, EventArgs^ e)
        {
            ItemTemplate^ t = safe_cast<ItemTemplate^>(list->SelectedItem);
            if (t == nullptr) return;
            String^ stem = "newfile";
            if (nameBox->Text->Trim()->Length > 0) stem = Path::GetFileNameWithoutExtension(nameBox->Text->Trim());
            nameBox->Text = stem + t->Extension;
            folderBox->Text = t->Folder;
        }
    };

    // -------------------------------------------------------------------------
    public ref class ProjectPropertiesDialog : public ThemedDialog
    {
    public:
        ProjectPropertiesDialog(GrideProject^ p, Workspace^ w, String^ config, Target target)
            : ThemedDialog(p->Name + " Property Pages", 900, 600)
        {
            project = p; workspace = w;
            FormBorderStyle = WF::FormBorderStyle::Sizable;
            MinimumSize = SD::Size(700, 450);
            clones = gcnew List<BuildConfiguration^>();
            for each (BuildConfiguration^ c in p->Configurations) clones->Add(c->Clone());

            FlowLayoutPanel^ top = gcnew FlowLayoutPanel();
            top->Dock = DockStyle::Top;
            top->Height = 40;
            top->Padding = WF::Padding(8, 8, 8, 0);
            top->Controls->Add(MakeLabel("Configuration:"));
            configBox = gcnew ComboBox();
            configBox->DropDownStyle = ComboBoxStyle::DropDownList;
            configBox->Items->AddRange(gcnew array<Object^>{ "Debug", "Release" });
            configBox->SelectedItem = config;
            if (configBox->SelectedIndex < 0) configBox->SelectedIndex = 0;
            top->Controls->Add(configBox);
            top->Controls->Add(MakeLabel("   Target:"));
            targetBox = gcnew ComboBox();
            targetBox->DropDownStyle = ComboBoxStyle::DropDownList;
            targetBox->Width = 180;
            for each (Target t in TargetInfo::All()) targetBox->Items->Add(TargetInfo::Name(t));
            targetBox->SelectedItem = TargetInfo::Name(target);
            if (targetBox->SelectedIndex < 0) targetBox->SelectedIndex = 0;
            top->Controls->Add(targetBox);
            configBox->SelectedIndexChanged += gcnew EventHandler(this, &ProjectPropertiesDialog::OnSelectionChanged);
            targetBox->SelectedIndexChanged += gcnew EventHandler(this, &ProjectPropertiesDialog::OnSelectionChanged);

            SplitContainer^ split = gcnew SplitContainer();
            split->Size = SD::Size(900, 500);
            split->Dock = DockStyle::Fill;
            split->SplitterDistance = 230;
            split->FixedPanel = WF::FixedPanel::Panel1;

            targetsList = gcnew CheckedListBox();
            targetsList->Dock = DockStyle::Top;
            targetsList->Height = 100;
            targetsList->CheckOnClick = true;
            for each (Target t in TargetInfo::All())
                targetsList->Items->Add(TargetInfo::Name(t), p->Targets->Contains(t));
            depsList = gcnew CheckedListBox();
            depsList->Dock = DockStyle::Fill;
            depsList->CheckOnClick = true;
            if (w != nullptr)
                for each (GrideProject^ o in w->Projects)
                    if (o != p) depsList->Items->Add(o->Name, p->DependsOn->Contains(o->Name));
            split->Panel1->Padding = WF::Padding(8);
            split->Panel1->Controls->Add(depsList);
            split->Panel1->Controls->Add(Heading("Depends on (built first)"));
            split->Panel1->Controls->Add(targetsList);
            split->Panel1->Controls->Add(Heading("Targets of this project"));

            grid = gcnew PropertyGrid();
            grid->Dock = DockStyle::Fill;
            grid->ToolbarVisible = false;
            grid->PropertySort = WF::PropertySort::Categorized;
            split->Panel2->Padding = WF::Padding(4, 8, 8, 8);
            split->Panel2->Controls->Add(grid);

            Controls->Add(split);
            Controls->Add(top);
            split->BringToFront();
            AddOkCancel("OK");
            if (p->IsReadOnly)
            {
                Text = Text + "  (read-only CCS project)";
                okButton->Enabled = false;
            }
            ShowCurrent();
        }

    protected:
        virtual bool OnAccept() override
        {
            project->Configurations->Clear();
            project->Configurations->AddRange(clones);
            project->Targets->Clear();
            for (int i = 0; i < targetsList->Items->Count; i++)
                if (targetsList->GetItemChecked(i))
                    project->Targets->Add(TargetInfo::Parse(safe_cast<String^>(targetsList->Items[i])));
            project->DependsOn->Clear();
            for each (Object^ o in depsList->CheckedItems) project->DependsOn->Add(safe_cast<String^>(o));
            project->IsDirty = true;
            project->Save();
            return true;
        }

    private:
        GrideProject^ project;
        Workspace^ workspace;
        List<BuildConfiguration^>^ clones;
        ComboBox^ configBox;
        ComboBox^ targetBox;
        CheckedListBox^ targetsList;
        CheckedListBox^ depsList;
        PropertyGrid^ grid;

        static Label^ MakeLabel(String^ s)
        {
            Label^ l = gcnew Label();
            l->Text = s; l->AutoSize = true; l->Margin = WF::Padding(0, 6, 4, 0);
            return l;
        }

        void ShowCurrent()
        {
            String^ cfg = safe_cast<String^>(configBox->SelectedItem);
            Target t = TargetInfo::Parse(safe_cast<String^>(targetBox->SelectedItem));
            String^ key = ConfigName::Make(cfg, t);
            BuildConfiguration^ found = nullptr;
            for each (BuildConfiguration^ c in clones) if (String::Compare(c->Key, key, true) == 0) found = c;
            if (found == nullptr) { found = gcnew BuildConfiguration(cfg, t); clones->Add(found); }
            grid->SelectedObject = gcnew ConfigurationView(found);
        }

        void OnSelectionChanged(Object^ sender, EventArgs^ e) { ShowCurrent(); }
    };

    // -------------------------------------------------------------------------
    public ref class ConfigurationManagerDialog : public ThemedDialog
    {
    public:
        ConfigurationManagerDialog(Workspace^ w) : ThemedDialog("Configuration Manager", 560, 420)
        {
            workspace = w;
            TableLayoutPanel^ grid = MakeGrid(170);
            configBox = gcnew ComboBox();
            configBox->DropDownStyle = ComboBoxStyle::DropDownList;
            configBox->Items->AddRange(gcnew array<Object^>{ "Debug", "Release" });
            configBox->SelectedItem = w->ActiveConfig;
            if (configBox->SelectedIndex < 0) configBox->SelectedIndex = 0;
            AddRow(grid, "Active configuration", configBox);
            targetBox = gcnew ComboBox();
            targetBox->DropDownStyle = ComboBoxStyle::DropDownList;
            for each (Target t in TargetInfo::All()) targetBox->Items->Add(TargetInfo::Name(t));
            targetBox->SelectedItem = TargetInfo::Name(w->ActiveTarget);
            if (targetBox->SelectedIndex < 0) targetBox->SelectedIndex = 0;
            targetBox->SelectedIndexChanged += gcnew EventHandler(this, &ConfigurationManagerDialog::OnTargetChanged);
            AddRow(grid, "Active target", targetBox);

            projects = gcnew CheckedListBox();
            projects->Dock = DockStyle::Fill;
            projects->CheckOnClick = true;
            Panel^ host = gcnew Panel();
            host->Dock = DockStyle::Fill;
            host->Padding = WF::Padding(10);
            host->Controls->Add(projects);
            host->Controls->Add(Heading("Projects built for the selected target"));
            Controls->Add(host);
            Controls->Add(grid);
            host->BringToFront();
            AddOkCancel("OK");
            changes = gcnew Dictionary<String^, bool>();
            FillProjects();
        }

    protected:
        virtual bool OnAccept() override
        {
            StoreChecks();
            workspace->ActiveConfig = safe_cast<String^>(configBox->SelectedItem);
            workspace->ActiveTarget = TargetInfo::Parse(safe_cast<String^>(targetBox->SelectedItem));
            for each (KeyValuePair<String^, bool> kv in changes)
            {
                array<String^>^ parts = kv.Key->Split(L'|');
                GrideProject^ p = workspace->FindProject(parts[0]);
                if (p == nullptr || p->IsReadOnly) continue;
                Target t = TargetInfo::Parse(parts[1]);
                if (kv.Value && !p->Targets->Contains(t)) p->Targets->Add(t);
                if (!kv.Value) p->Targets->Remove(t);
                p->IsDirty = true;
            }
            workspace->SaveProjects();
            workspace->IsDirty = true;
            return true;
        }

    private:
        Workspace^ workspace;
        ComboBox^ configBox;
        ComboBox^ targetBox;
        CheckedListBox^ projects;
        Dictionary<String^, bool>^ changes;
        String^ shownTarget;

        void FillProjects()
        {
            shownTarget = safe_cast<String^>(targetBox->SelectedItem);
            Target t = TargetInfo::Parse(shownTarget);
            projects->Items->Clear();
            for each (GrideProject^ p in workspace->Projects)
            {
                bool on;
                if (!changes->TryGetValue(p->Name + "|" + shownTarget, on)) on = p->Targets->Contains(t);
                projects->Items->Add(p->Name, on);
            }
        }

        void StoreChecks()
        {
            if (shownTarget == nullptr) return;
            for (int i = 0; i < projects->Items->Count; i++)
                changes[safe_cast<String^>(projects->Items[i]) + "|" + shownTarget] = projects->GetItemChecked(i);
        }

        void OnTargetChanged(Object^ sender, EventArgs^ e) { StoreChecks(); FillProjects(); }
    };

    // -------------------------------------------------------------------------
    public ref class DependenciesDialog : public ThemedDialog
    {
    public:
        DependenciesDialog(Workspace^ w, GrideProject^ selected) : ThemedDialog("Project Dependencies", 480, 420)
        {
            workspace = w;
            deps = gcnew Dictionary<String^, List<String^>^>();
            for each (GrideProject^ p in w->Projects) deps[p->Name] = gcnew List<String^>(p->DependsOn);
            TableLayoutPanel^ grid = MakeGrid(80);
            projectBox = gcnew ComboBox();
            projectBox->DropDownStyle = ComboBoxStyle::DropDownList;
            for each (GrideProject^ p in w->Projects) projectBox->Items->Add(p->Name);
            AddRow(grid, "Projects", projectBox);
            list = gcnew CheckedListBox();
            list->Dock = DockStyle::Fill;
            list->CheckOnClick = true;
            list->ItemCheck += gcnew ItemCheckEventHandler(this, &DependenciesDialog::OnItemCheck);
            Panel^ host = gcnew Panel();
            host->Dock = DockStyle::Fill;
            host->Padding = WF::Padding(10);
            host->Controls->Add(list);
            host->Controls->Add(Heading("Depends on:"));
            Controls->Add(host);
            Controls->Add(grid);
            host->BringToFront();
            AddOkCancel("OK");
            projectBox->SelectedIndexChanged += gcnew EventHandler(this, &DependenciesDialog::OnProjectChanged);
            if (selected != nullptr) projectBox->SelectedItem = selected->Name;
            if (projectBox->SelectedIndex < 0 && projectBox->Items->Count > 0) projectBox->SelectedIndex = 0;
        }

    protected:
        virtual bool OnAccept() override
        {
            for each (GrideProject^ p in workspace->Projects)
            {
                if (p->IsReadOnly) continue;
                p->DependsOn = deps[p->Name];
                p->IsDirty = true;
            }
            workspace->SaveProjects();
            return true;
        }

    private:
        Workspace^ workspace;
        Dictionary<String^, List<String^>^>^ deps;
        ComboBox^ projectBox;
        CheckedListBox^ list;
        bool filling;

        void OnProjectChanged(Object^ sender, EventArgs^ e)
        {
            filling = true;
            list->Items->Clear();
            String^ cur = safe_cast<String^>(projectBox->SelectedItem);
            for each (GrideProject^ p in workspace->Projects)
                if (p->Name != cur) list->Items->Add(p->Name, deps[cur]->Contains(p->Name));
            filling = false;
        }

        void OnItemCheck(Object^ sender, ItemCheckEventArgs^ e)
        {
            if (filling) return;
            String^ cur = safe_cast<String^>(projectBox->SelectedItem);
            String^ other = safe_cast<String^>(list->Items[e->Index]);
            deps[cur]->Remove(other);
            if (e->NewValue == CheckState::Checked) deps[cur]->Add(other);
        }
    };

    // -------------------------------------------------------------------------
    public ref class BatchItem
    {
    public:
        BatchItem(GrideProject^ p, String^ c, Target t) { Project = p; Config = c; TargetKind = t; }
        property GrideProject^ Project;
        property String^ Config;
        property Target TargetKind;
        virtual String^ ToString() override { return String::Format("{0}  |  {1}  |  {2}", Project->Name, Config, TargetInfo::Name(TargetKind)); }
    };

    public ref class BatchBuildDialog : public ThemedDialog
    {
    public:
        BatchBuildDialog(Workspace^ w) : ThemedDialog("Batch Build", 560, 460)
        {
            Selected = gcnew List<BatchItem^>();
            Action = BuildAction::Build;
            list = gcnew CheckedListBox();
            list->Dock = DockStyle::Fill;
            list->CheckOnClick = true;
            for each (GrideProject^ p in w->Projects)
                for each (Target t in TargetInfo::All())
                    if (p->SupportsTarget(t))
                    {
                        list->Items->Add(gcnew BatchItem(p, "Debug", t), false);
                        list->Items->Add(gcnew BatchItem(p, "Release", t), false);
                    }
            Panel^ host = gcnew Panel();
            host->Dock = DockStyle::Fill;
            host->Padding = WF::Padding(10);
            host->Controls->Add(list);
            host->Controls->Add(Heading("Check the project configurations to build:"));
            Controls->Add(host);
            host->BringToFront();

            Button^ cancel = AddButton("Close");
            cancel->DialogResult = WF::DialogResult::Cancel;
            CancelButton = cancel;
            AddAction("Clean")->Tag = "clean";
            AddAction("Rebuild")->Tag = "rebuild";
            Button^ build = AddAction("Build");
            build->Tag = "accent";
            Button^ none = AddButton("Deselect All");
            none->Click += gcnew EventHandler(this, &BatchBuildDialog::OnNone);
            Button^ all = AddButton("Select All");
            all->Click += gcnew EventHandler(this, &BatchBuildDialog::OnAll);
        }

        property List<BatchItem^>^ Selected;
        property BuildAction Action;

    private:
        CheckedListBox^ list;

        Button^ AddAction(String^ text)
        {
            Button^ b = AddButton(text);
            b->Click += gcnew EventHandler(this, &BatchBuildDialog::OnAction);
            return b;
        }

        void OnAction(Object^ sender, EventArgs^ e)
        {
            Button^ b = safe_cast<Button^>(sender);
            if (b->Text == "Clean") Action = BuildAction::Clean;
            else if (b->Text == "Rebuild") Action = BuildAction::Rebuild;
            else Action = BuildAction::Build;
            Selected->Clear();
            for each (Object^ o in list->CheckedItems) Selected->Add(safe_cast<BatchItem^>(o));
            if (Selected->Count == 0) { MessageBox::Show(this, "Nothing is checked.", Text); return; }
            DialogResult = WF::DialogResult::OK;
            Close();
        }
        void OnAll(Object^ sender, EventArgs^ e) { for (int i = 0; i < list->Items->Count; i++) list->SetItemChecked(i, true); }
        void OnNone(Object^ sender, EventArgs^ e) { for (int i = 0; i < list->Items->Count; i++) list->SetItemChecked(i, false); }
    };
}}
