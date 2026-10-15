// =============================================================================
//  DockArea.h - simple, dependency-free docking
//
//  ToolPane    base class of every tool window (Project Explorer, Output ...)
//  DockArea    one region of the main window (left / right / bottom) that
//              hosts several panes as tabs, with a header (title, float, close)
//  FloatWindow a small tool window a pane moves into when it is floated;
//              closing it docks the pane back into its home area
// =============================================================================
#pragma once
#include "../Controls/TabStrip.h"
#include "../IconFactory.h"

namespace Gride { namespace UI {

    ref class DockArea;

    // -------------------------------------------------------------------------
    public ref class ToolPane : public UserControl
    {
    public:
        ToolPane(String^ key, String^ title, String^ icon)
        {
            Key = key; Title = title; IconName = icon;
            Dock = DockStyle::Fill;
            IsHidden = false;
            IsFloating = false;
            Home = nullptr;
        }
        property String^ Key;          // stable id stored in layout settings
        property String^ Title;
        property String^ IconName;
        property DockArea^ Home;
        property bool IsHidden;
        property bool IsFloating;

        // panes with custom drawing override this to refresh after a theme change
        virtual void ApplyTheme() { ThemeManager::Apply(this); }
    };

    // -------------------------------------------------------------------------
    public ref class FloatWindow : public Form
    {
    public:
        FloatWindow(ToolPane^ pane, Form^ owner)
        {
            this->pane = pane;
            Text = pane->Title;
            FormBorderStyle = WF::FormBorderStyle::SizableToolWindow;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition::Manual;
            Size = SD::Size(420, 320);
            Location = SD::Point(WF::Cursor::Position.X - 200, WF::Cursor::Position.Y - 20);
            Owner = owner;
            Controls->Add(pane);
            pane->Dock = DockStyle::Fill;
            ThemeManager::Apply(this);
            pane->ApplyTheme();
        }
        property ToolPane^ Pane { ToolPane^ get() { return pane; } }
    private:
        ToolPane^ pane;
    };

    // -------------------------------------------------------------------------
    public ref class DockArea : public UserControl
    {
    public:
        DockArea(String^ name)
        {
            AreaName = name;
            panes = gcnew List<ToolPane^>();
            floats = gcnew Dictionary<ToolPane^, FloatWindow^>();

            header = gcnew Panel();
            header->Dock = DockStyle::Top;
            header->Height = 26;
            header->Tag = "header";
            header->Paint += gcnew PaintEventHandler(this, &DockArea::OnHeaderPaint);
            header->MouseDown += gcnew MouseEventHandler(this, &DockArea::OnHeaderMouseDown);
            header->DoubleClick += gcnew EventHandler(this, &DockArea::OnHeaderDoubleClick);

            content = gcnew Panel();
            content->Dock = DockStyle::Fill;

            tabs = gcnew TabStrip(TabStripStyle::Panes);
            tabs->Dock = DockStyle::Bottom;
            tabs->SelectedChanged += gcnew EventHandler(this, &DockArea::OnTabChanged);
            tabs->TabDoubleClicked += gcnew TabEventHandler(this, &DockArea::OnTabDoubleClicked);

            Controls->Add(content);
            Controls->Add(tabs);
            Controls->Add(header);
        }

        property String^ AreaName;
        event EventHandler^ VisibleCountChanged;

        property bool HasVisiblePanes { bool get() { return tabs->Items->Count > 0; } }
        property List<ToolPane^>^ Panes { List<ToolPane^>^ get() { return panes; } }
        property ToolPane^ ActivePane
        {
            ToolPane^ get()
            {
                TabItem^ t = tabs->SelectedItem;
                if (t == nullptr) return nullptr;
                return safe_cast<ToolPane^>(t->Tag);
            }
        }

        void AddPane(ToolPane^ p, bool hidden)
        {
            p->Home = this;
            panes->Add(p);
            p->IsHidden = hidden;
            p->Visible = false;
            content->Controls->Add(p);
            if (!hidden) AddTab(p);
            Refresh();
        }

        void ShowPane(ToolPane^ p)
        {
            if (p->IsFloating) { floats[p]->Activate(); return; }
            if (p->IsHidden) { p->IsHidden = false; AddTab(p); }
            int i = tabs->IndexOfTag(p);
            if (i >= 0) tabs->SelectedIndex = i;
            ShowSelected();
            VisibleCountChanged(this, EventArgs::Empty);
        }

        void HidePane(ToolPane^ p)
        {
            if (p->IsFloating) DockPaneInternal(p, true);
            p->IsHidden = true;
            int i = tabs->IndexOfTag(p);
            if (i >= 0) tabs->Remove(tabs->Items[i]);
            p->Visible = false;
            ShowSelected();
            VisibleCountChanged(this, EventArgs::Empty);
        }

        void FloatPane(ToolPane^ p)
        {
            if (p->IsFloating) return;
            int i = tabs->IndexOfTag(p);
            if (i >= 0) tabs->Remove(tabs->Items[i]);
            content->Controls->Remove(p);
            p->IsFloating = true;
            p->Visible = true;
            FloatWindow^ w = gcnew FloatWindow(p, FindForm());
            w->FormClosing += gcnew FormClosingEventHandler(this, &DockArea::OnFloatClosing);
            floats[p] = w;
            w->Show();
            ShowSelected();
            VisibleCountChanged(this, EventArgs::Empty);
        }

        void DockPane(ToolPane^ p) { DockPaneInternal(p, true); }

        void DockAll()
        {
            for each (ToolPane^ p in gcnew List<ToolPane^>(panes)) if (p->IsFloating) DockPane(p);
        }

        bool IsShown(ToolPane^ p) { return p->IsFloating || !p->IsHidden; }

        void ApplyTheme()
        {
            ThemeManager::Apply(this);
            for each (ToolPane^ p in panes) p->ApplyTheme();
            for each (FloatWindow^ w in floats->Values) ThemeManager::Apply(w);
            header->Invalidate();
            tabs->Invalidate();
        }

    private:
        List<ToolPane^>^ panes;
        Dictionary<ToolPane^, FloatWindow^>^ floats;
        Panel^ header;
        Panel^ content;
        TabStrip^ tabs;

        void AddTab(ToolPane^ p)
        {
            if (tabs->IndexOfTag(p) >= 0) return;
            tabs->Add(p->Title, p);
            if (tabs->SelectedIndex < 0) tabs->SelectedIndex = 0;
            ShowSelected();
        }

        void ShowSelected()
        {
            ToolPane^ a = ActivePane;
            for each (ToolPane^ p in panes)
                if (!p->IsFloating) p->Visible = (p == a);
            tabs->Visible = tabs->Items->Count > 1;
            header->Invalidate();
        }

        SD::Rectangle CloseRect() { return SD::Rectangle(header->Width - 24, 3, 20, 20); }
        SD::Rectangle FloatRect() { return SD::Rectangle(header->Width - 46, 3, 20, 20); }

        void OnHeaderPaint(Object^ sender, PaintEventArgs^ e)
        {
            Theme^ t = ThemeManager::Current;
            e->Graphics->Clear(t->Header);
            ToolPane^ a = ActivePane;
            String^ title = a == nullptr ? String::Empty : a->Title->ToUpperInvariant();
            SD::Font^ f = gcnew SD::Font("Segoe UI", 8.25f, FontStyle::Bold);
            TextRenderer::DrawText(e->Graphics, title, f, SD::Rectangle(8, 0, header->Width - 60, header->Height), t->MutedText,
                                   TextFormatFlags::VerticalCenter | TextFormatFlags::Left | TextFormatFlags::EndEllipsis);
            e->Graphics->DrawImage(IconFactory::Get("float"), FloatRect().X + 2, FloatRect().Y + 2);
            e->Graphics->DrawImage(IconFactory::Get("close"), CloseRect().X + 2, CloseRect().Y + 2);
            Pen^ p = gcnew Pen(t->Border);
            e->Graphics->DrawLine(p, 0, header->Height - 1, header->Width, header->Height - 1);
            delete p;
        }

        void OnHeaderMouseDown(Object^ sender, MouseEventArgs^ e)
        {
            ToolPane^ a = ActivePane;
            if (a == nullptr) return;
            if (CloseRect().Contains(e->Location)) HidePane(a);
            else if (FloatRect().Contains(e->Location)) FloatPane(a);
        }

        void OnHeaderDoubleClick(Object^ sender, EventArgs^ e)
        {
            ToolPane^ a = ActivePane;
            if (a != nullptr) FloatPane(a);
        }

        void OnTabChanged(Object^ sender, EventArgs^ e) { ShowSelected(); }

        void OnTabDoubleClicked(TabItem^ t) { FloatPane(safe_cast<ToolPane^>(t->Tag)); }

        void OnFloatClosing(Object^ sender, FormClosingEventArgs^ e)
        {
            // closing a floating window docks the pane back into its area
            FloatWindow^ w = safe_cast<FloatWindow^>(sender);
            if (e->CloseReason == CloseReason::UserClosing && w->Pane->IsFloating)
                DockPaneInternal(w->Pane, false);
        }

        void DockPaneInternal(ToolPane^ p, bool closeWindow)
        {
            if (!p->IsFloating) return;
            FloatWindow^ w = floats[p];
            floats->Remove(p);
            w->FormClosing -= gcnew FormClosingEventHandler(this, &DockArea::OnFloatClosing);
            w->Controls->Remove(p);
            p->IsFloating = false;
            p->Visible = false;
            content->Controls->Add(p);
            p->Dock = DockStyle::Fill;
            AddTab(p);
            int i = tabs->IndexOfTag(p);
            if (i >= 0) tabs->SelectedIndex = i;
            ShowSelected();
            if (closeWindow) w->Close();
            VisibleCountChanged(this, EventArgs::Empty);
        }
    };
}}
