#ifndef EDITOR_EDITOR_H
#define EDITOR_EDITOR_H

#include <chrono>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "buffer.h"
#include "compile.h"
#include "debugger.h"
#include "find.h"
#include "indent.h"
#include "menu.h"
#include "project.h"
#include "syntax.h"
#include "toolchain.h"
#include "terminal.h"
#include "tree.h"

#include "shalimar/session.h"

namespace editor {

const size_t kTabStop = 8;

struct Frame {
    const char* across;
    const char* down;
    const char* topLeft;
    const char* topRight;
    const char* footLeft;
    const char* footRight;
    const char* teeDown;
    const char* teeUp;
    const char* teeRight;
    const char* teeLeft;

    const char* chosen;
};

extern const Frame kBoxFrame;
extern const Frame kPlainFrame;

struct Document {
    Buffer buf;
    size_t cx = 0, cy = 0, rowoff = 0, coloff = 0;
    Language lang = LangPlain;
};

class Editor {
public:
    Editor();

    void open(const std::string& path);
    void closeDocument();
    void closeProject();

    std::vector<std::string> openPaths() const;
    void switchTo(size_t index);
    void nextDocument(int by);
    void openProject(const std::string& path);

    void openFirstFile();
    void setCc1(const std::string& path) { tool_.cc1 = path; }
    void setCl(const std::string& path) { tool_.cl = path; }
    void setShc(const std::string& path) { tool_.shc = path; }

    void setConverter(const std::string& path) { c2s_ = path; }
    void setCxx(const std::string& path) { tool_.cxx = path; }
    void setCxx1(const std::string& path) { tool_.cxx1 = path; }
    void setToolchain(ToolchainKind kind) { tool_.kind = kind; }
    void setConfig(Configuration config) { config_ = config; }

    void applyLanguage();

    void setStyle(const IndentStyle& style) {
        IndentDialect dialect = style_.dialect;
        style_ = style;
        style_.dialect = dialect;
    }

    void setIndentWidth(size_t width) { style_.width = width; }
    void setTabs(bool tabs) { style_.tabs = tabs; }

    void setPlainFrame(bool plain) { frame_ = plain ? &kPlainFrame : &kBoxFrame; }
    void setCaseIndent(size_t levels) { style_.caseIndent = levels; }
    void run();

    void console(const std::string& line);
    // The build's question - the project's own tools failed it, use the vendor's? - on the status line; y is the only yes.
    bool askNativeTools(const std::string& question);

    // **Batch: the project built, and run, from a command line, with no
    // terminal** - the same buildProject the menu runs, its console printed
    // afterwards. What a script drives; a person still uses the keys.
    void setBatch(bool b) { batch_ = b; }
    bool setArchNamed(const std::string& name);
    void buildProjectBatch(bool andRun, int runner = 0) { runner_ = runner; buildProject(andRun); runner_ = 0; }
    // For --build and --run, which draw no screen: whether a project opened, and the line that said why not.
    bool projectLoaded() const { return project_.loaded(); }
    const std::string& lastMessage() const { return message_; }
    const std::vector<std::string>& consoleLines() const { return console_; }
    bool lastBuildOk() const { return lastBuildOk_; }
    int lastRunStatus() const { return lastRunStatus_; }

private:
    enum Focus { FocusText, FocusTree, FocusPanel };
    enum Tab { TabConsole, TabDebug, TabAssembly, TabCount };

    void layout();
    void scroll();
    void refresh();
    void drawMenuBar(std::string& out) const;
    void drawFrameTop(std::string& out) const;
    void drawBody(std::string& out) const;
    void drawPanel(std::string& out) const;
    void drawFrameFoot(std::string& out) const;
    void drawStatus(std::string& out) const;
    void drawMessage(std::string& out) const;
    void drawDropdown(std::string& out, std::vector<size_t>& covered) const;
    void drawDialog(std::string& out, std::vector<size_t>& covered) const;
    void placeCursor(std::string& out) const;

    std::string rule(const char* left, const char* right, const char* junction,
                     const std::string& labels, int labelColumns,
                     const std::string& tail, int tailColumns) const;

    void present(const std::vector<std::string>& rows);

    void processKey(int key);
    // **The mouse** (terminal.h's MouseEvent): what is under a click, by the same arithmetic the screen
    // is drawn with - menu bar, open menu, file tabs, project tree, gutter, text, panel tabs, panel.
    void processMouse(const MouseEvent& m);
    // In a prompt's dialog: a choice clicked is highlighted, double-clicked is taken; the wheel moves it.
    // True when the double click took it.
    bool promptMouse(const MouseEvent& m);
    // A press in the same cell as the last, within half a second: terminals report presses, not doubles.
    bool doubleClick(const MouseEvent& m);
    void dropdownBox(size_t& at, size_t& width) const;
    size_t menuTitleUnder(int col) const;
    size_t byteAtColumn(size_t row, size_t column) const;
    void cursorTo(int screenRow, int screenCol);
    std::chrono::steady_clock::time_point lastClick_;
    int lastClickRow_ = -1, lastClickCol_ = -1;
    bool dragging_ = false;
    void perform(Action action);
    void moveCursor(int key);
    void moveTree(int key);
    void movePanel(int key);
    void resizePanel(int by);
    void fitPanelTo(const std::vector<std::string>& lines);
    void cycleFocus();

    void insertChar(char c);
    void insertNewline();
    void backspace();
    void deleteForward();
    void realign();
    void undoEdit();
    bool selection(Range& range) const;
    bool selectionOn(size_t row, size_t& from, size_t& to) const;

    size_t unsaved(std::string& named) const;
    bool mayLeave();
    void extendTo(int key);
    void dropSelection() { marked_ = false; }
    bool eraseSelection();
    void copySelection(bool cut);
    void pasteClipboard();
    void selectAll();
    void redoEdit();
    void tabKey();
    void reindentAll();
    void findPrompt();
    void findAgain(bool forwards);
    void replacePrompt();

    bool save();
    void saveAs();
    void openPrompt();
    void newFile();
    void compile();
    void buildAndRun();

    void convertFile();

    void buildProject(bool andRun);
    // Emulate on vm6747 and Verify: Run again with what runs the program changed, and then put back.
    void runWith(int runner);
    Ran runChosen(int runner, const std::string& program, bool shalimar, const std::vector<std::string>& args);
    void clean();
    bool saveEveryDirty();

    void toggleBreak();

    std::string compilersNamed(const std::vector<Part>& parts) const;
    void debug(bool project);
    void debugStep(Action how);
    void debugStop();

    bool debugging() const;
    bool debuggingShalimar() const;

    std::string whereThatFileIs(const std::string& named) const;
    static void sayLines(std::vector<std::string>& into, const std::string& text);

    void showStop(const Stop& where);
    bool breakpointOn(size_t line) const;
    void openSelected();
    void goToProblem();
    void goToFrame();
    void lookAlongStack(int by);
    void lookAt(size_t which);
    void editVariable(size_t which);
    void watchExpression();
    void editWatch(size_t which);
    void writeDebugTab();

    void refreshTree();
    void applyProject();
    std::string targetFile() const;
    std::string groupUnderCursor() const;
    void createFile();
    void renameFile();
    void deleteFile();
    void regroupFile();
    void addToProject();
    void removeFromProject();
    void editProjectIncludes();
    void editOwnIncludes();
    void editOwnLibraries();
    void editProjectLibraries();
    void editHeaderDirs();
    void locateVcvars();
    void locateAssembler();
    void locateLinker();
    void locateTi();
    void locateTilinker();

    // tool_ with the header directories and the project's paths filled in,
    // which is what every build is given.
    Toolchain toolFor() const;
    void refreshRecent();

    enum PaneMode { PaneProject, PaneFiles };
    void newProject();
    void resetDebug();
    void chooseArch(size_t which);
    void chooseTool(ToolchainKind kind, const std::string& named);
    void showHelpContents();
    void showKeys();
    void showAbout();
    void showEnvironment();

    void stash();
    void restore();
    size_t findDocument(const std::string& path) const;

    std::string prompt(const std::string& text, bool& cancelled);

    std::string prompt(const std::string& text, bool& cancelled,
                       const std::vector<std::string>& choices);

    std::vector<std::string> whatIsIn(const std::string& directory) const;

    std::vector<std::string> projectsIn(const std::string& directory) const;

    void openProjectPrompt();
    void saveProjectAs();

    void dialogBox(int& at, int& top, int& wide) const;
    void say(const std::string& text) { message_ = text; }

    void sayIfSettingsWereBad();
    size_t renderCol(const std::string& line, size_t col) const;
    void clampCursor();
    const std::vector<std::string>& panelLines() const;
    // The panel wraps, so a line is not a row: these two are the arithmetic
    // every scroll and every auto-scroll has to do instead of counting rows.
    size_t panelLinesShowing(size_t from) const;
    size_t panelTopForEnd() const;

    Terminal term_;

    Buffer buf_;
    std::vector<Document> docs_;
    size_t doc_;
    Menu menu_;
    Tree tree_;
    Project project_;
    std::string projectDir_;
    IndentStyle style_;

    Language langChoice_ = LangCount;
    Toolchain tool_;
    // True until main.cpp has chosen what to open, so the start-up project does not open a file the command line will.
    bool starting_ = true;

    size_t cx_, cy_, rx_;
    size_t rowoff_, coloff_;

    size_t treeSel_, treeOff_;
    bool treeOpen_;
    PaneMode paneMode_;

    std::vector<std::string> console_;
    std::vector<std::string> debug_;
    std::vector<std::string> assembly_;
    Diagnostic lastDiag_;
    size_t panelOff_;
    bool panelOpen_;
    Tab tab_;

    Focus focus_;
    Language lang_;
    std::string c2s_;
    Configuration config_;
    size_t arch_;
    bool batch_ = false;
    bool lastBuildOk_ = false;
    int lastRunStatus_ = 0;
    // What a Run runs: 0 the program (or the emulator), 1 the simulator on the .out, 2 both compared.
    int runner_ = 0;
    bool numbers_;

    bool menuItemIsCurrent(Action action) const;

    void openMenu();
    bool needsDraw_;

    struct FileBreaks {
        std::string path;
        std::set<size_t> lines;
    };
    Debugger debugger_;

    shalimar::Session shm_;
    std::map<std::string, FileBreaks> breaks_;
    Built debugBuilt_;

    bool debugTemporary_;
    std::string stopFile_;
    size_t stopLine_;
    std::vector<Variable> locals_;
    std::vector<StackFrame> stack_;
    std::string stopFunction_;
    size_t looking_;

    bool marked_;
    size_t markRow_, markCol_;
    std::string clipboard_;

    std::string needle_;
    std::string message_;
    int quitConfirm_;
    bool running_;

    const Frame* frame_;

    int screenRows_, screenCols_;
    int bodyRows_, panelRows_;
    // What the panel was asked for, as against what the screen allows.
    // layout() clamps the second from the first every draw, so a small
    // terminal shrinks the panel without forgetting how tall it should be.
    int panelWanted_;
    int treeCols_, sourceCols_, gutterCols_;

    std::vector<std::string> painted_;
    int paintedCols_;

    std::string askTitle_;
    std::string askAnswer_;

    std::vector<std::string> askShown_;
    size_t askChoice_;
};

}

#endif
