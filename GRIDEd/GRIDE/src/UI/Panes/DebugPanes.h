// =============================================================================
//  DebugPanes.h - Properties, Breakpoints and the Debug > Windows panes
//    PropertiesPane, BreakpointsPane,
//    WatchPane, LocalsPane, CallStackPane, RegistersPane, MemoryPane,
//    DisassemblyPane
//  Debug panes derive from DebugPaneBase and refresh from any DebugEngine.
//  Locals and Watch show RIDE's Name | Value | Type | Address, a value changed
//  since the last stop in colour; the Call Stack shows the frame looked at.
// =============================================================================
#pragma once
#include "OutputPanes.h"
#include "../../Services/Debugger.h"
#include "../../Services/GdbDebugger.h"

namespace Gride { namespace UI {

    using namespace Gride::Services;

    public delegate void IndexChosenHandler(int index);
    public delegate void VariableSetHandler(String^ name, String^ value);

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

    // RIDE's DebugView rows: a value changed since the stop before is drawn in colour and bold; what
    // was seen at the frame stopped in becomes what the next stop is compared with (NewStop), so a
    // look up the stack never counts as a change
    private ref class ValueMarks
    {
    public:
        ValueMarks() { before = gcnew Dictionary<String^, String^>(); seen = gcnew Dictionary<String^, String^>(); }
        void NewStop() { before = seen; seen = gcnew Dictionary<String^, String^>(); }
        void Forget() { before = gcnew Dictionary<String^, String^>(); seen = gcnew Dictionary<String^, String^>(); }
        void Mark(ListViewItem^ row, int cell, String^ key, String^ value, bool atStop)
        {
            String^ was;
            if (before->TryGetValue(key, was) && was != value)
            {
                row->UseItemStyleForSubItems = false;
                row->SubItems[cell]->ForeColor = ThemeManager::Current->ErrorText;
                row->SubItems[cell]->Font = gcnew SD::Font(row->ListView->Font, FontStyle::Bold);
            }
            if (atStop) seen[key] = value;
        }
        // RIDE: a value with no place is "-", a missing value nothing
        static String^ Address(String^ address, String^ value)
        {
            if (!String::IsNullOrEmpty(address)) return address;
            return String::IsNullOrEmpty(value) ? String::Empty : "-";
        }
        static void Muted(ListViewItem^ row) { row->ForeColor = SD::Color::Gray; }
    private:
        Dictionary<String^, String^>^ before;
        Dictionary<String^, String^>^ seen;
    };

    // -------------------------------------------------------------------------
    //  WatchPane - RIDE's watches: the expressions live in the engine and are read at every stop and
    //  frame change; the pane keeps them so a new session gets them again. The line under the list
    //  adds one (Enter); Enter on a watch brings it there to be changed, and an empty answer takes
    //  it away; Delete takes one away.
    // -------------------------------------------------------------------------
    public ref class WatchPane : public DebugPaneBase
    {
    public:
        literal String^ AddWatchRow = "add a watch...";

        WatchPane() : DebugPaneBase("Watch", "Watch", "find")
        {
            expressions = gcnew List<String^>();
            marks = gcnew ValueMarks();
            editing = -1;
            addBox = gcnew TextBox();
            addBox->Dock = DockStyle::Bottom;
            addBox->Font = PaneUtil::MonoFont();
            addBox->KeyDown += gcnew KeyEventHandler(this, &WatchPane::OnAddKey);
            addBox->AccessibleDescription = "an expression to watch, Enter adds it";
            list = PaneUtil::MakeList(gcnew array<String^>{ "Name", "Value", "Type", "Address" }, gcnew array<int>{ 180, 300, 130, 150 });
            list->Font = PaneUtil::MonoFont();
            list->KeyDown += gcnew KeyEventHandler(this, &WatchPane::OnListKey);
            list->DoubleClick += gcnew EventHandler(this, &WatchPane::OnDouble);
            Controls->Add(list);
            Controls->Add(addBox);
            engine = nullptr;
        }

        property List<String^>^ Expressions { List<String^>^ get() { return expressions; } }
        void FocusAdd() { addBox->Focus(); }
        void NewStop() { marks->NewStop(); }

        virtual void UpdateFrom(DebugEngine^ d) override
        {
            if (d != engine)
            {
                // a new engine: the expressions kept here go into it
                engine = d;
                if (d != nullptr)
                {
                    List<String^>^ theirs = gcnew List<String^>();
                    for each (DebugWatch^ w in d->Watches()) theirs->Add(w->Expression);
                    for each (String^ x in expressions) if (!theirs->Contains(x)) d->AddWatch(x);
                }
            }
            list->BeginUpdate();
            list->Items->Clear();
            if (d != nullptr)
                for each (DebugWatch^ w in d->Watches())
                {
                    ListViewItem^ it = gcnew ListViewItem(w->Expression);
                    it->SubItems->Add(w->Value);
                    it->SubItems->Add(w->Type);
                    it->SubItems->Add(ValueMarks::Address(w->Address, w->Value));
                    it->Tag = w;
                    list->Items->Add(it);
                    if (!w->Ok) { it->UseItemStyleForSubItems = false; it->SubItems[1]->ForeColor = SD::Color::Gray; }
                    else if (d->State == DebugState::Break) marks->Mark(it, 1, "watch|" + w->Expression, w->Value, d->CurrentFrame == 0);
                }
            ListViewItem^ add = gcnew ListViewItem(AddWatchRow);
            ValueMarks::Muted(add);
            list->Items->Add(add);
            list->EndUpdate();
        }

        virtual void ClearView() override
        {
            engine = nullptr;
            marks->Forget();
            editing = -1;
            list->Items->Clear();
        }

    private:
        List<String^>^ expressions;
        ValueMarks^ marks;
        TextBox^ addBox;
        ListView^ list;
        DebugEngine^ engine;
        int editing;       // the watch the line below changes, -1 to add

        void OnAddKey(Object^ sender, KeyEventArgs^ e)
        {
            if (e->KeyCode == Keys::Escape) { editing = -1; addBox->Clear(); e->SuppressKeyPress = true; return; }
            if (e->KeyCode != Keys::Enter) return;
            e->SuppressKeyPress = true;
            String^ text = addBox->Text->Trim();
            if (editing >= 0 && editing < expressions->Count)
            {
                if (text->Length == 0) { if (engine != nullptr) engine->RemoveWatch(editing); expressions->RemoveAt(editing); }
                else { if (engine != nullptr) engine->SetWatch(editing, text); expressions[editing] = text; }
                editing = -1;
            }
            else if (text->Length > 0)
            {
                expressions->Add(text);
                if (engine != nullptr) engine->AddWatch(text);
            }
            addBox->Clear();
            UpdateFrom(engine);
        }

        void OnDouble(Object^ sender, EventArgs^ e)
        {
            if (list->SelectedItems->Count == 0) return;
            int i = list->SelectedItems[0]->Index;
            if (i >= expressions->Count) { editing = -1; addBox->Clear(); addBox->Focus(); return; }
            editing = i;
            addBox->Text = expressions[i];
            addBox->Focus();
            addBox->SelectAll();
        }

        void OnListKey(Object^ sender, KeyEventArgs^ e)
        {
            if (list->SelectedItems->Count == 0) return;
            int i = list->SelectedItems[0]->Index;
            if (e->KeyCode == Keys::Enter) { e->SuppressKeyPress = true; OnDouble(sender, e); return; }
            if (e->KeyCode != Keys::Delete || i >= expressions->Count) return;
            if (engine != nullptr) engine->RemoveWatch(i);
            expressions->RemoveAt(i);
            UpdateFrom(engine);
        }
    };

    // -------------------------------------------------------------------------
    //  LocalsPane - RIDE's Debug tab: the stop line on top ("stopped at f.c:11 in main", and "the
    //  variables are main's, at ..." when a frame above is looked at), the variables as Name | Value
    //  | Type | Address, the engine's reason in brackets when there are none, the keys along the
    //  bottom; the line above them sets a variable ("name = value", Enter; Enter on a row fills it)
    // -------------------------------------------------------------------------
    public ref class LocalsPane : public DebugPaneBase
    {
    public:
        LocalsPane() : DebugPaneBase("Locals", "Locals", "variable")
        {
            marks = gcnew ValueMarks();
            stopLabel = gcnew Label();
            stopLabel->Dock = DockStyle::Top;
            stopLabel->Height = 20;
            stopLabel->TextAlign = ContentAlignment::MiddleLeft;
            stopLabel->Font = gcnew SD::Font(PaneUtil::MonoFont(), FontStyle::Bold);
            keysLabel = gcnew Label();
            keysLabel->Dock = DockStyle::Bottom;
            keysLabel->Height = 18;
            keysLabel->TextAlign = ContentAlignment::MiddleLeft;
            keysLabel->Tag = "muted";
            setBox = gcnew TextBox();
            setBox->Dock = DockStyle::Bottom;
            setBox->Font = PaneUtil::MonoFont();
            setBox->KeyDown += gcnew KeyEventHandler(this, &LocalsPane::OnSetKey);
            setBox->AccessibleDescription = "name = value sets a variable (Enter)";
            list = PaneUtil::MakeList(gcnew array<String^>{ "Name", "Value", "Type", "Address" }, gcnew array<int>{ 160, 300, 140, 150 });
            list->Font = PaneUtil::MonoFont();
            list->DoubleClick += gcnew EventHandler(this, &LocalsPane::OnDouble);
            list->KeyDown += gcnew KeyEventHandler(this, &LocalsPane::OnListKey);
            Controls->Add(list);
            Controls->Add(setBox);
            Controls->Add(keysLabel);
            Controls->Add(stopLabel);
        }

        event VariableSetHandler^ SetRequested;

        property String^ KeysLine
        {
            String^ get() { return keysLabel->Text; }
            void set(String^ v) { keysLabel->Text = v == nullptr ? String::Empty : v; }
        }
        property String^ StopLine { String^ get() { return stopLabel->Text; } }
        void FocusSet() { setBox->Focus(); }
        void NewStop() { marks->NewStop(); }

        virtual void UpdateFrom(DebugEngine^ d) override
        {
            list->BeginUpdate();
            list->Items->Clear();
            if (d == nullptr || d->State != DebugState::Break)
            {
                stopLabel->Text = String::Empty;
                list->EndUpdate();
                return;
            }
            StopInfo^ loc = d->Location;
            String^ head = loc == nullptr ? String::Empty : DebugTranscript::StopLine(loc->File, loc->Line, loc->Function);
            String^ frameName = loc == nullptr ? String::Empty : loc->Function;
            if (d->CurrentFrame > 0)
            {
                List<DebugFrame^>^ frames = d->CallStack();
                if (d->CurrentFrame < frames->Count)
                {
                    head += "    -    " + DebugTranscript::LookingAt(frames[d->CurrentFrame]);
                    frameName = frames[d->CurrentFrame]->Function;
                }
            }
            stopLabel->Text = head;
            List<DebugVariable^>^ vars = d->Locals();
            for each (DebugVariable^ v in vars)
            {
                ListViewItem^ it = gcnew ListViewItem(v->Name);
                it->SubItems->Add(v->Value);
                it->SubItems->Add(v->Type);
                it->SubItems->Add(ValueMarks::Address(v->Address, v->Value));
                it->Tag = v;
                list->Items->Add(it);
                marks->Mark(it, 1, frameName + "|" + v->Name, v->Value, d->CurrentFrame == 0);
            }
            if (vars->Count == 0)
            {
                String^ why = d->WhyNoLocals();
                ListViewItem^ none = gcnew ListViewItem("(" + (String::IsNullOrEmpty(why) ? "nothing in scope here" : why) + ")");
                ValueMarks::Muted(none);
                list->Items->Add(none);
            }
            list->EndUpdate();
        }

        virtual void ClearView() override
        {
            list->Items->Clear();
            stopLabel->Text = String::Empty;
            marks->Forget();
        }

    private:
        ValueMarks^ marks;
        Label^ stopLabel;
        Label^ keysLabel;
        TextBox^ setBox;
        ListView^ list;

        void OnDouble(Object^ sender, EventArgs^ e)
        {
            if (list->SelectedItems->Count == 0) return;
            DebugVariable^ v = dynamic_cast<DebugVariable^>(list->SelectedItems[0]->Tag);
            if (v == nullptr) return;
            setBox->Text = v->Name + " = " + v->Value;
            setBox->Focus();
            setBox->Select(setBox->Text->Length, 0);
        }
        void OnListKey(Object^ sender, KeyEventArgs^ e)
        {
            if (e->KeyCode == Keys::Enter) { e->SuppressKeyPress = true; OnDouble(sender, e); }
        }
        void OnSetKey(Object^ sender, KeyEventArgs^ e)
        {
            if (e->KeyCode == Keys::Escape) { setBox->Clear(); e->SuppressKeyPress = true; return; }
            if (e->KeyCode != Keys::Enter) return;
            e->SuppressKeyPress = true;
            String^ text = setBox->Text;
            int eq = text->IndexOf(L'=');
            if (eq <= 0) return;
            String^ name = text->Substring(0, eq)->Trim();
            String^ value = text->Substring(eq + 1)->Trim();
            if (name->Length == 0 || value->Length == 0) return;
            setBox->Clear();
            SetRequested(name, value);
        }
    };

    // -------------------------------------------------------------------------
    //  CallStackPane - Function | File | Line, the frame looked at in bold on its own colour;
    //  a double-click (or Enter) looks at that frame, the top line comes back to the stop
    // -------------------------------------------------------------------------
    public ref class CallStackPane : public DebugPaneBase
    {
    public:
        CallStackPane() : DebugPaneBase("CallStack", "Call Stack", "function")
        {
            list = PaneUtil::MakeList(gcnew array<String^>{ "Function", "File", "Line" }, gcnew array<int>{ 260, 220, 60 });
            list->DoubleClick += gcnew EventHandler(this, &CallStackPane::OnDoubleClick);
            list->KeyDown += gcnew KeyEventHandler(this, &CallStackPane::OnKeyDown);
            Controls->Add(list);
        }
        event IndexChosenHandler^ FrameChosen;

        virtual void UpdateFrom(DebugEngine^ d) override
        {
            list->BeginUpdate();
            list->Items->Clear();
            if (d != nullptr && d->State == DebugState::Break)
            {
                int looking = d->CurrentFrame;
                int i = 0;
                for each (DebugFrame^ f in d->CallStack())
                {
                    ListViewItem^ it = gcnew ListViewItem(f->Function);
                    it->SubItems->Add(String::IsNullOrEmpty(f->File) ? String::Empty : Path::GetFileName(f->File));
                    it->SubItems->Add(f->Line > 0 ? f->Line.ToString() : String::Empty);
                    it->Tag = f;
                    if (i == looking)
                    {
                        it->Font = gcnew SD::Font(list->Font, FontStyle::Bold);
                        it->BackColor = SD::Color::FromArgb(255, 243, 176);
                        it->ForeColor = SD::Color::Black;
                    }
                    list->Items->Add(it);
                    i++;
                }
            }
            list->EndUpdate();
        }
        virtual void ClearView() override { list->Items->Clear(); }
    private:
        ListView^ list;
        void OnDoubleClick(Object^ sender, EventArgs^ e)
        {
            if (list->SelectedItems->Count == 0) return;
            FrameChosen(list->SelectedItems[0]->Index);
        }
        void OnKeyDown(Object^ sender, KeyEventArgs^ e)
        {
            if (e->KeyCode == Keys::Enter) { e->SuppressKeyPress = true; OnDoubleClick(sender, e); }
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
                if (regs->Count == 0)
                {
                    ListViewItem^ none = gcnew ListViewItem("(no registers from " + d->Backend + ")");
                    ValueMarks::Muted(none);
                    list->Items->Add(none);
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
            address->Text = String::Empty;      // until one is typed: the first local variable's
            address->Width = 160;
            chosen = false;
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
            // nothing typed yet: where the stop's first variable lives (a 64-bit stack address on Windows,
            // the C6747's own on the simulators), so the pane shows something of the program's
            if (!chosen)
                for each (DebugVariable^ v in d->Locals())
                    if (!String::IsNullOrEmpty(v->Address) && v->Address->StartsWith("0x")) { address->Text = v->Address; break; }
            if (address->Text->Trim()->Length == 0) { view->Text = "Type an address and press Enter."; return; }
            UInt64 addr = 0;
            String^ a = address->Text->Trim()->ToLowerInvariant()->Replace("0x", "")->Replace("`", "");
            if (!UInt64::TryParse(a, NumberStyles::HexNumber, CultureInfo::InvariantCulture, addr)) { view->Text = "Invalid address."; return; }
            array<Byte>^ b = d->ReadMemory(addr, 256);
            if (b->Length == 0) { view->Text = "(no memory at 0x" + addr.ToString("x") + " from " + d->Backend + ")"; return; }
            StringBuilder^ sb = gcnew StringBuilder();
            String^ wide = addr + (UInt64)b->Length > 0xFFFFFFFFull ? "{0:X16}  " : "{0:X8}  ";
            for (int row = 0; row < b->Length; row += 16)
            {
                sb->AppendFormat(wide, addr + (UInt64)row);
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
        bool chosen;                        // an address was typed: kept from stop to stop
        void OnGo(Object^ sender, EventArgs^ e) { chosen = address->Text->Trim()->Length > 0; UpdateFrom(engine); }
        void OnAddressKey(Object^ sender, KeyEventArgs^ e) { if (e->KeyCode == Keys::Enter) { e->SuppressKeyPress = true; chosen = address->Text->Trim()->Length > 0; UpdateFrom(engine); } }
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
