#pragma once
// ============================================================================
//  GRIDE 2.0 - the debug panes (R10: GRIDEd's UI\Panes\DebugPanes.h ported as
//  plain panels on the bottom tabs, RIDE's DebugView in them)
//    LocalsPane     the stop line on top ("stopped at f.c:11 in main", and "the
//                   variables are main's, at ..." when a frame above is looked
//                   at), Name | Value | Type | Address, the engine's reason in
//                   brackets when there are none, the keys line; the line above
//                   it sets a variable ("name = value", Enter)
//    WatchPane      the engine's watches, read at every stop and frame change;
//                   the line under the list adds one, Enter on a watch brings it
//                   there, an empty answer removes it; refused values grey;
//                   "add a watch..." last
//    CallStackPane  Function | File | Line, the frame looked at bold; a double-
//                   click (or Enter) looks at that frame
//    RegistersPane, MemoryPane, BreakpointsPane
//  A value changed since the stop before is drawn red and bold (RIDE's NewStop /
//  Mark), compared only at the frame stopped in.
// ============================================================================
#include "../Debug/ShalimarSession.h"

namespace Gride
{
    public delegate void IndexChosenHandler(int index);
    public delegate void VariableSetHandler(String^ name, String^ value);
    public delegate void NavigateHandler(String^ file, int line);

    public ref class PaneUtil abstract sealed
    {
    public:
        static ListView^ MakeList(array<String^>^ columns, array<int>^ widths)
        {
            ListView^ lv = gcnew ListView();
            lv->Dock = DockStyle::Fill;
            lv->View = System::Windows::Forms::View::Details;
            lv->FullRowSelect = true;
            lv->HideSelection = false;
            lv->GridLines = false;
            lv->HeaderStyle = ColumnHeaderStyle::Nonclickable;
            for (int i = 0; i < columns->Length; i++) lv->Columns->Add(columns[i], widths[i]);
            return lv;
        }
        static System::Drawing::Font^ MonoFont() { return gcnew System::Drawing::Font("Consolas", 9.0f); }
    };

    public ref class DebugPaneBase abstract : public Panel
    {
    public:
        DebugPaneBase() { Dock = DockStyle::Fill; }
        virtual void UpdateFrom(DebugEngine^ d) abstract;
        virtual void ClearView() abstract;
    };

    // RIDE's DebugView rows: a value changed since the stop before is drawn red and bold; what was seen at
    // the frame stopped in becomes what the next stop is compared with (NewStop), so a look up the stack
    // never counts as a change
    public ref class ValueMarks
    {
    public:
        ValueMarks() { before = gcnew Dictionary<String^, String^>(); seen = gcnew Dictionary<String^, String^>(); }
        void NewStop() { before = seen; seen = gcnew Dictionary<String^, String^>(); }
        void Forget() { before = gcnew Dictionary<String^, String^>(); seen = gcnew Dictionary<String^, String^>(); }
        // whether the row's value is marked as changed
        bool Mark(ListViewItem^ row, int cell, String^ key, String^ value, bool atStop)
        {
            String^ was;
            bool changed = before->TryGetValue(key, was) && was != value;
            if (changed)
            {
                row->UseItemStyleForSubItems = false;
                row->SubItems[cell]->ForeColor = Color::Red;
                row->SubItems[cell]->Font = gcnew System::Drawing::Font(row->ListView->Font, FontStyle::Bold);
            }
            if (atStop) seen[key] = value;
            return changed;
        }
        // RIDE: a value with no place is "-", a missing value nothing
        static String^ Address(String^ address, String^ value)
        {
            if (!String::IsNullOrEmpty(address)) return address;
            return String::IsNullOrEmpty(value) ? String::Empty : Str("-");
        }
        static void Muted(ListViewItem^ row) { row->ForeColor = Color::Gray; }
    private:
        Dictionary<String^, String^>^ before;
        Dictionary<String^, String^>^ seen;
    };

    // ------------------------------------------------------------------ Watch
    public ref class WatchPane : public DebugPaneBase
    {
    public:
        literal String^ AddWatchRow = "add a watch...";

        WatchPane()
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
        property ListView^ Rows { ListView^ get() { return list; } }
        property TextBox^ AddBox { TextBox^ get() { return addBox; } }
        void FocusAdd() { addBox->Focus(); }
        void NewStop() { marks->NewStop(); }

        // a watch added from the menu (Debug > Watch Expression)
        void AddExpression(String^ text)
        {
            if (String::IsNullOrWhiteSpace(text)) return;
            expressions->Add(text->Trim());
            if (engine != nullptr) engine->AddWatch(text->Trim());
            UpdateFrom(engine);
        }

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
                    if (!w->Ok) { it->UseItemStyleForSubItems = false; it->SubItems[1]->ForeColor = Color::Gray; }
                    else if (d->State == DebugState::Break) marks->Mark(it, 1, String::Concat("watch|", w->Expression), w->Value, d->CurrentFrame == 0);
                }
            else
                for each (String^ x in expressions)
                {
                    ListViewItem^ it = gcnew ListViewItem(x);
                    it->SubItems->Add("not running");
                    ValueMarks::Muted(it);
                    list->Items->Add(it);
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
            UpdateFrom(nullptr);
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

    // ------------------------------------------------------------------ Locals
    public ref class LocalsPane : public DebugPaneBase
    {
    public:
        LocalsPane()
        {
            marks = gcnew ValueMarks();
            stopLabel = gcnew Label();
            stopLabel->Dock = DockStyle::Top;
            stopLabel->Height = 20;
            stopLabel->TextAlign = ContentAlignment::MiddleLeft;
            stopLabel->Font = gcnew System::Drawing::Font(PaneUtil::MonoFont(), FontStyle::Bold);
            keysLabel = gcnew Label();
            keysLabel->Dock = DockStyle::Bottom;
            keysLabel->Height = 18;
            keysLabel->TextAlign = ContentAlignment::MiddleLeft;
            keysLabel->ForeColor = Color::DimGray;
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
        property ListView^ Rows { ListView^ get() { return list; } }
        property TextBox^ SetBox { TextBox^ get() { return setBox; } }
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
                    head = String::Concat(head, "    -    ", DebugTranscript::LookingAt(frames[d->CurrentFrame]));
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
                marks->Mark(it, 1, String::Concat(frameName, "|", v->Name), v->Value, d->CurrentFrame == 0);
            }
            if (vars->Count == 0)
            {
                String^ why = d->WhyNoLocals();
                ListViewItem^ none = gcnew ListViewItem(String::Concat("(", String::IsNullOrEmpty(why) ? Str("nothing in scope here") : why, ")"));
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
            setBox->Text = String::Concat(v->Name, " = ", v->Value);
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

    // ------------------------------------------------------------------ Call Stack
    public ref class CallStackPane : public DebugPaneBase
    {
    public:
        CallStackPane()
        {
            list = PaneUtil::MakeList(gcnew array<String^>{ "Function", "File", "Line" }, gcnew array<int>{ 260, 220, 60 });
            list->DoubleClick += gcnew EventHandler(this, &CallStackPane::OnDoubleClick);
            list->KeyDown += gcnew KeyEventHandler(this, &CallStackPane::OnKeyDown);
            Controls->Add(list);
        }
        event IndexChosenHandler^ FrameChosen;
        property ListView^ Rows { ListView^ get() { return list; } }

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
                        it->Font = gcnew System::Drawing::Font(list->Font, FontStyle::Bold);
                        it->BackColor = Color::FromArgb(255, 243, 176);
                        it->ForeColor = Color::Black;
                    }
                    list->Items->Add(it);
                    i++;
                }
                String^ why = d->WhyNoStackWalk();
                if (!String::IsNullOrEmpty(why)) list->Items->Add(gcnew ListViewItem(String::Concat("(", why, ")")))->ForeColor = Color::Gray;
            }
            list->EndUpdate();
        }
        virtual void ClearView() override { list->Items->Clear(); }
    private:
        ListView^ list;
        void OnDoubleClick(Object^ sender, EventArgs^ e)
        {
            if (list->SelectedItems->Count == 0 || dynamic_cast<DebugFrame^>(list->SelectedItems[0]->Tag) == nullptr) return;
            FrameChosen(list->SelectedItems[0]->Index);
        }
        void OnKeyDown(Object^ sender, KeyEventArgs^ e)
        {
            if (e->KeyCode == Keys::Enter) { e->SuppressKeyPress = true; OnDoubleClick(sender, e); }
        }
    };

    // ------------------------------------------------------------------ Registers
    public ref class RegistersPane : public DebugPaneBase
    {
    public:
        RegistersPane()
        {
            previous = gcnew Dictionary<String^, String^>();
            list = PaneUtil::MakeList(gcnew array<String^>{ "Register", "Value", "Register", "Value" }, gcnew array<int>{ 80, 140, 80, 140 });
            list->Font = PaneUtil::MonoFont();
            Controls->Add(list);
        }
        property ListView^ Rows { ListView^ get() { return list; } }
        virtual void UpdateFrom(DebugEngine^ d) override
        {
            list->BeginUpdate();
            list->Items->Clear();
            if (d != nullptr && d->State == DebugState::Break)
            {
                List<RegisterValue^>^ regs = d->Registers();
                // two registers per row, a changed value red
                for (int i = 0; i < regs->Count; i += 2)
                {
                    ListViewItem^ it = gcnew ListViewItem(regs[i]->Name);
                    it->UseItemStyleForSubItems = false;
                    AddValue(it, regs[i]);
                    if (i + 1 < regs->Count)
                    {
                        it->SubItems->Add(regs[i + 1]->Name);
                        AddValue(it, regs[i + 1]);
                    }
                    list->Items->Add(it);
                }
                if (regs->Count == 0)
                {
                    ListViewItem^ none = gcnew ListViewItem(String::Concat("(no registers from ", d->Backend, ")"));
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
        void AddValue(ListViewItem^ it, RegisterValue^ r)
        {
            ListViewItem::ListViewSubItem^ s = it->SubItems->Add(r->Value);
            String^ old;
            if (previous->TryGetValue(r->Name, old) && old != r->Value) s->ForeColor = Color::Red;
        }
    };

    // ------------------------------------------------------------------ Memory
    public ref class MemoryPane : public DebugPaneBase
    {
    public:
        MemoryPane()
        {
            Panel^ bar = gcnew Panel();
            bar->Dock = DockStyle::Top;
            bar->Height = 26;
            Label^ l = gcnew Label();
            l->Text = "Address:"; l->AutoSize = true; l->Location = Point(4, 6);
            bar->Controls->Add(l);
            address = gcnew TextBox();
            address->Text = "0x11800000";
            address->Location = Point(64, 2);
            address->Width = 130;
            address->KeyDown += gcnew KeyEventHandler(this, &MemoryPane::OnAddressKey);
            bar->Controls->Add(address);
            Button^ go = gcnew Button();
            go->Text = "Go"; go->Location = Point(200, 1); go->Width = 50;
            go->Click += gcnew EventHandler(this, &MemoryPane::OnGo);
            bar->Controls->Add(go);
            view = gcnew TextBox();
            view->Multiline = true;
            view->Dock = DockStyle::Fill;
            view->ReadOnly = true;
            view->WordWrap = false;
            view->ScrollBars = System::Windows::Forms::ScrollBars::Both;
            view->Font = PaneUtil::MonoFont();
            Controls->Add(view);
            Controls->Add(bar);
            engine = nullptr;
        }
        property TextBox^ View { TextBox^ get() { return view; } }
        property TextBox^ Address { TextBox^ get() { return address; } }
        virtual void UpdateFrom(DebugEngine^ d) override
        {
            engine = d;
            if (d == nullptr || d->State != DebugState::Break) { view->Text = "Memory is shown while the program is stopped."; return; }
            UInt32 addr = 0;
            String^ a = address->Text->Trim()->ToLowerInvariant()->Replace("0x", "");
            if (!UInt32::TryParse(a, System::Globalization::NumberStyles::HexNumber, System::Globalization::CultureInfo::InvariantCulture, addr)) { view->Text = "Not an address."; return; }
            array<Byte>^ b = d->ReadMemory(addr, 256);
            if (b->Length == 0) { view->Text = String::Concat("(no memory from ", d->Backend, ")"); return; }
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
                sb->Append(" ")->Append(ascii->ToString())->Append("\r\n");
            }
            view->Text = sb->ToString();
        }
        virtual void ClearView() override { view->Clear(); }
    private:
        TextBox^ address;
        TextBox^ view;
        DebugEngine^ engine;
        void OnGo(Object^ sender, EventArgs^ e) { UpdateFrom(engine); }
        void OnAddressKey(Object^ sender, KeyEventArgs^ e) { if (e->KeyCode == Keys::Enter) { e->SuppressKeyPress = true; UpdateFrom(engine); } }
    };

    // ------------------------------------------------------------------ Breakpoints
    public ref class BreakpointsPane : public Panel
    {
    public:
        BreakpointsPane()
        {
            Dock = DockStyle::Fill;
            list = PaneUtil::MakeList(gcnew array<String^>{ "File", "Line", "Folder" }, gcnew array<int>{ 200, 60, 400 });
            list->DoubleClick += gcnew EventHandler(this, &BreakpointsPane::OnDoubleClick);
            list->KeyDown += gcnew KeyEventHandler(this, &BreakpointsPane::OnKeyDown);
            Controls->Add(list);
        }

        event NavigateHandler^ Navigate;
        event NavigateHandler^ RemoveRequested;
        property ListView^ Rows { ListView^ get() { return list; } }

        void Fill(Dictionary<String^, SortedSet<int>^>^ bps)
        {
            list->BeginUpdate();
            list->Items->Clear();
            for each (KeyValuePair<String^, SortedSet<int>^> kv in bps)
                for each (int line in kv.Value)
                {
                    ListViewItem^ it = gcnew ListViewItem(Path::GetFileName(kv.Key));
                    it->SubItems->Add(line.ToString());
                    it->SubItems->Add(Path::GetDirectoryName(kv.Key));
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
            else if (t != nullptr && e->KeyCode == Keys::Enter) { e->SuppressKeyPress = true; Navigate(t->Item1, t->Item2); }
        }
    };
}
