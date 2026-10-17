// ErrorList.h - the Errors tab: a build's errors and warnings as a table, File | Line | Column | Message.
// The output is read line by line with ride_parse_diagnostic, the parser the macOS window's issue list
// uses, and a line not read alone is tried with the one before it (cc1's two-line preprocessor form).

// The error the build itself reported is added at the top when the reading missed it, as macOS does.
// A click raises IssueChosen with the row's file, line and column; finding the file is the window's.
#pragma once

#include "bridge.h"
#include "Marshal.h"

namespace ridegui {

using namespace System;
using namespace System::Windows::Forms;

delegate void PlaceChosen(String^ file, int line, int column);

ref class ErrorList : public DataGridView {
public:
    event PlaceChosen^ IssueChosen;

    ErrorList() {
        Name = "errors";
        AccessibleName = "errors";
        Dock = DockStyle::Fill;
        ReadOnly = true;
        AllowUserToAddRows = false;
        AllowUserToDeleteRows = false;
        AllowUserToResizeRows = false;
        RowHeadersVisible = false;
        SelectionMode = DataGridViewSelectionMode::FullRowSelect;
        MultiSelect = false;
        BackgroundColor = System::Drawing::SystemColors::Window;
        BorderStyle = System::Windows::Forms::BorderStyle::None;
        Font = gcnew System::Drawing::Font("Consolas", 9.5f);
        RowTemplate->Height = 20;
        Columns->Add("File", "File");
        Columns->Add("Line", "Line");
        Columns->Add("Column", "Column");
        Columns->Add("Message", "Message");
        Columns[0]->Width = 180;
        Columns[1]->Width = 60;
        Columns[2]->Width = 60;
        Columns[3]->AutoSizeMode = DataGridViewAutoSizeColumnMode::Fill;
        for each (DataGridViewColumn^ column in Columns) column->SortMode = DataGridViewColumnSortMode::NotSortable;
        files_ = gcnew System::Collections::Generic::List<String^>();
        errors_ = 0;
        CellClick += gcnew DataGridViewCellEventHandler(this, &ErrorList::OnRow);
    }

    // Every diagnostic in output; source is the file built, or empty for a project.
    // Answers how many errors (not warnings) there were, for the tab's title.
    int Collect(String^ output, String^ source) {
        Rows->Clear();
        files_ = gcnew System::Collections::Generic::List<String^>();
        errors_ = 0;
        Utf8 sourceBytes(source == nullptr ? "" : source);
        array<String^>^ all = (output == nullptr ? "" : output)->Replace("\r\n", "\n")->Split('\n');
        String^ previous = "";
        for each (String^ line in all) {
            if (!Read(line, sourceBytes) && previous->Length > 0) Read(previous + "\n" + line, sourceBytes);
            previous = line;
        }
        return errors_;
    }

    // The error the build reported, when reading the output did not find it.
    void Reported(String^ file, int line, int column, String^ message) {
        for (int i = 0; i < Rows->Count; ++i)
            if (String::Equals(safe_cast<String^>(Rows[i]->Cells[3]->Value), message) &&
                safe_cast<String^>(Rows[i]->Cells[1]->Value) == line.ToString())
                return;
        Rows->Insert(0, gcnew array<Object^>{Leaf(file), line.ToString(), column.ToString(), message});
        files_->Insert(0, file);
        ++errors_;
    }

    int Errors() { return errors_; }

private:
    System::Collections::Generic::List<String^>^ files_;
    int errors_;

    static String^ Leaf(String^ file) {
        return String::IsNullOrEmpty(file) ? "" : System::IO::Path::GetFileName(file);
    }

    bool Read(String^ line, Utf8% source) {
        Utf8 text(line);
        int row = 0, column = 0;
        const char* file = "";
        const char* message = "";
        if (ride_parse_diagnostic(text.c(), source.c(), &row, &column, &file, &message) == 0) return false;
        String^ named = FromUtf8(file);
        String^ said = FromUtf8(message);
        for (int i = 0; i < Rows->Count; ++i)
            if (String::Equals(safe_cast<String^>(Rows[i]->Cells[3]->Value), said) &&
                safe_cast<String^>(Rows[i]->Cells[1]->Value) == row.ToString())
                return true;
        bool warning = line->Contains("warning") && !line->Contains("error");
        int at = Rows->Add(Leaf(named), row.ToString(), column > 0 ? column.ToString() : "", said);
        Rows[at]->Cells[3]->Style->ForeColor = warning ? System::Drawing::Color::FromArgb(150, 100, 0)
                                                       : System::Drawing::Color::FromArgb(190, 20, 20);
        files_->Add(named);
        if (!warning) ++errors_;
        return true;
    }

    void OnRow(Object^, DataGridViewCellEventArgs^ e) {
        if (e->RowIndex < 0 || files_ == nullptr || e->RowIndex >= files_->Count) return;
        int line = 0, column = 1;
        Int32::TryParse(safe_cast<String^>(Rows[e->RowIndex]->Cells[1]->Value), line);
        if (!Int32::TryParse(safe_cast<String^>(Rows[e->RowIndex]->Cells[2]->Value), column)) column = 1;
        IssueChosen(files_[e->RowIndex], line, column);
    }
};

}
