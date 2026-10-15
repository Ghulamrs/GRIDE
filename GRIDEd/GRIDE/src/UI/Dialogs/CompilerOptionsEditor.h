// =============================================================================
//  CompilerOptionsEditor.h - RIDE's Compiler Options dialog as a panel: one tab per
//  toolchain.json compilerOptions tab, a control per option (check box, choice, text),
//  the configuration edited chosen above, an option that means nothing for the target
//  greyed with the reason as its tip, the command line the tab's compiler would get
//  shown below, "Restore defaults" per configuration. Edits a draft of a store; Commit
//  copies the draft back. Used by Tools > Options > Projects > Compiler Options (the
//  settings' store) and Project > Properties (the project's).
// =============================================================================
#pragma once
#include "DialogBase.h"
#include "../../Settings/GrideSettings.h"

namespace Gride { namespace UI {

    using namespace Gride::Core;
    using namespace Gride::Settings;
    using namespace Gride::Services;

    public ref class CompilerOptionsEditor : public Panel
    {
    public:
        CompilerOptionsEditor(CompilerOptionStore^ store, String^ config, Target target, bool readOnly)
        {
            original = store;
            draft = store == nullptr ? gcnew CompilerOptionStore() : store->Clone();
            this->target = target;
            this->readOnly = readOnly;
            controls = gcnew Dictionary<String^, Control^>();
            tips = gcnew ToolTip();
            pushing = false;
            Dock = DockStyle::Fill;

            FlowLayoutPanel^ top = gcnew FlowLayoutPanel();
            top->Dock = DockStyle::Top;
            top->Height = 34;
            top->Padding = WF::Padding(4, 6, 4, 0);
            Label^ forLabel = gcnew Label();
            forLabel->Text = "Edit options for:"; forLabel->AutoSize = true; forLabel->Margin = WF::Padding(0, 5, 6, 0);
            top->Controls->Add(forLabel);
            configPick = gcnew ComboBox();
            configPick->DropDownStyle = ComboBoxStyle::DropDownList;
            for each (String^ c in CompilerOptionStore::Configurations()) configPick->Items->Add(c);
            configPick->SelectedItem = CompilerOptionStore::Canon(config);
            configPick->Width = 110;
            configPick->SelectedIndexChanged += gcnew EventHandler(this, &CompilerOptionsEditor::OnConfigChosen);
            top->Controls->Add(configPick);
            targetLabel = gcnew Label();
            targetLabel->AutoSize = true; targetLabel->Margin = WF::Padding(16, 5, 0, 0); targetLabel->Tag = "muted";
            top->Controls->Add(targetLabel);

            tabs = gcnew TabControl();
            tabs->Dock = DockStyle::Fill;
            ToolchainDefinitions^ defs = ToolchainDefinitions::Current;
            for each (String^ tab in defs->CompilerOptionTabs)
            {
                TabPage^ page = gcnew TabPage(tab);
                page->AutoScroll = true;
                int y = 14;
                for each (CompilerOptionDefinition^ o in defs->CompilerOptionsOf(tab))
                {
                    Control^ made;
                    if (o->IsCheck())
                    {
                        CheckBox^ box = gcnew CheckBox();
                        box->Text = o->Label; box->AutoSize = false; box->SetBounds(12, y, 600, 24);
                        box->CheckedChanged += gcnew EventHandler(this, &CompilerOptionsEditor::OnChanged);
                        made = box;
                    }
                    else
                    {
                        Label^ title = gcnew Label();
                        title->Text = o->Label; title->SetBounds(12, y + 4, 230, 20);
                        page->Controls->Add(title);
                        if (o->IsChoice())
                        {
                            ComboBox^ pick = gcnew ComboBox();
                            pick->DropDownStyle = ComboBoxStyle::DropDownList;
                            for each (String^ c in o->Choices) pick->Items->Add(c);
                            pick->SetBounds(250, y, 200, 24);
                            pick->SelectedIndexChanged += gcnew EventHandler(this, &CompilerOptionsEditor::OnChanged);
                            made = pick;
                        }
                        else
                        {
                            TextBox^ field = gcnew TextBox();
                            field->SetBounds(250, y, 380, 24);
                            field->TextChanged += gcnew EventHandler(this, &CompilerOptionsEditor::OnChanged);
                            made = field;
                        }
                    }
                    made->Tag = o->Id;
                    page->Controls->Add(made);
                    controls[o->Id] = made;
                    y += 34;
                }
                tabs->TabPages->Add(page);
            }
            tabs->SelectedIndexChanged += gcnew EventHandler(this, &CompilerOptionsEditor::OnTabChosen);

            Panel^ bottom = gcnew Panel();
            bottom->Dock = DockStyle::Bottom;
            bottom->Height = 96;
            Label^ heading = gcnew Label();
            heading->Text = "Command line:"; heading->SetBounds(4, 4, 200, 18);
            bottom->Controls->Add(heading);
            preview = gcnew TextBox();
            preview->ReadOnly = true; preview->Multiline = true;
            preview->Font = gcnew SD::Font("Consolas", 9.0f);
            preview->SetBounds(4, 24, 640, 40);
            preview->Anchor = static_cast<AnchorStyles>(AnchorStyles::Left | AnchorStyles::Right | AnchorStyles::Top);
            bottom->Controls->Add(preview);
            reset = gcnew Button();
            reset->Text = "Restore defaults"; reset->SetBounds(4, 68, 140, 26);
            reset->Click += gcnew EventHandler(this, &CompilerOptionsEditor::OnRestore);
            reset->Enabled = !readOnly;
            bottom->Controls->Add(reset);

            Controls->Add(tabs);
            Controls->Add(bottom);
            Controls->Add(top);
            tabs->BringToFront();
            SetTarget(target);
        }

        property CompilerOptionStore^ Draft { CompilerOptionStore^ get() { return draft; } }
        property String^ Config { String^ get() { return safe_cast<String^>(configPick->SelectedItem); } }

        // the target availability is judged for (Project > Properties changes it)
        void SetTarget(Target t)
        {
            target = t;
            targetLabel->Text = "Target: " + RideToolchain::Arch(t) + " (" + TargetInfo::Name(t) + ")";
            Push();
        }

        // the draft into the store it came from
        void Commit()
        {
            if (original != nullptr && !readOnly) original->CopyFrom(draft);
        }

    private:
        CompilerOptionStore^ original;
        CompilerOptionStore^ draft;
        Target target;
        bool readOnly;
        bool pushing;
        Dictionary<String^, Control^>^ controls;
        ToolTip^ tips;
        ComboBox^ configPick;
        Label^ targetLabel;
        TabControl^ tabs;
        TextBox^ preview;
        Button^ reset;

        // the draft's values for the configuration shown, into the controls; the tips and the greying
        void Push()
        {
            pushing = true;
            String^ config = Config;
            for each (KeyValuePair<String^, Control^> pair in controls)
            {
                String^ value = draft->Value(config, pair.Key);
                CheckBox^ box = dynamic_cast<CheckBox^>(pair.Value);
                ComboBox^ pick = dynamic_cast<ComboBox^>(pair.Value);
                TextBox^ field = dynamic_cast<TextBox^>(pair.Value);
                if (box != nullptr) box->Checked = value == "1";
                else if (pick != nullptr) { pick->SelectedItem = value; if (pick->SelectedIndex < 0 && pick->Items->Count > 0) pick->SelectedIndex = 0; }
                else if (field != nullptr) field->Text = value;
                String^ why;
                bool usable = RideToolchain::OptionAvailable(pair.Key, target, why);
                CompilerOptionDefinition^ o = ToolchainDefinitions::Current->CompilerOption(pair.Key);
                String^ tip = readOnly ? "Read from the CCS project - edit it in CCS" : usable ? (o == nullptr ? String::Empty : o->Hint) : "Unavailable: " + why;
                pair.Value->Enabled = usable && !readOnly;
                tips->SetToolTip(pair.Value, tip);
            }
            pushing = false;
            ShowPreview();
        }

        void Pull(Control^ c)
        {
            String^ id = dynamic_cast<String^>(c->Tag);
            if (id == nullptr) return;
            CheckBox^ box = dynamic_cast<CheckBox^>(c);
            ComboBox^ pick = dynamic_cast<ComboBox^>(c);
            TextBox^ field = dynamic_cast<TextBox^>(c);
            String^ value = box != nullptr ? (box->Checked ? "1" : "0") : pick != nullptr ? dynamic_cast<String^>(pick->SelectedItem) : field != nullptr ? field->Text : nullptr;
            if (value != nullptr) draft->Set(Config, id, value);
        }

        // the tab's compiler and the flags it would be given (RIDE's preview line)
        void ShowPreview()
        {
            if (tabs->SelectedTab == nullptr) { preview->Text = String::Empty; return; }
            ToolchainDefinitions^ defs = ToolchainDefinitions::Current;
            ToolDefinition^ tool = nullptr;
            for each (CompilerOptionDefinition^ o in defs->CompilerOptionsOf(tabs->SelectedTab->Text))
            {
                tool = defs->ToolOfOption(o->Id);
                if (tool != nullptr && tool->Role != "cCompiler" && o->ToolWord()->Length == 0) continue;   // general.*: cpp11 shows them, as in RIDE
                if (tool != nullptr) break;
            }
            if (tool == nullptr) tool = defs->ToolByRole("cppCompiler");
            if (tool == nullptr) { preview->Text = String::Empty; return; }
            String^ archFlag = tool->ArchFlag->Replace("$(Arch)", RideToolchain::Arch(target));
            String^ flags = RideToolchain::OptionFlags(draft, tool, Config, target);
            preview->Text = (tool->Name + " " + archFlag + " " + flags)->Trim();
        }

        void OnChanged(Object^ sender, EventArgs^ e)
        {
            if (pushing) return;
            Pull(safe_cast<Control^>(sender));
            ShowPreview();
        }
        void OnConfigChosen(Object^ sender, EventArgs^ e) { Push(); }
        void OnTabChosen(Object^ sender, EventArgs^ e) { ShowPreview(); }
        void OnRestore(Object^ sender, EventArgs^ e) { draft->Reset(Config); Push(); }
    };
}}
