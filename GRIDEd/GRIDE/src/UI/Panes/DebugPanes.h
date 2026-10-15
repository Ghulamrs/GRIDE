// =============================================================================
//  DebugPanes.h - Properties, Breakpoints and the Debug > Windows panes
//    PropertiesPane, BreakpointsPane,
//    WatchPane, LocalsPane, CallStackPane, RegistersPane, MemoryPane,
//    DisassemblyPane
//  Debug panes derive from DebugPaneBase and refresh from any DebugEngine.
// =============================================================================
#pragma once
#include "OutputPanes.h"
#include "../../Services/Debugger.h"

namespace Gride { namespace UI {

    using namespace Gride::Services;

    // -------------------------------------------------------------------------
    public ref class PropertiesPane : public ToolPane
    {
    public:
        PropertiesPane() : ToolPane("Properties", "Properties", "properties")
        {
            grid = gcnew PropertyGrid();
            grid->Dock = DockStyle::Fill;
            grid->ToolbarVisible = false;
            grid->HelpVisible = true;
            grid->PropertySort = WF::PropertySort::Categorized;
            Controls->Add(grid);
        }
        void SetObject(Object^ o) { grid->SelectedObject = o; }
        void RefreshValues() { grid->Refresh(); }
    private:
        PropertyGrid^ grid;
    };

    // -------------------------------------------------------------------------
    public ref class BreakpointsPane : public ToolPane
    {
    public:
        BreakpointsPane() : ToolPane("Breakpoints", "Breakpoints", "breakpoint")
        {
            list = PaneUtil::MakeList(gcnew array<String^>{ "File", "Line" }, gcnew array<int>{ 170, 50 });
            list->SmallImageList = IconFactory::CreateImageList(gcnew array<String^>{ "breakpoint" });
            list->DoubleClick += gcnew EventHandler(this, &BreakpointsPane::OnDoubleClick);
            list->KeyDown += gcnew KeyEventHandler(this, &BreakpointsPane::OnKeyDown);
            Controls->Add(list);
        }

        event NavigateHandler^ Navigate;
        event NavigateHandler^ RemoveRequested;

        void Fill(Dictionary<String^, SortedSet<int>^>^ bps)
        {
            list->BeginUpdate();
            list->Items->Clear();
            for each (KeyValuePair<String^, SortedSet<int>^> kv in bps)
                for each (int line in kv.Value)
                {
                    ListViewItem^ it = gcnew ListViewItem(Path::GetFileName(kv.Key), "breakpoint");
                    it->SubItems->Add(line.ToString());
                    it->Tag = gcnew Tuple<String^, int>(kv.Key, line);
                    it->ToolTipText = kv.Key;
                    list->Items->Add(it);
                }
            list->EndUpdate();
        }

    private:
        ListView^ list;

        Tuple<String^, int>^ Current()
        {
            if (list->SelectedItems->Count == 0) return nullptr;
            return safe_cast<Tuple<String^, int>^>(list->SelectedItems[0]->Tag);
        }
        void OnDoubleClick(Object^ sender, EventArgs^ e)
        {
            Tuple<String^, int>^ t = Current();
            if (t != nullptr) Navigate(t->Item1, t->Item2);
        }
        void OnKeyDown(Object^ sender, KeyEventArgs^ e)
        {
            Tuple<String^, int>^ t = Current();
            if (t != nullptr && e->KeyCode == Keys::Delete) RemoveRequested(t->Item1, t->Item2);
        }
    };

    // -------------------------------------------------------------------------
    public ref class DebugPaneBase abstract : public ToolPane
    {
    public:
        DebugPaneBase(String^ key, String^ title, String^ icon) : ToolPane(key, title, icon) { }
        virtual void UpdateFrom(DebugEngine^ d) abstract;
        virtual void ClearView() abstract;
    };

    // -------------------------------------------------------------------------
    public ref class WatchPane : public DebugPaneBase
    {
    public:
        WatchPane() : DebugPaneBase("Watch", "Watch", "find")
        {
            expressions = gcnew List<String^>();
            addBox = gcnew TextBox();
            addBox->Dock = DockStyle::Top;
            addBox->Font = PaneUtil::MonoFont();
            addBox->KeyDown += gcnew KeyEventHandler(this, &WatchPane::OnAddKey);
            SetCue(addBox, "Type an expression and press Enter to add a watch");
            list = PaneUtil::MakeList(gcnew array<String^>{ "Name", "Value", "Type" }, gcnew array<int>{ 180, 320, 100 });
            list->Font = PaneUtil::MonoFont();
            list->KeyDown += gcnew KeyEventHandler(this, &WatchPane::OnListKey);
            Controls->Add(list);
            Controls->Add(addBox);
            engine = nullptr;
        }

        virtual void UpdateFrom(DebugEngine^ d) override
        {
            engine = d;
            list->BeginUpdate();
            list->Items->Clear();
            for each (String^ x in expressions)
            {
                ListViewItem^ it = gcnew ListViewItem(x);
                String^ value = "<not in break mode>";
                if (d != nullptr && d->State == DebugState::Break) value = d->Evaluate(x);
                it->SubItems->Add(value);
                it->SubItems->Add(String::Empty);
                list->Items->Add(it);
            }
            list->EndUpdate();
        }
        virtual void ClearView() override { UpdateFrom(nullptr); }

    private:
        List<String^>^ expressions;
        TextBox^ addBox;
        ListView^ list;
        DebugEngine^ engine;

        static void SetCue(TextBox^ tb, String^ text) { tb->Text = String::Empty; tb->AccessibleDescription = text; }

        void OnAddKey(Object^ sender, KeyEventArgs^ e)
        {
            if (e->KeyCode != Keys::Enter || addBox->Text->Trim()->Length == 0) return;
            e->SuppressKeyPress = true;
            expressions->Add(addBox->Text->Trim());
            addBox->Clear();
            UpdateFrom(engine);
        }
        void OnListKey(Object^ sender, KeyEventArgs^ e)
        {
            if (e->KeyCode != Keys::Delete || list->SelectedItems->Count == 0) return;
            expressions->Remove(list->SelectedItems[0]->Text);
            UpdateFrom(engine);
        }
    };

    // -------------------------------------------------------------------------
    public ref class LocalsPane : public DebugPaneBase
    {
    public:
        LocalsPane() : DebugPaneBase("Locals", "Locals", "variable")
        {
            list = PaneUtil::MakeList(gcnew array<String^>{ "Name", "Value", "Type" }, gcnew array<int>{ 180, 320, 120 });
            list->Font = PaneUtil::MonoFont();
            Controls->Add(list);
        }
        virtual void UpdateFrom(DebugEngine^ d) override
        {
            list->BeginUpdate();
            list->Items->Clear();
            if (d != nullptr && d->State == DebugState::Break)
                for each (DebugVariable^ v in d->Locals())
                {
                    ListViewItem^ it = gcnew ListViewItem(v->Name);
                    it->SubItems->Add(v->Value);
                    it->SubItems->Add(v->Type);
                    list->Items->Add(it);
                }
            list->EndUpdate();
        }
        virtual void ClearView() override { list->Items->Clear(); }
    private:
        ListView^ list;
    };

    // -------------------------------------------------------------------------
    public ref class CallStackPane : public DebugPaneBase
    {
    public:
        CallStackPane() : DebugPaneBase("CallStack", "Call Stack", "function")
        {
            list = PaneUtil::MakeList(gcnew array<String^>{ "Function", "File", "Line" }, gcnew array<int>{ 260, 220, 60 });
            list->DoubleClick += gcnew EventHandler(this, &CallStackPane::OnDoubleClick);
            Controls->Add(list);
        }
        event NavigateHandler^ Navigate;
        virtual void UpdateFrom(DebugEngine^ d) override
        {
            list->BeginUpdate();
            list->Items->Clear();
            if (d != nullptr && d->State == DebugState::Break)
                for each (DebugFrame^ f in d->CallStack())
                {
                    ListViewItem^ it = gcnew ListViewItem(f->Function);
                    it->SubItems->Add(String::IsNullOrEmpty(f->File) ? String::Empty : Path::GetFileName(f->File));
                    it->SubItems->Add(f->Line > 0 ? f->Line.ToString() : String::Empty);
                    it->Tag = f;
                    list->Items->Add(it);
                }
            list->EndUpdate();
        }
        virtual void ClearView() override { list->Items->Clear(); }
    private:
        ListView^ list;
        void OnDoubleClick(Object^ sender, EventArgs^ e)
        {
            if (list->SelectedItems->Count == 0) return;
            DebugFrame^ f = safe_cast<DebugFrame^>(list->SelectedItems[0]->Tag);
            if (!String::IsNullOrEmpty(f->File)) Navigate(f->File, f->Line);
        }
    };

    // -------------------------------------------------------------------------
    public ref class RegistersPane : public DebugPaneBase
    {
    public:
        RegistersPane() : DebugPaneBase("Registers", "Registers", "settings")
        {
            previous = gcnew Dictionary<String^, String^>();
            list = PaneUtil::MakeList(gcnew array<String^>{ "Register", "Value", "Register", "Value" }, gcnew array<int>{ 80, 120, 80, 120 });
            list->Font = PaneUtil::MonoFont();
            Controls->Add(list);
        }
        virtual void UpdateFrom(DebugEngine^ d) override
        {
            list->BeginUpdate();
            list->Items->Clear();
            if (d != nullptr && d->State == DebugState::Break)
            {
                List<RegisterValue^>^ regs = d->Registers();
                Color changed = ThemeManager::Current->ErrorText;
                // two registers per row
                for (int i = 0; i < regs->Count; i += 2)
                {
                    ListViewItem^ it = gcnew ListViewItem(regs[i]->Name);
                    it->UseItemStyleForSubItems = false;
                    AddValue(it, regs[i], changed);
                    if (i + 1 < regs->Count)
                    {
                        it->SubItems->Add(regs[i + 1]->Name);
                        AddValue(it, regs[i + 1], changed);
                    }
                    list->Items->Add(it);
                }
                previous->Clear();
                for each (RegisterValue^ r in regs) previous[r->Name] = r->Value;
            }
            list->EndUpdate();
        }
        virtual void ClearView() override { list->Items->Clear(); previous->Clear(); }
    private:
        ListView^ list;
        Dictionary<String^, String^>^ previous;
        void AddValue(ListViewItem^ it, RegisterValue^ r, Color changed)
        {
            ListViewItem::ListViewSubItem^ s = it->SubItems->Add(r->Value);
            String^ old;
            if (previous->TryGetValue(r->Name, old) && old != r->Value) s->ForeColor = changed;
        }
    };

    // -------------------------------------------------------------------------
    public ref class MemoryPane : public DebugPaneBase
    {
    public:
        MemoryPane() : DebugPaneBase("Memory", "Memory", "properties")
        {
            ToolStrip^ bar = PaneUtil::MakeBar();
            bar->Items->Add(gcnew ToolStripLabel("Address:"));
            address = gcnew ToolStripTextBox();
            address->Text = "0x11800000";
            address->Width = 120;
            address->KeyDown += gcnew KeyEventHandler(this, &MemoryPane::OnAddressKey);
            bar->Items->Add(address);
            ToolStripButton^ go = gcnew ToolStripButton("Go");
            go->Click += gcnew EventHandler(this, &MemoryPane::OnGo);
            bar->Items->Add(go);
            view = gcnew RichTextBox();
            view->Dock = DockStyle::Fill;
            view->ReadOnly = true;
            view->WordWrap = false;
            view->BorderStyle = WF::BorderStyle::None;
            view->Font = PaneUtil::MonoFont();
            Controls->Add(view);
            Controls->Add(bar);
            engine = nullptr;
        }
        virtual void UpdateFrom(DebugEngine^ d) override
        {
            engine = d;
            view->Clear();
            if (d == nullptr || d->State != DebugState::Break) { view->Text = "Memory is available while the program is paused."; return; }
            UInt32 addr = 0;
            String^ a = address->Text->Trim()->ToLowerInvariant()->Replace("0x", "");
            if (!UInt32::TryParse(a, NumberStyles::HexNumber, CultureInfo::InvariantCulture, addr)) { view->Text = "Invalid address."; return; }
            array<Byte>^ b = d->ReadMemory(addr, 256);
            StringBuilder^ sb = gcnew StringBuilder();
            for (int row = 0; row < b->Length; row += 16)
            {
                sb->AppendFormat("{0:X8}  ", addr + (UInt32)row);
                StringBuilder^ ascii = gcnew StringBuilder();
                for (int i = 0; i < 16 && row + i < b->Length; i++)
                {
                    Byte v = b[row + i];
                    sb->AppendFormat("{0:X2} ", v);
                    ascii->Append(v >= 32 && v < 127 ? (wchar_t)v : L'.');
                }
                sb->Append(" ");
                sb->Append(ascii->ToString());
                sb->Append("\n");
            }
            view->Text = sb->ToString();
        }
        virtual void ClearView() override { view->Clear(); }
    private:
        ToolStripTextBox^ address;
        RichTextBox^ view;
        DebugEngine^ engine;
        void OnGo(Object^ sender, EventArgs^ e) { UpdateFrom(engine); }
        void OnAddressKey(Object^ sender, KeyEventArgs^ e) { if (e->KeyCode == Keys::Enter) { e->SuppressKeyPress = true; UpdateFrom(engine); } }
    };

    // -------------------------------------------------------------------------
    //  DisassemblyPane - shows the .asm the compiler generated for a source
    // -------------------------------------------------------------------------
    public ref class DisassemblyPane : public DebugPaneBase
    {
    public:
        DisassemblyPane() : DebugPaneBase("Disassembly", "Disassembly", "fileasm")
        {
            header = gcnew Label();
            header->Dock = DockStyle::Top;
            header->Height = 22;
            header->TextAlign = ContentAlignment::MiddleLeft;
            header->Tag = "muted";
            view = gcnew RichTextBox();
            view->Dock = DockStyle::Fill;
            view->ReadOnly = true;
            view->WordWrap = false;
            view->BorderStyle = WF::BorderStyle::None;
            view->Font = PaneUtil::MonoFont();
            Controls->Add(view);
            Controls->Add(header);
        }

        // asmPath = generated assembly of the active source (may not exist yet)
        void ShowFile(String^ source, String^ asmPath)
        {
            if (String::IsNullOrEmpty(asmPath) || !File::Exists(asmPath))
            {
                header->Text = "  " + (source == nullptr ? String::Empty : Path::GetFileName(source));
                view->Text = "No generated assembly found yet for this file.\nBuild the project (F7) - the compiler writes it to:\n  " +
                             (asmPath == nullptr ? String::Empty : asmPath);
                return;
            }
            header->Text = "  " + asmPath;
            view->Text = File::ReadAllText(asmPath);
        }

        virtual void UpdateFrom(DebugEngine^ d) override { }
        virtual void ClearView() override { }
    private:
        Label^ header;
        RichTextBox^ view;
    };
}}
