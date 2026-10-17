#include "editor.h"
#include "product.h"

#include "about.h"
#include "convert.h"
#include "help.h"
#include "utf8.h"
#include "symbols.h"
#include "workspace.h"
#include "path.h"
#include "settings.h"
#include "ccs/ccsproject.h"
#include "ccs/ccsworkspace.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <system_error>

namespace editor {

namespace {

const int kTreeWidth = 22;
const int kPanelRows = 7;

const char* const kPlainAcross = "-";
const char* const kPlainDown   = "|";
const char* const kPlainJoint  = "+";

const char* const kAcross    = "\xe2\x94\x80";
const char* const kDown      = "\xe2\x94\x82";
const char* const kTopLeft   = "\xe2\x94\x8c";
const char* const kTopRight  = "\xe2\x94\x90";
const char* const kFootLeft  = "\xe2\x94\x94";
const char* const kFootRight = "\xe2\x94\x98";
const char* const kTeeDown   = "\xe2\x94\xac";
const char* const kTeeUp     = "\xe2\x94\xb4";
const char* const kTeeRight  = "\xe2\x94\x9c";
const char* const kTeeLeft   = "\xe2\x94\xa4";

}

const Frame kBoxFrame = {kAcross, kDown, kTopLeft, kTopRight, kFootLeft, kFootRight,
                         kTeeDown, kTeeUp, kTeeRight, kTeeLeft, "\xe2\x80\xa2"};
const Frame kPlainFrame = {kPlainAcross, kPlainDown, kPlainJoint, kPlainJoint,
                           kPlainJoint, kPlainJoint, kPlainJoint, kPlainJoint,
                           kPlainJoint, kPlainJoint, "*"};

namespace {

std::string repeated(const char* one, int times) {
    std::string out;
    for (int i = 0; i < times; ++i) out += one;
    return out;
}

void wrapInto(std::vector<std::string>& lines, const std::string& text, size_t width) {
    if (width < 20) width = 20;
    std::string line;
    size_t at = 0;
    while (at <= text.size()) {
        size_t space = text.find(' ', at);
        std::string word = text.substr(at, space == std::string::npos ? std::string::npos
                                                                      : space - at);
        if (!line.empty() && line.size() + 1 + word.size() > width) {
            lines.push_back(line);
            line.clear();
        }
        if (!line.empty()) line += ' ';
        line += word;
        if (space == std::string::npos) break;
        at = space + 1;
    }
    if (!line.empty()) lines.push_back(line);
}

std::string window(const std::string& s, size_t from, size_t width) {
    std::string out;
    if (from < s.size()) out = s.substr(from, width);
    out.resize(width, ' ');
    return out;
}

std::string number(size_t n) { return std::to_string(n); }

std::string lineCountText(size_t n) {
    return std::to_string(n) + (n == 1 ? " line" : " lines");
}

size_t digitsIn(size_t n) {
    size_t d = 1;
    while (n >= 10) { n /= 10; ++d; }
    return d;
}

void expandWithKinds(const std::string& s, const std::vector<unsigned char>& kinds,
                     std::string& text, std::vector<unsigned char>& out) {
    size_t column = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char k = i < kinds.size() ? kinds[i] : KindNormal;

        if (s[i] == '\t') {
            do {
                text += ' ';
                out.push_back(k);
                ++column;
            } while (column % kTabStop != 0);
            ++i;
            continue;
        }

        size_t step = utf8::next(s, i);
        if (step <= i) step = i + 1;
        for (size_t b = i; b < step && b < s.size(); ++b) {
            text += s[b];
            out.push_back(k);
        }
        column += utf8::widthOf(utf8::codePointAt(s, i));
        i = step;
    }
}

// What a line takes on screen, in columns, counted the way expandWithKinds
// lays it out: tabs to the next stop, and a wide character as two.
size_t shownWidth(const std::string& s) {
    size_t column = 0;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\t') {
            do { ++column; } while (column % kTabStop != 0);
            ++i;
            continue;
        }
        size_t step = utf8::next(s, i);
        if (step <= i) step = i + 1;
        column += utf8::widthOf(utf8::codePointAt(s, i));
        i = step;
    }
    return column;
}

// **The panel wraps rather than cutting, and that is why this exists.** A compiler's line runs
// past ninety columns - c2s's questions about a preprocessor directive are the longest - and the
// panel is fifty-odd beside an open project pane; nothing scrolls sideways, so the half that said what to fix was unreadable. One line is one or more rows now.
size_t rowsForLine(const std::string& line, size_t cols) {
    if (cols == 0) return 1;
    const size_t wide = shownWidth(line);
    return wide <= cols ? 1 : (wide + cols - 1) / cols;
}

std::string colouredWindow(const std::string& text,
                           const std::vector<unsigned char>& kinds,
                           size_t from, size_t width,
                           size_t selFrom, size_t selTo) {
    std::string out;
    size_t column = 0;
    size_t drawn = 0;
    int current = -1;
    bool inverted = false;

    for (size_t i = 0; i < text.size();) {
        size_t step = utf8::next(text, i);
        if (step <= i) step = i + 1;
        size_t wide = utf8::widthOf(utf8::codePointAt(text, i));

        if (column + wide <= from) {
            column += wide;
            i = step;
            continue;
        }
        if (drawn + wide > width) break;

        bool wanted = (column >= selFrom && column < selTo);
        if (wanted != inverted) {
            out += wanted ? "\x1b[7m" : "\x1b[27m";
            inverted = wanted;
        }

        unsigned char k = i < kinds.size() ? kinds[i] : KindNormal;
        if (static_cast<int>(k) != current) {
            out += "\x1b[";
            out += colourFor(k);
            out += "m";
            current = k;
        }
        for (size_t b = i; b < step && b < text.size(); ++b) out += text[b];

        column += wide;
        drawn += wide;
        i = step;
    }

    if (inverted) out += "\x1b[27m";
    if (current != -1 && current != KindNormal) out += "\x1b[39m";
    if (drawn < width) out.append(width - drawn, ' ');
    return out;
}

size_t leadingSpace(const std::string& line) {
    size_t n = 0;
    while (n < line.size() && (line[n] == ' ' || line[n] == '\t')) ++n;
    return n;
}

bool endsALabel(const std::string& before) {
    std::string t = before.substr(leadingSpace(before));
    if (t.compare(0, 5, "case ") == 0 || t == "default") return true;
    if (t.empty() || (t[0] >= '0' && t[0] <= '9')) return false;
    for (size_t i = 0; i < t.size(); ++i) {
        char ch = t[i];
        bool ident = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                     (ch >= '0' && ch <= '9') || ch == '_';
        if (!ident) return false;
    }
    return true;
}

std::string baseName(const std::string& path) {
    size_t at = path.find_last_of("/\\");
    return at == std::string::npos ? path : path.substr(at + 1);
}

void consoleSink(void* context, const std::string& line) {
    static_cast<Editor*>(context)->console(line);
}

// The question a build asks when the project's own tools failed it - see
// compile.h - as the console front end puts it.
bool askNativeOnConsole(void* context, const std::string& question) {
    return static_cast<Editor*>(context)->askNativeTools(question);
}

}

Editor::Editor()
    : docs_(1), doc_(0),
      cx_(0), cy_(0), rx_(0), rowoff_(0), coloff_(0),
      treeSel_(0), treeOff_(0), treeOpen_(true), paneMode_(PaneProject),
      panelOff_(0), panelOpen_(true), tab_(TabConsole),
      focus_(FocusText), lang_(LangPlain), config_(ConfigDebug), arch_(0), numbers_(true), needsDraw_(true), debugTemporary_(true),
      stopLine_(0),
      looking_(0),
      marked_(false), markRow_(0), markCol_(0),
      quitConfirm_(0), running_(true),
      screenRows_(24), screenCols_(80),
      bodyRows_(14), panelRows_(kPanelRows), panelWanted_(kPanelRows),
      treeCols_(kTreeWidth), sourceCols_(80), gutterCols_(4), paintedCols_(0),
      askChoice_(0) {
    refreshRecent();
    frame_ = &kBoxFrame;

    for (size_t i = 0; i < kArchCount; ++i)
        if (std::string(kArches[i]) == hostArch()) arch_ = i;

    const char* fromEnv = std::getenv("C90");
    if (fromEnv && *fromEnv) {
        tool_.cc1 = fromEnv;
    } else {

        // c90 since 3.5, the compiler that carries tms6747; a cc1 beside
        // the editor still serves for the three host targets.
        std::string beside = path::besideProgram("c90.exe");
        if (beside.empty()) beside = path::besideProgram("c90");
        if (beside.empty()) beside = path::besideProgram("cc1.exe");
        if (beside.empty()) beside = path::besideProgram("cc1");
        if (!beside.empty()) tool_.cc1 = beside;
    }

    // cxx1 the same way: named, beside this program, or left to PATH.
    const char* cxx1FromEnv = std::getenv("CPP11");
    if (cxx1FromEnv && *cxx1FromEnv) {
        tool_.cxx1 = cxx1FromEnv;
    } else {
        std::string beside = path::besideProgram("cpp11.exe");
        if (beside.empty()) beside = path::besideProgram("cpp11");
        if (beside.empty()) beside = path::besideProgram("cxx1.exe");
        if (beside.empty()) beside = path::besideProgram("cxx1");
        if (!beside.empty()) tool_.cxx1 = beside;
    }

    const char* shcFromEnv = std::getenv("SHALIMAR");
    if (shcFromEnv && *shcFromEnv) {
        tool_.shc = shcFromEnv;
    } else {
        // shalimar since 3.5, the VM6747 line; a shc beside the editor still
        // serves, being the same compiler.
        std::string beside = path::besideProgram("shalimar.exe");
        if (beside.empty()) beside = path::besideProgram("shalimar");
        if (beside.empty()) beside = path::besideProgram("shc.exe");
        if (beside.empty()) beside = path::besideProgram("shc");
        if (!beside.empty()) tool_.shc = beside;
    }

    console_.push_back("Compiler output appears here.  Ctrl-B builds.");
    resetDebug();
}

void Editor::stash() {
    docs_[doc_].buf = buf_;
    docs_[doc_].cx = cx_;
    docs_[doc_].cy = cy_;
    docs_[doc_].rowoff = rowoff_;
    docs_[doc_].coloff = coloff_;
    docs_[doc_].lang = lang_;
}

void Editor::restore() {
    buf_ = docs_[doc_].buf;
    cx_ = docs_[doc_].cx;
    cy_ = docs_[doc_].cy;
    rowoff_ = docs_[doc_].rowoff;
    coloff_ = docs_[doc_].coloff;
    lang_ = docs_[doc_].lang;
}

size_t Editor::findDocument(const std::string& path) const {
    std::string want = path::oneName(path);

    for (size_t i = 0; i < docs_.size(); ++i) {
        const std::string& have = (i == doc_) ? buf_.path() : docs_[i].buf.path();
        if (have.empty()) continue;
        if (path::oneName(have) == want) return i;
    }
    return docs_.size();
}

void Editor::switchTo(size_t index) {
    if (index >= docs_.size() || index == doc_) return;
    stash();
    doc_ = index;
    restore();
    focus_ = FocusText;
}

void Editor::nextDocument(int by) {
    if (docs_.size() < 2) { say("only one file is open"); return; }
    size_t at = (doc_ + docs_.size() + static_cast<size_t>(by)) % docs_.size();
    switchTo(at);
    say(buf_.path().empty() ? std::string("[no name]") : buf_.path());
}

size_t Editor::unsaved(std::string& named) const {
    size_t count = 0;
    named.clear();
    for (size_t i = 0; i < docs_.size(); ++i) {

        const Buffer& b = (i == doc_) ? buf_ : docs_[i].buf;
        if (!b.dirty()) continue;
        if (named.empty())
            named = b.path().empty() ? std::string("[no name]") : baseName(b.path());
        ++count;
    }
    return count;
}

bool Editor::mayLeave() {
    std::string named;
    size_t count = unsaved(named);
    if (count == 0 || quitConfirm_ != 0) return true;

    quitConfirm_ = 1;
    if (count == 1)
        say("unsaved changes in " + named + " - press Ctrl-Q again to leave them behind");
    else
        say(named + " and " + number(count - 1) + " other" + (count == 2 ? "" : "s") +
            " have unsaved changes - press Ctrl-Q again to leave them behind");
    return false;
}

void Editor::closeDocument() {
    if (buf_.dirty()) {
        say("unsaved changes - save it, or Ctrl-Q twice to leave");
        return;
    }
    stash();
    docs_.erase(docs_.begin() + static_cast<long>(doc_));

    if (docs_.empty()) docs_.push_back(Document());
    if (doc_ >= docs_.size()) doc_ = docs_.size() - 1;
    restore();

    refreshTree();

    console_.clear();
    say("closed");
}

void Editor::open(const std::string& path) {
    size_t already = findDocument(path);
    if (already < docs_.size()) {
        switchTo(already);
        say(path + " - already open");
        return;
    }

    bool reuse = buf_.path().empty() && !buf_.dirty() && buf_.lineCount() == 1 &&
                 buf_.line(0).empty();
    if (!reuse) {
        stash();
        docs_.push_back(Document());
        doc_ = docs_.size() - 1;
        restore();
    }

    std::string error;
    switch (buf_.load(path, error)) {
        case Buffer::Opened:  say(path + "  " + lineCountText(buf_.lineCount())); break;
        case Buffer::NewFile: say(path + "  new file"); break;
        case Buffer::Failed:  say(error); break;
    }
    cx_ = cy_ = rowoff_ = coloff_ = 0;
    applyLanguage();

    refreshTree();

    size_t at = tree_.find(path);
    if (at < tree_.size()) treeSel_ = at;
}

void Editor::applyProject() {
    if (!project_.loaded()) return;

    style_ = project_.indent();
    // The project's compiler when it names one; on automatic, the
    // installation's default from settings.json stands.
    tool_.kind = project_.toolchain() != ToolAuto ? project_.toolchain()
                                                   : toolchainFrom(settings::defaultCompiler());
    for (size_t i = 0; i < kArchCount; ++i)
        if (project_.arch() == kArches[i]) arch_ = i;
}

// The project's own choice of file first, then the one that defines main,
// then the first file there is - see Project::fileToOpen. Start-up only:
// openProject does the same itself once the editor is running.
void Editor::openFirstFile() {
    if (!buf_.path().empty()) return;

    std::string chosen = project_.fileToOpen();
    if (chosen.empty()) return;

    std::string already = message_;
    open(project_.absolute(chosen));
    if (!already.empty()) say(already);
}

void Editor::refreshTree() {
    std::vector<std::string> open = openPaths();
    if (project_.loaded() && paneMode_ == PaneProject) {
        tree_.showProject(project_);
    } else {
        if (open.empty()) tree_.clear(); else tree_.showOpenFiles(open);
    }
    if (treeSel_ >= tree_.size()) treeSel_ = tree_.size() ? tree_.size() - 1 : 0;
}

std::vector<std::string> Editor::openPaths() const {
    std::vector<std::string> paths;
    for (size_t i = 0; i < docs_.size(); ++i) {

        const Buffer& b = (i == doc_) ? buf_ : docs_[i].buf;

        paths.push_back(b.path());
    }
    return paths;
}

void Editor::closeProject() {
    if (!project_.loaded()) { say("there is no project open"); return; }

    std::string was = project_.name();
    editor::rememberOpen(project_, buf_.path());

    // The project's files go with it - every document it lists that is
    // open; one merely under its directory stays - and one with unsaved
    // changes keeps the project open.
    stash();
    for (size_t i = 0; i < docs_.size(); ++i) {
        const std::string& at = docs_[i].buf.path();
        if (!at.empty() && project_.groupOf(project_.relative(at)) < project_.groups().size() &&
            docs_[i].buf.dirty()) {
            restore();
            say("not closed - " + path::filename(at) + " has unsaved changes");
            return;
        }
    }
    size_t closed = 0;
    for (size_t i = 0; i < docs_.size();) {
        const std::string& at = docs_[i].buf.path();
        if (!at.empty() && project_.groupOf(project_.relative(at)) < project_.groups().size()) {
            docs_.erase(docs_.begin() + static_cast<long>(i));
            ++closed;
        } else {
            ++i;
        }
    }
    if (docs_.empty()) docs_.push_back(Document());
    doc_ = docs_.size() - 1;
    for (size_t i = 0; i < docs_.size(); ++i)
        if (!buf_.path().empty() && docs_[i].buf.path() == buf_.path()) { doc_ = i; break; }
    restore();
    project_.close();

    paneMode_ = PaneFiles;
    refreshTree();
    treeSel_ = 0;
    treeOff_ = 0;
    console_.clear();
    say(was + " closed" + (closed ? ", and its " + std::to_string(closed) + " file(s) with it" : ""));
}

bool Editor::setArchNamed(const std::string& name) {
    for (size_t i = 0; i < kArchCount; ++i)
        if (name == kArches[i]) { arch_ = i; return true; }
    return false;
}

void Editor::openProject(const std::string& path) {
    projectDir_ = path;

    std::string error;
    if (project_.load(path, error)) {
        applyProject();

        paneMode_ = PaneProject;
        refreshTree();

        settings::rememberProject(project_.file());
        refreshRecent();

        size_t howMany = project_.groups().size();
        std::string said = "ready - " + project_.name() + ", " + number(howMany) +
                           (howMany == 1 ? " group" : " groups");

        size_t named = Project::projectFilesIn(project_.root()).size();
        if (named > 1)
            said += " - " + number(named) + " projects here, Project > Open chooses";
        say(said);
        sayIfSettingsWereBad();
        // A CCS project: its remembered configuration, and the line naming what of it RIDE
        // cannot honour - on the console, so that it is read rather than overwritten by the next message.
        if (project_.isCcs()) {
            int remembered = project_.ccsConfiguration();
            if (remembered >= 0) config_ = static_cast<Configuration>(remembered);
            std::vector<std::string> lines = project_.ccsReport(config_);
            for (size_t i = 0; i < lines.size(); ++i) console_.push_back(lines[i]);
            say(lines.empty() ? said : lines[0]);
        }

        // The project's own file comes to the front - the one it names,
        // else the one defining main, else the first - whichever way the
        // project was opened; at start-up, main.cpp asks the same.
        if (!starting_) {
            std::string chosen = project_.fileToOpen();
            if (!chosen.empty()) {
                std::string kept = message_;
                open(project_.absolute(chosen));
                say(kept);
            }
        }
    } else if (error.empty()) {

        Outcome made = beginFromWhatIsThere(project_, path);
        if (made.ok) {
            applyProject();
            refreshTree();
            settings::rememberProject(project_.file());
        refreshRecent();
            say(made.message);
        } else {
            tree_.setRoot(path);
            project_.setRoot(tree_.root());
            say(made.message);
        }
    } else {

        tree_.setRoot(path);
        project_.setRoot(tree_.root());
        say(error);
    }
    treeSel_ = 0;
    treeOff_ = 0;
}

void Editor::console(const std::string& line) {
    console_.push_back(line);

    if (panelOpen_ && tab_ == TabConsole) panelOff_ = panelTopForEnd();
    refresh();
}

// How many lines are visible from `from` down, wrapping counted. At least
// one, so that a line longer than the whole panel still scrolls past.
size_t Editor::panelLinesShowing(size_t from) const {
    const std::vector<std::string>& lines = panelLines();
    const size_t cols = static_cast<size_t>(screenCols_ > 2 ? screenCols_ - 2 : 1);
    size_t rows = 0, count = 0;
    for (size_t i = from; i < lines.size() && rows < static_cast<size_t>(panelRows_); ++i) {
        rows += rowsForLine(lines[i], cols);
        ++count;
    }
    return count ? count : 1;
}

// The line to start at so that the last one is on screen - which is not
// size() - panelRows_ once lines wrap, and was wrong by however many rows the
// tail had wrapped onto.
size_t Editor::panelTopForEnd() const {
    const std::vector<std::string>& lines = panelLines();
    if (lines.empty()) return 0;
    const size_t cols = static_cast<size_t>(screenCols_ > 2 ? screenCols_ - 2 : 1);
    size_t rows = 0, top = lines.size();
    while (top > 0) {
        const size_t next = rows + rowsForLine(lines[top - 1], cols);
        if (next > static_cast<size_t>(panelRows_) && top < lines.size()) break;
        rows = next;
        --top;
    }
    return top;
}

const std::vector<std::string>& Editor::panelLines() const {
    if (tab_ == TabConsole) return console_;
    if (tab_ == TabDebug) return debug_;
    return assembly_;
}

void Editor::layout() {
    term_.size(screenRows_, screenCols_);
    if (screenRows_ < 8) screenRows_ = 8;
    if (screenCols_ < 40) screenCols_ = 40;

    int taken = 5;
    panelRows_ = panelWanted_;
    if (panelOpen_) {

        int room = screenRows_ - taken - 4;
        if (panelRows_ > room) panelRows_ = room;
        if (panelRows_ < 2) panelRows_ = 2;
        taken += panelRows_ + 1;
    }
    bodyRows_ = screenRows_ - taken;

    while (bodyRows_ < 1 && panelOpen_ && panelRows_ > 1) {
        --panelRows_;
        ++bodyRows_;
    }
    if (bodyRows_ < 1) bodyRows_ = 1;

    if (treeOpen_) {
        treeCols_ = kTreeWidth;
        if (treeCols_ > screenCols_ / 3) treeCols_ = screenCols_ / 3;
        sourceCols_ = screenCols_ - treeCols_ - 3;
    } else {
        treeCols_ = 0;
        sourceCols_ = screenCols_ - 2;
    }

    gutterCols_ = 0;
    if (numbers_) {
        gutterCols_ = static_cast<int>(digitsIn(buf_.lineCount())) + 1;
        if (gutterCols_ < 4) gutterCols_ = 4;
        sourceCols_ -= gutterCols_;
    }
    if (sourceCols_ < 10) sourceCols_ = 10;
}

size_t Editor::renderCol(const std::string& line, size_t col) const {
    size_t r = 0;
    for (size_t i = 0; i < col && i < line.size();) {
        if (line[i] == '\t') {
            r += kTabStop - (r % kTabStop);
            ++i;
            continue;
        }
        r += utf8::widthOf(utf8::codePointAt(line, i));
        size_t step = utf8::next(line, i);
        i = (step > i) ? step : i + 1;
    }
    return r;
}

void Editor::clampCursor() {
    if (cy_ >= buf_.lineCount()) cy_ = buf_.lineCount() - 1;
    if (cx_ > buf_.line(cy_).size()) cx_ = buf_.line(cy_).size();

    cx_ = utf8::startOf(buf_.line(cy_), cx_);
}

void Editor::scroll() {
    rx_ = renderCol(buf_.line(cy_), cx_);

    if (cy_ < rowoff_) rowoff_ = cy_;
    if (cy_ >= rowoff_ + static_cast<size_t>(bodyRows_))
        rowoff_ = cy_ - static_cast<size_t>(bodyRows_) + 1;

    if (rx_ < coloff_) coloff_ = rx_;
    if (rx_ >= coloff_ + static_cast<size_t>(sourceCols_))
        coloff_ = rx_ - static_cast<size_t>(sourceCols_) + 1;

    if (treeSel_ < treeOff_) treeOff_ = treeSel_;
    if (treeSel_ >= treeOff_ + static_cast<size_t>(bodyRows_))
        treeOff_ = treeSel_ - static_cast<size_t>(bodyRows_) + 1;
}

void Editor::drawMenuBar(std::string& out) const {
    std::string bar(static_cast<size_t>(screenCols_), ' ');
    for (size_t i = 0; i < menu_.columns().size(); ++i) {
        const std::string& title = menu_.columns()[i].title;
        size_t at = menu_.titleAt(i);
        for (size_t j = 0; j < title.size() && at + j < bar.size(); ++j)
            bar[at + j] = title[j];
    }

    out += "\x1b[7m";

    if (menu_.active()) {
        size_t at = menu_.titleAt(menu_.column());
        size_t len = menu_.columns()[menu_.column()].title.size();
        out += bar.substr(0, at);
        out += "\x1b[m";
        out += bar.substr(at, len);
        out += "\x1b[7m";
        out += bar.substr(at + len);
    } else {
        out += bar;
    }
    out += "\x1b[m\r\n";
}

std::string Editor::rule(const char* left, const char* right, const char* junction,
                         const std::string& labels, int labelColumns,
                         const std::string& tail, int tailColumns) const {
    std::string out = left;
    int used = 1;
    if (treeOpen_ && junction != 0) {
        out += repeated(frame_->across, treeCols_);
        out += junction;
        used += treeCols_ + 1;
    }

    int room = screenCols_ - used - 1;
    if (room < 0) room = 0;

    if (labelColumns > 0 && labelColumns + 1 <= room) {
        out += frame_->across;
        out += labels;
        room -= labelColumns + 1;
    }
    if (tailColumns > 0 && tailColumns + 2 <= room) {
        out += repeated(frame_->across, room - tailColumns - 1);
        out += tail;
        out += frame_->across;
        room = 0;
    }
    out += repeated(frame_->across, room);
    out += right;
    return out;
}

void Editor::drawFrameTop(std::string& out) const {

    int room = screenCols_ - 2 - (treeOpen_ ? treeCols_ + 1 : 0);
    std::string tabs;
    int wide = 0;

    for (size_t i = 0; i < docs_.size(); ++i) {
        const Buffer& b = (i == doc_) ? buf_ : docs_[i].buf;
        std::string name = b.path().empty() ? std::string("[no name]") : baseName(b.path());
        if (b.dirty()) name += "*";
        std::string cell = " " + name + " ";
        int cellWide = static_cast<int>(utf8::columns(cell, cell.size()));

        if (wide + cellWide + 2 > room) break;
        if (i == doc_) {
            tabs += "\x1b[7m";
            tabs += cell;
            tabs += "\x1b[m";
        } else {
            tabs += "\x1b[90m";
            tabs += cell;
            tabs += "\x1b[39m";
        }
        tabs += frame_->across;
        wide += cellWide + 1;
    }

    out += rule(frame_->topLeft, frame_->topRight, frame_->teeDown, tabs, wide, std::string(), 0);
    out += "\x1b[K\r\n";
}

void Editor::drawFrameFoot(std::string& out) const {
    out += rule(frame_->footLeft, frame_->footRight, panelOpen_ ? 0 : frame_->teeUp,
                std::string(), 0, std::string(), 0);
    out += "\x1b[K\r\n";
}

void Editor::drawBody(std::string& out) const {

    SyntaxState state;
    for (size_t i = 0; i < rowoff_ && i < buf_.lineCount(); ++i)
        advanceState(buf_.line(i), lang_, state);

    for (int y = 0; y < bodyRows_; ++y) {
        out += frame_->down;
        if (treeOpen_) {
            size_t row = treeOff_ + static_cast<size_t>(y);
            std::string cell;
            if (row < tree_.size()) {
                const TreeEntry& e = tree_.entries()[row];
                cell = std::string(static_cast<size_t>(e.depth) * 2, ' ');
                cell += e.directory ? (e.open ? "-" : "+") : " ";
                cell += " ";
                cell += e.name;
            }

            bool picked;
            if (focus_ == FocusTree) {
                picked = (row < tree_.size() && row == treeSel_);
            } else {
                picked = row < tree_.size() && !buf_.path().empty() &&
                         path::same(tree_.entries()[row].path, buf_.path());
            }
            if (picked) out += (focus_ == FocusTree) ? "\x1b[7m" : "\x1b[4m";
            out += window(cell, 0, static_cast<size_t>(treeCols_));
            if (picked) out += "\x1b[m";
            out += frame_->down;
        }

        size_t row = rowoff_ + static_cast<size_t>(y);

        if (numbers_) {
            std::string cell(static_cast<size_t>(gutterCols_), ' ');
            bool hasBreak = false, isStopped = false, isLooked = false;
            if (row < buf_.lineCount()) {
                std::string num = number(row + 1);

                size_t at = cell.size() - 1 - num.size();
                for (size_t i = 0; i < num.size(); ++i) cell[at + i] = num[i];

                hasBreak = breakpointOn(row + 1);
                isStopped = stopLine_ == row + 1 && !stopFile_.empty() &&
                            path::filename(stopFile_) == path::filename(buf_.path());

                isLooked = looking_ > 0 && looking_ < stack_.size() &&
                           stack_[looking_].line == row + 1 &&
                           !stack_[looking_].file.empty() &&
                           path::filename(stack_[looking_].file) ==
                               path::filename(buf_.path());

                if (isStopped) cell[0] = '>';
                else if (isLooked) cell[0] = ':';
                else if (hasBreak) cell[0] = '*';
            }

            if (isStopped) out += "\x1b[92m";
            else if (isLooked) out += "\x1b[96m";
            else if (hasBreak) out += "\x1b[91m";
            else out += (row == cy_) ? "\x1b[93m" : "\x1b[90m";
            out += cell;
            out += "\x1b[39m";
        }

        if (row < buf_.lineCount()) {
            std::vector<unsigned char> kinds = highlight(buf_.line(row), lang_, state);
            std::string text;
            std::vector<unsigned char> spread;
            expandWithKinds(buf_.line(row), kinds, text, spread);

            size_t selFrom = 0, selTo = 0;
            size_t rawFrom = 0, rawTo = 0;
            if (selectionOn(row, rawFrom, rawTo)) {
                selFrom = renderCol(buf_.line(row), rawFrom);
                selTo = renderCol(buf_.line(row), rawTo);
            }
            out += colouredWindow(text, spread, coloff_,
                                  static_cast<size_t>(sourceCols_), selFrom, selTo);
        } else {
            std::string empty = numbers_ ? std::string() : std::string("~");
            empty.resize(static_cast<size_t>(sourceCols_), ' ');
            out += empty;
        }

        out += "\x1b[m";
        out += frame_->down;
        out += "\x1b[K\r\n";
    }
}

void Editor::drawPanel(std::string& out) const {
    if (!panelOpen_) return;

    std::string header;
    const char* names[TabCount] = {" Console ", " Debug ", " Assembly "};
    int visible = 0;
    for (int i = 0; i < TabCount; ++i) {
        bool on = (tab_ == static_cast<Tab>(i));
        if (on) {
            header += "\x1b[7m";
            header += names[i];
            header += "\x1b[m";
        } else {
            header += "\x1b[90m";
            header += names[i];
            header += "\x1b[39m";
        }
        visible += static_cast<int>(std::string(names[i]).size());

        if (i + 1 < TabCount) {
            header += frame_->across;
            visible += 1;
        }
    }

    std::string right;
    if (tab_ == TabConsole)
        right = number(console_.size()) + (console_.size() == 1 ? " line" : " lines");
    else if (tab_ == TabDebug)
        right = assembly_.empty() ? "nothing built yet" : "read from the assembly";
    else
        right = assembly_.empty() ? std::string("nothing built yet")
                                  : number(assembly_.size()) + " lines";

    out += rule(frame_->teeRight, frame_->teeLeft, frame_->teeUp, header, visible, " " + right + " ",
                static_cast<int>(right.size()) + 2);
    out += "\x1b[K\r\n";

    const std::vector<std::string>& lines = panelLines();
    Language panelLang = (tab_ == TabAssembly) ? LangAsm : LangPlain;
    size_t panelCols = static_cast<size_t>(screenCols_ > 2 ? screenCols_ - 2 : 1);
    int y = 0;
    for (size_t row = panelOff_; row < lines.size() && y < panelRows_; ++row) {
        SyntaxState panelState;
        std::vector<unsigned char> kinds = highlight(lines[row], panelLang, panelState);
        std::string text;
        std::vector<unsigned char> spread;
        expandWithKinds(lines[row], kinds, text, spread);

        // The continuation rows are the same line drawn from further along,
        // which is what colouredWindow already takes; nothing about the line
        // is copied or split, so a wrapped line highlights as one thing.
        const size_t segments = rowsForLine(lines[row], panelCols);
        for (size_t seg = 0; seg < segments && y < panelRows_; ++seg, ++y) {
            out += frame_->down;
            out += colouredWindow(text, spread, seg * panelCols, panelCols, 0, 0);
            out += "\x1b[m";
            out += frame_->down;
            out += "\x1b[K\r\n";
        }
    }
    for (; y < panelRows_; ++y) {
        out += frame_->down;
        out += std::string(panelCols, ' ');
        out += "\x1b[m";
        out += frame_->down;
        out += "\x1b[K\r\n";
    }
}

void Editor::drawStatus(std::string& out) const {
    std::string name = buf_.path().empty() ? std::string("[no name]") : baseName(buf_.path());
    std::string left = " " + name;
    if (buf_.dirty()) left += " *";
    left += "  " + lineCountText(buf_.lineCount());

    // **What will not fit goes a part at a time, least use first**, so the
    // language and the compiler are the last to go: all or nothing lost both at
    // once when "shalimar*" replaced a shorter name on an 80-column console.
    ToolchainKind kind = resolve(tool_, lang_);
    std::string compiler = toolchainShown(tool_, kind);
    if (tool_.kind == ToolAuto) compiler += "*";
    if (usesArch(kind)) compiler += std::string(" ") + kArches[arch_];
    const std::string parts[] = {
        languageName(lang_), configName(config_), compiler,
        number(cy_ + 1) + "/" + number(buf_.lineCount()), "col " + number(rx_ + 1),
        (focus_ == FocusText) ? "[text]" : (focus_ == FocusTree ? "[tree]" : "[panel]") };
    const int kParts = 6;
    // Dropped in this order when the bar is too narrow: focus, column, position, configuration.
    const int dropOrder[] = { 5, 4, 3, 1 };
    bool shown[kParts] = { true, true, true, true, true, true };
    size_t width = static_cast<size_t>(screenCols_);
    std::string right;
    for (int d = 0;; ++d) {
        right.clear();
        for (int k = 0; k < kParts; ++k)
            if (shown[k]) right += (right.empty() ? "" : "  ") + parts[k];
        right += " ";
        if (left.size() + right.size() <= width || d == 4) break;
        shown[dropOrder[d]] = false;
    }

    std::string bar = left;
    if (bar.size() + right.size() <= width) {
        bar.resize(width - right.size(), ' ');
        bar += right;
    }
    bar.resize(width, ' ');

    out += "\x1b[7m";
    out += bar;
    out += "\x1b[m\r\n";
}

void Editor::drawMessage(std::string& out) const {

    std::string text = message_;
    if (text.size() > static_cast<size_t>(screenCols_))
        text.resize(static_cast<size_t>(screenCols_));
    out += text;
    out += "\x1b[K";
}

void Editor::openMenu() {
    std::vector<Action> unavailable;
    // A dialog of tabs and drop-downs, which the windows draw and the terminal does not.
    unavailable.push_back(ActionCompilerOptions);

    if (debuggingShalimar()) {
        unavailable.push_back(ActionFrameUp);
        unavailable.push_back(ActionFrameDown);
        unavailable.push_back(ActionWatch);
    }

    menu_.disable(unavailable);
    menu_.open();
}

bool Editor::menuItemIsCurrent(Action action) const {
    switch (action) {

        case ActionLangAuto:     return langChoice_ == LangCount;
        case ActionLangC:        return langChoice_ == LangC;
        case ActionLangCpp:      return langChoice_ == LangCpp;
        case ActionLangShalimar: return langChoice_ == LangShalimar;
        case ActionLangJson:     return langChoice_ == LangJson;
        case ActionLangText:     return langChoice_ == LangPlain;

        case ActionToolAuto: return tool_.kind == ToolAuto;
        case ActionToolCc1:  return tool_.kind == ToolCc1;
        case ActionToolCxx1: return tool_.kind == ToolCxx1;
        case ActionToolShc:  return tool_.kind == ToolShc;
        case ActionToolMsvc: return tool_.kind == ToolMsvc;
        case ActionToolCxx:  return tool_.kind == ToolCxx;

        case ActionArchWindows: return kArches[arch_] == std::string("x86_64-windows");
        case ActionArchLinux:   return kArches[arch_] == std::string("x86_64-linux");
        case ActionArchDarwin:  return kArches[arch_] == std::string("arm64-darwin");
        case ActionArchTms6747: return kArches[arch_] == std::string("tms6747");

        case ActionConfigDebug:   return config_ == ConfigDebug;
        case ActionConfigRelease: return config_ == ConfigRelease;

        case ActionToggleTree:    return treeOpen_;
        case ActionTogglePanel:   return panelOpen_;
        case ActionToggleNumbers: return numbers_;
        case ActionTogglePlain:   return frame_ != &kBoxFrame;

        default: return false;
    }
}

// Where the open menu's box is: its left edge, 0-based, and the width inside its two sides. The mouse asks the same.
void Editor::dropdownBox(size_t& at, size_t& width) const {
    const MenuColumn& col = menu_.columns()[menu_.column()];
    width = 0;
    for (size_t i = 0; i < col.items.size(); ++i) {
        if (col.items[i].rule) continue;
        size_t w = col.items[i].label.size() + col.items[i].key.size() + 5;
        if (w > width) width = w;
    }
    at = menu_.titleAt(menu_.column());
    if (at > 0) --at;
    if (at + width + 3 > static_cast<size_t>(screenCols_))
        at = static_cast<size_t>(screenCols_) - width - 3;
}

void Editor::drawDropdown(std::string& out, std::vector<size_t>& covered) const {
    if (!menu_.dropped()) return;

    const MenuColumn& col = menu_.columns()[menu_.column()];

    size_t at = 0, width = 0;
    dropdownBox(at, width);

    out += "\x1b[2;" + number(at + 1) + "H\x1b[m";
    out += frame_->topLeft;
    out += repeated(frame_->across, static_cast<int>(width));
    out += frame_->topRight;
    covered.push_back(1);

    for (size_t i = 0; i < col.items.size(); ++i) {
        out += "\x1b[" + number(i + 3) + ";" + number(at + 1) + "H\x1b[m";

        if (col.items[i].rule) {
            out += frame_->teeRight;
            out += repeated(frame_->across, static_cast<int>(width));
            out += frame_->teeLeft;
            covered.push_back(i + 2);
            continue;
        }

        out += frame_->down;

        const std::string mark = menuItemIsCurrent(col.items[i].action)
                                     ? std::string(frame_->chosen)
                                     : std::string(" ");
        std::string row = mark + " " + col.items[i].label;
        row.resize(width - col.items[i].key.size() - 1 + (mark.size() - 1), ' ');
        row += col.items[i].key;
        row += " ";

        if (menu_.disabled(col.items[i].action)) out += "\x1b[2m";
        if (i == menu_.item()) out += "\x1b[7m";
        out += row;
        out += "\x1b[m";
        out += frame_->down;
        covered.push_back(i + 2);
    }

    out += "\x1b[" + number(col.items.size() + 3) + ";" + number(at + 1) + "H\x1b[m";
    out += frame_->footLeft;
    out += repeated(frame_->across, static_cast<int>(width));
    out += frame_->footRight;
    covered.push_back(col.items.size() + 2);
}

void Editor::dialogBox(int& at, int& top, int& wide) const {
    wide = static_cast<int>(askTitle_.size());
    int answer = static_cast<int>(utf8::columns(askAnswer_, askAnswer_.size()));
    if (answer + 2 > wide) wide = answer + 2;

    for (size_t i = 0; i < askShown_.size(); ++i) {
        int len = static_cast<int>(utf8::columns(askShown_[i], askShown_[i].size()));
        if (len + 4 > wide) wide = len + 4;
    }

    if (wide < 40) wide = 40;
    if (wide > screenCols_ - 6) wide = screenCols_ - 6;
    if (wide < 8) wide = 8;

    at = (screenCols_ - wide - 2) / 2;
    if (at < 0) at = 0;
    top = 3 + bodyRows_ / 3;
    if (top < 3) top = 3;

    int rows = 3 + static_cast<int>(askShown_.size());
    if (top + rows > bodyRows_ + 3) top = bodyRows_ + 3 - rows;
    if (top < 3) top = 3;
}

void Editor::drawDialog(std::string& out, std::vector<size_t>& covered) const {
    if (askTitle_.empty()) return;

    int at = 0, top = 0, wide = 0;
    dialogBox(at, top, wide);

    std::string title = " " + askTitle_ + " ";
    int titleWide = static_cast<int>(utf8::columns(title, title.size()));
    if (titleWide > wide - 2) {
        title = " ";
        titleWide = 1;
    }

    std::string head = frame_->topLeft;
    head += frame_->across;
    head += title;
    head += repeated(frame_->across, wide - titleWide - 1);
    head += frame_->topRight;

    std::string shown = askAnswer_;
    while (static_cast<int>(utf8::columns(shown, shown.size())) > wide - 2)
        shown.erase(0, utf8::next(shown, 0));
    std::string middle = frame_->down;
    middle += " " + shown;
    middle += std::string(
        static_cast<size_t>(wide - 1 - static_cast<int>(utf8::columns(shown, shown.size()))), ' ');
    middle += frame_->down;

    std::string foot = frame_->footLeft;
    foot += repeated(frame_->across, wide);
    foot += frame_->footRight;

    std::vector<std::string> rows;
    rows.push_back(head);
    rows.push_back(middle);
    for (size_t i = 0; i < askShown_.size(); ++i) {
        std::string text = askShown_[i];
        while (static_cast<int>(utf8::columns(text, text.size())) > wide - 2)
            text.resize(utf8::startOf(text, text.size() - 1));

        std::string pad(static_cast<size_t>(
                            wide - 1 - static_cast<int>(utf8::columns(text, text.size()))),
                        ' ');
        std::string row = frame_->down;
        if (i == askChoice_) row += "\x1b[7m " + text + pad + "\x1b[m";
        else                 row += " " + text + pad;
        row += frame_->down;
        rows.push_back(row);
    }
    rows.push_back(foot);

    for (size_t i = 0; i < rows.size(); ++i) {
        out += "\x1b[" + number(static_cast<size_t>(top) + i) + ";" +
               number(static_cast<size_t>(at + 1)) + "H";
        out += "\x1b[m";
        out += rows[i];
        covered.push_back(static_cast<size_t>(top) + i - 1);
    }
}

void Editor::placeCursor(std::string& out) const {
    size_t row = 1, col = 1;
    if (!askTitle_.empty()) {

        int at = 0, top = 0, wide = 0;
        dialogBox(at, top, wide);
        int answer = static_cast<int>(utf8::columns(askAnswer_, askAnswer_.size()));
        if (answer > wide - 2) answer = wide - 2;
        row = static_cast<size_t>(top + 1);
        col = static_cast<size_t>(at + 3 + answer);
    } else if (menu_.dropped()) {

        row = menu_.item() + 3;
        col = menu_.titleAt(menu_.column()) + 2;
    } else if (focus_ == FocusTree) {
        row = 3 + (treeSel_ - treeOff_);
        col = 2;
    } else if (focus_ == FocusPanel) {
        row = static_cast<size_t>(3 + bodyRows_ + 1);
        col = 2;
    } else {
        row = 3 + (cy_ - rowoff_);

        col = (treeOpen_ ? static_cast<size_t>(treeCols_) + 3 : 2) +
              static_cast<size_t>(gutterCols_) + (rx_ - coloff_);
    }
    out += "\x1b[" + number(row) + ";" + number(col) + "H";
}

void Editor::refresh() {
    if (batch_) return;         // no terminal to draw on
    layout();
    clampCursor();
    scroll();

    std::string screen;
    drawMenuBar(screen);
    drawFrameTop(screen);
    drawBody(screen);
    drawPanel(screen);
    drawFrameFoot(screen);
    drawStatus(screen);
    drawMessage(screen);

    std::vector<std::string> rows;
    size_t from = 0;
    for (;;) {
        size_t end = screen.find("\r\n", from);
        if (end == std::string::npos) {
            rows.push_back(screen.substr(from));
            break;
        }
        rows.push_back(screen.substr(from, end - from));
        from = end + 2;
    }
    present(rows);
}

void Editor::present(const std::vector<std::string>& rows) {
    std::string out;

    out += "\x1b[?2026h\x1b[?25l";

    bool everything = painted_.size() != rows.size() || paintedCols_ != screenCols_;
    if (everything) {
        out += "\x1b[2J";
        painted_.assign(rows.size(), std::string());
        paintedCols_ = screenCols_;
    }

    for (size_t i = 0; i < rows.size(); ++i) {
        if (!everything && painted_[i] == rows[i]) continue;
        out += "\x1b[" + number(i + 1) + ";1H";
        out += rows[i];
        out += "\x1b[m\x1b[K";
    }
    painted_ = rows;

    std::vector<size_t> covered;
    drawDropdown(out, covered);
    drawDialog(out, covered);
    for (size_t i = 0; i < covered.size(); ++i)
        if (covered[i] < painted_.size()) painted_[covered[i]].clear();

    placeCursor(out);
    out += "\x1b[?25h\x1b[?2026l";
    Terminal::write(out);
}

void Editor::moveCursor(int key) {
    const std::string& line = buf_.line(cy_);
    switch (key) {
        case KEY_ARROW_LEFT:

            if (cx_ > 0) cx_ = utf8::previous(line, cx_);
            else if (cy_ > 0) { --cy_; cx_ = buf_.line(cy_).size(); }
            break;
        case KEY_ARROW_RIGHT:
            if (cx_ < line.size()) cx_ = utf8::next(line, cx_);
            else if (cy_ + 1 < buf_.lineCount()) { ++cy_; cx_ = 0; }
            break;
        case KEY_ARROW_UP:   if (cy_ > 0) --cy_; break;
        case KEY_ARROW_DOWN: if (cy_ + 1 < buf_.lineCount()) ++cy_; break;
        case KEY_HOME:       cx_ = 0; break;
        case KEY_END:        cx_ = line.size(); break;
        case KEY_PAGE_UP:
            cy_ = (cy_ > static_cast<size_t>(bodyRows_)) ? cy_ - static_cast<size_t>(bodyRows_) : 0;
            break;
        case KEY_PAGE_DOWN:
            cy_ += static_cast<size_t>(bodyRows_);
            if (cy_ >= buf_.lineCount()) cy_ = buf_.lineCount() - 1;
            break;
        default: break;
    }
    clampCursor();
    buf_.breakRun();
}

void Editor::moveTree(int key) {
    if (tree_.size() == 0) return;
    size_t step = static_cast<size_t>(bodyRows_);
    switch (key) {
        case KEY_ARROW_UP:   if (treeSel_ > 0) --treeSel_; break;
        case KEY_ARROW_DOWN: if (treeSel_ + 1 < tree_.size()) ++treeSel_; break;
        case KEY_PAGE_UP:    treeSel_ = (treeSel_ > step) ? treeSel_ - step : 0; break;
        case KEY_PAGE_DOWN:
            treeSel_ += step;
            if (treeSel_ >= tree_.size()) treeSel_ = tree_.size() - 1;
            break;
        case KEY_HOME:       treeSel_ = 0; break;
        case KEY_END:        treeSel_ = tree_.size() - 1; break;
        case KEY_ARROW_LEFT:
        case KEY_ARROW_RIGHT:
            tree_.toggle(treeSel_);
            refreshTree();
            break;
        default: break;
    }
}

// Growing and shrinking the bottom panel. Kept between draws in panelWanted_
// rather than applied to panelRows_, which layout() overwrites from the
// screen size every time it runs.
void Editor::resizePanel(int by) {
    if (!panelOpen_) { say("the panel is hidden - Ctrl-E shows it"); return; }
    int wanted = panelWanted_ + by;
    if (wanted < 2) wanted = 2;
    if (wanted > screenRows_ - 8) wanted = screenRows_ - 8;
    if (wanted < 2) wanted = 2;
    panelWanted_ = wanted;
    refresh();

    // A taller panel should show more of what came before, not blank rows
    // under the same top line. Anything already at the end is re-anchored
    // there; a view scrolled back stays where it was put.
    if (panelOff_ >= panelTopForEnd()) panelOff_ = panelTopForEnd();
    say("panel: " + number(panelRows_) + " rows");
}

// **What About says is meant to be read at once.** A seven-row panel held its seven lines until
// the panel began to wrap: where the compilers' row takes two rows the last line drops off a
// block nobody scrolls. The Windows suite caught it and the Mac's did not - cmd over ssh is narrower than eighty columns - so the room is asked for rather than assumed.
void Editor::fitPanelTo(const std::vector<std::string>& lines) {
    const size_t cols = static_cast<size_t>(screenCols_ > 2 ? screenCols_ - 2 : 1);
    size_t rows = 0;
    for (size_t i = 0; i < lines.size(); ++i) rows += rowsForLine(lines[i], cols);

    int wanted = static_cast<int>(rows);
    if (wanted > screenRows_ - 8) wanted = screenRows_ - 8;
    if (wanted > panelWanted_) panelWanted_ = wanted;
}

void Editor::movePanel(int key) {
    const std::vector<std::string>& lines = panelLines();
    // A page is what is on screen, which is fewer lines than rows when they
    // wrap - stepping by rows walked past whatever had wrapped.
    size_t step = (key == KEY_PAGE_UP || key == KEY_PAGE_DOWN)
                      ? panelLinesShowing(panelOff_) : 1;
    switch (key) {
        case KEY_ARROW_UP:
        case KEY_PAGE_UP:   panelOff_ = (panelOff_ > step) ? panelOff_ - step : 0; break;
        case KEY_ARROW_DOWN:
        case KEY_PAGE_DOWN: if (panelOff_ + step < lines.size()) panelOff_ += step; break;
        case KEY_HOME:      panelOff_ = 0; break;
        case KEY_END:       panelOff_ = panelTopForEnd(); break;
        case KEY_ARROW_RIGHT:
            tab_ = static_cast<Tab>((tab_ + 1) % TabCount);
            panelOff_ = 0;
            break;
        case KEY_ARROW_LEFT:
            tab_ = static_cast<Tab>((tab_ + TabCount - 1) % TabCount);
            panelOff_ = 0;
            break;
        default: break;
    }
}

void Editor::cycleFocus() {
    for (int i = 0; i < 3; ++i) {
        focus_ = (focus_ == FocusText) ? FocusTree
                                       : (focus_ == FocusTree ? FocusPanel : FocusText);
        if (focus_ == FocusTree && !treeOpen_) continue;
        if (focus_ == FocusPanel && !panelOpen_) continue;
        break;
    }
}

void Editor::insertChar(char c) {
    buf_.beginEdit(EditTyping, cx_, cy_);
    buf_.insertChar(cy_, cx_, c);
    ++cx_;
}

bool Editor::selection(Range& range) const {
    if (!marked_) return false;
    if (markRow_ == cy_ && markCol_ == cx_) return false;
    range = ordered(markRow_, markCol_, cy_, cx_);
    return true;
}

bool Editor::selectionOn(size_t row, size_t& from, size_t& to) const {
    Range range;
    if (!selection(range)) return false;
    if (row < range.fromRow || row > range.toRow) return false;

    from = (row == range.fromRow) ? range.fromCol : 0;
    to = (row == range.toRow) ? range.toCol : buf_.line(row).size();
    return to > from;
}

void Editor::extendTo(int key) {

    if (!marked_) {
        marked_ = true;
        markRow_ = cy_;
        markCol_ = cx_;
    }
    moveCursor(unshifted(key));
}

bool Editor::eraseSelection() {
    Range range;
    if (!selection(range)) return false;

    buf_.beginEdit(EditOther, cx_, cy_);
    buf_.eraseRange(range);
    cy_ = range.fromRow;
    cx_ = range.fromCol;
    marked_ = false;
    clampCursor();
    return true;
}

void Editor::copySelection(bool cut) {
    Range range;
    if (selection(range)) {
        clipboard_ = buf_.textIn(range);
        if (cut) eraseSelection();
        else marked_ = false;
        say(number(clipboard_.size()) + " characters " + (cut ? "cut" : "copied"));
        return;
    }

    clipboard_ = buf_.line(cy_) + "\n";
    if (cut) {
        buf_.beginEdit(EditOther, cx_, cy_);
        if (buf_.lineCount() == 1) {
            buf_.replaceLine(0, std::string());
        } else {
            Range whole;
            whole.fromRow = cy_;
            whole.fromCol = 0;
            whole.toRow = cy_ + 1;
            whole.toCol = 0;
            buf_.eraseRange(whole);
        }
        cx_ = 0;
        clampCursor();
    }
    say(cut ? "line cut" : "line copied");
}

void Editor::pasteClipboard() {
    if (clipboard_.empty()) { say("there is nothing to paste"); return; }

    buf_.beginEdit(EditOther, cx_, cy_);
    eraseSelection();

    size_t endRow = cy_, endCol = cx_;
    buf_.insertText(cy_, cx_, clipboard_, endRow, endCol);
    cy_ = endRow;
    cx_ = endCol;
    marked_ = false;
    clampCursor();
    say(number(clipboard_.size()) + " characters pasted");
}

void Editor::selectAll() {
    marked_ = true;
    markRow_ = 0;
    markCol_ = 0;
    cy_ = buf_.lineCount() - 1;
    cx_ = buf_.line(cy_).size();
    focus_ = FocusText;
    say("all of it selected");
}

void Editor::undoEdit() {
    marked_ = false;
    if (!buf_.undo(cx_, cy_)) { say("nothing to undo"); return; }
    clampCursor();
    focus_ = FocusText;
    say("undone" + std::string(buf_.canUndo() ? "" : " - that was the last of it"));
}

void Editor::redoEdit() {
    if (!buf_.redo(cx_, cy_)) { say("nothing to redo"); return; }
    clampCursor();
    focus_ = FocusText;
    say("redone");
}

void Editor::insertNewline() {

    buf_.beginEdit(EditOther, cx_, cy_);

    std::string lead = indentAfterNewline(buf_.lines(), cy_, cx_, style_);
    buf_.splitLine(cy_, cx_);

    if (buf_.line(cy_).find_first_not_of(" \t") == std::string::npos)
        buf_.replaceLine(cy_, std::string());

    ++cy_;

    buf_.replaceLine(cy_, lead + withoutLeadingSpace(buf_.line(cy_)));
    cx_ = lead.size();
}

void Editor::backspace() {
    buf_.beginEdit(EditErasing, cx_, cy_);
    if (cx_ > 0) {

        size_t start = utf8::previous(buf_.line(cy_), cx_);
        Range range;
        range.fromRow = range.toRow = cy_;
        range.fromCol = start;
        range.toCol = cx_;
        buf_.eraseRange(range);
        cx_ = start;
    } else if (cy_ > 0) {
        cx_ = buf_.line(cy_ - 1).size();
        buf_.joinLine(cy_ - 1);
        --cy_;
    }
}

void Editor::deleteForward() {
    buf_.beginEdit(EditErasing, cx_, cy_);
    if (cx_ < buf_.line(cy_).size()) {
        Range range;
        range.fromRow = range.toRow = cy_;
        range.fromCol = cx_;
        range.toCol = utf8::next(buf_.line(cy_), cx_);
        buf_.eraseRange(range);
    } else if (cy_ + 1 < buf_.lineCount()) {
        buf_.joinLine(cy_);
    }
}

void Editor::realign() {
    const std::string& line = buf_.line(cy_);
    size_t had = leadingSpace(line);
    std::string want = indentFor(buf_.lines(), cy_, style_);
    if (want == line.substr(0, had)) return;

    buf_.beginEdit(EditTyping, cx_, cy_);
    buf_.replaceLine(cy_, want + line.substr(had));
    cx_ = (cx_ >= had) ? cx_ - had + want.size() : want.size();
}

void Editor::tabKey() {
    const std::string& line = buf_.line(cy_);
    size_t lead = leadingSpace(line);

    if (cx_ <= lead) {
        std::string want = indentFor(buf_.lines(), cy_, style_);
        buf_.beginEdit(EditOther, cx_, cy_);
        buf_.replaceLine(cy_, want + line.substr(lead));
        cx_ = want.size();
        return;
    }
    if (style_.tabs) { insertChar('\t'); return; }
    for (size_t i = 0; i < style_.width; ++i) insertChar(' ');
}

void Editor::findAgain(bool forwards) {
    if (needle_.empty()) { say("nothing to look for yet - Ctrl-F asks"); return; }

    Match match = forwards ? findNext(buf_.lines(), needle_, cy_, cx_ + 1)
                           : findPrevious(buf_.lines(), needle_, cy_, cx_);
    if (!match.found) { say(needle_ + " is not in this file"); return; }

    cy_ = match.row;
    cx_ = match.col;
    focus_ = FocusText;
    say(needle_ + " - line " + number(match.row + 1));
}

void Editor::findPrompt() {
    bool cancelled = false;
    std::string want = prompt("find: ", cancelled);

    if (cancelled || want.empty()) { say("nothing looked for"); return; }

    needle_ = want;

    Match match = findNext(buf_.lines(), needle_, cy_, cx_);
    if (!match.found) { say(needle_ + " is not in this file"); return; }

    cy_ = match.row;
    cx_ = match.col;
    focus_ = FocusText;
    say(needle_ + " - line " + number(match.row + 1) + ", Ctrl-G for the next");
}

void Editor::replacePrompt() {
    bool cancelled = false;
    std::string want = prompt("replace: ", cancelled);
    if (cancelled || want.empty()) { say("nothing replaced"); return; }

    std::string with = prompt("replace " + want + " with: ", cancelled);
    if (cancelled) { say("nothing replaced"); return; }

    std::vector<std::string> lines = buf_.lines();
    size_t count = replaceAll(lines, want, with);
    if (count == 0) { say(want + " is not in this file"); return; }

    buf_.beginEdit(EditOther, cx_, cy_);
    buf_.replaceAll(lines);
    needle_ = with;
    clampCursor();

    say(number(count) + (count == 1 ? " change" : " changes") + " - Ctrl-Z puts them back");
}

void Editor::reindentAll() {
    const std::vector<std::string> laid = reindent(buf_.lines(), style_);

    Range range;
    bool some = selection(range) && !range.empty();

    if (some && laid.size() == buf_.lineCount()) {
        size_t first = range.fromRow, last = range.toRow;
        if (last >= buf_.lineCount()) last = buf_.lineCount() - 1;

        std::vector<std::string> kept = buf_.lines();
        size_t moved = 0;
        for (size_t row = first; row <= last; ++row)
            if (kept[row] != laid[row]) {
                kept[row] = laid[row];
                ++moved;
            }

        buf_.beginEdit(EditOther, cx_, cy_);
        buf_.replaceAll(kept);
        clampCursor();
        cx_ = leadingSpace(buf_.line(cy_));
        size_t howMany = last - first + 1;
        say("laid out " + number(howMany) + (howMany == 1 ? " line" : " lines") +
            " of the selection - " + number(moved) + " moved");
        return;
    }

    buf_.beginEdit(EditOther, cx_, cy_);
    buf_.replaceAll(laid);
    clampCursor();
    cx_ = leadingSpace(buf_.line(cy_));
    say("laid out - " + lineCountText(buf_.lineCount()));
}

bool Editor::save() {
    if (buf_.path().empty()) {
        bool cancelled = false;
        std::string name = prompt("save as: ", cancelled);
        if (cancelled || name.empty()) { say("not saved"); return false; }
        buf_.setPath(name);
    }

    std::string error;
    if (!buf_.save(error)) { say(error); return false; }
    buf_.breakRun();
    say(buf_.path() + " written");
    refreshTree();
    return true;
}

void Editor::saveAs() {
    bool cancelled = false;
    std::string name = prompt("save as: ", cancelled);
    if (cancelled || name.empty()) { say("not saved"); return; }
    buf_.setPath(name);
    if (!save()) return;
    settings::rememberFile(buf_.path());
    refreshRecent();

    // Saved into the project's directory is saved into the project.
    Outcome joined = editor::adoptSaved(project_, buf_.path());
    if (joined.ok) { say(joined.message); refreshTree(); }
}

std::vector<std::string> Editor::whatIsIn(const std::string& directory) const {
    std::vector<std::string> found;
    std::vector<path::Entry> here = path::entries(directory);

    for (size_t i = 0; i < here.size(); ++i) {
        if (here[i].name.empty() || here[i].name[0] == '.') continue;
        if (here[i].directory) {

            if (here[i].name == "obj" || here[i].name == "build" ||
                here[i].name == "x64" || here[i].name == "Debug" ||
                here[i].name == "Release" || here[i].name == "node_modules")
                continue;
            found.push_back(here[i].name + "/");
            continue;
        }

        const char* const kinds[9] = {".c", ".h", ".cpp", ".hpp", ".cc", ".cxx",
                                      ".s", ".shl", ".json"};
        for (size_t k = 0; k < 9; ++k) {
            const std::string suffix = kinds[k];
            if (here[i].name.size() < suffix.size()) continue;
            if (here[i].name.compare(here[i].name.size() - suffix.size(),
                                     suffix.size(), suffix) != 0) continue;
            found.push_back(here[i].name);
            break;
        }
    }

    std::sort(found.begin(), found.end());
    return found;
}

std::vector<std::string> Editor::projectsIn(const std::string& directory) const {
    std::vector<std::string> found;

    std::vector<std::string> named = Project::projectFilesIn(directory);
    for (size_t i = 0; i < named.size(); ++i) found.push_back(path::filename(named[i]));

    // A CCS workspace offers one .pro per project, the ones not yet written as well: choosing one writes it.
    if (ccs::isWorkspace(directory)) {
        ccs::Workspace ws;
        std::string error;
        if (ccs::readWorkspace(directory, ws, error))
            for (size_t i = 0; i < ws.projects.size(); ++i) {
                std::string pro = ws.projects[i].name + Project::suffix();
                if (std::find(found.begin(), found.end(), pro) == found.end()) found.push_back(pro);
            }
        std::sort(found.begin(), found.end());
        named = found;
    }

    // Inside a CCS project folder, "./" opens it.
    if (named.empty() && ccs::isProject(directory)) found.push_back("./");

    std::vector<path::Entry> here = path::entries(directory);
    for (size_t i = 0; i < here.size(); ++i) {
        if (!here[i].directory) continue;
        if (here[i].name.empty() || here[i].name[0] == '.') continue;
        found.push_back(here[i].name + "/");
    }

    std::sort(found.begin() + static_cast<long>(named.empty() ? (found.empty() ? 0 : 1)
                                                              : named.size()),
              found.end());
    return found;
}

void Editor::openPrompt() {

    paneMode_ = PaneFiles;
    std::string where = project_.loaded() ? project_.root() : path::absolute(".");
    std::string prefix;

    for (;;) {

        std::vector<std::string> offered;
        if (project_.loaded() && prefix.empty()) {
            const std::vector<Group>& groups = project_.groups();
            for (size_t i = 0; i < groups.size(); ++i)
                for (size_t j = 0; j < groups[i].files.size(); ++j)
                    offered.push_back(groups[i].files[j]);
        }
        std::vector<std::string> here = whatIsIn(where);
        for (size_t i = 0; i < here.size(); ++i) offered.push_back(here[i]);

        bool cancelled = false;
        std::string name = prompt("open" + (prefix.empty() ? std::string() : " " + prefix) + ": ",
                                  cancelled, offered);
        if (cancelled || name.empty()) { say("not opened"); return; }

        if (name[name.size() - 1] == '/') {
            std::string leaf = name.substr(0, name.size() - 1);
            where = path::join(where, leaf);
            prefix += leaf + "/";
            continue;
        }

        open(path::join(where, name));
        settings::rememberFile(path::join(where, name));
        refreshRecent();
        return;
    }
}

void Editor::openProjectPrompt() {
    std::string where = project_.loaded() ? project_.root() : path::absolute(".");

    for (;;) {
        bool cancelled = false;
        std::string name = prompt("open project in: ", cancelled, projectsIn(where));
        if (cancelled || name.empty()) { say("no project opened"); return; }

        if (name == "./") { openProject(where); return; }

        if (name.size() > 4 && name[name.size() - 1] != '/') {
            std::string named = path::join(where, name);
            if (path::exists(named) && !path::isDirectory(named)) {
                openProject(named);
                return;
            }
            // A workspace's project with no .pro yet: written now, workspace and project and nothing else.
            const std::string suffix = Project::suffix();
            if (ccs::isWorkspace(where) && name.size() > suffix.size() &&
                name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                ccs::Workspace ws;
                std::string error, pro;
                if (ccs::readWorkspace(where, ws, error) && ws.project(name.substr(0, name.size() - suffix.size()))) {
                    if (!ccs::writePro(where, name.substr(0, name.size() - suffix.size()), pro, error)) { say(error); return; }
                    openProject(pro);
                    return;
                }
            }
        }

        if (name[name.size() - 1] == '/') {
            std::string into = path::join(where, name.substr(0, name.size() - 1));

            // A folder with a .pro, or a CCS project's: opened rather than walked into.
            if (!Project::fileIn(into).empty() ||
                (ccs::isProject(into) && (settings::ccsEnabled() || !ccs::workspaceOf(into).empty()))) {
                openProject(into);
                return;
            }
            where = into;
            continue;
        }

        openProject(path::join(where, name));
        return;
    }
}

void Editor::newFile() {
    stash();
    docs_.push_back(Document());
    doc_ = docs_.size() - 1;
    restore();
    lang_ = LangPlain;

    paneMode_ = PaneFiles;
    refreshTree();
    say("new file - Ctrl-S names it");
}

void Editor::openSelected() {
    if (treeSel_ >= tree_.size()) return;
    const TreeEntry& e = tree_.entries()[treeSel_];
    if (e.directory) {
        tree_.toggle(treeSel_);
        refreshTree();
        return;
    }
    open(e.path);
    focus_ = FocusText;
}

std::string Editor::targetFile() const {
    if (focus_ == FocusTree && treeSel_ < tree_.size()) {
        const TreeEntry& e = tree_.entries()[treeSel_];
        if (!e.directory) return e.path;
    }
    return buf_.path();
}

std::string Editor::groupUnderCursor() const {
    if (!project_.loaded()) return std::string();
    for (size_t i = treeSel_ + 1; i-- > 0;) {
        if (i >= tree_.size() || !tree_.entries()[i].group) continue;

        if (tree_.entries()[i].session) return std::string();
        return tree_.entries()[i].name;
    }
    return std::string();
}

void Editor::createFile() {
    bool cancelled = false;
    std::string name = prompt("new file: ", cancelled);
    if (cancelled || name.empty()) { say("nothing made"); return; }

    std::string group = groupForFile(path::filename(name));
    if (group.empty()) group = groupUnderCursor();

    Outcome done = editor::createFile(project_, name, group, tool_.kind);
    say(done.message);
    if (!done.ok) return;

    refreshTree();
    open(done.path);
}

void Editor::renameFile() {
    std::string path = targetFile();
    if (path.empty()) { say("no file to rename"); return; }

    std::string was = project_.relative(path);
    bool cancelled = false;
    std::string name = prompt("rename " + was + " to: ", cancelled);
    if (cancelled || name.empty()) { say("not renamed"); return; }

    Outcome done = editor::renameFile(project_, path, name);
    say(done.message);
    if (!done.ok) return;

    for (size_t i = 0; i < docs_.size(); ++i) {
        Buffer& b = (i == doc_) ? buf_ : docs_[i].buf;
        if (!b.path().empty() && path::same(b.path(), path)) b.setPath(done.path);
    }

    std::map<std::string, FileBreaks>::iterator had = breaks_.find(path::oneName(path));
    if (had != breaks_.end()) {
        std::set<size_t> lines = had->second.lines;
        breaks_.erase(had);
        FileBreaks& now = breaks_[path::oneName(done.path)];
        now.path = done.path;
        now.lines = lines;
    }

    applyLanguage();
    refreshTree();
}

void Editor::deleteFile() {
    std::string path = targetFile();
    if (path.empty()) { say("no file to delete"); return; }

    std::string shown = project_.relative(path);

    bool cancelled = false;
    std::string answer = prompt("delete " + shown + " from disk? type yes: ", cancelled);
    if (cancelled || answer != "yes") { say("not deleted"); return; }

    Outcome done = editor::deleteFile(project_, path);
    say(done.message);
    if (!done.ok) return;

    for (size_t i = 0; i < docs_.size(); ++i) {
        Buffer& b = (i == doc_) ? buf_ : docs_[i].buf;
        if (!b.path().empty() && path::same(b.path(), path)) b.setPath(std::string());
    }

    breaks_.erase(path::oneName(path));

    refreshTree();
}

void Editor::regroupFile() {
    if (!project_.loaded()) { say("there is no project to regroup in"); return; }

    std::string path = targetFile();
    if (path.empty()) { say("no file to move"); return; }

    bool cancelled = false;
    std::string group = prompt("move " + project_.relative(path) + " to group: ", cancelled);
    if (cancelled || group.empty()) { say("not moved"); return; }

    Outcome done = editor::moveToGroup(project_, path, group);
    say(done.message);
    if (done.ok) refreshTree();
}

// Which file, then which group. The files under the project's root that are not in it yet are
// offered, one directory deep as the project allows; the file in front is the answer when
// nothing is typed. The window's Add File asks the same with an Open dialog.
void Editor::addToProject() {
    if (!project_.loaded()) { say("there is no project - make one first"); return; }

    std::vector<std::string> offered;
    std::vector<std::string> top = whatIsIn(project_.root());
    for (size_t i = 0; i < top.size(); ++i) {
        if (top[i].empty()) continue;
        if (top[i][top[i].size() - 1] != '/') {
            if (project_.groupOf(top[i]) >= project_.groups().size()) offered.push_back(top[i]);
            continue;
        }
        std::vector<std::string> under = whatIsIn(path::join(project_.root(), top[i].substr(0, top[i].size() - 1)));
        for (size_t j = 0; j < under.size(); ++j) {
            if (under[j].empty() || under[j][under[j].size() - 1] == '/') continue;
            std::string relative = top[i] + under[j];
            if (project_.groupOf(relative) >= project_.groups().size()) offered.push_back(relative);
        }
    }
    std::string inFront = buf_.path().empty() ? std::string() : project_.relative(buf_.path());
    if (!inFront.empty() && project_.groupOf(inFront) >= project_.groups().size()) {
        // first, so that Enter alone takes it
        for (size_t i = 0; i < offered.size(); ++i)
            if (offered[i] == inFront) { offered.erase(offered.begin() + static_cast<long>(i)); break; }
        offered.insert(offered.begin(), inFront);
    }

    bool cancelled = false;
    std::string chosen = prompt("add file" + (inFront.empty() ? std::string() : " [" + inFront + "]") + ": ",
                                cancelled, offered);
    if (cancelled) { say("not added"); return; }
    if (chosen.empty()) chosen = inFront;
    if (chosen.empty()) { say("no file named - save the one in front, or type a name"); return; }
    bool rooted = chosen[0] == '/' || (chosen.size() > 1 && chosen[1] == ':');
    std::string absolute = rooted ? chosen : path::join(project_.root(), chosen);

    std::string wanted = groupForFile(path::filename(absolute));
    if (wanted.empty()) wanted = "Sources";
    std::string group = prompt("add " + project_.relative(absolute) + " to group [" + wanted + "]: ", cancelled);
    if (cancelled) { say("not added"); return; }
    if (group.empty()) group = wanted;

    Outcome done = editor::addExisting(project_, absolute, group);
    say(done.message);
    if (done.ok) refreshTree();
}

// A list of paths is edited as one line, the entries separated by ';' - the one character that is not in a path on any of the three systems.
namespace {

std::string joined(const std::vector<std::string>& parts) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) out += (i ? ";" : "") + parts[i];
    return out;
}

std::vector<std::string> splitList(const std::string& line) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at <= line.size()) {
        size_t cut = line.find(';', at);
        if (cut == std::string::npos) cut = line.size();
        std::string part = line.substr(at, cut - at);
        size_t a = part.find_first_not_of(" \t"), b = part.find_last_not_of(" \t");
        if (a != std::string::npos) out.push_back(part.substr(a, b - a + 1));
        at = cut + 1;
    }
    return out;
}

}

// The Project menu's last three entries name the projects remembered.
void Editor::refreshRecent() {
    std::vector<std::string> recent = settings::recentProjects();
    const Action actions[3] = { ActionProjectRecent, ActionProjectRecent2, ActionProjectRecent3 };
    for (size_t i = 0; i < 3; ++i) {
        std::string label = std::to_string(i + 1) + ".";
        if (i < recent.size()) {
            std::string leaf = path::filename(recent[i]);
            size_t dot = leaf.rfind('.');
            if (dot != std::string::npos && leaf.compare(dot, std::string::npos, Project::suffix()) == 0)
                leaf.resize(dot);
            label += " " + leaf;
        } else if (i == 0) {
            label += " (no recent project)";
        }
        menu_.relabel(actions[i], label);
    }

    // And the File menu's, the last three files opened on their own.
    std::vector<std::string> files = settings::recentFiles();
    const Action fileActions[3] = { ActionFileRecent, ActionFileRecent2, ActionFileRecent3 };
    for (size_t i = 0; i < 3; ++i) {
        std::string label = std::to_string(i + 1) + ".";
        if (i < files.size()) label += " " + path::filename(files[i]);
        else if (i == 0) label += " (no recent file)";
        menu_.relabel(fileActions[i], label);
    }
}

Toolchain Editor::toolFor() const {
    Toolchain tool = tool_;
    tool.include = settings::includeDir();
    tool.lib = settings::libDir();
    if (project_.loaded()) {
        tool.includes = project_.absoluteIncludes();
        tool.libraries = project_.absoluteLibraries();
        tool.tiLink = project_.tiLink(config_);
        tool.emulateOnly = project_.emulateOnly();
    }
    std::vector<std::string> shared = settings::includes();
    tool.includes.insert(tool.includes.end(), shared.begin(), shared.end());
    shared = settings::libraries();
    tool.libraries.insert(tool.libraries.end(), shared.begin(), shared.end());
    return tool;
}

// The installation's, in settings.json - every compile searches them after
// a project's own, which a .pro may still carry.
void Editor::editProjectIncludes() {
    std::string file = settings::installFile();
    if (file.empty()) { say("no installation directory to keep this in"); return; }
    bool cancelled = false;
    std::string line = prompt("shared include paths, ';' between, '-' for none [" +
                              joined(settings::includes()) + "]: ", cancelled);
    if (cancelled || line.empty()) { say("shared include paths unchanged"); return; }
    if (settings::rememberIncludes(splitList(line == "-" ? std::string() : line)))
        say("shared include paths: " + joined(settings::includes()) + " - written to " + file);
    else
        say("cannot write " + file);
}

void Editor::editProjectLibraries() {
    std::string file = settings::installFile();
    if (file.empty()) { say("no installation directory to keep this in"); return; }
    bool cancelled = false;
    std::string line = prompt("shared libraries, ';' between, '-' for none [" +
                              joined(settings::libraries()) + "]: ", cancelled);
    if (cancelled || line.empty()) { say("shared libraries unchanged"); return; }
    if (settings::rememberLibraries(splitList(line == "-" ? std::string() : line)))
        say("shared libraries: " + joined(settings::libraries()) + " - written to " + file);
    else
        say("cannot write " + file);
}

// The open project's own header directories and libraries, relative to its
// root, written to its .pro - searched and linked before the installation's.
// The window has the same over ride_project_set_includes/libraries.
void Editor::editOwnIncludes() {
    if (!project_.loaded()) { say("there is no project open - these are a project's own; Tools > Shared include paths... is the installation's"); return; }
    bool cancelled = false;
    std::string line = prompt("the project's include paths, ';' between, '-' for none [" +
                              joined(project_.includes()) + "]: ", cancelled);
    if (cancelled || line.empty()) { say("the project's include paths are unchanged"); return; }
    project_.setIncludes(splitList(line == "-" ? std::string() : line));
    Outcome done = editor::saveProject(project_);
    say(done.ok ? "the project's include paths: " + joined(project_.includes()) + " - written to " + path::filename(project_.file())
                : done.message);
}

void Editor::editOwnLibraries() {
    if (!project_.loaded()) { say("there is no project open - these are a project's own; Tools > Shared libraries... is the installation's"); return; }
    bool cancelled = false;
    std::string line = prompt("the project's libraries, ';' between, '-' for none [" +
                              joined(project_.libraries()) + "]: ", cancelled);
    if (cancelled || line.empty()) { say("the project's libraries are unchanged"); return; }
    project_.setLibraries(splitList(line == "-" ? std::string() : line));
    Outcome done = editor::saveProject(project_);
    say(done.ok ? "the project's libraries: " + joined(project_.libraries()) + " - written to " + path::filename(project_.file())
                : done.message);
}

void Editor::editHeaderDirs() {
    std::string file = settings::installFile();
    if (file.empty()) { say("no installation directory to keep this in"); return; }
    bool cancelled = false;
    std::string include = prompt("cpp11's headers (include) [" + settings::includeDir() + "]: ", cancelled);
    if (cancelled) { say("header directories unchanged"); return; }
    std::string lib = prompt("c90's headers (lib) [" + settings::libDir() + "]: ", cancelled);
    if (cancelled) { say("header directories unchanged"); return; }
    if (include.empty()) include = settings::includeDir();
    if (lib.empty()) lib = settings::libDir();
    if (settings::rememberHeaderDirs(include, lib))
        say("written to " + file + " - include " + settings::includeDir() + ", lib " + settings::libDir());
    else
        say("cannot write " + file);
}

void Editor::locateVcvars() {
    std::string file = settings::installFile();
    if (file.empty()) { say("no installation directory to keep this in"); return; }
    bool cancelled = false;
    std::string bat = prompt("vcvars64.bat, full path [" + settings::vcvars() + "]: ", cancelled);
    if (cancelled || bat.empty()) { say("vcvars unchanged"); return; }
    if (bat != "-" && !path::exists(bat)) { say("no such file: " + bat); return; }
    if (settings::rememberVcvars(bat == "-" ? std::string() : bat))
        say("written to " + file + (bat == "-" ? " - the editor looks for Visual Studio itself again"
                                              : " - Visual Studio's tools come from " + bat));
    else
        say("cannot write " + file);
}

// TI's C6000 compiler directory (CCS's ti-cgt-c6000_x.y.z), whose lnk6x links what asm6x made of
// a tms6747 build into a .out; a second directory may hold the exception-handling runtime CCS
// does not ship. `-` clears it. See settings::ti.
void Editor::locateTi() {
    std::string file = settings::installFile();
    if (file.empty()) { say("no installation directory to keep this in"); return; }
    bool cancelled = false;
    std::string dir = prompt("TI C6000 compiler directory, the one with bin/lnk6x [" + settings::ti() + "]: ", cancelled);
    if (cancelled || dir.empty()) { say("TI compiler unchanged"); return; }
    if (dir == "-") {
        if (settings::rememberTi(std::string(), std::string())) say("written to " + file + " - a tms6747 build stops at the objects");
        else say("cannot write " + file);
        return;
    }
    if (!path::isDirectory(dir)) { say("no such directory: " + dir); return; }
    std::string lib = prompt("a directory with rts6740_elf_eh.lib, or none [" + settings::tilib() + "]: ", cancelled);
    if (cancelled) { say("TI compiler unchanged"); return; }
    if (!lib.empty() && lib != "-" && !path::isDirectory(lib)) { say("no such directory: " + lib); return; }
    if (settings::rememberTi(dir, lib == "-" ? std::string() : lib))
        say("written to " + file + " - a tms6747 build links a .out with " + dir);
    else
        say("cannot write " + file);
}

// The project's own assembler in place of ml64 and clang, for x86_64-windows: named by its path
// and kept in settings.json; `-` puts the compilers' own choice back. See settings::assembler.
void Editor::locateAssembler() {
    std::string file = settings::installFile();
    if (file.empty()) { say("no installation directory to keep this in"); return; }
    bool cancelled = false;
    std::string as = prompt("assembler for x86_64-windows, full path [" + settings::assembler() + "]: ", cancelled);
    if (cancelled || as.empty()) { say("assembler unchanged"); return; }
    if (as != "-" && !path::exists(as)) { say("no such file: " + as); return; }
    if (settings::rememberAssembler(as == "-" ? std::string() : as))
        say("written to " + file + (as == "-" ? " - c90 and cpp11 assemble as they choose again"
                                             : " - c90 and cpp11 assemble through " + as));
    else
        say("cannot write " + file);
}

// The project's own linker in place of Microsoft's, for x86_64-windows: named
// by its path and kept in settings.json; `-` puts link.exe back. See
// settings::linker.
void Editor::locateLinker() {
    std::string file = settings::installFile();
    if (file.empty()) { say("no installation directory to keep this in"); return; }
    bool cancelled = false;
    std::string ld = prompt("linker for x86_64-windows, full path [" + settings::linker() + "]: ", cancelled);
    if (cancelled || ld.empty()) { say("linker unchanged"); return; }
    if (ld != "-" && !path::exists(ld)) { say("no such file: " + ld); return; }
    if (settings::rememberLinker(ld == "-" ? std::string() : ld))
        say("written to " + file + (ld == "-" ? " - a Windows build links with link.exe again"
                                             : " - a Windows build links through " + ld));
    else
        say("cannot write " + file);
}

// The project's own C6000 linker in place of TI's lnk6x, for tms6747: named by its path and kept
// in settings.json; `-` puts TI's back. The runtime it links against is still TI's, named under
// Tools > TI compiler. See settings::tilinker.
void Editor::locateTilinker() {
    std::string file = settings::installFile();
    if (file.empty()) { say("no installation directory to keep this in"); return; }
    bool cancelled = false;
    std::string ld = prompt("linker for tms6747, full path [" + settings::tilinker() + "]: ", cancelled);
    if (cancelled || ld.empty()) { say("linker unchanged"); return; }
    if (ld != "-" && !path::exists(ld)) { say("no such file: " + ld); return; }
    if (settings::rememberTilinker(ld == "-" ? std::string() : ld))
        say("written to " + file + (ld == "-" ? " - a tms6747 build links with TI's lnk6x again"
                                             : " - a tms6747 build links through " + ld));
    else
        say("cannot write " + file);
}

void Editor::removeFromProject() {
    if (!project_.loaded()) { say("there is no project open"); return; }
    if (buf_.path().empty()) { say("this buffer has no name to look for"); return; }

    Outcome done = editor::removeExisting(project_, buf_.path());
    say(done.message);
    if (done.ok) refreshTree();
}

// Name, then where: the directory offered is the open project's, or the one the editor stands
// in, and a new name under it is made. The window asks the same two with a folder picker. A
// project already open is closed first, its files with it, as Project > Close would.
void Editor::newProject() {
    bool cancelled = false;
    std::string name = prompt("project name: ", cancelled);
    if (cancelled || name.empty()) { say("no project made"); return; }

    std::string offered = path::absolute(projectDir_.empty() ? "." : projectDir_);
    std::string where = prompt("in directory [" + offered + "]: ", cancelled);
    if (cancelled) { say("no project made"); return; }
    if (where.empty()) where = offered;
    else if (where[0] != '/' && !(where.size() > 1 && where[1] == ':')) where = path::join(offered, where);

    if (project_.loaded()) {
        std::string was = project_.name();
        closeProject();
        if (project_.loaded()) return;      // an unsaved file kept it open, and said so
        say(was + " closed");
    }
    projectDir_ = where;
    Outcome done = editor::beginProject(project_, where, name, buf_.path());
    say(done.message);
    if (done.ok) {
        paneMode_ = PaneProject;
        refreshTree();
        settings::rememberProject(project_.file());
        refreshRecent();
    }
}

void Editor::saveProjectAs() {
    if (!project_.loaded()) { say("there is no project to save"); return; }

    std::string offered = project_.name() + Project::suffix();

    bool cancelled = false;
    std::string name = prompt("save the project as [" + offered + "]: ", cancelled);
    if (cancelled) { say("not saved"); return; }
    if (name.empty()) name = offered;

    if (name.size() <= 4 || name.rfind(Project::suffix()) != name.size() - 4)
        name += Project::suffix();

    std::string error;
    if (!project_.saveAs(path::join(project_.root(), name), error)) {
        say(error.empty() ? "could not save the project" : error);
        return;
    }
    refreshTree();
    settings::rememberProject(project_.file());
        refreshRecent();
    say(name + " written - the project is saved there from now on");
}

// The target and the compiler are the project's while one is open - written to its .pro, as the
// manual promised - and the installation's default otherwise. Until 2026-09-19 the Target menu
// wrote nothing and the Tools menu wrote settings.json whatever was open, so Ctrl-K inside one project changed the compiler of every other.
void Editor::chooseArch(size_t which) {
    arch_ = which;
    resetDebug();
    std::string said = std::string("target: ") + kArches[arch_];
    ToolchainKind takes = resolve(tool_, lang_);
    if (!usesArch(takes))
        said += " - a c90, cpp11 or shalimar setting; " + toolchainShown(tool_, takes) + " builds for its own host";
    if (project_.loaded()) {
        project_.setArch(kArches[arch_]);
        std::string error;
        said += project_.save(error) ? " - written to " + path::filename(project_.file())
                                     : " - but " + error;
    }
    say(said);
}

void Editor::chooseTool(ToolchainKind kind, const std::string& named) {
    tool_.kind = kind;
    resetDebug();
    std::string said = "compiler: " + named +
                       (kind == ToolAuto ? " - this one goes to " + std::string(toolchainName(resolve(tool_, lang_)))
                                         : std::string(", for every file"));
    if (project_.loaded()) {
        project_.setToolchain(kind);
        std::string error;
        said += project_.save(error) ? " - written to " + path::filename(project_.file())
                                     : " - but " + error;
    } else {
        settings::rememberDefaultCompiler(toolchainWord(kind));
    }
    say(said);
}

void Editor::resetDebug() {

    debug_.clear();
    ToolchainKind kind = resolve(tool_, lang_);

    std::vector<std::string> note = debugNote(kind, kArches[arch_]);
    for (size_t i = 0; i < note.size(); ++i) debug_.push_back(note[i]);
    debug_.push_back("");

    std::vector<std::string> said = describe(symbolsIn(assembly_));
    for (size_t i = 0; i < said.size(); ++i) debug_.push_back(said[i]);

    debug_.push_back("");
    debug_.push_back("target: " + std::string(kArches[arch_]));
    debug_.push_back("build:  " + std::string(configName(config_)) + " (" +
                     configFlags(kind, config_, kArches[arch_]) + " )");
}

void Editor::goToFrame() {
    const std::vector<std::string>& lines = panelLines();
    if (panelOff_ >= lines.size()) { say("there is nothing on that line"); return; }

    size_t which = dbg_frameOnLine(stack_, lines[panelOff_]);

    if (which >= stack_.size() && !stack_.empty() &&
        lines[panelOff_] == dbg_stopLine(stopFile_, stopLine_, stopFunction_))
        which = 0;

    if (which >= stack_.size()) {

        size_t variable = dbg_variableOnLine(locals_, lines[panelOff_]);
        if (variable < locals_.size()) { editVariable(variable); return; }

        size_t watch = dbg_watchOnLine(debugger_.watches(), lines[panelOff_]);
        if (watch < debugger_.watches().size()) { editWatch(watch); return; }

        say("that line is neither a frame nor a variable nor a watch");
        return;
    }

    lookAt(which);
}

void Editor::watchExpression() {
    bool cancelled = false;
    std::string what = prompt("watch: ", cancelled);
    if (cancelled || what.empty()) { say("nothing to watch"); return; }

    if (shm_.running()) {
        say(std::string(shalimar::saysWhereOnly()) + " - nothing to watch with");
        return;
    }

    debugger_.addWatch(what);
    panelOpen_ = true;
    tab_ = TabDebug;
    if (stopLine_ > 0) writeDebugTab();
    say(debugger_.running() ? "watching " + what
                            : "watching " + what + " - it is read when the program stops");
}

void Editor::editWatch(size_t which) {
    if (which >= debugger_.watches().size()) return;
    const std::string was = debugger_.watches()[which].expression;

    bool cancelled = false;
    std::string what = prompt("watch " + was + ", or empty to drop it: ", cancelled);
    if (cancelled) { say(was + " is still watched"); return; }

    debugger_.setWatch(which, what);
    writeDebugTab();
    say(what.empty() ? "stopped watching " + was : "watching " + what);
}

void Editor::editVariable(size_t which) {
    if (which >= locals_.size()) return;

    const std::string name = locals_[which].name;
    const std::string was = locals_[which].value;

    bool cancelled = false;
    std::string value = prompt("set " + name + " (" + was + ") to: ", cancelled);
    if (cancelled || value.empty()) { say(name + " is still " + was); return; }

    std::string said;
    if (!debugger_.setVariable(name, value, &said)) {
        say(said.empty() ? "the debugger would not set " + name : said);
        return;
    }

    locals_ = debugger_.locals();
    writeDebugTab();

    std::string now = value;
    for (size_t i = 0; i < locals_.size(); ++i)
        if (locals_[i].name == name) now = locals_[i].value;
    say(name + " is " + now + " now");
}

void Editor::lookAlongStack(int by) {
    if (!debugging() || stack_.empty()) {
        say("nothing is stopped, so there is no stack to walk");
        return;
    }

    if (shm_.running()) {
        say(shalimar::saysHowDeepOnly());
        return;
    }
    if (by > 0) {
        if (looking_ + 1 >= stack_.size()) {
            say("nothing called " + stack_[stack_.size() - 1].function + ", which is the top");
            return;
        }
        lookAt(looking_ + 1);
        return;
    }
    if (looking_ == 0) {
        say("this is where the program stopped - there is nothing below it");
        return;
    }
    lookAt(looking_ - 1);
}

void Editor::lookAt(size_t which) {
    if (which >= stack_.size()) return;

    if (!shm_.running() && !debugger_.selectFrame(which)) {
        say("the debugger would not go to that frame");
        return;
    }
    looking_ = which;
    if (!shm_.running()) locals_ = debugger_.locals();
    writeDebugTab();
    panelOff_ = 0;

    const StackFrame& frame = stack_[which];
    if (!frame.file.empty() && path::filename(frame.file) != path::filename(buf_.path())) {
        std::string full = whereThatFileIs(frame.file);
        if (full.empty()) {
            say(path::filename(frame.file) + " is not in this project");
            return;
        }
        open(full);
    }

    if (frame.line > 0) {
        cy_ = frame.line - 1;
        if (cy_ >= buf_.lineCount()) cy_ = buf_.lineCount() - 1;
        cx_ = 0;
        clampCursor();
    }
    focus_ = FocusText;
    say(which == 0 ? path::filename(frame.file) + ":" + number(frame.line) + " in " +
                         frame.function + " - back where it stopped"
                   : path::filename(frame.file) + ":" + number(frame.line) + " in " +
                         frame.function + " - where the call came from");
}

void Editor::goToProblem() {
    if (!lastDiag_.present) { say("no error to go to"); return; }
    cy_ = lastDiag_.line - 1;
    if (cy_ >= buf_.lineCount()) cy_ = buf_.lineCount() - 1;
    cx_ = lastDiag_.col - 1;
    clampCursor();
    focus_ = FocusText;
    say(number(lastDiag_.line) + ":" + number(lastDiag_.col) + ": " + lastDiag_.message);
}

void Editor::convertFile() {

    bool toShalimar = false;
    if (!convertsFrom(lang_, &toShalimar)) {
        say(std::string("c2s converts between C and Shalimar, and this is ") +
            languageName(lang_) + " - set the language above if that is wrong");
        return;
    }

    const std::string converter = c2s_.empty() ? findConverter() : c2s_;
    if (converter.empty()) {
        say("no c2s beside this editor - set C2S, or build Converter-C2S here");
        return;
    }

    if (buf_.dirty() || buf_.path().empty()) {
        if (!save()) return;
    }

    const std::string produced = convertedName(buf_.path(), toShalimar);
    if (produced.empty()) {
        say("this file has no name to convert - save it first");
        return;
    }
    if (produced == buf_.path()) {

        say(std::string("that would write over ") + baseName(buf_.path()) +
            " - it is already what you asked for");
        return;
    }

    panelOpen_ = true;
    tab_ = TabConsole;
    console_.clear();
    console_.push_back("$ " + baseName(converter) + " " +
                       (toShalimar ? "--to-shalimar " : "--to-c ") +
                       baseName(buf_.path()) + " -o " + baseName(produced));
    panelOff_ = 0;
    say(std::string("converting ") + baseName(buf_.path()) + " ...");
    refresh();

    Conversion result = convert(converter, buf_.path(), produced, toShalimar,
                                consoleSink, this);

    if (!result.ran) {
        say(std::string("could not run ") + converter);
        return;
    }

    // Nothing on disk, so nothing to open. The console holds why - c2s's questions, or a file it
    // could not read - and its last line is the summary the status line can hold. Until 2026-08-27
    // this branch was reached only for the unreadable file: a refusal was taken for a conversion and an empty buffer opened, said to be written with parts marked BEYOND.
    if (result.produced.empty()) {
        const std::string why = console_.empty() ? std::string("see the console")
                                                 : console_.back();
        say(baseName(buf_.path()) + " - not converted: " + why);
        return;
    }

    open(result.produced);
    if (result.ok) {
        say(baseName(result.produced) + " - converted");
    } else {

        say(baseName(result.produced) +
            " - written with unconverted parts marked; search for BEYOND");
    }
}

void Editor::compile() {

    ToolchainKind kind = resolve(tool_, lang_);
    if (!canCompile(kind, lang_)) {
        say(refusal(kind, lang_));
        return;
    }

    if (buf_.dirty() || buf_.path().empty()) {
        if (!save()) return;
    }

    panelOpen_ = true;
    tab_ = TabConsole;
    console_.clear();

    Toolchain shownAs = toolFor();
    shownAs.cc1 = baseName(tool_.cc1);
    shownAs.cxx1 = baseName(tool_.cxx1);
    shownAs.cl = baseName(tool_.cl);
    std::string shownFile = project_.loaded() ? project_.relative(buf_.path())
                                              : baseName(buf_.path());
    console_.push_back("$ " + shownCommand(shownAs, kind, shownFile, lang_,
                                           kArches[arch_], config_));
    panelOff_ = 0;
    say(std::string("building ") + configName(config_) + " with " + toolchainName(kind) +
        (usesArch(kind) ? std::string(" for ") + kArches[arch_] : std::string()) +
        " ...");
    refresh();

    Build result = build(toolFor(), kind, buf_.path(), lang_, kArches[arch_], config_,
                         consoleSink, this);

    assembly_ = result.asmLines;
    lastDiag_ = result.diag;
    resetDebug();

    if (result.diag.present) {

        cy_ = result.diag.line - 1;
        if (cy_ >= buf_.lineCount()) cy_ = buf_.lineCount() - 1;
        cx_ = result.diag.col - 1;
        clampCursor();
        focus_ = FocusText;
        tab_ = TabConsole;
        console_.push_back("");
        console_.push_back("[enter] here goes to the line above");
        // Rows, not lines: a long command wraps, and counting lines left the last one under the frame.
        panelOff_ = panelTopForEnd();
        say(number(result.diag.line) + ":" + number(result.diag.col) + ": error: " +
            result.diag.message);
    } else if (!result.ok) {
        console_.push_back(std::string(toolchainName(kind)) + " failed");
        say(std::string(toolchainName(kind)) + " failed - see the console");
    } else {
        console_.push_back("ok - " + number(assembly_.size()) + " lines of assembly");
        tab_ = TabAssembly;
        panelOff_ = 0;
        say(number(assembly_.size()) + " lines of " +
            (usesArch(kind) ? kArches[arch_] : toolchainName(kind)) + " assembly");
    }
}

void Editor::buildAndRun() {
    // One source of several: the project is what runs (Project::runsAsProject).
    if (size_t of = project_.runsAsProject(buf_.path())) {
        say(baseName(buf_.path()) + " is one of " + std::to_string(of) + " sources of " +
            project_.name() + " - running the project");
        buildProject(true);
        return;
    }
    ToolchainKind kind = resolve(tool_, lang_);
    if (!canCompile(kind, lang_)) {
        say(refusal(kind, lang_));
        return;
    }
    if (!runsHere(kind, kArches[arch_])) {
        say(whyNotRun(kind, kArches[arch_]));
        return;
    }

    if (buf_.dirty() || buf_.path().empty()) {
        if (!save()) return;
    }

    panelOpen_ = true;
    tab_ = TabConsole;
    console_.clear();

    Toolchain shownAs = toolFor();
    shownAs.cc1 = baseName(tool_.cc1);
    shownAs.cxx1 = baseName(tool_.cxx1);
    shownAs.cl = baseName(tool_.cl);
    std::string shownFile = project_.loaded() ? project_.relative(buf_.path())
                                              : baseName(buf_.path());
    // A single file has no .pro, so no emulateOnly: its Run links the .out, its Emulate does not (R5).
    const int runner = runnerFor(runner_, kArches[arch_], false);
    console_.push_back("$ " + shownProgramCommand(shownAs, kind, shownFile, lang_,
                                                  kArches[arch_], config_, runner == RunEmulator));
    panelOff_ = 0;
    say(std::string("building and running with ") + toolchainName(kind) + " ...");
    refresh();

    Ran result;
    if (runner == RunAsked) {
        result = runProgram(toolFor(), kind, buf_.path(), lang_, kArches[arch_], config_, consoleSink, this);
    } else {
        Toolchain linking = toolFor();
        linking.linkSingleFile = runner != RunEmulator;
        Built made = buildProgram(linking, kind, buf_.path(), lang_, kArches[arch_], config_, consoleSink, this);
        result.output = made.output;
        result.diag = made.diag;
        result.built = made.ok;
        if (made.ok) result = runChosen(runner, made.program, made.shalimar, std::vector<std::string>());
        removeProgram(made);
    }

    lastDiag_ = result.diag;

    if (result.diag.present) {
        cy_ = result.diag.line - 1;
        if (cy_ >= buf_.lineCount()) cy_ = buf_.lineCount() - 1;
        cx_ = result.diag.col - 1;
        clampCursor();
        focus_ = FocusText;
        console_.push_back("");
        console_.push_back("[enter] here goes to the line above");
        say(number(result.diag.line) + ":" + number(result.diag.col) + ": error: " +
            result.diag.message);
    } else if (!result.built) {
        console_.push_back(std::string(toolchainName(kind)) + " built no program");
        say(std::string(toolchainName(kind)) + " built no program - see the console");
    } else {

        console_.push_back("");
        console_.push_back("[program returned " + number(static_cast<size_t>(result.status)) + "]");
        say("ran " + shownFile + " - it returned " + number(static_cast<size_t>(result.status)));
    }

    // Rows, not lines: a long command wraps, and counting lines left "[program returned N]" under the frame.
    panelOff_ = panelTopForEnd();
}

// **Build > Emulate on vm6747 and Build > Verify.** The same build a Run makes - the project when one
// is open and the file is its own, the file otherwise - with what runs the program changed for that run.
void Editor::runWith(int runner) {
    if (std::string(kArches[arch_]) != "tms6747") {
        say(runner == RunEmulator ? "Emulate on vm6747 is for tms6747 - choose it under Target"
                                  : "Verify is for tms6747 - choose it under Target");
        return;
    }
    runner_ = runner;
    const bool held = !buf_.path().empty() &&
                      project_.groupOf(project_.relative(buf_.path())) < project_.groups().size();
    if (project_.loaded() && (buf_.path().empty() || held))
        buildProject(true);
    else
        buildAndRun();
    runner_ = 0;
}

Ran Editor::runChosen(int runner, const std::string& program, bool shalimar, const std::vector<std::string>& args) {
    if (runner == RunVerify) return verifyBuilt(program, shalimar, args, consoleSink, this);
    if (runner == RunEmulator) console_.push_back(runnerLine(runner, program));
    if (runner == RunSimulator) {
        Ran result;
        result.built = true;
        const std::string why = simulationMissing(program);
        if (!why.empty()) { console_.push_back(why); result.status = 2; return result; }
        if (!args.empty()) console_.push_back("[run] the simulator takes no command line - Emulate on vm6747 hands them over");
        console_.push_back(runnerLine(runner, program));
        result.ran = true;
        result.status = runCaptured(simulateCommand(tiProgramOf(program)), result.output, consoleSink, this);
        return result;
    }
    return runBuilt(program, consoleSink, this, shalimar, args);
}

bool Editor::saveEveryDirty() {
    stash();
    for (size_t i = 0; i < docs_.size(); ++i) {
        if (!docs_[i].buf.dirty() || docs_[i].buf.path().empty()) continue;
        std::string error;
        if (!docs_[i].buf.save(error)) {
            say(error);
            restore();
            return false;
        }
        docs_[i].buf.breakRun();
    }
    restore();
    return true;
}

std::string Editor::compilersNamed(const std::vector<Part>& parts) const {
    std::vector<std::string> named;
    for (size_t i = 0; i < parts.size(); ++i) {
        std::string word = toolchainShown(tool_, toolchainOf(tool_, parts[i]));
        bool already = false;
        for (size_t j = 0; j < named.size(); ++j)
            if (named[j] == word) already = true;
        if (!already) named.push_back(word);
    }
    std::string all = named.empty() ? std::string() : named[0];
    for (size_t i = 1; i < named.size(); ++i)
        all += (i + 1 == named.size() ? " and " : ", ") + named[i];
    return all;
}

// **Build > Clean**, as the windows have it: what the build made removed (compile.h's cleanBuilt), the
// Console emptied, and the result said in so many words.
void Editor::clean() {
    std::vector<std::string> removed =
        cleanBuilt(project_.loaded() ? project_.targetProgram() : std::string(), project_.loaded() && project_.isCcs());
    const std::string name = project_.loaded() ? project_.name() : std::string("no project");
    const std::string head = "Clean succeeded: " + name + " - " +
                             (removed.empty() ? std::string("nothing was left to remove")
                                              : number(removed.size()) + " removed");
    console_.clear();
    console_.push_back("========== " + head + " ==========");
    for (size_t i = 0; i < removed.size(); ++i) console_.push_back(removed[i]);
    lastDiag_ = Diagnostic();
    panelOpen_ = true;
    tab_ = TabConsole;
    panelOff_ = 0;
    say(head);
}

void Editor::buildProject(bool andRun) {
    std::vector<Part> parts;
    std::string why, detail;
    // A CCS project is read again for every build, so an edit made in CCS is what is built.
    if (project_.isCcs() && !project_.reloadIfCcs(why)) {
        say(why);
        return;
    }
    if (!project_.targetParts(parts, why, &detail)) {

        say(why);
        if (!detail.empty()) {
            panelOpen_ = true;
            tab_ = TabConsole;
            console_.clear();
            console_.push_back(why);
            console_.push_back("");
            wrapInto(console_, detail, static_cast<size_t>(screenCols_ - 4));
            panelOff_ = 0;
        }
        return;
    }

    for (size_t i = 0; i < parts.size(); ++i) {
        ToolchainKind kind = toolchainOf(tool_, parts[i]);
        if (!canCompile(kind, parts[i].lang)) { say(refusal(kind, parts[i].lang)); return; }
        if (andRun && !runsHere(kind, kArches[arch_])) {
            say(whyNotRun(kind, kArches[arch_]));
            return;
        }
    }

    if (!saveEveryDirty()) return;

    panelOpen_ = true;
    tab_ = TabConsole;
    console_.clear();
    if (project_.isCcs()) {
        std::vector<std::string> lines = project_.ccsReport(config_);
        for (size_t i = 0; i < lines.size(); ++i) console_.push_back(lines[i]);
    }

    std::string program = project_.targetProgram();
    size_t count = 0;
    for (size_t i = 0; i < parts.size(); ++i) count += parts[i].sources.size();

    console_.push_back("$ " + compilersNamed(parts) + " " + number(count) +
                       (count == 1 ? " source -o " : " sources -o ") +
                       project_.relative(program));
    for (size_t i = 0; i < parts.size(); ++i)
        for (size_t f = 0; f < parts[i].sources.size(); ++f)
            console_.push_back("    " + project_.relative(parts[i].sources[f]));
    panelOff_ = 0;
    say("building " + baseName(program) + " with " + compilersNamed(parts) + " ...");
    refresh();

    // On tms6747 a Run is the .out on sim6747 and an Emulate builds no .out (R5).
    const int runner = andRun ? runnerFor(runner_, kArches[arch_], project_.emulateOnly()) : RunAsked;
    Toolchain building = toolFor();
    building.emulating = runner == RunEmulator;
    Built made = buildParts(building, parts, kArches[arch_], config_, program,
                            consoleSink, this);

    const std::string compilers = compilersNamed(parts);

    lastDiag_ = made.diag;
    lastBuildOk_ = made.ok && !made.diag.present;
    lastRunStatus_ = 0;

    if (made.diag.present) {

        std::string where = made.diag.file;
        if (!where.empty() && !path::exists(where)) where = project_.absolute(where);
        if (!where.empty() && path::exists(where) && where != buf_.path()) open(where);

        cy_ = made.diag.line > 0 ? made.diag.line - 1 : 0;
        if (cy_ >= buf_.lineCount()) cy_ = buf_.lineCount() - 1;
        cx_ = made.diag.col > 0 ? made.diag.col - 1 : 0;
        clampCursor();
        focus_ = FocusText;
        say(baseName(made.diag.file) + ":" + number(made.diag.line) + ":" +
            number(made.diag.col) + ": error: " + made.diag.message);
    } else if (!made.ok) {
        say(compilers + " did not build it - see the console");
    } else if (!andRun) {
        console_.push_back("");
        console_.push_back("[built " + program + "]");
        say("built " + project_.relative(program) + " from " + number(count) +
            (count == 1 ? " source" : " sources"));
    } else {
        console_.push_back("");
        // What was built, which for the emulated target is <program>.vm.
        std::vector<std::string> args = project_.absoluteTargetArgs();
        //  Say what is being run, arguments and all. A project whose program is a tool prints its
        //  usage when handed nothing, and the console should show that nothing was handed to it
        //  rather than leave the usage looking like a fault.
        std::string shown = project_.relative(made.program);
        for (size_t a = 0; a < project_.targetArgs().size(); ++a)
            shown += " " + project_.targetArgs()[a];
        if (runner == RunAsked) console_.push_back("$ " + shown);
        Ran result = runChosen(runner, made.program, made.shalimar, args);
        lastRunStatus_ = result.status;
        console_.push_back("[program returned " + number(static_cast<size_t>(result.status)) + "]");
        say("ran " + project_.relative(program) + " - it returned " +
            number(static_cast<size_t>(result.status)));
    }

    // Rows, not lines: a long command wraps, and counting lines left "[program returned N]" under the frame.
    panelOff_ = panelTopForEnd();
}

bool Editor::breakpointOn(size_t line) const {
    if (buf_.path().empty()) return false;
    std::map<std::string, FileBreaks>::const_iterator found =
        breaks_.find(path::oneName(buf_.path()));
    if (found == breaks_.end()) return false;
    return found->second.lines.count(line) > 0;
}

void Editor::toggleBreak() {
    if (buf_.path().empty()) { say("save the file first - a breakpoint is on a line of a file"); return; }

    size_t line = cy_ + 1;
    FileBreaks& file = breaks_[path::oneName(buf_.path())];
    if (file.path.empty()) file.path = buf_.path();
    std::set<size_t>& here = file.lines;
    if (here.count(line)) {
        here.erase(line);
        if (debugging()) {

            if (shm_.running()) {
                shm_.clearBreakpoints();
                for (std::set<size_t>::iterator it = here.begin(); it != here.end(); ++it)
                    shm_.breakAt(file.path, *it);
            } else {
                debugger_.clearBreakpoints();
                for (std::set<size_t>::iterator it = here.begin(); it != here.end(); ++it)
                    debugger_.breakAt(file.path, *it);
            }
        }
        say("breakpoint off line " + number(line));
        return;
    }

    here.insert(line);
    if (shm_.running()) shm_.breakAt(file.path, line);
    else if (debugger_.running()) debugger_.breakAt(file.path, line);
    say("breakpoint on line " + number(line));
}

std::string Editor::whereThatFileIs(const std::string& named) const {
    if (named.empty()) return std::string();
    if (path::exists(named)) return named;

    std::string underRoot = project_.absolute(named);
    if (path::exists(underRoot)) return underRoot;

    std::string leaf = path::filename(named);
    const std::vector<Group>& groups = project_.groups();
    for (size_t g = 0; g < groups.size(); ++g)
        for (size_t f = 0; f < groups[g].files.size(); ++f)
            if (path::filename(groups[g].files[f]) == leaf) {
                std::string full = project_.absolute(groups[g].files[f]);
                if (path::exists(full)) return full;
            }
    return std::string();
}

void Editor::sayIfSettingsWereBad() {
    std::string kept = settings::setAside();
    if (kept.empty()) return;
    say("bad configuration file - kept as " + baseName(kept) + ", a new one made");
}

void Editor::sayLines(std::vector<std::string>& into, const std::string& text) {
    size_t from = 0;
    while (from <= text.size()) {
        size_t end = text.find('\n', from);
        std::string line = text.substr(from, end == std::string::npos ? std::string::npos
                                                                      : end - from);
        while (!line.empty() && line[line.size() - 1] == '\r') line.resize(line.size() - 1);
        if (!line.empty()) into.push_back(line);
        if (end == std::string::npos) break;
        from = end + 1;
    }
}

void Editor::showStop(const Stop& where) {
    panelOpen_ = true;
    tab_ = TabDebug;
    panelOff_ = 0;
    debug_.clear();

    const std::string printed = shm_.ownsTheStop()
                                    ? where.said
                                    : dbg_programOutput(debugger_.kind(), where.said);
    if (!printed.empty()) sayLines(console_, printed);

    if (where.exited) {
        stopFile_.clear();
        stopLine_ = 0;
        locals_.clear();
        stack_.clear();
        looking_ = 0;
        debug_.push_back("the program ran to the end and returned " + number(static_cast<size_t>(where.status)));
        debug_.push_back("");
        debug_.push_back("F8 starts it again. The breakpoints are still where you put them.");
        debugStop();
        say("the program returned " + number(static_cast<size_t>(where.status)));
        return;
    }

    if (!where.stopped) {

        if (dbg_stoppedWithNoSource(where.said)) {
            stopFile_.clear();
            stopLine_ = 0;
            locals_.clear();
            stack_.clear();
            looking_ = 0;
            debug_.push_back("stopped where there is no source to show");
            debug_.push_back("");
            debug_.push_back("Stepping past the end of main arrives in the code that");
            debug_.push_back("started it, which was not compiled here. F8 carries on to");
            debug_.push_back("the end, and Stop debugging leaves it.");
            debug_.push_back("");
            sayLines(debug_, where.said);
            say("stopped where there is no source - F8 carries on");
            return;
        }

        debug_.push_back("the debugger stopped answering");
        debug_.push_back("");
        sayLines(debug_, where.said);
        debugStop();
        say("the debugger stopped answering - see the Debug tab");
        return;
    }

    stopFile_ = where.file;
    stopLine_ = where.line;

    if (!where.file.empty() && path::filename(where.file) != path::filename(buf_.path())) {
        std::string full = whereThatFileIs(where.file);
        if (!full.empty()) open(full);
    }

    if (debuggingShalimar()) {
        locals_.clear();
        stack_ = shm_.frames();
    } else {
        locals_ = debugger_.locals();
        stack_ = debugger_.frames();
    }

    if (path::filename(where.file) == path::filename(buf_.path()) && where.line > 0) {
        cy_ = where.line - 1;
        if (cy_ >= buf_.lineCount()) cy_ = buf_.lineCount() - 1;
        cx_ = 0;
        clampCursor();
        focus_ = FocusText;
    }

    stopFunction_ = where.function;
    looking_ = 0;
    writeDebugTab();

    say(path::filename(where.file) + ":" + number(where.line) +
        (where.function.empty() ? std::string() : " in " + where.function));
}

void Editor::writeDebugTab() {
    debug_.clear();
    debug_.push_back(dbg_stopLine(stopFile_, stopLine_, stopFunction_));
    debug_.push_back("");

    if (looking_ > 0 && looking_ < stack_.size()) {
        debug_.push_back(dbg_lookingAt(stack_[looking_]));
        debug_.push_back("");
    }

    if (locals_.empty()) {

        debug_.push_back(debuggingShalimar()
                             ? "  (" + std::string(shalimar::saysWhereOnly()) + ")"
                             : std::string("  (nothing in scope here)"));
    } else {
        for (size_t i = 0; i < locals_.size(); ++i)
            debug_.push_back(dbg_variableLine(locals_[i]));
    }

    const std::vector<Watch>& watching = debugger_.watches();
    if (!watching.empty()) {
        debug_.push_back("");
        debug_.push_back("watching");
        for (size_t i = 0; i < watching.size(); ++i)
            debug_.push_back(dbg_watchLine(watching[i]));
    }

    if (stack_.size() > 1) {
        debug_.push_back("");
        debug_.push_back("called from");
        for (size_t i = 1; i < stack_.size(); ++i)
            debug_.push_back(dbg_frameLine(stack_[i], i == looking_));
    }

    debug_.push_back("");
    debug_.push_back("F8 carries on   F7 steps over   F6 steps into   F9 sets a breakpoint");

    if (debuggingShalimar()) return;

    debug_.push_back("Ctrl-W puts the cursor in the panel; Enter on a variable sets it");
    if (!watching.empty())
        debug_.push_back("Enter on a watch changes it, and an empty answer takes it away");
    if (stack_.size() > 1) {
        debug_.push_back("Ctrl-Up looks at what called this   Ctrl-Down comes back down");
        debug_.push_back("Enter on a frame looks at it, and on the top line goes back");
    }
}

bool Editor::debuggingShalimar() const { return shm_.running(); }
bool Editor::debugging() const { return debugger_.running() || shm_.running(); }

void Editor::debug(bool project) {
    if (shm_.running()) { showStop(shm_.resume()); return; }
    if (debugger_.running()) { showStop(debugger_.resume()); return; }

    std::vector<Part> parts;
    if (project) {
        std::string why, detail;
        if (!project_.targetParts(parts, why, &detail)) {
            say(why);
            if (!detail.empty()) {
                panelOpen_ = true;
                tab_ = TabConsole;
                console_.clear();
                console_.push_back(why);
                console_.push_back("");
                wrapInto(console_, detail, static_cast<size_t>(screenCols_ - 4));
                panelOff_ = 0;
            }
            return;
        }
    } else {
        Part one;
        one.lang = lang_;
        one.sources.push_back(buf_.path());
        parts.push_back(one);
    }

    for (size_t i = 0; i < parts.size(); ++i) {
        ToolchainKind each = toolchainOf(tool_, parts[i]);
        if (!canCompile(each, parts[i].lang)) { say(refusal(each, parts[i].lang)); return; }
        if (!runsHere(each, kArches[arch_])) { say(whyNotRun(each, kArches[arch_])); return; }
    }

    const DebugPlan plan = dbg_planFor(tool_, parts, kArches[arch_]);
    ToolchainKind kind = plan.kind;
    const DebuggerKind engine = plan.engine;
    const bool stopsItself = plan.stopsItself;
    const std::vector<std::string>& blind = plan.blind;
    if (!plan.possible()) {
        say(dbg_whyNot(kind, kArches[arch_]));
        return;
    }
    if (config_ != ConfigDebug) {
        say(stopsItself ? std::string(shalimar::releaseHasNoSession()) + " - Ctrl-D, then F8"
                     : std::string("release is built without -g - Ctrl-D for debug, then F8"));
        return;
    }
    if (project) {
        if (!saveEveryDirty()) return;
    } else if (buf_.dirty() || buf_.path().empty()) {
        if (!save()) return;
    }

    panelOpen_ = true;
    tab_ = TabConsole;
    console_.clear();
    console_.push_back(project ? "$ building the project for the debugger"
                               : "$ building for the debugger");
    if (project)
        for (size_t i = 0; i < parts.size(); ++i)
            for (size_t f = 0; f < parts[i].sources.size(); ++f)
                console_.push_back("    " + project_.relative(parts[i].sources[f]));

    for (size_t i = 0; i < blind.size(); ++i)
        console_.push_back("  (" + blind[i] + " carries no debug information - the "
                           "debugger cannot stop in it)");

    panelOff_ = 0;
    say("building for the debugger ...");
    refresh();

    debugTemporary_ = !project;
    if (project)
        debugBuilt_ = buildParts(toolFor(), parts, kArches[arch_], config_,
                                 project_.targetProgram(), consoleSink, this);
    else {
        // A single file's debug build links the .out beside its .s as its Run does (R5): on tms6747 that is
        // the image the Debug session chooses sim6747 by when it carries the program's lines (dbg_c6xBackend).
        Toolchain linking = toolFor();
        linking.linkSingleFile = isEmulated(kArches[arch_]);
        debugBuilt_ = buildProgram(linking, kind, buf_.path(), parts[0].lang, kArches[arch_],
                                   config_, consoleSink, this);
    }
    lastDiag_ = debugBuilt_.diag;
    if (!debugBuilt_.ok) {
        if (debugBuilt_.diag.present) {
            std::string where = debugBuilt_.diag.file;
            if (!where.empty() && !path::exists(where)) where = project_.absolute(where);
            if (!where.empty() && path::exists(where) && where != buf_.path()) open(where);
            cy_ = debugBuilt_.diag.line - 1;
            if (cy_ >= buf_.lineCount()) cy_ = buf_.lineCount() - 1;
            cx_ = debugBuilt_.diag.col - 1;
            clampCursor();
            focus_ = FocusText;
            say(number(debugBuilt_.diag.line) + ":" + number(debugBuilt_.diag.col) + ": error: " +
                debugBuilt_.diag.message);
        } else {
            say(std::string(toolchainName(kind)) + " built no program - see the console");
        }
        if (debugTemporary_) removeProgram(debugBuilt_);
        debugBuilt_ = Built();
        return;
    }

    if (stopsItself) {

        if (!shm_.start(debugBuilt_.program, sessionCommand(debugBuilt_.program))) {
            console_.push_back(shalimar::didNotArm());
            say("built without --debug, so there is nothing in it to stop - Ctrl-D, then F8");
            if (debugTemporary_) removeProgram(debugBuilt_);
            debugBuilt_ = Built();
            return;
        }
    } else if (!debugger_.start(engine, debugBuilt_.program)) {
        const char* named = dbg_name(engine);
        console_.push_back(std::string(named) + " could not be started");
        say(std::string(named) + " could not be started - is it installed?");
        if (debugTemporary_) removeProgram(debugBuilt_);
        debugBuilt_ = Built();
        return;
    }

    size_t set = 0;
    for (std::map<std::string, FileBreaks>::iterator file = breaks_.begin();
         file != breaks_.end(); ++file)
        for (std::set<size_t>::iterator line = file->second.lines.begin();
             line != file->second.lines.end(); ++line)
            if (stopsItself ? shm_.breakAt(file->second.path, *line)
                            : debugger_.breakAt(file->second.path, *line)) ++set;

    // On tms6747 the backend is a choice (dbg_c6xBackend): which one, and why the emulator when the
    // simulator was passed over, go in the line.
    console_.push_back(std::string("started ") +
                       (stopsItself ? "the program itself" : debugger_.backend()) +
                       " with " + number(set) + " breakpoint" + (set == 1 ? "" : "s") +
                       (debugger_.backendWhy().empty() ? std::string() : " (" + debugger_.backendWhy() + ")"));
    showStop(stopsItself ? shm_.run() : debugger_.run());
}

void Editor::debugStep(Action how) {
    if (!debugging()) { say("nothing is running - F8 starts it"); return; }

    if (shm_.running()) {
        if (how == ActionStepInto)      showStop(shm_.stepInto());
        else if (how == ActionStepOut)  showStop(shm_.stepOut());
        else                            showStop(shm_.stepOver());
        return;
    }

    if (how == ActionStepInto)      showStop(debugger_.stepInto());
    else if (how == ActionStepOut)  showStop(debugger_.stepOut());
    else                            showStop(debugger_.stepOver());
}

void Editor::debugStop() {
    if (debugger_.running()) debugger_.stop();
    if (shm_.running()) shm_.stop();
    if (debugTemporary_) removeProgram(debugBuilt_);
    debugBuilt_ = Built();
    stopFile_.clear();
    stopLine_ = 0;
    locals_.clear();
    stack_.clear();
    looking_ = 0;
}

void Editor::showAbout() {
    panelOpen_ = true;
    tab_ = TabConsole;
    console_.clear();

    std::vector<std::string> said = about::lines();
    for (size_t i = 0; i < said.size(); ++i) console_.push_back(said[i]);

    fitPanelTo(said);
    panelOff_ = 0;
    say(std::string(about::name()) + " " + about::version());
}

// Help > Environment in the Console panel, where About is: each tool and the file it resolved to.
void Editor::showEnvironment() {
    panelOpen_ = true;
    tab_ = TabConsole;
    console_.clear();
    std::vector<std::string> said = about::environment();
    for (size_t i = 0; i < said.size(); ++i) console_.push_back(said[i]);
    fitPanelTo(said);
    panelOff_ = 0;
    say("the environment in force");
}

void Editor::showHelpContents() {
    panelOpen_ = true;
    tab_ = TabConsole;
    console_.clear();
    std::vector<std::string> said = help::contents();
    for (size_t i = 0; i < said.size(); ++i) console_.push_back(said[i]);
    panelOff_ = 0;
    say("the manual is in help/ - F1 for the keys");
}

void Editor::showKeys() {
    panelOpen_ = true;
    tab_ = TabConsole;
    console_.clear();
    console_.push_back("F10          the menu             Ctrl-B   build this file");
    console_.push_back("F5           run this file        F4       build the project");
    console_.push_back("F1           these keys");
    console_.push_back("F9 / F8      breakpoint / debug   F7 / F6  step over / into");
    console_.push_back("Ctrl-Up      up the stack         Ctrl-Down down again");
    console_.push_back("             Debug menu: Debug project puts the project's own");
    console_.push_back("             program under the debugger instead of this file");
    console_.push_back("F2 / F3      previous / next file Ctrl-L   line numbers");
    console_.push_back("Ctrl-K       next compiler        Ctrl-T   next target");
    console_.push_back("             automatic, c90, cpp11, shalimar, cl and the host's C++");
    console_.push_back("Ctrl-D       debug or release");
    console_.push_back("Ctrl-W       next pane            Ctrl-T   next target");
    console_.push_back("Ctrl-P       project pane         Ctrl-A   re-indent (selection)");
    console_.push_back("             Edit menu: Plain frame draws it with - | + instead,");
    console_.push_back("             for a console whose font breaks the box characters");
    console_.push_back("Ctrl-E       bottom panel         Tab      lay this line out");
    console_.push_back("Ctrl-F       find                 Ctrl-G   find the next one");
    console_.push_back("Ctrl-R       replace              Ctrl-Z   undo, Ctrl-Y redo");
    console_.push_back("shift+arrows select              Ctrl-C   copy, X cut, V paste");
    console_.push_back("Ctrl-S       save                 Ctrl-Q   leave");
    console_.push_back("In the project pane, enter opens. In the panel, left and right");
    console_.push_back("change tab - Console, Debug, Assembly - and on Console,");
    console_.push_back("enter goes to the line c90 named. On Debug, enter on a frame");
    console_.push_back("looks at it, and on the top line goes back to the stop.");
    panelOff_ = 0;
    say("keys");
}

void Editor::perform(Action action) {
    switch (action) {
        case ActionNew:          newFile(); break;
        case ActionOpen:         openPrompt(); break;
        case ActionSave:         save(); break;
        case ActionSaveAs:       saveAs(); break;
        case ActionQuit:
            if (mayLeave()) {
                editor::rememberOpen(project_, buf_.path());
                running_ = false;
            }
            break;
        case ActionCloseFile:    closeDocument(); break;
        case ActionProjectNew:   newProject(); break;
        case ActionProjectOpen:  openProjectPrompt(); break;
        case ActionProjectSaveAs: saveProjectAs(); break;
        case ActionProjectClose: closeProject(); break;
        case ActionProjectAdd:   addToProject(); break;
        case ActionProjectRemove: removeFromProject(); break;
        case ActionProjectIncludes: editProjectIncludes(); break;
        case ActionProjectRecent:
        case ActionProjectRecent2:
        case ActionProjectRecent3: {
            std::vector<std::string> recent = settings::recentProjects();
            size_t which = action == ActionProjectRecent ? 0 : action == ActionProjectRecent2 ? 1 : 2;
            if (which >= recent.size()) say("no such recent project");
            else openProject(recent[which]);
            break;
        }
        case ActionProjectLibraries: editProjectLibraries(); break;
        case ActionOwnIncludes:  editOwnIncludes(); break;
        case ActionOwnLibraries: editOwnLibraries(); break;
        case ActionCompilerOptions: say("Compiler options are set in the window (RIDEGui): Tools > Compiler options"); break;
        case ActionHeaderDirs:   editHeaderDirs(); break;
        case ActionLocateVcvars: locateVcvars(); break;
        case ActionLocateAssembler: locateAssembler(); break;
        case ActionLocateTi:     locateTi(); break;
        case ActionLocateLinker:   locateLinker(); break;
        case ActionLocateTilinker: locateTilinker(); break;
        case ActionFileCreate:   createFile(); break;
        case ActionFileRename:   renameFile(); break;
        case ActionFileDelete:   deleteFile(); break;
        case ActionFileRegroup:  regroupFile(); break;
        case ActionNextFile:     nextDocument(1); break;
        case ActionPrevFile:     nextDocument(-1); break;
        case ActionFileRecent:
        case ActionFileRecent2:
        case ActionFileRecent3: {
            std::vector<std::string> files = settings::recentFiles();
            size_t which = action == ActionFileRecent ? 0 : action == ActionFileRecent2 ? 1 : 2;
            if (which >= files.size()) say("no such recent file");
            else open(files[which]);
            break;
        }
        case ActionLayOut:       reindentAll(); break;
        case ActionCopy:         copySelection(false); break;
        case ActionCut:          copySelection(true); break;
        case ActionPaste:        pasteClipboard(); break;
        case ActionSelectAll:    selectAll(); break;
        case ActionUndo:         undoEdit(); break;
        case ActionRedo:         redoEdit(); break;
        case ActionFind:         findPrompt(); break;
        case ActionFindNext:     findAgain(true); break;
        case ActionFindPrevious: findAgain(false); break;
        case ActionReplace:      replacePrompt(); break;
        case ActionToggleTree:
            treeOpen_ = !treeOpen_;
            if (!treeOpen_ && focus_ == FocusTree) focus_ = FocusText;
            break;
        case ActionTogglePlain: {
            bool plain = frame_ != &kBoxFrame;
            setPlainFrame(!plain);
            settings::rememberPlainFrame(!plain);

            painted_.clear();
            say(plain ? "the frame is drawn with the box characters again"
                      : "the frame is drawn with - | + now, and will be next time");
            break;
        }
        case ActionToggleNumbers:
            numbers_ = !numbers_;
            say(numbers_ ? "line numbers on" : "line numbers off");
            break;
        case ActionTogglePanel:
            panelOpen_ = !panelOpen_;
            if (!panelOpen_ && focus_ == FocusPanel) focus_ = FocusText;
            break;
        case ActionBuild:        compile(); break;
        case ActionConvert:      convertFile(); break;
        case ActionRun:          buildAndRun(); break;
        case ActionBuildProject: buildProject(false); break;
        case ActionRunProject:   buildProject(true); break;
        case ActionEmulate:      runWith(RunEmulator); break;
        case ActionVerify:       runWith(RunVerify); break;
        case ActionClean:        clean(); break;
        case ActionToggleBreak:  toggleBreak(); break;
        case ActionDebug:        debug(false); break;
        case ActionDebugProject: debug(true); break;
        case ActionStepOver:
        case ActionStepInto:
        case ActionStepOut:      debugStep(action); break;
        case ActionFrameUp:      lookAlongStack(1); break;
        case ActionFrameDown:    lookAlongStack(-1); break;
        case ActionWatch:        watchExpression(); break;
        case ActionDebugStop:
            if (debugging()) { debugStop(); say("debugging stopped"); }
            else say("nothing is running");
            break;
        case ActionConfigDebug:
            config_ = ConfigDebug;
            settings::rememberConfiguration("debug");
            project_.rememberConfiguration(config_);
            resetDebug();

            say("debug:" + configFlags(resolve(tool_, lang_), config_, kArches[arch_]) +
                (optimises(resolve(tool_, lang_))
                     ? std::string()
                     : std::string(" - ") + toolchainName(resolve(tool_, lang_)) + " has no -O"));
            break;
        case ActionConfigRelease:
            config_ = ConfigRelease;
            settings::rememberConfiguration("release");
            project_.rememberConfiguration(config_);
            resetDebug();
            say("release:" + configFlags(resolve(tool_, lang_), config_, kArches[arch_]) +
                (optimises(resolve(tool_, lang_))
                     ? std::string()
                     : std::string(" - ") + toolchainName(resolve(tool_, lang_)) + " has no -O"));
            break;
        case ActionShowConsole:  panelOpen_ = true; tab_ = TabConsole; panelOff_ = 0; break;
        case ActionShowDebug:    panelOpen_ = true; tab_ = TabDebug; panelOff_ = 0; break;
        case ActionShowAssembly: panelOpen_ = true; tab_ = TabAssembly; panelOff_ = 0; break;
        case ActionArchWindows:
        case ActionArchLinux:
        case ActionArchDarwin:
        case ActionArchTms6747:
            chooseArch(static_cast<size_t>(action - ActionArchWindows));
            break;
        case ActionLangAuto:
            langChoice_ = LangCount;
            applyLanguage();
            say(std::string("language: chosen by the name - this one is ") +
                languageName(lang_));
            break;
        case ActionLangC:
        case ActionLangCpp:
        case ActionLangShalimar:
        case ActionLangJson:
        case ActionLangText: {

            static const Language chosen[] = {LangC, LangCpp, LangShalimar, LangJson, LangPlain};
            langChoice_ = chosen[action - ActionLangC];
            applyLanguage();
            resetDebug();
            say(std::string("language: ") + languageName(lang_) + ", for every file");
            break;
        }
        case ActionToolShc:  chooseTool(ToolShc, product::kCompilerShalimar); break;
        case ActionToolAuto: chooseTool(ToolAuto, "chosen by the file"); break;
        case ActionToolCc1:  chooseTool(ToolCc1, product::kCompilerC); break;
        case ActionToolCxx1: chooseTool(ToolCxx1, product::kCompilerCpp); break;
        case ActionToolMsvc: chooseTool(ToolMsvc, "cl"); break;
        case ActionToolCxx:  chooseTool(hostCppToolchain(), toolchainShown(tool_, hostCppToolchain())); break;
        case ActionHelpContents: showHelpContents(); break;
        case ActionKeys:         showKeys(); break;
        case ActionAbout:        showAbout(); break;
        case ActionEnvironment:  showEnvironment(); break;
        case ActionNone:         break;
    }
}

void Editor::applyLanguage() {
    lang_ = (langChoice_ == LangCount) ? languageFor(buf_.path()) : langChoice_;

    style_.dialect = (lang_ == LangShalimar) ? DialectShalimar : DialectC;
}

std::string Editor::prompt(const std::string& text, bool& cancelled) {
    return prompt(text, cancelled, std::vector<std::string>());
}

namespace {

bool holds(const std::string& name, const std::string& typed) {
    if (typed.empty()) return true;
    if (typed.size() > name.size()) return false;
    for (size_t i = 0; i + typed.size() <= name.size(); ++i) {
        size_t j = 0;
        for (; j < typed.size(); ++j) {
            char a = name[i + j], b = typed[j];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
            if (a != b) break;
        }
        if (j == typed.size()) return true;
    }
    return false;
}

}

std::string Editor::prompt(const std::string& text, bool& cancelled,
                           const std::vector<std::string>& choices) {
    std::string answer;
    cancelled = false;

    std::string title = text;
    while (!title.empty() && (title[title.size() - 1] == ' ' || title[title.size() - 1] == ':'))
        title.resize(title.size() - 1);
    if (title.empty()) title = "?";

    if (title[0] >= 'a' && title[0] <= 'z') title[0] = static_cast<char>(title[0] - 'a' + 'A');

    const size_t kMostShown = 12;

    bool done = false;
    for (; !done;) {

        askShown_.clear();
        for (size_t i = 0; i < choices.size() && askShown_.size() < kMostShown; ++i)
            if (holds(choices[i], answer)) askShown_.push_back(choices[i]);
        if (askChoice_ >= askShown_.size())
            askChoice_ = askShown_.empty() ? static_cast<size_t>(-1) : 0;

        askTitle_ = title;
        askAnswer_ = answer;
        refresh();

        int key = term_.readKey();
        if (key == KEY_MOUSE) {
            // A double click on a choice is Enter on it.
            if (promptMouse(term_.mouse()) && askChoice_ < askShown_.size()) {
                answer = askShown_[askChoice_];
                done = true;
            }
            continue;
        }
        if (key == KEY_NONE) {
            if (!term_.eof()) continue;
            cancelled = true;
            done = true;
        } else if (key == '\x1b') {
            cancelled = true;
            done = true;
        } else if (key == KEY_ARROW_DOWN) {
            if (!askShown_.empty() && askChoice_ + 1 < askShown_.size()) ++askChoice_;
        } else if (key == KEY_ARROW_UP) {
            if (!askShown_.empty() && askChoice_ > 0 && askChoice_ != static_cast<size_t>(-1))
                --askChoice_;
        } else if (key == '\t') {

            if (askChoice_ < askShown_.size()) {
                answer = askShown_[askChoice_];
                askChoice_ = 0;
            }
        } else if (key == '\r' || key == '\n') {

            if (askChoice_ < askShown_.size()) answer = askShown_[askChoice_];
            done = true;
        } else if (key == KEY_BACKSPACE || key == ctrl('h')) {
            if (!answer.empty()) answer.resize(utf8::startOf(answer, answer.size() - 1));
            askChoice_ = 0;
        } else if (key >= 32 && key < 127) {
            answer += static_cast<char>(key);
            askChoice_ = 0;
        }
    }

    askTitle_.clear();
    askAnswer_.clear();
    askShown_.clear();
    askChoice_ = 0;
    return cancelled ? std::string() : answer;
}

// ---- the mouse ---------------------------------------------------------------

bool Editor::doubleClick(const MouseEvent& m) {
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    bool twice = m.row == lastClickRow_ && m.col == lastClickCol_ &&
                 now - lastClick_ < std::chrono::milliseconds(500);
    lastClick_ = now;
    lastClickRow_ = twice ? -1 : m.row;   // a third press starts again
    lastClickCol_ = m.col;
    return twice;
}

// The menu title a column of the bar falls in, a space either side included; the count of columns for none.
size_t Editor::menuTitleUnder(int col) const {
    const size_t x = static_cast<size_t>(col > 0 ? col - 1 : 0);
    for (size_t i = 0; i < menu_.columns().size(); ++i) {
        size_t at = menu_.titleAt(i), len = menu_.columns()[i].title.size();
        if (x + 1 >= at && x <= at + len) return i;
    }
    return menu_.columns().size();
}

// The byte of a line drawn at a render column - renderCol turned round, a tab or a wide character taken whole.
size_t Editor::byteAtColumn(size_t row, size_t column) const {
    const std::string& line = buf_.line(row);
    size_t r = 0, i = 0;
    while (i < line.size()) {
        size_t w = line[i] == '\t' ? kTabStop - (r % kTabStop) : utf8::widthOf(utf8::codePointAt(line, i));
        if (r + w > column) break;
        r += w;
        size_t step = line[i] == '\t' ? i + 1 : utf8::next(line, i);
        i = (step > i) ? step : i + 1;
    }
    return i;
}

// The text cursor to a screen cell of the body, held inside the text: a cell past a line's end is its end.
void Editor::cursorTo(int screenRow, int screenCol) {
    const int textLeft = 2 + (treeOpen_ ? treeCols_ + 1 : 0) + gutterCols_;
    int y = screenRow - 3;
    if (y < 0) y = 0;
    if (y >= bodyRows_) y = bodyRows_ - 1;
    size_t row = rowoff_ + static_cast<size_t>(y);
    if (row >= buf_.lineCount()) row = buf_.lineCount() - 1;
    int x = screenCol - textLeft;
    cy_ = row;
    cx_ = byteAtColumn(row, coloff_ + static_cast<size_t>(x < 0 ? 0 : x));
}

bool Editor::promptMouse(const MouseEvent& m) {
    if (m.kind == MouseEvent::WheelDown) {
        if (!askShown_.empty() && askChoice_ + 1 < askShown_.size()) ++askChoice_;
        return false;
    }
    if (m.kind == MouseEvent::WheelUp) {
        if (askChoice_ > 0 && askChoice_ != static_cast<size_t>(-1)) --askChoice_;
        return false;
    }
    if (m.kind != MouseEvent::Press || m.button != 0) return false;
    int at = 0, top = 0, wide = 0;
    dialogBox(at, top, wide);
    // The box's rows: its top, the answer, then the choices; its columns at+1 to at+wide+2.
    int i = m.row - (top + 2);
    if (m.col < at + 1 || m.col > at + wide + 2 || i < 0 || i >= static_cast<int>(askShown_.size())) return false;
    bool twice = doubleClick(m);
    askChoice_ = static_cast<size_t>(i);
    return twice;
}

void Editor::processMouse(const MouseEvent& m) {
    if (m.kind == MouseEvent::Release) { dragging_ = false; return; }

    // An open menu takes every click: an item chooses, a title moves to its column, anywhere else closes it.
    if (menu_.active()) {
        if (m.kind != MouseEvent::Press && m.kind != MouseEvent::Drag) return;
        if (m.row == 1) {
            size_t c = menuTitleUnder(m.col);
            if (c >= menu_.columns().size()) { if (m.kind == MouseEvent::Press) menu_.close(); return; }
            if (c != menu_.column()) menu_.openColumn(c);
            else if (m.kind == MouseEvent::Press) menu_.close();
            return;
        }
        if (menu_.dropped()) {
            size_t at = 0, width = 0;
            dropdownBox(at, width);
            int index = m.row - 3;
            const size_t items = menu_.columns()[menu_.column()].items.size();
            if (index >= 0 && static_cast<size_t>(index) < items &&
                m.col >= static_cast<int>(at) + 2 && m.col <= static_cast<int>(at + width) + 1) {
                if (m.kind == MouseEvent::Drag) menu_.hover(static_cast<size_t>(index));
                else perform(menu_.choose(static_cast<size_t>(index)));
                return;
            }
        }
        if (m.kind == MouseEvent::Press) menu_.close();
        return;
    }

    const int treeRight = treeOpen_ ? treeCols_ + 1 : 0;              // last column of the tree
    const int textLeft = 2 + (treeOpen_ ? treeCols_ + 1 : 0);         // first column of the gutter or text
    const int bodyTop = 3, bodyBottom = 2 + bodyRows_;
    const int panelHead = bodyBottom + 1, panelTop = panelHead + 1, panelBottom = panelHead + panelRows_;
    const bool inBody = m.row >= bodyTop && m.row <= bodyBottom;
    const bool inPanel = panelOpen_ && m.row >= panelTop && m.row <= panelBottom;

    // The wheel scrolls what is under it, three lines a notch; the text's cursor goes with the view.
    if (m.kind == MouseEvent::WheelUp || m.kind == MouseEvent::WheelDown) {
        const bool down = m.kind == MouseEvent::WheelDown;
        if (inPanel) {
            for (int i = 0; i < 3; ++i) movePanel(down ? KEY_ARROW_DOWN : KEY_ARROW_UP);
        } else if (inBody && treeOpen_ && m.col >= 2 && m.col <= treeRight) {
            for (int i = 0; i < 3; ++i) moveTree(down ? KEY_ARROW_DOWN : KEY_ARROW_UP);
        } else if (inBody) {
            size_t last = buf_.lineCount() - 1;
            for (int i = 0; i < 3; ++i) {
                if (down && rowoff_ + static_cast<size_t>(bodyRows_) <= last) { ++rowoff_; if (cy_ < last) ++cy_; }
                if (!down && rowoff_ > 0) { --rowoff_; if (cy_ > 0) --cy_; }
            }
            clampCursor();
        }
        return;
    }

    // A drag carries on the selection the press began.
    if (m.kind == MouseEvent::Drag) {
        if (dragging_ && m.button == 0) cursorTo(m.row, m.col);
        return;
    }
    if (m.kind != MouseEvent::Press) return;
    const bool twice = doubleClick(m);

    if (m.row == 1) {                                   // the menu bar
        size_t c = menuTitleUnder(m.col);
        if (c < menu_.columns().size()) menu_.openColumn(c);
        return;
    }

    if (m.row == 2) {                                   // the open files: drawFrameTop's arithmetic
        int room = screenCols_ - 2 - (treeOpen_ ? treeCols_ + 1 : 0);
        int x = textLeft + 1, wide = 0;
        for (size_t i = 0; i < docs_.size(); ++i) {
            const Buffer& b = (i == doc_) ? buf_ : docs_[i].buf;
            std::string name = b.path().empty() ? std::string("[no name]") : baseName(b.path());
            if (b.dirty()) name += "*";
            std::string cell = " " + name + " ";
            int cellWide = static_cast<int>(utf8::columns(cell, cell.size()));
            if (wide + cellWide + 2 > room) break;
            if (m.col >= x && m.col < x + cellWide) { if (i != doc_) switchTo(i); focus_ = FocusText; return; }
            x += cellWide + 1;
            wide += cellWide + 1;
        }
        return;
    }

    if (panelOpen_ && m.row == panelHead) {             // the panel's tabs: drawPanel's arithmetic
        const char* names[TabCount] = {" Console ", " Debug ", " Assembly "};
        int x = textLeft + 1;
        for (int i = 0; i < TabCount; ++i) {
            int w = static_cast<int>(std::string(names[i]).size());
            if (m.col >= x && m.col < x + w) {
                if (tab_ != static_cast<Tab>(i)) { tab_ = static_cast<Tab>(i); panelOff_ = 0; }
                focus_ = FocusPanel;
                return;
            }
            x += w + 1;
        }
        return;
    }

    if (inPanel) {                                      // a panel line: a double click goes to it
        focus_ = FocusPanel;
        if (!twice) return;
        // Which line is under the click, wrapped lines counted as the rows they take.
        const std::vector<std::string>& lines = panelLines();
        size_t cols = static_cast<size_t>(screenCols_ > 2 ? screenCols_ - 2 : 1);
        int y = m.row - panelTop;
        size_t line = panelOff_;
        while (line < lines.size()) {
            int rows = static_cast<int>(rowsForLine(lines[line], cols));
            if (y < rows) break;
            y -= rows;
            ++line;
        }
        if (line >= lines.size()) return;
        if (tab_ == TabConsole) goToProblem();
        else if (tab_ == TabDebug) { panelOff_ = line; goToFrame(); }
        return;
    }

    if (!inBody) return;

    if (treeOpen_ && m.col >= 2 && m.col <= treeRight) {   // the project tree
        size_t row = treeOff_ + static_cast<size_t>(m.row - bodyTop);
        if (row >= tree_.size()) return;
        const TreeEntry& e = tree_.entries()[row];
        treeSel_ = row;
        focus_ = FocusTree;
        // The + or - before a folder opens or closes it at once; anything else wants a double click.
        const int marker = 2 + e.depth * 2;
        if (twice || (e.directory && m.col == marker)) openSelected();
        return;
    }

    if (m.col < textLeft) return;
    focus_ = FocusText;
    if (numbers_ && m.col < textLeft + gutterCols_) {   // the gutter: a breakpoint on that line
        size_t row = rowoff_ + static_cast<size_t>(m.row - bodyTop);
        if (row >= buf_.lineCount()) return;
        cy_ = row;
        cx_ = 0;
        marked_ = false;
        toggleBreak();
        return;
    }

    // The text: the cursor to the click; shift extends the selection, a double click takes the word.
    const size_t wasRow = cy_, wasCol = cx_;
    cursorTo(m.row, m.col);
    if (m.shift) {
        if (!marked_) { marked_ = true; markRow_ = wasRow; markCol_ = wasCol; }
        return;
    }
    if (twice) {
        const std::string& line = buf_.line(cy_);
        auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
        size_t from = cx_, to = cx_;
        while (from > 0 && word(line[from - 1])) --from;
        while (to < line.size() && word(line[to])) ++to;
        if (from < to) { marked_ = true; markRow_ = cy_; markCol_ = from; cx_ = to; }
        return;
    }
    marked_ = true;                                     // empty until a drag moves the cursor away
    markRow_ = cy_;
    markCol_ = cx_;
    dragging_ = m.button == 0;
}

void Editor::processKey(int key) {

    if (menu_.active()) {
        perform(menu_.key(key));
        return;
    }

    if (key != ctrl('q')) quitConfirm_ = 0;

    switch (key) {
        case KEY_F10: openMenu(); return;
        case KEY_F1:  showKeys(); return;
        case KEY_F2:  nextDocument(-1); return;
        case KEY_F3:  nextDocument(1); return;
        case KEY_F4:  perform(ActionBuildProject); return;
        case KEY_F5:  perform(ActionRun); return;
        case KEY_F6:  perform(ActionStepInto); return;
        case KEY_F7:  perform(ActionStepOver); return;
        case KEY_F8:  perform(ActionDebug); return;
        case KEY_F9:  perform(ActionToggleBreak); return;

        case KEY_CTRL_UP:   perform(ActionFrameUp); return;
        case KEY_CTRL_DOWN: perform(ActionFrameDown); return;

        case ctrl('q'): perform(ActionQuit); return;   // the menu's Quit: the same way out

        case ctrl('s'): perform(ActionSave); return;
        case ctrl('b'): perform(ActionBuild); return;
        case ctrl('c'): perform(ActionCopy); return;
        case ctrl('x'): perform(ActionCut); return;
        case ctrl('v'): perform(ActionPaste); return;
        case ctrl('z'): perform(ActionUndo); return;
        case ctrl('y'): perform(ActionRedo); return;
        case ctrl('a'): perform(ActionLayOut); return;
        case ctrl('f'): perform(ActionFind); return;
        case ctrl('g'): perform(ActionFindNext); return;
        case ctrl('r'): perform(ActionReplace); return;
        case ctrl('p'): perform(ActionToggleTree); return;
        case ctrl('e'): perform(ActionTogglePanel); return;
        case ctrl('l'): perform(ActionToggleNumbers); return;

        case ctrl('d'):
            perform(config_ == ConfigDebug ? ActionConfigRelease : ActionConfigDebug);
            return;

        case ctrl('k'):

            // The Tools menu's order: automatic, cc1, cxx1, shc, cl, and the
            // host's C++ compiler where that is not cl.
            perform(tool_.kind == ToolAuto
                        ? ActionToolCc1
                        : (tool_.kind == ToolCc1
                               ? ActionToolCxx1
                               : (tool_.kind == ToolCxx1
                                      ? ActionToolShc
                                      : (tool_.kind == ToolShc
                                             ? ActionToolMsvc
                                             : (tool_.kind == ToolMsvc &&
                                                        hostCppToolchain() != ToolMsvc
                                                    ? ActionToolCxx
                                                    : ActionToolAuto)))));
            return;
        case ctrl('w'): cycleFocus(); return;

        case ctrl('t'): chooseArch((arch_ + 1) % kArchCount); return;

        case KEY_ARROW_UP:
        case KEY_ARROW_DOWN:
        case KEY_ARROW_LEFT:
        case KEY_ARROW_RIGHT:
        case KEY_HOME:
        case KEY_END:
        case KEY_PAGE_UP:
        case KEY_PAGE_DOWN:
            if (focus_ == FocusTree) moveTree(key);
            else if (focus_ == FocusPanel) movePanel(key);
            else {

                dropSelection();
                moveCursor(key);
            }
            return;

        case KEY_SHIFT_UP:
        case KEY_SHIFT_DOWN:
        case KEY_SHIFT_LEFT:
        case KEY_SHIFT_RIGHT:
        case KEY_SHIFT_HOME:
        case KEY_SHIFT_END:
        case KEY_SHIFT_PAGE_UP:
        case KEY_SHIFT_PAGE_DOWN:
            if (focus_ == FocusText) extendTo(key);
            // Nothing in the panel is selected, so shift with an arrow is
            // free there and is what resizes it: Ctrl-W, then shift-up for a
            // taller panel. Documented in help/09-the-panel.md.
            else if (focus_ == FocusPanel && key == KEY_SHIFT_UP) resizePanel(1);
            else if (focus_ == FocusPanel && key == KEY_SHIFT_DOWN) resizePanel(-1);
            return;

        default: break;
    }

    if (focus_ == FocusTree) {
        if (key == '\r' || key == '\n') openSelected();
        return;
    }
    if (focus_ == FocusPanel) {
        if (key == '\r' || key == '\n') {
            if (tab_ == TabConsole) goToProblem();
            else if (tab_ == TabDebug) goToFrame();
        }
        return;
    }

    switch (key) {
        case '\r':
        case '\n':

            insertNewline();
            return;

        case KEY_BACKSPACE:
        case ctrl('h'):
            if (!eraseSelection()) backspace();
            return;
        case KEY_DELETE:
            if (!eraseSelection()) deleteForward();
            return;
        case '\t': tabKey(); return;

        default: {
            if (key < 32 || key >= 127) return;
            char c = static_cast<char>(key);

            eraseSelection();

            std::string before = buf_.line(cy_).substr(0, cx_);
            bool atHead = before.find_first_not_of(" \t") == std::string::npos;

            insertChar(c);

            // The second colon of `std::` undoes what the first did: `std:` read as a label.
            if ((c == '}' && atHead) || (c == '#' && atHead) ||
                (c == ':' && (endsALabel(before) ||
                              (!before.empty() && before[before.size() - 1] == ':'))))
                realign();
            return;
        }
    }
}

bool Editor::askNativeTools(const std::string& question) {
    bool cancelled = false;
    std::string answer = prompt(question + " [y/N] ", cancelled);
    return !cancelled && !answer.empty() && (answer[0] == 'y' || answer[0] == 'Y');
}

void Editor::run() {
    starting_ = false;
    term_.mouseReporting(true);
    setAskNative(askNativeOnConsole, this);
    if (message_.empty()) say("F10 menu  Ctrl-B build  Ctrl-Z undo  Ctrl-F find  F1 keys  Ctrl-Q quit");

    int wasRows = 0, wasCols = 0;
    while (running_) {

        int rows = 0, cols = 0;
        term_.size(rows, cols);
        if (rows != wasRows || cols != wasCols) {
            needsDraw_ = true;
            wasRows = rows;
            wasCols = cols;
        }
        if (needsDraw_) {
            refresh();
            needsDraw_ = false;
        }

        int key = term_.readKey();
        if (key == KEY_MOUSE) {
            processMouse(term_.mouse());
            // A click may have run a command that ran a program, as a key may: the console's modes are
            // taken back the same way, or on Windows the first click that built anything was the last one heard.
            term_.reclaim();
            needsDraw_ = true;
        } else if (key != KEY_NONE) {
            processKey(key);

            term_.reclaim();
            needsDraw_ = true;
        }
        if (term_.eof()) break;
    }

    // Leaving ends a debugging session as Stop debugging does: the destructor stops the debugger but cannot remove the program F8 built, which a quit mid-session left in the temporary directory, .dSYM and all.
    if (debugging()) debugStop();

    term_.mouseReporting(false);
    Terminal::write("\x1b[2J\x1b[H");
}

}
