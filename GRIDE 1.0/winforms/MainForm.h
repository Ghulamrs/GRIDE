#pragma once

#include "bridge.h"
#include "children.h"
#include "Marshal.h"
#include "TextFile.h"
#include "DebugView.h"
#include "ErrorList.h"
#include <cstring>
#include <msclr/auto_handle.h>

namespace ridegui {

using namespace System;
using namespace System::Windows::Forms;

// One open file: its box and gutter, and what belongs to it rather than to the window.
ref class Sheet {
public:
    String^ path;
    RichTextBox^ box;
    Panel^ gutter;
    TabPage^ page;
    TextFile^ file;        // how it was on disk, so that a save writes it the same way
    int language;          // the Language menu's choice for this file, or -1 for its suffix's
    int lines;             // how many lines, kept by OnTextChanged so nobody asks the box for all of them
    String^ saved;         // the text as it is on disk, so an undo back to it is not a change
    DateTime stamp;        // the file's write time when it was read or written here

    Sheet() : file(gcnew TextFile()), language(-1), lines(1), stamp(DateTime::MinValue) {}
};

ref class Gutter : public Panel {
public:
    Gutter() { DoubleBuffered = true; }
};

value struct Spot {
    int x;
    int y;
};

// The file tabs, with a close × in the tab view itself: it owner-draws each tab, hit-tests the × and raises TabCloseRequested(index), so the close button is part of the control.
public delegate void TabCloseHandler(int index);

// The build's question - the project's own masm/link/lnk6x did not build it, use the native tools
// instead? - as the window asks it. A plain function so the bridge can hold its address;
// MessageBox needs no owner, and the build may be on the worker thread, whose own message loop shows it.
static int AskNativeInWindow(const char* question) {
    String^ text = gcnew String(question, 0, static_cast<int>(std::strlen(question)),
                                System::Text::Encoding::UTF8);
    return MessageBox::Show(text, gcnew String(ride_product_name()), MessageBoxButtons::YesNo, MessageBoxIcon::Question,
                            MessageBoxDefaultButton::Button1) == System::Windows::Forms::DialogResult::Yes ? 1 : 0;
}

public ref class ClosableTabControl : public System::Windows::Forms::TabControl {
public:
    event TabCloseHandler^ TabCloseRequested;

    ClosableTabControl() {
        this->DrawMode = System::Windows::Forms::TabDrawMode::OwnerDrawFixed;
        this->Padding = System::Drawing::Point(16, 3);
    }

protected:
    System::Drawing::Rectangle CloseRect(System::Drawing::Rectangle tab) {
        int s = 14;
        return System::Drawing::Rectangle(tab.Right - s - 4,
                                          tab.Y + (tab.Height - s) / 2, s, s);
    }

    virtual void OnDrawItem(System::Windows::Forms::DrawItemEventArgs^ e) override {
        if (e->Index < 0 || e->Index >= this->TabCount) return;
        System::Drawing::Rectangle r = this->GetTabRect(e->Index);
        bool selected = (e->Index == this->SelectedIndex);
        e->Graphics->FillRectangle(selected ? System::Drawing::SystemBrushes::Window
                                            : System::Drawing::SystemBrushes::Control, r);
        System::Drawing::Rectangle textRect(r.X + 6, r.Y, r.Width - 26, r.Height);
        System::Windows::Forms::TextRenderer::DrawText(
            e->Graphics, this->TabPages[e->Index]->Text, this->Font, textRect,
            System::Drawing::SystemColors::ControlText,
            static_cast<System::Windows::Forms::TextFormatFlags>(
                static_cast<int>(System::Windows::Forms::TextFormatFlags::Left) |
                static_cast<int>(System::Windows::Forms::TextFormatFlags::VerticalCenter) |
                static_cast<int>(System::Windows::Forms::TextFormatFlags::EndEllipsis)));
        System::Windows::Forms::TextRenderer::DrawText(
            e->Graphics, gcnew System::String(static_cast<wchar_t>(0x00D7), 1), this->Font, CloseRect(r),
            System::Drawing::Color::FromArgb(110, 110, 110),
            static_cast<System::Windows::Forms::TextFormatFlags>(
                static_cast<int>(System::Windows::Forms::TextFormatFlags::HorizontalCenter) |
                static_cast<int>(System::Windows::Forms::TextFormatFlags::VerticalCenter)));
    }

    virtual void OnMouseDown(System::Windows::Forms::MouseEventArgs^ e) override {
        // The left button closes; the right and the middle ones over the × are not a click on it.
        for (int i = 0; e->Button == System::Windows::Forms::MouseButtons::Left && i < this->TabCount; ++i) {
            if (CloseRect(this->GetTabRect(i)).Contains(e->Location)) {
                TabCloseRequested(i);
                return;
            }
        }
        System::Windows::Forms::TabControl::OnMouseDown(e);
    }
};

// One piece of work for the worker thread. Everything it reads is pinned here, on the window's
// thread, before it starts - the worker never reads the window's own fields, which the window may
// change while it waits. What it makes is left on the job for the window to take and free.
ref class Job {
public:
    literal int Build = 1;          // one file compiled: build
    literal int BuildTarget = 3;    // the project built: build
    literal int Convert = 5;        // c2s: converted
    literal int BuildProgram = 6;   // one file built for the debugger: made
    literal int DebugStart = 7;     // the debugger started on program: result
    literal int DebugGo = 8;
    literal int DebugResume = 9;
    literal int StepOver = 10;
    literal int StepInto = 11;
    literal int StepOut = 12;

    int what;
    Toolchain^ tools;
    Utf8^ source;       // the file, or the converter's input
    Utf8^ program;      // the program to run or debug, or c2s itself
    Utf8^ into;         // c2s's output
    int kind;
    int language;
    int config;
    int toolKind;
    int toShalimar;

    RIDEBuild* build;
    RIDEProgram* made;
    RIDEConversion* converted;
    int result;
    bool stopped;       // Build > Stop ended it: what it made is what was there when it died

    Job(int what) : what(what), build(nullptr), made(nullptr), converted(nullptr),
                    result(0), stopped(false) {}
    ~Job() { delete tools; delete source; delete program; delete into; }
};

// The bridge's RIDEOutput for a program the window runs: defined after MainForm, which it posts to.
void OutputToWindow(void* user, const char* bytes, int size, int stream);

// ---- Compiler Options: a tabbed dialog drawn from the bridge's table (src/options.h) ------------
// One tab per compiler and one row per option; what each does, or why it is greyed for this target,
// is its tooltip. It edits the bridge's draft: OK pulls the controls into it, Commit writes it.
// Edit > Find in Files: what to look for, where, in which files - or file names alone, Find files.
public ref class FindFilesDialog : public Form {
public:
    TextBox^ text;
    TextBox^ folder;
    TextBox^ types;
    CheckBox^ matchCase;
    CheckBox^ wholeWord;
    CheckBox^ subfolders;
    CheckBox^ namesOnly;

    // What was asked last time comes in from the window, which keeps it: no static state in this class.
    FindFilesDialog(String^ lookFor, String^ where, String^ lastTypes, bool lastCase, bool lastWord, bool lastSub) {
        Text = "Find in Files";
        FormBorderStyle = System::Windows::Forms::FormBorderStyle::FixedDialog;
        MaximizeBox = false;
        MinimizeBox = false;
        ShowInTaskbar = false;
        StartPosition = FormStartPosition::CenterParent;
        ClientSize = System::Drawing::Size(520, 230);
        Font = gcnew System::Drawing::Font("Segoe UI", 9.0f);

        text = Field("Find what:", 16, lookFor);
        folder = Field("Look in:", 50, where);
        folder->Width = 300;
        Button^ browse = gcnew Button();
        browse->Text = "Browse...";
        browse->SetBounds(418, 48, 86, 25);
        browse->Click += gcnew EventHandler(this, &FindFilesDialog::OnBrowse);
        Controls->Add(browse);
        types = Field("File types:", 84, lastTypes);

        matchCase = Box("Match case", 20, 122, lastCase);
        wholeWord = Box("Whole word", 140, 122, lastWord);
        subfolders = Box("Include subfolders", 260, 122, lastSub);
        namesOnly = Box("Find files by name - not what is in them", 20, 152, false);

        Button^ ok = gcnew Button();
        ok->Text = "Find";
        ok->DialogResult = System::Windows::Forms::DialogResult::OK;
        ok->SetBounds(332, 190, 84, 28);
        Controls->Add(ok);
        Button^ cancel = gcnew Button();
        cancel->Text = "Cancel";
        cancel->DialogResult = System::Windows::Forms::DialogResult::Cancel;
        cancel->SetBounds(420, 190, 84, 28);
        Controls->Add(cancel);
        AcceptButton = ok;
        CancelButton = cancel;
    }

private:

    TextBox^ Field(String^ label, int y, String^ value) {
        Label^ l = gcnew Label();
        l->Text = label;
        l->SetBounds(16, y + 3, 80, 20);
        Controls->Add(l);
        TextBox^ box = gcnew TextBox();
        box->SetBounds(104, y, 400, 23);
        box->Text = value != nullptr ? value : "";
        Controls->Add(box);
        return box;
    }

    CheckBox^ Box(String^ label, int x, int y, bool on) {
        CheckBox^ box = gcnew CheckBox();
        box->Text = label;
        box->AutoSize = true;
        box->Location = System::Drawing::Point(x, y);
        box->Checked = on;
        Controls->Add(box);
        return box;
    }

    void OnBrowse(Object^, EventArgs^) {
        FolderBrowserDialog^ pick = gcnew FolderBrowserDialog();
        pick->SelectedPath = folder->Text;
        if (pick->ShowDialog(this) == System::Windows::Forms::DialogResult::OK) folder->Text = pick->SelectedPath;
    }
};

public ref class OptionsDialog : public Form {
public:
    OptionsDialog(RIDEProject* project, int config, String^ arch, int tab)
        : project_(project), config_(config), arch_(arch), pushing_(false) {
        controls_ = gcnew System::Collections::Generic::Dictionary<String^, Control^>();
        tips_ = gcnew ToolTip();
        ride_options_begin(project_);
        Build();
        if (ccsText_ != nullptr) tab = 0;   // the CCS page first: it is what there is to read
        if (tab >= 0 && tab < tabs_->TabCount) tabs_->SelectedIndex = tab;
        Push();
    }

    bool Commit() { return ride_options_commit(project_) != 0; }

private:
    RIDEProject* project_;
    int config_;
    String^ arch_;
    bool pushing_;
    TabControl^ tabs_;
    ComboBox^ configPick_;
    TextBox^ preview_;
    TextBox^ ccsText_;
    System::Collections::Generic::Dictionary<String^, Control^>^ controls_;
    ToolTip^ tips_;

    static array<Byte>^ Z(String^ text) {
        array<Byte>^ raw = System::Text::Encoding::UTF8->GetBytes(text == nullptr ? "" : text);
        array<Byte>^ out = gcnew array<Byte>(raw->Length + 1);
        Array::Copy(raw, out, raw->Length);
        return out;
    }

    String^ Value(String^ id) {
        array<Byte>^ key = Z(id);
        pin_ptr<Byte> k = &key[0];
        return FromUtf8(ride_options_value(project_, config_, reinterpret_cast<const char*>(k)));
    }

    void Set(String^ id, String^ value) {
        array<Byte>^ key = Z(id);
        array<Byte>^ val = Z(value);
        pin_ptr<Byte> k = &key[0];
        pin_ptr<Byte> v = &val[0];
        ride_options_set(project_, config_, reinterpret_cast<const char*>(k), reinterpret_cast<const char*>(v));
    }

    void Build() {
        Text = "Compiler Options";
        FormBorderStyle = System::Windows::Forms::FormBorderStyle::FixedDialog;
        StartPosition = System::Windows::Forms::FormStartPosition::CenterParent;
        MinimizeBox = false;
        MaximizeBox = false;
        ClientSize = System::Drawing::Size(640, 490);

        // Which configuration's options are edited - not which one builds, which is Build > Debug/Release.
        Label^ forLabel = gcnew Label();
        forLabel->Text = "Edit options for:";
        forLabel->SetBounds(12, 16, 110, 20);
        Controls->Add(forLabel);
        configPick_ = gcnew ComboBox();
        configPick_->DropDownStyle = ComboBoxStyle::DropDownList;
        configPick_->Items->Add("Debug");
        configPick_->Items->Add("Release");
        configPick_->SetBounds(124, 12, 120, 24);
        configPick_->SelectedIndex = config_ == RIDE_CONFIG_RELEASE ? 1 : 0;
        configPick_->SelectedIndexChanged += gcnew EventHandler(this, &OptionsDialog::ConfigChosen);
        Controls->Add(configPick_);
        Label^ target = gcnew Label();
        target->Text = "Target: " + arch_ + "  (the Target menu)";
        target->SetBounds(270, 16, 350, 20);
        Controls->Add(target);

        tabs_ = gcnew TabControl();
        tabs_->SetBounds(12, 48, 616, 290);
        array<Byte>^ archBytes = Z(arch_);
        pin_ptr<Byte> archPin = &archBytes[0];
        const char* arch = reinterpret_cast<const char*>(archPin);
        for (int t = 0; t < ride_option_tab_count(); ++t) {
            String^ name = FromUtf8(ride_option_tab_name(t));
            TabPage^ page = gcnew TabPage(name);
            int y = 14;
            for (int i = 0; i < ride_option_count(); ++i) {
                if (FromUtf8(ride_option_tab(i)) != name) continue;
                String^ id = FromUtf8(ride_option_id(i));
                String^ label = FromUtf8(ride_option_label(i));
                int kind = ride_option_control(i);
                Control^ made;
                if (kind == RIDE_OPTION_CHECK) {
                    CheckBox^ box = gcnew CheckBox();
                    box->Text = label;
                    box->SetBounds(12, y, 570, 24);
                    box->CheckedChanged += gcnew EventHandler(this, &OptionsDialog::Changed);
                    made = box;
                } else {
                    Label^ title = gcnew Label();
                    title->Text = label;
                    title->SetBounds(12, y + 4, 200, 20);
                    page->Controls->Add(title);
                    if (kind == RIDE_OPTION_CHOICE) {
                        ComboBox^ pick = gcnew ComboBox();
                        pick->DropDownStyle = ComboBoxStyle::DropDownList;
                        for each (String^ choice in FromUtf8(ride_option_choices(i))->Split('|')) pick->Items->Add(choice);
                        pick->SetBounds(220, y, 200, 24);
                        pick->SelectedIndexChanged += gcnew EventHandler(this, &OptionsDialog::Changed);
                        made = pick;
                    } else {
                        TextBox^ field = gcnew TextBox();
                        field->SetBounds(220, y, 360, 24);
                        field->TextChanged += gcnew EventHandler(this, &OptionsDialog::Changed);
                        made = field;
                    }
                }
                array<Byte>^ key = Z(id);
                pin_ptr<Byte> k = &key[0];
                bool usable = ride_options_available(reinterpret_cast<const char*>(k), arch) != 0;
                String^ tip = usable ? FromUtf8(ride_option_hint(i))
                                     : "Unavailable: " + FromUtf8(ride_options_why(reinterpret_cast<const char*>(k), arch));
                made->Enabled = usable;
                if (tip->Length > 0) tips_->SetToolTip(made, tip);
                made->Tag = id;
                page->Controls->Add(made);
                controls_[id] = made;
                y += 34;
            }
            tabs_->TabPages->Add(page);
        }
        // A CCS project's options are read from its files and shown, not edited: a page of what
        // was read and where each option went, every control greyed, and OK keeps nothing (bridge.h).
        if (ride_project_is_ccs(project_) != 0) {
            TabPage^ page = gcnew TabPage("CCS project");
            ccsText_ = gcnew TextBox();
            ccsText_->ReadOnly = true;
            ccsText_->Multiline = true;
            ccsText_->ScrollBars = ScrollBars::Vertical;
            ccsText_->Font = gcnew System::Drawing::Font("Consolas", 9.0f);
            ccsText_->Dock = DockStyle::Fill;
            ccsText_->Text = FromUtf8(ride_project_ccs_mapping(project_, config_))->Replace("\n", "\r\n");
            page->Controls->Add(ccsText_);
            tabs_->TabPages->Insert(0, page);
            for each (System::Collections::Generic::KeyValuePair<String^, Control^> pair in controls_) {
                pair.Value->Enabled = false;
                tips_->SetToolTip(pair.Value, "Read from the CCS project - edit it in CCS");
            }
        }
        tabs_->SelectedIndexChanged += gcnew EventHandler(this, &OptionsDialog::TabChosen);
        Controls->Add(tabs_);

        Label^ heading = gcnew Label();
        heading->Text = "Command line:";
        heading->SetBounds(12, 350, 200, 20);
        Controls->Add(heading);
        preview_ = gcnew TextBox();
        preview_->ReadOnly = true;
        preview_->Multiline = true;
        preview_->Font = gcnew System::Drawing::Font("Consolas", 9.0f);
        preview_->SetBounds(12, 372, 616, 52);
        Controls->Add(preview_);

        Button^ reset = gcnew Button();
        reset->Text = "Restore defaults";
        reset->SetBounds(12, 444, 140, 30);
        reset->Click += gcnew EventHandler(this, &OptionsDialog::RestoreDefaults);
        reset->Enabled = ride_project_is_ccs(project_) == 0;
        Controls->Add(reset);
        Button^ ok = gcnew Button();
        ok->Text = "OK";
        ok->DialogResult = System::Windows::Forms::DialogResult::OK;
        ok->SetBounds(466, 444, 78, 30);
        ok->Click += gcnew EventHandler(this, &OptionsDialog::Accepted);
        Controls->Add(ok);
        Button^ cancel = gcnew Button();
        cancel->Text = "Cancel";
        cancel->DialogResult = System::Windows::Forms::DialogResult::Cancel;
        cancel->SetBounds(550, 444, 78, 30);
        Controls->Add(cancel);
        AcceptButton = ok;
        CancelButton = cancel;
    }

    // The draft's values for the configuration shown, into the controls; and back.
    void Push() {
        pushing_ = true;
        for each (System::Collections::Generic::KeyValuePair<String^, Control^> pair in controls_) {
            String^ value = Value(pair.Key);
            CheckBox^ box = dynamic_cast<CheckBox^>(pair.Value);
            ComboBox^ pick = dynamic_cast<ComboBox^>(pair.Value);
            if (box != nullptr) box->Checked = value == "1";
            else if (pick != nullptr) pick->SelectedIndex = pick->Items->IndexOf(value);
            else pair.Value->Text = value;
        }
        pushing_ = false;
        ShowPreview();
    }

    void Pull() {
        for each (System::Collections::Generic::KeyValuePair<String^, Control^> pair in controls_) {
            CheckBox^ box = dynamic_cast<CheckBox^>(pair.Value);
            ComboBox^ pick = dynamic_cast<ComboBox^>(pair.Value);
            String^ value;
            if (box != nullptr) value = box->Checked ? "1" : "0";
            else if (pick != nullptr) value = pick->SelectedItem == nullptr ? "" : pick->SelectedItem->ToString();
            else value = pair.Value->Text;
            Set(pair.Key, value);
        }
    }

    void ShowPreview() {
        array<Byte>^ archBytes = Z(arch_);
        pin_ptr<Byte> a = &archBytes[0];
        int tab = tabs_->SelectedIndex < 0 ? 0 : tabs_->SelectedIndex;
        preview_->Text = FromUtf8(ride_options_preview(project_, config_, tab, reinterpret_cast<const char*>(a)));
    }

    void Changed(Object^, EventArgs^) {
        if (pushing_) return;
        Pull();
        ShowPreview();
    }

    void TabChosen(Object^, EventArgs^) { ShowPreview(); }

    void ConfigChosen(Object^, EventArgs^) {
        if (pushing_) return;
        if (ccsText_ == nullptr) Pull();
        config_ = configPick_->SelectedIndex == 1 ? RIDE_CONFIG_RELEASE : RIDE_CONFIG_DEBUG;
        Push();
        if (ccsText_ != nullptr) ccsText_->Text = FromUtf8(ride_project_ccs_mapping(project_, config_))->Replace("\n", "\r\n");
    }

    void RestoreDefaults(Object^, EventArgs^) {
        ride_options_reset(project_, config_);
        Push();
    }

    void Accepted(Object^, EventArgs^) { if (ccsText_ == nullptr) Pull(); }
};

// Find in Files on a thread of its own: the walk reads files and nothing of the window or a project,
// and ride_find_stop ends it early.
ref class FindJob {
public:
    Utf8^ text;
    Utf8^ folder;
    Utf8^ types;
    int matchCase, wholeWord, subfolders, namesOnly;
    String^ found;
    void Run() {
        found = TakeUtf8(ride_find_in_files(text->c(), folder->c(), types->c(), matchCase, wholeWord, subfolders, namesOnly));
    }
};

public ref class MainForm : public Form {
public:
    MainForm() { Start(nullptr, nullptr); }
    MainForm(String^ projectDirectory, array<String^>^ files) {
        Start(projectDirectory, files);
    }

protected:

    // Back from another program: the file in front is checked against the disk.
    virtual void OnActivated(EventArgs^ e) override {
        Form::OnActivated(e);
        if (started_) ReloadIfChanged(Current());
    }

    virtual void OnResize(EventArgs^ e) override {
        Form::OnResize(e);
        if (stopBar_ != nullptr) PlaceStopBar();
    }

    virtual bool ProcessCmdKey(System::Windows::Forms::Message% message, Keys keys) override {
        if (keys == static_cast<Keys>(Keys::Control | Keys::Z) && input_ != nullptr && input_->Focused) {
            EndInput();
            return true;
        }
        if (keys == static_cast<Keys>(Keys::Control | Keys::PageDown)) { StepFile(1); return true; }
        if (keys == static_cast<Keys>(Keys::Control | Keys::PageUp)) { StepFile(-1); return true; }
        bool moving = keys == static_cast<Keys>(Keys::Control | Keys::D) ||
                      keys == static_cast<Keys>(Keys::Control | Keys::K) ||
                      keys == static_cast<Keys>(Keys::Control | Keys::T);
        // While the worker has the core, a key whose item is greyed says so rather than reaching the box.
        if (busy_ && (moving || GatedKey(keys))) { what_->Text = StillWorking(); return true; }
        if (keys == static_cast<Keys>(Keys::Control | Keys::D)) { NextConfig(); return true; }
        if (keys == static_cast<Keys>(Keys::Control | Keys::K)) { NextTool(); return true; }
        if (keys == static_cast<Keys>(Keys::Control | Keys::T)) { NextTarget(); return true; }
        // Ctrl+Up and Ctrl+Down walk the stack only while something is stopped; otherwise they are the box's.
        bool up = keys == static_cast<Keys>(Keys::Control | Keys::Up);
        if ((up || keys == static_cast<Keys>(Keys::Control | Keys::Down)) && !busy_ &&
            ride_debugger_running(debugger_) != 0 && ride_stack_count(debugger_) > 0) {
            LookAlongStack(up ? 1 : -1);
            return true;
        }
        return Form::ProcessCmdKey(message, keys);
    }

public:
    // Called on the run's worker thread: a copy of what it said, or nullptr when it is over, sent
    // to this window's thread. Nothing here waits for the window, and a window gone drops it.
    // Collected under a lock and drained by one message at a time, so a program printing without
    // end costs the window one append per drain, not one per chunk; past QueueCap the oldest goes.
    void Post(array<Byte>^ bytes, int stream) {
        try {
            if (IsDisposed || !IsHandleCreated) return;
            bool post;
            System::Threading::Monitor::Enter(outLock_);
            try {
                outQueue_->Add(gcnew OutChunk(bytes, stream));
                if (bytes != nullptr) outQueued_ += bytes->Length;
                while (outQueued_ > QueueCap && outQueue_->Count > 1 && outQueue_[0]->bytes != nullptr) {
                    outQueued_ -= outQueue_[0]->bytes->Length;
                    outQueue_->RemoveAt(0);
                    outDropped_ = true;
                }
                post = !outPosted_;
                outPosted_ = true;
            } finally {
                System::Threading::Monitor::Exit(outLock_);
            }
            if (post) BeginInvoke(gcnew Action(this, &MainForm::DrainOutput));
        } catch (Exception^) { }
    }

protected:
    virtual void OnFormClosing(System::Windows::Forms::FormClosingEventArgs^ e) override {
        // Nothing the worker holds is freed under it: the close waits for the work to stop.
        if (busy_) {
            e->Cancel = true;
            if (closeWhenIdle_) return;
            System::Windows::Forms::DialogResult answer = MessageBox::Show(
                this, "A build, a run or the debugger is still working.\r\n\r\nStop it and close the window?",
                ProductName(), MessageBoxButtons::YesNo, MessageBoxIcon::Warning, MessageBoxDefaultButton::Button2);
            if (answer != System::Windows::Forms::DialogResult::Yes) return;
            closeWhenIdle_ = true;
            StopWork();
            return;
        }
        for (int i = 0; i < sheets_->Count; ++i)
            if (!MayDiscard(sheets_[i])) {
                e->Cancel = true;
                return;
            }
        if (ride_debugger_running(debugger_) != 0) EndDebugging();
        RememberOpen();
        Form::OnFormClosing(e);
    }

    virtual void OnShown(EventArgs^ e) override {
        Form::OnShown(e);
        Arrange();
        // A start with nothing to open has no sheet to select - the
        // genuinely empty environment CloseSheet leaves, from the first.
        if (text_ != nullptr) {
            text_->Select(0, 0);
            text_->Focus();
        }
        Recolour();

        for (int i = 0; i < sheets_->Count; ++i) {
            sheets_[i]->box->Modified = false;
            MarkTab(sheets_[i]);
        }
    }

    static String^ ProductName() { return gcnew String(ride_product_name()); }

    // The window title: the product and its version, then the project it is in, then the file in
    // front - "RIDE 5.17 - demo - main.c"; "RIDE 5.17 - main.c" with no project, "RIDE 5.17" with
    // neither. One place, so opening, loading or closing a project and saving-as all say it the same way.
    void RefreshTitle() {
        String^ title = ProductName() + " " + FromUtf8(ride_version());
        // The worker may be inside the project; its name is the one read before it started.
        if (!busy_) projectName_ = project_ == nullptr ? nullptr : FromUtf8(ride_project_name(project_));
        String^ project = projectName_;
        if (project != nullptr && project->Length > 0) title += " - " + project;
        if (path_ != nullptr && path_->Length > 0)
            title += " - " + System::IO::Path::GetFileName(path_);
        Text = title;
        RefreshProjectMenu();
    }

    ~MainForm() {
        delete settle_;
        delete recolourTimer_;
        this->!MainForm();
    }
    !MainForm() {
        if (running_ != nullptr) {
            ride_running_free(running_);
            running_ = nullptr;
        }
        if (self_.IsAllocated) self_.Free();
        if (project_ != nullptr) {
            ride_project_free(project_);
            project_ = nullptr;
        }

        if (built_ != nullptr) {
            ride_program_free(built_);
            built_ = nullptr;
        }
        if (targetBuilt_ != nullptr) {
            ride_build_free(targetBuilt_);
            targetBuilt_ = nullptr;
        }
        if (debugger_ != nullptr) {
            ride_debugger_free(debugger_);
            debugger_ = nullptr;
        }
    }

private:
    RIDEProject* project_;

    String^ arch_;
    String^ cc1_;
    String^ cl_;
    String^ shc_;
    String^ cxx1_;
    int toolKind_;
    int config_;
    int indentWidth_;
    int indentTabs_;
    int indentCase_;

    RIDEDebugger* debugger_;
    RIDEProgram* built_;

    ToolStripMenuItem^ upTheStack_;
    ToolStripMenuItem^ downTheStack_;
    ToolStripMenuItem^ watchItem_;
    // The worker: while busy_ the core is its, the items that reach the core are greyed, and
    // closing waits. README.md, "One thing at a time", has the whole of it.
    bool busy_;
    Job^ job_;
    bool closeWhenIdle_;
    bool treeStale_;
    String^ projectName_;
    ToolStripMenuItem^ stopItem_;
    // A program running with a real input: its handle, what it was built from, and each stream's
    // decoder, so a character split between two pieces of output arrives whole.
    RIDERunning* running_;
    Toolchain^ runTools_;
    String^ runSource_;
    String^ runProgram_;
    array<System::Text::Decoder^>^ decoders_;
    Runtime::InteropServices::GCHandle self_;
    TextBox^ input_;
    System::Collections::Generic::List<ToolStripMenuItem^>^ gated_;
    System::Collections::Generic::List<ToolStripItem^>^ live_;

    RIDEBuild* targetBuilt_;

    System::Collections::Generic::Dictionary<String^,
        System::Collections::Generic::List<int>^>^ breaks_;
    System::Collections::Generic::Dictionary<String^, String^>^ breakNames_;

    int errorLine_;
    int errorColumn_;
    String^ errorMessage_;
    String^ errorFile_;
    String^ stopFile_;
    String^ stopFunction_;
    String^ debugSource_;     // the one file being debugged, or nullptr for a project
    String^ lookingFile_;
    int lookingLine_;
    int stopLine_;

    int highlightRow_;
    Sheet^ highlightSheet_;   // the sheet highlightRow_ was painted in
    int digitWidth_;          // a digit of codeFont_ in the gutter, measured once per font

    Panel^ stopBar_;

    System::Drawing::Font^ codeFont_;

    String^ path_;
    String^ projectDirectory_;
    String^ needle_;
    bool colouring_;

    bool stateGood_;
    int stateRow_;
    int stateAt_;

    Timer^ settle_;

    SplitContainer^ outer_;
    // Build > Command-line arguments: the line Run and Run project hand the program - the project's own,
    // kept in its .pro, or for a file run on its own, this session's.
    ToolStripTextBox^ argsBox_;
    String^ fileArgs_;
    // Edit > Find in Files: what was asked last time, asked again - the text excepted, which comes from the selection.
    String^ findTypes_;
    bool findCase_, findWord_, findSub_;
    // The bottom panel's minimise button: checked, the panel is only its tab strip and the editor takes the rest.
    CheckBox^ fold_;
    bool folded_;
    int keptPanel_;
    SplitContainer^ upper_;

    TreeView^ tree_;
    ClosableTabControl^ files_;
    System::Collections::Generic::List<Sheet^>^ sheets_;

    RichTextBox^ text_;
    TabControl^ panel_;

    bool numbers_;

    System::Collections::Generic::List<ToolStripMenuItem^>^ targetItems_;
    ToolStripMenuItem^ toolAutoItem_;
    ToolStripMenuItem^ toolCc1Item_;
    ToolStripMenuItem^ toolClItem_;
    ToolStripMenuItem^ toolShcItem_;
    ToolStripMenuItem^ toolCxx1Item_;
    ToolStripMenuItem^ langAutoItem_;
    ToolStripMenuItem^ langCItem_;
    ToolStripMenuItem^ langCppItem_;
    ToolStripMenuItem^ langShalimarItem_;
    ToolStripMenuItem^ langJsonItem_;
    ToolStripMenuItem^ langTextItem_;
    ToolStripMenuItem^ convertItem_;
    ToolStripMenuItem^ debugConfigItem_;
    ToolStripMenuItem^ releaseConfigItem_;
    ToolStripMenuItem^ numbersItem_;
    ToolStripMenuItem^ paneItem_;
    ToolStripMenuItem^ panelItem_;
    TextBox^ console_;
    ref struct OutChunk {
        array<Byte>^ bytes;
        int stream;
        OutChunk(array<Byte>^ b, int s) : bytes(b), stream(s) { }
    };
    literal int QueueCap = 4 * 1024 * 1024;
    literal int ConsoleCap = 2 * 1024 * 1024;
    Object^ outLock_;
    System::Collections::Generic::List<OutChunk^>^ outQueue_;
    int outQueued_;
    bool outPosted_;
    bool outDropped_;
    bool heardProgram_;
    bool finding_;
    TextBox^ debug_;
    DebugView^ debugView_;
    ErrorList^ errors_;
    TabPage^ errorsPage_;
    ToolStripStatusLabel^ step_;
    ToolStripProgressBar^ progress_;
    RichTextBox^ assembly_;
    StatusStrip^ status_;
    ToolStripStatusLabel^ build_;
    ToolStripStatusLabel^ where_;
    ToolStripStatusLabel^ what_;
    ToolStripStatusLabel^ root_;
    // The compiler in use, shown at the right end of the menu bar, plain text,
    // changing with the file, the Language menu and the Tools menu.
    ToolStripLabel^ compilerHint_;
    System::Windows::Forms::Timer^ recolourTimer_;

    static array<Byte>^ Utf8Of(String^ text) {
        array<Byte>^ raw = System::Text::Encoding::UTF8->GetBytes(text == nullptr ? "" : text);
        array<Byte>^ out = gcnew array<Byte>(raw->Length + 1);
        Array::Copy(raw, out, raw->Length);
        return out;
    }

    static String^ StillWorking() { return "still working - Build > Stop (Ctrl+Break) ends it"; }

    // %USERPROFILE%\RIDE 5.17\settings.json, begun from the installation's defaults: the user's to change.
    void OnSettingsFile(Object^, EventArgs^) {
        ride_write_install_file_if_absent();
        String^ file = FromUtf8(ride_install_file());
        if (file == nullptr || file->Length == 0 || !System::IO::File::Exists(file)) {
            what_->Text = "there is no settings.json to open - no home directory";
            return;
        }
        OpenPath(file);
        what_->Text = file + " - what is saved here is used from the next build";
    }

    void Start(String^ projectDirectory, array<String^>^ files) {
        // The user's settings.json, made from the installation's the first time - as the console does.
        ride_write_install_file_if_absent();
        outLock_ = gcnew Object();
        outQueue_ = gcnew System::Collections::Generic::List<OutChunk^>();
        outQueued_ = 0;
        outPosted_ = false;
        outDropped_ = false;
        heardProgram_ = false;
        finding_ = false;
        project_ = ride_project_new();
        arch_ = "x86_64-windows";
        ride_ask_native(AskNativeInWindow);

        // The i-line names, since 3.5: the compilers docked beside the editor are c90, cpp11 and
        // shalimar. Named looks for "<name>.exe" beside the editor, so the plain names used to find a
        // stale cc1.exe left from a 3.0 build, which read a .cpp as C. cl stays the host compiler, found on PATH.
        cc1_ = Named("C90", "c90");
        cl_ = Named("CL", "cl");
        shc_ = Named("SHALIMAR", "shalimar");
        cxx1_ = Named("CPP11", "cpp11");
        toolKind_ = ride_default_compiler();
        config_ = RIDE_CONFIG_DEBUG;
        debugger_ = ride_debugger_new();
        built_ = nullptr;
        targetBuilt_ = nullptr;
        busy_ = false;
        job_ = nullptr;
        running_ = nullptr;
        self_ = Runtime::InteropServices::GCHandle::Alloc(this);
        closeWhenIdle_ = false;
        treeStale_ = false;
        projectName_ = nullptr;
        highlightSheet_ = nullptr;
        breaks_ = gcnew System::Collections::Generic::Dictionary<String^,
            System::Collections::Generic::List<int>^>();
        breakNames_ = gcnew System::Collections::Generic::Dictionary<String^, String^>();
        stopFile_ = nullptr;
        stopLine_ = 0;
        lookingFile_ = nullptr;
        lookingLine_ = 0;
        highlightRow_ = -1;
        stopBar_ = nullptr;
        codeFont_ = RememberedFont();
        digitWidth_ = DigitWidth(codeFont_);
        numbers_ = true;
        ForgetError();
        indentWidth_ = ride_default_indent_width();
        indentTabs_ = ride_default_indent_tabs();
        indentCase_ = 0;

        Lay();

        // The last project is remembered and offered - Project > Recent -
        // never opened on its own: a start with nothing named is empty.
        if (projectDirectory != nullptr) LoadProject(projectDirectory);
        RefreshRecent();

        bool anyNamed = false;
        if (files != nullptr)
            for (int i = 0; i < files->Length; ++i)
                if (files[i] != nullptr) {
                    OpenPath(files[i]);
                    anyNamed = true;
                }

        if (!anyNamed) OpenFirstOfProject();
        started_ = true;

        // Nothing opened: a genuinely empty environment, not an untitled
        // sheet with a bare group name beside it. The sheet Lay() made is
        // the spare OpenPath would have taken, so it goes when unused.
        if (sheets_->Count == 1 && sheets_[0]->path == nullptr &&
            sheets_[0]->box->TextLength == 0 && !sheets_[0]->box->Modified) {
            DropSheet(sheets_[0]);
            text_ = nullptr;
            path_ = nullptr;
            RefreshTitle();
            FillTree();
            SayBuild();
            what_->Text = "ready";
        }
    }

    void Lay() {
        RefreshTitle();
        // The icon the exe carries (winforms/RIDEGui.rc, resource 1), for the title bar and the
        // taskbar; a build without it keeps the default. Through LoadIcon rather than .NET's
        // ExtractAssociatedIcon, whose name <windows.h> rewrites into a Win32 call.
        HICON loaded = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1));
        if (loaded != NULL) Icon = System::Drawing::Icon::FromHandle(IntPtr(loaded));
        Width = 1100;
        Height = 760;
        MinimumSize = System::Drawing::Size(840, 560);
        StartPosition = FormStartPosition::CenterScreen;
        DoubleBuffered = true;
        colouring_ = false;
        stateGood_ = false;
        stateRow_ = 0;
        stateAt_ = 0;

        settle_ = gcnew Timer();
        settle_->Interval = 250;
        settle_->Tick += gcnew EventHandler(this, &MainForm::OnSettled);

        MenuStrip^ bar = gcnew MenuStrip();
        live_ = gcnew System::Collections::Generic::List<ToolStripItem^>();

        ToolStripMenuItem^ file = gcnew ToolStripMenuItem("&File");
        live_->Add(file->DropDownItems->Add("New", nullptr,
                                            gcnew EventHandler(this, &MainForm::OnNewBuffer)));
        // A new file in a project and a new project are the Project menu's, and only there.
        openFileItem_ = gcnew ToolStripMenuItem("Open...", nullptr, gcnew EventHandler(this, &MainForm::OnOpenFile));
        file->DropDownItems->Add(openFileItem_);
        // The last three files opened on their own, most recent first, in a submenu as macOS has them
        // and as the Project menu has its projects. Next and previous file keep Ctrl+PageDown and Ctrl+PageUp.
        recentFilesMenu_ = gcnew ToolStripMenuItem("Recent");
        recentFileItems_ = gcnew System::Collections::Generic::List<ToolStripMenuItem^>();
        for (int i = 0; i < 3; ++i) {
            ToolStripMenuItem^ one = gcnew ToolStripMenuItem(
                "Recent", nullptr, gcnew EventHandler(this, &MainForm::OnOpenRecentFile));
            one->Tag = i;
            one->Visible = false;
            recentFileItems_->Add(one);
            recentFilesMenu_->DropDownItems->Add(one);
        }
        recentFilesMenu_->Enabled = false;
        file->DropDownItems->Add(recentFilesMenu_);
        file->DropDownItems->Add(gcnew ToolStripSeparator());
        ToolStripMenuItem^ close = Item("Close", Keys::Control | Keys::W, gcnew EventHandler(this, &MainForm::OnCloseFile));
        file->DropDownItems->Add(close);
        live_->Add(close);
        ToolStripMenuItem^ save = gcnew ToolStripMenuItem(
            "Save", nullptr, gcnew EventHandler(this, &MainForm::OnSave));
        save->ShortcutKeys = static_cast<Keys>(Keys::Control | Keys::S);
        file->DropDownItems->Add(save);
        live_->Add(save);
        saveAsItem_ = gcnew ToolStripMenuItem("Save as...", nullptr, gcnew EventHandler(this, &MainForm::OnSaveAs));
        file->DropDownItems->Add(saveAsItem_);
        file->DropDownItems->Add(gcnew ToolStripSeparator());
        ToolStripMenuItem^ exit = Item("Exit", Keys::Control | Keys::Q, gcnew EventHandler(this, &MainForm::OnExit));
        file->DropDownItems->Add(exit);
        live_->Add(exit);

        ToolStripMenuItem^ edit = gcnew ToolStripMenuItem("&Edit");
        edit->DropDownItems->Add(Item("Undo", Keys::Control | Keys::Z, gcnew EventHandler(this, &MainForm::OnUndo)));
        edit->DropDownItems->Add(Item("Redo", Keys::Control | Keys::Y, gcnew EventHandler(this, &MainForm::OnRedo)));
        edit->DropDownItems->Add(gcnew ToolStripSeparator());
        edit->DropDownItems->Add(Item("Cut", Keys::Control | Keys::X, gcnew EventHandler(this, &MainForm::OnCut)));
        edit->DropDownItems->Add(Item("Copy", Keys::Control | Keys::C, gcnew EventHandler(this, &MainForm::OnCopy)));
        edit->DropDownItems->Add(Item("Paste", Keys::Control | Keys::V, gcnew EventHandler(this, &MainForm::OnPaste)));
        edit->DropDownItems->Add(
            Item("Select all", Keys::Control | Keys::A, gcnew EventHandler(this, &MainForm::OnSelectAll)));
        edit->DropDownItems->Add(gcnew ToolStripSeparator());
        edit->DropDownItems->Add(Item("Find...", Keys::Control | Keys::F, gcnew EventHandler(this, &MainForm::OnFind)));
        edit->DropDownItems->Add(Item("Find next", Keys::F3, gcnew EventHandler(this, &MainForm::OnFindNext)));
        edit->DropDownItems->Add(
            Item("Find previous", Keys::Shift | Keys::F3, gcnew EventHandler(this, &MainForm::OnFindPrevious)));
        edit->DropDownItems->Add(
            Item("Replace...", Keys::Control | Keys::H, gcnew EventHandler(this, &MainForm::OnReplace)));
        // The last of the Find group: every file under a folder, or the files by name, the hits in the Console.
        edit->DropDownItems->Add(Item("Find in Files...", Keys::Control | Keys::Shift | Keys::F,
                                      gcnew EventHandler(this, &MainForm::OnFindInFiles)));
        edit->DropDownItems->Add(gcnew ToolStripSeparator());
        edit->DropDownItems->Add(
            Item("Re-indent", Keys::Control | Keys::L, gcnew EventHandler(this, &MainForm::OnLayOut)));

        ToolStripMenuItem^ project = gcnew ToolStripMenuItem("&Project");

        project->DropDownItems->Add("New...", nullptr,
                                    gcnew EventHandler(this, &MainForm::OnNewProject));
        // Open takes a RIDE .pro, a CCS project's .project, or a file in a CCS workspace.
        project->DropDownItems->Add("Open...", nullptr,
                                    gcnew EventHandler(this, &MainForm::OnOpenProjectFile));
        // The last three projects, most recent first, to recall one by name.
        recentMenu_ = gcnew ToolStripMenuItem("Recent");
        recentItems_ = gcnew System::Collections::Generic::List<ToolStripMenuItem^>();
        for (int i = 0; i < 3; ++i) {
            ToolStripMenuItem^ one = gcnew ToolStripMenuItem(
                "Recent", nullptr, gcnew EventHandler(this, &MainForm::OnOpenRecent));
            one->Tag = i;
            one->Visible = false;
            recentItems_->Add(one);
            recentMenu_->DropDownItems->Add(one);
        }
        project->DropDownItems->Add(recentMenu_);
        projSaveAs_ = gcnew ToolStripMenuItem("Save As...", nullptr,
                                              gcnew EventHandler(this, &MainForm::OnSaveProjectAs));
        project->DropDownItems->Add(projSaveAs_);
        projClose_ = gcnew ToolStripMenuItem("Close", nullptr,
                                             gcnew EventHandler(this, &MainForm::OnCloseProject));
        project->DropDownItems->Add(projClose_);

        // The open project's files: only the first two say so, the rest are understood.
        project->DropDownItems->Add(gcnew ToolStripSeparator());
        projNewFile_ = Item("New File", Keys::Control | Keys::N, gcnew EventHandler(this, &MainForm::OnNewFile));
        project->DropDownItems->Add(projNewFile_);
        projAddFile_ = gcnew ToolStripMenuItem("Add File", nullptr,
                                               gcnew EventHandler(this, &MainForm::OnAddThisFile));
        project->DropDownItems->Add(projAddFile_);
        projRemove_ = gcnew ToolStripMenuItem("Remove", nullptr,
                                              gcnew EventHandler(this, &MainForm::OnRemoveFromProject));
        project->DropDownItems->Add(projRemove_);
        projRename_ = Item("Rename...", Keys::F2, gcnew EventHandler(this, &MainForm::OnRenameFile));
        project->DropDownItems->Add(projRename_);
        projDelete_ = gcnew ToolStripMenuItem("Delete...", nullptr,
                                              gcnew EventHandler(this, &MainForm::OnDeleteFile));
        project->DropDownItems->Add(projDelete_);

        project->DropDownItems->Add(gcnew ToolStripSeparator());
        projIncludes_ = gcnew ToolStripMenuItem("Include Paths...", nullptr,
                                                gcnew EventHandler(this, &MainForm::OnProjectIncludes));
        project->DropDownItems->Add(projIncludes_);
        projLibraries_ = gcnew ToolStripMenuItem("Libraries...", nullptr,
                                                 gcnew EventHandler(this, &MainForm::OnProjectLibraries));
        project->DropDownItems->Add(projLibraries_);
        project->DropDownOpening += gcnew EventHandler(this, &MainForm::OnProjectMenuOpening);

        ToolStripMenuItem^ build = gcnew ToolStripMenuItem("&Build");
        ToolStripMenuItem^ compile = gcnew ToolStripMenuItem(
            "Compile", nullptr, gcnew EventHandler(this, &MainForm::OnCompile));
        compileItem_ = compile;

        compile->ShortcutKeys = static_cast<Keys>(Keys::Control | Keys::B);
        build->DropDownItems->Add(compile);
        ToolStripMenuItem^ runIt = gcnew ToolStripMenuItem(
            "Run", nullptr, gcnew EventHandler(this, &MainForm::OnRun));
        runIt->ShortcutKeys = Keys::F5;
        build->DropDownItems->Add(runIt);
        runItem_ = runIt;

        build->DropDownItems->Add(gcnew ToolStripSeparator());
        buildProjectItem_ = Item("Build project", Keys::F4, gcnew EventHandler(this, &MainForm::OnBuildProject));
        build->DropDownItems->Add(buildProjectItem_);
        runProjectItem_ = gcnew ToolStripMenuItem("Run project", nullptr,
                                                  gcnew EventHandler(this, &MainForm::OnRunProject));
        build->DropDownItems->Add(runProjectItem_);
        // Run on tms6747 is the .out on sim6747 (R5): the assembly on vm6747 is asked for, and the two compared.
        emulateItem_ = gcnew ToolStripMenuItem("Emulate on vm6747", nullptr,
                                               gcnew EventHandler(this, &MainForm::OnEmulate));
        build->DropDownItems->Add(emulateItem_);
        verifyItem_ = gcnew ToolStripMenuItem("Verify", nullptr, gcnew EventHandler(this, &MainForm::OnVerify));
        build->DropDownItems->Add(verifyItem_);
        build->DropDownOpening += gcnew EventHandler(this, &MainForm::OnModeMenuOpening);
        // The command line Run and Run project hand the program: a submenu, as Recent is, holding a box to type it in.
        ToolStripMenuItem^ argsMenu = gcnew ToolStripMenuItem("Command-line arguments");
        argsBox_ = gcnew ToolStripTextBox();
        argsBox_->AutoSize = false;
        argsBox_->Width = 320;
        argsBox_->Font = gcnew System::Drawing::Font("Consolas", 10.0f);
        argsBox_->ToolTipText = "Handed to the program by Run and Run project - e.g. -run sample.cpp; "
                                "a word in quotes may hold spaces. Enter keeps it.";
        argsBox_->KeyDown += gcnew KeyEventHandler(this, &MainForm::OnArgsKey);
        argsMenu->DropDownItems->Add(argsBox_);
        argsMenu->DropDownOpening += gcnew EventHandler(this, &MainForm::OnBuildMenuOpening);
        argsMenu->DropDownClosed += gcnew EventHandler(this, &MainForm::OnBuildMenuClosed);
        build->DropDownItems->Add(argsMenu);
        fileArgs_ = "";
        findTypes_ = "*.c;*.cpp;*.h;*.hpp;*.shl;*.s";
        findCase_ = false; findWord_ = false; findSub_ = true;
        // Debug or Release, one of the two, in a submenu as Recent is; the check says which is in force.
        ToolStripMenuItem^ configuration = gcnew ToolStripMenuItem("Configuration");
        debugConfigItem_ = gcnew ToolStripMenuItem(
            "Debug", nullptr, gcnew EventHandler(this, &MainForm::OnDebugConfig));

        debugConfigItem_->ShortcutKeyDisplayString = "Ctrl+D";
        configuration->DropDownItems->Add(debugConfigItem_);
        releaseConfigItem_ = gcnew ToolStripMenuItem(
            "Release", nullptr, gcnew EventHandler(this, &MainForm::OnReleaseConfig));
        releaseConfigItem_->ShortcutKeyDisplayString = "Ctrl+D";
        configuration->DropDownItems->Add(releaseConfigItem_);
        build->DropDownItems->Add(configuration);
        build->DropDownItems->Add(gcnew ToolStripSeparator());
        // Ctrl+Break, Visual Studio's key for it: ends the build, the run or the debugged program.
        stopItem_ = Item("Stop", Keys::Control | Keys::Cancel, gcnew EventHandler(this, &MainForm::OnStop));
        stopItem_->ShortcutKeyDisplayString = "Ctrl+Break";
        stopItem_->Enabled = false;
        build->DropDownItems->Add(stopItem_);
        live_->Add(stopItem_);
        // What a build made, removed, and the panes emptied - said in so many words.
        build->DropDownItems->Add("Clean", nullptr, gcnew EventHandler(this, &MainForm::OnClean));

        ToolStripMenuItem^ debug = gcnew ToolStripMenuItem("&Debug");
        debugItem_ = Item("Start / continue", Keys::F8, gcnew EventHandler(this, &MainForm::OnDebug));
        debug->DropDownItems->Add(debugItem_);
        debugProjectItem_ = gcnew ToolStripMenuItem("Debug project", nullptr,
                                                    gcnew EventHandler(this, &MainForm::OnDebugProject));
        debug->DropDownItems->Add(debugProjectItem_);

        debug->DropDownItems->Add(gcnew ToolStripSeparator());
        debug->DropDownItems->Add(Item("Toggle breakpoint", Keys::F9,
                                       gcnew EventHandler(this, &MainForm::OnToggleBreak)));
        debug->DropDownItems->Add(Item("Step over", Keys::F7,
                                       gcnew EventHandler(this, &MainForm::OnStepOver)));
        debug->DropDownItems->Add(Item("Step into", Keys::F6,
                                       gcnew EventHandler(this, &MainForm::OnStepInto)));
        debug->DropDownItems->Add("Step out", nullptr,
                                  gcnew EventHandler(this, &MainForm::OnStepOut));
        debug->DropDownItems->Add(gcnew ToolStripSeparator());

        upTheStack_ = gcnew ToolStripMenuItem(
            "Up the stack", nullptr, gcnew EventHandler(this, &MainForm::OnFrameUp));
        upTheStack_->ShortcutKeyDisplayString = "Ctrl+Up";
        debug->DropDownItems->Add(upTheStack_);
        downTheStack_ = gcnew ToolStripMenuItem(
            "Down the stack", nullptr, gcnew EventHandler(this, &MainForm::OnFrameDown));
        downTheStack_->ShortcutKeyDisplayString = "Ctrl+Down";
        debug->DropDownItems->Add(downTheStack_);
        watchItem_ = gcnew ToolStripMenuItem(
            "Watch expression...", nullptr, gcnew EventHandler(this, &MainForm::OnWatch));
        debug->DropDownItems->Add(watchItem_);

        debug->DropDownOpening +=
            gcnew EventHandler(this, &MainForm::OnDebugMenuOpening);

        debug->DropDownItems->Add(gcnew ToolStripSeparator());
        live_->Add(debug->DropDownItems->Add("Stop debugging", nullptr,
                                             gcnew EventHandler(this, &MainForm::OnDebugStop)));

        ToolStripMenuItem^ view = gcnew ToolStripMenuItem("&View");

        view->DropDownItems->Add(Item("Project pane", Keys::Control | Keys::D0,
                                      gcnew EventHandler(this, &MainForm::OnFocusTree)));
        view->DropDownItems->Add(Item("The file", Keys::Control | Keys::D4,
                                      gcnew EventHandler(this, &MainForm::OnFocusText)));
        view->DropDownItems->Add(gcnew ToolStripSeparator());
        view->DropDownItems->Add(Item("Output", Keys::Control | Keys::D1,
                                      gcnew EventHandler(this, &MainForm::OnShowConsole)));
        view->DropDownItems->Add(Item("Errors", Keys::Control | Keys::D5,
                                      gcnew EventHandler(this, &MainForm::OnShowErrors)));
        view->DropDownItems->Add(Item("Debug", Keys::Control | Keys::D2,
                                      gcnew EventHandler(this, &MainForm::OnShowDebug)));
        view->DropDownItems->Add(Item("Assembly", Keys::Control | Keys::D3,
                                      gcnew EventHandler(this, &MainForm::OnShowAssembly)));
        view->DropDownItems->Add(gcnew ToolStripSeparator());

        numbersItem_ = Item("Show line numbers", Keys::None,
                            gcnew EventHandler(this, &MainForm::OnToggleNumbers));
        numbersItem_->Checked = true;
        view->DropDownItems->Add(numbersItem_);

        paneItem_ = Item("Show project pane", Keys::Control | Keys::P,
                         gcnew EventHandler(this, &MainForm::OnTogglePane));
        paneItem_->Checked = true;
        view->DropDownItems->Add(paneItem_);

        panelItem_ = Item("Show bottom panel", Keys::Control | Keys::E,
                          gcnew EventHandler(this, &MainForm::OnTogglePanel));
        panelItem_->Checked = true;
        view->DropDownItems->Add(panelItem_);


        ToolStripMenuItem^ target = gcnew ToolStripMenuItem("&Target");
        targetItems_ = gcnew System::Collections::Generic::List<ToolStripMenuItem^>();
        for (int i = 0; i < ride_arch_count(); ++i) {
            ToolStripMenuItem^ one = gcnew ToolStripMenuItem(
                FromUtf8(ride_arch(i)), nullptr, gcnew EventHandler(this, &MainForm::OnTarget));
            one->ShortcutKeyDisplayString = "Ctrl+T";
            targetItems_->Add(one);
            target->DropDownItems->Add(one);
        }
        target->DropDownOpening += gcnew EventHandler(this, &MainForm::OnChoicesOpening);

        ToolStripMenuItem^ language = gcnew ToolStripMenuItem("Lan&guage");
        langAutoItem_ = gcnew ToolStripMenuItem(
            "By extension", nullptr, gcnew EventHandler(this, &MainForm::OnLangAuto));
        language->DropDownItems->Add(langAutoItem_);
        langCItem_ = gcnew ToolStripMenuItem(
            "C", nullptr, gcnew EventHandler(this, &MainForm::OnLangC));
        language->DropDownItems->Add(langCItem_);
        langCppItem_ = gcnew ToolStripMenuItem(
            "C++", nullptr, gcnew EventHandler(this, &MainForm::OnLangCpp));
        language->DropDownItems->Add(langCppItem_);
        langShalimarItem_ = gcnew ToolStripMenuItem(
            "Shalimar", nullptr, gcnew EventHandler(this, &MainForm::OnLangShalimar));
        language->DropDownItems->Add(langShalimarItem_);
        langJsonItem_ = gcnew ToolStripMenuItem(
            "JSON", nullptr, gcnew EventHandler(this, &MainForm::OnLangJson));
        language->DropDownItems->Add(langJsonItem_);
        langTextItem_ = gcnew ToolStripMenuItem(
            "Plain text", nullptr, gcnew EventHandler(this, &MainForm::OnLangText));
        language->DropDownItems->Add(langTextItem_);
        language->DropDownItems->Add(gcnew ToolStripSeparator());
        convertItem_ = gcnew ToolStripMenuItem(
            "Convert (c2s / s2c)", nullptr, gcnew EventHandler(this, &MainForm::OnConvert));
        language->DropDownItems->Add(convertItem_);

        ToolStripMenuItem^ tools = gcnew ToolStripMenuItem("Too&ls");
        tools->DropDownOpening += gcnew EventHandler(this, &MainForm::OnChoicesOpening);
        toolAutoItem_ = gcnew ToolStripMenuItem(
            "By language", nullptr, gcnew EventHandler(this, &MainForm::OnToolAuto));
        toolAutoItem_->ShortcutKeyDisplayString = "Ctrl+K";
        tools->DropDownItems->Add(toolAutoItem_);
        toolCc1Item_ = gcnew ToolStripMenuItem(
            gcnew String(ride_toolchain_name(RIDE_TOOL_CC1)), nullptr, gcnew EventHandler(this, &MainForm::OnToolCc1));
        toolCc1Item_->ShortcutKeyDisplayString = "Ctrl+K";
        tools->DropDownItems->Add(toolCc1Item_);
        toolCxx1Item_ = gcnew ToolStripMenuItem(
            gcnew String(ride_toolchain_name(RIDE_TOOL_CXX1)), nullptr, gcnew EventHandler(this, &MainForm::OnToolCxx1));
        toolCxx1Item_->ShortcutKeyDisplayString = "Ctrl+K";
        tools->DropDownItems->Add(toolCxx1Item_);

        toolShcItem_ = gcnew ToolStripMenuItem(
            gcnew String(ride_toolchain_name(RIDE_TOOL_SHC)), nullptr, gcnew EventHandler(this, &MainForm::OnToolShc));
        toolShcItem_->ShortcutKeyDisplayString = "Ctrl+K";
        tools->DropDownItems->Add(toolShcItem_);
        toolClItem_ = gcnew ToolStripMenuItem(
            "MSVC (cl)", nullptr, gcnew EventHandler(this, &MainForm::OnToolCl));
        toolClItem_->ShortcutKeyDisplayString = "Ctrl+K";
        tools->DropDownItems->Add(toolClItem_);
        tools->DropDownItems->Add(gcnew ToolStripSeparator());

        tools->DropDownItems->Add("Font...", nullptr,
                                  gcnew EventHandler(this, &MainForm::OnFont));
        // The user's own settings.json, opened here to read and change; a save is the next build's.
        tools->DropDownItems->Add("Settings file...", nullptr,
                                  gcnew EventHandler(this, &MainForm::OnSettingsFile));
        tools->DropDownItems->Add(gcnew ToolStripSeparator());
        tools->DropDownItems->Add("Header directories...", nullptr,
                                  gcnew EventHandler(this, &MainForm::OnHeaderDirs));
        tools->DropDownItems->Add("Shared include paths...", nullptr,
                                  gcnew EventHandler(this, &MainForm::OnSharedIncludes));
        tools->DropDownItems->Add("Shared libraries...", nullptr,
                                  gcnew EventHandler(this, &MainForm::OnSharedLibraries));
        tools->DropDownItems->Add("Locate vcvars64.bat...", nullptr,
                                  gcnew EventHandler(this, &MainForm::OnLocateVcvars));
        tools->DropDownItems->Add("Assembler for x86_64-windows...", nullptr,
                                  gcnew EventHandler(this, &MainForm::OnLocateAssembler));
        tools->DropDownItems->Add("Linker for x86_64-windows...", nullptr,
                                  gcnew EventHandler(this, &MainForm::OnLocateLinker));
        tools->DropDownItems->Add("TI compiler for tms6747...", nullptr,
                                  gcnew EventHandler(this, &MainForm::OnLocateTi));
        tools->DropDownItems->Add("Linker for tms6747...", nullptr,
                                  gcnew EventHandler(this, &MainForm::OnLocateTilinker));
        tools->DropDownItems->Add(gcnew ToolStripSeparator());
        tools->DropDownItems->Add("Compiler options...", nullptr,
                                  gcnew EventHandler(this, &MainForm::OnCompilerOptions));

        ToolStripMenuItem^ help = gcnew ToolStripMenuItem("&Help");
        help->DropDownItems->Add("Contents", nullptr,
                                 gcnew EventHandler(this, &MainForm::OnHelpContents));
        help->DropDownItems->Add(Item("Keys", Keys::F1,
                                      gcnew EventHandler(this, &MainForm::OnKeys)));
        // As the macOS window has them: the language reference, and the one page of everything installed.
        help->DropDownItems->Add("Shalimar Language Reference", nullptr,
                                 gcnew EventHandler(this, &MainForm::OnHelpShalimar));
        help->DropDownItems->Add("Resources", nullptr,
                                 gcnew EventHandler(this, &MainForm::OnHelpResources));
        help->DropDownItems->Add("Environment", nullptr,
                                 gcnew EventHandler(this, &MainForm::OnEnvironment));
        help->DropDownItems->Add("About", nullptr,
                                 gcnew EventHandler(this, &MainForm::OnAbout));
        // One order in both windows: File, Edit, View, then the work - Project, Build, Debug, Target - then the
        // settings, then Help; macOS has the same with its Option for Language and Tools, and its Window menu.
        bar->Items->Add(file);
        bar->Items->Add(edit);
        bar->Items->Add(view);
        bar->Items->Add(project);
        bar->Items->Add(build);
        bar->Items->Add(debug);
        bar->Items->Add(target);
        bar->Items->Add(language);
        bar->Items->Add(tools);
        bar->Items->Add(help);

        // The compiler in use, at the right end of the menu bar: plain text,
        // no box, changing as the file, the Language menu or the Tools menu
        // change it. SayBuild fills it in beside the status bar's fuller line.
        compilerHint_ = gcnew ToolStripLabel();
        compilerHint_->Alignment = ToolStripItemAlignment::Right;
        compilerHint_->ForeColor = System::Drawing::Color::FromArgb(90, 90, 90);
        compilerHint_->Font = gcnew System::Drawing::Font(compilerHint_->Font,
                                                          System::Drawing::FontStyle::Bold);
        // Keep it off the right edge - a little breathing room.
        compilerHint_->Margin = System::Windows::Forms::Padding(0, 1, 16, 2);
        bar->Items->Add(compilerHint_);

        // Re-highlighting is deferred a beat after the last scroll so the mouse
        // wheel does not fight the highlighter (which selects text as it colours
        // and made the view tremble when it ran on every wheel tick).
        recolourTimer_ = gcnew System::Windows::Forms::Timer();
        recolourTimer_->Interval = 70;
        recolourTimer_->Tick += gcnew EventHandler(this, &MainForm::OnRecolourTick);

        MainMenuStrip = bar;
        Controls->Add(bar);
        // Editing, looking and asking touch no core the worker holds, so they stay live while it works.
        for each (ToolStripMenuItem^ top in gcnew array<ToolStripMenuItem^>{edit, view, help, language})
            for each (ToolStripItem^ each in top->DropDownItems)
                if (each != convertItem_) live_->Add(each);
        gated_ = gcnew System::Collections::Generic::List<ToolStripMenuItem^>();
        GateBelow(bar->Items);
        ShowChoices();

        outer_ = gcnew SplitContainer();
        outer_->Dock = DockStyle::Fill;
        outer_->Orientation = Orientation::Horizontal;
        // Sunken edges on every pane and raised bars between them - the
        // Windows look, at the user's request: a plain flat divider read as
        // no border at all.
        outer_->SplitterWidth = 7;
        outer_->BorderStyle = System::Windows::Forms::BorderStyle::Fixed3D;
        outer_->BackColor = System::Drawing::SystemColors::Control;
        outer_->Panel1->BorderStyle = System::Windows::Forms::BorderStyle::Fixed3D;
        outer_->Panel2->BorderStyle = System::Windows::Forms::BorderStyle::Fixed3D;
        SplitContainer^ outer = outer_;

        upper_ = gcnew SplitContainer();
        upper_->Dock = DockStyle::Fill;
        upper_->SplitterWidth = 7;
        upper_->BackColor = System::Drawing::SystemColors::Control;
        upper_->Panel1->BorderStyle = System::Windows::Forms::BorderStyle::Fixed3D;
        upper_->Panel2->BorderStyle = System::Windows::Forms::BorderStyle::Fixed3D;
        SplitContainer^ upper = upper_;

        tree_ = gcnew TreeView();
        tree_->Dock = DockStyle::Fill;
        tree_->BorderStyle = System::Windows::Forms::BorderStyle::Fixed3D;
        tree_->BackColor = System::Drawing::Color::FromArgb(250, 250, 250);
        tree_->ItemHeight = 20;
        tree_->FullRowSelect = true;
        tree_->HideSelection = false;
        tree_->ShowLines = false;
        tree_->ShowRootLines = false;
        tree_->Indent = 16;
        tree_->NodeMouseDoubleClick +=
            gcnew TreeNodeMouseClickEventHandler(this, &MainForm::OnTreeOpen);
        // One click opens a file, as on macOS; a group's row still only folds and unfolds.
        tree_->NodeMouseClick += gcnew TreeNodeMouseClickEventHandler(this, &MainForm::OnTreeClick);

        tree_->KeyDown += gcnew KeyEventHandler(this, &MainForm::OnTreeKey);
        // The navigator's right-click menu on macOS, item for item but Add Files.
        treeMenu_ = gcnew System::Windows::Forms::ContextMenuStrip();
        treeOpen_ = gcnew ToolStripMenuItem("Open", nullptr, gcnew EventHandler(this, &MainForm::OnTreeMenuOpen));
        treeNewFile_ = gcnew ToolStripMenuItem("New File", nullptr, gcnew EventHandler(this, &MainForm::OnNewFile));
        treeRename_ = gcnew ToolStripMenuItem("Rename...", nullptr, gcnew EventHandler(this, &MainForm::OnRenameFile));
        treeMove_ = gcnew ToolStripMenuItem("Move to Group...", nullptr, gcnew EventHandler(this, &MainForm::OnMoveToGroup));
        treeRemove_ = gcnew ToolStripMenuItem("Remove", nullptr, gcnew EventHandler(this, &MainForm::OnRemoveFromProject));
        treeDelete_ = gcnew ToolStripMenuItem("Delete...", nullptr, gcnew EventHandler(this, &MainForm::OnDeleteFile));
        treeShow_ = gcnew ToolStripMenuItem("Show in Explorer", nullptr, gcnew EventHandler(this, &MainForm::OnShowInExplorer));
        treeMenu_->Items->Add(treeOpen_);
        treeMenu_->Items->Add(gcnew ToolStripSeparator());
        treeMenu_->Items->Add(treeNewFile_);
        treeMenu_->Items->Add(treeRename_);
        treeMenu_->Items->Add(treeMove_);
        treeMenu_->Items->Add(treeRemove_);
        treeMenu_->Items->Add(treeDelete_);
        treeMenu_->Items->Add(gcnew ToolStripSeparator());
        treeMenu_->Items->Add(treeShow_);
        treeMenu_->Opening += gcnew System::ComponentModel::CancelEventHandler(this, &MainForm::OnTreeMenuOpening);
        tree_->ContextMenuStrip = treeMenu_;
        upper->Panel1->Controls->Add(tree_);

        sheets_ = gcnew System::Collections::Generic::List<Sheet^>();
        files_ = gcnew ClosableTabControl();
        files_->Dock = DockStyle::Fill;
        files_->SelectedIndexChanged +=
            gcnew EventHandler(this, &MainForm::OnSheetChanged);
        files_->TabCloseRequested += gcnew TabCloseHandler(this, &MainForm::OnTabClose);
        upper->Panel2->Controls->Add(files_);
        outer->Panel1->Controls->Add(upper);

        panel_ = gcnew TabControl();
        panel_->Dock = DockStyle::Fill;

        console_ = ReadOnlyBox();
        console_->Name = "console";

        console_->KeyDown += gcnew KeyEventHandler(this, &MainForm::OnConsoleKey);
        console_->DoubleClick += gcnew EventHandler(this, &MainForm::OnConsoleDoubleClick);
        debug_ = ReadOnlyBox();
        debugView_ = gcnew DebugView(debug_);
        debugView_->FrameChosen += gcnew IndexChosen(this, &MainForm::OnFrameChosen);
        debugView_->VariableChosen += gcnew IndexChosen(this, &MainForm::OnVariableChosen);
        debugView_->WatchChosen += gcnew IndexChosen(this, &MainForm::OnWatchChosen);
        debugView_->AddWatchChosen += gcnew EventHandler(this, &MainForm::OnWatch);
        errors_ = gcnew ErrorList();
        errors_->IssueChosen += gcnew PlaceChosen(this, &MainForm::OnIssueChosen);
        assembly_ = gcnew RichTextBox();
        assembly_->Dock = DockStyle::Fill;
        assembly_->BorderStyle = System::Windows::Forms::BorderStyle::Fixed3D;
        assembly_->Font = gcnew System::Drawing::Font("Consolas", 10.0f);
        assembly_->ReadOnly = true;
        assembly_->WordWrap = false;

        TabPage^ one = gcnew TabPage("Output");
        one->Controls->Add(console_);
        // The running program's input, under what it prints: Enter sends the line, Ctrl+Z ends it.
        Panel^ inputRow = gcnew Panel();
        inputRow->Dock = DockStyle::Bottom;
        inputRow->Height = 24;
        Label^ inputLabel = gcnew Label();
        inputLabel->Text = "input";
        inputLabel->Dock = DockStyle::Left;
        inputLabel->Width = 44;
        inputLabel->TextAlign = System::Drawing::ContentAlignment::MiddleLeft;
        inputLabel->ForeColor = System::Drawing::Color::FromArgb(90, 90, 90);
        input_ = gcnew TextBox();
        input_->Name = "input";
        input_->Dock = DockStyle::Fill;
        input_->Font = gcnew System::Drawing::Font("Consolas", 10.0f);
        input_->Enabled = false;
        input_->KeyDown += gcnew KeyEventHandler(this, &MainForm::OnInputKey);
        inputRow->Controls->Add(input_);
        inputRow->Controls->Add(inputLabel);
        one->Controls->Add(inputRow);
        console_->BringToFront();
        // Output, Errors, Debug, Assembly - the order the indices below (0 to 3) name them in.
        errorsPage_ = gcnew TabPage("Errors");
        errorsPage_->Controls->Add(errors_);
        TabPage^ two = gcnew TabPage("Debug");
        two->Controls->Add(debugView_);
        TabPage^ three = gcnew TabPage("Assembly");
        three->Controls->Add(assembly_);
        panel_->TabPages->Add(one);
        panel_->TabPages->Add(errorsPage_);
        panel_->TabPages->Add(two);
        panel_->TabPages->Add(three);
        outer->Panel2->Controls->Add(panel_);
        fold_ = gcnew CheckBox();
        fold_->Appearance = Appearance::Button;
        fold_->FlatStyle = FlatStyle::Flat;
        fold_->FlatAppearance->BorderSize = 1;
        fold_->Size = System::Drawing::Size(24, 20);
        fold_->Text = L"\u25BE";
        fold_->TextAlign = System::Drawing::ContentAlignment::MiddleCenter;
        fold_->Font = gcnew System::Drawing::Font("Segoe UI", 8.0f);
        fold_->Anchor = AnchorStyles::Top | AnchorStyles::Right;
        fold_->TabStop = false;
        (gcnew ToolTip())->SetToolTip(fold_, "Minimise the panel to give the editor the room; click again to restore it");
        fold_->CheckedChanged += gcnew EventHandler(this, &MainForm::OnFold);
        outer->Panel2->Controls->Add(fold_);
        fold_->BringToFront();
        folded_ = false;
        keptPanel_ = 0;
        outer->Panel2->Resize += gcnew EventHandler(this, &MainForm::PlaceFold);

        Controls->Add(outer);
        outer->BringToFront();

        status_ = gcnew StatusStrip();
        what_ = gcnew ToolStripStatusLabel("no file");
        what_->Spring = true;
        what_->TextAlign = System::Drawing::ContentAlignment::MiddleLeft;

        build_ = gcnew ToolStripStatusLabel("");
        build_->BorderSides = ToolStripStatusLabelBorderSides::Left;
        build_->ToolTipText =
            "language, debug or release, the compiler that will run "
            "(* when the file chose it) and the target when it matters";
        root_ = gcnew ToolStripStatusLabel("no project");
        root_->BorderSides = ToolStripStatusLabelBorderSides::Left;
        root_->ForeColor = System::Drawing::Color::FromArgb(90, 90, 90);
        where_ = gcnew ToolStripStatusLabel("1:1");
        where_->BorderSides = ToolStripStatusLabelBorderSides::Left;
        // A build's current step and a bar that moves while the worker has the core.
        step_ = gcnew ToolStripStatusLabel("");
        step_->Name = "step";
        step_->Visible = false;
        progress_ = gcnew ToolStripProgressBar();
        progress_->Name = "progress";
        progress_->Style = ProgressBarStyle::Marquee;
        progress_->MarqueeAnimationSpeed = 30;
        progress_->Visible = false;
        status_->Items->Add(what_);
        status_->Items->Add(step_);
        status_->Items->Add(progress_);
        status_->Items->Add(build_);
        status_->Items->Add(root_);
        status_->Items->Add(where_);
        Controls->Add(status_);
        SayBuild();

        String^ kept = FromUtf8(ride_settings_set_aside());
        if (kept != nullptr && kept->Length > 0)
            what_->Text = "bad configuration file - kept as " +
                          System::IO::Path::GetFileName(kept) + ", a new one made";

        upper->FixedPanel = FixedPanel::Panel1;
        outer->FixedPanel = FixedPanel::Panel2;

        Sheet^ first = MakeSheet(nullptr, "");
        text_ = first->box;

        console_->Text = "c90 or cl output appears here.  Ctrl-B builds, F5 runs.";
        SayDebugTab(nullptr);
        SayWhere();
    }

    void Arrange() {
        const int forTree = 120;
        const int forCode = 240;
        const int forUpper = 160;
        const int forPanel = 80;

        if (upper_ != nullptr && upper_->Width > forTree + forCode) {
            upper_->Panel1MinSize = forTree;
            upper_->Panel2MinSize = forCode;
            upper_->SplitterDistance =
                Math::Max(forTree, Math::Min(240, upper_->Width - forCode));
        }

        if (outer_ != nullptr && folded_) {
            outer_->SplitterDistance = Math::Max(forUpper, outer_->Height - FoldedHeight() - outer_->SplitterWidth);
        } else if (outer_ != nullptr &&
            outer_->Height > forUpper + forPanel + outer_->SplitterWidth) {
            outer_->Panel1MinSize = forUpper;
            outer_->Panel2MinSize = forPanel;

            int deep = Math::Max(120, Math::Min(220, outer_->Height / 4));
            int distance = outer_->Height - deep - outer_->SplitterWidth;
            int most = outer_->Height - forPanel - outer_->SplitterWidth;
            outer_->SplitterDistance = Math::Max(forUpper, Math::Min(distance, most));
        }
    }

    // The project's own choice of file first, then the one that defines
    // main, then the first file there is.
    void OpenFirstOfProject() {
        String^ relative = FromUtf8(ride_project_file_to_open(project_));
        if (relative->Length == 0) return;
        array<Byte>^ bytes = Utf8Of(relative);
        pin_ptr<Byte> pinned = &bytes[0];
        String^ full = FromUtf8(
            ride_project_absolute(project_, reinterpret_cast<const char*>(pinned)));
        if (full->Length > 0 && System::IO::File::Exists(full)) OpenPath(full);
    }

    void RememberOpen() {
        if (path_ == nullptr || path_->Length == 0) return;
        array<Byte>^ bytes = Utf8Of(path_);
        pin_ptr<Byte> pinned = &bytes[0];
        ride_remember_open(project_, reinterpret_cast<const char*>(pinned));
    }

    String^ RootNow() {
        String^ root = FromUtf8(ride_project_root(project_));
        if (root == nullptr || root->Length == 0) root = projectDirectory_;
        return root;
    }

    // {app} is the folder above bin\, where RIDE.exe lives. Projects and single programs are the
    // user's, so they default under Documents\RIDE\projects and \programs - as the macOS window
    // has them - and not beside the install, which under Program Files is not the user's to write. Made on demand.
    String^ AppDir() {
        try {
            System::IO::DirectoryInfo^ above =
                System::IO::Directory::GetParent(Application::StartupPath);
            if (above != nullptr) return above->FullName;
        } catch (Exception^) { }
        return Application::StartupPath;
    }
    String^ MadeUnderApp(String^ leaf) {
        String^ d = System::IO::Path::Combine(AppDir(), leaf);
        try { System::IO::Directory::CreateDirectory(d); } catch (Exception^) { }
        return d;
    }
    // Missing or empty, Documents\RIDE\<leaf> is filled from {app}\<leaf>: the
    // sample projects and programs the installer carries reach the user there.
    static void CopyTree(String^ from, String^ to) {
        System::IO::Directory::CreateDirectory(to);
        for each (String^ f in System::IO::Directory::GetFiles(from))
            System::IO::File::Copy(f, System::IO::Path::Combine(to, System::IO::Path::GetFileName(f)), false);
        for each (String^ d in System::IO::Directory::GetDirectories(from))
            CopyTree(d, System::IO::Path::Combine(to, System::IO::Path::GetFileName(d)));
    }
    // What of from is not in to yet - a whole folder or a file - is copied; nothing there is touched.
    static void CopyMissing(String^ from, String^ to) {
        if (!System::IO::Directory::Exists(from)) return;
        System::IO::Directory::CreateDirectory(to);
        for each (String^ d in System::IO::Directory::GetDirectories(from)) {
            String^ there = System::IO::Path::Combine(to, System::IO::Path::GetFileName(d));
            if (!System::IO::Directory::Exists(there)) CopyTree(d, there);
        }
        for each (String^ f in System::IO::Directory::GetFiles(from)) {
            String^ there = System::IO::Path::Combine(to, System::IO::Path::GetFileName(f));
            if (!System::IO::File::Exists(there)) System::IO::File::Copy(f, there, false);
        }
    }
    String^ MadeUnderDocuments(String^ leaf) {
        String^ docs = Environment::GetFolderPath(Environment::SpecialFolder::MyDocuments);
        if (docs == nullptr || docs->Length == 0) return MadeUnderApp(leaf);
        String^ d = System::IO::Path::Combine(System::IO::Path::Combine(docs, ProductName()), leaf);
        try { System::IO::Directory::CreateDirectory(d); } catch (Exception^) { return MadeUnderApp(leaf); }
        try {
            String^ seed = System::IO::Path::Combine(AppDir(), leaf);
            if (System::IO::Directory::GetFileSystemEntries(d)->Length == 0 &&
                System::IO::Directory::Exists(seed))
                CopyTree(seed, d);
            // The CCS samples reach a projects folder that already has others, each one it lacks.
            if (leaf == "projects")
                CopyMissing(System::IO::Path::Combine(seed, "ccs"), System::IO::Path::Combine(d, "ccs"));
        } catch (Exception^) { }
        return d;
    }
    String^ ProjectsDir() { return MadeUnderDocuments("projects"); }
    String^ ProgramsDir() { return MadeUnderDocuments("programs"); }

    void SayWhere() {
        String^ root = RootNow();
        root_->Text = root == nullptr || root->Length == 0 ? "no project" : root;
    }

    void ShowConsoleEnd() {
        console_->SelectionStart = console_->TextLength;
        console_->SelectionLength = 0;
        console_->ScrollToCaret();
    }

    static String^ Lines(String^ text) {
        if (String::IsNullOrEmpty(text)) return text;
        return text->Replace("\r\n", "\n")->Replace("\r", "\n")->Replace("\n", "\r\n");
    }

    // A tool by its full path: the variable, then beside the editor, then the first on PATH. A bare
    // name would be searched for in the current directory before PATH, where a project could have put one.
    static String^ Named(String^ variable, String^ orElse) {
        String^ said = Environment::GetEnvironmentVariable(variable);
        if (said != nullptr && said->Length != 0) return said;
        String^ here = Application::StartupPath;
        if (here != nullptr && here->Length != 0) {
            String^ beside = System::IO::Path::Combine(here, orElse + ".exe");
            if (System::IO::File::Exists(beside)) return beside;
        }
        String^ path = Environment::GetEnvironmentVariable("PATH");
        if (path != nullptr)
            for each (String^ dir in path->Split(';')) {
                if (dir->Trim()->Length == 0) continue;
                try {
                    String^ found = System::IO::Path::Combine(dir->Trim()->Trim('"'), orElse + ".exe");
                    if (System::IO::File::Exists(found)) return found;
                } catch (ArgumentException^) { }
            }
        return orElse;
    }

    System::Drawing::Font^ RememberedFont() {
        String^ said = FromUtf8(ride_code_font());
        if (said != nullptr && said->Length > 0) {
            int cut = said->LastIndexOf(' ');
            if (cut > 0) {
                String^ name = said->Substring(0, cut);
                double points = 0;
                if (Double::TryParse(said->Substring(cut + 1),
                                     System::Globalization::NumberStyles::Float,
                                     System::Globalization::CultureInfo::InvariantCulture,
                                     points) && points >= 6 && points <= 48) {
                    try {
                        System::Drawing::Font^ made =
                            gcnew System::Drawing::Font(name, (float)points);

                        if (made->FontFamily->Name == name) return made;
                    } catch (Exception^) { }
                }
            }
        }
        return gcnew System::Drawing::Font("Courier New", 14.0f, System::Drawing::FontStyle::Regular);
    }

    ToolStripMenuItem^ Item(String^ label, Keys key, EventHandler^ handler) {
        ToolStripMenuItem^ item = gcnew ToolStripMenuItem(label, nullptr, handler);
        item->ShortcutKeys = key;
        return item;
    }

    String^ Ask(String^ title, String^ initial) { return Ask(title, nullptr, initial); }

    String^ Ask(String^ title, String^ note, String^ initial) {
        msclr::auto_handle<Form> box(gcnew Form());
        box->Text = title;
        box->FormBorderStyle = System::Windows::Forms::FormBorderStyle::FixedDialog;
        box->StartPosition = System::Windows::Forms::FormStartPosition::CenterParent;
        box->MinimizeBox = false;
        box->MaximizeBox = false;

        int lift = note == nullptr || note->Length == 0 ? 0 : 24;
        box->ClientSize = System::Drawing::Size(480, 96 + lift);

        if (lift > 0) {
            Label^ says = gcnew Label();
            says->Text = note;
            says->AutoEllipsis = true;
            says->ForeColor = System::Drawing::Color::FromArgb(90, 90, 90);
            says->SetBounds(12, 12, 456, 20);
            box->Controls->Add(says);
        }

        TextBox^ entry = gcnew TextBox();
        entry->Text = initial == nullptr ? "" : initial;
        entry->SetBounds(12, 16 + lift, 456, 26);
        entry->Font = gcnew System::Drawing::Font("Consolas", 10.0f);
        entry->SelectAll();

        Button^ yes = gcnew Button();
        yes->Text = "OK";
        yes->DialogResult = System::Windows::Forms::DialogResult::OK;
        yes->SetBounds(306, 56 + lift, 78, 28);

        Button^ no = gcnew Button();
        no->Text = "Cancel";
        no->DialogResult = System::Windows::Forms::DialogResult::Cancel;
        no->SetBounds(390, 56 + lift, 78, 28);

        box->Controls->Add(entry);
        box->Controls->Add(yes);
        box->Controls->Add(no);
        box->AcceptButton = yes;
        box->CancelButton = no;

        if (box->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) return nullptr;
        return entry->Text;
    }

    // One of a list, or null when cancelled: the CCS workspace's projects, for now.
    String^ Pick(String^ title, String^ note, array<String^>^ choices) {
        msclr::auto_handle<Form> box(gcnew Form());
        box->Text = title;
        box->FormBorderStyle = System::Windows::Forms::FormBorderStyle::FixedDialog;
        box->StartPosition = System::Windows::Forms::FormStartPosition::CenterParent;
        box->MinimizeBox = false;
        box->MaximizeBox = false;
        box->ClientSize = System::Drawing::Size(480, 300);

        Label^ says = gcnew Label();
        says->Text = note;
        says->AutoEllipsis = true;
        says->ForeColor = System::Drawing::Color::FromArgb(90, 90, 90);
        says->SetBounds(12, 12, 456, 20);

        ListBox^ list = gcnew ListBox();
        list->SetBounds(12, 36, 456, 212);
        list->Font = gcnew System::Drawing::Font("Consolas", 10.0f);
        list->Items->AddRange(choices);
        if (choices->Length > 0) list->SelectedIndex = 0;

        Button^ yes = gcnew Button();
        yes->Text = "Open";
        yes->DialogResult = System::Windows::Forms::DialogResult::OK;
        yes->SetBounds(306, 260, 78, 28);
        Button^ no = gcnew Button();
        no->Text = "Cancel";
        no->DialogResult = System::Windows::Forms::DialogResult::Cancel;
        no->SetBounds(390, 260, 78, 28);
        // A double click is a choice, as Enter is.
        list->DoubleClick += gcnew EventHandler(this, &MainForm::OnPickDoubleClick);

        box->Controls->Add(says);
        box->Controls->Add(list);
        box->Controls->Add(yes);
        box->Controls->Add(no);
        box->AcceptButton = yes;
        box->CancelButton = no;

        if (box->ShowDialog(this) != System::Windows::Forms::DialogResult::OK || list->SelectedItem == nullptr) return nullptr;
        return safe_cast<String^>(list->SelectedItem);
    }

    void OnPickDoubleClick(Object^ sender, EventArgs^) {
        Form^ box = safe_cast<Control^>(sender)->FindForm();
        if (box == nullptr) return;
        box->DialogResult = System::Windows::Forms::DialogResult::OK;
    }

    // **A CCS workspace is a folder of projects, never one**: which of them, and then its
    // <workspace>/<project>.pro - workspace and project and nothing else - written if it is not there.
    String^ ChooseWorkspaceProject(String^ workspace) {
        Utf8 dir(workspace);
        String^ names = FromUtf8(ride_ccs_workspace_projects(dir.c()));
        if (names->Length == 0) {
            what_->Text = System::IO::Path::GetFileName(workspace) + " is a CCS workspace with no project RIDE can build";
            return nullptr;
        }
        array<String^>^ choices = names->Split(gcnew array<wchar_t>{ L'\n' });
        String^ chosen = Pick("Open CCS project", System::IO::Path::GetFileName(workspace) +
                              " is a CCS workspace - one of its projects opens", choices);
        if (chosen == nullptr) { what_->Text = "no project opened"; return nullptr; }

        Utf8 name(chosen);
        array<Byte>^ file = gcnew array<Byte>(1024);
        pin_ptr<Byte> filePin = &file[0];
        array<Byte>^ why = gcnew array<Byte>(512);
        pin_ptr<Byte> whyPin = &why[0];
        if (ride_ccs_workspace_pro(dir.c(), name.c(), reinterpret_cast<char*>(filePin), file->Length,
                                   reinterpret_cast<char*>(whyPin), why->Length) == 0) {
            what_->Text = FromUtf8(reinterpret_cast<const char*>(whyPin));
            return nullptr;
        }
        return FromUtf8(reinterpret_cast<const char*>(filePin));
    }

    // ---- one line at a time ------------------------------------------------------
    // Rich Edit's own messages, so that a keystroke reads the line it is on and not the document:
    // RichTextBox::Lines and ::Text copy all of it, which on a large file is the whole cost of typing.

    literal int kLineLength = 0x00C1;   // EM_LINELENGTH
    literal int kTextRange = 0x044B;    // EM_GETTEXTRANGE

    value struct TextRange {
        int from;
        int to;
        IntPtr text;
    };

    [System::Runtime::InteropServices::DllImport("user32.dll", EntryPoint = "SendMessageW")]
    static IntPtr Tell(IntPtr window, int message, IntPtr one, TextRange% range);

    // The characters from..to of a box, its paragraph marks as "\n".
    static String^ TextBetween(RichTextBox^ box, int from, int to) {
        if (to <= from) return String::Empty;
        array<wchar_t>^ room = gcnew array<wchar_t>(to - from + 1);
        pin_ptr<wchar_t> pinned = &room[0];
        TextRange range;
        range.from = from;
        range.to = to;
        range.text = IntPtr(pinned);
        int got = Tell(box->Handle, kTextRange, IntPtr::Zero, range).ToInt32();
        if (got <= 0) return String::Empty;
        return (gcnew String(room, 0, Math::Min(got, to - from)))->Replace("\r\n", "\n")->Replace('\r', '\n');
    }

    static String^ LineText(RichTextBox^ box, int row) {
        if (box == nullptr || row < 0) return String::Empty;
        int start = box->GetFirstCharIndexFromLine(row);
        if (start < 0) return String::Empty;
        int length = Tell(box->Handle, kLineLength, IntPtr(start), IntPtr::Zero).ToInt32();
        return TextBetween(box, start, start + length);
    }

    static int LineCount(RichTextBox^ box) {
        return box == nullptr ? 0 : box->GetLineFromCharIndex(box->TextLength) + 1;
    }

    // What the indenter reads: every line down to and including row - it never looks below.
    array<Byte>^ TextThrough(int row) {
        int end = text_->GetFirstCharIndexFromLine(row);
        if (end < 0) end = text_->TextLength;
        else end += Tell(text_->Handle, kLineLength, IntPtr(end), IntPtr::Zero).ToInt32();
        return Utf8Of(TextBetween(text_, 0, end));
    }

    int LeadingOf(int row) {
        String^ line = LineText(text_, row);
        int lead = 0;
        while (lead < line->Length && (line[lead] == ' ' || line[lead] == '\t')) ++lead;
        return lead;
    }

    int CaretRow() { return text_->GetLineFromCharIndex(text_->SelectionStart); }
    int CaretColumn() {
        return text_->SelectionStart - text_->GetFirstCharIndexFromLine(CaretRow());
    }

    int CharacterColumn(int row, int byteColumn) {
        if (row < 0 || row >= LineCount(text_)) return 0;
        array<Byte>^ bytes = Utf8Of(LineText(text_, row));
        int usable = bytes->Length - 1;
        if (byteColumn > usable) byteColumn = usable;
        if (byteColumn <= 0) return 0;
        return System::Text::Encoding::UTF8->GetString(bytes, 0, byteColumn)->Length;
    }

    int ByteColumn(int row, int characterColumn) {
        if (row < 0 || row >= LineCount(text_)) return 0;
        String^ line = LineText(text_, row);
        if (characterColumn > line->Length) characterColumn = line->Length;
        if (characterColumn <= 0) return 0;
        return System::Text::Encoding::UTF8->GetByteCount(line->Substring(0, characterColumn));
    }

    array<Byte>^ WholeText() { return Utf8Of(text_->Text->Replace("\r\n", "\n")); }

    Sheet^ Current() {
        if (files_ == nullptr || sheets_ == nullptr) return nullptr;
        int at = files_->SelectedIndex;
        if (at < 0 || at >= sheets_->Count) return nullptr;
        return sheets_[at];
    }

    static String^ OneName(String^ path) {
        if (path == nullptr) return nullptr;
        String^ full = path;
        try {
            full = System::IO::Path::GetFullPath(path);
        } catch (Exception^) {

        }
        return full->Replace('/', '\\')->TrimEnd('\\')->ToLowerInvariant();
    }

    static bool SamePath(String^ one, String^ other) {
        if (one == nullptr || other == nullptr) return false;
        return String::Equals(OneName(one), OneName(other), StringComparison::Ordinal);
    }

    Sheet^ SheetFor(String^ path) {
        for (int i = 0; i < sheets_->Count; ++i)
            if (SamePath(sheets_[i]->path, path)) return sheets_[i];
        return nullptr;
    }

    // The type spelled in full: inside a Form, the bare name is the
    // control's own ContextMenuStrip property.
    System::Windows::Forms::ContextMenuStrip^ EditMenuFor(RichTextBox^ box) {
        System::Windows::Forms::ContextMenuStrip^ menu = gcnew System::Windows::Forms::ContextMenuStrip();
        // First, where it is looked for: a header the line or the selection names, opened as a compiler finds it.
        menu->Items->Add("Open header", nullptr, gcnew EventHandler(this, &MainForm::OnOpenHeader));
        menu->Items->Add(gcnew ToolStripSeparator());
        menu->Items->Add("Undo", nullptr, gcnew EventHandler(this, &MainForm::OnUndo));
        menu->Items->Add("Redo", nullptr, gcnew EventHandler(this, &MainForm::OnRedo));
        menu->Items->Add(gcnew ToolStripSeparator());
        menu->Items->Add("Cut", nullptr, gcnew EventHandler(this, &MainForm::OnCut));
        menu->Items->Add("Copy", nullptr, gcnew EventHandler(this, &MainForm::OnCopy));
        menu->Items->Add("Paste", nullptr, gcnew EventHandler(this, &MainForm::OnPaste));
        menu->Items->Add(gcnew ToolStripSeparator());
        menu->Items->Add("Select all", nullptr, gcnew EventHandler(this, &MainForm::OnSelectAll));
        // Before it opens: Cut and Copy want a selection, Paste text on the
        // clipboard, Undo and Redo something to undo or redo.
        menu->Opening += gcnew System::ComponentModel::CancelEventHandler(this, &MainForm::OnEditMenuOpening);
        menu->Tag = box;
        return menu;
    }

    void OnEditMenuOpening(Object^ sender, System::ComponentModel::CancelEventArgs^) {
        System::Windows::Forms::ContextMenuStrip^ menu =
            safe_cast<System::Windows::Forms::ContextMenuStrip^>(sender);
        RichTextBox^ box = safe_cast<RichTextBox^>(menu->Tag);
        bool selected = box->SelectionLength > 0;
        // Open header and its separator are items 0 and 1; the editing ones follow.
        menu->Items[2]->Enabled = box->CanUndo;
        menu->Items[3]->Enabled = box->CanRedo;
        menu->Items[5]->Enabled = selected;
        menu->Items[6]->Enabled = selected;
        menu->Items[7]->Enabled = Clipboard::ContainsText();
        menu->Items[9]->Enabled = box->TextLength > 0;

        // The selection when there is one, else the line the caret is on.
        String^ text = box->SelectedText;
        if (String::IsNullOrEmpty(text)) {
            int line = box->GetLineFromCharIndex(box->SelectionStart);
            int from = box->GetFirstCharIndexFromLine(line);
            int to = box->GetFirstCharIndexFromLine(line + 1);
            if (to < 0) to = box->TextLength;
            text = from >= 0 ? TextBetween(box, from, to) : String::Empty;
        }
        Utf8 said(text);
        String^ name = FromUtf8(ride_header_named(said.c()));
        ToolStripItem^ open = menu->Items[0];
        menu->Items[1]->Visible = name->Length > 0;
        open->Visible = name->Length > 0;
        if (name->Length == 0) return;
        Utf8 wanted(name);
        Utf8 from(path_ == nullptr ? String::Empty : path_);
        String^ found = FromUtf8(ride_find_header(project_, from.c(), wanted.c()));
        open->Tag = found->Length > 0 ? found : nullptr;
        open->Enabled = found->Length > 0;
        open->Text = found->Length > 0 ? "Open \"" + name + "\"" : "\"" + name + "\" is not in any include folder";
        open->ToolTipText = found;
    }

    void OnOpenHeader(Object^ sender, EventArgs^) {
        String^ found = safe_cast<String^>(safe_cast<ToolStripItem^>(sender)->Tag);
        if (found == nullptr || found->Length == 0) return;
        OpenPath(found);
        what_->Text = found;
    }

    Sheet^ MakeSheet(String^ path, String^ contents) { return MakeSheet(path, contents, gcnew TextFile()); }

    Sheet^ MakeSheet(String^ path, String^ contents, TextFile^ file) {
        Sheet^ sheet = gcnew Sheet();
        sheet->path = path;
        sheet->file = file;

        sheet->box = gcnew RichTextBox();
        sheet->box->Dock = DockStyle::Fill;
        sheet->box->Font = codeFont_;
        sheet->box->WordWrap = false;
        sheet->box->AcceptsTab = true;
        sheet->box->HideSelection = false;
        sheet->box->BorderStyle = System::Windows::Forms::BorderStyle::Fixed3D;
        sheet->box->Text = contents == nullptr ? "" : contents;
        sheet->box->KeyDown += gcnew KeyEventHandler(this, &MainForm::OnKeyDown);
        sheet->box->KeyPress += gcnew KeyPressEventHandler(this, &MainForm::OnKeyPressed);
        sheet->box->SelectionChanged += gcnew EventHandler(this, &MainForm::OnCaretMoved);
        // The right-click menu: the Edit menu's own handlers, so that a paste
        // from here is the paste Ctrl-V does. A RichTextBox has none of its
        // own, which read as a dead right button.
        sheet->box->ContextMenuStrip = EditMenuFor(sheet->box);
        sheet->box->TextChanged += gcnew EventHandler(this, &MainForm::OnTextChanged);
        sheet->box->VScroll += gcnew EventHandler(this, &MainForm::OnScrolled);

        sheet->gutter = gcnew Gutter();
        sheet->gutter->Dock = DockStyle::Left;
        sheet->gutter->Width = GutterWidth(1);
        sheet->gutter->BackColor = System::Drawing::Color::FromArgb(245, 245, 245);
        sheet->gutter->Tag = sheet->box;
        sheet->gutter->Paint += gcnew PaintEventHandler(this, &MainForm::OnGutterPaint);
        sheet->gutter->Visible = numbers_;
        sheet->box->Tag = sheet->gutter;

        sheet->page = gcnew TabPage(TabName(sheet));
        sheet->page->Controls->Add(sheet->box);
        sheet->page->Controls->Add(sheet->gutter);
        sheet->box->BringToFront();

        sheets_->Add(sheet);
        PaneFollowsTabs();
        files_->TabPages->Add(sheet->page);
        files_->SelectedTab = sheet->page;
        // The first tab of an empty environment is selected as it is added, and selecting it again
        // raises no change - so the sheet is made current here, not left to the event. Without
        // this, File > New after an empty start had a sheet nothing could paste into.
        OnSheetChanged(nullptr, nullptr);
        return sheet;
    }

    // A sheet leaves the window: its tab, its box, its gutter and its menu go with it, now rather
    // than whenever the finalizer gets round to them; the stopped-line highlight goes if it was here.
    void DropSheet(Sheet^ sheet) {
        if (highlightSheet_ == sheet) { highlightSheet_ = nullptr; highlightRow_ = -1; }
        sheets_->Remove(sheet);
        files_->TabPages->Remove(sheet->page);
        System::Windows::Forms::ContextMenuStrip^ menu = sheet->box->ContextMenuStrip;
        delete sheet->page;
        delete menu;
    }

    // One name for a tab with no file behind it, everywhere it is shown.
    static String^ Untitled() { return "untitled"; }
    static String^ TabName(Sheet^ sheet) {
        return sheet->path == nullptr ? Untitled() : System::IO::Path::GetFileName(sheet->path);
    }

    void OnSheetChanged(Object^, EventArgs^) {
        Sheet^ sheet = Current();
        if (sheet == nullptr) return;

        text_ = sheet->box;
        path_ = sheet->path;
        RefreshTitle();
        what_->Text = path_ == nullptr
                          ? Untitled()
                          : System::IO::Path::GetFileName(path_) + "  " + LineCount(text_) + " lines";
        text_->Focus();
        ShowChoices();
        PlaceStopBar();
        sheet->gutter->Invalidate();

        Recolour();
        ReloadIfChanged(sheet);
        RefreshModeItems();
    }

    literal int kDrawing = 0x000B;
    literal int kWhereScrolled = 0x04DD;
    literal int kScrollTo = 0x04DE;

    [System::Runtime::InteropServices::DllImport("user32.dll", EntryPoint = "SendMessageW")]
    static IntPtr Tell(IntPtr window, int message, IntPtr one, IntPtr two);

    [System::Runtime::InteropServices::DllImport("user32.dll", EntryPoint = "SendMessageW")]
    static IntPtr Tell(IntPtr window, int message, IntPtr one, Spot% where);

    static void Drawing(Control^ box, bool allowed) {
        Tell(box->Handle, kDrawing, IntPtr(allowed ? 1 : 0), IntPtr::Zero);
        if (allowed) {
            box->Invalidate();
            box->Update();
        }
    }

    void OnTextChanged(Object^ sender, EventArgs^) {

        if (colouring_) return;

        Sheet^ sheet = Current();
        if (sheet == nullptr) return;

        int lines = LineCount(sheet->box);
        bool lineCountMoved = lines != sheet->lines;
        sheet->lines = lines;
        int wanted = GutterWidth(lines);
        if (wanted > sheet->gutter->Width) sheet->gutter->Width = wanted;
        sheet->gutter->Invalidate();

        if (sender == text_) {
            // The lexer state kept for stateRow_ holds while nothing above that row changed: a
            // line added or taken away, or an edit above it, is what moves it.
            int row = CaretRow();
            if (lineCountMoved || row < stateRow_) stateGood_ = false;
            RecolourLine(row);
            settle_->Stop();
            settle_->Start();
            MarkTab(sheet);
        }
    }

    void OnScrolled(Object^, EventArgs^) {
        // Recolour puts the scroll position back itself, and Rich Edit reports that as a scroll too.
        if (colouring_) return;
        PlaceStopBar();
        Sheet^ sheet = Current();
        if (sheet == nullptr) return;

        sheet->gutter->Invalidate();   // line numbers keep up while scrolling
        recolourTimer_->Stop();        // recolour once the wheel settles, so it
        recolourTimer_->Start();       // does not fight the scroll and tremble
    }

    void OnRecolourTick(Object^, EventArgs^) {
        recolourTimer_->Stop();
        Recolour();
        Sheet^ sheet = Current();
        if (sheet != nullptr) sheet->gutter->Invalidate();
    }

    // Room for the dot or arrow, the digits in the code's own face, and a margin.
    int GutterWidth(int lines) {
        int digits = Math::Max(2, (lines < 1 ? 1 : lines).ToString()->Length);
        return 16 + digitWidth_ * digits + 8;
    }

    static int DigitWidth(System::Drawing::Font^ font) {
        return System::Windows::Forms::TextRenderer::MeasureText(
            "0", font, System::Drawing::Size(100, 100),
            System::Windows::Forms::TextFormatFlags::NoPadding).Width;
    }

    // The gutter's pens and brushes, made once: it repaints on every keystroke and caret move.
    static System::Drawing::Pen^ edgePen_ = gcnew System::Drawing::Pen(System::Drawing::Color::FromArgb(228, 228, 228));
    static System::Drawing::Pen^ lookPen_ = gcnew System::Drawing::Pen(System::Drawing::Color::FromArgb(40, 150, 60));
    static System::Drawing::Brush^ breakBrush_ = gcnew System::Drawing::SolidBrush(System::Drawing::Color::FromArgb(200, 60, 60));
    static System::Drawing::Brush^ stopBrush_ = gcnew System::Drawing::SolidBrush(System::Drawing::Color::FromArgb(40, 150, 60));

    void OnGutterPaint(Object^ sender, PaintEventArgs^ e) {
        Panel^ panel = safe_cast<Panel^>(sender);
        RichTextBox^ box = safe_cast<RichTextBox^>(panel->Tag);
        if (box == nullptr) return;

        e->Graphics->DrawLine(edgePen_, panel->Width - 1, 0, panel->Width - 1, panel->Height);

        Sheet^ sheet = nullptr;
        for each (Sheet^ one in sheets_)
            if (one->gutter == panel) sheet = one;
        int lines = sheet != nullptr ? sheet->lines : LineCount(box);
        if (lines < 1) lines = 1;

        int first = box->GetLineFromCharIndex(box->GetCharIndexFromPosition(
            System::Drawing::Point(1, 1)));
        int caretLine = box->GetLineFromCharIndex(box->SelectionStart);

        String^ file = sheet == nullptr ? nullptr : sheet->path;
        System::Collections::Generic::List<int>^ marks = nullptr;
        if (file != nullptr) breaks_->TryGetValue(OneName(file), marks);

        bool sameFile = SamePath(file, stopFile_);
        bool sameLookFile = SamePath(file, lookingFile_);

        for (int row = first; row < lines; ++row) {
            int at = box->GetFirstCharIndexFromLine(row);
            if (at < 0) break;
            System::Drawing::Point where = box->GetPositionFromCharIndex(at);
            if (where.Y > panel->Height) break;

            float top = static_cast<float>(where.Y) + 3.0f;
            bool standingHere = sameFile && stopLine_ == row + 1;
            bool lookingHere = sameLookFile && lookingLine_ == row + 1;
            if (standingHere || lookingHere) {
                array<System::Drawing::PointF>^ arrow = gcnew array<System::Drawing::PointF>(3);
                arrow[0] = System::Drawing::PointF(3.0f, top);
                arrow[1] = System::Drawing::PointF(12.0f, top + 4.5f);
                arrow[2] = System::Drawing::PointF(3.0f, top + 9.0f);
                if (standingHere) e->Graphics->FillPolygon(stopBrush_, arrow);
                else e->Graphics->DrawPolygon(lookPen_, arrow);
            } else if (marks != nullptr && marks->Contains(row + 1)) {
                e->Graphics->FillEllipse(breakBrush_, 3.0f, top, 9.0f, 9.0f);
            }

            System::Drawing::Rectangle room(0, where.Y, panel->Width - 6, box->Font->Height);
            System::Windows::Forms::TextRenderer::DrawText(
                e->Graphics, (row + 1).ToString(), box->Font, room,
                row == caretLine ? System::Drawing::Color::FromArgb(60, 60, 60)
                                 : System::Drawing::Color::FromArgb(150, 150, 150),
                static_cast<System::Windows::Forms::TextFormatFlags>(
                    static_cast<int>(System::Windows::Forms::TextFormatFlags::Right) |
                    static_cast<int>(System::Windows::Forms::TextFormatFlags::NoPadding)));
        }
    }

    TextBox^ ReadOnlyBox() {
        TextBox^ box = gcnew TextBox();
        box->Dock = DockStyle::Fill;
        box->Multiline = true;
        box->ReadOnly = true;
        box->ScrollBars = ScrollBars::Both;
        box->WordWrap = false;
        box->BorderStyle = System::Windows::Forms::BorderStyle::Fixed3D;
        box->Font = gcnew System::Drawing::Font("Consolas", 10.0f);
        return box;
    }

    void SayDebugTab(String^ assembly) {
        array<Byte>^ bytes = Utf8Of(assembly == nullptr ? "" : assembly);
        pin_ptr<Byte> pinned = &bytes[0];
        String^ found = TakeUtf8(ride_describe_build(reinterpret_cast<const char*>(pinned)));

        array<Byte>^ archBytes = Utf8Of(arch_ == nullptr ? "" : arch_);
        pin_ptr<Byte> archPin = &archBytes[0];
        String^ note = TakeUtf8(ride_debug_note(
            ride_resolve(toolKind_, LanguageNow()),
            reinterpret_cast<const char*>(archPin)));

        debugView_->ShowMessage(String::Join(
            "\r\n",
            gcnew array<String^>{note->Replace("\n", "\r\n"), "",
                                 found->Replace("\n", "\r\n")}));
    }

    void RefreshDebugTab() {
        SayDebugTab(assembly_->Text->Replace("\r\n", "\n"));
    }

    // The Language menu's choice is the file's own: it stays with the tab, and another file keeps its suffix's.
    int LanguageNow() {
        Sheet^ sheet = Current();
        if (sheet != nullptr && sheet->language >= 0) return sheet->language;
        array<Byte>^ bytes = Utf8Of(path_ == nullptr ? "" : path_);
        pin_ptr<Byte> pinned = &bytes[0];
        return ride_language_for(reinterpret_cast<const char*>(pinned));
    }

    int DialectNow() { return ride_dialect_for(LanguageNow()); }

    // Put text in place of from..to as one step Ctrl+Z takes back: SelectedText, since assigning Text
    // empties Rich Edit's undo (measured on the box - CanUndo is false after it). The view stays put.
    void ReplaceRange(int from, int to, String^ with, int caret) {
        Spot scrolled;
        Tell(text_->Handle, kWhereScrolled, IntPtr::Zero, scrolled);
        Drawing(text_, false);
        try {
            text_->Select(from, to - from);
            text_->SelectedText = with;
            text_->Select(Math::Max(0, Math::Min(caret, text_->TextLength)), 0);
            Tell(text_->Handle, kScrollTo, IntPtr::Zero, scrolled);
        } finally {
            Drawing(text_, true);
        }
        stateGood_ = false;
        Recolour();
    }

    void OnLayOut(Object^, EventArgs^) {
        if (text_ == nullptr) { what_->Text = "no file is open"; return; }
        array<Byte>^ bytes = Utf8Of(text_->Text->Replace("\r\n", "\n"));
        pin_ptr<Byte> pinned = &bytes[0];

        String^ laid = TakeUtf8(ride_reindent(reinterpret_cast<const char*>(pinned),
                                             indentWidth_, indentTabs_, indentCase_,
                                             DialectNow()));

        int caret = text_->SelectionStart;
        int length = text_->SelectionLength;

        array<String^>^ now = laid->Split('\n');
        int howManyNow = now->Length;
        if (howManyNow > 0 && now[howManyNow - 1]->Length == 0) --howManyNow;
        int rows = LineCount(text_);

        if (length > 0 && howManyNow == rows) {
            int first = text_->GetLineFromCharIndex(caret);
            int last = text_->GetLineFromCharIndex(caret + length - 1);
            if (last >= rows) last = rows - 1;

            // Only the selected lines are replaced, so undo takes back only them.
            int from = text_->GetFirstCharIndexFromLine(first);
            int lastStart = text_->GetFirstCharIndexFromLine(last);
            int to = lastStart + LineText(text_, last)->Length;
            array<String^>^ block = gcnew array<String^>(last - first + 1);
            for (int row = first; row <= last; ++row) block[row - first] = now[row];
            ReplaceRange(from, to, String::Join("\n", block), caret);
            int howMany = last - first + 1;
            what_->Text = String::Format("laid out {0} line{1} of the selection - Ctrl+Z puts them back",
                                         howMany, howMany == 1 ? "" : "s");
            return;
        }

        ReplaceRange(0, text_->TextLength, laid, caret);
        what_->Text = String::Format("laid out - {0} lines - Ctrl+Z puts it back", LineCount(text_));
    }

    // Tab and Shift+Tab over a selection of whole lines: one level in or out on each, one undo step.
    void ShiftLines(bool outwards) {
        int start = text_->SelectionStart;
        int first = text_->GetLineFromCharIndex(start);
        int last = text_->GetLineFromCharIndex(start + Math::Max(0, text_->SelectionLength - 1));
        String^ level = indentTabs_ != 0 ? "\t" : gcnew String(' ', indentWidth_);
        array<String^>^ block = gcnew array<String^>(last - first + 1);
        for (int row = first; row <= last; ++row) {
            String^ line = LineText(text_, row);
            if (!outwards) {
                block[row - first] = line->Length == 0 ? line : level + line;
                continue;
            }
            int cut = 0;
            if (line->Length > 0 && line[0] == '\t') cut = 1;
            else while (cut < indentWidth_ && cut < line->Length && line[cut] == ' ') ++cut;
            block[row - first] = line->Substring(cut);
        }
        int from = text_->GetFirstCharIndexFromLine(first);
        int to = text_->GetFirstCharIndexFromLine(last) + LineText(text_, last)->Length;
        String^ joined = String::Join("\n", block);
        int caret = first == last ? Math::Max(from, start + joined->Length - (to - from)) : from;
        ReplaceRange(from, to, joined, caret);
        if (first != last) text_->Select(from, joined->Length);
    }

    void OnKeyDown(Object^, KeyEventArgs^ e) {
        if (text_ == nullptr) return;
        // Shift+Insert is Rich Edit's own paste and would bring the formatting OnPaste leaves behind.
        if (e->KeyCode == Keys::Insert && e->Shift && !e->Control) {
            e->SuppressKeyPress = true;
            OnPaste(nullptr, nullptr);
            return;
        }

        if (e->KeyCode == Keys::Tab && !e->Control) {
            e->SuppressKeyPress = true;
            int start = text_->SelectionStart;
            bool lines = text_->SelectionLength > 0 &&
                         text_->GetLineFromCharIndex(start) !=
                             text_->GetLineFromCharIndex(start + text_->SelectionLength - 1);
            if (e->Shift || lines) { ShiftLines(e->Shift); return; }

            int row = CaretRow();
            int column = CaretColumn();
            String^ line = LineText(text_, row);
            int lead = 0;
            while (lead < line->Length && (line[lead] == ' ' || line[lead] == '\t')) ++lead;

            if (column <= lead && text_->SelectionLength == 0) {
                Realign(row);
                text_->SelectionStart = text_->GetFirstCharIndexFromLine(row) +
                                        LeadingOf(row);
            } else if (indentTabs_ != 0) {
                text_->SelectedText = "\t";
            } else {
                // Spaces to the next stop, counting a tab already in the line as reaching one.
                int seen = 0;
                for (int i = 0; i < column && i < line->Length; ++i)
                    seen = line[i] == '\t' ? (seen / indentWidth_ + 1) * indentWidth_ : seen + 1;
                int width = Math::Max(1, indentWidth_);
                text_->SelectedText = gcnew String(' ', width - seen % width);
            }
            return;
        }

        if (e->KeyCode != Keys::Enter || e->Control || e->Shift) return;

        int caret = text_->SelectionStart;
        int row = text_->GetLineFromCharIndex(caret);
        int column = caret - text_->GetFirstCharIndexFromLine(row);

        array<Byte>^ bytes = TextThrough(row);
        pin_ptr<Byte> pinned = &bytes[0];

        String^ lead = TakeUtf8(ride_indent_after_newline(
            reinterpret_cast<const char*>(pinned), row, column, indentWidth_, indentTabs_,
            indentCase_, DialectNow()));

        e->SuppressKeyPress = true;
        text_->SelectedText = "\r\n" + lead;
    }

    // Every pair below is closed in a finally: one exception between the two used to leave the
    // box unpainted, undo unrecorded and every later colouring pass skipped as already running.
    void BeginColouring() {
        colouring_ = true;
        if (text_ != nullptr && text_->IsHandleCreated)
            ride_undo_suspend(text_->Handle.ToPointer());
    }

    void EndColouring() {
        if (text_ != nullptr && text_->IsHandleCreated)
            ride_undo_resume(text_->Handle.ToPointer());
        colouring_ = false;
    }

    // The stopped-at background, painted into the sheet it belongs to, whichever is in front.
    void PaintRow(Sheet^ sheet, int row, bool on) {
        if (sheet == nullptr) return;
        RichTextBox^ box = sheet->box;
        if (box == nullptr || box->IsDisposed || row < 0 || row >= LineCount(box)) return;
        int start = box->GetFirstCharIndexFromLine(row);
        int length = LineText(box, row)->Length;
        if (row < LineCount(box) - 1) length += 1;
        int caret = box->SelectionStart;
        int chosen = box->SelectionLength;
        bool touched = box->Modified;

        RichTextBox^ was = text_;
        text_ = box;
        BeginColouring();
        try {
            box->Select(start, length);
            box->SelectionBackColor =
                on ? System::Drawing::Color::FromArgb(214, 234, 255) : box->BackColor;
            box->Select(caret, chosen);
            box->Modified = touched;
        } finally {
            EndColouring();
            text_ = was;
        }
    }

    void ShowStoppedLine(Sheet^ sheet, int row) {
        if (highlightSheet_ != sheet || highlightRow_ != row) {
            if (highlightRow_ >= 0) PaintRow(highlightSheet_, highlightRow_, false);
            highlightSheet_ = row >= 0 ? sheet : nullptr;
            highlightRow_ = row;
            if (row >= 0) PaintRow(sheet, row, true);
        }
        PlaceStopBar();
    }

    void MakeStopBar() {
        if (stopBar_ != nullptr) return;
        stopBar_ = gcnew Panel();
        stopBar_->BackColor = System::Drawing::Color::FromArgb(214, 234, 255);
        stopBar_->Visible = false;
        stopBar_->TabStop = false;
        Controls->Add(stopBar_);
        stopBar_->BringToFront();
    }

    void PlaceStopBar() {
        MakeStopBar();
        Sheet^ sheet = Current();
        if (text_ == nullptr || highlightRow_ < 0 || !text_->IsHandleCreated ||
            sheet == nullptr || sheet != highlightSheet_) {
            stopBar_->Visible = false;
            return;
        }
        if (highlightRow_ >= LineCount(text_)) { stopBar_->Visible = false; return; }

        int index = text_->GetFirstCharIndexFromLine(highlightRow_);
        System::Drawing::Point where = text_->GetPositionFromCharIndex(index);
        int height = System::Windows::Forms::TextRenderer::MeasureText("Ay", text_->Font).Height;

        if (where.Y < -height || where.Y > text_->ClientSize.Height) {
            stopBar_->Visible = false;
            return;
        }

        int after = index + LineText(text_, highlightRow_)->Length;
        System::Drawing::Point ends = text_->GetPositionFromCharIndex(after);
        int from = ends.Y == where.Y ? ends.X : where.X;

        System::Drawing::Point corner =
            PointToClient(text_->PointToScreen(System::Drawing::Point(from, where.Y)));
        int width = text_->ClientSize.Width - from;
        if (width <= 0) { stopBar_->Visible = false; return; }

        stopBar_->SetBounds(corner.X, corner.Y, width, height);
        stopBar_->Visible = true;
        stopBar_->BringToFront();
    }

    // Colours one line's runs from the lexer's kinds, starting at character at.
    void ColourRuns(int at, array<Byte>^ bytes, array<Byte>^ kinds, int howMany) {
        int column = 0;
        int byte = 0;
        while (byte < howMany) {
            Byte kind = kinds[byte];
            int end = byte;
            while (end < howMany && kinds[end] == kind) ++end;

            int width = System::Text::Encoding::UTF8->GetString(bytes, byte, end - byte)->Length;
            if (width > 0 && kind != RIDE_KIND_NORMAL) {
                text_->Select(at + column, width);
                text_->SelectionColor = ColourOf(kind);
            }
            column += width;
            byte = end;
        }
    }

    // One line through the lexer, state carried in and out; its kinds, and how many it filled.
    static array<Byte>^ Lex(String^ line, int language, int% state, array<Byte>^% bytes, int% howMany) {
        bytes = Utf8Of(line);
        pin_ptr<Byte> linePin = &bytes[0];
        array<Byte>^ kinds = gcnew array<Byte>(bytes->Length);
        pin_ptr<Byte> kindPin = &kinds[0];
        int carried = state;
        howMany = ride_highlight(reinterpret_cast<const char*>(linePin), language, &carried,
                                 kindPin, kinds->Length);
        state = carried;
        return kinds;
    }

    void Recolour() {
        if (colouring_) return;
        if (text_ == nullptr || !text_->IsHandleCreated) return;
        BeginColouring();
        bool drawingOff = false;
        Spot scrolled;
        try {
            array<String^>^ all = text_->Lines;
            int language = LanguageNow();

            int top = text_->GetLineFromCharIndex(
                text_->GetCharIndexFromPosition(System::Drawing::Point(1, 1)));
            int bottom = text_->GetLineFromCharIndex(text_->GetCharIndexFromPosition(
                System::Drawing::Point(1, Math::Max(1, text_->ClientSize.Height - 2))));
            int deep = Math::Max(1, bottom - top + 1);
            int from = Math::Max(0, top - deep);
            int to = Math::Min(all->Length - 1, bottom + deep);

            bool touched = text_->Modified;
            int caret = text_->SelectionStart;
            int length = text_->SelectionLength;

            stateGood_ = false;
            Tell(text_->Handle, kWhereScrolled, IntPtr::Zero, scrolled);
            Drawing(text_, false);
            drawingOff = true;

            int state = 0;
            array<Byte>^ bytes;
            int howMany = 0;
            for (int row = 0; row < from; ++row) Lex(all[row], language, state, bytes, howMany);

            if (from <= to) {
                int start = text_->GetFirstCharIndexFromLine(from);
                int end = to + 1 < all->Length ? text_->GetFirstCharIndexFromLine(to + 1)
                                               : text_->TextLength;
                if (start >= 0 && end > start) {
                    text_->Select(start, end - start);
                    text_->SelectionColor = System::Drawing::Color::Black;
                }
            }

            for (int row = from; row <= to; ++row) {
                array<Byte>^ kinds = Lex(all[row], language, state, bytes, howMany);
                int at = text_->GetFirstCharIndexFromLine(row);
                if (at < 0) break;
                ColourRuns(at, bytes, kinds, howMany);
            }

            text_->Select(caret, length);
            text_->SelectionColor = System::Drawing::Color::Black;
            Tell(text_->Handle, kScrollTo, IntPtr::Zero, scrolled);
            text_->Modified = touched;
        } finally {
            if (drawingOff) Drawing(text_, true);
            EndColouring();
        }
    }

    void OnSettled(Object^, EventArgs^) {
        settle_->Stop();
        Recolour();
    }

    void RecolourLine(int row) {
        if (colouring_ || text_ == nullptr || !text_->IsHandleCreated) return;
        if (row < 0 || row >= LineCount(text_)) return;

        BeginColouring();
        try {
            int language = LanguageNow();
            array<Byte>^ bytes;
            int howMany = 0;

            int state = 0;
            if (stateGood_ && stateRow_ == row) {
                state = stateAt_;
            } else {
                // Once per row the caret settles on: every line above, lexed for the state it leaves.
                array<String^>^ all = text_->Lines;
                for (int above = 0; above < row && above < all->Length; ++above)
                    Lex(all[above], language, state, bytes, howMany);
                stateGood_ = true;
                stateRow_ = row;
                stateAt_ = state;
            }

            bool touched = text_->Modified;
            int caret = text_->SelectionStart;
            int length = text_->SelectionLength;

            int at = text_->GetFirstCharIndexFromLine(row);
            if (at >= 0) {
                String^ line = LineText(text_, row);
                text_->Select(at, line->Length);
                text_->SelectionColor = System::Drawing::Color::Black;
                array<Byte>^ kinds = Lex(line, language, state, bytes, howMany);
                ColourRuns(at, bytes, kinds, howMany);
            }

            text_->Select(caret, length);
            text_->SelectionColor = System::Drawing::Color::Black;
            text_->Modified = touched;
        } finally {
            EndColouring();
        }
    }

    System::Drawing::Color ColourOf(Byte kind) {
        switch (kind) {
            case RIDE_KIND_KEYWORD: return System::Drawing::Color::Blue;
            case RIDE_KIND_TYPE:    return System::Drawing::Color::Teal;
            case RIDE_KIND_STRING:  return System::Drawing::Color::FromArgb(0, 128, 0);
            case RIDE_KIND_CHAR:    return System::Drawing::Color::FromArgb(0, 128, 0);
            case RIDE_KIND_COMMENT: return System::Drawing::Color::Gray;
            case RIDE_KIND_PREPROC: return System::Drawing::Color::Purple;
            case RIDE_KIND_NUMBER:  return System::Drawing::Color::FromArgb(180, 100, 0);
            case RIDE_KIND_LABEL:   return System::Drawing::Color::FromArgb(150, 120, 0);
            default:               return System::Drawing::Color::Black;
        }
    }

    void OnUndo(Object^, EventArgs^) {
        if (text_ == nullptr) { what_->Text = "no file is open"; return; }

        if (!text_->CanUndo) { what_->Text = "nothing to undo"; return; }
        text_->Undo();
        stateGood_ = false;
    }
    void OnRedo(Object^, EventArgs^) {
        if (text_ == nullptr) { what_->Text = "no file is open"; return; }
        if (!text_->CanRedo) { what_->Text = "nothing to redo"; return; }
        text_->Redo();
        stateGood_ = false;
    }
    void OnCut(Object^, EventArgs^) { if (text_ != nullptr) text_->Cut(); }
    void OnCopy(Object^, EventArgs^) { if (text_ != nullptr) text_->Copy(); }
    void OnPaste(Object^, EventArgs^) {
        if (text_ == nullptr) { what_->Text = "no file is open"; return; }

        if (!Clipboard::ContainsText()) {
            what_->Text = "there is nothing to paste";
            return;
        }
        // The text alone: Paste() takes the richest format there, and Word's fonts or a
        // browser's pictures have no place in a source file.
        text_->Paste(DataFormats::GetFormat(DataFormats::UnicodeText));
        stateGood_ = false;
        Recolour();
    }
    void OnSelectAll(Object^, EventArgs^) { if (text_ != nullptr) text_->SelectAll(); }

    void OnFind(Object^, EventArgs^) {
        String^ want = Ask("Find", needle_);

        if (want == nullptr || want->Length == 0) {
            what_->Text = "nothing looked for";
            return;
        }
        needle_ = want;
        Seek(CaretRow(), ByteColumn(CaretRow(), CaretColumn()), true);
    }

    void OnFindNext(Object^, EventArgs^) {
        if (needle_ == nullptr) { OnFind(nullptr, nullptr); return; }
        Seek(CaretRow(), ByteColumn(CaretRow(), CaretColumn()) + 1, true);
    }

    void OnFindPrevious(Object^, EventArgs^) {
        if (needle_ == nullptr) { OnFind(nullptr, nullptr); return; }
        Seek(CaretRow(), ByteColumn(CaretRow(), CaretColumn()), false);
    }

    void Seek(int row, int column, bool forwards) {
        if (text_ == nullptr) return;
        array<Byte>^ text = WholeText();
        pin_ptr<Byte> textPin = &text[0];
        array<Byte>^ needle = Utf8Of(needle_);
        pin_ptr<Byte> needlePin = &needle[0];

        int foundRow = 0, foundColumn = 0;
        int found = forwards ? ride_find_next(reinterpret_cast<const char*>(textPin),
                                             reinterpret_cast<const char*>(needlePin), row,
                                             column, &foundRow, &foundColumn)
                             : ride_find_previous(reinterpret_cast<const char*>(textPin),
                                                 reinterpret_cast<const char*>(needlePin), row,
                                                 column, &foundRow, &foundColumn);
        if (found == 0) {
            what_->Text = needle_ + " is not in this file";
            return;
        }

        int at = text_->GetFirstCharIndexFromLine(foundRow) +
                 CharacterColumn(foundRow, foundColumn);
        text_->Select(at, needle_->Length);
        text_->ScrollToCaret();

        Recolour();
        text_->Focus();
        what_->Text = String::Format("{0} - line {1}", needle_, foundRow + 1);
    }

    void OnReplace(Object^, EventArgs^) {
        if (text_ == nullptr) { what_->Text = "no file is open"; return; }

        String^ want = Ask("Replace what", needle_);
        if (want == nullptr || want->Length == 0) {
            what_->Text = "nothing replaced";
            return;
        }
        String^ with = Ask("Replace \"" + want + "\" with", "");
        if (with == nullptr) {
            what_->Text = "nothing replaced";
            return;
        }

        array<Byte>^ text = WholeText();
        pin_ptr<Byte> textPin = &text[0];
        array<Byte>^ needle = Utf8Of(want);
        pin_ptr<Byte> needlePin = &needle[0];
        array<Byte>^ replacement = Utf8Of(with);
        pin_ptr<Byte> replacementPin = &replacement[0];

        int howMany = 0;
        String^ changed = TakeUtf8(ride_replace_all(
            reinterpret_cast<const char*>(textPin), reinterpret_cast<const char*>(needlePin),
            reinterpret_cast<const char*>(replacementPin), &howMany));

        if (howMany == 0) {
            what_->Text = want + " is not in this file";
            return;
        }

        ReplaceRange(0, text_->TextLength, changed, text_->SelectionStart);
        needle_ = with;
        what_->Text = String::Format("{0} change{1} - Ctrl-Z puts them back", howMany,
                                     howMany == 1 ? "" : "s");
    }

    void Realign(int row) {
        if (row < 0 || row >= LineCount(text_)) return;

        String^ line = LineText(text_, row);
        int lead = 0;
        while (lead < line->Length && (line[lead] == ' ' || line[lead] == '\t')) ++lead;

        array<Byte>^ text = TextThrough(row);
        pin_ptr<Byte> textPin = &text[0];
        String^ want = TakeUtf8(ride_indent_for(reinterpret_cast<const char*>(textPin), row,
                                               indentWidth_, indentTabs_, indentCase_,
                                               DialectNow()));
        if (want == line->Substring(0, lead)) return;

        int start = text_->GetFirstCharIndexFromLine(row);
        int caret = text_->SelectionStart;

        text_->Select(start, lead);
        text_->SelectedText = want;
        text_->SelectionStart = Math::Max(0, Math::Min(caret + want->Length - lead,
                                                       text_->TextLength));
    }

    // The core's indenter reads code; a JSON or plain file is left as typed.
    bool IndentsCode() {
        int language = LanguageNow();
        return language == RIDE_LANG_C || language == RIDE_LANG_CPP || language == RIDE_LANG_SHALIMAR;
    }

    // Typing }, # or : may move the line to where it belongs. Asked of the character typed - never of a
    // key that only moves - and after it is in the box, which a posted call is.
    void OnKeyPressed(Object^, KeyPressEventArgs^ e) {
        if (e->KeyChar != '}' && e->KeyChar != '#' && e->KeyChar != ':') return;
        if (text_ == nullptr || text_->ReadOnly || !IndentsCode()) return;
        BeginInvoke(gcnew Action(this, &MainForm::RealignTyped));
    }

    void RealignTyped() {
        if (text_ == nullptr || text_->SelectionLength != 0) return;
        int caret = text_->SelectionStart;
        if (caret <= 0) return;
        int row = text_->GetLineFromCharIndex(caret - 1);
        String^ line = LineText(text_, row);
        int column = (caret - 1) - text_->GetFirstCharIndexFromLine(row);
        if (column < 0 || column >= line->Length) return;
        wchar_t just = line[column];
        if (just != '}' && just != '#' && just != ':') return;

        int lead = 0;
        while (lead < line->Length && (line[lead] == ' ' || line[lead] == '\t')) ++lead;
        if (just != ':') {
            if (column != lead) return;   // only a } or # that opens its line
        } else {
            // A case, a default or an access specifier - not a ternary, a :: or a string. A plain
            // goto label is left alone too: "std:" looks like one until the second colon arrives.
            if (column + 1 < line->Length && line[column + 1] == ':') return;
            if (column > 0 && line[column - 1] == ':') return;
            String^ head = line->Substring(lead, column - lead)->TrimEnd();
            String^ word = head;
            int space = head->IndexOfAny(gcnew array<wchar_t>{' ', '\t', '('});
            if (space >= 0) word = head->Substring(0, space);
            bool caseLike = word == "case" || word == "default";
            bool access = head == "public" || head == "private" || head == "protected";
            if (!caseLike && !access) return;
            if (head->IndexOf('"') >= 0) return;
        }
        Realign(row);
    }

    void OnCaretMoved(Object^ sender, EventArgs^) {

        if (colouring_) return;
        RichTextBox^ box = dynamic_cast<RichTextBox^>(sender);
        if (box == nullptr || box != text_) return;

        int caret = box->SelectionStart;
        int row = box->GetLineFromCharIndex(caret);
        where_->Text =
            String::Format("{0}:{1}", row + 1, caret - box->GetFirstCharIndexFromLine(row) + 1);

        Sheet^ sheet = Current();
        if (sheet != nullptr) sheet->gutter->Invalidate();
    }

    // The project's own settings, read when it is opened or begun - never on the way to a tab, where
    // they used to put back a compiler the Tools menu had just changed. AUTO stays AUTO: "By language".
    void TakeProjectSettings() {
        indentWidth_ = ride_project_indent_width(project_);
        indentTabs_ = ride_project_indent_tabs(project_);
        indentCase_ = ride_project_case_indent(project_);
        toolKind_ = ride_project_toolchain(project_);
        config_ = ride_configuration();
        arch_ = FromUtf8(ride_project_arch(project_));
        ShowChoices();
    }

    // The installation's, when no project is open.
    void TakeInstallationSettings() {
        indentWidth_ = ride_default_indent_width();
        indentTabs_ = ride_default_indent_tabs();
        indentCase_ = 0;
        toolKind_ = ride_default_compiler();
        arch_ = "x86_64-windows";
        ShowChoices();
    }

    // A .pro file, or a directory with one in it. Tried on a project of its own, so a load that
    // fails leaves the one already open exactly as it was.
    void LoadProject(String^ where) {
        // A CCS workspace, however it arrived - the recent list, a dropped folder: a project of it.
        if (System::IO::Directory::Exists(where)) {
            Utf8 dir(where);
            if (ride_ccs_is_workspace(dir.c()) != 0) {
                where = ChooseWorkspaceProject(where);
                if (where == nullptr) return;
            }
        }
        bool named = System::IO::File::Exists(where);
        String^ directory = named ? System::IO::Path::GetDirectoryName(where) : where;

        RIDEProject* trying = ride_project_new();
        Utf8 whereText(where);
        array<Byte>^ error = gcnew array<Byte>(512);
        pin_ptr<Byte> errorPin = &error[0];

        int loaded = ride_project_load(trying, whereText.c(), reinterpret_cast<char*>(errorPin), error->Length);
        String^ said = nullptr;
        if (loaded == 0) {
            String^ why = FromUtf8(reinterpret_cast<const char*>(errorPin));
            Utf8 dirText(directory);
            if (why->Length > 0 || ride_begin_from_what_is_there(trying, dirText.c()) == 0) {
                ride_project_free(trying);
                what_->Text = why->Length > 0 ? why : "no .pro project in that directory";
                // With nothing open, the directory asked for is where a new file would go.
                if (ride_project_loaded(project_) == 0 && System::IO::Directory::Exists(directory)) {
                    ride_project_set_root(project_, dirText.c());
                    projectDirectory_ = directory;
                    SayWhere();
                }
                return;
            }
            said = FromUtf8(ride_outcome_message(trying));
        }

        ride_project_free(project_);
        project_ = trying;
        projectDirectory_ = directory;
        FillTree();
        TakeProjectSettings();

        Utf8 remembered(said != nullptr ? where : directory);
        ride_remember_project(remembered.c());
        RefreshRecent();

        what_->Text = said != nullptr ? said
                                      : String::Format("ready - {0}, {1} groups",
                                                       FromUtf8(ride_project_name(project_)),
                                                       ride_project_groups(project_));
        SayWhere();
        RefreshTitle();
        SayCcsProject();

        // The project's own file comes to the front whichever way the
        // project was opened - Start() asks the same after the command
        // line's files, and skips this when one of them is named.
        if (started_ && said == nullptr) {
            String^ kept = what_->Text;
            OpenFirstOfProject();
            what_->Text = kept;
        }
    }

    // A CCS project opened as it is (bridge.h): its remembered configuration, and the line naming
    // what of it RIDE cannot honour - said, and kept on the Console where the next message does not overwrite it.
    void SayCcsProject() {
        if (project_ == nullptr || ride_project_is_ccs(project_) == 0) return;
        int remembered = ride_project_ccs_configuration(project_);
        if (remembered >= 0) { config_ = remembered; ShowChoices(); }
        String^ report = FromUtf8(ride_project_ccs_report(project_, config_));
        if (report->Length == 0) return;
        // On a line of its own, never run on from the placeholder or whatever the pane ended with.
        String^ before = console_->TextLength > 0 && !console_->Text->EndsWith("\n") ? "\n" : "";
        Say(before + report + "\n");
        what_->Text = report->Split('\n')[0];
    }

    void PaneFollowsTabs() {

        if (project_ == nullptr || tree_ == nullptr) return;

        FillTree();
    }

    // The pane and nothing else: what is open, or the project's groups. Kept for later while the
    // worker is in the project, and its selection and scroll are put back after.
    void FillTree() {
        if (busy_) { treeStale_ = true; return; }
        treeStale_ = false;
        String^ chosen = tree_->SelectedNode == nullptr ? nullptr : tree_->SelectedNode->FullPath;
        TreeNode^ topNode = tree_->TopNode;
        String^ top = topNode == nullptr ? nullptr : topNode->FullPath;

        tree_->BeginUpdate();
        try {
            tree_->Nodes->Clear();

            if (ride_project_loaded(project_) == 0) {
                for (int i = 0; i < sheets_->Count; ++i) {
                    TreeNode^ leaf = gcnew TreeNode(TabName(sheets_[i]));
                    leaf->Tag = sheets_[i]->path;
                    tree_->Nodes->Add(leaf);
                }
            } else {
                int groups = ride_project_groups(project_);
                for (int group = 0; group < groups; ++group) {
                    TreeNode^ node = gcnew TreeNode(FromUtf8(ride_project_group_name(project_, group)));
                    int files = ride_project_files(project_, group);
                    for (int file = 0; file < files; ++file) {
                        String^ relative = FromUtf8(ride_project_file(project_, group, file));
                        TreeNode^ leaf = gcnew TreeNode(relative);
                        Utf8 rel(relative);
                        leaf->Tag = FromUtf8(ride_project_absolute(project_, rel.c()));
                        node->Nodes->Add(leaf);
                    }
                    tree_->Nodes->Add(node);
                }
                // The project and what is open, both at once, as the macOS navigator shows them.
                TreeNode^ open = gcnew TreeNode("Open files");
                open->Name = OpenFilesNode;
                for (int i = 0; i < sheets_->Count; ++i) {
                    TreeNode^ leaf = gcnew TreeNode(TabName(sheets_[i]));
                    leaf->Tag = sheets_[i]->path;
                    open->Nodes->Add(leaf);
                }
                tree_->Nodes->Add(open);
                tree_->ExpandAll();
            }
            TreeNode^ again = FindNode(tree_->Nodes, chosen);
            if (again != nullptr) tree_->SelectedNode = again;
            TreeNode^ above = FindNode(tree_->Nodes, top);
            if (above != nullptr) tree_->TopNode = above;
        } finally {
            tree_->EndUpdate();
        }
    }

    static TreeNode^ FindNode(TreeNodeCollection^ nodes, String^ path) {
        if (path == nullptr) return nullptr;
        for each (TreeNode^ node in nodes) {
            if (node->FullPath == path) return node;
            TreeNode^ below = FindNode(node->Nodes, path);
            if (below != nullptr) return below;
        }
        return nullptr;
    }

    literal String^ OpenFilesNode = "$open";

    String^ TargetFile() {
        if (tree_->SelectedNode != nullptr && tree_->SelectedNode->Tag != nullptr)
            return safe_cast<String^>(tree_->SelectedNode->Tag);
        return path_;
    }

    String^ GroupUnderCursor() {
        TreeNode^ node = tree_->SelectedNode;
        while (node != nullptr && node->Tag != nullptr) node = node->Parent;
        return node == nullptr || node->Name == OpenFilesNode ? "Sources" : node->Text;
    }

    bool Did(int outcome) {
        what_->Text = FromUtf8(ride_outcome_message(project_));
        return outcome != 0;
    }

    String^ OutcomePath() { return FromUtf8(ride_outcome_path(project_)); }

    String^ GroupForFile(String^ name) {
        if (name == nullptr || name->Length == 0) return "";
        array<Byte>^ leaf = Utf8Of(System::IO::Path::GetFileName(name));
        pin_ptr<Byte> pinned = &leaf[0];
        return FromUtf8(ride_group_for_file(reinterpret_cast<const char*>(pinned)));
    }

    void OnNewFile(Object^, EventArgs^) {
        String^ root = RootNow();

        // No project open: a single program is made under Documents\RIDE\programs rather
        // than in the read-only directory the shortcut started the editor in.
        if (root == nullptr || root->Length == 0) {
            String^ programs = ProgramsDir();
            String^ only = Ask("New program (name, or one directory and a name)",
                               "It will be made in " + programs, "");
            if (only == nullptr || only->Length == 0) { what_->Text = "nothing made"; return; }
            // The extension picks the compiler; a name without one gets the
            // chosen compiler's, and C when the choice is automatic.
            if (System::IO::Path::GetFileName(only)->IndexOf('.') < 0)
                only += toolKind_ == RIDE_TOOL_SHC ? ".shl"
                      : toolKind_ == RIDE_TOOL_CC1 || toolKind_ == RIDE_TOOL_AUTO ? ".c"
                                                                                        : ".cpp";
            String^ target = System::IO::Path::Combine(programs, only);
            if (System::IO::File::Exists(target)) {
                what_->Text = only + " is already there"; return;
            }
            try {
                String^ parent = System::IO::Path::GetDirectoryName(target);
                if (parent != nullptr && parent->Length > 0)
                    System::IO::Directory::CreateDirectory(parent);
                System::IO::File::Create(target)->Close();
            } catch (Exception^ ex) {
                what_->Text = "could not make " + only + " - " + ex->Message; return;
            }
            OpenPath(target);
            what_->Text = only + " made in " + programs;
            return;
        }

        String^ name = Ask("New file (name, or one directory and a name)",
                           "It will be made in " + root,
                           "");
        if (name == nullptr || name->Length == 0) { what_->Text = "nothing made"; return; }

        array<Byte>^ relative = Utf8Of(name);
        pin_ptr<Byte> relativePin = &relative[0];

        String^ wanted = GroupForFile(name);
        if (wanted->Length == 0) wanted = GroupUnderCursor();
        array<Byte>^ group = Utf8Of(wanted);
        pin_ptr<Byte> groupPin = &group[0];

        if (!Did(ride_create_file(project_, reinterpret_cast<const char*>(relativePin),
                                 reinterpret_cast<const char*>(groupPin), toolKind_)))
            return;

        FillTree();
        OpenPath(OutcomePath());
    }

    void OnFont(Object^, EventArgs^) {
        msclr::auto_handle<FontDialog> pick(gcnew FontDialog());
        pick->Font = codeFont_;
        pick->FixedPitchOnly = true;
        pick->ShowEffects = false;
        pick->ShowColor = false;
        pick->MinSize = 6;
        pick->MaxSize = 48;
        if (pick->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) {
            what_->Text = "font unchanged";
            return;
        }

        UseFont(pick->Font);

        String^ said = String::Format(
            System::Globalization::CultureInfo::InvariantCulture, "{0} {1}",
            codeFont_->FontFamily->Name, codeFont_->SizeInPoints);
        array<Byte>^ bytes = Utf8Of(said);
        pin_ptr<Byte> pinned = &bytes[0];
        ride_remember_code_font(reinterpret_cast<const char*>(pinned));
        what_->Text = said;
    }

    void UseFont(System::Drawing::Font^ chosen) {
        codeFont_ = chosen;
        digitWidth_ = DigitWidth(codeFont_);
        for each (Sheet^ sheet in sheets_) {
            bool touched = sheet->box->Modified;
            sheet->box->Font = codeFont_;
            sheet->box->Modified = touched;
            sheet->gutter->Width = GutterWidth(sheet->lines);
            sheet->gutter->Invalidate();
        }
        Recolour();
        PlaceStopBar();
    }

    void OnToggleNumbers(Object^, EventArgs^) {
        numbers_ = !numbers_;
        numbersItem_->Checked = numbers_;
        for each (Sheet^ sheet in sheets_)
            if (sheet->gutter != nullptr) sheet->gutter->Visible = numbers_;
        what_->Text = numbers_ ? "line numbers on" : "line numbers off";
    }

    void OnTogglePane(Object^, EventArgs^) {
        bool hidden = !upper_->Panel1Collapsed;
        upper_->Panel1Collapsed = hidden;
        paneItem_->Checked = !hidden;
        what_->Text = hidden ? "project pane hidden" : "project pane showing";
    }

    void OnTogglePanel(Object^, EventArgs^) {
        bool hidden = !outer_->Panel2Collapsed;
        outer_->Panel2Collapsed = hidden;
        panelItem_->Checked = !hidden;
        what_->Text = hidden ? "bottom panel hidden" : "bottom panel showing";
    }

    void OnNewBuffer(Object^, EventArgs^) {

        MakeSheet(nullptr, "");
        what_->Text = "new file - Ctrl+S names it";
    }

    // Ctrl+PageDown and Ctrl+PageUp, caught in ProcessCmdKey: off the menu, as the File menu says.
    void StepFile(int by) {
        int count = files_->TabPages->Count;
        if (count < 2) { what_->Text = "only one file is open"; return; }
        int at = (files_->SelectedIndex + count + by) % count;
        files_->SelectedTab = files_->TabPages[at];
    }

    void OnRenameFile(Object^, EventArgs^) {
        String^ target = TargetFile();
        if (target == nullptr) { what_->Text = "no file to rename"; return; }

        array<Byte>^ was = Utf8Of(target);
        pin_ptr<Byte> wasPin = &was[0];
        // Offered by its name in the project, or by its bare name when it
        // is not in one - which is what the new name is taken relative to.
        String^ shown = ride_project_holds(project_, reinterpret_cast<const char*>(wasPin)) != 0
                            ? FromUtf8(ride_project_relative(project_, reinterpret_cast<const char*>(wasPin)))
                            : System::IO::Path::GetFileName(target);

        String^ name = Ask("Rename " + shown + " to", shown);
        if (name == nullptr || name->Length == 0) { what_->Text = "not renamed"; return; }

        array<Byte>^ from = Utf8Of(target);
        pin_ptr<Byte> fromPin = &from[0];
        array<Byte>^ to = Utf8Of(name);
        pin_ptr<Byte> toPin = &to[0];

        if (!Did(ride_rename_file(project_, reinterpret_cast<const char*>(fromPin),
                                 reinterpret_cast<const char*>(toPin))))
            return;

        String^ now = OutcomePath();
        for (int i = 0; i < sheets_->Count; ++i) {
            if (sheets_[i]->path == nullptr) continue;
            if (!SamePath(sheets_[i]->path, target)) continue;
            sheets_[i]->path = now;
            MarkTab(sheets_[i]);
            PaneFollowsTabs();
        }
        if (SamePath(path_, target)) {
            path_ = now;
            RefreshTitle();
            SayBuild();
        }

        String^ wasKey = OneName(target);
        System::Collections::Generic::List<int>^ hadBreaks = nullptr;
        if (breaks_->TryGetValue(wasKey, hadBreaks)) {
            breaks_->Remove(wasKey);
            breakNames_->Remove(wasKey);
            String^ nowKey = OneName(now);
            breaks_[nowKey] = hadBreaks;
            breakNames_[nowKey] = now;
            Sheet^ showing = Current();
            if (showing != nullptr && showing->gutter != nullptr)
                showing->gutter->Invalidate();
        }

        FillTree();
    }

    void OnDeleteFile(Object^, EventArgs^) {
        String^ target = TargetFile();
        if (target == nullptr) { what_->Text = "no file to delete"; return; }

        System::Windows::Forms::DialogResult answer = MessageBox::Show(
            this, "Delete " + System::IO::Path::GetFileName(target) + " from disk?",
            "Delete", MessageBoxButtons::YesNo, MessageBoxIcon::Warning,
            MessageBoxDefaultButton::Button2);
        if (answer != System::Windows::Forms::DialogResult::Yes) {
            what_->Text = "not deleted";
            return;
        }

        array<Byte>^ path = Utf8Of(target);
        pin_ptr<Byte> pathPin = &path[0];
        if (!Did(ride_delete_file(project_, reinterpret_cast<const char*>(pathPin)))) return;

        String^ said = what_->Text;
        for (int i = sheets_->Count - 1; i >= 0; --i) {
            if (sheets_[i]->path == nullptr) continue;
            if (!SamePath(sheets_[i]->path, target)) continue;
            DropSheet(sheets_[i]);
        }

        String^ key = OneName(target);
        breaks_->Remove(key);
        breakNames_->Remove(key);

        // The last tab gone leaves the empty environment CloseSheet leaves, not a spare untitled one.
        AfterSheetsGone();
        what_->Text = said;
    }

    void OnMoveToGroup(Object^, EventArgs^) {
        String^ target = TargetFile();
        if (target == nullptr) { what_->Text = "no file to move"; return; }

        String^ group = Ask("Move to group", GroupUnderCursor());
        if (group == nullptr || group->Length == 0) { what_->Text = "not moved"; return; }

        array<Byte>^ path = Utf8Of(target);
        pin_ptr<Byte> pathPin = &path[0];
        array<Byte>^ into = Utf8Of(group);
        pin_ptr<Byte> intoPin = &into[0];

        if (Did(ride_move_to_group(project_, reinterpret_cast<const char*>(pathPin),
                                  reinterpret_cast<const char*>(intoPin))))
            FillTree();
    }

    void OnAddThisFile(Object^, EventArgs^) {
        if (path_ == nullptr) {
            what_->Text = "save the file first, so it has a name";
            return;
        }

        String^ wanted = GroupForFile(path_);
        if (wanted->Length == 0) wanted = "Sources";

        String^ group = Ask("Add to group", wanted);
        if (group == nullptr || group->Length == 0) { what_->Text = "not added"; return; }

        array<Byte>^ path = Utf8Of(path_);
        pin_ptr<Byte> pathPin = &path[0];
        array<Byte>^ into = Utf8Of(group);
        pin_ptr<Byte> intoPin = &into[0];

        if (Did(ride_add_existing(project_, reinterpret_cast<const char*>(pathPin),
                                 reinterpret_cast<const char*>(intoPin))))
            FillTree();
    }

    // Set once Start() has opened what the command line and the project asked for; LoadProject opens the project's file only after that.
    bool started_ = false;

    // The file the menu was enabled for - the tree's selection, else the one in front - as Rename
    // and Delete do, and as the macOS window removes.
    void OnRemoveFromProject(Object^, EventArgs^) {
        String^ target = TargetFile();
        if (target == nullptr) {
            what_->Text = "this buffer has no name to look for";
            return;
        }

        array<Byte>^ path = Utf8Of(target);
        pin_ptr<Byte> pathPin = &path[0];

        if (Did(ride_remove_from_project(project_, reinterpret_cast<const char*>(pathPin))))
            FillTree();
    }

    // The installation's include directories and libraries, in settings.json,
    // edited as one line each with ';' between the entries; every compile
    // searches them after a project's own.
    void OnSharedIncludes(Object^, EventArgs^) {
        String^ file = FromUtf8(ride_install_file());
        if (file->Length == 0) { what_->Text = "no installation directory to keep this in"; return; }
        String^ line = Ask("Shared include paths", "kept in " + file + ", ';' between them",
                           FromUtf8(ride_includes()));
        if (line == nullptr) { what_->Text = "shared include paths unchanged"; return; }
        array<Byte>^ bytes = Utf8Of(line);
        pin_ptr<Byte> pinned = &bytes[0];
        what_->Text = ride_set_includes(reinterpret_cast<const char*>(pinned)) != 0
                          ? "shared include paths: " + FromUtf8(ride_includes()) + " - written to " + file
                          : "cannot write " + file;
    }

    void OnSharedLibraries(Object^, EventArgs^) {
        String^ file = FromUtf8(ride_install_file());
        if (file->Length == 0) { what_->Text = "no installation directory to keep this in"; return; }
        String^ line = Ask("Shared libraries", "kept in " + file + ", ';' between them, linked after the objects",
                           FromUtf8(ride_libraries()));
        if (line == nullptr) { what_->Text = "shared libraries unchanged"; return; }
        array<Byte>^ bytes = Utf8Of(line);
        pin_ptr<Byte> pinned = &bytes[0];
        what_->Text = ride_set_libraries(reinterpret_cast<const char*>(pinned)) != 0
                          ? "shared libraries: " + FromUtf8(ride_libraries()) + " - written to " + file
                          : "cannot write " + file;
    }

    // The open project's own, in its .pro, relative to its root - searched
    // and linked before the installation's. The bridge calls were there
    // from the start; the audit of 2026-09-19 found nothing calling them.
    // Tools > Compiler options: the tabbed dialog, opened on the configuration the window builds with
    // and on the tab of the compiler that builds the file in front; what OK keeps, the next build uses.
    void OnCompilerOptions(Object^, EventArgs^) {
        if (busy_) { what_->Text = StillWorking(); return; }
        int kind = ride_resolve(toolKind_, LanguageNow());
        int tab = kind == RIDE_TOOL_CC1 ? 1 : kind == RIDE_TOOL_CXX1 ? 2 : kind == RIDE_TOOL_SHC ? 3 : 0;
        msclr::auto_handle<OptionsDialog> box(gcnew OptionsDialog(project_, config_, arch_, tab));
        if (box->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) {
            what_->Text = "compiler options unchanged";
            return;
        }
        bool inProject = project_ != nullptr && ride_project_loaded(project_) != 0;
        if (box->Commit())
            what_->Text = inProject ? "compiler options written to the project" : "compiler options written to settings.json";
        else if (ride_project_is_ccs(project_) != 0)
            what_->Text = "a CCS project's options are read from it - edit them in CCS";
        else
            what_->Text = "compiler options were not written - settings.json may be unreadable, or the project read-only";
    }

    void OnProjectIncludes(Object^, EventArgs^) {
        if (project_ == nullptr || ride_project_loaded(project_) == 0) {
            what_->Text = "there is no project open - these are a project's own; Shared include paths... is the installation's";
            return;
        }
        String^ line = Ask("Project include paths", "kept in the project's .pro, relative to it, ';' between them",
                           FromUtf8(ride_project_includes(project_)));
        if (line == nullptr) { what_->Text = "the project's include paths are unchanged"; return; }
        array<Byte>^ bytes = Utf8Of(line);
        pin_ptr<Byte> pinned = &bytes[0];
        if (Did(ride_project_set_includes(project_, reinterpret_cast<const char*>(pinned))))
            what_->Text = "the project's include paths: " + FromUtf8(ride_project_includes(project_)) + " - written";
    }

    void OnProjectLibraries(Object^, EventArgs^) {
        if (project_ == nullptr || ride_project_loaded(project_) == 0) {
            what_->Text = "there is no project open - these are a project's own; Shared libraries... is the installation's";
            return;
        }
        String^ line = Ask("Project libraries", "kept in the project's .pro, relative to it, ';' between them, linked before the shared ones",
                           FromUtf8(ride_project_libraries(project_)));
        if (line == nullptr) { what_->Text = "the project's libraries are unchanged"; return; }
        array<Byte>^ bytes = Utf8Of(line);
        pin_ptr<Byte> pinned = &bytes[0];
        if (Did(ride_project_set_libraries(project_, reinterpret_cast<const char*>(pinned))))
            what_->Text = "the project's libraries: " + FromUtf8(ride_project_libraries(project_)) + " - written";
    }

    // The installation's settings.json: where the shipped headers are.
    void OnHeaderDirs(Object^, EventArgs^) {
        String^ file = FromUtf8(ride_install_file());
        if (file->Length == 0) { what_->Text = "no installation directory to keep this in"; return; }
        String^ include = Ask("cpp11's headers (include)", "kept in " + file,
                              FromUtf8(ride_include_dir()));
        if (include == nullptr) { what_->Text = "header directories unchanged"; return; }
        String^ lib = Ask("c90's headers (lib)", "kept in " + file, FromUtf8(ride_lib_dir()));
        if (lib == nullptr) { what_->Text = "header directories unchanged"; return; }
        array<Byte>^ a = Utf8Of(include);
        pin_ptr<Byte> aPin = &a[0];
        array<Byte>^ b = Utf8Of(lib);
        pin_ptr<Byte> bPin = &b[0];
        if (ride_remember_header_dirs(reinterpret_cast<const char*>(aPin),
                                         reinterpret_cast<const char*>(bPin)) == 0) {
            what_->Text = "cannot write " + file;
            return;
        }
        what_->Text = "include " + FromUtf8(ride_include_dir()) + ", lib " +
                      FromUtf8(ride_lib_dir()) + " - written to " + file;
    }

    // Visual Studio's tools, when the editor's own search did not find them.
    void OnLocateVcvars(Object^, EventArgs^) {
        String^ file = FromUtf8(ride_install_file());
        if (file->Length == 0) { what_->Text = "no installation directory to keep this in"; return; }
        msclr::auto_handle<OpenFileDialog> pick(gcnew OpenFileDialog());
        pick->Title = "Locate vcvars64.bat (Visual Studio\\VC\\Auxiliary\\Build)";
        pick->Filter = "vcvars64.bat|vcvars64.bat|Batch files (*.bat)|*.bat";
        String^ now = FromUtf8(ride_vcvars());
        if (now->Length > 0) pick->InitialDirectory = System::IO::Path::GetDirectoryName(now);
        if (pick->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) {
            what_->Text = "vcvars unchanged";
            return;
        }
        array<Byte>^ bytes = Utf8Of(pick->FileName);
        pin_ptr<Byte> pinned = &bytes[0];
        if (ride_remember_vcvars(reinterpret_cast<const char*>(pinned)) == 0) {
            what_->Text = "cannot write " + file;
            return;
        }
        what_->Text = "Visual Studio's tools come from " + pick->FileName + " - written to " + file;
    }

    // The project's own assembler in place of ml64 and clang - Cancel with
    // one named keeps it; the console front end's `-` clears it.
    void OnLocateAssembler(Object^, EventArgs^) {
        String^ file = FromUtf8(ride_install_file());
        if (file->Length == 0) { what_->Text = "no installation directory to keep this in"; return; }
        msclr::auto_handle<OpenFileDialog> pick(gcnew OpenFileDialog());
        pick->Title = "The assembler for x86_64-windows (asm.exe)";
        pick->Filter = "Programs (*.exe)|*.exe";
        String^ now = FromUtf8(ride_assembler());
        if (now->Length > 0) pick->InitialDirectory = System::IO::Path::GetDirectoryName(now);
        if (pick->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) {
            what_->Text = "assembler unchanged";
            return;
        }
        array<Byte>^ bytes = Utf8Of(pick->FileName);
        pin_ptr<Byte> pinned = &bytes[0];
        if (ride_remember_assembler(reinterpret_cast<const char*>(pinned)) == 0) {
            what_->Text = "cannot write " + file;
            return;
        }
        what_->Text = "c90 and cpp11 assemble through " + pick->FileName + " - written to " + file;
    }

    // The project's own linkers - LINK's for x86_64-windows in place of link.exe, LNK6X's for
    // tms6747 in place of TI's lnk6x - each named by its path; Cancel keeps what is named, and the
    // console front end's `-` clears it. One picker serves both, told which setting it is for.
    void PickLinker(bool ti) {
        String^ file = FromUtf8(ride_install_file());
        if (file->Length == 0) { what_->Text = "no installation directory to keep this in"; return; }
        msclr::auto_handle<OpenFileDialog> pick(gcnew OpenFileDialog());
        pick->Title = ti ? "The linker for tms6747 (lnk6x.exe)" : "The linker for x86_64-windows (link.exe)";
        pick->Filter = "Programs (*.exe)|*.exe";
        String^ now = FromUtf8(ti ? ride_tilinker() : ride_linker());
        if (now->Length > 0) pick->InitialDirectory = System::IO::Path::GetDirectoryName(now);
        if (pick->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) {
            what_->Text = "linker unchanged";
            return;
        }
        array<Byte>^ bytes = Utf8Of(pick->FileName);
        pin_ptr<Byte> pinned = &bytes[0];
        int written = ti ? ride_remember_tilinker(reinterpret_cast<const char*>(pinned))
                         : ride_remember_linker(reinterpret_cast<const char*>(pinned));
        if (written == 0) {
            what_->Text = "cannot write " + file;
            return;
        }
        what_->Text = (ti ? "a tms6747 build links through " : "a Windows build links through ") + pick->FileName + " - written to " + file;
    }
    void OnLocateLinker(Object^, EventArgs^) { PickLinker(false); }
    void OnLocateTilinker(Object^, EventArgs^) { PickLinker(true); }

    // TI's C6000 compiler directory (CCS's ti-cgt-c6000_x.y.z), whose lnk6x links what asm6x made
    // of a tms6747 build into a .out - and a second directory, asked for next, for the exception-
    // handling runtime CCS does not ship (Cancel there keeps none). Clear the setting by hand in settings.json; the console's `-` does it there.
    void OnLocateTi(Object^, EventArgs^) {
        String^ file = FromUtf8(ride_install_file());
        if (file->Length == 0) { what_->Text = "no installation directory to keep this in"; return; }
        msclr::auto_handle<FolderBrowserDialog> pick(gcnew FolderBrowserDialog());
        pick->Description = "TI's C6000 compiler directory - the one with bin\\lnk6x.exe";
        String^ now = FromUtf8(ride_ti());
        if (now->Length > 0) pick->SelectedPath = now;
        if (pick->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) {
            what_->Text = "TI compiler unchanged";
            return;
        }
        String^ dir = pick->SelectedPath;
        msclr::auto_handle<FolderBrowserDialog> lib(gcnew FolderBrowserDialog());
        lib->Description = "A directory with rts6740_elf_eh.lib, the exception-handling runtime (Cancel for none)";
        String^ nowLib = FromUtf8(ride_tilib());
        if (nowLib->Length > 0) lib->SelectedPath = nowLib;
        String^ libDir = lib->ShowDialog(this) == System::Windows::Forms::DialogResult::OK ? lib->SelectedPath : "";
        array<Byte>^ bytes = Utf8Of(dir);
        array<Byte>^ libBytes = Utf8Of(libDir);
        pin_ptr<Byte> pinned = &bytes[0];
        pin_ptr<Byte> pinnedLib = &libBytes[0];
        if (ride_remember_ti(reinterpret_cast<const char*>(pinned), reinterpret_cast<const char*>(pinnedLib)) == 0) {
            what_->Text = "cannot write " + file;
            return;
        }
        what_->Text = "a tms6747 build links a .out with " + dir + " - written to " + file;
    }

    // The last three projects remembered, named at the end of the Project menu; one whose project is gone is not shown.
    System::Collections::Generic::List<ToolStripMenuItem^>^ recentItems_;
    ToolStripMenuItem^ recentMenu_;
    // The Project menu's items that want a project open, or one of its files, to mean anything.
    ToolStripMenuItem^ projSaveAs_;
    ToolStripMenuItem^ projClose_;
    ToolStripMenuItem^ projNewFile_;
    ToolStripMenuItem^ projAddFile_;
    ToolStripMenuItem^ projRemove_;
    ToolStripMenuItem^ projRename_;
    ToolStripMenuItem^ projDelete_;
    ToolStripMenuItem^ projIncludes_;
    ToolStripMenuItem^ projLibraries_;

    void OnProjectMenuOpening(Object^, EventArgs^) { RefreshProjectMenu(); }

    // File or project, as the file in front decides: one of the open project's files, or no file but a
    // project, is the project's; a loose file - File > New, Open or Recent - or no project at all is the file's.
    ToolStripMenuItem^ compileItem_;
    ToolStripMenuItem^ runItem_;
    ToolStripMenuItem^ buildProjectItem_;
    ToolStripMenuItem^ runProjectItem_;
    ToolStripMenuItem^ emulateItem_;
    ToolStripMenuItem^ verifyItem_;
    ToolStripMenuItem^ debugItem_;
    ToolStripMenuItem^ debugProjectItem_;

    bool ProjectMode() {
        if (project_ == nullptr || ride_project_loaded(project_) == 0) return false;
        Sheet^ sheet = Current();
        if (text_ == nullptr || sheet == nullptr) return true;
        // The blank sheet a window starts with is no file: a project opened beside it is in front.
        if (path_ == nullptr || path_->Length == 0)
            return sheets_->Count == 1 && text_->TextLength == 0 && !text_->Modified;
        Utf8 here(path_);
        return ride_project_holds(project_, here.c()) != 0;
    }

    void OnModeMenuOpening(Object^, EventArgs^) { RefreshModeItems(); }

    // The menus and their keys alike: a debugger already running continues whichever started it.
    void RefreshModeItems() {
        if (compileItem_ == nullptr) return;
        bool mode = ProjectMode();
        bool idle = !busy_;
        bool debugging = debugger_ != nullptr && ride_debugger_running(debugger_) != 0;
        compileItem_->Enabled = idle && !mode;
        runItem_->Enabled = idle && !mode;
        buildProjectItem_->Enabled = idle && mode;
        runProjectItem_->Enabled = idle && mode;
        bool c6747 = arch_ != nullptr && String::Equals(arch_, "tms6747", StringComparison::Ordinal);
        emulateItem_->Enabled = idle && c6747;
        verifyItem_->Enabled = idle && c6747;
        debugItem_->Enabled = idle && (!mode || debugging);
        debugProjectItem_->Enabled = idle && (mode || debugging);
    }

    // What a key that reached a command out of its mode is told, instead of a build of the wrong thing.
    bool InFileMode() {
        if (!ProjectMode()) return true;
        String^ owner = FromUtf8(ride_project_name(project_));
        what_->Text = String::Concat("this is one of ", owner, "'s files - Build project (F4) or Run project builds it");
        return false;
    }

    bool InProjectMode() {
        if (ProjectMode()) return true;
        bool project = project_ != nullptr && ride_project_loaded(project_) != 0;
        if (!project) {
            what_->Text = "no project is open - Compile (Ctrl+B) or Run (F5) builds this file";
            return false;
        }
        String^ name = "this file";
        if (path_ != nullptr) name = System::IO::Path::GetFileName(path_);
        what_->Text = name + " is not in " + FromUtf8(ride_project_name(project_)) +
                      " - Compile or Run builds it on its own, Project > Add File puts it in";
        return false;
    }

    // As the macOS window decides it: a file item needs a project, the last three a file of it; a CCS
    // project's own files are CCS's, so what would change them is offered disabled; nothing while a build runs.
    void RefreshProjectMenu() {
        if (projNewFile_ == nullptr || tree_ == nullptr || project_ == nullptr) return;
        bool project = ride_project_loaded(project_) != 0;
        bool ccs = project && ride_project_is_ccs(project_) != 0;
        bool idle = !busy_;
        String^ target = TargetFile();
        bool held = false;
        if (project && target != nullptr && target->Length > 0) {
            Utf8 t(target);
            held = ride_project_holds(project_, t.c()) != 0;
        }
        bool fresh = false;
        if (project && path_ != nullptr && path_->Length > 0) {
            Utf8 here(path_);
            fresh = ride_project_holds(project_, here.c()) == 0;
        }
        projSaveAs_->Enabled = project && !ccs && idle;
        projClose_->Enabled = project && idle;
        // A loose file in front is the File menu's: of the project's file items only Add File, which takes it in.
        bool mode = ProjectMode();
        projNewFile_->Enabled = mode && !ccs && idle;
        projAddFile_->Enabled = project && !ccs && idle && fresh;
        projRemove_->Enabled = mode && held && !ccs && idle;
        projRename_->Enabled = mode && held && idle;
        projDelete_->Enabled = mode && held && idle;
        projIncludes_->Enabled = project && !ccs && idle;
        projLibraries_->Enabled = project && !ccs && idle;
        bool any = false;
        // Available, not Visible: an item of a submenu never shown reads not visible whatever it was set to.
        for each (ToolStripMenuItem^ one in recentItems_) any = any || one->Available;
        recentMenu_->Enabled = any && idle;
    }

    void RefreshRecent() {
        if (recentItems_ == nullptr) return;
        for (int i = 0; i < recentItems_->Count; ++i) {
            String^ where = FromUtf8(ride_recent_project(i));
            ToolStripMenuItem^ item = recentItems_[i];
            if (where->Length == 0) { item->Visible = false; continue; }
            String^ shown = System::IO::File::Exists(where)
                                ? System::IO::Path::GetFileNameWithoutExtension(where)
                                : System::IO::Path::GetFileName(where->TrimEnd('/', '\\'));
            // Never a blank entry: a name that is all extension is shown by its folder.
            if (shown->Length == 0)
                shown = System::IO::Path::GetFileName(System::IO::Path::GetDirectoryName(where));
            item->Text = String::Format("&{0}. {1}", i + 1, shown);
            item->ToolTipText = where;
            item->Visible = true;
        }
        RefreshRecentFiles();
    }

    System::Collections::Generic::List<ToolStripMenuItem^>^ recentFileItems_;
    ToolStripMenuItem^ recentFilesMenu_;
    ToolStripMenuItem^ openFileItem_;
    ToolStripMenuItem^ saveAsItem_;

    // A program running, its build over: what it holds is its own process, not the core, so a
    // file may be opened, picked from Recent or saved under a new name, as on macOS.
    bool ProgramOnly() { return busy_ && running_ != nullptr && job_ == nullptr && heardProgram_; }
    void FreeForProgram() {
        if (!ProgramOnly()) return;
        if (openFileItem_ != nullptr) openFileItem_->Enabled = true;
        if (saveAsItem_ != nullptr) saveAsItem_->Enabled = true;
        for each (ToolStripMenuItem^ one in recentFileItems_) one->Enabled = true;
    }

    // The name and the two folders above it, so two main.c read apart, as the macOS menu shows them.
    static String^ RecentLabel(String^ where) {
        String^ name = System::IO::Path::GetFileName(where);
        String^ folder = System::IO::Path::GetDirectoryName(where);
        if (String::IsNullOrEmpty(folder)) return name;
        String^ last = System::IO::Path::GetFileName(folder);
        String^ above = System::IO::Path::GetDirectoryName(folder);
        String^ before = String::IsNullOrEmpty(above) ? nullptr : System::IO::Path::GetFileName(above);
        String^ shown = String::IsNullOrEmpty(before) ? last : before + "\\" + last;
        return String::IsNullOrEmpty(shown) ? name : name + "  -  " + shown;
    }

    void RefreshRecentFiles() {
        if (recentFileItems_ == nullptr) return;
        bool any = false;
        for (int i = 0; i < recentFileItems_->Count; ++i) {
            String^ where = FromUtf8(ride_recent_file(i));
            ToolStripMenuItem^ item = recentFileItems_[i];
            if (where->Length == 0) { item->Visible = false; continue; }
            item->Text = String::Format("&{0}. {1}", i + 1, RecentLabel(where));
            item->ToolTipText = where;
            item->Visible = true;
            any = true;
        }
        if (recentFilesMenu_ != nullptr) recentFilesMenu_->Enabled = any;
    }

    void OnOpenRecentFile(Object^ sender, EventArgs^) {
        ToolStripMenuItem^ item = safe_cast<ToolStripMenuItem^>(sender);
        String^ where = FromUtf8(ride_recent_file(safe_cast<int>(item->Tag)));
        if (where->Length == 0) { what_->Text = "no file remembered"; return; }
        OpenPath(where);
    }

    void OnOpenRecent(Object^ sender, EventArgs^) {
        ToolStripMenuItem^ item = safe_cast<ToolStripMenuItem^>(sender);
        String^ where = FromUtf8(ride_recent_project(safe_cast<int>(item->Tag)));
        if (where->Length == 0) { what_->Text = "no project remembered"; return; }
        LoadProject(where);
    }

    void OnNewProject(Object^, EventArgs^) {
        msclr::auto_handle<FolderBrowserDialog> pick(gcnew FolderBrowserDialog());
        pick->Description = "Where to put the project";
        pick->ShowNewFolderButton = true;
        pick->SelectedPath = ProjectsDir();
        if (pick->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) {
            what_->Text = "no project made";
            return;
        }

        String^ name = Ask("Project name", "It will be made in " + pick->SelectedPath,
                           "Project");
        if (name == nullptr || name->Length == 0) {
            what_->Text = "no project made";
            return;
        }

        array<Byte>^ where = Utf8Of(pick->SelectedPath);
        pin_ptr<Byte> wherePin = &where[0];
        array<Byte>^ called = Utf8Of(name);
        pin_ptr<Byte> calledPin = &called[0];
        array<Byte>^ first = Utf8Of(path_ == nullptr ? "" : path_);
        pin_ptr<Byte> firstPin = &first[0];

        if (Did(ride_begin_project(project_, reinterpret_cast<const char*>(wherePin),
                                  reinterpret_cast<const char*>(calledPin),
                                  reinterpret_cast<const char*>(firstPin)))) {
            projectDirectory_ = pick->SelectedPath;
            FillTree();
            TakeProjectSettings();
            SayWhere();
            RefreshTitle();
        }
    }

    void OnOpenProjectFile(Object^, EventArgs^) {
        String^ suffix = FromUtf8(ride_project_suffix());

        msclr::auto_handle<OpenFileDialog> pick(gcnew OpenFileDialog());
        pick->Title = "Open project";
        pick->Filter = "Projects (*" + suffix + ", CCS .project)|*" + suffix + ";.project;.cproject;.ccsproject"
                       "|All files (*.*)|*.*";
        pick->InitialDirectory = ProjectsDir();
        if (pick->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) {
            what_->Text = "no project opened";
            return;
        }
        String^ chosen = pick->FileName;
        String^ leaf = System::IO::Path::GetFileName(chosen);
        String^ folder = System::IO::Path::GetDirectoryName(chosen);
        // A CCS project's three files stand for its folder; any other file of a CCS workspace for the
        // workspace, which asks which of its projects. A .pro is always itself.
        if (leaf == ".project" || leaf == ".cproject" || leaf == ".ccsproject") {
            LoadProject(folder);
            return;
        }
        if (!chosen->EndsWith(suffix, StringComparison::OrdinalIgnoreCase)) {
            Utf8 dir(folder);
            if (ride_ccs_is_workspace(dir.c()) != 0) {
                LoadProject(folder);
                return;
            }
        }
        LoadProject(chosen);
    }

    void OnSaveProjectAs(Object^, EventArgs^) {
        if (ride_project_loaded(project_) == 0) {
            what_->Text = "there is no project to save";
            return;
        }

        String^ suffix = FromUtf8(ride_project_suffix());
        String^ offered = FromUtf8(ride_project_name(project_)) + suffix;

        msclr::auto_handle<SaveFileDialog> pick(gcnew SaveFileDialog());
        pick->Title = "Save as project file";
        pick->FileName = offered;
        pick->Filter = ProductName() + " projects (*" + suffix + ")|*" + suffix;
        pick->InitialDirectory = ProjectsDir();
        if (pick->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) {
            what_->Text = "not saved";
            return;
        }

        array<Byte>^ where = Utf8Of(pick->FileName);
        pin_ptr<Byte> wherePin = &where[0];
        array<Byte>^ why = gcnew array<Byte>(512);
        pin_ptr<Byte> whyPin = &why[0];

        if (ride_project_save_as(project_, reinterpret_cast<const char*>(wherePin),
                                    reinterpret_cast<char*>(whyPin), why->Length) == 0) {
            what_->Text = FromUtf8(reinterpret_cast<const char*>(whyPin));
            return;
        }

        projectDirectory_ = System::IO::Path::GetDirectoryName(pick->FileName);
        FillTree();
        what_->Text = System::IO::Path::GetFileName(pick->FileName) +
                      " written - the project is saved there from now on";
    }

    void OnCloseProject(Object^, EventArgs^) {
        if (ride_project_loaded(project_) == 0) {
            what_->Text = "there is no project open";
            return;
        }
        String^ was = FromUtf8(ride_project_name(project_));
        RememberOpen();

        // The project's files go with it - every file it lists that is open; one merely under its
        // directory stays - each unsaved one asking first, and one refusal keeps the project open
        // with everything as it was.
        System::Collections::Generic::List<Sheet^>^ theirs = gcnew System::Collections::Generic::List<Sheet^>();
        for (int i = 0; i < sheets_->Count; ++i) {
            if (sheets_[i]->path == nullptr) continue;
            array<Byte>^ bytes = Utf8Of(sheets_[i]->path);
            pin_ptr<Byte> pinned = &bytes[0];
            if (ride_project_holds(project_, reinterpret_cast<const char*>(pinned)) != 0) theirs->Add(sheets_[i]);
        }
        for (int i = 0; i < theirs->Count; ++i)
            if (!MayDiscard(theirs[i])) { what_->Text = "not closed - " + System::IO::Path::GetFileName(theirs[i]->path) + " has unsaved changes"; return; }
        int closed = theirs->Count;
        // A program of the project's under the debugger goes with it, and so do the breakpoints
        // in its files, the last error and the project's own compiler, target and layout.
        if (ride_debugger_running(debugger_) != 0) EndDebugging();
        for (int i = 0; i < theirs->Count; ++i) {
            String^ key = OneName(theirs[i]->path);
            breaks_->Remove(key);
            breakNames_->Remove(key);
            DropSheet(theirs[i]);
        }
        ForgetError();

        ride_project_close(project_);
        projectDirectory_ = nullptr;
        TakeInstallationSettings();

        AfterSheetsGone();
        // All three panes were the project's: the Console alone was cleared, and the Debug and
        // Assembly tabs kept the closed project's build. Assembly first - the Debug tab is
        // rebuilt from it.
        assembly_->Text = "";
        debugView_->Clear();
        console_->Text = "";
        what_->Text = was + " closed" + (closed > 0 ? String::Format(", and its {0} file(s) with it", closed) : "");
    }

    // What every way of removing tabs ends with: the one in front made current, or - none left -
    // the genuinely empty environment, with no untitled buffer and no entry in the pane.
    void AfterSheetsGone() {
        if (sheets_->Count == 0) {
            text_ = nullptr;
            path_ = nullptr;
            ForgetError();
            what_->Text = "no file open";
        } else {
            OnSheetChanged(nullptr, nullptr);
        }
        RefreshTitle();
        FillTree();
        SayBuild();
        SayWhere();
    }


    System::Windows::Forms::ContextMenuStrip^ treeMenu_;
    ToolStripMenuItem^ treeOpen_;
    ToolStripMenuItem^ treeNewFile_;
    ToolStripMenuItem^ treeRename_;
    ToolStripMenuItem^ treeMove_;
    ToolStripMenuItem^ treeRemove_;
    ToolStripMenuItem^ treeDelete_;
    ToolStripMenuItem^ treeShow_;

    // As the Project menu decides them, for the row under the right click.
    void OnTreeMenuOpening(Object^, System::ComponentModel::CancelEventArgs^ e) {
        bool project = ride_project_loaded(project_) != 0;
        bool ccs = project && ride_project_is_ccs(project_) != 0;
        bool idle = !busy_;
        // The row under the pointer, selected before anything is decided: the menu may open before the click is seen.
        TreeNode^ under = tree_->GetNodeAt(tree_->PointToClient(Control::MousePosition));
        if (under != nullptr) tree_->SelectedNode = under;
        TreeNode^ node = tree_->SelectedNode;
        String^ file = node != nullptr && node->Tag != nullptr ? safe_cast<String^>(node->Tag) : nullptr;
        bool held = false;
        if (project && file != nullptr) {
            Utf8 f(file);
            held = ride_project_holds(project_, f.c()) != 0;
        }
        if (!project && file == nullptr) { e->Cancel = true; return; }
        treeOpen_->Enabled = file != nullptr;
        treeNewFile_->Enabled = project && !ccs && idle;
        treeRename_->Enabled = held && idle;
        treeMove_->Enabled = held && !ccs && idle;
        treeRemove_->Enabled = held && !ccs && idle;
        treeDelete_->Enabled = held && idle;
        treeShow_->Enabled = file != nullptr || project;
    }

    void OnTreeMenuOpen(Object^, EventArgs^) {
        TreeNode^ node = tree_->SelectedNode;
        if (node != nullptr && node->Tag != nullptr) OpenPath(safe_cast<String^>(node->Tag));
    }

    void OnShowInExplorer(Object^, EventArgs^) {
        TreeNode^ node = tree_->SelectedNode;
        String^ file = node != nullptr && node->Tag != nullptr ? safe_cast<String^>(node->Tag) : nullptr;
        try {
            if (file != nullptr && System::IO::File::Exists(file))
                System::Diagnostics::Process::Start("explorer.exe", "/select,\"" + file + "\"");
            else {
                String^ root = RootNow();
                if (root != nullptr && System::IO::Directory::Exists(root))
                    System::Diagnostics::Process::Start("explorer.exe", "\"" + root + "\"");
                else what_->Text = "nothing on disk to show";
            }
        } catch (Exception^ problem) {
            what_->Text = problem->Message;
        }
    }

    void OnTreeClick(Object^, TreeNodeMouseClickEventArgs^ e) {
        // A right click selects its row first, so the menu acts on what was clicked.
        if (e->Button == System::Windows::Forms::MouseButtons::Right) { tree_->SelectedNode = e->Node; return; }
        if (e->Button != System::Windows::Forms::MouseButtons::Left) return;
        if (e->Node == nullptr || e->Node->Tag == nullptr) return;
        OpenPath(safe_cast<String^>(e->Node->Tag));
    }

    void OnTreeOpen(Object^, TreeNodeMouseClickEventArgs^ e) {
        if (e->Node == nullptr || e->Node->Tag == nullptr) return;
        OpenPath(safe_cast<String^>(e->Node->Tag));
    }

    void OnFocusTree(Object^, EventArgs^) {
        if (tree_->Nodes->Count == 0) { what_->Text = "no project is open"; return; }
        if (tree_->SelectedNode == nullptr) tree_->SelectedNode = tree_->Nodes[0];
        tree_->Focus();
        what_->Text = "the project pane - enter opens, Ctrl-4 goes back to the file";
    }

    void OnFocusText(Object^, EventArgs^) {
        if (text_ != nullptr) text_->Focus();
    }

    void OnTreeKey(Object^, KeyEventArgs^ e) {
        if (e->KeyCode != Keys::Return) return;
        TreeNode^ node = tree_->SelectedNode;
        if (node == nullptr) return;

        e->Handled = true;
        e->SuppressKeyPress = true;

        if (node->Tag == nullptr) {
            if (node->IsExpanded) node->Collapse();
            else node->Expand();
            return;
        }
        OpenPath(safe_cast<String^>(node->Tag));
    }

    void OnOpenFile(Object^, EventArgs^) {

        msclr::auto_handle<OpenFileDialog> pick(gcnew OpenFileDialog());
        pick->Filter = "Sources|*.c;*.h;*.cpp;*.hpp;*.cc;*.cxx;*.shl;*.s;*.json"
                       "|C and C++|*.c;*.h;*.cpp;*.hpp;*.cc;*.cxx|Shalimar|*.shl|All files|*.*";
        pick->InitialDirectory = ProgramsDir();
        if (pick->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) {
            what_->Text = "not opened";
            return;
        }
        OpenPath(pick->FileName);
    }

    void OpenPath(String^ path) {

        Sheet^ already = SheetFor(path);
        if (already != nullptr) {
            if (files_->SelectedTab == already->page) ReloadIfChanged(already);
            else files_->SelectedTab = already->page;
            RememberFile(path);
            return;
        }

        String^ contents;
        TextFile^ file = nullptr;
        try {
            String^ why = nullptr;
            contents = TextFile::Read(path, file, why);
            if (contents == nullptr) { what_->Text = why; return; }
        } catch (Exception^ problem) {
            what_->Text = problem->Message;
            return;
        }

        Sheet^ spare = Current();
        Sheet^ sheet;
        if (spare != nullptr && spare->path == nullptr && spare->box->TextLength == 0 &&
            !spare->box->Modified) {
            spare->path = path;
            spare->file = file;
            spare->box->Text = contents;

            spare->box->Modified = false;
            sheet = spare;

            PaneFollowsTabs();
        } else {
            sheet = MakeSheet(path, contents, file);
        }
        text_ = sheet->box;
        path_ = path;
        stateGood_ = false;
        RefreshTitle();
        ShowChoices();
        Recolour();
        OnTextChanged(nullptr, nullptr);

        sheet->box->Modified = false;
        Settle(sheet, path);
        MarkTab(sheet);
        text_->Select(0, 0);
        text_->Focus();
        what_->Text = System::IO::Path::GetFileName(path) + "  " + LineCount(text_) + " lines";
        RememberFile(path);
    }

    // Every way a file is opened puts it first in File > Recent - the tree, an error line, the
    // command line and Recent itself, as on macOS - unless the core is the worker's.
    void RememberFile(String^ path) {
        if (path == nullptr || (busy_ && !ProgramOnly())) return;
        Utf8 where(path);
        ride_remember_file(where.c());
        RefreshRecentFiles();
    }

    void OnCloseFile(Object^, EventArgs^) { CloseSheet(Current()); }

    void CloseSheet(Sheet^ sheet) {
        if (sheet == nullptr) return;
        if (!MayDiscard(sheet)) return;

        // An error remembered for this file has nowhere to go once it is gone.
        if (errorFile_ == nullptr ? sheet->box == text_ : SamePath(errorFile_, sheet->path)) ForgetError();
        DropSheet(sheet);
        AfterSheetsGone();
        console_->Text = "";
        if (sheets_->Count > 0) what_->Text = "closed";
    }

    Sheet^ SheetForPage(TabPage^ page) {
        for (int i = 0; i < sheets_->Count; ++i)
            if (sheets_[i]->page == page) return sheets_[i];
        return nullptr;
    }
    // The tab-view raises this from its built-in × (see ClosableTabControl);
    // MayDiscard prompts first if the file has unsaved changes.
    void OnTabClose(int index) {
        if (index < 0 || index >= files_->TabPages->Count) return;
        Sheet^ s = SheetForPage(files_->TabPages[index]);
        if (s != nullptr) CloseSheet(s);
    }

    // A sheet written to where it says it is, as it was found; false with the reason on the status bar.
    bool WriteSheet(Sheet^ sheet, String^ where) {
        String^ note = nullptr;
        String^ why = nullptr;
        if (!TextFile::Write(where, sheet->box->Text, sheet->file, note, why)) {
            what_->Text = "not written - " + why;
            return false;
        }
        sheet->box->Modified = false;
        Settle(sheet, where);
        MarkTab(sheet);
        what_->Text = System::IO::Path::GetFileName(where) + " written" + (note == nullptr ? "" : " - " + note);
        return true;
    }

    // What a build does first: the file in front written if it has changed, and left alone if not.
    bool SaveIfChanged() {
        Sheet^ sheet = Current();
        if (sheet == nullptr || path_ == nullptr || !sheet->box->Modified) return true;
        return WriteSheet(sheet, path_);
    }

    void OnSave(Object^, EventArgs^) {
        if (text_ == nullptr) { what_->Text = "no file is open"; return; }
        if (path_ == nullptr) {
            OnSaveAs(nullptr, nullptr);
            return;
        }
        Sheet^ sheet = Current();
        if (sheet != nullptr) WriteSheet(sheet, path_);
    }

    void OnSaveAs(Object^, EventArgs^) {
        if (text_ == nullptr) { what_->Text = "no file is open"; return; }
        if (busy_ && !ProgramOnly()) { what_->Text = StillWorking(); return; }
        Sheet^ sheet = Current();
        if (sheet == nullptr) return;

        msclr::auto_handle<SaveFileDialog> pick(gcnew SaveFileDialog());
        pick->Filter = "Sources|*.c;*.h;*.cpp;*.hpp;*.cc;*.cxx;*.shl;*.s;*.json;*.pro"
                       "|C and C++|*.c;*.h;*.cpp;*.hpp;*.cc;*.cxx|Shalimar|*.shl|All files|*.*";
        pick->InitialDirectory = ProgramsDir();
        if (sheet->path != nullptr) pick->FileName = System::IO::Path::GetFileName(sheet->path);
        if (pick->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) {
            what_->Text = "not saved";
            return;
        }

        // One tab per file: a name another tab already holds is refused rather than opened twice.
        Sheet^ other = SheetFor(pick->FileName);
        if (other != nullptr && other != sheet) {
            what_->Text = System::IO::Path::GetFileName(pick->FileName) +
                          " is open in another tab - close it first, or save under another name";
            return;
        }

        // Written first: the tab takes the new name only once there is a file by that name.
        if (!WriteSheet(sheet, pick->FileName)) return;
        sheet->path = pick->FileName;
        path_ = pick->FileName;
        MarkTab(sheet);
        RefreshTitle();
        ShowChoices();

        // Saved into the project's directory is saved into the project;
        // and a file saved under a name is one to recall from the menu.
        Utf8 saved(pick->FileName);
        if (ride_adopt_saved(project_, saved.c()) != 0)
            what_->Text = FromUtf8(ride_outcome_message(project_));
        ride_remember_file(saved.c());
        RefreshRecentFiles();
        FillTree();
    }

    static DateTime StampOf(String^ path) {
        if (path == nullptr) return DateTime::MinValue;
        try {
            return System::IO::File::Exists(path) ? System::IO::File::GetLastWriteTimeUtc(path) : DateTime::MinValue;
        } catch (Exception^) {
            return DateTime::MinValue;
        }
    }

    // What the sheet now holds is what is on disk at where.
    void Settle(Sheet^ sheet, String^ where) {
        sheet->saved = sheet->box->Text;
        sheet->stamp = StampOf(where);
    }

    // A file changed on disk since it was read or written here is read again when nothing in its
    // tab is unsaved; with unsaved edits they are kept and the status line says so, once.
    bool ReloadIfChanged(Sheet^ sheet) {
        if (sheet == nullptr || sheet->path == nullptr || busy_) return false;
        DateTime now = StampOf(sheet->path);
        if (now == DateTime::MinValue || sheet->stamp == DateTime::MinValue || now == sheet->stamp) return false;
        String^ name = System::IO::Path::GetFileName(sheet->path);
        if (sheet->box->Modified) {
            sheet->stamp = now;
            what_->Text = name + " changed on disk - your unsaved edits are kept, and Save writes over it";
            return false;
        }
        String^ contents;
        TextFile^ file = nullptr;
        try {
            String^ why = nullptr;
            contents = TextFile::Read(sheet->path, file, why);
            if (contents == nullptr) { what_->Text = why; return false; }
        } catch (Exception^ problem) {
            what_->Text = problem->Message;
            return false;
        }
        int at = sheet->box->SelectionStart;
        sheet->file = file;
        sheet->box->Text = contents;
        sheet->box->Modified = false;
        Settle(sheet, sheet->path);
        if (sheet->box == text_) {
            stateGood_ = false;
            Recolour();
            OnTextChanged(nullptr, nullptr);
            sheet->box->Select(Math::Min(at, sheet->box->TextLength), 0);
            sheet->box->ScrollToCaret();
        }
        MarkTab(sheet);
        what_->Text = name + " re-read - it changed on disk";
        return true;
    }

    void MarkTab(Sheet^ sheet) {
        if (sheet == nullptr || sheet->page == nullptr) return;
        // Undone back to what is on disk is no change (the box's own flag stays set once typed in).
        if (sheet->box->Modified && sheet->saved != nullptr && sheet->box->TextLength == sheet->saved->Length &&
            String::Equals(sheet->box->Text, sheet->saved, StringComparison::Ordinal))
            sheet->box->Modified = false;
        String^ name = TabName(sheet);
        sheet->page->Text = sheet->box->Modified ? name + "*" : name;
        if (files_ != nullptr) files_->Invalidate();
    }

    void OnExit(Object^, EventArgs^) { Close(); }

    bool MayDiscard(Sheet^ sheet) {
        if (sheet != nullptr) MarkTab(sheet);
        if (sheet == nullptr || !sheet->box->Modified) return true;

        String^ named = sheet->path == nullptr
                            ? "This file has never been saved."
                            : System::IO::Path::GetFileName(sheet->path) + " has changes.";
        System::Windows::Forms::DialogResult answer =
            MessageBox::Show(this, named + "\r\n\r\nSave it before closing?",
                             ProductName(), MessageBoxButtons::YesNoCancel,
                             MessageBoxIcon::Warning);

        if (answer == System::Windows::Forms::DialogResult::Cancel) return false;
        if (answer == System::Windows::Forms::DialogResult::No) return true;

        files_->SelectedTab = sheet->page;
        OnSheetChanged(nullptr, nullptr);
        OnSave(nullptr, nullptr);
        return !sheet->box->Modified;
    }

    void OnKeys(Object^, EventArgs^) {
        System::Text::StringBuilder^ table = gcnew System::Text::StringBuilder();
        System::Windows::Forms::KeysConverter^ spelling =
            gcnew System::Windows::Forms::KeysConverter();

        for each (ToolStripItem^ top in MainMenuStrip->Items) {
            ToolStripMenuItem^ menu = dynamic_cast<ToolStripMenuItem^>(top);
            if (menu == nullptr) continue;

            System::Text::StringBuilder^ under = gcnew System::Text::StringBuilder();
            for each (ToolStripItem^ each in menu->DropDownItems) {
                ToolStripMenuItem^ item = dynamic_cast<ToolStripMenuItem^>(each);
                if (item == nullptr) continue;

                String^ key = item->ShortcutKeys == Keys::None
                                  ? item->ShortcutKeyDisplayString
                                  : spelling->ConvertToString(item->ShortcutKeys);
                if (key == nullptr || key->Length == 0) continue;
                under->AppendFormat("  {0,-18}{1}\r\n", key, item->Text->Replace("&", ""));
            }

            if (under->Length == 0) continue;
            table->Append(menu->Text->Replace("&", ""))->Append("\r\n");
            table->Append(under->ToString())->Append("\r\n");
        }

        table->Append("Editing\r\n");
        table->Append("  Tab               lay this line out, in the leading space\r\n");
        table->Append("  Tab, Shift+Tab    over lines chosen, one level in or out\r\n");
        table->Append("  Ctrl+PageDown     the next open file, Ctrl+PageUp the one before\r\n");
        table->Append("  Enter             on the Console, go to the place that line names\r\n");
        table->Append("  Enter, Ctrl+Z     in the Console's input line: send it, end the input\r\n");

        msclr::auto_handle<Form> box(gcnew Form());
        box->Text = "Keys";
        box->FormBorderStyle = System::Windows::Forms::FormBorderStyle::SizableToolWindow;
        box->StartPosition = System::Windows::Forms::FormStartPosition::CenterParent;
        box->ClientSize = System::Drawing::Size(460, 560);
        box->ShowInTaskbar = false;

        TextBox^ shown = gcnew TextBox();
        shown->Multiline = true;
        shown->ReadOnly = true;
        shown->WordWrap = false;
        shown->ScrollBars = System::Windows::Forms::ScrollBars::Vertical;
        shown->Dock = DockStyle::Fill;
        shown->BorderStyle = System::Windows::Forms::BorderStyle::None;
        shown->BackColor = System::Drawing::Color::White;
        shown->Font = gcnew System::Drawing::Font("Consolas", 10.0f);
        shown->Text = table->ToString();
        box->Controls->Add(shown);

        box->Shown += gcnew EventHandler(this, &MainForm::OnKeysShown);
        box->ShowDialog(this);
    }

    void OnKeysShown(Object^ sender, EventArgs^) {
        Form^ box = dynamic_cast<Form^>(sender);
        if (box == nullptr || box->Controls->Count == 0) return;
        TextBox^ shown = dynamic_cast<TextBox^>(box->Controls[0]);
        if (shown == nullptr) return;
        shown->Select(0, 0);
    }

    // Help > Environment on the Console, in its fixed-width font, so the columns line up.
    // A report in the Console: in its place while nothing runs, and under a running program's
    // output rather than over it.
    void ShowReport(String^ text) {
        if (running_ != nullptr) SayCapped("\r\n" + text);
        else console_->Text = text;
    }

    // The status bar's step: the last line the worker printed, cut to fit beside the bar.
    void SayStep(String^ text) {
        array<String^>^ lines = text->Replace("\r", "")->Split('\n');
        for (int i = lines->Length - 1; i >= 0; --i) {
            String^ line = lines[i]->Trim();
            if (line->Length == 0) continue;
            step_->Text = line->Length > 70 ? line->Substring(0, 67) + "..." : line;
            return;
        }
    }

    void OnEnvironment(Object^, EventArgs^) {
        ShowReport(Lines(TakeUtf8(ride_environment())));
        ShowPanel(0);
    }

    void OnAbout(Object^, EventArgs^) {
        MessageBox::Show(this, TakeUtf8(ride_about())->Replace("\n", "\r\n"),
                         "About",
                         MessageBoxButtons::OK, MessageBoxIcon::Information);
    }

    Toolchain^ ToolsNow() { return gcnew Toolchain(cc1_, cl_, shc_, cxx1_, arch_); }

    // Output of the core's, on the end of the Console: its line endings made the box's own
    // whichever a tool wrote, and appended rather than the whole text written again.
    void Say(String^ text) {
        if (String::IsNullOrEmpty(text)) return;
        if (busy_) SayStep(text);
        console_->AppendText(Lines(text));
        ShowConsoleEnd();
    }

    // What Compile and Run both ask first. False when there is nothing to do, or it has been done
    // another way - a file that is one of the project's sources runs the project.
    bool SingleFileReady(int% kind, int% language) {
        if (text_ == nullptr) { what_->Text = "no file is open"; return false; }
        if (busy_) { what_->Text = StillWorking(); return false; }
        ForgetError();
        if (path_ == nullptr) {
            what_->Text = "open a file first";
            return false;
        }
        if (!SaveIfChanged()) return false;

        {
            Utf8 ask(path_);
            int of = ride_project_runs_as_project(project_, ask.c());
            if (of > 0) {
                what_->Text = System::IO::Path::GetFileName(path_) + " is one of " + of +
                              " sources of " + FromUtf8(ride_project_name(project_)) +
                              " - running the project";
                BuildProject(true);
                return false;
            }
        }

        language = LanguageNow();
        kind = ride_resolve(toolKind_, language);
        if (ride_can_compile(kind, language) == 0) {
            what_->Text = FromUtf8(ride_refusal(kind, language));
            return false;
        }
        return true;
    }

    Job^ SingleFileJob(int what, int kind, int language) {
        Job^ job = gcnew Job(what);
        job->tools = ToolsNow();
        job->source = gcnew Utf8(path_);
        job->kind = kind;
        job->language = language;
        job->config = config_;
        return job;
    }

    void OnCompile(Object^, EventArgs^) {
        if (!InFileMode()) return;
        int kind = 0, language = 0;
        if (!SingleFileReady(kind, language)) return;

        String^ source = path_;
        Job^ job = SingleFileJob(Job::Build, kind, language);
        console_->Text = "$ " + FromUtf8(ride_shown_command(project_, job->tools->cc1(), job->tools->cl(),
                                                            job->tools->shc(), job->tools->cxx1(), kind,
                                                            job->source->c(), language,
                                                            job->tools->arch(), config_)) + "\r\n";
        panel_->SelectedIndex = 0;
        what_->Text = "compiling " + System::IO::Path::GetFileName(source) + " ...";

        bool finished = WhileBusy(job);
        RIDEBuild* built = job->build;
        delete job;
        if (built == nullptr) { what_->Text = finished ? "nothing was built" : "stopped"; return; }

        Say(FromUtf8(ride_build_output(built)));
        CollectIssues(source);
        if (!finished) {
            ride_build_free(built);
            Say("\n[stopped]\n");
            what_->Text = "stopped";
            return;
        }

        if (ride_build_has_error(built) != 0) {
            int line = ride_build_error_line(built);
            int column = ride_build_error_column(built);
            String^ message = FromUtf8(ride_build_error_message(built));
            String^ where = FromUtf8(ride_build_error_file(built));
            ride_build_free(built);
            ShowError(line, column, message, where, source);
            return;
        }

        if (ride_build_ok(built) == 0) {
            what_->Text = FromUtf8(ride_toolchain_name(kind)) + " failed - see the console";
            ride_build_free(built);
            return;
        }

        String^ produced = FromUtf8(ride_build_assembly(built));
        assembly_->Text = produced->Replace("\n", "\r\n");
        SayDebugTab(produced);
        int lines = ride_build_assembly_lines(built);
        ride_build_free(built);

        // Said in so many words, as Visual Studio and CCS do, and on the Console tab where it is looked for;
        // the listing is on the Assembly tab beside it.
        String^ name = System::IO::Path::GetFileName(source);
        Say("\n========== Compilation succeeded: " + name + " - 0 errors ==========\n" +
            lines + " lines of assembly, on the Assembly tab\n");
        panel_->SelectedIndex = 0;
        what_->Text = "Compilation succeeded: " + name + " - 0 errors, " + lines + " lines of assembly";
    }

    // ---- Build > Command-line arguments --------------------------------------------
    String^ CurrentArgs() {
        if (ride_project_loaded(project_) != 0) return TakeUtf8(ride_project_arguments(project_));
        return fileArgs_ == nullptr ? "" : fileArgs_;
    }

    String^ ArgsBase() {
        if (ride_project_loaded(project_) != 0 && projectDirectory_ != nullptr) return projectDirectory_;
        return path_ == nullptr ? "" : System::IO::Path::GetDirectoryName(path_);
    }

    void CommitArgs() {
        String^ typed = argsBox_->Text->Trim();
        if (typed == CurrentArgs()) return;
        if (ride_project_loaded(project_) != 0) {
            Utf8 line(typed);
            what_->Text = ride_project_set_arguments(project_, line.c()) != 0
                              ? "command-line arguments: " + (typed->Length > 0 ? typed : "none")
                              : "the command-line arguments could not be kept with the project";
        } else {
            fileArgs_ = typed;
            what_->Text = "command-line arguments: " + (typed->Length > 0 ? typed : "none");
        }
    }

    // ---- Edit > Find in Files --------------------------------------------------------
    void OnFindInFiles(Object^, EventArgs^) {
        if (finding_) { what_->Text = "a search is running - Build > Stop ends it"; return; }
        String^ lookFor = text_ != nullptr && text_->SelectionLength > 0 && text_->SelectedText->IndexOf('\n') < 0
                              ? text_->SelectedText : "";
        String^ where = ride_project_loaded(project_) != 0 && projectDirectory_ != nullptr ? projectDirectory_
                        : path_ != nullptr ? System::IO::Path::GetDirectoryName(path_)
                        : System::Environment::GetFolderPath(System::Environment::SpecialFolder::MyDocuments);
        msclr::auto_handle<FindFilesDialog> ask(
            gcnew FindFilesDialog(lookFor, where, findTypes_, findCase_, findWord_, findSub_));
        if (ask->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) { what_->Text = "nothing searched"; return; }
        findTypes_ = ask->types->Text;
        findCase_ = ask->matchCase->Checked;
        findWord_ = ask->wholeWord->Checked;
        findSub_ = ask->subfolders->Checked;
        if (ask->text->Text->Length == 0 && !ask->namesOnly->Checked) { what_->Text = "nothing to find"; return; }
        if (!System::IO::Directory::Exists(ask->folder->Text)) { what_->Text = "no folder " + ask->folder->Text; return; }
        FindJob^ job = gcnew FindJob();
        job->text = gcnew Utf8(ask->text->Text);
        job->folder = gcnew Utf8(ask->folder->Text);
        job->types = gcnew Utf8(ask->types->Text);
        job->matchCase = ask->matchCase->Checked ? 1 : 0;
        job->wholeWord = ask->wholeWord->Checked ? 1 : 0;
        job->subfolders = ask->subfolders->Checked ? 1 : 0;
        job->namesOnly = ask->namesOnly->Checked ? 1 : 0;
        // Off the window's thread, so a search of a large tree leaves the window live and Stop ends it.
        System::Threading::Thread^ worker =
            gcnew System::Threading::Thread(gcnew System::Threading::ThreadStart(job, &FindJob::Run));
        worker->IsBackground = true;
        bool wasBusy = busy_;
        if (!wasBusy) SetBusy(true);
        finding_ = true;
        what_->Text = "searching ... Build > Stop ends it";
        worker->Start();
        while (!worker->Join(40)) Application::DoEvents();
        finding_ = false;
        if (!wasBusy) SetBusy(false);
        delete job->text;
        delete job->folder;
        delete job->types;
        String^ found = job->found == nullptr ? "" : job->found;
        if (closeWhenIdle_ && !busy_) { CloseNow(); return; }
        ShowReport(found->Replace("\n", "\r\n"));
        ShowPanel(0);
        int end = found->IndexOf('\n');
        what_->Text = (end > 0 ? found->Substring(0, end) : found) + " - double-click a line to go to it";
    }

    void OnBuildMenuOpening(Object^, EventArgs^) { argsBox_->Text = CurrentArgs(); }
    void OnBuildMenuClosed(Object^, EventArgs^) { CommitArgs(); }

    void OnArgsKey(Object^, KeyEventArgs^ e) {
        if (e->KeyCode != Keys::Enter) return;
        e->SuppressKeyPress = true;
        CommitArgs();
        // Close the submenu and the Build menu with it.
        ToolStripDropDown^ owner = dynamic_cast<ToolStripDropDown^>(argsBox_->Owner);
        while (owner != nullptr) {
            ToolStripItem^ parent = owner->OwnerItem;
            owner->Close();
            owner = parent != nullptr ? dynamic_cast<ToolStripDropDown^>(parent->Owner) : nullptr;
        }
    }

    void OnRun(Object^, EventArgs^) {
        ride_run_next(RIDE_RUN_PROGRAM);
        RunFile();
    }

    // **Build > Emulate on vm6747 and Build > Verify, for tms6747.** The Run the file or the project in
    // front would make, with the bridge told which runs the program: the assembly on vm6747 and no .out
    // linked, or both runs compared. Said to the next start (ride_run_next), which takes it.
    void RunOn(int runner) {
        if (arch_ == nullptr || !String::Equals(arch_, "tms6747", StringComparison::Ordinal)) {
            what_->Text = runner == RIDE_RUN_EMULATOR ? "Emulate on vm6747 is for tms6747 - choose it under Target"
                                                      : "Verify is for tms6747 - choose it under Target";
            return;
        }
        ride_run_next(runner);
        if (ProjectMode()) BuildProject(true);
        else RunFile();
    }
    void OnEmulate(Object^, EventArgs^) { RunOn(RIDE_RUN_EMULATOR); }
    void OnVerify(Object^, EventArgs^) { RunOn(RIDE_RUN_VERIFY); }

    void RunFile() {
        if (!InFileMode()) return;
        int kind = 0, language = 0;
        if (!SingleFileReady(kind, language)) return;

        Toolchain^ tools = ToolsNow();
        if (ride_runs_here(kind, tools->arch()) == 0) {
            what_->Text = FromUtf8(ride_why_not_run(kind, tools->arch()));
            delete tools;
            return;
        }

        Utf8 source(path_);
        console_->Text = "$ " + FromUtf8(ride_shown_run_command(project_, tools->cc1(), tools->cl(),
                                                                tools->shc(), tools->cxx1(), kind,
                                                                source.c(), language,
                                                                tools->arch(), config_)) + "\r\n";
        panel_->SelectedIndex = 0;
        what_->Text = "building and running " + System::IO::Path::GetFileName(path_) + " ...";

        String^ args = CurrentArgs();
        if (args->Length > 0) console_->AppendText("$ " + System::IO::Path::GetFileNameWithoutExtension(path_) + " " + args + "\r\n");
        Utf8 argsLine(args);
        Utf8 argsBase(ArgsBase());
        RIDERunning* running = ride_run_start_line(project_, tools->cc1(), tools->cl(), tools->shc(),
                                                   tools->cxx1(), kind, source.c(), language,
                                                   tools->arch(), config_, argsLine.c(), argsBase.c(), &OutputToWindow,
                                                   Runtime::InteropServices::GCHandle::ToIntPtr(self_).ToPointer());
        StartedRunning(running, tools, path_, nullptr);
    }

    // ---- a program running with a real input -------------------------------------
    // Its output arrives through Post as it comes and is added to the Console here; the window
    // stays live but for what reaches the core, as while the worker works, until Heard gets the end.

    void StartedRunning(RIDERunning* running, Toolchain^ tools, String^ source, String^ program) {
        if (running == nullptr) {
            delete tools;
            what_->Text = "the program could not be started";
            return;
        }
        running_ = running;
        runTools_ = tools;
        runSource_ = source;
        runProgram_ = program;
        decoders_ = gcnew array<System::Text::Decoder^>(3);
        for (int i = 0; i < 3; ++i) decoders_[i] = System::Text::Encoding::UTF8->GetDecoder();
        SetBusy(true);
        input_->Enabled = true;
        input_->Clear();
        heardProgram_ = false;
    }

    // Everything posted since the last drain, appended at once; the end of the run after its output.
    void DrainOutput() {
        System::Collections::Generic::List<OutChunk^>^ batch;
        bool dropped;
        System::Threading::Monitor::Enter(outLock_);
        try {
            batch = outQueue_;
            outQueue_ = gcnew System::Collections::Generic::List<OutChunk^>();
            outQueued_ = 0;
            outPosted_ = false;
            dropped = outDropped_;
            outDropped_ = false;
        } finally {
            System::Threading::Monitor::Exit(outLock_);
        }
        System::Text::StringBuilder^ text = gcnew System::Text::StringBuilder();
        if (dropped && running_ != nullptr) text->Append("\n[... output dropped - the program printed faster than it could be shown ...]\n");
        bool program = false;
        for each (OutChunk^ chunk in batch) {
            if (running_ == nullptr) break;
            if (chunk->bytes == nullptr) {
                SayCapped(text->ToString());
                text->Clear();
                EndedRunning();
                continue;
            }
            int stream = chunk->stream < 0 || chunk->stream > 2 ? RIDE_STREAM_OUT : chunk->stream;
            array<wchar_t>^ chars = gcnew array<wchar_t>(decoders_[stream]->GetCharCount(chunk->bytes, 0, chunk->bytes->Length));
            decoders_[stream]->GetChars(chunk->bytes, 0, chunk->bytes->Length, chars, 0);
            text->Append(chars);
            if (stream != RIDE_STREAM_BUILD) program = true;
        }
        SayCapped(text->ToString());
        if (program && running_ != nullptr) {
            if (what_->Text->StartsWith("building and running"))
                what_->Text = "running - type under the Console, Enter sends, Ctrl+Z ends the input";
            // The keyboard goes to the input line when the program first speaks, not while it compiles.
            if (!heardProgram_) {
                heardProgram_ = true;
                if (input_->Enabled) input_->Focus();
                FreeForProgram();
            }
        }
    }

    // Say, keeping the Console to its last ConsoleCap characters.
    void SayCapped(String^ text) {
        if (String::IsNullOrEmpty(text)) return;
        Say(text);
        if (console_->TextLength > ConsoleCap) {
            console_->Select(0, console_->TextLength - ConsoleCap * 3 / 4);
            console_->SelectedText = "";
            ShowConsoleEnd();
        }
    }

    void OnInputKey(Object^, KeyEventArgs^ e) {
        if (e->KeyCode != Keys::Enter) return;
        e->SuppressKeyPress = true;
        if (running_ == nullptr) return;
        String^ line = input_->Text;
        input_->Clear();
        // Not echoed here: the program's pseudo-console echoes what it reads, as a terminal would.
        Utf8 bytes(line + "\n");
        int length = System::Text::Encoding::UTF8->GetByteCount(line + "\n");
        if (ride_running_send(running_, bytes.c(), length) == 0)
            what_->Text = "the program is no longer reading - it has ended or its input was ended";
    }

    // Ctrl+Z in the input line: end of file, as Ctrl+Z then Enter is at a Windows console.
    void EndInput() {
        if (running_ == nullptr) return;
        ride_running_close_input(running_);
        input_->Enabled = false;
        what_->Text = "the program's input is ended";
    }

    void EndedRunning() {
        RIDERunning* running = running_;
        ride_running_wait(running, -1);
        String^ source = runSource_;
        String^ program = runProgram_;
        CollectIssues(source);

        if (ride_running_stopped(running) != 0) {
            Say("\n[stopped]\n");
            what_->Text = "stopped";
        } else if (ride_running_has_error(running) != 0) {
            int line = ride_running_error_line(running);
            int column = ride_running_error_column(running);
            String^ message = FromUtf8(ride_running_error_message(running));
            String^ where = FromUtf8(ride_running_error_file(running));
            ShowError(line, column, message, where, source);
        } else if (ride_running_built(running) == 0 || ride_running_ran(running) == 0) {
            what_->Text = program == nullptr ? "no program was built - see the console"
                                             : "could not start " + System::IO::Path::GetFileName(program);
        } else {
            int status = ride_running_status(running);
            Say(String::Format("\n[program returned {0}]\n", status));
            String^ named = program != nullptr ? program : source;
            what_->Text = String::Format("{0} ran - it returned {1}",
                                         System::IO::Path::GetFileName(named), status);
        }

        running_ = nullptr;
        ride_running_free(running);
        delete runTools_;
        runTools_ = nullptr;
        input_->Enabled = false;
        SetBusy(false);
        if (closeWhenIdle_) BeginInvoke(gcnew Action(this, &MainForm::CloseNow));
    }

    void OnBuildProject(Object^, EventArgs^) { ride_run_next(RIDE_RUN_PROGRAM); if (InProjectMode()) BuildProject(false); }
    void OnRunProject(Object^, EventArgs^) { ride_run_next(RIDE_RUN_PROGRAM); if (InProjectMode()) BuildProject(true); }

    void BuildProject(bool andRun) {
        if (busy_) { what_->Text = StillWorking(); return; }
        ForgetError();

        if (ride_project_target_ready(project_) == 0) {
            String^ why = FromUtf8(ride_project_target_why(project_));
            String^ detail = FromUtf8(ride_project_target_detail(project_));
            what_->Text = why;
            console_->Text = detail->Length > 0 ? why + "\r\n\r\n" + detail : why;
            panel_->SelectedIndex = 0;
            return;
        }

        if (!SaveEveryDirty()) return;

        // The compilers, pinned before the checks: naming each part's compiler needs them, and so does the build.
        Toolchain^ tools = ToolsNow();

        // **Every part, not the target as a whole.** A group of C and C++ is split into a part each
        // and the build sends each to its own compiler through toolKind_, so the build call must
        // pass toolKind_, not one kind resolved from the first part: that sent a .cpp to cc1, which said "expected a type" while the status bar showed cxx1.
        int parts = ride_project_target_parts(project_);
        System::Collections::Generic::List<String^>^ compilers =
            gcnew System::Collections::Generic::List<String^>();
        for (int i = 0; i < parts; ++i) {
            int partLang = ride_project_part_language(project_, i);
            int partKind = ride_project_part_toolchain(project_, i, tools->cc1(), tools->cl(),
                                                       tools->shc(), tools->cxx1(), toolKind_);
            if (ride_can_compile(partKind, partLang) == 0) {
                what_->Text = FromUtf8(ride_refusal(partKind, partLang));
                delete tools;
                return;
            }
            if (andRun && ride_runs_here(partKind, tools->arch()) == 0) {
                what_->Text = FromUtf8(ride_why_not_run(partKind, tools->arch()));
                delete tools;
                return;
            }
            String^ word = FromUtf8(ride_toolchain_name(partKind));
            if (!compilers->Contains(word)) compilers->Add(word);
        }

        String^ program = FromUtf8(ride_project_target_program(project_));
        int howMany = ride_project_target_sources(project_);

        System::Text::StringBuilder^ said = gcnew System::Text::StringBuilder();
        said->Append("$ " + String::Join(", ", compilers->ToArray()) + " " + howMany +
                     (howMany == 1 ? " source -o " : " sources -o ") + program + "\r\n");
        for (int i = 0; i < howMany; ++i)
            said->Append("    " + FromUtf8(ride_project_target_source(project_, i)) + "\r\n");
        console_->Text = said->ToString();
        panel_->SelectedIndex = 0;
        what_->Text = "building " + System::IO::Path::GetFileName(program) + " ...";

        Job^ job = gcnew Job(Job::BuildTarget);
        job->tools = tools;
        job->toolKind = toolKind_;
        job->config = config_;
        bool finished = WhileBusy(job);
        RIDEBuild* made = job->build;
        delete job;
        if (made == nullptr) {
            what_->Text = finished ? FromUtf8(ride_project_target_why(project_)) : "stopped";
            return;
        }

        Say(FromUtf8(ride_build_output(made)));
        CollectIssues(nullptr);
        if (!finished) {
            ride_build_free(made);
            Say("\n[stopped]\n");
            what_->Text = "stopped";
            return;
        }

        if (ride_build_has_error(made) != 0) {
            int line = ride_build_error_line(made);
            int column = ride_build_error_column(made);
            String^ message = FromUtf8(ride_build_error_message(made));
            String^ where = FromUtf8(ride_build_error_file(made));
            ride_build_free(made);
            ShowError(line, column, message, where, nullptr);
            return;
        }

        bool ok = ride_build_ok(made) != 0;
        // What the build made is what runs - <program>.vm for a C6747 project - and never the target's name.
        String^ madeProgram = FromUtf8(ride_build_made(made));
        bool madeShalimar = ride_build_made_shalimar(made) != 0;
        ride_build_free(made);
        if (madeProgram->Length == 0) madeProgram = program;

        if (!ok) {
            what_->Text = String::Join(", ", compilers->ToArray()) +
                          " did not build it - see the console";
            return;
        }

        String^ succeeded = "Build succeeded: " + System::IO::Path::GetFileNameWithoutExtension(program) + " - " +
                            howMany + (howMany == 1 ? " source" : " sources") + ", 0 errors";
        Say("\n========== " + succeeded + " ==========\n");
        if (!andRun) {
            Say(madeProgram + "\n");
            what_->Text = succeeded;
            return;
        }

        Utf8 built(madeProgram);
        String^ args = CurrentArgs();
        Utf8 argsLine(args);
        Utf8 argsBase(ArgsBase());
        // The command run, arguments and all: a tool handed none prints its usage, and that should not read as a fault.
        Say("\n" + TakeUtf8(ride_run_line(project_, built.c(), argsLine.c())) + "\n");
        what_->Text = "running " + System::IO::Path::GetFileName(madeProgram) + " ...";
        RIDERunning* running = ride_run_made_start_line(built.c(), madeShalimar ? 1 : 0, argsLine.c(), argsBase.c(),
                                                        &OutputToWindow,
                                                        Runtime::InteropServices::GCHandle::ToIntPtr(self_).ToPointer());
        StartedRunning(running, nullptr, nullptr, madeProgram);
    }

    bool SaveEveryDirty() {
        for (int i = 0; i < sheets_->Count; ++i) {
            Sheet^ sheet = sheets_[i];
            if (sheet->path == nullptr || !sheet->box->Modified) continue;
            if (!WriteSheet(sheet, sheet->path)) return false;
        }
        return true;
    }

    // An error a build reported, taken to: the file it names - relative to the file built, then to
    // the project - brought to the front, and the file built when it names none.
    void ShowError(int line, int column, String^ message, String^ where, String^ source) {
        String^ file = source;
        if (!String::IsNullOrEmpty(where)) {
            file = where;
            if (!System::IO::Path::IsPathRooted(where)) {
                String^ beside = source == nullptr ? nullptr
                    : System::IO::Path::Combine(System::IO::Path::GetDirectoryName(source), where);
                if (beside != nullptr && System::IO::File::Exists(beside)) {
                    file = beside;
                } else {
                    Utf8 relative(where);
                    file = FromUtf8(ride_project_absolute(project_, relative.c()));
                }
            }
        }
        if (file != nullptr && !SamePath(path_, file) && System::IO::File::Exists(file)) OpenPath(file);

        RememberError(line, column, message, file);
        CollectIssues(source);
        errors_->Reported(file, line, column, message);
        errorsPage_->Text = String::Format("Errors ({0})", errors_->Errors());
        if (SamePath(path_, file)) GoTo(line, column);
        panel_->SelectedIndex = 0;
        what_->Text = String::Format("{0}{1}:{2}: error: {3}",
                                     file == nullptr ? "" : System::IO::Path::GetFileName(file) + ":",
                                     line, column, message);
    }

    // ---- the worker -------------------------------------------------------------

    void DoWork(Object^ given) {
        Job^ job = safe_cast<Job^>(given);
        Toolchain^ t = job->tools;
        switch (job->what) {
            case Job::Build:
                job->build = ride_build(project_, t->cc1(), t->cl(), t->shc(), t->cxx1(), job->kind,
                                        job->source->c(), job->language, t->arch(), job->config);
                break;
            case Job::BuildTarget:
                job->build = ride_build_target(project_, t->cc1(), t->cl(), t->shc(), t->cxx1(),
                                               job->toolKind, t->arch(), job->config);
                job->result = job->build != nullptr && ride_build_ok(job->build) != 0 ? 1 : 0;
                break;
            case Job::Convert:
                job->converted = ride_convert(job->program->c(), job->source->c(), job->into->c(),
                                              job->toShalimar);
                break;
            case Job::BuildProgram:
                job->made = ride_build_program(project_, t->cc1(), t->cl(), t->shc(), t->cxx1(), job->kind,
                                               job->source->c(), job->language, t->arch(), job->config);
                job->result = ride_program_ok(job->made);
                break;
            case Job::DebugStart:
                job->result = ride_debugger_start(debugger_, job->kind, t->arch(), job->program->c());
                break;
            case Job::DebugGo:     ride_debugger_run(debugger_); break;
            case Job::DebugResume: ride_debugger_resume(debugger_); break;
            case Job::StepOver:    ride_debugger_step_over(debugger_); break;
            case Job::StepInto:    ride_debugger_step_into(debugger_); break;
            case Job::StepOut:     ride_debugger_step_out(debugger_); break;
            default: break;
        }
    }

    // The job on a thread of its own while this one keeps the window drawn. False when Build >
    // Stop ended it, or the window is closing - the caller then tidies up and goes no further.
    bool WhileBusy(Job^ job) {
        if (busy_) { what_->Text = StillWorking(); return false; }

        job_ = job;
        SetBusy(true);
        System::Threading::Thread^ worker = gcnew System::Threading::Thread(
            gcnew System::Threading::ParameterizedThreadStart(this, &MainForm::DoWork));
        worker->IsBackground = true;
        worker->Start(job);

        while (!worker->Join(50)) {
            try {
                Application::DoEvents();
            } catch (Exception^ problem) {
                what_->Text = problem->Message;
            }
        }

        job_ = nullptr;
        SetBusy(false);
        if (closeWhenIdle_) BeginInvoke(gcnew Action(this, &MainForm::CloseNow));
        return !job->stopped && !closeWhenIdle_;
    }

    void CloseNow() {
        closeWhenIdle_ = false;
        Close();
    }

    // Grey what would reach the core the worker holds, or give it back.
    void SetBusy(bool on) {
        busy_ = on;
        progress_->Visible = on;
        step_->Visible = on;
        step_->Text = on ? what_->Text : "";
        for each (ToolStripMenuItem^ item in gated_) item->Enabled = !on;
        stopItem_->Enabled = on;
        RefreshProjectMenu();
        RefreshModeItems();
        if (!on && treeStale_) FillTree();
    }

    void GateBelow(ToolStripItemCollection^ items) {
        for each (ToolStripItem^ each in items) {
            ToolStripMenuItem^ item = dynamic_cast<ToolStripMenuItem^>(each);
            if (item == nullptr) continue;
            if (item->HasDropDownItems) GateBelow(item->DropDownItems);
            else if (!live_->Contains(item)) gated_->Add(item);
        }
    }

    bool GatedKey(Keys keys) {
        if (keys == Keys::None) return false;
        for each (ToolStripMenuItem^ item in gated_)
            if (item->ShortcutKeys == keys) return true;
        return false;
    }

    void OnStop(Object^, EventArgs^) {
        if (finding_) { ride_find_stop(); what_->Text = "stopping the search ..."; return; }
        if (running_ == nullptr && (!busy_ || job_ == nullptr)) { what_->Text = "nothing is running"; return; }
        StopWork();
    }

    // Ends whatever is running so its call comes back: a program through its own Stop, a build
    // through the core's, and the debugger - which has none - by ending the processes the window started.
    void StopWork() {
        what_->Text = "stopping ...";
        if (finding_) ride_find_stop();
        if (running_ != nullptr) { ride_running_stop(running_); return; }
        if (job_ == nullptr) return;
        job_->stopped = true;
        ride_cancel_builds();
        if (job_->what >= Job::DebugStart) StopChildren();
    }

    System::Collections::Generic::List<int>^ BreaksFor(String^ file) {
        if (file == nullptr) return nullptr;
        String^ key = OneName(file);
        System::Collections::Generic::List<int>^ lines = nullptr;
        if (!breaks_->TryGetValue(key, lines)) {
            lines = gcnew System::Collections::Generic::List<int>();
            breaks_[key] = lines;
        }
        breakNames_[key] = file;
        return lines;
    }

    void OnToggleBreak(Object^, EventArgs^) {
        if (text_ == nullptr) { what_->Text = "no file is open"; return; }
        if (path_ == nullptr) {
            what_->Text = "save the file first - a breakpoint is on a line of a file";
            return;
        }

        System::Collections::Generic::List<int>^ lines = BreaksFor(path_);
        int line = CaretRow() + 1;

        if (lines->Contains(line)) {
            lines->Remove(line);
            if (ride_debugger_running(debugger_) != 0) SetEveryBreakpoint();
            what_->Text = String::Format("breakpoint off line {0}", line);
        } else {
            lines->Add(line);
            if (ride_debugger_running(debugger_) != 0) {
                Utf8 named(path_);
                ride_debugger_break(debugger_, named.c(), line);
            }
            what_->Text = String::Format("breakpoint on line {0}", line);
        }
        Sheet^ sheet = Current();
        if (sheet != nullptr) sheet->gutter->Invalidate();
    }

    void SetEveryBreakpoint() {
        ride_debugger_clear(debugger_);
        for each (System::Collections::Generic::KeyValuePair<String^,
                      System::Collections::Generic::List<int>^> pair in breaks_) {
            String^ named = nullptr;
            if (!breakNames_->TryGetValue(pair.Key, named)) named = pair.Key;
            Utf8 file(named);
            for each (int line in pair.Value) ride_debugger_break(debugger_, file.c(), line);
        }
    }

    void OnDebug(Object^, EventArgs^) { Debug(false); }
    void OnDebugProject(Object^, EventArgs^) { Debug(true); }

    // One step of a session already running, on the worker; a stop on the way ends the session.
    void DebugStep(int what) {
        if (!WhileBusy(gcnew Job(what))) {
            EndDebugging();
            what_->Text = "debugging stopped";
            return;
        }
        ShowStop();
    }

    void Debug(bool project) {
        if (busy_) { what_->Text = StillWorking(); return; }
        if (ride_debugger_running(debugger_) != 0) {
            DebugStep(Job::DebugResume);
            return;
        }
        if (project ? !InProjectMode() : !InFileMode()) return;

        ForgetError();
        Toolchain^ tools = ToolsNow();

        int kind = 0;
        int language = 0;

        if (project) {

            if (ride_project_target_ready(project_) == 0) {
                String^ why = FromUtf8(ride_project_target_why(project_));
                String^ detail = FromUtf8(ride_project_target_detail(project_));
                what_->Text = why;
                console_->Text = detail->Length > 0 ? why + "\r\n\r\n" + detail : why;
                panel_->SelectedIndex = 0;
                delete tools;
                return;
            }
            if (!SaveEveryDirty()) { delete tools; return; }
            language = ride_project_target_language(project_);

            if (ride_project_debug_plan(project_, tools->cc1(), tools->cl(), tools->shc(),
                                        tools->cxx1(), toolKind_, tools->arch()) == 0) {
                what_->Text = FromUtf8(ride_project_why_not_debug(project_));
                delete tools;
                return;
            }
            kind = ride_project_debug_kind(project_);
        } else {
            if (path_ == nullptr) { what_->Text = "open a file first"; delete tools; return; }
            if (!SaveIfChanged()) { delete tools; return; }

            language = LanguageNow();
            kind = ride_resolve(toolKind_, language);
            if (ride_can_compile(kind, language) == 0) {
                what_->Text = FromUtf8(ride_refusal(kind, language));
                delete tools;
                return;
            }

            if (ride_debugger_stops_itself(kind) == 0 &&
                ride_debugger_for(kind, tools->arch()) == 0) {
                what_->Text = FromUtf8(ride_no_debugger_because(kind, tools->arch()));
                delete tools;
                return;
            }
        }

        if (ride_runs_here(kind, tools->arch()) == 0) {
            what_->Text = FromUtf8(ride_why_not_run(kind, tools->arch()));
            delete tools;
            return;
        }
        if (config_ != RIDE_CONFIG_DEBUG) {

            what_->Text =
                FromUtf8(ride_release_cannot_stop(kind)) + " - choose Build > Configuration > Debug, then F8";
            delete tools;
            return;
        }

        console_->Text = project ? "$ building the project for the debugger\r\n"
                                 : "$ building for the debugger\r\n";
        if (project) {
            System::Text::StringBuilder^ listed = gcnew System::Text::StringBuilder();
            int howMany = ride_project_target_sources(project_);
            for (int i = 0; i < howMany; ++i)
                listed->Append("    " + FromUtf8(ride_project_target_source(project_, i)) + "\r\n");

            int blind = ride_project_blind_groups(project_);
            for (int i = 0; i < blind; ++i)
                listed->Append("  (" + FromUtf8(ride_project_blind_group(project_, i)) +
                               " carries no debug information - the debugger cannot stop in it)\r\n");
            console_->AppendText(listed->ToString());
        }
        panel_->SelectedIndex = 0;
        what_->Text = "building for the debugger ...";

        if (built_ != nullptr) { ride_program_free(built_); built_ = nullptr; }
        if (targetBuilt_ != nullptr) { ride_build_free(targetBuilt_); targetBuilt_ = nullptr; }

        debugSource_ = project ? nullptr : path_;
        Job^ job = gcnew Job(project ? Job::BuildTarget : Job::BuildProgram);
        job->tools = tools;
        job->toolKind = toolKind_;
        job->kind = kind;
        job->language = language;
        job->config = config_;
        if (!project) job->source = gcnew Utf8(path_);
        bool finished = WhileBusy(job);
        targetBuilt_ = job->build;
        built_ = job->made;
        int result = job->result;
        delete job;

        if (project && targetBuilt_ == nullptr) {
            what_->Text = finished ? FromUtf8(ride_project_target_why(project_)) : "stopped";
            return;
        }

        Say(FromUtf8(project ? ride_build_output(targetBuilt_) : ride_program_output(built_)));
        if (!finished) { EndDebugging(); what_->Text = "stopped"; return; }

        if (result == 0) { DebugBuildFailed(project, kind); return; }

        // What the build made: a C6000 project's is <program>.vm, which the session runs through sim6747.
        String^ program = project ? FromUtf8(ride_build_made(targetBuilt_))
                                  : FromUtf8(ride_program_path(built_));
        if (project && String::IsNullOrEmpty(program)) program = FromUtf8(ride_project_target_program(project_));

        what_->Text = ride_debugger_stops_itself(kind) != 0
                          ? "starting the program ..."
                          : "starting the debugger ...";
        Job^ start = gcnew Job(Job::DebugStart);
        start->tools = ToolsNow();
        start->kind = kind;
        start->program = gcnew Utf8(program);
        finished = WhileBusy(start);
        result = start->result;
        delete start;
        if (!finished) { EndDebugging(); what_->Text = "debugging stopped"; return; }
        if (result == 0) {
            Utf8 arch(arch_);
            what_->Text = FromUtf8(ride_why_it_did_not_start(kind, arch.c()));
            EndDebugging();
            return;
        }

        SetEveryBreakpoint();
        DebugStep(Job::DebugGo);
    }

    void DebugBuildFailed(bool project, int kind) {
        bool told = project ? ride_build_has_error(targetBuilt_) != 0
                            : ride_program_has_error(built_) != 0;
        if (told) {
            int line = project ? ride_build_error_line(targetBuilt_)
                               : ride_program_error_line(built_);
            int column = project ? ride_build_error_column(targetBuilt_)
                                 : ride_program_error_column(built_);
            String^ message = FromUtf8(project ? ride_build_error_message(targetBuilt_)
                                               : ride_program_error_message(built_));
            String^ where = FromUtf8(project ? ride_build_error_file(targetBuilt_)
                                             : ride_program_error_file(built_));
            ShowError(line, column, message, where, debugSource_);
        } else {
            what_->Text = FromUtf8(ride_toolchain_name(kind)) +
                          " built no program - see the console";
        }
        String^ said = what_->Text;
        EndDebugging();
        what_->Text = said;
    }

    void OnDebugMenuOpening(Object^, EventArgs^) {
        RefreshModeItems();
        bool itsOwn = !busy_ && ride_debugging_shalimar(debugger_) != 0;
        upTheStack_->Enabled = !busy_ && !itsOwn;
        downTheStack_->Enabled = !busy_ && !itsOwn;
        watchItem_->Enabled = !busy_ && !itsOwn;
    }

    void OnStepOver(Object^, EventArgs^) { Step(0); }
    void OnStepInto(Object^, EventArgs^) { Step(1); }
    void OnStepOut(Object^, EventArgs^) { Step(2); }

    void OnFrameUp(Object^, EventArgs^) { LookAlongStack(1); }
    void OnFrameDown(Object^, EventArgs^) { LookAlongStack(-1); }

    void Step(int how) {
        if (busy_) { what_->Text = StillWorking(); return; }
        if (ride_debugger_running(debugger_) == 0) {
            what_->Text = "nothing is running - F8 starts it";
            return;
        }
        DebugStep(how == 1 ? Job::StepInto : how == 2 ? Job::StepOut : Job::StepOver);
    }

    void OnDebugStop(Object^, EventArgs^) {
        // The program running under the worker is ended where it stands; the step that was waiting
        // for it then sees the session gone and ends it on this thread, not under the worker.
        if (busy_) { StopWork(); return; }
        if (ride_debugger_running(debugger_) == 0) {
            what_->Text = "nothing is running";
            return;
        }
        EndDebugging();
        what_->Text = "debugging stopped";
    }

    void EndDebugging() {
        ride_debugger_stop(debugger_);

        if (built_ != nullptr) { ride_program_free(built_); built_ = nullptr; }
        if (targetBuilt_ != nullptr) { ride_build_free(targetBuilt_); targetBuilt_ = nullptr; }
        stopFile_ = nullptr;
        stopLine_ = 0;
        lookingFile_ = nullptr;
        lookingLine_ = 0;
        ShowStoppedLine(nullptr, -1);
        for each (Sheet^ sheet in sheets_) sheet->gutter->Invalidate();
    }

    // Where the debugger says it stopped, as a file this window can open: its name as given when
    // that exists, else under the project, else beside the file debugged, else an open file of that name.
    String^ StopFileFor(String^ said) {
        if (String::IsNullOrEmpty(said)) return said;
        if (System::IO::File::Exists(said) && System::IO::Path::IsPathRooted(said)) return said;
        if (!System::IO::Path::IsPathRooted(said)) {
            Utf8 relative(said);
            String^ under = FromUtf8(ride_project_absolute(project_, relative.c()));
            if (under->Length > 0 && System::IO::File::Exists(under)) return under;
            if (debugSource_ != nullptr) {
                String^ beside = System::IO::Path::Combine(System::IO::Path::GetDirectoryName(debugSource_), said);
                if (System::IO::File::Exists(beside)) return beside;
            }
        }
        String^ leaf = System::IO::Path::GetFileName(said);
        for each (Sheet^ sheet in sheets_)
            if (sheet->path != nullptr &&
                String::Equals(System::IO::Path::GetFileName(sheet->path), leaf, StringComparison::OrdinalIgnoreCase))
                return sheet->path;
        return said;
    }

    void ShowStop() {

        String^ printed = Lines(FromUtf8(ride_stop_output(debugger_)));
        if (!String::IsNullOrEmpty(printed)) {
            console_->AppendText(printed);
            ShowConsoleEnd();
        }

        panel_->SelectedIndex = 2;

        if (ride_stop_exited(debugger_) != 0) {
            int status = ride_stop_status(debugger_);
            debugView_->ShowMessage(String::Format(
                "the program ran to the end and returned {0}\r\n\r\n"
                "F8 starts it again. The breakpoints are still where you put them.", status));
            debugView_->Forget();
            EndDebugging();
            what_->Text = String::Format("the program returned {0}", status);
            return;
        }

        if (ride_stop_stopped(debugger_) == 0) {
            String^ heard = Lines(FromUtf8(ride_stop_said(debugger_)));

            if (ride_stop_no_source(debugger_) != 0) {
                stopFile_ = nullptr;
                stopLine_ = 0;
                lookingFile_ = nullptr;
                lookingLine_ = 0;
                ShowStoppedLine(nullptr, -1);
                for each (Sheet^ sheet in sheets_) sheet->gutter->Invalidate();
                debugView_->ShowMessage(
                    "stopped where there is no source to show\r\n\r\n"
                    "Stepping past the end of main arrives in the code that\r\n"
                    "started it, which was not compiled here. F8 carries on to\r\n"
                    "the end, and Stop debugging leaves it.\r\n\r\n" + heard);
                what_->Text = "stopped where there is no source - F8 carries on";
                return;
            }

            debugView_->ShowMessage(String::IsNullOrEmpty(heard)
                ? "the debugger stopped answering"
                : "the debugger stopped answering\r\n\r\n" + heard);
            debugView_->Forget();
            EndDebugging();
            what_->Text = "the debugger stopped answering - see the Debug tab";
            return;
        }

        stopFile_ = StopFileFor(FromUtf8(ride_stop_file(debugger_)));
        stopLine_ = ride_stop_line(debugger_);
        String^ function = FromUtf8(ride_stop_function(debugger_));

        // The file stopped in comes to the front, whichever was there - F8 puts it in front of you.
        if (stopLine_ > 0 && !SamePath(path_, stopFile_) && System::IO::File::Exists(stopFile_))
            OpenPath(stopFile_);
        if (stopLine_ > 0 && SamePath(path_, stopFile_)) {
            GoTo(stopLine_, 1);
            ShowStoppedLine(Current(), stopLine_ - 1);
        } else {
            ShowStoppedLine(nullptr, -1);
        }

        stopFunction_ = function;
        lookingFile_ = nullptr;
        lookingLine_ = 0;
        debugView_->NewStop();
        PanelRoom(340);
        WriteDebugTab();

        for each (Sheet^ sheet in sheets_) sheet->gutter->Invalidate();
        what_->Text = String::Format("{0}:{1}{2}", System::IO::Path::GetFileName(stopFile_),
                                     stopLine_,
                                     String::IsNullOrEmpty(function) ? "" : " in " + function);
    }

    void RememberError(int line, int column, String^ message, String^ file) {
        errorLine_ = line;
        errorColumn_ = column;
        errorMessage_ = message;
        errorFile_ = file;
    }

    void OnClean(Object^, EventArgs^) {
        if (busy_) { what_->Text = StillWorking(); return; }
        // The program stopped at a breakpoint is one Clean would remove from under the debugger.
        if (ride_debugger_running(debugger_) != 0) { what_->Text = "not cleaned - the debugger is running the program; Debug > Stop debugging first"; return; }
        String^ removed = FromUtf8(ride_project_clean(project_));
        array<String^>^ files = removed->Split(gcnew array<wchar_t>{'\n'}, StringSplitOptions::RemoveEmptyEntries);
        console_->Clear();
        debugView_->Clear();
        errors_->Rows->Clear();
        assembly_->Clear();
        ForgetError();
        String^ name = ride_project_loaded(project_) != 0 ? FromUtf8(ride_project_name(project_)) : "no project";
        String^ head = "Clean succeeded: " + name + " - " +
                       (files->Length == 0 ? gcnew String("nothing was left to remove")
                                           : files->Length.ToString() + " removed");
        Say("========== " + head + " ==========\n");
        for each (String^ f in files) Say(f + "\n");
        panel_->SelectedIndex = 0;
        what_->Text = head;
    }

    void ForgetError() {
        errorLine_ = 0;
        errorColumn_ = 0;
        errorMessage_ = nullptr;
        errorFile_ = nullptr;
    }

    void GoToError() {
        if (errorMessage_ == nullptr) { what_->Text = "no error to go to"; return; }
        if (errorFile_ != nullptr && !SamePath(path_, errorFile_) &&
            System::IO::File::Exists(errorFile_))
            OpenPath(errorFile_);
        if (text_ == nullptr) { what_->Text = "the file that error is in is not open"; return; }
        GoTo(errorLine_, errorColumn_);
        what_->Text = String::Format("{0}:{1}: error: {2}", errorLine_, errorColumn_,
                                     errorMessage_);
    }

    // file:line:col (c90, cpp11, gcc, clang) or file(line,col) (cl, link): the drive letter's colon
    // is passed over because a line number has to follow the separator.
    static System::Text::RegularExpressions::Regex^ whereIs_ = gcnew System::Text::RegularExpressions::Regex(
        "^\\s*(?<file>[^:(]*(?::[\\\\/][^:(]*)?)(?::(?<line>\\d+)(?::(?<col>\\d+))?:|\\((?<line>\\d+)(?:,(?<col>\\d+))?\\)\\s*:)");

    // Enter or a double-click on the Console: the place the line under the caret names, and the
    // remembered error only when that line names none.
    void GoToConsoleLine() {
        int row = console_->GetLineFromCharIndex(console_->SelectionStart);
        String^ line = row >= 0 && row < console_->Lines->Length ? console_->Lines[row] : "";
        // A line that is a file's full name alone - Find in Files by name - opens that file.
        String^ whole = line->Trim();
        if (whole->Length > 3 && System::IO::Path::IsPathRooted(whole) && System::IO::File::Exists(whole)) {
            OpenPath(whole);
            what_->Text = whole;
            return;
        }
        System::Text::RegularExpressions::Match^ found = whereIs_->Match(line);
        if (!found->Success) { GoToError(); return; }

        String^ file = found->Groups["file"]->Value->Trim();
        if (file->Length == 0) { GoToError(); return; }
        int at = Int32::Parse(found->Groups["line"]->Value);
        int column = found->Groups["col"]->Success ? Int32::Parse(found->Groups["col"]->Value) : 1;
        if (!System::IO::Path::IsPathRooted(file)) {
            String^ nearby = errorFile_ != nullptr ? errorFile_ : path_;
            String^ beside = nearby == nullptr ? nullptr
                : System::IO::Path::Combine(System::IO::Path::GetDirectoryName(nearby), file);
            if (beside != nullptr && System::IO::File::Exists(beside)) {
                file = beside;
            } else {
                Utf8 relative(file);
                file = FromUtf8(ride_project_absolute(project_, relative.c()));
            }
        }
        if (!System::IO::File::Exists(file)) { what_->Text = "no file " + file + " to go to"; return; }
        if (!SamePath(path_, file)) OpenPath(file);
        if (text_ == nullptr || !SamePath(path_, file)) return;
        GoTo(at, column);
        what_->Text = line->Trim();
    }

    void OnConsoleKey(Object^, KeyEventArgs^ e) {
        if (e->KeyCode != Keys::Enter) return;
        e->SuppressKeyPress = true;
        GoToConsoleLine();
    }

    void OnConsoleDoubleClick(Object^, EventArgs^) { GoToConsoleLine(); }

    // The stop as grids: the stop line, Locals | Watch, the Call Stack, and the keys that work here.
    void WriteDebugTab() {
        bool shalimar = ride_debugging_shalimar(debugger_) != 0;
        String^ keys = "F8 carries on   F7 steps over   F6 steps into   F9 sets a breakpoint";
        if (!shalimar)
            keys += "   double-click a value to set it   click a frame to look at it   Ctrl+Up/Down walks the stack";
        debugView_->Fill(debugger_, StopLine(), keys);
    }

    void OnFrameChosen(int which) {
        if (busy_) { what_->Text = StillWorking(); return; }
        if (which == ride_looking_at(debugger_)) return;
        String^ no = FromUtf8(ride_cannot_walk_stack(debugger_));
        if (no->Length > 0) { what_->Text = no; WriteDebugTab(); return; }
        LookAt(which);
    }

    void OnVariableChosen(int which) {
        if (busy_) { what_->Text = StillWorking(); return; }
        if (ride_debugging_shalimar(debugger_) != 0) { what_->Text = FromUtf8(ride_cannot_watch(debugger_)); return; }
        EditVariable(which);
    }

    void OnWatchChosen(int which) {
        if (busy_) { what_->Text = StillWorking(); return; }
        EditWatch(which);
    }

    // A row of the Errors tab: its file brought to the front and the caret put on the place.
    void OnIssueChosen(String^ file, int line, int column) {
        if (String::IsNullOrEmpty(file)) file = errorFile_ != nullptr ? errorFile_ : path_;
        if (file != nullptr && !System::IO::Path::IsPathRooted(file)) {
            String^ nearby = errorFile_ != nullptr ? errorFile_ : path_;
            String^ beside = nearby == nullptr ? nullptr
                : System::IO::Path::Combine(System::IO::Path::GetDirectoryName(nearby), file);
            if (beside != nullptr && System::IO::File::Exists(beside)) {
                file = beside;
            } else {
                Utf8 relative(file);
                file = FromUtf8(ride_project_absolute(project_, relative.c()));
            }
        }
        if (file == nullptr || !System::IO::File::Exists(file)) { what_->Text = "no file " + file + " to go to"; return; }
        if (!SamePath(path_, file)) OpenPath(file);
        if (text_ == nullptr || !SamePath(path_, file)) return;
        GoTo(line, column);
        what_->Text = String::Format("{0}:{1}:{2}", System::IO::Path::GetFileName(file), line, column);
    }

    // The Errors tab from the Output tab's text, after every build; its title counts the errors.
    void CollectIssues(String^ source) {
        int count = errors_->Collect(console_->Text, source);
        errorsPage_->Text = count > 0 ? String::Format("Errors ({0})", count) : "Errors";
    }

    String^ StopLine() {
        pin_ptr<Byte> file = &Utf8Of(stopFile_)[0];
        pin_ptr<Byte> function = &Utf8Of(stopFunction_)[0];
        return FromUtf8(ride_stop_line_text(reinterpret_cast<const char*>(file), stopLine_,
                                           reinterpret_cast<const char*>(function)));
    }

    void OnWatch(Object^, EventArgs^) {

        String^ no = FromUtf8(ride_cannot_watch(debugger_));
        if (no->Length > 0) { what_->Text = no; return; }

        String^ what = Ask("watch expression", "");
        if (what == nullptr || what->Length == 0) { what_->Text = "nothing to watch"; return; }

        pin_ptr<Byte> wanted = &Utf8Of(what)[0];
        ride_watch_add(debugger_, reinterpret_cast<const char*>(wanted));
        panel_->SelectedIndex = 2;
        if (stopLine_ > 0) WriteDebugTab();
        what_->Text = ride_debugger_running(debugger_) != 0
                          ? "watching " + what
                          : "watching " + what + " - it is read when the program stops";
    }

    void EditWatch(int which) {
        String^ was = FromUtf8(ride_watch_expression(debugger_, which));
        String^ what = Ask("watch, or empty to drop it", was);
        if (what == nullptr) { what_->Text = was + " is still watched"; return; }

        pin_ptr<Byte> wanted = &Utf8Of(what)[0];
        ride_watch_set(debugger_, which, reinterpret_cast<const char*>(wanted));
        WriteDebugTab();
        what_->Text = what->Length == 0 ? "stopped watching " + was : "watching " + what;
    }

    void EditVariable(int which) {
        String^ name = FromUtf8(ride_local_name(debugger_, which));
        String^ was = FromUtf8(ride_local_value(debugger_, which));
        if (name->Length == 0) return;

        String^ value = Ask(String::Format("set {0}", name), was);
        if (value == nullptr || value->Length == 0) {
            what_->Text = String::Format("{0} is still {1}", name, was);
            return;
        }

        pin_ptr<Byte> named = &Utf8Of(name)[0];
        pin_ptr<Byte> wanted = &Utf8Of(value)[0];
        if (ride_set_variable(debugger_, reinterpret_cast<const char*>(named),
                             reinterpret_cast<const char*>(wanted)) == 0) {
            String^ complaint = FromUtf8(ride_set_complaint(debugger_));
            what_->Text = complaint->Length > 0
                              ? complaint
                              : String::Format("the debugger would not set {0}", name);
            return;
        }

        WriteDebugTab();
        what_->Text = String::Format("{0} is {1} now", name,
                                     FromUtf8(ride_local_value(debugger_, which)));
    }

    void LookAlongStack(int by) {
        int deep = ride_stack_count(debugger_);
        if (ride_debugger_running(debugger_) == 0 || deep == 0) {
            what_->Text = "nothing is stopped, so there is no stack to walk";
            return;
        }

        String^ no = FromUtf8(ride_cannot_walk_stack(debugger_));
        if (no->Length > 0) { what_->Text = no; return; }

        int looking = ride_looking_at(debugger_);
        if (by > 0) {
            if (looking + 1 >= deep) {
                what_->Text = String::Format("nothing called {0}, which is the top",
                                             FromUtf8(ride_stack_function(debugger_, deep - 1)));
                return;
            }
            LookAt(looking + 1);
            return;
        }
        if (looking == 0) {
            what_->Text = "this is where the program stopped - there is nothing below it";
            return;
        }
        LookAt(looking - 1);
    }

    void LookAt(int which) {
        if (ride_debugger_look_at(debugger_, which) == 0) {
            what_->Text = "the debugger would not go to that frame";
            return;
        }

        WriteDebugTab();

        String^ file = FromUtf8(ride_stack_file(debugger_, which));
        int at = ride_stack_line(debugger_, which);

        lookingFile_ = which == 0 ? nullptr : file;
        lookingLine_ = which == 0 ? 0 : at;

        if (file->Length > 0 && !SamePath(path_, file) && System::IO::File::Exists(file))
            OpenPath(file);
        GoTo(at, 1);
        for each (Sheet^ sheet in sheets_) sheet->gutter->Invalidate();
        what_->Text = String::Format("{0}:{1} in {2} - {3}",
                                     System::IO::Path::GetFileName(file), at,
                                     FromUtf8(ride_stack_function(debugger_, which)),
                                     which == 0 ? "back where it stopped"
                                                : "where the call came from");
    }

    void GoTo(int line, int column) {
        if (text_ == nullptr) return;
        int row = line - 1;
        int rows = LineCount(text_);
        if (row >= rows) row = rows - 1;
        if (row < 0) row = 0;

        int at = text_->GetFirstCharIndexFromLine(row) + CharacterColumn(row, column - 1);
        if (at < 0) at = 0;
        text_->Select(at, 0);
        text_->ScrollToCaret();
        Recolour();
        text_->Focus();
    }

    // The button sits at the right of the panel's tab strip, where no tab reaches.
    void PlaceFold(Object^, EventArgs^) {
        if (fold_ == nullptr) return;
        fold_->Location = System::Drawing::Point(outer_->Panel2->ClientSize.Width - fold_->Width - 4, 1);
    }

    int FoldedHeight() { return panel_->ItemSize.Height + 8; }

    void OnFold(Object^, EventArgs^) {
        if (fold_->Checked == folded_) return;
        if (fold_->Checked) {
            keptPanel_ = outer_->Height - outer_->SplitterDistance - outer_->SplitterWidth;
            folded_ = true;
            outer_->Panel2MinSize = FoldedHeight();
            outer_->SplitterDistance = Math::Max(outer_->Panel1MinSize,
                                                 outer_->Height - FoldedHeight() - outer_->SplitterWidth);
            fold_->Text = L"\u25B4";
        } else {
            folded_ = false;
            int deep = keptPanel_ > FoldedHeight() + 20 ? keptPanel_ : Math::Max(120, outer_->Height / 4);
            outer_->SplitterDistance = Math::Max(outer_->Panel1MinSize,
                                                 Math::Min(outer_->Height - 80 - outer_->SplitterWidth,
                                                           outer_->Height - deep - outer_->SplitterWidth));
            outer_->Panel2MinSize = 80;
            fold_->Text = L"\u25BE";
        }
        PlaceFold(nullptr, nullptr);
    }

    // A stop wants its grids readable: the lower panel grows to at least this height, never shrinks.
    void PanelRoom(int height) {
        if (folded_) fold_->Checked = false;
        int now = outer_->Height - outer_->SplitterDistance - outer_->SplitterWidth;
        if (now >= height) return;
        int distance = outer_->Height - height - outer_->SplitterWidth;
        if (distance >= outer_->Panel1MinSize) outer_->SplitterDistance = distance;
    }

    void ShowPanel(int which) {
        if (folded_) fold_->Checked = false;
        panel_->SelectedIndex = which;
        if (which == 0) console_->Focus();
        else if (which == 1) errors_->Focus();
        else if (which == 2) debugView_->FocusGrids();
        else assembly_->Focus();

        console_->SelectionLength = 0;
        assembly_->SelectionLength = 0;
    }

    void OnShowConsole(Object^, EventArgs^) { ShowPanel(0); }
    void OnShowErrors(Object^, EventArgs^) { ShowPanel(1); }
    void OnShowDebug(Object^, EventArgs^) { ShowPanel(2); }
    void OnShowAssembly(Object^, EventArgs^) { ShowPanel(3); }

    void SayBuild() {
        if (build_ == nullptr) return;
        int language = LanguageNow();
        int kind = ride_resolve(toolKind_, language);
        String^ said = FromUtf8(ride_language_name(language)) + "  " +
                       FromUtf8(ride_config_name(config_)) + "  " +
                       FromUtf8(ride_toolchain_name(kind));

        if (toolKind_ == RIDE_TOOL_AUTO) said += "*";

        if (ride_uses_arch(kind) != 0) said += "  " + arch_;
        build_->Text = said;

        // The menu-bar hint: just the compiler, in plain words and bold so it
        // stands out. It changes whenever the resolved compiler does. (The
        // status bar still marks an auto-chosen compiler with a star.)
        if (compilerHint_ != nullptr) {
            compilerHint_->Text = FromUtf8(ride_toolchain_name(kind));
        }
    }


    void ShowChoices() {
        for each (ToolStripMenuItem^ one in targetItems_)
            one->Checked = String::Equals(one->Text, arch_, StringComparison::Ordinal);
        toolAutoItem_->Checked = toolKind_ == RIDE_TOOL_AUTO;
        toolCc1Item_->Checked = toolKind_ == RIDE_TOOL_CC1;
        toolCxx1Item_->Checked = toolKind_ == RIDE_TOOL_CXX1;
        toolClItem_->Checked = toolKind_ == RIDE_TOOL_MSVC;
        toolShcItem_->Checked = toolKind_ == RIDE_TOOL_SHC;
        if (langAutoItem_ != nullptr) {
            Sheet^ sheet = Current();
            int chosen = sheet == nullptr ? -1 : sheet->language;
            langAutoItem_->Checked = chosen < 0;
            langCItem_->Checked = chosen == RIDE_LANG_C;
            langCppItem_->Checked = chosen == RIDE_LANG_CPP;
            langShalimarItem_->Checked = chosen == RIDE_LANG_SHALIMAR;
            langJsonItem_->Checked = chosen == RIDE_LANG_JSON;
            langTextItem_->Checked = chosen == RIDE_LANG_PLAIN;
        }
        debugConfigItem_->Checked = config_ == RIDE_CONFIG_DEBUG;
        releaseConfigItem_->Checked = config_ == RIDE_CONFIG_RELEASE;
        SayBuild();
    }

    void NextConfig() {
        if (config_ == RIDE_CONFIG_DEBUG) OnReleaseConfig(nullptr, nullptr);
        else OnDebugConfig(nullptr, nullptr);
    }

    void NextTool() {
        if (toolKind_ == RIDE_TOOL_AUTO) OnToolCc1(nullptr, nullptr);
        else if (toolKind_ == RIDE_TOOL_CC1) OnToolCxx1(nullptr, nullptr);
        else if (toolKind_ == RIDE_TOOL_CXX1) OnToolShc(nullptr, nullptr);
        else if (toolKind_ == RIDE_TOOL_SHC) OnToolCl(nullptr, nullptr);
        else OnToolAuto(nullptr, nullptr);
    }

    void NextTarget() {
        if (targetItems_ == nullptr || targetItems_->Count == 0) return;
        int at = 0;
        for (int i = 0; i < targetItems_->Count; ++i)
            if (String::Equals(targetItems_[i]->Text, arch_, StringComparison::Ordinal)) {
                at = i;
                break;
            }

        OnTarget(targetItems_[(at + 1) % targetItems_->Count], nullptr);
    }

    void OnDebugConfig(Object^, EventArgs^) {
        config_ = RIDE_CONFIG_DEBUG;
        ride_remember_configuration(config_);
        ride_project_remember_configuration(project_, config_);
        ShowChoices();
        what_->Text = "debug";
    }
    void OnReleaseConfig(Object^, EventArgs^) {
        config_ = RIDE_CONFIG_RELEASE;
        ride_remember_configuration(config_);
        ride_project_remember_configuration(project_, config_);
        ShowChoices();
        what_->Text = "release";
    }
    // The target and the compiler are the project's while one is open - written to its .pro, as
    // the manual promised - and the installation's default otherwise. Until 2026-09-19 the Target
    // menu wrote nothing and the Tools menu wrote settings.json whatever was open.
    String^ WrittenToProject(int outcome) {
        if (outcome != 0) return " - written to " + System::IO::Path::GetFileName(OutcomePath());
        return " - but " + FromUtf8(ride_outcome_message(project_));
    }
    // A CCS project's target and compiler are CCS's: its save keeps neither, and a build reloads
    // them, so a choice here would be reported written and then lost. Offered disabled, as on macOS.
    bool CcsOwnsChoices() {
        return project_ != nullptr && ride_project_loaded(project_) != 0 && ride_project_is_ccs(project_) != 0;
    }
    bool RefusedForCcs(String^ what) {
        if (!CcsOwnsChoices()) return false;
        what_->Text = "the " + what + " of a CCS project is CCS's - change it in CCS";
        return true;
    }
    void OnChoicesOpening(Object^, EventArgs^) {
        bool free = !CcsOwnsChoices() && !busy_;
        if (targetItems_ != nullptr)
            for each (ToolStripMenuItem^ one in targetItems_) one->Enabled = free;
        for each (ToolStripMenuItem^ one in gcnew array<ToolStripMenuItem^>{
                 toolAutoItem_, toolCc1Item_, toolCxx1Item_, toolShcItem_, toolClItem_})
            if (one != nullptr) one->Enabled = free;
    }
    void OnTarget(Object^ sender, EventArgs^) {
        if (RefusedForCcs("target")) return;
        arch_ = safe_cast<ToolStripMenuItem^>(sender)->Text;
        String^ said = "target: " + arch_;
        if (project_ != nullptr && ride_project_loaded(project_) != 0) {
            array<Byte>^ bytes = Utf8Of(arch_);
            pin_ptr<Byte> pinned = &bytes[0];
            said += WrittenToProject(ride_project_set_arch(project_, reinterpret_cast<const char*>(pinned)));
        }
        ShowChoices();
        RefreshDebugTab();
        what_->Text = said;
    }
    void ChooseTool(int kind, String^ said) {
        if (RefusedForCcs("compiler")) return;
        toolKind_ = kind;
        if (project_ != nullptr && ride_project_loaded(project_) != 0) {
            said += WrittenToProject(ride_project_set_toolchain(project_, kind));
        } else {
            ride_remember_default_compiler(kind);
        }
        ShowChoices();
        RefreshDebugTab();
        what_->Text = said;
    }
    void OnToolAuto(Object^, EventArgs^) { ChooseTool(RIDE_TOOL_AUTO, "compiler: chosen by the file"); }
    void OnToolCc1(Object^, EventArgs^) { ChooseTool(RIDE_TOOL_CC1, "compiler: c90"); }
    void OnToolCl(Object^, EventArgs^) { ChooseTool(RIDE_TOOL_MSVC, "compiler: cl"); }
    void OnToolCxx1(Object^, EventArgs^) { ChooseTool(RIDE_TOOL_CXX1, "compiler: cpp11"); }
    void OnToolShc(Object^, EventArgs^) { ChooseTool(RIDE_TOOL_SHC, "compiler: shalimar"); }

    void ChooseLanguage(int language, String^ said) {
        Sheet^ sheet = Current();
        if (sheet == nullptr) { what_->Text = "no file is open - the language is chosen for a file"; return; }
        sheet->language = language;
        stateGood_ = false;
        ShowChoices();
        RefreshDebugTab();
        Recolour();
        what_->Text = said;
    }
    void OnLangAuto(Object^, EventArgs^) {
        ChooseLanguage(-1, "language: chosen by the name");
    }
    void OnConvert(Object^, EventArgs^) {
        if (text_ == nullptr) { what_->Text = "no file is open"; return; }
        if (busy_) { what_->Text = StillWorking(); return; }
        ForgetError();
        if (path_ == nullptr) { what_->Text = "open a file first"; return; }

        int toShalimar = 0;
        if (ride_converts_from(LanguageNow(), &toShalimar) == 0) {
            what_->Text = "c2s converts between C and Shalimar - set the language above "
                          "if that is wrong";
            return;
        }

        if (!SaveIfChanged()) return;

        String^ converter = TakeUtf8(ride_find_converter());
        if (converter->Length == 0) {
            what_->Text = "no c2s beside this editor - build Converter-C2S here, or set C2S";
            return;
        }

        Utf8 source(path_);
        String^ produced = TakeUtf8(ride_converted_name(source.c(), toShalimar));
        if (produced->Length == 0 || SamePath(produced, path_)) {
            what_->Text = "that would write over the file it is reading";
            return;
        }

        console_->Text = "$ c2s " + (toShalimar ? "--to-shalimar " : "--to-c ") +
                         produced + "\r\n";
        panel_->SelectedIndex = 0;
        what_->Text = "converting ...";

        Job^ job = gcnew Job(Job::Convert);
        job->program = gcnew Utf8(converter);
        job->source = gcnew Utf8(path_);
        job->into = gcnew Utf8(produced);
        job->toShalimar = toShalimar;
        bool finished = WhileBusy(job);
        RIDEConversion* made = job->converted;
        delete job;
        if (made == nullptr) { what_->Text = finished ? "could not run " + converter : "stopped"; return; }

        Say(FromUtf8(ride_conversion_output(made)));
        if (!finished) {
            ride_conversion_free(made);
            what_->Text = "stopped";
            return;
        }

        if (ride_conversion_ran(made) == 0) {
            what_->Text = "could not run " + converter;
            ride_conversion_free(made);
            return;
        }

        String^ written = FromUtf8(ride_conversion_produced(made));
        int ok = ride_conversion_ok(made);
        ride_conversion_free(made);

        if (written->Length == 0) {
            what_->Text = "nothing was written - c2s could not read or write a file";
            return;
        }

        OpenPath(written);
        what_->Text = ok != 0
            ? System::IO::Path::GetFileName(written) + " - converted"
            : System::IO::Path::GetFileName(written) +
                  " - written with unconverted parts marked; search for BEYOND";
    }

    // The manual, in the browser: help\manual.html from the installation, or from the tree it was built in.
    void OnHelpContents(Object^, EventArgs^) { OpenHelpPage("manual.html", "the manual"); }
    void OnHelpShalimar(Object^, EventArgs^) { OpenHelpPage("shalimar-language.html", "the Shalimar language reference"); }
    void OnHelpResources(Object^, EventArgs^) { OpenHelpPage("resources.html", "RIDE's resources"); }

    void OpenHelpPage(String^ leaf, String^ said) {
        array<String^>^ places = gcnew array<String^>{
            System::IO::Path::Combine(AppDir(), "help"),
            System::IO::Path::Combine(Application::StartupPath, "help"),
            System::IO::Path::Combine(System::IO::Path::Combine(AppDir(), ".."), "help")};
        for each (String^ place in places) {
            String^ page = System::IO::Path::Combine(place, leaf);
            if (!System::IO::File::Exists(page)) continue;
            try {
                System::Diagnostics::ProcessStartInfo^ start = gcnew System::Diagnostics::ProcessStartInfo(page);
                start->UseShellExecute = true;
                delete System::Diagnostics::Process::Start(start);
                what_->Text = said + " - " + page;
            } catch (Exception^ problem) {
                what_->Text = "could not open " + page + " - " + problem->Message;
            }
            return;
        }
        what_->Text = said + " is not beside this editor - help\\" + leaf;
    }

    void OnLangC(Object^, EventArgs^) { ChooseLanguage(RIDE_LANG_C, "language: C"); }
    void OnLangCpp(Object^, EventArgs^) { ChooseLanguage(RIDE_LANG_CPP, "language: C++"); }
    void OnLangShalimar(Object^, EventArgs^) {
        ChooseLanguage(RIDE_LANG_SHALIMAR, "language: Shalimar");
    }
    void OnLangJson(Object^, EventArgs^) {
        ChooseLanguage(RIDE_LANG_JSON, "language: JSON");
    }
    void OnLangText(Object^, EventArgs^) {
        ChooseLanguage(RIDE_LANG_PLAIN, "language: plain text");
    }
};

void OutputToWindow(void* user, const char* bytes, int size, int stream) {
    MainForm^ form = dynamic_cast<MainForm^>(
        Runtime::InteropServices::GCHandle::FromIntPtr(IntPtr(user)).Target);
    if (form == nullptr) return;
    array<Byte>^ copy = nullptr;
    if (bytes != nullptr) {
        copy = gcnew array<Byte>(size > 0 ? size : 0);
        if (size > 0) Runtime::InteropServices::Marshal::Copy(IntPtr(const_cast<char*>(bytes)), copy, 0, size);
    }
    form->Post(copy, stream);
}

}
