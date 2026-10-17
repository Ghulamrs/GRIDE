// DebugView.h - the Debug tab of the lower panel, as grids rather than a page of text.
// Stop line on top; Locals and Watch side by side (Name | Value | Type | Address); the Call Stack
// below (Function | File | Line), the frame looked at highlighted; the keys along the bottom.

// A program not stopped - returned, no source, the debugger quiet, no session - is a message in the
// grids' place. Every field comes through the bridge (ride_local_*, ride_watch_*, ride_stack_*), the
// calls the macOS window will make; what a click means is the window's, raised as an event.

// A value changed since the last stop is drawn in colour: the view keeps each frame's values at the
// stop before ("function|name") and NewStop moves today's into the past.
#pragma once

#include "bridge.h"
#include "Marshal.h"

namespace ridegui {

using namespace System;
using namespace System::Windows::Forms;
using System::Collections::Generic::Dictionary;

delegate void IndexChosen(int which);

ref class DebugView : public Panel {
public:
    event IndexChosen^ FrameChosen;
    event IndexChosen^ VariableChosen;
    event IndexChosen^ WatchChosen;
    event EventHandler^ AddWatchChosen;

    literal String^ AddWatchRow = "add a watch...";

    DebugView(TextBox^ message) : message_(message) {
        Dock = DockStyle::Fill;
        before_ = gcnew Dictionary<String^, String^>();
        seen_ = gcnew Dictionary<String^, String^>();
        changed_ = System::Drawing::Color::FromArgb(200, 30, 30);

        grids_ = gcnew Panel();
        grids_->Dock = DockStyle::Fill;
        stop_ = gcnew Label();
        stop_->Name = "debugStop";
        stop_->Dock = DockStyle::Top;
        stop_->Height = 22;
        stop_->TextAlign = System::Drawing::ContentAlignment::MiddleLeft;
        stop_->Font = gcnew System::Drawing::Font("Segoe UI", 9.0f, System::Drawing::FontStyle::Bold);
        keys_ = gcnew Label();
        keys_->Name = "debugKeys";
        keys_->Dock = DockStyle::Bottom;
        keys_->Height = 20;
        keys_->ForeColor = System::Drawing::Color::FromArgb(90, 90, 90);
        keys_->TextAlign = System::Drawing::ContentAlignment::MiddleLeft;

        locals_ = Grid("locals", gcnew array<String^>{"Name", "Value", "Type", "Address"});
        watch_ = Grid("watch", gcnew array<String^>{"Name", "Value", "Type", "Address"});
        stack_ = Grid("stack", gcnew array<String^>{"Function", "File", "Line"});
        locals_->CellDoubleClick += gcnew DataGridViewCellEventHandler(this, &DebugView::OnLocal);
        watch_->CellDoubleClick += gcnew DataGridViewCellEventHandler(this, &DebugView::OnWatch);
        stack_->CellClick += gcnew DataGridViewCellEventHandler(this, &DebugView::OnFrame);

        // Locals | Watch on top, the Call Stack below; each under a caption of its own.
        SplitContainer^ sides = gcnew SplitContainer();
        sides->Dock = DockStyle::Fill;
        sides->Orientation = Orientation::Vertical;
        sides->Panel1->Controls->Add(Captioned("Locals", locals_));
        sides->Panel2->Controls->Add(Captioned("Watch", watch_));
        upDown_ = gcnew SplitContainer();
        upDown_->Dock = DockStyle::Fill;
        upDown_->Orientation = Orientation::Horizontal;
        upDown_->Panel1->Controls->Add(sides);
        upDown_->Panel2->Controls->Add(Captioned("Call Stack", stack_));
        upDown_->Panel1MinSize = 40;
        upDown_->Panel2MinSize = 40;
        // The variables keep three fifths of the height however the panel is resized; the stack the rest.
        upDown_->Resize += gcnew EventHandler(this, &DebugView::Share);
        grids_->Controls->Add(upDown_);
        grids_->Controls->Add(keys_);
        grids_->Controls->Add(stop_);

        message_->Dock = DockStyle::Fill;
        Controls->Add(grids_);
        Controls->Add(message_);
        ShowMessage("");
    }

    // A state with no frame to show: the message alone, where the grids were.
    void ShowMessage(String^ text) {
        message_->Text = text == nullptr ? "" : text;
        grids_->Visible = false;
        message_->Visible = true;
        message_->BringToFront();
    }

    void Clear() { ShowMessage(""); }

    // A new stop: what was seen at the last one becomes what a value is compared with.
    void NewStop() {
        before_ = seen_;
        seen_ = gcnew Dictionary<String^, String^>();
    }

    // A session ended: nothing is compared with a run that is gone.
    void Forget() {
        before_ = gcnew Dictionary<String^, String^>();
        seen_ = gcnew Dictionary<String^, String^>();
    }

    void Fill(RIDEDebugger* debugger, String^ stopLine, String^ keys) {
        int looking = ride_looking_at(debugger);
        String^ looked = FromUtf8(ride_looking_text(debugger));
        stop_->Text = looked->Length > 0 ? stopLine + "    -    " + looked : stopLine;
        keys_->Text = keys;
        String^ frame = FromUtf8(ride_stack_function(debugger, looking));

        locals_->Rows->Clear();
        int howMany = ride_locals_count(debugger);
        for (int i = 0; i < howMany; ++i) {
            String^ name = FromUtf8(ride_local_name(debugger, i));
            String^ value = FromUtf8(ride_local_value(debugger, i));
            int row = locals_->Rows->Add(name, value, FromUtf8(ride_local_type(debugger, i)),
                                         Address(FromUtf8(ride_local_address(debugger, i)), value));
            Mark(locals_->Rows[row], frame + "|" + name, value, looking == 0);
        }
        if (howMany == 0) {
            int row = locals_->Rows->Add(FromUtf8(ride_locals_none_because(debugger))->Trim(), "", "", "");
            locals_->Rows[row]->DefaultCellStyle->ForeColor = System::Drawing::Color::Gray;
        }

        watch_->Rows->Clear();
        int watching = ride_watch_count(debugger);
        for (int i = 0; i < watching; ++i) {
            String^ name = FromUtf8(ride_watch_expression(debugger, i));
            String^ value = FromUtf8(ride_watch_value(debugger, i));
            int row = watch_->Rows->Add(name, value, FromUtf8(ride_watch_type(debugger, i)),
                                        Address(FromUtf8(ride_watch_address(debugger, i)), value));
            if (ride_watch_ok(debugger, i) == 0)
                watch_->Rows[row]->Cells[1]->Style->ForeColor = System::Drawing::Color::Gray;
            else
                Mark(watch_->Rows[row], "watch|" + name, value, looking == 0);
        }
        int add = watch_->Rows->Add(AddWatchRow, "", "", "");
        watch_->Rows[add]->DefaultCellStyle->ForeColor = System::Drawing::Color::Gray;

        stack_->Rows->Clear();
        int deep = ride_stack_count(debugger);
        for (int i = 0; i < deep; ++i) {
            int line = ride_stack_line(debugger, i);
            int row = stack_->Rows->Add(FromUtf8(ride_stack_function(debugger, i)),
                                        System::IO::Path::GetFileName(FromUtf8(ride_stack_file(debugger, i))),
                                        line > 0 ? line.ToString() : "");
            if (i == looking) {
                stack_->Rows[row]->DefaultCellStyle->BackColor = System::Drawing::Color::FromArgb(255, 243, 176);
                stack_->Rows[row]->DefaultCellStyle->Font =
                    gcnew System::Drawing::Font(stack_->Font, System::Drawing::FontStyle::Bold);
            }
        }
        for each (DataGridView^ grid in gcnew array<DataGridView^>{locals_, watch_, stack_})
            grid->ClearSelection();

        message_->Visible = false;
        grids_->Visible = true;
        grids_->BringToFront();
    }

    void FocusGrids() { (grids_->Visible ? static_cast<Control^>(locals_) : message_)->Focus(); }

private:
    TextBox^ message_;
    Panel^ grids_;
    SplitContainer^ upDown_;
    Label^ stop_;
    Label^ keys_;
    DataGridView^ locals_;
    DataGridView^ watch_;
    DataGridView^ stack_;
    Dictionary<String^, String^>^ before_;
    Dictionary<String^, String^>^ seen_;
    System::Drawing::Color changed_;

    // Shalimar says names only: no value, so no address either - "-" is kept for a value with no place.
    void Share(Object^, EventArgs^) {
        int room = upDown_->Height - upDown_->SplitterWidth;
        if (room > 80) upDown_->SplitterDistance = Math::Max(40, Math::Min(room - 40, room * 3 / 5));
    }

    static String^ Address(String^ address, String^ value) {
        if (address->Length > 0) return address;
        return value->Length > 0 ? "-" : "";
    }

    // Remembered only at the frame stopped in, so a look up the stack never counts as a change.
    void Mark(DataGridViewRow^ row, String^ key, String^ value, bool atStop) {
        String^ was;
        if (before_->TryGetValue(key, was) && was != value) {
            row->Cells[1]->Style->ForeColor = changed_;
            row->Cells[1]->Style->Font = gcnew System::Drawing::Font(locals_->Font, System::Drawing::FontStyle::Bold);
        }
        if (atStop) seen_[key] = value;
    }

    static DataGridView^ Grid(String^ name, array<String^>^ columns) {
        DataGridView^ grid = gcnew DataGridView();
        grid->Name = name;
        grid->AccessibleName = name;
        grid->Dock = DockStyle::Fill;
        grid->ReadOnly = true;
        grid->AllowUserToAddRows = false;
        grid->AllowUserToDeleteRows = false;
        grid->AllowUserToResizeRows = false;
        grid->RowHeadersVisible = false;
        grid->SelectionMode = DataGridViewSelectionMode::FullRowSelect;
        grid->MultiSelect = false;
        grid->BackgroundColor = System::Drawing::SystemColors::Window;
        grid->BorderStyle = System::Windows::Forms::BorderStyle::None;
        grid->Font = gcnew System::Drawing::Font("Consolas", 9.5f);
        grid->AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode::Fill;
        grid->RowTemplate->Height = 18;
        grid->ColumnHeadersHeight = 20;
        grid->ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode::DisableResizing;
        for each (String^ column in columns) grid->Columns->Add(column, column);
        for each (DataGridViewColumn^ column in grid->Columns)
            column->SortMode = DataGridViewColumnSortMode::NotSortable;
        return grid;
    }

    static Panel^ Captioned(String^ caption, Control^ inside) {
        Panel^ panel = gcnew Panel();
        panel->Dock = DockStyle::Fill;
        Label^ label = gcnew Label();
        label->Text = caption;
        label->Dock = DockStyle::Top;
        label->Height = 18;
        label->ForeColor = System::Drawing::Color::FromArgb(60, 60, 60);
        panel->Controls->Add(inside);
        panel->Controls->Add(label);
        return panel;
    }

    void OnLocal(Object^, DataGridViewCellEventArgs^ e) {
        if (e->RowIndex >= 0 && e->RowIndex < locals_->Rows->Count &&
            !String::IsNullOrEmpty(safe_cast<String^>(locals_->Rows[e->RowIndex]->Cells[1]->Value)))
            VariableChosen(e->RowIndex);
    }

    void OnWatch(Object^, DataGridViewCellEventArgs^ e) {
        if (e->RowIndex < 0) return;
        if (e->RowIndex == watch_->Rows->Count - 1) { AddWatchChosen(this, EventArgs::Empty); return; }
        WatchChosen(e->RowIndex);
    }

    void OnFrame(Object^, DataGridViewCellEventArgs^ e) {
        if (e->RowIndex >= 0) FrameChosen(e->RowIndex);
    }
};

}
