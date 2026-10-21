// =============================================================================
//  Theme.h - colour themes (Dark / Light / Blue) and how they are applied
//
//  Theme             the colour set (chrome + editor + syntax colours)
//  ThemeColorTable   feeds the colours to WinForms' ToolStrip renderer
//  ThemeRenderer     menu / toolbar / status bar painting
//  ThemeManager      Current theme + Apply(control) that walks a control tree
// =============================================================================
#pragma once
#include "../pch.h"

namespace Gride { namespace UI {

    public ref class Theme
    {
    public:
        property String^ Name;
        // chrome
        property Color Window;        // editor / document background
        property Color Panel;         // tool panes, dialogs
        property Color Chrome;        // menu bar, title strips
        property Color Header;        // pane headers, inactive tabs
        property Color Border;
        property Color Text;
        property Color MutedText;
        property Color Accent;
        property Color AccentText;    // text drawn on the accent colour
        property Color Selection;
        property Color Hover;
        property Color Input;
        property Color InputBorder;
        // editor
        property Color EditorText;
        property Color LineNumber;
        property Color CurrentLine;
        property Color ExecLine;      // debugger current statement
        property Color Breakpoint;
        property Color Bookmark;
        // syntax
        property Color Keyword;
        property Color TypeName;
        property Color StringLit;
        property Color Number;
        property Color Comment;
        property Color Preprocessor;
        property Color Function;
        // output
        property Color ErrorText;
        property Color WarningText;
        property Color SuccessText;

        property bool IsDark { bool get() { return Window.GetBrightness() < 0.5f; } }

        static Color Hex(String^ h) { return ColorTranslator::FromHtml(h); }

        static Theme^ Dark()
        {
            Theme^ t = gcnew Theme();
            t->Name = "Dark";
            t->Window = Hex("#17191C"); t->Panel = Hex("#1F2226"); t->Chrome = Hex("#1A1C20"); t->Header = Hex("#26292E");
            t->Border = Hex("#33373D"); t->Text = Hex("#D6D9DE"); t->MutedText = Hex("#9AA1AB");
            t->Accent = Hex("#E0A23B"); t->AccentText = Hex("#15171A"); t->Selection = Hex("#2C3A4A"); t->Hover = Hex("#33373D");
            t->Input = Hex("#17191C"); t->InputBorder = Hex("#3A3F46");
            t->EditorText = Hex("#D6D9DE"); t->LineNumber = Hex("#5F666F"); t->CurrentLine = Hex("#222A33");
            t->ExecLine = Hex("#4A4020"); t->Breakpoint = Hex("#E06C5B"); t->Bookmark = Hex("#4FB3BF");
            t->Keyword = Hex("#6FA8DC"); t->TypeName = Hex("#5FC1A8"); t->StringLit = Hex("#D9A066"); t->Number = Hex("#B5CEA8");
            t->Comment = Hex("#8A9A86"); t->Preprocessor = Hex("#C49BD8"); t->Function = Hex("#E6DB8A");
            t->ErrorText = Hex("#F08A7E"); t->WarningText = Hex("#E8C27A"); t->SuccessText = Hex("#8FD5B4");
            return t;
        }

        static Theme^ Light()
        {
            Theme^ t = gcnew Theme();
            t->Name = "Light";
            t->Window = Hex("#FFFFFF"); t->Panel = Hex("#F3F3F5"); t->Chrome = Hex("#E8EAED"); t->Header = Hex("#E2E4E8");
            t->Border = Hex("#CDD0D5"); t->Text = Hex("#1E1F22"); t->MutedText = Hex("#5D636B");
            t->Accent = Hex("#D08A1A"); t->AccentText = Hex("#15171A"); t->Selection = Hex("#CFE1F5"); t->Hover = Hex("#DDE1E6");
            t->Input = Hex("#FFFFFF"); t->InputBorder = Hex("#B9BEC6");
            t->EditorText = Hex("#1E1F22"); t->LineNumber = Hex("#8A9099"); t->CurrentLine = Hex("#F1F5FB");
            t->ExecLine = Hex("#FFF2B3"); t->Breakpoint = Hex("#C8392B"); t->Bookmark = Hex("#1F7F8C");
            t->Keyword = Hex("#0B57A4"); t->TypeName = Hex("#127E6B"); t->StringLit = Hex("#A1490F"); t->Number = Hex("#2F7D32");
            t->Comment = Hex("#5F7A5B"); t->Preprocessor = Hex("#7A3E9D"); t->Function = Hex("#6F5A00");
            t->ErrorText = Hex("#B3261E"); t->WarningText = Hex("#8A5A00"); t->SuccessText = Hex("#1E7A46");
            return t;
        }

        static Theme^ Blue()
        {
            Theme^ t = Light();
            t->Name = "Blue";
            t->Panel = Hex("#DCE2EE"); t->Chrome = Hex("#C9D3E6"); t->Header = Hex("#C2CCE0");
            t->Border = Hex("#8E9BBC"); t->Accent = Hex("#3D6CC0"); t->AccentText = Hex("#FFFFFF");
            t->Selection = Hex("#B7CBEE"); t->Hover = Hex("#CCD7EC"); t->InputBorder = Hex("#8E9BBC");
            return t;
        }

        static Theme^ ByName(String^ name)
        {
            if (String::Compare(name, "Light", true) == 0) return Light();
            if (String::Compare(name, "Blue", true) == 0) return Blue();
            return Dark();
        }

        static array<String^>^ Names() { return gcnew array<String^>{ "Dark", "Light", "Blue" }; }

        // token names used by Options > Fonts and Colors and settings "syntaxColors"
        static array<String^>^ TokenNames()
        {
            return gcnew array<String^>{ "text", "keyword", "type", "string", "number", "comment", "preprocessor", "function" };
        }

        Color GetToken(String^ token)
        {
            if (token == "keyword") return Keyword;
            if (token == "type") return TypeName;
            if (token == "string") return StringLit;
            if (token == "number") return Number;
            if (token == "comment") return Comment;
            if (token == "preprocessor") return Preprocessor;
            if (token == "function") return Function;
            return EditorText;
        }

        void SetToken(String^ token, Color c)
        {
            if (token == "keyword") Keyword = c;
            else if (token == "type") TypeName = c;
            else if (token == "string") StringLit = c;
            else if (token == "number") Number = c;
            else if (token == "comment") Comment = c;
            else if (token == "preprocessor") Preprocessor = c;
            else if (token == "function") Function = c;
            else EditorText = c;
        }

        void ApplyOverrides(Dictionary<String^, String^>^ overrides)
        {
            if (overrides == nullptr) return;
            for each (KeyValuePair<String^, String^> kv in overrides)
            {
                try { if (!String::IsNullOrEmpty(kv.Value)) SetToken(kv.Key, Hex(kv.Value)); }
                catch (Exception^) { }
            }
        }
    };

// helper: one overridden colour property of ProfessionalColorTable
#define GR_COLOR(name, expr) property Color name { virtual Color get() override { return expr; } }

    public ref class ThemeColorTable : public ProfessionalColorTable
    {
    public:
        ThemeColorTable(Theme^ theme) { t = theme; UseSystemColors = false; }

        GR_COLOR(MenuStripGradientBegin, t->Chrome)
        GR_COLOR(MenuStripGradientEnd, t->Chrome)
        GR_COLOR(MenuItemSelected, t->Hover)
        GR_COLOR(MenuItemSelectedGradientBegin, t->Hover)
        GR_COLOR(MenuItemSelectedGradientEnd, t->Hover)
        GR_COLOR(MenuItemPressedGradientBegin, t->Header)
        GR_COLOR(MenuItemPressedGradientMiddle, t->Header)
        GR_COLOR(MenuItemPressedGradientEnd, t->Header)
        GR_COLOR(MenuItemBorder, t->Border)
        GR_COLOR(MenuBorder, t->Border)
        GR_COLOR(ToolStripDropDownBackground, t->Panel)
        GR_COLOR(ImageMarginGradientBegin, t->Panel)
        GR_COLOR(ImageMarginGradientMiddle, t->Panel)
        GR_COLOR(ImageMarginGradientEnd, t->Panel)
        GR_COLOR(ToolStripGradientBegin, t->Chrome)
        GR_COLOR(ToolStripGradientMiddle, t->Chrome)
        GR_COLOR(ToolStripGradientEnd, t->Chrome)
        GR_COLOR(ToolStripBorder, t->Chrome)
        GR_COLOR(ToolStripContentPanelGradientBegin, t->Chrome)
        GR_COLOR(ToolStripContentPanelGradientEnd, t->Chrome)
        GR_COLOR(ToolStripPanelGradientBegin, t->Chrome)
        GR_COLOR(ToolStripPanelGradientEnd, t->Chrome)
        GR_COLOR(SeparatorDark, t->Border)
        GR_COLOR(SeparatorLight, t->Chrome)
        GR_COLOR(GripDark, t->Border)
        GR_COLOR(GripLight, t->Chrome)
        GR_COLOR(ButtonSelectedHighlight, t->Hover)
        GR_COLOR(ButtonSelectedGradientBegin, t->Hover)
        GR_COLOR(ButtonSelectedGradientMiddle, t->Hover)
        GR_COLOR(ButtonSelectedGradientEnd, t->Hover)
        GR_COLOR(ButtonSelectedBorder, t->Border)
        GR_COLOR(ButtonPressedGradientBegin, t->Selection)
        GR_COLOR(ButtonPressedGradientMiddle, t->Selection)
        GR_COLOR(ButtonPressedGradientEnd, t->Selection)
        GR_COLOR(ButtonPressedBorder, t->Border)
        GR_COLOR(ButtonCheckedGradientBegin, t->Selection)
        GR_COLOR(ButtonCheckedGradientMiddle, t->Selection)
        GR_COLOR(ButtonCheckedGradientEnd, t->Selection)
        GR_COLOR(CheckBackground, t->Selection)
        GR_COLOR(CheckSelectedBackground, t->Selection)
        GR_COLOR(CheckPressedBackground, t->Selection)
        GR_COLOR(StatusStripGradientBegin, t->Accent)
        GR_COLOR(StatusStripGradientEnd, t->Accent)
        GR_COLOR(OverflowButtonGradientBegin, t->Chrome)
        GR_COLOR(OverflowButtonGradientMiddle, t->Chrome)
        GR_COLOR(OverflowButtonGradientEnd, t->Chrome)

    private:
        Theme^ t;
    };

#undef GR_COLOR

    public ref class ThemeRenderer : public ToolStripProfessionalRenderer
    {
    public:
        ThemeRenderer(Theme^ theme) : ToolStripProfessionalRenderer(gcnew ThemeColorTable(theme))
        {
            t = theme;
            RoundedEdges = false;
        }

    protected:
        virtual void OnRenderItemText(ToolStripItemTextRenderEventArgs^ e) override
        {
            if (dynamic_cast<StatusStrip^>(e->ToolStrip) != nullptr) e->TextColor = t->AccentText;
            else if (e->Item->BackColor == t->Accent && e->Item->Enabled) e->TextColor = t->AccentText;   // accent buttons
            else e->TextColor = e->Item->Enabled ? t->Text : t->MutedText;
            ToolStripProfessionalRenderer::OnRenderItemText(e);
        }

        virtual void OnRenderArrow(ToolStripArrowRenderEventArgs^ e) override
        {
            e->ArrowColor = t->Text;
            ToolStripProfessionalRenderer::OnRenderArrow(e);
        }

        virtual void OnRenderToolStripBorder(ToolStripRenderEventArgs^ e) override
        {
            // drop-downs keep a thin border, bars get none (avoids the white line)
            if (dynamic_cast<ToolStripDropDown^>(e->ToolStrip) != nullptr)
            {
                Pen^ p = gcnew Pen(t->Border);
                SD::Rectangle r = e->AffectedBounds;
                e->Graphics->DrawRectangle(p, r.X, r.Y, r.Width - 1, r.Height - 1);
                delete p;
            }
        }

        virtual void OnRenderItemCheck(ToolStripItemImageRenderEventArgs^ e) override
        {
            SD::Rectangle r = e->ImageRectangle;
            SolidBrush^ b = gcnew SolidBrush(t->Selection);
            e->Graphics->FillRectangle(b, r.X - 1, r.Y - 1, r.Width + 2, r.Height + 2);
            delete b;
            Pen^ p = gcnew Pen(t->Accent, 2.0f);
            e->Graphics->SmoothingMode = SD::Drawing2D::SmoothingMode::AntiAlias;
            e->Graphics->DrawLines(p, gcnew array<SD::Point>{
                SD::Point(r.X + 3, r.Y + r.Height / 2), SD::Point(r.X + r.Width / 2 - 1, r.Bottom - 4), SD::Point(r.Right - 3, r.Y + 3) });
            delete p;
        }

    private:
        Theme^ t;
    };

    // -------------------------------------------------------------------------
    public ref class ThemeManager abstract sealed
    {
    public:
        static property Theme^ Current
        {
            Theme^ get() { if (current == nullptr) current = Theme::Dark(); return current; }
            void set(Theme^ t) { current = t; ToolStripManager::Renderer = gcnew ThemeRenderer(t); }
        }

        // walks the control tree and colours every known control type.
        // Controls whose Tag is "accent" get the accent colour, "keep" are skipped.
        static void Apply(Control^ c)
        {
            if (c == nullptr) return;
            Theme^ t = Current;
            String^ tag = dynamic_cast<String^>(c->Tag);
            if (tag == "keep") return;

            if (tag == "accent")
            {
                c->BackColor = t->Accent; c->ForeColor = t->AccentText;
                Button^ ab = dynamic_cast<Button^>(c);
                if (ab != nullptr) { ab->FlatStyle = WF::FlatStyle::Flat; ab->FlatAppearance->BorderColor = t->Accent; }
            }
            else if (dynamic_cast<ToolStrip^>(c) != nullptr)
            {
                ToolStrip^ ts = safe_cast<ToolStrip^>(c);
                ts->RenderMode = ToolStripRenderMode::ManagerRenderMode;
                if (dynamic_cast<StatusStrip^>(c) != nullptr) { c->BackColor = t->Accent; c->ForeColor = t->AccentText; }
                else { c->BackColor = t->Chrome; c->ForeColor = t->Text; }
            }
            else if (dynamic_cast<RichTextBox^>(c) != nullptr)
            {
                c->BackColor = t->Window; c->ForeColor = t->EditorText;
            }
            else if (dynamic_cast<TextBoxBase^>(c) != nullptr || dynamic_cast<ComboBox^>(c) != nullptr ||
                     dynamic_cast<ListBox^>(c) != nullptr || dynamic_cast<NumericUpDown^>(c) != nullptr)
            {
                c->BackColor = t->Input; c->ForeColor = t->Text;
                TextBox^ tb = dynamic_cast<TextBox^>(c);
                if (tb != nullptr) tb->BorderStyle = WF::BorderStyle::FixedSingle;
                ComboBox^ cb = dynamic_cast<ComboBox^>(c);
                if (cb != nullptr) cb->FlatStyle = WF::FlatStyle::Flat;
            }
            else if (dynamic_cast<TreeView^>(c) != nullptr)
            {
                TreeView^ tv = safe_cast<TreeView^>(c);
                tv->BackColor = t->Panel; tv->ForeColor = t->Text; tv->LineColor = t->MutedText;
                tv->BorderStyle = WF::BorderStyle::None;
            }
            else if (dynamic_cast<ListView^>(c) != nullptr)
            {
                ListView^ lv = safe_cast<ListView^>(c);
                lv->BackColor = t->Panel; lv->ForeColor = t->Text;
                lv->BorderStyle = WF::BorderStyle::None;
            }
            else if (dynamic_cast<PropertyGrid^>(c) != nullptr)
            {
                PropertyGrid^ pg = safe_cast<PropertyGrid^>(c);
                pg->BackColor = t->Panel; pg->ViewBackColor = t->Panel; pg->ViewForeColor = t->Text;
                pg->LineColor = t->Header; pg->CategoryForeColor = t->Text; pg->HelpBackColor = t->Panel;
                pg->HelpForeColor = t->Text; pg->CommandsBackColor = t->Panel; pg->CommandsForeColor = t->Text;
                pg->ViewBorderColor = t->Border; pg->HelpBorderColor = t->Border; pg->CategorySplitterColor = t->Header;
                pg->SelectedItemWithFocusBackColor = t->Selection; pg->SelectedItemWithFocusForeColor = t->Text;
                return;   // its children are internal
            }
            else if (dynamic_cast<Button^>(c) != nullptr)
            {
                Button^ b = safe_cast<Button^>(c);
                b->FlatStyle = WF::FlatStyle::Flat;
                b->FlatAppearance->BorderColor = t->InputBorder;
                b->FlatAppearance->MouseOverBackColor = t->Hover;
                b->BackColor = t->Header; b->ForeColor = t->Text;
            }
            else if (dynamic_cast<CheckBox^>(c) != nullptr || dynamic_cast<RadioButton^>(c) != nullptr ||
                     dynamic_cast<Label^>(c) != nullptr || dynamic_cast<GroupBox^>(c) != nullptr)
            {
                c->ForeColor = (tag == "muted") ? t->MutedText : t->Text;
                c->BackColor = Color::Transparent;
                if (dynamic_cast<GroupBox^>(c) != nullptr) c->BackColor = t->Panel;
            }
            else if (tag == "header")
            {
                c->BackColor = t->Header; c->ForeColor = t->Text;
            }
            else
            {
                c->BackColor = t->Panel; c->ForeColor = t->Text;
            }

            if (c->ContextMenuStrip != nullptr) c->ContextMenuStrip->RenderMode = ToolStripRenderMode::ManagerRenderMode;
            for each (Control^ child in c->Controls) Apply(child);
        }

    private:
        static Theme^ current = nullptr;
    };
}}
