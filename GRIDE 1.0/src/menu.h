#ifndef EDITOR_MENU_H
#define EDITOR_MENU_H

#include <cstddef>
#include <string>
#include <vector>

namespace editor {

enum Action {
    ActionNone = 0,
    ActionNew,
    ActionOpen,
    ActionSave,
    ActionSaveAs,
    ActionQuit,
    ActionCloseFile,
    ActionNextFile,
    ActionPrevFile,
    ActionUndo,
    ActionCut,
    ActionCopy,
    ActionPaste,
    ActionSelectAll,
    ActionRedo,
    ActionLayOut,
    ActionFind,
    ActionFindNext,
    ActionFindPrevious,
    ActionReplace,
    ActionToggleTree,
    ActionTogglePanel,
    ActionToggleNumbers,
    ActionTogglePlain,
    ActionProjectNew,
    ActionProjectOpen,
    ActionProjectSaveAs,
    ActionProjectClose,
    ActionProjectAdd,

    ActionProjectRemove,
    ActionFileCreate,

    ActionFileRename,
    ActionFileDelete,
    ActionFileRegroup,
    ActionBuild,
    ActionRun,

    ActionBuildProject,
    ActionRunProject,
    ActionClean,
    ActionToggleBreak,
    ActionDebug,
    ActionDebugProject,
    ActionStepOver,
    ActionStepInto,
    ActionStepOut,
    ActionFrameUp,
    ActionFrameDown,
    ActionWatch,
    ActionDebugStop,
    ActionConfigDebug,
    ActionConfigRelease,
    ActionShowConsole,
    ActionShowDebug,
    ActionShowAssembly,
    ActionArchWindows,
    ActionArchLinux,
    ActionArchDarwin,
    ActionArchTms6747,
    ActionLangAuto,
    ActionLangC,
    ActionLangCpp,
    ActionLangShalimar,
    ActionLangJson,
    ActionLangText,
    ActionToolAuto,
    ActionToolCc1,
    ActionToolMsvc,
    ActionToolShc,
    ActionToolCxx,
    ActionToolCxx1,
    ActionHelpContents,
    ActionKeys,
    ActionAbout,
    ActionEnvironment,

    ActionConvert,

    // Header directories and libraries: the installation's two, and a
    // project's own. Last, so the session suite's counts of menu items above
    // them hold.
    ActionProjectIncludes,
    ActionProjectLibraries,
    ActionHeaderDirs,
    ActionLocateVcvars,
    ActionLocateAssembler,
    ActionLocateTi,
    ActionLocateLinker,
    ActionLocateTilinker,
    ActionProjectRecent,
    ActionProjectRecent2,
    ActionProjectRecent3,
    ActionFileRecent,
    ActionFileRecent2,
    ActionFileRecent3,
    // The open project's own header directories and libraries, in its .pro
    // - beside the installation's two above, which every project shares.
    ActionOwnIncludes,
    ActionOwnLibraries,
    // The tabbed Compiler Options dialog (options.h) - the windows' alone; the terminal shows it disabled.
    ActionCompilerOptions,
    // Build > Emulate on vm6747 and Build > Verify: tms6747's assembly on vm6747 (Run is sim6747's, R5),
    // and the two runs compared.
    ActionEmulate,
    ActionVerify
};

struct MenuItem {
    std::string label;
    std::string key;
    Action action;

    bool rule;

    MenuItem(const std::string& text = std::string(),
             const std::string& shortcut = std::string(),
             Action what = ActionNone)
        : label(text), key(shortcut), action(what), rule(false) {}
};

MenuItem separator();

struct MenuColumn {
    std::string title;
    std::vector<MenuItem> items;
};

class Menu {
public:
    Menu();

    const std::vector<MenuColumn>& columns() const { return columns_; }

    bool active() const { return active_; }
    bool dropped() const { return active_ && dropped_; }
    size_t column() const { return column_; }
    size_t item() const { return item_; }

    void open();
    void close();

    size_t titleAt(size_t index) const;
    size_t barWidth() const;

    Action key(int k);
    // The mouse's two: a column's title clicked opens it, an item clicked is chosen - ActionNone, and
    // the menu left open, for a rule or a disabled item. hover moves the highlight without choosing.
    void openColumn(size_t index);
    Action choose(size_t index);
    void hover(size_t index);

    void disable(const std::vector<Action>& actions);
    // Relabel the item carrying an action - the recent projects, by name.
    void relabel(Action action, const std::string& label);
    bool disabled(Action action) const;

    bool selectable(const MenuItem& item) const;

private:
    std::vector<MenuColumn> columns_;
    bool active_;
    bool dropped_;
    size_t column_;
    size_t item_;
    std::vector<Action> disabled_;
};

}

#endif
