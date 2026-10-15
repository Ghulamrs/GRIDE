// =============================================================================
//  TabStrip.h - owner-drawn, fully themed tab header row
//  Used by the document area (tabs on top, with close buttons and a dirty
//  marker) and by the dock areas (tool pane tabs at the bottom).
//  The standard TabControl cannot be coloured in dark themes, hence this.
// =============================================================================
#pragma once
#include "../Theme.h"

namespace Gride { namespace UI {

    public ref class TabItem
    {
    public:
        TabItem(String^ text, Object^ tag) { Text = text; Tag = tag; Dirty = false; Pinned = false; Closable = true; ToolTip = text; }
        property String^ Text;
        property Object^ Tag;
        property bool Dirty;
        property bool Pinned;
        property bool Closable;
        property String^ ToolTip;
        property SD::Rectangle Bounds;
        property SD::Rectangle CloseBounds;
    };

    public delegate void TabEventHandler(TabItem^ item);

    public enum class TabStripStyle { Documents, Panes };

    public ref class TabStrip : public Control
    {
    public:
        TabStrip(TabStripStyle style)
        {
            this->style = style;
            items = gcnew List<TabItem^>();
            selected = -1; hover = -1; hoverClose = false;
            SetStyle(ControlStyles::AllPaintingInWmPaint | ControlStyles::OptimizedDoubleBuffer |
                     ControlStyles::UserPaint | ControlStyles::ResizeRedraw, true);
            Height = style == TabStripStyle::Documents ? 30 : 26;
            Dock = DockStyle::Top;
            tip = gcnew ToolTip();
            overflowMenu = gcnew System::Windows::Forms::ContextMenuStrip();
        }

        event EventHandler^ SelectedChanged;
        event TabEventHandler^ CloseRequested;
        event TabEventHandler^ TabDoubleClicked;
        event TabEventHandler^ TabRightClicked;

        property List<TabItem^>^ Items { List<TabItem^>^ get() { return items; } }

        property int SelectedIndex
        {
            int get() { return selected; }
            void set(int v)
            {
                if (v < -1 || v >= items->Count) v = items->Count > 0 ? 0 : -1;
                if (v == selected) { Invalidate(); return; }
                selected = v;
                Invalidate();
                SelectedChanged(this, EventArgs::Empty);
            }
        }

        property TabItem^ SelectedItem
        {
            TabItem^ get() { if (selected >= 0 && selected < items->Count) return items[selected]; return nullptr; }
        }

        TabItem^ Add(String^ text, Object^ tag)
        {
            TabItem^ t = gcnew TabItem(text, tag);
            // pinned tabs stay in front
            items->Add(t);
            Invalidate();
            return t;
        }

        void Remove(TabItem^ t)
        {
            int i = items->IndexOf(t);
            if (i < 0) return;
            items->RemoveAt(i);
            if (selected >= items->Count) selected = items->Count - 1;
            else if (i < selected) selected--;
            else if (i == selected) selected = Math::Min(i, items->Count - 1);
            Invalidate();
            SelectedChanged(this, EventArgs::Empty);
        }

        int IndexOfTag(Object^ tag)
        {
            for (int i = 0; i < items->Count; i++)
                if (Object::ReferenceEquals(items[i]->Tag, tag)) return i;
            return -1;
        }

        void Pin(TabItem^ t, bool pin)
        {
            t->Pinned = pin;
            // move pinned tabs to the front, keep selection on the same item
            TabItem^ sel = SelectedItem;
            items->Remove(t);
            int pos = 0;
            if (pin) { while (pos < items->Count && items[pos]->Pinned) pos++; }
            else { pos = 0; while (pos < items->Count && items[pos]->Pinned) pos++; }
            items->Insert(pos, t);
            selected = sel != nullptr ? items->IndexOf(sel) : -1;
            Invalidate();
        }

    protected:
        virtual void OnPaint(PaintEventArgs^ e) override
        {
            Theme^ t = ThemeManager::Current;
            Graphics^ g = e->Graphics;
            g->Clear(t->Panel);
            LayoutTabs();

            SD::Font^ f = this->Font;
            for (int i = 0; i < items->Count; i++)
            {
                TabItem^ it = items[i];
                SD::Rectangle r = it->Bounds;
                if (r.Width <= 0) continue;
                bool sel = i == selected;
                Color back = sel ? t->Window : (i == hover ? t->Hover : t->Panel);
                if (style == TabStripStyle::Panes && sel) back = t->Header;
                SolidBrush^ bb = gcnew SolidBrush(back);
                g->FillRectangle(bb, r);
                delete bb;

                if (sel)
                {
                    SolidBrush^ ab = gcnew SolidBrush(t->Accent);
                    if (style == TabStripStyle::Documents) g->FillRectangle(ab, r.X, r.Y, r.Width, 2);
                    else g->FillRectangle(ab, r.X, r.Bottom - 2, r.Width, 2);
                    delete ab;
                }
                Pen^ sep = gcnew Pen(t->Border);
                g->DrawLine(sep, r.Right - 1, r.Y + 4, r.Right - 1, r.Bottom - 4);
                delete sep;

                String^ text = it->Text;
                if (it->Dirty) text = text + " *";
                if (it->Pinned) text = gcnew String(L'\x25B8', 1) + " " + text;
                SD::Rectangle tr = SD::Rectangle(r.X + 10, r.Y, r.Width - (ShowClose(it, i) ? 30 : 16), r.Height);
                TextRenderer::DrawText(g, text, f, tr, sel ? t->Text : t->MutedText,
                    TextFormatFlags::VerticalCenter | TextFormatFlags::Left | TextFormatFlags::EndEllipsis | TextFormatFlags::NoPrefix);

                if (ShowClose(it, i))
                {
                    SD::Rectangle cr = it->CloseBounds;
                    if (i == hover && hoverClose)
                    {
                        SolidBrush^ hb = gcnew SolidBrush(t->Hover);
                        g->FillRectangle(hb, cr);
                        delete hb;
                    }
                    Pen^ xp = gcnew Pen(sel ? t->Text : t->MutedText, 1.3f);
                    g->DrawLine(xp, cr.X + 4, cr.Y + 4, cr.Right - 4, cr.Bottom - 4);
                    g->DrawLine(xp, cr.Right - 4, cr.Y + 4, cr.X + 4, cr.Bottom - 4);
                    delete xp;
                }
            }

            if (overflow)
            {
                SD::Rectangle o = OverflowBounds();
                TextRenderer::DrawText(g, gcnew String(L'\x25BE', 1), f, o, t->Text,
                    TextFormatFlags::HorizontalCenter | TextFormatFlags::VerticalCenter);
            }
            Pen^ bp = gcnew Pen(t->Border);
            if (style == TabStripStyle::Documents) g->DrawLine(bp, 0, Height - 1, Width, Height - 1);
            else g->DrawLine(bp, 0, 0, Width, 0);
            delete bp;
        }

        virtual void OnMouseMove(MouseEventArgs^ e) override
        {
            int h = HitTest(e->Location);
            bool hc = h >= 0 && ShowClose(items[h], h) && items[h]->CloseBounds.Contains(e->Location);
            if (h != hover || hc != hoverClose)
            {
                hover = h; hoverClose = hc;
                Invalidate();
                if (h >= 0) tip->SetToolTip(this, items[h]->ToolTip); else tip->SetToolTip(this, String::Empty);
            }
            Control::OnMouseMove(e);
        }

        virtual void OnMouseLeave(EventArgs^ e) override
        {
            hover = -1; hoverClose = false; Invalidate();
            Control::OnMouseLeave(e);
        }

        virtual void OnMouseDown(MouseEventArgs^ e) override
        {
            Control::OnMouseDown(e);
            if (overflow && OverflowBounds().Contains(e->Location)) { ShowOverflowMenu(); return; }
            int h = HitTest(e->Location);
            if (h < 0) return;
            TabItem^ it = items[h];
            if (e->Button == WF::MouseButtons::Middle && it->Closable) { CloseRequested(it); return; }
            if (e->Button == WF::MouseButtons::Left && ShowClose(it, h) && it->CloseBounds.Contains(e->Location)) { CloseRequested(it); return; }
            SelectedIndex = h;
            if (e->Button == WF::MouseButtons::Right) TabRightClicked(it);
        }

        virtual void OnMouseDoubleClick(MouseEventArgs^ e) override
        {
            Control::OnMouseDoubleClick(e);
            int h = HitTest(e->Location);
            if (h >= 0) TabDoubleClicked(items[h]);
        }

    private:
        TabStripStyle style;
        List<TabItem^>^ items;
        int selected;
        int hover;
        bool hoverClose;
        bool overflow;
        ToolTip^ tip;
        System::Windows::Forms::ContextMenuStrip^ overflowMenu;

        bool ShowClose(TabItem^ it, int index)
        {
            return it->Closable && !it->Pinned && style == TabStripStyle::Documents && (index == selected || index == hover);
        }

        SD::Rectangle OverflowBounds() { return SD::Rectangle(Width - 24, 0, 24, Height); }

        void LayoutTabs()
        {
            int x = 0;
            int avail = Width - 26;
            overflow = false;
            SD::Font^ f = this->Font;
            for (int i = 0; i < items->Count; i++)
            {
                TabItem^ it = items[i];
                String^ text = it->Text;
                if (it->Dirty) text = text + " *";
                if (it->Pinned) text = text + "  ";
                int w = TextRenderer::MeasureText(text, f).Width + (style == TabStripStyle::Documents ? 40 : 24);
                w = Math::Min(Math::Max(w, 60), 260);
                if (x + w > avail && i != selected)
                {
                    it->Bounds = SD::Rectangle::Empty;
                    it->CloseBounds = SD::Rectangle::Empty;
                    overflow = true;
                    continue;
                }
                it->Bounds = SD::Rectangle(x, 0, w, Height - (style == TabStripStyle::Documents ? 1 : 0));
                it->CloseBounds = SD::Rectangle(x + w - 22, (Height - 16) / 2, 16, 16);
                x += w;
            }
        }

        int HitTest(SD::Point p)
        {
            for (int i = 0; i < items->Count; i++)
                if (items[i]->Bounds.Contains(p)) return i;
            return -1;
        }

        void ShowOverflowMenu()
        {
            overflowMenu->Items->Clear();
            for (int i = 0; i < items->Count; i++)
            {
                ToolStripMenuItem^ mi = gcnew ToolStripMenuItem(items[i]->Text);
                mi->Tag = i;
                mi->Checked = i == selected;
                mi->Click += gcnew EventHandler(this, &TabStrip::OnOverflowClick);
                overflowMenu->Items->Add(mi);
            }
            overflowMenu->Show(this, SD::Point(Width - 24, Height));
        }

        void OnOverflowClick(Object^ sender, EventArgs^ e)
        {
            ToolStripMenuItem^ mi = safe_cast<ToolStripMenuItem^>(sender);
            int idx = safe_cast<int>(mi->Tag);
            // bring the chosen tab to the front so it becomes visible
            TabItem^ it = items[idx];
            items->RemoveAt(idx);
            int pos = 0;
            while (pos < items->Count && items[pos]->Pinned) pos++;
            items->Insert(pos, it);
            selected = -2;          // force change notification
            SelectedIndex = pos;
        }
    };
}}
