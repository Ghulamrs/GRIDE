#include "menu.h"
#include "product.h"

#include "terminal.h"

namespace editor {

Menu::Menu() : active_(false), dropped_(false), column_(0), item_(0) {
    MenuColumn file;
    file.title = "File";
    file.items.push_back({"New", "", ActionNew});
    file.items.push_back({"Open...", "", ActionOpen});
    file.items.push_back({"Save", "Ctrl-S", ActionSave});
    file.items.push_back({"Save As...", "", ActionSaveAs});
    file.items.push_back({"Close", "", ActionCloseFile});
    file.items.push_back({"Quit", "Ctrl-Q", ActionQuit});
    // The last three files opened on their own; F3 and F2 still step
    // through what is open, off the menu.
    file.items.push_back(separator());
    file.items.push_back({"1. (no recent file)", "", ActionFileRecent});
    file.items.push_back({"2.", "", ActionFileRecent2});
    file.items.push_back({"3.", "", ActionFileRecent3});
    columns_.push_back(file);

    MenuColumn edit;
    edit.title = "Edit";
    edit.items.push_back({"Undo", "Ctrl-Z", ActionUndo});
    edit.items.push_back({"Redo", "Ctrl-Y", ActionRedo});
    edit.items.push_back({"Cut", "Ctrl-X", ActionCut});
    edit.items.push_back({"Copy", "Ctrl-C", ActionCopy});
    edit.items.push_back({"Paste", "Ctrl-V", ActionPaste});
    edit.items.push_back({"Select all", "", ActionSelectAll});
    edit.items.push_back({"Find...", "Ctrl-F", ActionFind});
    edit.items.push_back({"Find next", "Ctrl-G", ActionFindNext});
    edit.items.push_back({"Find previous", "", ActionFindPrevious});
    edit.items.push_back({"Replace...", "Ctrl-R", ActionReplace});
    edit.items.push_back({"Re-indent", "Ctrl-A", ActionLayOut});
    columns_.push_back(edit);

    MenuColumn project;
    project.title = "Project";

    // No Save: every change to a project is written as it is made, so a Save
    // had nothing left to do (the audit of 2026-09-19); Save as names another file.
    project.items.push_back({"New", "", ActionProjectNew});
    project.items.push_back({"Open...", "", ActionProjectOpen});
    project.items.push_back({"Save as...", "", ActionProjectSaveAs});
    project.items.push_back({"Close", "", ActionProjectClose});

    project.items.push_back(separator());
    project.items.push_back({"New File", "", ActionFileCreate});
    project.items.push_back({"Add File", "", ActionProjectAdd});
    project.items.push_back({"Remove File", "", ActionProjectRemove});
    project.items.push_back({"Rename File...", "", ActionFileRename});
    project.items.push_back({"Delete File...", "", ActionFileDelete});
    project.items.push_back({"Move to Group...", "", ActionFileRegroup});
    project.items.push_back(separator());
    project.items.push_back({"1 (no recent project)", "", ActionProjectRecent});
    project.items.push_back({"2", "", ActionProjectRecent2});
    project.items.push_back({"3", "", ActionProjectRecent3});
    columns_.push_back(project);

    MenuColumn build;
    build.title = "Build";
    build.items.push_back({"Compile file", "Ctrl-B", ActionBuild});
    build.items.push_back({"Run file", "F5", ActionRun});
    build.items.push_back({"Build project", "F4", ActionBuildProject});
    build.items.push_back({"Run project", "", ActionRunProject});
    build.items.push_back({"Debug", "Ctrl-D", ActionConfigDebug});
    build.items.push_back({"Release", "Ctrl-D", ActionConfigRelease});
    // Last, so every item above keeps its place: what a build made, removed, as the windows' Clean does.
    build.items.push_back({"Clean", "", ActionClean});
    // After Clean for the same reason: tms6747's .s on the emulator - Run is the simulator (R5) - and the two compared.
    build.items.push_back({"Emulate on vm6747", "", ActionEmulate});
    build.items.push_back({"Verify", "", ActionVerify});
    columns_.push_back(build);

    MenuColumn debug;
    debug.title = "Debug";
    debug.items.push_back({"Start / continue", "F8", ActionDebug});
    debug.items.push_back({"Debug project", "", ActionDebugProject});
    debug.items.push_back(separator());
    debug.items.push_back({"Toggle breakpoint", "F9", ActionToggleBreak});
    debug.items.push_back({"Step over", "F7", ActionStepOver});
    debug.items.push_back({"Step into", "F6", ActionStepInto});
    debug.items.push_back({"Step out", "", ActionStepOut});

    debug.items.push_back(separator());
    debug.items.push_back({"Up the stack", "Ctrl-Up", ActionFrameUp});
    debug.items.push_back({"Down the stack", "Ctrl-Down", ActionFrameDown});
    debug.items.push_back({"Watch expression...", "", ActionWatch});
    debug.items.push_back(separator());
    debug.items.push_back({"Stop debugging", "", ActionDebugStop});
    columns_.push_back(debug);

    // What is shown, in one place and in the window's order - the panes and
    // the panel's three tabs used to sit under Edit and Build.
    MenuColumn view;
    view.title = "View";
    view.items.push_back({"Project pane", "Ctrl-P", ActionToggleTree});
    view.items.push_back({"Bottom panel", "Ctrl-E", ActionTogglePanel});
    view.items.push_back(separator());
    view.items.push_back({"Console", "", ActionShowConsole});
    view.items.push_back({"Debug", "", ActionShowDebug});
    view.items.push_back({"Assembly", "", ActionShowAssembly});
    view.items.push_back(separator());
    view.items.push_back({"Line numbers", "Ctrl-L", ActionToggleNumbers});
    view.items.push_back({"Plain frame", "", ActionTogglePlain});
    columns_.push_back(view);

    MenuColumn language;
    language.title = "Language";
    language.items.push_back({"By extension", "", ActionLangAuto});
    language.items.push_back({"C", "", ActionLangC});
    language.items.push_back({"C++", "", ActionLangCpp});
    language.items.push_back({"Shalimar", "", ActionLangShalimar});
    language.items.push_back({"JSON", "", ActionLangJson});
    language.items.push_back({"Plain text", "", ActionLangText});

    language.items.push_back(separator());
    language.items.push_back({"Convert (c2s / s2c)", "", ActionConvert});
    columns_.push_back(language);

    MenuColumn tools;
    tools.title = "Tools";

    tools.items.push_back({"By language", "Ctrl-K", ActionToolAuto});
    tools.items.push_back({product::kCompilerC, "", ActionToolCc1});
    tools.items.push_back({product::kCompilerCpp, "", ActionToolCxx1});
    tools.items.push_back({product::kCompilerShalimar, "", ActionToolShc});
    tools.items.push_back({"MSVC (cl)", "", ActionToolMsvc});
    tools.items.push_back({"C++ (host)", "", ActionToolCxx});
    tools.items.push_back(separator());
    tools.items.push_back({"Header directories...", "", ActionHeaderDirs});
    tools.items.push_back({"Shared include paths...", "", ActionProjectIncludes});
    tools.items.push_back({"Shared libraries...", "", ActionProjectLibraries});
    tools.items.push_back({"Project include paths...", "", ActionOwnIncludes});
    tools.items.push_back({"Project libraries...", "", ActionOwnLibraries});
    tools.items.push_back({"Locate vcvars64.bat...", "", ActionLocateVcvars});
    tools.items.push_back({"Assembler for x86_64-windows...", "", ActionLocateAssembler});
    tools.items.push_back({"Linker for x86_64-windows...", "", ActionLocateLinker});
    tools.items.push_back({"TI compiler for tms6747...", "", ActionLocateTi});
    tools.items.push_back({"Linker for tms6747...", "", ActionLocateTilinker});
    tools.items.push_back(separator());
    tools.items.push_back({"Compiler options...", "", ActionCompilerOptions});
    columns_.push_back(tools);

    MenuColumn target;
    target.title = "Target";
    target.items.push_back({"x86_64-windows", "", ActionArchWindows});
    target.items.push_back({"x86_64-linux", "", ActionArchLinux});
    target.items.push_back({"arm64-darwin", "", ActionArchDarwin});
    target.items.push_back({"tms6747", "", ActionArchTms6747});
    columns_.push_back(target);

    MenuColumn help;
    help.title = "Help";
    help.items.push_back({"Contents", "", ActionHelpContents});
    help.items.push_back({"Keys", "F1", ActionKeys});
    help.items.push_back({"Environment", "", ActionEnvironment});
    help.items.push_back({"About", "", ActionAbout});
    columns_.push_back(help);
}

namespace {

size_t stepTo(const MenuColumn& col, const Menu& menu, size_t from, int by) {
    const size_t count = col.items.size();
    if (count == 0) return from;
    size_t at = from;
    for (size_t tried = 0; tried < count; ++tried) {
        at = (by > 0) ? (at + 1) % count : (at == 0 ? count - 1 : at - 1);
        if (menu.selectable(col.items[at])) return at;
    }
    return from;
}

size_t firstSelectable(const MenuColumn& col, const Menu& menu) {
    for (size_t i = 0; i < col.items.size(); ++i)
        if (menu.selectable(col.items[i])) return i;
    return 0;
}

size_t lastSelectable(const MenuColumn& col, const Menu& menu) {
    for (size_t i = col.items.size(); i > 0; --i)
        if (menu.selectable(col.items[i - 1])) return i - 1;
    return col.items.empty() ? 0 : col.items.size() - 1;
}

}

void Menu::open() {
    active_ = true;
    dropped_ = true;

    item_ = firstSelectable(columns_[column_], *this);
}

// Closed, the menu forgets where it was: F10 opens on File every time, so a
// key sequence means the same thing whatever was chosen last.
void Menu::close() {
    active_ = false;
    dropped_ = false;
    column_ = 0;
    item_ = 0;
}

size_t Menu::titleAt(size_t index) const {
    size_t at = 1;
    for (size_t i = 0; i < index && i < columns_.size(); ++i)
        at += columns_[i].title.size() + 3;
    return at;
}

size_t Menu::barWidth() const {
    return columns_.empty() ? 0 : titleAt(columns_.size());
}

MenuItem separator() {
    MenuItem line;
    line.rule = true;
    return line;
}

void Menu::disable(const std::vector<Action>& actions) { disabled_ = actions; }

bool Menu::disabled(Action action) const {
    for (size_t i = 0; i < disabled_.size(); ++i)
        if (disabled_[i] == action) return true;
    return false;
}

void Menu::relabel(Action action, const std::string& label) {
    for (size_t c = 0; c < columns_.size(); ++c)
        for (size_t i = 0; i < columns_[c].items.size(); ++i)
            if (columns_[c].items[i].action == action) columns_[c].items[i].label = label;
}

bool Menu::selectable(const MenuItem& item) const {
    return !item.rule && !disabled(item.action);
}

void Menu::openColumn(size_t index) {
    if (index >= columns_.size()) return;
    column_ = index;
    open();
}

Action Menu::choose(size_t index) {
    if (!active_ || index >= columns_[column_].items.size()) return ActionNone;
    const MenuItem& item = columns_[column_].items[index];
    if (!selectable(item)) return ActionNone;
    Action chosen = item.action;
    close();
    return chosen;
}

void Menu::hover(size_t index) {
    if (active_ && index < columns_[column_].items.size() && selectable(columns_[column_].items[index])) item_ = index;
}

Action Menu::key(int k) {
    if (!active_) return ActionNone;

    const MenuColumn& col = columns_[column_];

    switch (k) {
        case '\x1b':
            close();
            return ActionNone;

        case KEY_ARROW_LEFT:
            column_ = (column_ == 0) ? columns_.size() - 1 : column_ - 1;
            item_ = firstSelectable(columns_[column_], *this);
            return ActionNone;

        case KEY_ARROW_RIGHT:
            column_ = (column_ + 1) % columns_.size();
            item_ = firstSelectable(columns_[column_], *this);
            return ActionNone;

        case KEY_ARROW_UP:
            item_ = stepTo(col, *this, item_, -1);
            return ActionNone;

        case KEY_ARROW_DOWN:
            item_ = stepTo(col, *this, item_, 1);
            return ActionNone;

        case KEY_HOME:
            item_ = firstSelectable(col, *this);
            return ActionNone;

        case KEY_END:
            item_ = lastSelectable(col, *this);
            return ActionNone;

        case '\r':
        case '\n': {
            Action chosen = col.items[item_].action;
            close();
            return chosen;
        }

        default:
            break;
    }

    if (k >= 32 && k < 127) {
        // A letter goes to the next column, after the one open, whose title
        // starts with it - so T reaches Tools, and T again Target.
        char want = static_cast<char>(k);
        if (want >= 'A' && want <= 'Z') want = static_cast<char>(want - 'A' + 'a');
        for (size_t step = 1; step <= columns_.size(); ++step) {
            size_t i = (column_ + step) % columns_.size();
            char first = columns_[i].title.empty() ? 0 : columns_[i].title[0];
            if (first >= 'A' && first <= 'Z') first = static_cast<char>(first - 'A' + 'a');
            if (first == want) {
                column_ = i;
                item_ = firstSelectable(columns_[i], *this);
                return ActionNone;
            }
        }
    }

    return ActionNone;
}

}
