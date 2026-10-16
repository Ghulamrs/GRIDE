// =============================================================================
//  StartPage.h - shown in the document area when no file is open
// =============================================================================
#pragma once
#include "../Theme.h"

namespace Gride { namespace UI {

    public delegate void PathHandler(String^ path);

    public ref class StartPage : public UserControl
    {
    public:
        StartPage()
        {
            Dock = DockStyle::Fill;
            AutoScroll = true;
            layout = gcnew FlowLayoutPanel();
            layout->FlowDirection = FlowDirection::TopDown;
            layout->WrapContents = false;
            layout->AutoSize = true;
            layout->Location = SD::Point(48, 40);
            layout->Padding = WF::Padding(0);
            Controls->Add(layout);
        }

        event PathHandler^ CommandRequested;     // command id, e.g. "File.NewProject"
        event PathHandler^ RecentRequested;      // full path of a workspace / project

        void Build(List<String^>^ recentWorkspaces)
        {
            layout->Controls->Clear();
            Theme^ t = ThemeManager::Current;

            Label^ title = gcnew Label();
            title->Text = "GRIDE";
            title->Font = gcnew SD::Font("Segoe UI Semibold", 30.0f);
            title->AutoSize = true;
            title->ForeColor = t->Accent;
            title->Tag = "keep";
            layout->Controls->Add(title);

            Label^ sub = gcnew Label();
            // middle dots as \x00B7 escapes: the source is UTF-8 without BOM and the project has no /utf-8
            sub->Text = L"C90  \x00B7  C++11  \x00B7  Shalimar      Windows  \x00B7  Linux  \x00B7  macOS  \x00B7  TMS320C6747";
            sub->Font = gcnew SD::Font("Segoe UI", 11.0f);
            sub->AutoSize = true;
            sub->Tag = "muted";
            sub->Margin = WF::Padding(3, 0, 3, 28);
            layout->Controls->Add(sub);

            AddHeading("Start");
            AddLink("New Project...", "File.NewProject", true);
            AddLink("Open Workspace / Project...", "File.OpenWorkspace", true);
            AddLink("Open File...", "File.OpenFile", true);
            AddLink("Import CCS 7.4 / 5.5 Project...", "File.OpenCcs", true);

            AddHeading("Recent");
            if (recentWorkspaces == nullptr || recentWorkspaces->Count == 0)
            {
                Label^ none = gcnew Label();
                none->Text = "No recent workspaces yet.";
                none->AutoSize = true;
                none->Tag = "muted";
                layout->Controls->Add(none);
            }
            else
                for each (String^ p in recentWorkspaces) AddLink(p, p, false);

            ThemeManager::Apply(this);
            title->ForeColor = t->Accent;
        }

    private:
        FlowLayoutPanel^ layout;

        void AddHeading(String^ text)
        {
            Label^ h = gcnew Label();
            h->Text = text;
            h->Font = gcnew SD::Font("Segoe UI Semibold", 12.0f);
            h->AutoSize = true;
            h->Margin = WF::Padding(3, 18, 3, 6);
            layout->Controls->Add(h);
        }

        void AddLink(String^ text, String^ key, bool isCommand)
        {
            LinkLabel^ l = gcnew LinkLabel();
            l->Text = text;
            l->AutoSize = true;
            l->Font = gcnew SD::Font("Segoe UI", 10.5f);
            l->LinkColor = ThemeManager::Current->Accent;
            l->ActiveLinkColor = ThemeManager::Current->Text;
            l->LinkBehavior = LinkBehavior::HoverUnderline;
            l->Tag = "keep";
            l->Margin = WF::Padding(3, 3, 3, 3);
            if (isCommand) l->Name = "cmd:" + key;
            else l->Name = "path:" + key;
            l->LinkClicked += gcnew LinkLabelLinkClickedEventHandler(this, &StartPage::OnLink);
            layout->Controls->Add(l);
        }

        void OnLink(Object^ sender, LinkLabelLinkClickedEventArgs^ e)
        {
            LinkLabel^ l = safe_cast<LinkLabel^>(sender);
            if (l->Name->StartsWith("cmd:")) CommandRequested(l->Name->Substring(4));
            else RecentRequested(l->Name->Substring(5));
        }
    };
}}
