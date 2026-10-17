// Drives the editor itself, the way a person does: keystrokes in, and what landed on the screen
// and on the disk checked afterwards - the half tests/test.cpp cannot see, which had only ever
// been tried by hand. One program for both machines. Usage: session [path-to-the-editor] [path-to-cc1]

#include <cstdio>
#include <cstdlib>
#include "path.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>


// What this harness used of <filesystem>, which is C++17 and so not here, spelled out over src/path.cpp - the code the editor uses, so a passing test has exercised what is shipped.
namespace file {

struct path {
    std::string text;

    path() {}
    path(const char* from) : text(editor::path::withSlashes(from)) {}
    path(const std::string& from) : text(editor::path::withSlashes(from)) {}

    std::string string() const { return text; }
    path operator/(const std::string& leaf) const {
        return path(editor::path::join(text, leaf));
    }
};

inline bool exists(const path& where) { return editor::path::exists(where.text); }
inline bool remove(const path& where) { return editor::path::remove(where.text); }
inline bool remove_all(const path& where) { return editor::path::removeTree(where.text); }
inline bool create_directories(const path& where) {
    return editor::path::makeDirectories(where.text);
}
inline path temp_directory_path() { return path(editor::path::tempDir()); }

}  // namespace file

namespace {

int checks = 0;
int failures = 0;

void check(bool ok, const std::string& what) {
    ++checks;
    if (ok) return;
    ++failures;
    std::printf("  FAIL  %s\n", what.c_str());
}

void checkEqual(const std::string& got, const std::string& want, const std::string& what) {
    ++checks;
    if (got == want) return;
    ++failures;
    std::printf("  FAIL  %s\n        got  [%s]\n        want [%s]\n", what.c_str(),
                got.c_str(), want.c_str());
}

// Keys, spelled the way the terminal sends them.
const std::string kF5 = "\x1b[15~";
const std::string kF1 = "\x1b[11~";
const std::string kF6 = "\x1b[17~";
const std::string kF7 = "\x1b[18~";
const std::string kF8 = "\x1b[19~";
const std::string kF9 = "\x1b[20~";
// Control with an arrow: the arrow's own sequence with the modifier in it.
const std::string kCtrlUp = "\x1b[1;5A";
const std::string kCtrlDown = "\x1b[1;5B";
const std::string kF4 = "\x1bOS";
const std::string kF10 = "\x1b[21~";
const std::string kDown = "\x1b[B";
const std::string kShiftUp = "\x1b[1;2A";
const std::string kRight = "\x1b[C";
const std::string kLeft = "\x1b[D";
const std::string kEnter = "\r";
// Shift with an arrow is the arrow's own sequence with a modifier in it.
const std::string kShiftRight = "\x1b[1;2C";
const std::string kShiftDown = "\x1b[1;2B";
const std::string kShiftEnd = "\x1b[1;2F";
std::string ctrl(char c) { return std::string(1, static_cast<char>(c & 0x1f)); }
std::string times(const std::string& key, int n) {
    std::string out;
    for (int i = 0; i < n; ++i) out += key;
    return out;
}

std::string readFile(const file::path& path) {
    std::ifstream in(path.string().c_str(), std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void writeFile(const file::path& path, const std::string& text) {
    std::ofstream out(path.string().c_str(), std::ios::binary);
    out << text;
}

struct Screen {
    std::string raw;                  // everything the editor wrote
    std::vector<std::string> rows;    // the last screen, escape codes replayed
};

// Replays the whole session onto a grid, as the terminal would: the editor writes only the rows
// that changed, so the last screen alone is a handful of rows, and what is checked is what a person
// was looking at when it stopped. Positioning, CR, NL and the two erases move or clear the grid; colours take no room and are stepped over.
std::vector<std::string> lastScreen(const std::string& raw) {
    std::string all = raw;
    // The editor clears the screen on its way out; the picture wanted is the
    // one it had before that.
    size_t leaving = all.rfind("\x1b[2J");
    if (leaving != std::string::npos) all = all.substr(0, leaving);

    std::vector<std::string> rows;
    size_t row = 0, col = 0;

    for (size_t i = 0; i < all.size();) {
        if (all[i] == '\x1b' && i + 1 < all.size() && all[i + 1] == '[') {
            size_t j = i + 2;
            std::string digits;
            while (j < all.size() && !((all[j] >= 'A' && all[j] <= 'Z') ||
                                       (all[j] >= 'a' && all[j] <= 'z')))
                digits += all[j++];
            char final = (j < all.size()) ? all[j] : 0;

            if (final == 'H') {
                size_t semi = digits.find(';');
                row = static_cast<size_t>(std::atol(digits.c_str()));
                row = row ? row - 1 : 0;
                col = (semi == std::string::npos)
                          ? 0
                          : static_cast<size_t>(std::atol(digits.c_str() + semi + 1)) - 1;
            } else if (final == 'J' && (digits == "2" || digits.empty())) {
                rows.clear();
                row = col = 0;
            } else if (final == 'K' && (digits.empty() || digits == "0")) {
                if (row < rows.size() && rows[row].size() > col) rows[row].resize(col);
            }
            i = (j < all.size()) ? j + 1 : all.size();
            continue;
        }

        char c = all[i++];
        if (c == '\r') { col = 0; continue; }
        if (c == '\n') { ++row; continue; }

        while (rows.size() <= row) rows.push_back(std::string());
        if (rows[row].size() <= col) rows[row].resize(col + 1, ' ');
        rows[row][col] = c;
        ++col;
    }
    return rows;
}

// A home directory of the run's own, so one drive cannot change what the next sees: the editor
// keeps per-machine settings - the last project, the font, debug or release - in the user's own
// directory, and a case pressing Ctrl-D left every case after it in release. Never cleaned between drives: some cases want what the last remembered.
std::string ownHome(const file::path& where) {
    std::string home = (where / ".home").string();
    editor::path::makeDirectories(home);
    return home;
}

Screen drive(const std::string& ride, const std::string& arguments,
             const std::string& keys, const file::path& where) {
    file::path keyFile = where / "keys.in";
    file::path outFile = where / "screen.out";
    writeFile(keyFile, keys);

    std::string home = ownHome(where);
    std::string command = "\"" + ride + "\" " + arguments + " < \"" + keyFile.string() +
                          "\" > \"" + outFile.string() + "\" 2>&1";
#ifdef _WIN32
    // Both, because path::homeDir asks USERPROFILE first and HOMEPATH after.
    command = "set \"USERPROFILE=" + home + "\" && set \"HOMEPATH=" + home + "\" && " + command;
    // cmd eats the outer pair when a command has both a quoted program and
    // quoted arguments, exactly as it does for the compiler commands.
    command = "\"" + command + "\"";
#else
    command = "HOME=\"" + home + "\" " + command;
#endif
    if (std::system(command.c_str()) < 0) std::printf("  (could not run %s)\n", ride.c_str());

    Screen screen;
    screen.raw = readFile(outFile);
    screen.rows = lastScreen(screen.raw);
    file::remove(keyFile);
    file::remove(outFile);
    return screen;
}

// The same, run *from* a directory rather than from wherever the suite is: a relative filename is
// the whole of one bug - which project `src/alpha.c` belongs to depends on where the editor stands,
// and an absolute path would test a path the bug never took. The editor is named absolutely for the same reason.
Screen driveIn(const std::string& ride, const std::string& arguments,
               const std::string& keys, const file::path& where, const file::path& from) {
    file::path keyFile = where / "keys.in";
    file::path outFile = where / "screen.out";
    writeFile(keyFile, keys);

    std::string editorPath = editor::path::absolute(ride);
    std::string home = ownHome(where);
#ifdef _WIN32
    std::string command = "set \"USERPROFILE=" + home + "\" && set \"HOMEPATH=" + home +
                          "\" && cd /d \"" + from.string() + "\" && ";
#else
    // The assignment goes on the editor, not on the cd: `HOME=x cd y && prog`
    // sets it for the cd alone and prog runs without it.
    std::string command = "cd \"" + from.string() + "\" && HOME=\"" + home + "\" ";
#endif
    command += "\"" + editorPath + "\" " + arguments + " < \"" + keyFile.string() +
               "\" > \"" + outFile.string() + "\" 2>&1";
#ifdef _WIN32
    command = "\"" + command + "\"";
#endif
    if (std::system(command.c_str()) < 0) std::printf("  (could not run %s)\n", ride.c_str());

    Screen screen;
    screen.raw = readFile(outFile);
    screen.rows = lastScreen(screen.raw);
    file::remove(keyFile);
    file::remove(outFile);
    return screen;
}

// The bottom line, which is where the editor says what just happened.
std::string message(const Screen& screen) {
    for (size_t i = screen.rows.size(); i-- > 0;) {
        std::string row = screen.rows[i];
        while (!row.empty() && row[row.size() - 1] == ' ') row.resize(row.size() - 1);
        if (!row.empty()) return row;
    }
    return std::string();
}

// How many rows say it. What a program printed is hard to tell apart from the
// source that printed it - both are on the screen at once - so the test that
// the output arrived is that the line is there twice.
size_t rowsSaying(const Screen& screen, const std::string& text) {
    size_t found = 0;
    for (size_t i = 0; i < screen.rows.size(); ++i)
        if (screen.rows[i].find(text) != std::string::npos) ++found;
    return found;
}

bool onScreen(const Screen& screen, const std::string& text) {
    for (size_t i = 0; i < screen.rows.size(); ++i)
        if (screen.rows[i].find(text) != std::string::npos) return true;
    return false;
}

// Anywhere in the whole session rather than on the last screen. A build shows
// the console while it runs and then moves to the assembly when it works, so
// what the console said is history by the time the editor is quit.
bool wasShown(const Screen& screen, const std::string& text) {
    return screen.raw.find(text) != std::string::npos;
}

file::path freshProject(const std::string& name) {
    file::path dir = file::temp_directory_path() / ("ride-session-" + name);
    file::remove_all(dir);
    file::create_directories(dir / "src");
    writeFile(dir / "project.pro",
              "{\n  \"name\": \"Trial\",\n  \"indent\": 4,\n"
              "  \"groups\": { \"Sources\": [] }\n}\n");
    return dir;
}

// ---------------------------------------------------------------------------

void editingAndLayout(const std::string& ride) {
    std::printf("typing, and what the editor does to it\n");

    file::path dir = freshProject("typing");
    file::path file = dir / "src" / "typed.c";

    // Typed with no leading space anywhere. What comes back should be laid out.
    std::string keys = "void f(void) {\ng();\nif (x)\ny();\nswitch (n) {\ncase 1:\n"
                       "break;\n}\n}" + ctrl('s') + ctrl('q');
    drive(ride, "\"" + file.string() + "\" --project \"" + dir.string() + "\"", keys, dir);

    checkEqual(readFile(file),
               "void f(void) {\n    g();\n    if (x)\n        y();\n    switch (n) {\n"
               "    case 1:\n        break;\n    }\n}\n",
               "a function typed flat is saved laid out");

    // A line typed as `std::` is a qualified name, not the label `std:` its first colon made.
    file::path typedCpp = dir / "src" / "typed.cpp";
    drive(ride, "\"" + typedCpp.string() + "\" --project \"" + dir.string() + "\"",
          "int main()\n{\nstd::printf(\"x\");\nagain:\nreturn 0;\n}" + ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(typedCpp),
               "int main()\n{\n    std::printf(\"x\");\nagain:\n    return 0;\n}\n",
               "a line typed as std:: is laid out as a statement, and a label still is not");

    // Ctrl-F lays out a file that arrived with no layout of its own.
    file::path flat = dir / "src" / "flat.c";
    writeFile(flat, "int main(void)\n{\nif (x)\nreturn 1;\nreturn 0;\n}\n");
    drive(ride, "\"" + flat.string() + "\" --project \"" + dir.string() + "\"",
          ctrl('a') + ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(flat),
               "int main(void)\n{\n    if (x)\n        return 1;\n    return 0;\n}\n",
               "Ctrl-A lays out a whole file");

    // Tabs instead of spaces, asked for on the command line.
    file::path tabbed = dir / "src" / "tabbed.c";
    writeFile(tabbed, "int f(void)\n{\nreturn 1;\n}\n");
    drive(ride, "\"" + tabbed.string() + "\" --project \"" + dir.string() + "\" --tabs",
          ctrl('a') + ctrl('s') + ctrl('q'), dir);
    check(readFile(tabbed).find("\treturn 1;") != std::string::npos,
          "--tabs indents with a tab");

    file::remove_all(dir);
}

void colouring(const std::string& ride) {
    std::printf("what the screen is coloured with\n");

    file::path dir = freshProject("colour");
    file::path file = dir / "src" / "colour.c";
    writeFile(file, "int main(void)\n{\n    return 0;   /* done */\n}\n");

    Screen screen = drive(ride, "\"" + file.string() + "\" --project \"" + dir.string() + "\"",
                          ctrl('q'), dir);

    // The colours are in the bytes even though they are not in the grid.
    check(screen.raw.find("\x1b[94m") != std::string::npos, "a keyword is coloured");
    check(screen.raw.find("\x1b[36m") != std::string::npos, "a type is coloured");
    check(screen.raw.find("\x1b[90m") != std::string::npos, "a comment is coloured");
    check(onScreen(screen, "  1 int main(void)"), "and the line numbers are there");
    check(onScreen(screen, "C  "), "the status bar names the language");

    file::remove_all(dir);
}

void addAndRemoveFile(const std::string& ride) {
    std::printf("making a file in the project, adding one, and taking one out\n");

    file::path dir = freshProject("files");
    std::string project = " --project \"" + dir.string() + "\"";

    // A file on the disk that the project does not list. That is the state
    // Add File exists for, and it is not the state freshProject leaves.
    file::path loose = dir / "src" / "loose.c";
    writeFile(loose, "int main(void) { return 0; }\n");
    check(readFile(dir / "project.pro").find("src/loose.c") == std::string::npos,
          "the file starts outside the project");

    // Project menu: right twice from File, then down to the item wanted. New File is the sixth
    // selectable item, Add File the seventh and Remove File the eighth; stepTo skips the rule above
    // them. These counts moved twice on 2026-08-24, which is what this comment is here to make findable the next time one moves.
    const std::string toProject = kF10 + times(kRight, 2);
    const std::string newFile = toProject + times(kDown, 4) + kEnter;
    // Add File asks which file first, and the empty answer is the one in front
    const std::string addFile = toProject + times(kDown, 5) + kEnter + kEnter;
    const std::string removeFile = toProject + times(kDown, 6) + kEnter;

    // New File makes one and puts it in the project in the same breath, which
    // is the difference between it and Add File below.
    drive(ride, project, newFile + "src/made.c" + kEnter + ctrl('q'), dir);
    check(file::exists(dir / "src" / "made.c"), "New File makes the file");
    check(readFile(dir / "project.pro").find("src/made.c") != std::string::npos,
          "and puts it in the project");

    // A path two directories deep is refused, and nothing is written.
    Screen deep = drive(ride, project, newFile + "a/b/deep.c" + kEnter + ctrl('q'), dir);
    check(!file::exists(dir / "a"), "a file two directories deep is not made");
    check(onScreen(deep, "two levels at most"), "and the rule says so on screen");

    // Add File asks which group, and offers one; the empty answer takes it.
    Screen added = drive(ride, "\"" + loose.string() + "\"" + project,
                         addFile + kEnter + ctrl('q'), dir);
    check(readFile(dir / "project.pro").find("src/loose.c") != std::string::npos,
          "Add File puts the open file in the project");
    check(onScreen(added, "added"), "and the line says so");

    // Remove File asks nothing - a file is in the project or it is not.
    Screen removed = drive(ride, "\"" + loose.string() + "\"" + project,
                           removeFile + ctrl('q'), dir);
    check(readFile(dir / "project.pro").find("src/loose.c") == std::string::npos,
          "Remove File takes it out of the project");
    check(file::exists(loose), "and leaves the file on the disk, which is the whole point");

    // Asking twice is not an error worth hiding: the second time says so.
    Screen again = drive(ride, "\"" + loose.string() + "\"" + project,
                         removeFile + ctrl('q'), dir);
    check(onScreen(again, "not in the project"),
          "removing a file that is already out says so");

    file::remove_all(dir);
}

void selectingAndPasting(const std::string& ride) {
    std::printf("selecting, copying and pasting\n");

    file::path dir = freshProject("clip");
    file::path file = dir / "src" / "clip.c";
    std::string args = "\"" + file.string() + "\" --project \"" + dir.string() + "\"";

    // Select three characters, copy, go to the end of the line, paste.
    writeFile(file, "abcdef\n");
    drive(ride, args,
          times(kShiftRight, 3) + ctrl('c') + "\x1b[F" + ctrl('v') + ctrl('s') + ctrl('q'),
          dir);
    checkEqual(readFile(file), "abcdefabc\n", "copy and paste move a selection about");

    // Cut takes it away.
    writeFile(file, "abcdef\n");
    drive(ride, args, times(kShiftRight, 3) + ctrl('x') + ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(file), "def\n", "cut takes the selection out");

    // And puts it back where the caret goes next.
    writeFile(file, "abcdef\n");
    drive(ride, args,
          times(kShiftRight, 3) + ctrl('x') + "\x1b[F" + ctrl('v') + ctrl('s') + ctrl('q'),
          dir);
    checkEqual(readFile(file), "defabc\n", "and paste puts it back");

    // With nothing selected, copy and cut take the whole line.
    writeFile(file, "first\nsecond\n");
    drive(ride, args, ctrl('x') + ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(file), "second\n", "cut with no selection takes the line");

    // A selection crossing lines.
    writeFile(file, "one\ntwo\nthree\n");
    drive(ride, args, kShiftDown + ctrl('x') + ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(file), "two\nthree\n", "a selection can cross a line ending");

    // Typing over a selection replaces it.
    writeFile(file, "abcdef\n");
    drive(ride, args, times(kShiftRight, 3) + "X" + ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(file), "Xdef\n", "typing over a selection replaces it");

    // Backspace over a selection removes all of it.
    writeFile(file, "abcdef\n");
    drive(ride, args, times(kShiftRight, 3) + "\x7f" + ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(file), "def\n", "and backspace removes all of it");

    // Undo puts a cut back in one step.
    writeFile(file, "abcdef\n");
    drive(ride, args, times(kShiftRight, 3) + ctrl('x') + ctrl('z') + ctrl('s') + ctrl('q'),
          dir);
    checkEqual(readFile(file), "abcdef\n", "and undo puts a cut back");

    Screen shown = drive(ride, args, times(kShiftRight, 3) + ctrl('q'), dir);
    check(shown.raw.find("\x1b[7m") != std::string::npos, "a selection is shown in reverse");

    file::remove_all(dir);
}

void multiByteText(const std::string& ride) {
    std::printf("text that is not ASCII\n");

    file::path dir = freshProject("utf8");
    file::path file = dir / "src" / "urdu.c";
    // A comment in Urdu and a string with an accented letter in it.
    const std::string text =
        "/* \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 */\nchar *s = \"caf\xc3\xa9\";\n";
    writeFile(file, text);

    std::string args = "\"" + file.string() + "\" --project \"" + dir.string() + "\"";

    // Opened and saved with nothing done to it, the bytes must be the same.
    drive(ride, args, ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(file), text, "a file that is not ASCII survives being saved");

    // Four steps right cross three ASCII characters and one Urdu letter, which
    // is five bytes but four columns.
    Screen moved = drive(ride, args, times(kRight, 4) + ctrl('q'), dir);
    check(onScreen(moved, "col 5"), "the caret moves by characters, not by bytes");

    // Backspace takes the whole letter, not its last byte.
    writeFile(file, text);
    drive(ride, args, times(kRight, 4) + "\x7f" + ctrl('s') + ctrl('q'), dir);
    std::string after = readFile(file);
    check(after.find("/* \xd9\x84") != std::string::npos,
          "backspace removes a whole letter");
    check(after.size() == text.size() - 2, "which is two bytes, not one");

    file::remove_all(dir);
}

// Re-indenting what is selected, and everything when nothing is.
// Leaving with work unsaved, in a file that is not the one in front.
void leavingWithChanges(const std::string& ride) {
    std::printf("what leaving does about unsaved work\n");

    file::path dir = freshProject("leaving");
    file::path one = dir / "src" / "one.c";
    file::path two = dir / "src" / "two.c";
    writeFile(one, "int one(void) { return 1; }\n");
    writeFile(two, "int two(void) { return 2; }\n");
    writeFile(dir / "project.pro",
              "{\n  \"name\": \"leaving\",\n"
              "  \"groups\": { \"Sources\": [\"src/one.c\", \"src/two.c\"] }\n}\n");

    std::string common = "\"" + one.string() + "\" --project \"" + dir.string() + "\"";

    // Type into one.c, then open two.c from the pane so the changed file is the one behind; Ctrl-W moves the focus, Ctrl-P would toggle the pane.
    const std::string behind = "X" + ctrl('w') + times(kDown, 2) + kEnter;

    Screen left = drive(ride, common, behind + ctrl('q'), dir);
    check(wasShown(left, "unsaved changes in one.c"),
          "leaving names the changed file even when another is in front");
    checkEqual(readFile(one), "int one(void) { return 1; }\n",
               "and nothing was written behind your back");

    // A file opened twice, spelled two ways, is one file. Opened by the name given on the command
    // line and then from the pane, which counts paths from the project's root - the tab strip must
    // hold one of it, and the changes must still be in it.
    Screen once = drive(ride, "\"src/one.c\" --project \"" + dir.string() + "\"",
                        "X" + ctrl('w') + kEnter + ctrl('q'), dir);
    check(rowsSaying(once, "one.c") >= 1, "the file is open");
    check(!onScreen(once, "one.c* - one.c"), "and opening it again from the pane opens no second tab");
    check(onScreen(once, "one.c*"), "with the changes still in it");

    // The one in front, which is the case that always worked.
    Screen front = drive(ride, common, "X" + ctrl('q'), dir);
    check(wasShown(front, "unsaved changes in one.c"), "and it says so for the one in front");

    // Twice leaves anyway, which is what the message promises. Proved by what comes after: keys
    // typed once it has gone are typed at nothing, so a ZZZZ that never appears is an editor that
    // had already left. Exiting on its own would prove nothing - a driven run ends when the keys do.
    Screen gone = drive(ride, common, "X" + ctrl('q') + ctrl('q') + "ZZZZ", dir);
    check(!wasShown(gone, "ZZZZ"), "and pressing it twice leaves, before the next key");
    checkEqual(readFile(one), "int one(void) { return 1; }\n", "still without saving");

    file::remove_all(dir);
}

void reindenting(const std::string& ride) {
    std::printf("re-indenting, all of it or the part that is selected\n");

    file::path dir = freshProject("reindent");
    file::path file = dir / "src" / "messy.c";
    const char* crooked =
        "int main(void)\n{\nint a = 1;\n        int b = 2;\n   if (a < b) {\n"
        "a = b;\n            }\n    int c = 3;\n  int d = 4;\n    return 0;\n}\n";
    writeFile(file, crooked);

    std::string common = "\"" + file.string() + "\" --project \"" + dir.string() + "\"";

    // Nothing selected: the whole file, as Ctrl-A has always done.
    Screen all = drive(ride, common, ctrl('a') + ctrl('s') + ctrl('q'), dir);
    // wasShown, not onScreen: saving comes after, and what the one message line says at the end is that the file was written.
    check(wasShown(all, "laid out - 11 lines"), "with nothing selected it lays the file out");
    // "  int d = 4;" laid out becomes "    int d = 4;", so the crooked spelling
    // is gone from the file - and looking for its absence has to allow for the
    // straight one containing it, which is why the whole line is matched.
    check(readFile(file).find("\n  int d = 4;") == std::string::npos,
          "and the line nobody selected is laid out too");

    // Selected: only those lines are written back, and the rest are left as
    // they were - which is the whole difference, and is checked by a line
    // outside the selection staying crooked.
    writeFile(file, crooked);
    Screen part = drive(ride, common,
                        times(kDown, 2) + times(kShiftDown, 4) + ctrl('a') +
                            ctrl('s') + ctrl('q'),
                        dir);
    check(wasShown(part, "of the selection"), "a selection is laid out on its own");
    std::string after = readFile(file);
    check(after.find("    int a = 1;") != std::string::npos,
          "the selected lines are laid out");
    check(after.find("  int d = 4;") != std::string::npos,
          "and a line below the selection is left exactly as it was");

    file::remove_all(dir);
}

void undoing(const std::string& ride) {
    std::printf("undo and redo, in the editor\n");

    file::path dir = freshProject("undo");
    file::path file = dir / "src" / "undo.c";
    const char* text = "int one(void) { return 1; }\n";
    std::string args = "\"" + file.string() + "\" --project \"" + dir.string() + "\"";

    // Typed, then taken back, then saved: the file should be as it started.
    writeFile(file, text);
    drive(ride, args, "xyz" + ctrl('z') + ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(file), text, "undo takes back what was typed");

    // A run of typing is one step, so one undo removes all three letters and
    // one redo brings all three back.
    writeFile(file, text);
    drive(ride, args, "xyz" + ctrl('z') + ctrl('y') + ctrl('s') + ctrl('q'), dir);
    check(readFile(file).compare(0, 3, "xyz") == 0, "redo puts it back");

    // Laying the file out is one step of its own.
    file::path flat = dir / "src" / "flat.c";
    const char* crooked = "int main(void)\n{\nreturn 0;\n}\n";
    writeFile(flat, crooked);
    std::string flatArgs = "\"" + flat.string() + "\" --project \"" + dir.string() + "\"";
    drive(ride, flatArgs, ctrl('a') + ctrl('z') + ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(flat), crooked, "and undo takes a whole re-layout back");

    // So is a replace.
    writeFile(file, text);
    drive(ride, args,
          ctrl('r') + "one" + kEnter + "two" + kEnter + ctrl('z') + ctrl('s') + ctrl('q'),
          dir);
    checkEqual(readFile(file), text, "and a replace, in one step");

    // And a newline is its own step, so undo gives back a line rather than
    // everything typed since the file was opened.
    writeFile(file, text);
    drive(ride, args, "abc\ndef" + ctrl('z') + ctrl('z') + ctrl('s') + ctrl('q'), dir);
    check(readFile(file).find("abc") != std::string::npos,
          "two undos after typing over a newline leave the first part");
    check(readFile(file).find("def") == std::string::npos, "and remove the second");

    // The star that says 'modified' has to follow undo as well as typing.
    writeFile(file, text);
    Screen back = drive(ride, args, "q" + ctrl('s') + ctrl('z') + ctrl('q') + ctrl('q'), dir);
    check(onScreen(back, "undo.c *"), "undoing past a save shows as modified again");

    writeFile(file, text);
    Screen forward = drive(ride, args,
                           "q" + ctrl('s') + ctrl('z') + ctrl('y') + ctrl('q'), dir);
    check(!onScreen(forward, "undo.c *"), "and redoing back to it shows as saved");

    Screen nothing = drive(ride, args, ctrl('z') + ctrl('q'), dir);
    check(onScreen(nothing, "nothing to undo"), "and with nothing done, it says so");

    file::remove_all(dir);
}

void findingAndReplacing(const std::string& ride) {
    std::printf("finding and replacing, in the editor\n");

    file::path dir = freshProject("find");
    file::path file = dir / "src" / "find.c";
    const char* text =
        "int one(void) { return 1; }\n"
        "int two(void) { return 2; }\n"
        "int three(void) { return 3; }\n";
    writeFile(file, text);

    std::string args = "\"" + file.string() + "\" --project \"" + dir.string() + "\"";

    // Ctrl-F, the word, enter: the caret should land on line three.
    Screen found = drive(ride, args, ctrl('f') + "three" + kEnter + ctrl('q'), dir);
    check(onScreen(found, "3/3"), "find moves the caret to the line it is on");
    check(onScreen(found, "three - line 3"), "and says where it went");

    Screen missing = drive(ride, args, ctrl('f') + "absent" + kEnter + ctrl('q'), dir);
    check(onScreen(missing, "is not in this file"), "and says when it is not there");

    // An empty answer looks for nothing at all - it used to be read as the last
    // search again here, while the window read it as a cancel and said nothing.
    // Looking on is what Ctrl-G is for, in both.
    Screen nothing = drive(ride, args,
                           ctrl('f') + "three" + kEnter + ctrl('f') + kEnter + ctrl('q'), dir);
    check(onScreen(nothing, "nothing looked for"), "an empty answer to find says so");
    check(onScreen(nothing, "3/3"), "and leaves the caret where the last find put it");

    // Ctrl-F for the first, Ctrl-G for the next: 'return' is on every line.
    Screen again = drive(ride, args,
                         ctrl('f') + "return" + kEnter + ctrl('g') + ctrl('q'), dir);
    check(onScreen(again, "2/3"), "Ctrl-G moves on to the next one");

    // Replace, then save, and look at the file.
    drive(ride, args, ctrl('r') + "return" + kEnter + "give" + kEnter + ctrl('s') + ctrl('q'),
          dir);
    std::string written = readFile(file);
    check(written.find("give 1") != std::string::npos, "replace changes the text");
    check(written.find("return") == std::string::npos, "everywhere it appeared");

    // And nothing is written unless it is saved.
    writeFile(file, text);
    drive(ride, args, ctrl('r') + "return" + kEnter + "gone" + kEnter + ctrl('q') + ctrl('q'),
          dir);
    checkEqual(readFile(file), text, "and quitting without saving leaves the file alone");

    file::remove_all(dir);
}

// Closing a project, and what the pane is when there is none. The pane has three states now, and
// the third is the one this is about: with no project it shows the files open, and with none open
// nothing - it used to list the directory the editor stood in, which looked like a project never closed.
void closingTheProject(const std::string& ride) {
    std::printf("closing the project, and the pane with no project\n");

    file::path dir = freshProject("closing");
    writeFile(dir / "src" / "one.c", "int one(void) { return 1; }\n");
    writeFile(dir / "src" / "two.c", "int two(void) { return 2; }\n");
    writeFile(dir / "project.pro",
              "{\n  \"name\": \"Closes\",\n"
              "  \"groups\": { \"First\": [\"src/one.c\", \"src/two.c\"] }\n}\n");

    std::string project = " --project \"" + dir.string() + "\"";
    std::string opened = "\"" + (dir / "src" / "one.c").string() + "\"" + project;

    // Project menu, third item.
    const std::string closeProject = kF10 + times(kRight, 2) + times(kDown, 3) + kEnter;

    Screen before = drive(ride, opened, ctrl('q'), dir);
    check(onScreen(before, "- First"), "the group is shown while the project is open");
    // By its own name, not by the path the project file writes: the pane
    // shows "two.c" where project.pro says "src/two.c".
    check(onScreen(before, "two.c"), "and so is a file that is not the one being edited");
    check(!onScreen(before, "src/two.c"), "named without the directory it sits in");

    Screen after = drive(ride, opened, closeProject + ctrl('q'), dir);
    check(!onScreen(after, "- First"), "closing the project takes the group off the pane");
    check(!onScreen(after, "src/two.c"), "and the files it held that were not open");
    check(!onScreen(after, "one.c"), "the file that was open closes with it, being one of its files");
    check(onScreen(after, "closed"), "and the line says so");

    // The file on disk is not touched. Closing a project is a change to what
    // is being looked at, and this is the check that keeps it that way.
    check(readFile(dir / "project.pro").find("src/two.c") != std::string::npos,
          "and project.pro still says everything it said before");

    // File menu, fifth item - Close. The menu opens on File every time since the audit of 2026-09-19; it used to reopen on the column it was left on.
    const std::string closeFile = kF10 + times(kDown, 4) + kEnter;

    Screen empty = drive(ride, opened, closeProject + closeFile + ctrl('q'), dir);
    check(!onScreen(empty, "one.c"), "closing the last open file empties the pane");
    check(!onScreen(empty, "- First"), "and nothing of the project has come back");
    check(!onScreen(empty, "src"), "and no directory listing has taken its place");

    file::remove_all(dir);
}

// Opening by picking from a list, rather than typing a name blind. The question used to be a bare
// line: you typed a filename and found out afterwards whether it was there. What is under it now
// is what is actually in the directory, narrowed by whatever has been typed.
void thePicker(const std::string& ride) {
    std::printf("picking a file, and picking a project\n");

    file::path dir = freshProject("picking");
    writeFile(dir / "src" / "alpha.c", "int a(void) { return 1; }\n");
    writeFile(dir / "src" / "beta.cpp", "int b(void) { return 2; }\n");
    writeFile(dir / "src" / "gcd.shl", "fun <> = main() {\n  ? \"hi\"\n}\n");
    writeFile(dir / "src" / "notes.txt", "not a source file\n");

    std::string project = " --project \"" + dir.string() + "\"";

    // File ▸ Open is the second item.
    const std::string toOpen = kF10 + kDown + kEnter;

    Screen listed = drive(ride, project, toOpen + ctrl('q'), dir);
    check(onScreen(listed, "Open"), "the question is asked in its own box");
    check(onScreen(listed, "src/"), "and a directory is offered, with a slash");

    // Into src/ - past project.pro, which sorts first - and what is inside is
    // what the list becomes.
    const std::string intoSrc = toOpen + kDown + kEnter;
    Screen inside = drive(ride, project, intoSrc + ctrl('q'), dir);
    check(onScreen(inside, "alpha.c"), "picking a directory lists what is in it");
    check(onScreen(inside, "beta.cpp"), "C++ as well as C");
    check(onScreen(inside, "gcd.shl"), "and Shalimar");
    check(!onScreen(inside, "notes.txt"), "and nothing that is not a source file");
    check(onScreen(inside, "Open src/"), "the question says where it is looking");

    // Typing narrows it, and enter takes the one row left.
    Screen opened = drive(ride, project, intoSrc + "gcd" + kEnter + ctrl('q'), dir);
    check(onScreen(opened, "gcd.shl"), "typing narrows the list and enter opens what is left");
    check(onScreen(opened, "Shalimar"), "and the file's language is picked up");

    // A name that matches nothing is still the answer, so a file that is not
    // there yet can be named - which is what Save as and New file need.
    Screen made = drive(ride, project, intoSrc + "brand-new.c" + kEnter + ctrl('q'), dir);
    check(onScreen(made, "brand-new.c"), "a name that matches nothing is taken as typed");

    file::remove_all(dir);
}

// Opening a project by picking one, which the terminal front end could not do
// at all before: it took one on the command line or remembered the last.
void pickingAProject(const std::string& ride) {
    std::printf("opening a project from the list\n");

    file::path parent = freshProject("many");
    file::create_directories(parent / "alpha");
    file::create_directories(parent / "beta");
    file::create_directories(parent / "plain");
    writeFile(parent / "alpha" / "a.c", "int a(void) { return 1; }\n");
    writeFile(parent / "alpha" / "project.pro",
              "{\n  \"name\": \"Alpha\",\n  \"groups\": { \"Sources\": [\"a.c\"] }\n}\n");
    writeFile(parent / "beta" / "b.c", "int b(void) { return 2; }\n");
    writeFile(parent / "beta" / "project.pro",
              "{\n  \"name\": \"Beta\",\n  \"groups\": { \"Sources\": [\"b.c\"] }\n}\n");

    std::string here = " --project \"" + parent.string() + "\"";

    // Project ▸ Open project... is the second item.
    const std::string toOpenProject = kF10 + times(kRight, 2) + kDown + kEnter;

    Screen listed = drive(ride, here, toOpenProject + ctrl('q'), parent);
    check(onScreen(listed, "Open project in"), "the project question has its own box");
    check(onScreen(listed, "alpha/"), "and the directories are offered");
    check(onScreen(listed, "beta/"), "all of them");

    // Down twice from "./" is beta/, and it holds a project.pro, so picking it
    // opens it rather than looking inside it.
    Screen went = drive(ride, here, toOpenProject + times(kDown, 2) + kEnter + ctrl('q'), parent);
    check(onScreen(went, "Beta"), "picking a directory that is a project opens it");

    file::remove_all(parent);
}

// **A CCS workspace in Project > Open** (RIDE 4.7): the workspace folder offers one .pro per CCS project,
// and picking one writes it - workspace and project and nothing else - and opens that project alone.
void pickingACcsWorkspaceProject(const std::string& ride) {
    std::printf("opening one project of a CCS workspace\n");
    const std::string examples = editor::path::absolute("docs/ccs-reference/ccs74");
    if (!file::exists(file::path(examples) / "K6747c")) { std::printf("  (no docs/ccs-reference/ccs74 here, so nothing is opened)\n"); return; }

    file::path dir = file::temp_directory_path() / "ride-session-ccs-workspace";
    file::remove_all(dir);
    file::path ws = dir / "workspace_v7", stage = dir / "stage";
    file::create_directories(ws / ".metadata" / ".plugins" / "org.eclipse.core.resources" / ".projects" / "K6747c");
    file::create_directories(ws / ".metadata" / ".plugins" / "org.eclipse.core.resources" / ".projects" / "K6747cpp");
    file::create_directories(stage);
    const char* names[2] = { "K6747c", "K6747cpp" };
    for (int n = 0; n < 2; ++n) {
        file::path from = file::path(examples) / names[n], to = ws / names[n];
        const char* each[5] = { ".project", ".cproject", ".ccsproject", "C6747.cmd", "main.c" };
        file::create_directories(to);
        for (int i = 0; i < 5; ++i)
            if (file::exists(from / each[i])) writeFile(to / each[i], readFile(from / each[i]));
        if (file::exists(from / "main.cpp")) writeFile(to / "main.cpp", readFile(from / "main.cpp"));
    }

    // Project > Open project... is the second item; with no project open, it asks where the editor stands.
    const std::string toOpenProject = kF10 + times(kRight, 2) + kDown + kEnter;
    const std::string here = "--project \"" + ws.string() + "\"";

    Screen listed = driveIn(ride, here, toOpenProject + ctrl('q'), stage, ws);
    check(wasShown(listed, "is a CCS workspace, not a project"), "the workspace itself is not opened, and says so");
    check(onScreen(listed, "K6747c.pro") && onScreen(listed, "K6747cpp.pro"), "Project > Open offers one .pro per project");
    check(!file::exists(ws / "K6747c.pro") && !file::exists(ws / "K6747cpp.pro"), "and has written none of them yet");

    // The list is in name order, so Down once is K6747cpp.pro.
    Screen opened = driveIn(ride, here, toOpenProject + kDown + kEnter + ctrl('q'), stage, ws);
    check(file::exists(ws / "K6747cpp.pro") && !file::exists(ws / "K6747c.pro"), "picking one writes its .pro and only its");
    check(onScreen(opened, "K6747cpp"), "and opens that project");
    check(readFile(ws / "K6747cpp.pro").find("\"project\": \"K6747cpp\"") != std::string::npos &&
              readFile(ws / "K6747cpp.pro").find("\"workspace\": \".\"") != std::string::npos,
          "the .pro names the workspace and the project: " + readFile(ws / "K6747cpp.pro"));

    // --build on the workspace builds nothing and says why, where it used to build an empty project.
    Screen batch = driveIn(ride, here + " --build", "", stage, ws);
    check(batch.raw.find("is a CCS workspace, not a project") != std::string::npos &&
              batch.raw.find("does not say what it builds") == std::string::npos,
          "--build on the workspace says it is one and builds nothing");

    // The .pro opens it again by itself.
    Screen again = driveIn(ride, "--project \"" + (ws / "K6747cpp.pro").string() + "\"", ctrl('q'), stage, ws);
    check(onScreen(again, "K6747cpp") && onScreen(again, "main.cpp"), "and the .pro, named, opens the same project");
    file::remove_all(dir);
}

// **The mouse** (RIDE 4.7): the SGR reports a terminal sends, piped in as keys are, against the screen the
// suite always gets - 80 by 24, the tree 22 wide, a gutter of 4, so the text begins at column 29 on row 3.
std::string click(int col, int row) {
    return "\x1b[<0;" + std::to_string(col) + ";" + std::to_string(row) + "M" +
           "\x1b[<0;" + std::to_string(col) + ";" + std::to_string(row) + "m";
}

void theMouse(const std::string& ride) {
    std::printf("the mouse\n");
    file::path dir = file::temp_directory_path() / "ride-session-mouse";
    file::remove_all(dir);
    file::create_directories(dir);
    const file::path text = dir / "words.txt";
    const std::string here = "\"" + text.string() + "\"";

    // A menu title clicked opens it; an item clicked does it: Project is the third title, Open... its second item.
    writeFile(text, "alpha\nbeta\ngamma\n");
    Screen menu = drive(ride, here, click(17, 1) + click(18, 4) + ctrl('q'), dir);
    check(onScreen(menu, "Open project in"), "clicking Project, then Open..., asks where");

    // A click puts the cursor there: line 3, column 4 - and what is typed goes in at it.
    Screen typed = drive(ride, here, click(32, 5) + "X" + ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(text), std::string("alpha\nbeta\ngamXma\n"), "a click moves the cursor, and typing goes in there");
    check(onScreen(typed, "col 5"), "and the status line follows it");

    // A double click takes the word, and typing replaces it.
    writeFile(text, "alpha\nbeta\ngamma\n");
    drive(ride, here, click(30, 4) + click(30, 4) + "Z" + ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(text), std::string("alpha\nZ\ngamma\n"), "a double click selects the word");

    // A click in the gutter is a breakpoint on that line.
    writeFile(text, "alpha\nbeta\ngamma\n");
    Screen gutter = drive(ride, here, click(26, 4) + ctrl('q'), dir);
    check(wasShown(gutter, "breakpoint on line 2"), "a click in the gutter sets a breakpoint there");

    // A drag selects, and Ctrl-X takes what it covered.
    writeFile(text, "alpha\nbeta\ngamma\n");
    std::string drag = "\x1b[<0;29;3M\x1b[<32;31;3M\x1b[<32;32;3M\x1b[<0;32;3m";
    drive(ride, here, drag + ctrl('x') + ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(text), std::string("ha\nbeta\ngamma\n"), "a drag selects from the press to where it ends");

    file::remove_all(dir);
}

// Which project a named file belongs to, which is not which directory it is in. A project keeps
// its sources a directory down, so looking only beside the file meant `RIDE src/alpha.c` found no
// project and fell through to the last one opened, or the demo: the pane filled with somebody else's files while the edit view held yours.
void whichProjectAFileBelongsTo(const std::string& ride) {
    std::printf("the project a named file belongs to\n");

    file::path dir = freshProject("belongs");
    writeFile(dir / "src" / "alpha.c", "int alpha(void) { return 1; }\n");
    writeFile(dir / "project.pro",
              "{\n  \"name\": \"Belongs\",\n"
              "  \"groups\": { \"Sources\": [\"src/alpha.c\"] }\n}\n");

    // No --project: the editor has to work it out from the file alone. Driven
    // from inside the project's root, which is how anybody would type it.
    Screen found = driveIn(ride, "src/alpha.c", ctrl('q'), dir, dir);
    check(onScreen(found, "- Sources"), "a file one directory down finds its project");
    check(onScreen(found, "alpha.c"), "and the pane holds that project's files");
    check(!onScreen(found, "first.c"),
          "not the demo project, which is where it used to end up");

    file::remove_all(dir);
}

// The pane draws one of two things, which is the half a project pane cannot do on its own. A
// loaded project draws the project - its groups and nothing else; an "Open files" heading above
// them listed the ordinary file twice until 2026-08-24. File > New and File > Open are questions about files, so the pane answers with a flat list of what is open; opening from the pane is not, and leaves the project showing.
void thePaneDrawsOneOfTwoThings(const std::string& ride) {
    std::printf("the pane: a project, or what is open\n");

    file::path dir = freshProject("following");
    writeFile(dir / "src" / "one.c", "int one(void) { return 1; }\n");
    writeFile(dir / "outside.c", "int outside(void) { return 2; }\n");
    writeFile(dir / "project.pro",
              "{\n  \"name\": \"Follows\",\n"
              "  \"groups\": { \"Sources\": [\"src/one.c\"] }\n}\n");

    std::string project = " --project \"" + dir.string() + "\"";
    std::string outside = "\"" + (dir / "outside.c").string() + "\"" + project;

    // Started on a project, so the project is what is drawn.
    Screen shown = drive(ride, outside, ctrl('q'), dir);
    check(onScreen(shown, "Sources"), "a loaded project shows its groups");
    check(onScreen(shown, "one.c"), "with the files they name");
    check(!onScreen(shown, "Open files"),
          "and no heading for what is open, which listed the same file twice");

    // File > New is the first item on the File menu.
    const std::string newFile = kF10 + kEnter;
    Screen made = drive(ride, outside, newFile + ctrl('q'), dir);
    check(!onScreen(made, "Sources"), "File > New takes the groups off the pane");
    check(onScreen(made, "untitled"),
          "and names the buffer that has none, rather than showing nothing at all");
    check(onScreen(made, "outside.c"), "the file already open is listed beside it");

    // Project > Close is the fifth item on the Project menu.
    const std::string closeProject = kF10 + times(kRight, 2) + times(kDown, 3) + kEnter;
    Screen closed = drive(ride, outside, closeProject + ctrl('q'), dir);
    check(!onScreen(closed, "Sources"), "closing the project takes its groups with it");
    check(onScreen(closed, "outside.c"), "and leaves what is open on the pane when it is not one of its files");

    file::remove_all(dir);
}

// The same .pro, opened two ways, has to be two different things. From the Project menu it is a
// project: its groups appear on the pane. From the File menu it is a file: its text appears and
// the pane goes flat. Nothing in Editor::open looks at the suffix, and this check keeps anyone from adding such a look.
void aProjectFileOpenedTwoWays(const std::string& ride) {
    std::printf("a .pro is a project from one menu and a file from the other\n");

    file::path dir = freshProject("twoways");
    writeFile(dir / "src" / "only.c", "int only(void) { return 1; }\n");
    writeFile(dir / "named.pro",
              "{\n  \"name\": \"Named\",\n"
              "  \"groups\": { \"Sources\": [\"src/only.c\"] }\n}\n");
    std::string here = " --project \"" + dir.string() + "\"";

    // Project > Open... is the second item, and takes a name typed at it.
    const std::string asProject =
        kF10 + times(kRight, 2) + kDown + kEnter + "named.pro" + kEnter;
    Screen project = drive(ride, here, asProject + ctrl('q'), dir);
    check(onScreen(project, "Sources"), "opened from the Project menu it is a project");
    check(onScreen(project, "only.c"), "and the pane lists what it names");

    // File > Open... is the second item there.
    const std::string asFile = kF10 + kDown + kEnter + "named.pro" + kEnter;
    Screen file = drive(ride, here, asFile + ctrl('q'), dir);
    check(onScreen(file, "\"name\""), "opened from the File menu it is its own text");
    check(onScreen(file, "JSON"), "read as JSON, which the editor knows");
    // "- Sources" and not "Sources": the file's own text says Sources too, so
    // the bare word is on the screen either way and proves nothing. The fold
    // marker in front of it is drawn by the pane and by nothing else.
    check(!onScreen(file, "- Sources"),
          "and the pane is the list of open files, not a project's groups");

    file::remove_all(dir);
}

void projectPane(const std::string& ride) {
    std::printf("the project pane\n");

    file::path dir = freshProject("pane");
    writeFile(dir / "src" / "one.c", "int one(void) { return 1; }\n");
    writeFile(dir / "src" / "two.c", "int two(void) { return 2; }\n");
    writeFile(dir / "project.pro",
              "{\n  \"name\": \"Panes\",\n  \"indent\": 2,\n"
              "  \"groups\": { \"First\": [\"src/one.c\"], \"Second\": [\"src/two.c\"] }\n}\n");

    Screen screen = drive(ride, "--project \"" + dir.string() + "\"", ctrl('q'), dir);
    check(onScreen(screen, "- First"), "a group is shown");
    check(onScreen(screen, "one.c"), "with what is in it");
    check(onScreen(screen, "- Second"), "and so is the next one");
    check(onScreen(screen, "Panes"), "the project's name is reported");

    // Opening from the pane: focus it, walk to the file, press enter.
    Screen opened = drive(ride, "--project \"" + dir.string() + "\"",
                          ctrl('w') + kDown + kEnter + ctrl('q'), dir);
    check(onScreen(opened, "int one(void)"), "enter in the pane opens the file");
    check(onScreen(opened, " one.c"), "and it gets a tab");

    // The project's indent setting is what the editor uses.
    file::path flat = dir / "src" / "three.c";
    writeFile(flat, "int f(void)\n{\nreturn 3;\n}\n");
    drive(ride, "\"" + flat.string() + "\" --project \"" + dir.string() + "\"",
          ctrl('a') + ctrl('s') + ctrl('q'), dir);
    check(readFile(flat).find("\n  return 3;") != std::string::npos,
          "the project's indent of 2 is what gets used");

    file::remove_all(dir);
}

// The MSVC half. No path is needed: the editor finds Visual Studio itself, so
// on Windows this runs whether or not anyone named a compiler.
void buildingWithCl(const std::string& ride) {
#ifndef _WIN32
    (void)ride;   // there is no cl to find anywhere else
#else
    std::printf("building with cl\n");

    file::path dir = freshProject("cl");

    file::path good = dir / "src" / "good.c";
    writeFile(good, "int twice(int n)\n{\n    return n + n;\n}\n");
    Screen ok = drive(ride, "\"" + good.string() + "\" --project \"" + dir.string() +
                           "\" --toolchain msvc",
                      ctrl('b') + ctrl('q'), dir);
    check(onScreen(ok, "lines of"), "cl builds C, found without a Developer prompt");

    // The one cc1 cannot take at all. Until 3.0 a C++ file went to cl on
    // its own here; it goes to cxx1 now, on every machine, and cl is the
    // compiler it is sent to by name - which is what this checks.
    file::path cpp = dir / "src" / "thing.cpp";
    writeFile(cpp, "class Thing {\npublic:\n    int twice(int n) { return n + n; }\n};\n"
                   "int main(void) { Thing t; return t.twice(2) - 4; }\n");
    Screen built = drive(ride, "\"" + cpp.string() + "\" --project \"" + dir.string() +
                                  "\" --toolchain msvc",
                         ctrl('b') + ctrl('q'), dir);
    check(onScreen(built, "lines of"), "and C++ goes to cl when cl is named");
    check(onScreen(built, "C++"), "with the status bar saying what it is");
    check(onScreen(built, "cl ") && !onScreen(built, "cl*"),
          "and no star, because the file did not choose it");

    file::path bad = dir / "src" / "bad.c";
    writeFile(bad, "int main(void)\n{\n    int x = ;\n    return 0;\n}\n");
    Screen broken = drive(ride, "\"" + bad.string() + "\" --project \"" + dir.string() +
                               "\" --toolchain msvc",
                          ctrl('b') + ctrl('q'), dir);
    check(onScreen(broken, "error"), "a build that fails says so");
    check(onScreen(broken, "3/5"), "and cl's line is read as well as cc1's");
    check(onScreen(broken, "col 13"), "and its column too");

    file::remove_all(dir);
#endif
}

void compiling(const std::string& ride, const std::string& cc1) {
    std::printf("building with cc1\n");

    if (cc1.empty()) {
        std::printf("  (no cc1 named, so those cases are not tried)\n");
        return;
    }

    file::path dir = freshProject("build");
    file::path good = dir / "src" / "good.c";
    writeFile(good, "int twice(int n)\n{\n    return n + n;\n}\n");

    std::string arguments = "\"" + good.string() + "\" --project \"" + dir.string() +
                            "\" --c90 \"" + cc1 + "\"";
    Screen ok = drive(ride, arguments, ctrl('b') + ctrl('q'), dir);
    check(onScreen(ok, "lines of"), "a build that works reports what it produced");
    check(onScreen(ok, "Assembly"), "and the assembly tab is there");

    file::path bad = dir / "src" / "bad.c";
    writeFile(bad, "int main(void)\n{\n    int x = ;\n    return 0;\n}\n");
    arguments = "\"" + bad.string() + "\" --project \"" + dir.string() +
                "\" --c90 \"" + cc1 + "\"";
    Screen broken = drive(ride, arguments, ctrl('b') + ctrl('q'), dir);
    check(onScreen(broken, "error"), "a build that fails says so");
    check(onScreen(broken, "3/5"), "and the caret lands on the line cc1 named");
    check(onScreen(broken, "col 13"), "in the column it named too");

    // C++ handed to cc1 is turned away before anything is run.
    file::path cpp = dir / "src" / "thing.cpp";
    writeFile(cpp, "class Thing { public: int n; };\n");
    Screen refused = drive(ride, "\"" + cpp.string() + "\" --project \"" + dir.string() +
                                "\" --toolchain c90 --c90 \"" + cc1 + "\"",
                           ctrl('b') + ctrl('q'), dir);
    check(onScreen(refused, "c90 compiles C, not C++"), "cc1 is not handed C++");

    file::remove_all(dir);
}

// The fourth compiler, driven from the keyboard: the same three things the
// cc1 case checks, for the file that goes to cxx1 on its own.
void compilingCpp(const std::string& ride, const std::string& cxx1) {
    std::printf("building with cxx1\n");

    if (cxx1.empty()) {
        std::printf("  (no cxx1 named, so those cases are not tried)\n");
        return;
    }

    file::path dir = freshProject("cxx1");
    file::path good = dir / "src" / "thing.cpp";
    writeFile(good, "class Thing {\npublic:\n    int twice(int n) { return n + n; }\n};\n"
                    "int main() { Thing t; return t.twice(2) - 4; }\n");

    std::string arguments = "\"" + good.string() + "\" --project \"" + dir.string() +
                            "\" --cpp11 \"" + cxx1 + "\"";
    Screen ok = drive(ride, arguments, ctrl('b') + ctrl('q'), dir);
    check(onScreen(ok, "lines of"), "C++ goes to cxx1 on its own, and it builds");
    check(onScreen(ok, "cpp11*"), "with a star, because the file chose it");
    check(wasShown(ok, "C++11"), "and cxx1's banner is in the console, as it printed it");

    file::path bad = dir / "src" / "bad.cpp";
    writeFile(bad, "int main()\n{\n    int x = ;\n    return 0;\n}\n");
    arguments = "\"" + bad.string() + "\" --project \"" + dir.string() +
                "\" --cpp11 \"" + cxx1 + "\"";
    Screen broken = drive(ride, arguments, ctrl('b') + ctrl('q'), dir);
    check(onScreen(broken, "error"), "a build that fails says so");
    check(onScreen(broken, "3/5"), "and the caret lands on the line cxx1 named");
    check(onScreen(broken, "col 13"), "in the column it named too");

    // C handed to cxx1 is turned away before anything is run, as C++ handed
    // to cc1 is: it would be read as C++ and the answer would be wrong.
    file::path c = dir / "src" / "plain.c";
    writeFile(c, "int main(void) { return 0; }\n");
    Screen refused = drive(ride, "\"" + c.string() + "\" --project \"" + dir.string() +
                                "\" --toolchain cpp11 --cpp11 \"" + cxx1 + "\"",
                           ctrl('b') + ctrl('q'), dir);
    check(onScreen(refused, "cpp11 compiles C++, not C"), "cxx1 is not handed C");

    // And F5: built, run, and what it printed and returned under the source.
    file::path prints = dir / "src" / "three.cpp";
    writeFile(prints, "#include <cstdio>\nint main()\n{\n    std::printf(\"counted to three\\n\");\n"
                      "    return 3;\n}\n");
    Screen ran = drive(ride, "\"" + prints.string() + "\" --project \"" + dir.string() +
                                "\" --cpp11 \"" + cxx1 + "\"",
                       kF5 + ctrl('q'), dir);
    check(rowsSaying(ran, "counted to three") == 2, "what the program printed reaches the console");
    check(wasShown(ran, "[program returned 3]"), "and what it returned is said as a number");

    file::remove_all(dir);
}

// What a tms6747 Run needs beside the editor (R5): asm6x, lnk6x, RTS6x and sim6747 - the TI program's path.
bool tiToolsBeside(const std::string& ride) {
    const std::string at = editor::path::parent(ride);
    return editor::path::exists(at + "/asm6x.exe") && editor::path::exists(at + "/lnk6x.exe") &&
           editor::path::exists(at + "/sim6747.exe") && editor::path::exists(at + "/lib/rts6x-tms6747/rts6x.lib");
}
// Build > Emulate on vm6747, the eighth item of the Build menu.
const std::string kEmulate = kF10 + times(kRight, 3) + times(kDown, 7) + kEnter;

// **The fourth target, tms6747** (R5): F5 builds a file with c90 -S, asm6x and lnk6x against RTS6x and runs
// the .out on sim6747; Build > Emulate on vm6747 runs the assembly; F4 writes one .s per source into
// <target>.vm and links <target>.out. Without the TI tools F5 fails, saying why, and Emulate still runs.
void emulatedTarget(const std::string& ride, const std::string& cc1,
                    const std::string& cxx1) {
    std::printf("the tms6747 target, run on sim6747 and emulated on vm6747\n");
    const bool ti = tiToolsBeside(ride);

    if (cc1.empty()) {
        std::printf("  (no cc1 named, so those cases are not tried)\n");
        return;
    }

    file::path dir = freshProject("tms6747");
    writeFile(dir / "project.pro",
              "{\n  \"name\": \"Trial\",\n  \"indent\": 4,\n  \"arch\": \"tms6747\",\n"
              "  \"groups\": { \"Sources\": [] }\n}\n");
    file::path file = dir / "src" / "three.c";
    writeFile(file, "#include <stdio.h>\nint main(void)\n{\n    printf(\"counted to three\\n\");\n"
                    "    return 3;\n}\n");
    std::string withCc1 = "\"" + file.string() + "\" --project \"" + dir.string() +
                          "\" --c90 \"" + cc1 + "\"";

    if (ti) {
        Screen simulated = drive(ride, withCc1, kF5 + ctrl('q'), dir);
        check(rowsSaying(simulated, "counted to three") == 2 && wasShown(simulated, "$ sim6747 --run"),
              "F5 on the C6000 target builds the TI program and runs its .out on sim6747 (R5)");
    } else {
        Screen refusedRun = drive(ride, withCc1, kF5 + ctrl('q'), dir);
        check(wasShown(refusedRun, "no asm6x beside"), "F5 with no asm6x fails the build, saying why (D2)");
    }
    Screen ran = drive(ride, withCc1, kEmulate + ctrl('q'), dir);
    check(rowsSaying(ran, "counted to three") == 2 && wasShown(ran, "$ vm6747"),
          "Emulate on vm6747 builds with c90 and runs the assembly, and the output reaches the console");
    check(wasShown(ran, "[program returned 3]"), "and what it returned is said as a number");
    check(message(ran).find("tms6747") != std::string::npos || onScreen(ran, "tms6747"),
          "and the target is named");

    // F8 on the C project: c90 -g's .dwpsn make a line table (since 2026-10-13), and the Debug session starts
    // sim6747 on the linked image where asm6x, lnk6x and RTS6x are beside the editor, vm6747 on the assembly
    // otherwise (dbg_c6xBackend) - the console's start line read after View > Console, Ctrl-W twice and End.
    Screen debug = drive(ride, withCc1, kF8 + kF10 + times(kRight, 5) + times(kDown, 2) + kEnter + ctrl('w') + ctrl('w') + "\x1b[F" + ctrl('q'), dir);
    check(wasShown(debug, ti ? "started sim6747" : "started vm6747"),
          ti ? "F8 on a C project starts sim6747 on the linked image, which carries the program's lines"
             : "F8 on a C project starts vm6747 on the assembly, there being no image to carry lines");

    if (!cxx1.empty()) {
        file::path cpp = dir / "src" / "throws.cpp";
        writeFile(cpp, "#include <cstdio>\nstruct E { int code; E(int c) : code(c) {} };\n"
                       "int risky(int x) { if (x > 1) throw E(x); return x; }\n"
                       "int main()\n{\n    try { risky(3); } catch (const E &e) { std::printf(\"caught %d\\n\", e.code); }\n"
                       "    return 5;\n}\n");
        Screen threw = drive(ride, "\"" + cpp.string() + "\" --project \"" + dir.string() +
                                      "\" --cpp11 \"" + cxx1 + "\"",
                             (ti ? kF5 : kEmulate) + ctrl('q'), dir);
        check(wasShown(threw, "caught 3"), ti ? "C++ with an exception runs on sim6747, against RTS6x's unwinder"
                                             : "C++ with an exception runs on the emulator too");
        check(wasShown(threw, "[program returned 5]"), "and returns what it returned");
        // F8 on a C++ project: cpp11 -g's .dwpsn make a line table, and the Debug session starts a C6747
        // backend - the simulator on the linked image where asm6x, lnk6x and RTS6x are beside the editor
        // (the image then carries the lines), the emulator on the assembly otherwise (dbg_c6xBackend). A
        // project of its own, since F8 with a project loaded debugs the project, and this directory's is C.
        file::path cppDir = dir / "cppproject";
        file::create_directories(cppDir / "src");
        writeFile(cppDir / "src" / "throws.cpp", readFile(cpp));
        writeFile(cppDir / "project.pro",
                  "{\n  \"name\": \"throws\",\n  \"arch\": \"tms6747\",\n  \"groups\": { \"Sources\": [\"src/throws.cpp\"] },\n"
                  "  \"build\": { \"target\": \"throws\", \"groups\": [\"Sources\"] }\n}\n");
        Screen debugged = drive(ride, "\"" + (cppDir / "src" / "throws.cpp").string() + "\" --project \"" + cppDir.string() +
                                          "\" --cpp11 \"" + cxx1 + "\"",
                                 kF8 + kF10 + times(kRight, 5) + times(kDown, 2) + kEnter + ctrl('w') + ctrl('w') + "\x1b[F" + ctrl('q'), cppDir);
        // The start line is the console's last; after the stop the Debug tab is in front, so View > Console
        // (the sixth menu, its third item) brings it back, Ctrl-W twice puts the focus in the panel (text, tree,
        // panel), and End shows the end.
        check(wasShown(debugged, ti ? "started sim6747" : "started vm6747"),
              ti ? "F8 on a C++ project starts sim6747 on the linked image, which carries the program's lines"
                 : "F8 on a C++ project starts vm6747 on the assembly, there being no image to carry lines");
    }

    // A project of two sources: built into a directory of assembly, then run.
    writeFile(dir / "src" / "sum.c", "#include \"sum.h\"\n\nint addUp(int a, int b) { return a + b; }\n");
    writeFile(dir / "src" / "sum.h", "int addUp(int a, int b);\n");
    writeFile(dir / "src" / "main.c",
              "#include <stdio.h>\n\n#include \"sum.h\"\n\n"
              "int main(void)\n{\n    printf(\"answer %d\\n\", addUp(2, 40));\n    return 0;\n}\n");
    writeFile(dir / "project.pro",
              "{\n  \"name\": \"sums\",\n  \"indent\": 4,\n  \"arch\": \"tms6747\",\n"
              "  \"groups\": {\n"
              "    \"Sources\": [\"src/sum.c\", \"src/main.c\"],\n"
              "    \"Headers\": [\"src/sum.h\"]\n  },\n"
              "  \"build\": { \"target\": \"sums\", \"groups\": [\"Sources\"] }" +
              std::string(ti ? "" : ",\n  \"emulateOnly\": true") + "\n}\n");
    std::string arguments = "--project \"" + dir.string() + "\" --c90 \"" + cc1 + "\"";
    Screen built = drive(ride, arguments, kF4 + ctrl('q'), dir);
    check(onScreen(built, "2 sources"), "F4 builds the project's two sources for the C6000");
    check(editor::path::isDirectory((dir / "sums.vm").string()), "into a directory of assembly beside the project");
    Screen ran2 = drive(ride, arguments,
                        kF10 + times(kRight, 3) + times(kDown, 3) + kEnter + ctrl('q'), dir);
    check(wasShown(ran2, "answer 42"), ti ? "and Run project runs the linked .out on sim6747 (R5)"
                                          : "and Run project, the .pro saying emulateOnly, hands it to vm6747");

    // **asm6x beside the editor turns that assembly into TI objects**, and lnk6x links them against RTS6x
    // into a .out (5.1); a TI directory named without lnk6x in it is refused by name.
    if (ti) {
        check(wasShown(built, "$ asm6x 2 sources"), "and with asm6x beside the editor the two .s are assembled");
        check(editor::path::exists((dir / "sums.vm" / "main.obj").string()) &&
              editor::path::exists((dir / "sums.vm" / "sum.obj").string()),
              "into TI objects beside the assembly");
        check(wasShown(built, "[linked ") && editor::path::exists((dir / "sums.out").string()),
              "and lnk6x links them against RTS6x into sums.out");
        Screen noTi = drive(ride, arguments + " --ti \"" + dir.string() + "\"", kF4 + ctrl('q'), dir);
        check(wasShown(noTi, "no lnk6x under"), "a TI directory without lnk6x is refused by name");
    } else {
        std::printf("  (no asm6x, lnk6x, RTS6x or sim6747 beside the editor, so the TI program cases are not tried)\n");
    }
    // Last: an Emulate builds <target>.vm again and links nothing, so the objects above are gone after it.
    Screen emulated2 = drive(ride, arguments, kEmulate + ctrl('q'), dir);
    check(wasShown(emulated2, "answer 42") && wasShown(emulated2, "$ vm6747"), "and Emulate on vm6747 runs the assembly");

    file::remove_all(dir);
}

// **Shalimar on the fourth target**: shalimar has it since 2026-09-14, and a
// program runs on vm6747 beside the runtime cpp11 compiled - lib/shmrt-tms6747
// beside the editor, which the launch adds and the compilers know nothing of.
void emulatedShalimar(const std::string& ride, const std::string& shc) {
    std::printf("Shalimar on the tms6747 target, run on sim6747 and emulated on vm6747\n");
    const bool ti = tiToolsBeside(ride);

    if (shc.empty()) {
        std::printf("  (no shc named, so those cases are not tried)\n");
        return;
    }
    const std::string runtime = editor::path::parent(ride) + "/lib/shmrt-tms6747";
    if (!editor::path::isDirectory(runtime)) {
        std::printf("  (no lib/shmrt-tms6747 beside the editor, so those cases are not tried)\n");
        return;
    }

    file::path dir = freshProject("tms6747-shalimar");
    writeFile(dir / "project.pro",
              "{\n  \"name\": \"Trial\",\n  \"indent\": 4,\n  \"arch\": \"tms6747\",\n"
              "  \"groups\": { \"Sources\": [] }\n}\n");
    file::path file = dir / "src" / "gcd.shl";
    writeFile(file, "fun <> = main() {\n  a : 48\n  b : 18\n  while b != 0 {\n    r : a % b\n"
                    "    a : b\n    b : r\n  }\n  ? \"gcd is\" a\n}\n");
    std::string arguments = "\"" + file.string() + "\" --project \"" + dir.string() +
                            "\" --shalimar \"" + shc + "\"";
    Screen ran = drive(ride, arguments, (ti ? kF5 : kEmulate) + ctrl('q'), dir);
    check(wasShown(ran, "gcd is 6"),
          ti ? "F5 on a Shalimar file for the C6000 links it against shmrt6x.lib and RTS6x and runs it on sim6747"
             : "Emulate on a Shalimar file for the C6000 runs it on vm6747 with the runtime");
    check(wasShown(ran, "[program returned 0]"), "and what it returned is said as a number");

    // A Shalimar project of two files, one of them a library with no main():
    // shc compiles them as one, so the project is one compilation and one
    // .s, not one per file - a file alone is refused for having no main().
    writeFile(dir / "src" / "twice.shl", "fun <int> = twice(n: int) {\n  return n * 2\n}\n");
    writeFile(dir / "src" / "prog.shl", "fun <> = main() {\n  ? \"twice\" twice(21)\n}\n");
    writeFile(dir / "project.pro",
              "{\n  \"name\": \"pair\",\n  \"indent\": 4,\n  \"arch\": \"tms6747\",\n"
              "  \"groups\": { \"Sources\": [\"src/prog.shl\", \"src/twice.shl\"] },\n"
              "  \"build\": { \"target\": \"prog\", \"groups\": [\"Sources\"] }" +
              std::string(ti ? "" : ",\n  \"emulateOnly\": true") + "\n}\n");
    std::string project = "--project \"" + dir.string() + "\" --shalimar \"" + shc + "\"";
    Screen built = drive(ride, project, kF4 + ctrl('q'), dir);
    check(editor::path::exists((dir / "prog.vm" / "prog.s").string()),
          "F4 on a two-file Shalimar project for the C6000 compiles them as one .s");
    Screen ran2 = drive(ride, project, kF10 + times(kRight, 3) + times(kDown, 3) + kEnter + ctrl('q'), dir);
    check(wasShown(ran2, "twice 42"), ti ? "and Run project runs its .out on sim6747" : "and Run project runs it on vm6747 with the runtime");
    if (ti)
        check(editor::path::exists((dir / "prog.out").string()), "and the build links prog.out, the runtime packed as shmrt6x.lib");

    file::remove_all(dir);
}

// The project's own build, as against the file in front of you. Two sources
// that only work together, so that a program coming out at all is proof they
// were linked and not merely compiled one at a time.
void buildingTheProject(const std::string& ride, const std::string& cc1,
                        const std::string& cxx1) {
    std::printf("building the project, not just the file\n");

    if (cc1.empty()) {
        std::printf("  (no cc1 named, so those cases are not tried)\n");
        return;
    }

    file::path dir = freshProject("target");
    writeFile(dir / "src" / "sum.c", "#include \"sum.h\"\n\nint addUp(int a, int b) { return a + b; }\n");
    writeFile(dir / "src" / "sum.h", "int addUp(int a, int b);\n");
    writeFile(dir / "src" / "main.c",
              "#include <stdio.h>\n\n#include \"sum.h\"\n\n"
              "int main(void)\n{\n    printf(\"answer %d\\n\", addUp(2, 40));\n    return 0;\n}\n");
    writeFile(dir / "project.pro",
              "{\n  \"name\": \"sums\",\n  \"indent\": 4,\n"
              "  \"groups\": {\n"
              "    \"Sources\": [\"src/sum.c\", \"src/main.c\"],\n"
              "    \"Headers\": [\"src/sum.h\"]\n  },\n"
              "  \"build\": { \"target\": \"sums\", \"groups\": [\"Sources\"] }\n}\n");

    std::string arguments = "--project \"" + dir.string() + "\" --c90 \"" + cc1 + "\"";

    Screen built = drive(ride, arguments, kF4 + ctrl('q'), dir);
    check(onScreen(built, "2 sources"), "F4 builds the project's sources, not the open file");
    check(onScreen(built, "built sums"), "and says what it built");
    check(file::exists(dir / "sums") || file::exists(dir / "sums.exe"),
          "the program is beside the project, where it can be found again");

    // Run project is on the Build menu under the two that build a file: F10,
    // three columns right to Build, three items down, enter.
    Screen ran = drive(ride, arguments,
                       kF10 + times(kRight, 3) + times(kDown, 3) + kEnter + ctrl('q'), dir);
    check(onScreen(ran, "answer 42"), "running the project runs the linked program");
    check(onScreen(ran, "returned 0"), "and reports what it returned");

    // Build > Clean, the last item: the program the build made goes, the sources and the .pro stay,
    // and the console says so.
    Screen cleaned = drive(ride, arguments, kF10 + times(kRight, 3) + times(kDown, 6) + kEnter + ctrl('q'), dir);
    check(wasShown(cleaned, "Clean succeeded: sums"), "Build > Clean says it ran, and for which project");
    check(!file::exists(dir / "sums") && !file::exists(dir / "sums.exe"), "the program the build made is gone");
    check(file::exists(dir / "src" / "main.c") && file::exists(dir / "project.pro"), "the sources and the .pro are not");
    Screen again = drive(ride, arguments, kF10 + times(kRight, 3) + times(kDown, 6) + kEnter + ctrl('q'), dir);
    check(wasShown(again, "nothing was left to remove"), "and a second Clean finds nothing left");

#ifdef _WIN32
    // Nothing to stop inside here: cc1 writes MASM for this target and MASM
    // carries no line table. The single-file case above already checks that
    // the editor says so rather than failing quietly.
    std::printf("  (x86_64-windows carries no line table, so the project's debugger is not tried)\n");
#else
    // Open sum.c, put the caret on the line that adds, break there, and ask the Debug menu for the
    // project rather than F8 for the file. The breakpoint is in the file that has no main in it,
    // which is the point: one program, two sources, and the line has to be found in the right one.
    Screen stopped = drive(ride, "\"" + (dir / "src" / "sum.c").string() + "\" " + arguments,
                           times(kDown, 2) + kF9 +
                               kF10 + times(kRight, 4) + kDown + kEnter + ctrl('q'),
                           dir);
    // "sum.c:3 in addUp" rather than "addUp" anywhere: the word is in the
    // source on the screen as well, and a check that the source is on the
    // screen is not a check that the debugger read the frame.
    check(onScreen(stopped, "sum.c:3 in addUp"),
          "the project's debugger stops in the file and function asked for");
    check(onScreen(stopped, "a = 2"), "with the argument it was called with");
    check(onScreen(stopped, "b = 40"), "and the other one");

    // Stepping out of the file it stopped in opens the file it arrives in. Nothing had main.c open
    // here, so the tab and the status bar naming it are the whole check - it used to say "stopped
    // at main.c:9" while showing sum.c, which is a stranger thing to say than saying nothing.
    Screen stepped = drive(ride, "\"" + (dir / "src" / "sum.c").string() + "\" " + arguments,
                           times(kDown, 2) + kF9 +
                               kF10 + times(kRight, 4) + kDown + kEnter + kF7 + ctrl('q'),
                           dir);
    check(onScreen(stepped, "main.c"), "stepping into another file opens it");
    check(onScreen(stepped, "in main"), "and says it is in main now");
#endif

    // An error in a file that is not open opens it first. Nothing is opened at
    // startup here, so the caret arriving in main.c is the whole check.
    writeFile(dir / "src" / "main.c",
              "#include <stdio.h>\n\n#include \"sum.h\"\n\n"
              "int main(void)\n{\n    int total = addUp(2, 40)\n"
              "    printf(\"answer %d\\n\", total);\n    return 0;\n}\n");
    Screen broken = drive(ride, arguments, kF4 + ctrl('q'), dir);
    check(onScreen(broken, "error"), "an error in the project build is reported");
    check(onScreen(broken, "main.c"), "naming the file it is in");
    // Line 8, not 7: a missing semicolon is reported where the next thing was found, the line after the one missing it.
    check(onScreen(broken, "8/10"), "and the caret goes there, in a file nothing had opened");

    // Both languages in one target, once refused as "cannot make one program" and now split by
    // language: cc1 takes the C, cxx1 the C++ (since 3.0), and the objects meet at the linker. The
    // main.c the case before left broken is written again, so a syntax error cannot stop this before the mixture is reached; without a cxx1 the case says so.
    if (cxx1.empty()) {
        std::printf("  (no cxx1 named, so the mixed target is not built)\n");
    } else {
    std::string mixedArguments = arguments + " --cpp11 \"" + cxx1 + "\"";
    writeFile(dir / "src" / "main.c",
              "#include <stdio.h>\n\n#include \"sum.h\"\n\n"
              "int twice(int n);\n\n"
              "int main(void)\n{\n    printf(\"answer %d\\n\", twice(addUp(2, 19)));\n"
              "    return 0;\n}\n");
    writeFile(dir / "src" / "extra.cpp",
              "extern \"C\" int twice(int n) { return n * 2; }\n");
    writeFile(dir / "project.pro",
              "{\n  \"name\": \"sums\",\n  \"indent\": 4,\n"
              "  \"groups\": {\n"
              "    \"Sources\": [\"src/sum.c\", \"src/main.c\", \"src/extra.cpp\"]\n  },\n"
              "  \"build\": { \"target\": \"sums\", \"groups\": [\"Sources\"] }\n}\n");
    Screen mixed = drive(ride, mixedArguments, kF4 + ctrl('q'), dir);
    // wasShown, not onScreen: the nine-row console has scrolled past the first compiler's line by the time two compilers and a linker are done - the check is that the editor said it.
    check(wasShown(mixed, "Sources (c90)"), "a group of two languages sends the C to cc1");
    // cxx1 on every machine, and nothing in the project file said so: until 3.0 this was the host's
    // compiler, which a group now has to ask for by name. Written out rather than asked of the
    // editor's resolve(): this harness links src/path.cpp only, since a test sharing the editor's opinion cannot catch it being wrong.
    check(wasShown(mixed, "Sources (cpp11)"),
          "and the C++ to cxx1, without being told to");
    check(!wasShown(mixed, "cannot make one program"),
          "and is not refused for holding both any more");
    check(wasShown(mixed, "linking with"), "and links the two compilers' objects itself");
    check(onScreen(mixed, "built sums"), "the two compilers' objects link into one program");

    // And it runs, which is the whole of what a mixed target is for: a C main
    // calling a function cxx1 compiled, in one program.
    Screen ranMixed = drive(ride, mixedArguments,
                            kF10 + times(kRight, 3) + times(kDown, 3) + kEnter + ctrl('q'),
                            dir);
    check(wasShown(ranMixed, "answer 42"), "and runs, C calling into what cxx1 made");
    }

    // Debugging the project is the same choice again: the program under the debugger is the one the
    // project builds, not the file in front of you. The breakpoint goes in a file that has no main
    // in it - one program, three sources, and the debugger has to find the line in the right one.
    writeFile(dir / "src" / "main.c",
              "#include <stdio.h>\n\n#include \"sum.h\"\n\n"
              "int main(void)\n{\n    printf(\"answer %d\\n\", addUp(2, 40));\n    return 0;\n}\n");
    // And a project that says nothing about building is not an error, just
    // nothing to build - the file in front of you is still Ctrl-B's business.
    file::path plain = freshProject("noTarget");
    writeFile(plain / "src" / "one.c", "int main(void) { return 0; }\n");
    Screen quiet = drive(ride, "--project \"" + plain.string() + "\" --c90 \"" + cc1 + "\"",
                         kF4 + ctrl('q'), plain);
    check(onScreen(quiet, "hold no source"),
          "a project with no build entry builds its Sources, and says when that is empty");

    file::remove_all(dir);
    file::remove_all(plain);
}

// A file that compiles to different code depending on NDEBUG, so that the two
// configurations can be told apart by what came out rather than by what the
// command line claimed.
const char* kTwoWays =
    "#ifdef NDEBUG\n"
    "int value(void) { return 1; }\n"
    "#else\n"
    "int value(void) { return 1; }\n"
    "int second(void) { return 2; }\n"
    "int third(void) { return 3; }\n"
    "int fourth(void) { return 4; }\n"
    "#endif\n";

void configurations(const std::string& ride, const std::string& cc1) {
    std::printf("debug and release\n");

    file::path dir = freshProject("config");
    file::path file = dir / "src" / "twoways.c";
    writeFile(file, kTwoWays);

    std::string common = "\"" + file.string() + "\" --project \"" + dir.string() + "\"";

    // The word is in the status bar whether or not anything can be built.
    Screen shown = drive(ride, common + " --config release", ctrl('q'), dir);
    check(onScreen(shown, "release"), "the status bar says which configuration");
    Screen shownDebug = drive(ride, common, ctrl('q'), dir);
    check(onScreen(shownDebug, "debug"), "and debug is what it starts in");

    // **The project file does not remember it, and that is the point.** Debug or release is what
    // you are doing today, not a property of the program - and a project file travels, so one with
    // "config": "release" would put everyone who opened it into release. A "config" key is read by nothing now.
    writeFile(dir / "Conf.pro",
              "{\n  \"name\": \"Conf\",\n  \"config\": \"release\",\n"
              "  \"groups\": { \"Sources\": [] }\n}\n");
    Screen fromFile = drive(ride, common, ctrl('q'), dir);
    check(onScreen(fromFile, "debug"),
          "a configuration written into a project file is ignored");

    Screen overridden = drive(ride, common + " --config release", ctrl('q'), dir);
    check(onScreen(overridden, "release"), "and the flag still decides the run");

#ifdef _WIN32
    // cl can show the difference whether or not cc1 is about: NDEBUG takes
    // three functions out, and /O2 rewrites what is left.
    {
        Screen clDebug = drive(ride, common + " --toolchain msvc --config debug",
                               ctrl('b') + ctrl('q'), dir);
        Screen clRelease = drive(ride, common + " --toolchain msvc --config release",
                                 ctrl('b') + ctrl('q'), dir);
        check(wasShown(clDebug, "/Od"), "cl is given /Od for debug");
        check(wasShown(clRelease, "/O2"), "and /O2 for release");
        check(!message(clDebug).empty() && message(clDebug) != message(clRelease),
              "and the two produce different code");
    }
#endif

    if (cc1.empty()) {
        std::printf("  (no cc1 named, so its two configurations are not compared)\n");
        file::remove_all(dir);
        return;
    }

    std::string withCc1 = common + " --c90 \"" + cc1 + "\"";
    Screen debug = drive(ride, withCc1 + " --config debug", ctrl('b') + ctrl('q'), dir);
    Screen release = drive(ride, withCc1 + " --config release", ctrl('b') + ctrl('q'), dir);

    check(wasShown(debug, "-D_DEBUG=1"), "the debug define is on the command line");
    check(wasShown(release, "-DNDEBUG=1"), "and the release one is");

    // And it did something: the same file compiled two ways gives two different
    // amounts of assembly, so the define reached the preprocessor rather than
    // merely being printed.
    check(!message(debug).empty() && message(debug) != message(release),
          "the two configurations produce different code");
    check(message(debug).find("lines of") != std::string::npos,
          "and both of them built");

    file::remove_all(dir);
}

// What the Debug panel says depends on the target: cc1 writes DWARF for two of the three and
// nothing for the one it generates MASM for. Both answers are the editor's own words about a
// compiler it has not run, so this needs no cc1. The menu opens on File every time since 2026-09-19, so every walk starts from File.
void debugPanelPerTarget(const std::string& ride) {
    std::printf("what the Debug panel says about each target\n");

    file::path dir = freshProject("debugpanel");
    file::path file = dir / "src" / "one.c";
    writeFile(file, "int main(void) { return 0; }\n");
    std::string common = "\"" + file.string() + "\" --project \"" + dir.string() + "\"";

    // Menus are reached by counting, so anything added to one moves everything after it - a column
    // added moves the columns to its right, an item added the items below. Build is the fourth
    // column, its Debug panel the sixth item, and Target is two columns further on now that Debug sits between them.
    const int kBuildColumn = 5;      // View, since 2026-09-19: the panel's tabs are there
    const int kTargetColumn = 8;
    // Fourth in View: Project pane, Bottom panel, a rule, Console, Debug.
    const int kDebugPanelItem = 3;
    const std::string showDebugTab =
        kF10 + times(kRight, kBuildColumn) + times(kDown, kDebugPanelItem) + kEnter;
    const std::string toTarget = kF10 + times(kRight, kTargetColumn);

    Screen linux = drive(ride, common,
                         showDebugTab + toTarget + kDown + kEnter + ctrl('q'), dir);
    check(onScreen(linux, "DWARF"), "x86_64-linux is said to carry DWARF");
    check(onScreen(linux, "x86_64-linux"), "and named while it is said");

    Screen darwin = drive(ride, common,
                          showDebugTab + toTarget + times(kDown, 2) + kEnter + ctrl('q'), dir);
    check(onScreen(darwin, "DWARF"), "and arm64-darwin carries it as well");

    Screen windows = drive(ride, common,
                           showDebugTab + toTarget + kEnter + ctrl('q'), dir);
    check(onScreen(windows, "CodeView"),
          "x86_64-windows is said to carry CodeView (M10)");
    check(!onScreen(windows, "DWARF"), "and is not told it has DWARF");

    // Switching the target under an open panel refills it, rather than leaving
    // what was true of the target before.
    // The third F10 walks to Target again, as the menu opens on File.
    Screen switched = drive(ride, common,
                            showDebugTab + toTarget + kDown + kEnter +
                                toTarget + kEnter + ctrl('q'),
                            dir);
    check(onScreen(switched, "CodeView") && !onScreen(switched, "DWARF"),
          "and switching target changes what the open panel already said");

    // The flag itself, in the status bar, with no compiler run; Ctrl-D toggles, so twice from debug is release and back.
    const std::string sayConfig = ctrl('d') + ctrl('d');

    Screen debugOnLinux = drive(ride, common,
                                kF10 + times(kRight, kTargetColumn) + kDown + kEnter +
                                    sayConfig + ctrl('q'),
                                dir);
    check(wasShown(debugOnLinux, "-g -D_DEBUG=1"), "a debug build of it asks for -g");

    Screen debugOnWindows = drive(ride, common,
                                  kF10 + times(kRight, kTargetColumn) + kEnter + sayConfig +
                                      ctrl('q'),
                                  dir);
    check(wasShown(debugOnWindows, "-D_DEBUG=1"),
          "a debug build of the third defines _DEBUG");
    check(wasShown(debugOnWindows, "-g -D_DEBUG=1"),
          "and asks for -g, which is CodeView there (M10)");

    file::remove_all(dir);
}

const char* const kPrintsAndReturns =
    "#include <stdio.h>\n"
    "int main(void)\n"
    "{\n"
    "    printf(\"counted to three\\n\");\n"
    "    return 3;\n"
    "}\n";

// F5 compiles, links and runs, three things that can each go their own way. What the console has
// to keep apart is a compiler that refused and a program that ran and returned something other than
// zero: only the program knows what its number meant, and a build that failed never got one.
void runningTheProgram(const std::string& ride, const std::string& cc1) {
    std::printf("building it, and running what came out\n");

    // A target this machine cannot run is turned away before anything is built, so this needs no
    // compiler: x86_64-windows is the one nothing here is, except on Windows, where x86_64-linux is.
    // A project of its own, because a chosen target is remembered in the project file and a second editor on the same one would open on it.
    const int kTargetColumn = 8;   // File, Edit, Project, Build, Debug, View, Language, Tools, Target
#ifdef _WIN32
    const std::string toElsewhere = kF10 + times(kRight, kTargetColumn) + kDown + kEnter;
#else
    const std::string toElsewhere = kF10 + times(kRight, kTargetColumn) + kEnter;
#endif
    file::path away = freshProject("run-elsewhere");
    file::path awayFile = away / "src" / "three.c";
    writeFile(awayFile, kPrintsAndReturns);
    Screen refused = drive(ride,
                           "\"" + awayFile.string() + "\" --project \"" + away.string() + "\"",
                           toElsewhere + kF5 + ctrl('q'), away);
    check(wasShown(refused, "only reaches -S here"),
          "a target this machine cannot run is turned away");
    check(!wasShown(refused, "program returned"), "and nothing is run");
    file::remove_all(away);

    file::path dir = freshProject("run");
    file::path file = dir / "src" / "three.c";
    writeFile(file, kPrintsAndReturns);
    std::string common = "\"" + file.string() + "\" --project \"" + dir.string() + "\"";

    if (cc1.empty()) {
        std::printf("  (no cc1 named, so nothing is actually built and run)\n");
        file::remove_all(dir);
        return;
    }

    std::string withCc1 = common + " --c90 \"" + cc1 + "\"";

    // Twice: once in the source being edited, once in the console under it. Once
    // would be the source alone, which is on the screen whether anything ran or
    // not.
    Screen ran = drive(ride, withCc1, kF5 + ctrl('q'), dir);
    check(rowsSaying(ran, "counted to three") == 2,
          "what the program printed reaches the console");
    check(wasShown(ran, "[program returned 3]"), "and what it returned is said as a number");
    check(message(ran).find("it returned 3") != std::string::npos,
          "and the status bar says so too");

    // The same file with the semicolon taken out: the compiler stops, and the
    // console must not go on to claim a program ran.
    writeFile(file, "int main(void) { return 0 }\n");
    Screen broken = drive(ride, withCc1, kF5 + ctrl('q'), dir);
    check(!wasShown(broken, "program returned"), "a file that will not compile runs nothing");
    check(message(broken).find("error") != std::string::npos, "and the error is what is said");

    file::remove_all(dir);
}

const char* const kWorthStoppingIn =
    "static int twice(int n)\n"
    "{\n"
    "    int doubled = n * 2;\n"
    "    return doubled;\n"
    "}\n"
    "\n"
    "int main(void)\n"
    "{\n"
    "    int total = 0;\n"
    "    for (int i = 1; i <= 3; ++i) {\n"
    "        total = total + twice(i);\n"
    "    }\n"
    "    return total;\n"
    "}\n";

// Stopping the program on a line and walking through it, driven the way a
// person drives it: F9 on the line, F8 to start, F7 and F6 to move.
void stoppingAndStepping(const std::string& ride, const std::string& cc1) {
    std::printf("breakpoints, and stepping through what stopped\n");

    file::path dir = freshProject("debugging");
    file::path file = dir / "src" / "stepped.c";
    writeFile(file, kWorthStoppingIn);
    std::string common = "\"" + file.string() + "\" --project \"" + dir.string() + "\"";

    // Line 11 is the one inside the loop, and the caret starts on line 1.
    const std::string toLoopBody = times(kDown, 10);

    // A breakpoint is the editor's own note and needs no compiler: it can be
    // set, seen and taken away with nothing installed at all.
    Screen marked = drive(ride, common, toLoopBody + kF9 + ctrl('q'), dir);
    check(wasShown(marked, "breakpoint on line 11"), "F9 puts a breakpoint on the line");
    // The number is right-aligned with a gap after it, so the marker sits in the column before the first digit and nothing moves.
    check(onScreen(marked, "*11"), "and marks it in the gutter, beside the number");

    // Ctrl with an arrow, a key the console has to send and the decoder read before anything can
    // be done with it. Asked with nothing stopped, because that answer needs no debugger and so is
    // asked on all three machines - including the one where cc1's target cannot be debugged and every check below is skipped.
    Screen noStack = drive(ride, common, kCtrlUp + ctrl('q'), dir);
    check(wasShown(noStack, "no stack to walk"),
          "Ctrl-Up arrives as Ctrl-Up, and says there is nothing stopped");

    Screen unmarked = drive(ride, common, toLoopBody + kF9 + kF9 + ctrl('q'), dir);
    check(wasShown(unmarked, "breakpoint off line 11"), "and F9 again takes it away");
    check(!onScreen(unmarked, "*11"), "leaving the gutter as it was");

    // Two blocks stood here until 2026-08-24 - a breakpoint following its file through Rename...,
    // and a deleted file's breakpoints not returning with the name - and went with those menu items.
    // Editor::renameFile and deleteFile still carry them; the blocks are in this file's history at the commit that removed them, and should return with the items.

    // Since M10 the same walk runs on Windows too: c90 writes CodeView there and cdb reads it.
    if (cc1.empty()) {
        std::printf("  (no cc1 named, so nothing is built to stop inside)\n");
        file::remove_all(dir);
        return;
    }

    std::string withCc1 = common + " --c90 \"" + cc1 + "\"";

    Screen stopped = drive(ride, withCc1, toLoopBody + kF9 + kF8 + ctrl('q'), dir);
    check(onScreen(stopped, "stopped at stepped.c:11"), "F8 runs it and it stops on the line");
    check(onScreen(stopped, "in main"), "saying which function that line is in");
    check(onScreen(stopped, ">11"), "the gutter marks where it is standing");

    // The variables are cc1's own debug information, read back by somebody
    // else's debugger and shown by this editor.
    check(onScreen(stopped, "total = 0"), "and the locals are there, with their values");
    check(onScreen(stopped, "i = 1"), "including the one the loop declared");
    check(!onScreen(stopped, "called from"),
          "and nothing is said about a stack, since main was called by nobody");

    Screen inside = drive(ride, withCc1, toLoopBody + kF9 + kF8 + kF6 + ctrl('q'), dir);
    check(onScreen(inside, "in twice"), "F6 steps into the call");
    check(onScreen(inside, "n = 1"), "where the argument is in scope");
    check(onScreen(inside, "called from"), "and now there is a stack to show");
    check(onScreen(inside, "main   stepped.c:11"),
          "naming what called it and the line waiting for it to come back");

    // And going to that frame, driven as a person does: Ctrl-W twice to reach the panel - the first
    // press is the project pane - then down to the frame and enter. The panel's top line is the
    // cursor's, so six presses put the frame there: "stopped at", the two locals n and doubled, a blank, "called from", then main stepped.c:11.
    const std::string toTheFrame = ctrl('w') + ctrl('w') + times(kDown, 6) + kEnter;
    Screen went = drive(ride, withCc1, toLoopBody + kF9 + kF8 + kF6 + toTheFrame + ctrl('q'), dir);
    check(wasShown(went, "where the call came from"), "enter on a frame goes to it");
    check(onScreen(went, "11/14"), "putting the caret on the line that is waiting");
    check(onScreen(went, "[text]"), "and the keyboard back in the text");

    // And the variables are that frame's: total and i belong to main and are
    // not in scope in twice at all. The tab says whose they are, since the
    // line above them still says the program stopped in twice.
    check(onScreen(went, "the variables are main's"), "the tab says whose variables it shows");
    check(onScreen(went, "total = 0"), "and they are the caller's own");
    check(!onScreen(went, "n = 1"), "with nothing left of the frame it stopped in");

    // The way back is the top line, which names that frame - and the cursor is
    // already standing on it, the tab having come back to the top.
    const std::string andBack = ctrl('w') + ctrl('w') + kEnter;
    Screen backAgain = drive(
        ride, withCc1, toLoopBody + kF9 + kF8 + kF6 + toTheFrame + andBack + ctrl('q'), dir);
    check(wasShown(backAgain, "back where it stopped"), "enter on the top line goes back");
    check(onScreen(backAgain, "n = 1"), "and the variables are the stopped frame's again");
    check(!onScreen(backAgain, "the variables are main's"),
          "with nothing said about whose they are, the top line saying it");

    // The program has not moved: going to a line is not stepping. "> 3", not ">3" - the number is right-aligned in the gutter, so a one-digit line has a space before it where an eleven has none.
    check(onScreen(went, "> 3"), "while the program is still standing where it stopped");

    // The same walk with a key, from the text, without going near the panel -
    // which is where a person is when the question occurs to them.
    Screen up = drive(ride, withCc1, toLoopBody + kF9 + kF8 + kF6 + kCtrlUp + ctrl('q'), dir);
    check(onScreen(up, "the variables are main's"), "Ctrl-Up looks at what called this");
    check(onScreen(up, "total = 0"), "with that frame's variables");
    check(onScreen(up, "11/14"), "and the caret on the line waiting for the call");

    // And the gutter says which line that is, in the code and not only the panel; it outranks the
    // breakpoint on the same line - the mark is about now, the breakpoint about every run. With the
    // line's own text after it, because "stepped.c:11" in the panel holds ":11" too and a check matching that passes whatever the gutter does.
    const std::string markedLine = ":11         total = total + twice(i);";
    const std::string breakLine = "*11         total = total + twice(i);";
    check(onScreen(up, markedLine), "the gutter marks the line the frame is waiting on");
    check(onScreen(up, "> 3     int doubled"),
          "while the arrow stays where the program is standing");
    check(!onScreen(up, breakLine), "and the breakpoint's own mark gives way to it");

    // And the ends of it, which are the two answers with nowhere to go.
    Screen top = drive(ride, withCc1,
                       toLoopBody + kF9 + kF8 + kF6 + kCtrlUp + kCtrlUp + ctrl('q'), dir);
    check(wasShown(top, "nothing called main"), "and says so at the top of the stack");

    Screen down = drive(ride, withCc1,
                        toLoopBody + kF9 + kF8 + kF6 + kCtrlUp + kCtrlDown + ctrl('q'), dir);
    check(wasShown(down, "back where it stopped"), "Ctrl-Down comes back down");
    check(onScreen(down, "n = 1"), "to the variables of the frame it stopped in");
    check(onScreen(down, "3/14"), "and the line it stopped on");
    check(!onScreen(down, markedLine), "with the mark gone from the frame it was looking at");
    check(onScreen(down, breakLine), "and the breakpoint's own mark back where it was");

    Screen bottom = drive(ride, withCc1, toLoopBody + kF9 + kF8 + kF6 + kCtrlDown + ctrl('q'), dir);
    check(wasShown(bottom, "nothing below it"), "and says so at the bottom of it");

    // Setting a variable: the cursor on its line in the panel, enter, and the value typed into the
    // box that asks for filenames. Which variable that line holds is the debugger's: lldb lists them
    // as declared and gdb the innermost block first, so the check is that the one on that line was set, whichever it is.
    const std::string toTheVariable = ctrl('w') + ctrl('w') + times(kDown, 2) + kEnter;
    Screen written = drive(ride, withCc1,
                           toLoopBody + kF9 + kF8 + toTheVariable + "7" + kEnter + ctrl('q'), dir);
    check(wasShown(written, "is 7 now"), "enter on a variable sets it");
    check(onScreen(written, "= 7"), "and the tab shows what is in there now");

    // And a value it will not take is refused in the debugger's own words,
    // which name the mistake better than anything the editor could invent.
    Screen refused = drive(ride, withCc1,
                           toLoopBody + kF9 + kF8 + toTheVariable + "nosuch" + kEnter + ctrl('q'),
                           dir);
    check(!onScreen(refused, "= nosuch"), "a value it will not take is not written into the tab");
    check(onScreen(refused, "total = 0"), "and the variable is left as it was");

    // A watch, added from the Debug menu and then left alone: what makes it a watch is that it is
    // read again at the next stop without being asked for. Nine items down that menu - Start, Debug
    // project, breakpoint, over, into, out, up, down, and then Watch expression.
    const std::string toWatch = kF10 + times(kRight, 4) + times(kDown, 8) + kEnter;
    Screen watching = drive(ride, withCc1,
                            toLoopBody + kF9 + kF8 + toWatch + "total + i" + kEnter + ctrl('q'),
                            dir);
    check(onScreen(watching, "watching"), "the tab has a block for what is being watched");
    check(onScreen(watching, "total + i = 1"), "with the expression answered where it stopped");

    Screen followed = drive(ride, withCc1,
                            toLoopBody + kF9 + kF8 + toWatch + "total + i" + kEnter + kF8 +
                                ctrl('q'),
                            dir);
    check(onScreen(followed, "total + i = 4"),
          "and it follows the stepping - 2 plus 2 the next time round");

    // A line that is neither says so rather than doing something. Six down from the top of that
    // tab is the "called from" heading, which names a frame without being one - the likeliest line
    // to press enter on by mistake.
    const std::string toTheHeading = ctrl('w') + ctrl('w') + times(kDown, 5) + kEnter;
    Screen neither = drive(ride, withCc1,
                           toLoopBody + kF9 + kF8 + kF6 + toTheHeading + ctrl('q'), dir);
    check(wasShown(neither, "neither a frame nor a variable"),
          "enter on a line that is neither says so");

    Screen carried = drive(ride, withCc1, toLoopBody + kF9 + kF8 + kF7 + kF8 + ctrl('q'), dir);
    check(onScreen(carried, "total = 2"), "F7 steps over it and F8 carries on round the loop");
    check(onScreen(carried, "i = 2"), "with the counter moved on");

    file::remove_all(dir);
}

// Opening a directory that has no project file. It gets one rather than the
// editor opening without a project, which is the difference between a tool
// that starts and a tool that asks you to go and make something first.
void aDirectoryWithNoProject(const std::string& ride) {
    std::printf("opening somewhere that has no project file\n");

    file::path dir = file::temp_directory_path() / "ride-session-noproject";
    file::remove_all(dir);
    editor::path::makeDirectories((dir / "src").string());
    writeFile(dir / "src" / "one.c", "int one;\n");
    writeFile(dir / "notes.txt", "not source\n");

    // Named after the directory, and none of the older whole-directory names.
    const std::string named = "ride-session-noproject.pro";
    check(!file::exists(dir / named), "there is no project file to begin with");

    Screen made = drive(ride, "--project \"" + dir.string() + "\"", ctrl('q'), dir);
    check(file::exists(dir / named), "opening there writes one, under the directory's name");
    check(wasShown(made, "no project here, so"), "and says that is what it did");
    check(onScreen(made, "one.c"), "the source it found is in the pane");
    check(!onScreen(made, "notes.txt"), "and what is not source is not");

    // Opened again, the file that was written is the file that is read - no
    // second one, and nothing said about making anything.
    Screen again = drive(ride, "--project \"" + dir.string() + "\"", ctrl('q'), dir);
    check(!wasShown(again, "no project here, so"), "opening it again makes nothing");
    check(onScreen(again, "one.c"), "and reads back what was written");
    check(wasShown(again, "ready"), "and says it is ready, having nothing to do first");

    // Help is the last column, and About is under it. Checked here as well as in the window, since
    // both show the core's lines. Eight rights rather than seven since Language joined the bar, three
    // downs since Contents and then Environment joined this menu above About - counted in the one place that walks them.
    Screen about = drive(ride, "--project \"" + dir.string() + "\"",
                         kF10 + times(kRight, 9) + times(kDown, 3) + kEnter + ctrl('q'), dir);
    check(onScreen(about, "RIDE 5.17"), "About names the product and version");
    check(onScreen(about, "cpp11"), "and the fourth compiler is on its list");
    check(onScreen(about, "G. R. Akhtar"), "and who it belongs to");
    check(onScreen(about, "Islamabad"), "and where they are, which the last line must not lose");

    // Environment, one above About: every tool, and the file each one resolved to.
    Screen environment = drive(ride, "--project \"" + dir.string() + "\"",
                               kF10 + times(kRight, 9) + times(kDown, 2) + kEnter + ctrl('q'), dir);
    check(onScreen(environment, "the environment in force"), "Help > Environment says what is in force");
    check(onScreen(environment, "Compilers and tools"), "and opens on the compilers, where each one was found");

    // A project file that will not parse is somebody's work and is left alone.
    writeFile(dir / "project.pro", "{ this is not json\n");
    Screen broken = drive(ride, "--project \"" + dir.string() + "\"", ctrl('q'), dir);
    check(readFile(dir / "project.pro").find("not json") != std::string::npos,
          "a project file that will not parse is not written over");
    check(!wasShown(broken, "no project here, so"), "and nothing is made in its place");

    file::remove_all(dir);
}

}  // namespace

// The third language, driven rather than described. Everything below asks the editor to do
// something with a .shl and looks at what came back on the screen; nothing here reaches into the core.

// The Language menu's Convert, which runs c2s over the open file. One item and not two: which way
// round it goes is what the file already is, and the column it sits in says so - worth driving from
// the keyboard because the direction is taken from the editor's idea of the language, not the file name, which is what a .txt holding C is for.
void convertingFromTheMenu(const std::string& ride, const std::string& c2s) {
    std::printf("converting between C and Shalimar from the Language menu\n");

    if (c2s.empty()) {
        std::printf("  (no c2s named, so those cases are not tried)\n");
        return;
    }

    // Language is the sixth column, and Convert the seventh selectable item in it - By extension,
    // C, C++, Shalimar, JSON, Plain text, then this; stepTo skips the separator. These counts move
    // if the column does, which is what this comment is here to make findable.
    const std::string toLanguage = kF10 + times(kRight, 6);
    const std::string convert = toLanguage + times(kDown, 6) + kEnter;
    const std::string asC = toLanguage + times(kDown, 1) + kEnter;

    file::path dir = freshProject("convert");

    // C to Shalimar. The direction is never stated - the .c is what says it.
    file::path source = dir / "src" / "adder.c";
    writeFile(source,
              "#include <stdio.h>\n"
              "int main(void)\n"
              "{\n"
              "    int i;\n"
              "    for (i = 0; i < 3; i++) {\n"
              "        printf(\"i %d \\n\", i);\n"
              "    }\n"
              "    return 0;\n"
              "}\n");

    std::string arguments = "\"" + source.string() + "\" --c2s \"" + c2s + "\"";
    Screen made = drive(ride, arguments, convert + ctrl('q'), dir);
    check(file::exists(dir / "src" / "adder.shl"),
          "a .c converts to a .shl beside it");
    const std::string shalimar = readFile(dir / "src" / "adder.shl");
    check(shalimar.find("fun <> = main()") != std::string::npos,
          "and what was written is Shalimar");
    check(onScreen(made, "adder.shl"), "and the converted file is opened");

    // **.shl and not .shm, and the screen is what says why.** The editor reads only .shl as
    // Shalimar, so a written .shm opened as plain text in the editor that had just written it - no
    // colouring, and Build behind the wrong compiler. Nothing else notices a suffix; this does.
    check(onScreen(made, "Shalimar"),
          "and it is Shalimar to the editor, not text with a suffix");

    // And back again, from the .shl this time - the round trip driven entirely from the menu, the
    // direction never named. The .c is removed first: it already held `int main`, so leaving it let
    // this pass without the second leg running at all, which it did from 2026-08-23 until 2026-08-27.
    file::path back = dir / "src" / "adder.shl";
    file::remove(dir / "src" / "adder.c");
    arguments = "\"" + back.string() + "\" --c2s \"" + c2s + "\"";
    drive(ride, arguments, convert + ctrl('q'), dir);
    check(readFile(dir / "src" / "adder.c").find("int main") != std::string::npos,
          "and a .shl converts back to C, over a file that has to be written");

    // The file whose suffix is wrong, which is the reason this item is in
    // the Language column at all. A .txt holding C is plain text until the
    // menu above says it is C, and then it converts like C.
    file::path odd = dir / "src" / "hidden.txt";
    writeFile(odd,
              "#include <stdio.h>\n"
              "int main(void)\n"
              "{\n"
              "    printf(\"hidden \\n\");\n"
              "    return 0;\n"
              "}\n");
    arguments = "\"" + odd.string() + "\" --c2s \"" + c2s + "\"";

    Screen refused = drive(ride, arguments, convert + ctrl('q'), dir);
    check(!file::exists(dir / "src" / "hidden.shl"),
          "plain text is not converted");
    check(onScreen(refused, "between C and Shalimar"),
          "and it says what it converts between instead");

    // From File again, as the menu opens there every time now.
    const std::string convertAgain = kF10 + times(kRight, 6) + times(kDown, 6) + kEnter;
    drive(ride, arguments, asC + convertAgain + ctrl('q'), dir);
    check(file::exists(dir / "src" / "hidden.shl"),
          "but the same file read as C converts");

    // **What c2s refuses outright, which is not what it converts badly.** A preprocessor directive
    // stops it before it starts: no file, exit 1 - the same 1 as a file written with BEYOND marks,
    // and believing it once opened an empty buffer as converted. #ifdef with an #else, since c2s drops a guard round its own #define since 2026-08-27 and the old #ifndef converts now.
    file::path asks = dir / "src" / "asks.c";
    writeFile(asks,
              "#ifdef LIMIT\n"
              "#define LIMIT 3\n"
              "#else\n"
              "#define LIMIT 4\n"
              "#endif\n"
              "int main(void)\n"
              "{\n"
              "    return LIMIT;\n"
              "}\n");
    arguments = "\"" + asks.string() + "\" --c2s \"" + c2s + "\"";

    Screen questions = drive(ride, arguments, convert + ctrl('q'), dir);
    check(!file::exists(dir / "src" / "asks.shl"),
          "a file c2s refuses leaves nothing behind");
    check(onScreen(questions, "not converted"),
          "and the editor says so rather than claiming it wrote one");
    check(!onScreen(questions, "BEYOND"),
          "and does not offer the message for a file that was written");

    // **The panel wraps, and this is the case that made it matter.** c2s's questions run past ninety
    // columns and the panel is fifty-odd beside an open project pane, so what the message asks for
    // sat past the border with no key able to reach it. Proved on the grown screen below: a wrap shows in a line's tail, which here is off the bottom of seven rows.

    // And taller when it is asked for: Ctrl-W twice puts the cursor in the
    // panel - past the project pane - and shift-up grows it, which shows more
    // of what came before rather than blank rows underneath.
    const std::string intoPanel = ctrl('w') + ctrl('w');
    const std::string taller = times(kShiftUp, 4);
    Screen grown = drive(ride, arguments, convert + intoPanel + taller + ctrl('q'), dir);
    check(onScreen(grown, "panel: 11 rows"), "the panel grows when it is asked to");

    // Named against the seven-row screen above rather than by eye: this is
    // the hint for the second question, which is four rows further back than
    // seven rows can reach.
    check(!onScreen(questions, "remove both the #define"),
          "seven rows do not reach the second question's hint");
    check(onScreen(grown, "remove both the #define"),
          "and eleven do, which is what the taller panel is for");

    // That hint runs past 120 characters and the panel is 78 wide, so its last words are on screen
    // only because the line wrapped. Cut at the border - the panel did until 2026-08-27 - they were
    // unreachable by any key, and it is the end of a diagnostic that says what to do about it.
    check(onScreen(grown, "the #if that reads it"),
          "and the end of a line too long for the panel, which is wrapped now");
}

void compilingShalimar(const std::string& ride, const std::string& shc) {
    std::printf("building Shalimar with shc\n");

    if (shc.empty()) {
        std::printf("  (no shc named, so those cases are not tried)\n");
        return;
    }

    file::path dir = freshProject("shalimar");
    file::path good = dir / "src" / "good.shl";
    writeFile(good,
              "fun <int> = twice(n: int) {\n"
              "  return n + n\n"
              "}\n"
              "\n"
              "fun <> = main() {\n"
              "  ? twice(21)\n"
              "}\n");

    std::string arguments = "\"" + good.string() + "\" --project \"" + dir.string() +
                            "\" --shalimar \"" + shc + "\"";

    Screen opened = drive(ride, arguments, ctrl('q'), dir);
    check(onScreen(opened, "Shalimar"), "the status bar names the language");

    Screen ok = drive(ride, arguments, ctrl('b') + ctrl('q'), dir);
    check(onScreen(ok, "lines of"), "a build that works reports what it produced");
    check(onScreen(ok, "Assembly"), "and the assembly tab is there");

    // F5 builds a program and runs it, which is the whole of what a Shalimar
    // program is for.
    Screen ran = drive(ride, arguments, kF5 + ctrl('q'), dir);
    check(wasShown(ran, "42"), "running it prints what the program prints");

    // shc names the line and no column, so the caret lands at the start of it.
    file::path bad = dir / "src" / "bad.shl";
    writeFile(bad,
              "fun <> = main() {\n"
              "  ? x\n"
              "}\n");
    std::string broken = "\"" + bad.string() + "\" --project \"" + dir.string() +
                         "\" --shalimar \"" + shc + "\"";
    Screen refusedIt = drive(ride, broken, ctrl('b') + ctrl('q'), dir);
    check(onScreen(refusedIt, "Undefined variable"), "a build that fails says why");
    check(onScreen(refusedIt, "2/3"), "and the caret lands on the line shc named");

    // The Language menu, which is what makes a file whose name says nothing
    // still a Shalimar file. Language is the seventh column and Shalimar the
    // fourth item in it.
    file::path anonymous = dir / "src" / "notes.txt";
    writeFile(anonymous, "fun <> = main() {\n  ? 1\n}\n");
    Screen asText = drive(ride, "\"" + anonymous.string() + "\" --project \"" +
                                   dir.string() + "\" --shalimar \"" + shc + "\"",
                          ctrl('q'), dir);
    check(onScreen(asText, "text"), "a .txt opens as plain text");

    Screen asShalimar = drive(ride, "\"" + anonymous.string() + "\" --project \"" +
                                       dir.string() + "\" --shalimar \"" + shc + "\"",
                              // Language is the seventh column, and its first
                              // item is already selected when the menu opens -
                              // so three downs reach the fourth, not four.
                              kF10 + times(kRight, 6) + times(kDown, 3) +
                                  kEnter + ctrl('b') + ctrl('q'),
                              dir);
    check(wasShown(asShalimar, "language: Shalimar"),
          "and the Language menu says it is Shalimar after all");
    check(onScreen(asShalimar, "lines of"), "which is enough for shc to build it");

    file::remove_all(dir);
}

// A project made of Shalimar, which is not the shape of one made of C: the language has no include
// and no separate compilation, so several .shl in a group are several programs rather than the
// parts of one - and the project has to say which it builds instead of taking whichever came first.
void aShalimarProject(const std::string& ride, const std::string& shc) {
    std::printf("a project made of Shalimar\n");

    if (shc.empty()) {
        std::printf("  (no shc named, so those cases are not tried)\n");
        return;
    }

    file::path dir = freshProject("shmproject");
    writeFile(dir / "src" / "hello.shl",
              "fun <> = main() {\n  ? 6 * 7\n}\n");
    writeFile(dir / "project.pro",
              "{\n"
              "  \"name\": \"hello\",\n"
              "  \"build\": { \"target\": \"hello\", \"groups\": [\"Sources\"] },\n"
              "  \"groups\": { \"Sources\": [\"src/hello.shl\"] }\n"
              "}\n");

    std::string arguments = "--project \"" + dir.string() + "\" --shalimar \"" + shc + "\"";
    Screen built = drive(ride, arguments, kF4 + ctrl('q'), dir);
    check(onScreen(built, "built hello"), "F4 builds the project's one program");
    check(file::exists(dir / "hello") || file::exists(dir / "hello.exe"),
          "and leaves it beside the project, where it can be found again");

    // Run project: F10, three columns right to Build, three items down.
    Screen ran = drive(ride, arguments,
                       kF10 + times(kRight, 3) + times(kDown, 3) + kEnter + ctrl('q'), dir);
    check(onScreen(ran, "42"), "running the project runs what came out");

    // A second program in the group, and the target names the first.
    writeFile(dir / "src" / "other.shl", "fun <> = main() {\n  ? 1\n}\n");
    writeFile(dir / "project.pro",
              "{\n"
              "  \"name\": \"hello\",\n"
              "  \"build\": { \"target\": \"hello\", \"groups\": [\"Sources\"] },\n"
              "  \"groups\": { \"Sources\": [\"src/other.shl\", \"src/hello.shl\"] }\n"
              "}\n");
    Screen chose = drive(ride, arguments, kF4 + ctrl('q'), dir);
    check(!wasShown(chose, "programs and builds one"),
          "a target named after one of them builds that one");

    // And a target named after none of them is refused rather than guessed.
    writeFile(dir / "project.pro",
              "{\n"
              "  \"name\": \"hello\",\n"
              "  \"build\": { \"target\": \"neither\", \"groups\": [\"Sources\"] },\n"
              "  \"groups\": { \"Sources\": [\"src/other.shl\", \"src/hello.shl\"] }\n"
              "}\n");
    Screen refusedIt = drive(ride, arguments, kF4 + ctrl('q'), dir);
    check(wasShown(refusedIt, "programs and builds one"),
          "a target named after none of them is refused, not guessed at");

    // Two files, one program: Shalimar has no include, so shc looks for what the program calls in the other files it was given, and the project says which those are.
    writeFile(dir / "src" / "shapes.shl",
              // `uses abs` belongs to THIS file, not to the one that calls area(). That is what
              // per-file borrowing buys: a file pulled into somebody else's program brings what it
              // needs with it, and the caller does not have to know what it reaches for.
              "uses abs\n"
              "\n"
              "real tolerance : 1e-9\n"
              "\n"
              "fun <real> = area(w: real, h: real) {\n"
              "  return w * h\n"
              "}\n"
              "\n"
              "fun <int> = nearly(a: real, b: real) {\n"
              "  if abs(a - b) < tolerance {\n"
              "    return 1\n"
              "  }\n"
              "  return 0\n"
              "}\n");
    writeFile(dir / "src" / "hello.shl",
              "fun <> = main() {\n"
              "  ? area(6.0, 7.0)\n"
              "  ? nearly(0.1 + 0.2, 0.3)\n"
              "}\n");
    writeFile(dir / "project.pro",
              "{\n"
              "  \"name\": \"hello\",\n"
              "  \"build\": { \"target\": \"hello\", \"groups\": [\"Sources\"] },\n"
              "  \"groups\": { \"Sources\": [\"src/shapes.shl\", \"src/hello.shl\"] }\n"
              "}\n");
    Screen two = drive(ride, arguments,
                       kF10 + times(kRight, 3) + times(kDown, 3) + kEnter + ctrl('q'), dir);
    check(wasShown(two, "42.0000000"),
          "a program calling a function in another file of the project builds and runs");
    check(wasShown(two, "also compiled shapes.shl"),
          "and the compiler says which file it went to, so nothing is silent");

    // Shalimar beside C is still refused - the one refusal that did not go away when a group got
    // its own compiler, since it is not about the editor. Refused twice over, saying different
    // things. In one group: no compiler takes both, so naming one cannot help.
    writeFile(dir / "src" / "bit.c", "int bit(void) { return 1; }\n");
    writeFile(dir / "project.pro",
              "{\n"
              "  \"name\": \"hello\",\n"
              "  \"build\": { \"target\": \"hello\", \"groups\": [\"Sources\"] },\n"
              "  \"groups\": { \"Sources\": [\"src/hello.shl\", \"src/bit.c\"] }\n"
              "}\n");
    Screen together = drive(ride, arguments, kF4 + ctrl('q'), dir);
    check(wasShown(together, "Shalimar and C or C++ in one group"),
          "Shalimar and C in one group is refused, naming the group");

    // In two groups, where every other pair of languages works: this is about what a Shalimar
    // object is. Whichever file it came from it exports the same three startup symbols, so two
    // collide - and the language has no declarations to check a call across a link. Compiler-S/docs/LINKING.md has it in full.
    writeFile(dir / "project.pro",
              "{\n"
              "  \"name\": \"hello\",\n"
              "  \"build\": { \"target\": \"hello\", \"groups\": [\"Sources\", \"C\"] },\n"
              "  \"groups\": {\n"
              "    \"Sources\": [\"src/hello.shl\"],\n"
              "    \"C\": [\"src/bit.c\"]\n"
              "  }\n"
              "}\n");
    Screen apart = drive(ride, arguments, kF4 + ctrl('q'), dir);
    check(wasShown(apart, "whole program"),
          "and in a group of its own it is refused for what a Shalimar object is");

    file::remove_all(dir);
}

// Whether a Shalimar Debug build stops itself here, read off a stop screen. Since D1 (2026-10-13)
// lldb on a Mac, gdb on Linux and cdb on a Windows box drive a Debug build directly, reading the
// DWARF (or CodeView) -g wrote; where none of the three is present - a Windows box without cdb -
// the in-process session still does, and its own words appear. The checks below take whichever
// path is live, so this runs honestly on every machine, and the session's half stays measured
// where it runs. shalimar::Session's protocol is unit-tested on its own in tests/test.cpp.
static bool shalimarStopsItself(const Screen& stop) {
    return onScreen(stop, "not what is in it");
}

// Stopping a Shalimar program from the Debug menu, a different thing from stopping a C one. On a
// machine with a host debugger the program is driven by it - variables by name, a named stack, the
// step sequence the debugger takes; where there is none the program stops itself and says only
// where it is. This runs on every machine the suite does - including Windows, where cc1's target
// cannot be debugged and every check in stoppingAndStepping is skipped.
void stoppingShalimar(const std::string& ride, const std::string& shc) {
    std::printf("stopping a Shalimar program from the editor\n");

    if (shc.empty()) {
        std::printf("  (no shc named, so those cases are not tried)\n");
        return;
    }

    file::path dir = freshProject("shmdebug");
    file::path file = dir / "src" / "steps.shl";
    writeFile(file,
              "fun <int> = twice(n: int) {\n"      // 1
              "  int d : n + n\n"                  // 2
              "  return d\n"                       // 3
              "}\n"                                // 4
              "\n"                                 // 5
              "fun <> = main() {\n"                // 6
              "  int a : 1\n"                      // 7
              "  int b : twice(a)\n"               // 8
              "  ? b\n"                            // 9
              "}\n");                              // 10

    std::string arguments = "\"" + file.string() + "\" --project \"" + dir.string() +
                            "\" --shalimar \"" + shc + "\"";

    // The caret starts on line 1; line 8 is the call.
    const std::string toTheCall = times(kDown, 7);

    Screen stopped = drive(ride, arguments, toTheCall + kF9 + kF8 + ctrl('q'), dir);
    check(onScreen(stopped, "stopped at steps.shl:8"),
          "F8 runs it and it stops on the line");
    check(onScreen(stopped, "> 8"), "the gutter marks where it is standing");

    const bool bySession = shalimarStopsItself(stopped);

    // What stands in the variables pane. A host debugger reads the DWARF -g wrote and shows the
    // locals by name and type; the session cannot, and says why rather than showing an empty list
    // that would read as "this line has none" rather than "there are none to have".
    if (bySession)
        check(onScreen(stopped, "not what is in it"),
              "the session says why it shows no variables, rather than showing none");
    else
        check(onScreen(stopped, "a = 1") && onScreen(stopped, "[int]"),
              "the debugger shows the variables in scope by name and type");

    Screen inside = drive(ride, arguments, toTheCall + kF9 + kF8 + kF6 + ctrl('q'), dir);
    check(onScreen(inside, "steps.shl:2"), "F6 steps into the call");

    // Stepping over the call's body and its return. The session steps out to the statement after
    // the call, line 9; a host debugger returns control to the caller, so it lands back in main
    // on the call's own line (line 8) and a further step reaches line 9. Either way it is back in
    // main past the body of twice, which is what this measures.
    Screen back = drive(ride, arguments, toTheCall + kF9 + kF8 + kF6 + kF7 + kF7 + ctrl('q'), dir);
    if (bySession)
        check(onScreen(back, "steps.shl:9"), "and stepping on comes back past the call, to line 9");
    else
        check(onScreen(back, "in main"), "and stepping over the call's body comes back into main");

    // The program's own printing reaches the console, which is the point of the channel keeping the
    // two streams apart: a #stop in the middle of a half-written line would have been unreadable
    // and would have changed what the program appeared to print.
    Screen printed = drive(ride, arguments,
                           toTheCall + kF9 + kF8 + kF8 + ctrl('q'), dir);
    check(wasShown(printed, "returned"), "carrying on to the end says so");

    // What a release build says when F8 has nothing to stop. Driven by a host debugger it is
    // "built without -g", like the other three toolchains; left to stop itself it links a runtime
    // with no debugger in it and says so, which shc has never had to say any other way. Ctrl-D is
    // a toggle and debug is where a project starts, so one press is release.
    Screen release = drive(ride, arguments, ctrl('d') + toTheCall + kF9 + kF8 + ctrl('q'), dir);
    if (bySession)
        check(wasShown(release, "no debugger in it"),
              "and a release build that stops itself says what it has not got");
    else
        check(wasShown(release, "built without -g"),
              "and a release build says it was built without -g, as for the other toolchains");

    file::remove_all(dir);
}

// A compiler per group, and one link at the end - the shape that used to be refused: a target
// whose groups do not all go to one compiler. Each compiles to objects with its own and the editor
// names the linker, because no compiler here takes an object - hand cc1 a .o and it reads it as C and complains about a stray byte on line 1.
void aCompilerPerGroup(const std::string& ride, const std::string& cc1,
                       const std::string& cxx1) {
    std::printf("a compiler per group, and one link\n");

    // The machine's real C++ compiler, by name. Written out rather than asked of the editor: this
    // harness links src/path.cpp and nothing else on purpose - a test that shares the editor's
    // opinion cannot catch the editor being wrong about it.
#if defined(_WIN32)
    const char* cpp = "cl";
#elif defined(__APPLE__)
    const char* cpp = "clang++";
#else
    const char* cpp = "g++";
#endif

    if (cc1.empty()) {
        std::printf("  (no cc1 named, so those cases are not tried)\n");
        return;
    }

    file::path dir = freshProject("per-group");
    file::create_directories(dir / "lib");
    writeFile(dir / "src" / "main.c",
              "#include <stdio.h>\n\nint helper(int n);\n\n"
              "int main(void)\n{\n    printf(\"helper %d\\n\", helper(6));\n"
              "    return 0;\n}\n");
    writeFile(dir / "lib" / "helper.c", "int helper(int n) { return n * 7; }\n");

    // Two groups, and the second names its compiler by hand. Both go to cc1 here, which is the
    // point: it is two *parts* rather than two languages, so the object-and-link path is what runs,
    // on a machine where the whole of it can be checked.
    writeFile(dir / "project.pro",
              "{\n  \"name\": \"two\",\n  \"indent\": 4,\n"
              "  \"groups\": {\n"
              "    \"Sources\": [\"src/main.c\"],\n"
              "    \"Library\": { \"files\": [\"lib/helper.c\"], \"toolchain\": \"c90\" }\n"
              "  },\n"
              "  \"build\": { \"target\": \"two\", \"groups\": [\"Sources\", \"Library\"] }\n}\n");

    std::string arguments = "--project \"" + dir.string() + "\" --c90 \"" + cc1 + "\"";

    Screen built = drive(ride, arguments, kF4 + ctrl('q'), dir);
    // wasShown, not onScreen: each compile opens with the compiler's banner (-nologo is not passed
    // for a project build), so the nine-row console has scrolled past the first group's header by
    // the end - the mixed target below reads its headers the same way. "built two" is the last line and still on screen.
    check(wasShown(built, "Sources (c90)"), "each group is compiled under its own name");
    check(wasShown(built, "Library (c90)"), "including the one that named its compiler");
    check(wasShown(built, "linking with"), "and the editor says what it linked with");
    check(onScreen(built, "built two"), "the program comes out");
    check(file::exists(dir / "two") || file::exists(dir / "two.exe"),
          "and is beside the project where it can be found again");

    // The objects are the editor's mess and go with it. A directory of stale
    // objects is how a later build comes to link something nobody compiled.
    check(!file::exists(dir / "main.o") && !file::exists(dir / "helper.o"),
          "and no objects are left lying about the project");

    Screen ran = drive(ride, arguments,
                       kF10 + times(kRight, 3) + times(kDown, 3) + kEnter + ctrl('q'), dir);
    check(wasShown(ran, "helper 42"), "running it runs what the two groups made together");

    // Three groups and three routings, the shape the whole thing was for. C++ names nothing and
    // goes to cxx1; the group that names a compiler is C wanting the host's C++ compiler. Since 3.0
    // C and C++ have the same shape - ours by default, the machine's by name - and cxx1 has to be here for the C++ group to build.
    if (cxx1.empty()) {
        std::printf("  (no cxx1 named, so the three-routing project is not built)\n");
    } else {
        file::path three = freshProject("three-routings");
        file::create_directories(three / "engine");
        writeFile(three / "src" / "main.c",
                  "#include <stdio.h>\n\nint spin(int n);\nint legacy(int n);\n\n"
                  "int main(void)\n{\n    printf(\"total %d\\n\", spin(3) + legacy(4));\n"
                  "    return 0;\n}\n");
        writeFile(three / "src" / "legacy.c", "int legacy(int n) { return n * 10; }\n");
        // std::vector on purpose: if the C++ runtime did not reach the link, this says so at link
        // time. <cstddef> and std::size_t spelled out: Apple's libc++ drags size_t in through
        // <vector> and libstdc++ does not, so the bare name stopped the Linux box on the day it was added.
        writeFile(three / "engine" / "engine.cpp",
                  "#include <cstddef>\n#include <vector>\n\n"
                  "extern \"C\" int spin(int n)\n{\n"
                  "    std::vector<int> v;\n"
                  "    for (int i = 0; i < n; ++i) v.push_back(i * i);\n"
                  "    int total = 0;\n"
                  "    for (std::size_t i = 0; i < v.size(); ++i) total += v[i];\n"
                  "    return total;\n}\n");
        writeFile(three / "project.pro",
                  "{\n  \"name\": \"three\",\n  \"indent\": 4,\n"
                  "  \"groups\": {\n"
                  "    \"Sources\": [\"src/main.c\"],\n"
                  "    \"Legacy\": { \"files\": [\"src/legacy.c\"], \"toolchain\": \"c++\" },\n"
                  "    \"Engine\": [\"engine/engine.cpp\"]\n  },\n"
                  "  \"build\": { \"target\": \"three\", "
                  "\"groups\": [\"Sources\", \"Legacy\", \"Engine\"] }\n}\n");

        std::string theirs = "--project \"" + three.string() + "\" --c90 \"" + cc1 +
                             "\" --cpp11 \"" + cxx1 + "\"";
        Screen made = drive(ride, theirs, kF4 + ctrl('q'), three);
        check(wasShown(made, "Sources (c90)"), "a C group that says nothing goes to cc1");
        check(wasShown(made, std::string("Legacy (") + cpp + ")"),
              "a C group that names the host's C++ compiler goes there instead");
        check(wasShown(made, "Engine (cpp11)"),
              "and a C++ group that names nothing goes to cxx1");
        check(onScreen(made, "built three"), "all three link into one program");

        Screen went = drive(ride, theirs,
                            kF10 + times(kRight, 3) + times(kDown, 3) + kEnter + ctrl('q'),
                            three);
        check(wasShown(went, "total 45"),
              "which runs - and the std::vector in it says the C++ runtime reached the link");
        file::remove_all(three);
    }

    // An error in the second group lands in the second group's file. It used
    // to be looked for among the target's sources starting at the first, which
    // is right when there is one command and wrong the moment there are two.
    writeFile(dir / "lib" / "helper.c", "int helper(int n) { return n * ; }\n");
    Screen broken = drive(ride, arguments, kF4 + ctrl('q'), dir);
    check(onScreen(broken, "error"), "an error in the second group is reported");
    check(onScreen(broken, "helper.c"), "naming the file it is actually in");

    file::remove_all(dir);
}

// Which item you are already on, marked in the menu that offers it. The status bar carries some of
// this - cc1* means the language chose it - but a .c reads "C" whether from its name or from
// Language > C by hand, and nowhere else says which. A menu of five compilers that does not say which you are on sends you across the screen to find out.
void theMenuSaysWhereYouAre(const std::string& ride) {
    std::printf("the menu marks what you are already on\n");

    file::path dir = freshProject("menu-marks");
    writeFile(dir / "src" / "one.c", "int main(void) { return 0; }\n");
    std::string arguments = "\"" + (dir / "src" / "one.c").string() +
                            "\" --project \"" + dir.string() + "\"";

    // Tools is the seventh column. Nothing has been overridden, so "By
    // language" is the one marked.
    const std::string toTools = kF10 + times(kRight, 7);
    Screen fresh = drive(ride, arguments, toTools + ctrl('q'), dir);
    check(onScreen(fresh, "\xe2\x80\xa2 By language"), "the compiler nobody chose is marked");
    check(onScreen(fresh, "  c90"), "and the ones nobody is on are not");

    // Choose cc1 - one down from By language - and the mark moves with it. The second F10 is bare:
    // a menu reopens on the column it was left on, so walking right again from Tools lands
    // somewhere else - the hazard this suite has been caught by more than once.
    Screen chose = drive(ride, arguments, toTools + kDown + kEnter + toTools + ctrl('q'), dir);
    check(onScreen(chose, "\xe2\x80\xa2 c90"), "choosing one marks it");
    check(!onScreen(chose, "\xe2\x80\xa2 By language"), "and unmarks what it replaced");

    // The Language menu is the sixth column, and this is the case the status
    // bar cannot show: the file is C either way, and only the mark says
    // whether that was its name or a choice.
    const std::string toLanguage = kF10 + times(kRight, 6);
    Screen byName = drive(ride, arguments, toLanguage + ctrl('q'), dir);
    check(onScreen(byName, "\xe2\x80\xa2 By extension"), "a language nobody chose is marked too");

    Screen byHand = drive(ride, arguments, toLanguage + kDown + kEnter + toLanguage + ctrl('q'), dir);
    check(onScreen(byHand, "\xe2\x80\xa2 C"), "and choosing C marks C");
    check(!onScreen(byHand, "\xe2\x80\xa2 By extension"),
          "which the status bar cannot tell you - it says C either way");

    // Debug and release are a state as much as a command, and are marked on
    // the same grounds. Build is the fourth column, Debug its fifth item.
    Screen release = drive(ride, arguments, ctrl('d') + kF10 + times(kRight, 3) + ctrl('q'), dir);
    check(onScreen(release, "\xe2\x80\xa2 Release"), "release is marked once you are in it");

    file::remove_all(dir);
}

// The Debug menu, grouped - and what is offered while a Shalimar program is stopped. A host
// debugger can walk the stack and watch an expression, so those items are live; where the program
// stops itself it cannot, and the three that need a stack or a variable are drawn faint.
void theDebugMenuGroups(const std::string& ride, const std::string& shc) {
    std::printf("the Debug menu's rules, and what Shalimar can and cannot do\n");

    file::path dir = freshProject("debug-menu");
    file::path file = dir / "src" / "steps.shl";
    writeFile(file,
              "fun <int> = twice(n: int) {\n  int d : n + n\n  return d\n}\n\n"
              "fun <> = main() {\n  int a : 1\n  int b : twice(a)\n  ? b\n}\n");
    std::string arguments = "\"" + file.string() + "\" --project \"" + dir.string() + "\"";
    if (!shc.empty()) arguments += " --shalimar \"" + shc + "\"";

    // Debug is the fifth column. The rules are there whatever is running.
    const std::string toDebug = kF10 + times(kRight, 4);
    Screen grouped = drive(ride, arguments, toDebug + ctrl('q'), dir);
    check(onScreen(grouped, "Start / continue"), "the Debug menu opens");
    // A rule joins the sides of the box, so its ends are the tee characters the panel's own rules use - how it is told from a plain row.
    check(onScreen(grouped, "\xe2\x94\x9c"), "and is grouped by a rule across it");

    // Down from Start / continue reaches Debug project and then, stepping over
    // the rule, Toggle breakpoint. If rules could be landed on, two downs
    // would stop on one.
    Screen stepped = drive(ride, arguments, toDebug + times(kDown, 2) + kEnter + ctrl('q'), dir);
    check(!wasShown(stepped, "nothing is running"),
          "and down steps over the rule rather than landing on it");

    if (shc.empty()) {
        std::printf("  (no shc named, so the greying is not tried)\n");
        file::remove_all(dir);
        return;
    }

    // With a Shalimar program stopped. The items are listed either way; whether the three that
    // need a stack or a variable are offered depends on whether a host debugger drives it. Line 8
    // is the call.
    const std::string stop = times(kDown, 7) + kF9 + kF8;
    Screen stopped = drive(ride, arguments, stop + ctrl('q'), dir);
    const bool bySession = shalimarStopsItself(stopped);

    Screen running = drive(ride, arguments, stop + toDebug + ctrl('q'), dir);
    check(onScreen(running, "Up the stack"), "the items are still listed while it is stopped");
    check(onScreen(running, "Watch expression"), "including the watch");

    // Greyed is drawn faint, which is the one escape that says so. A host debugger offers all
    // three, so nothing in the menu is faint; the session cannot, so they are.
    if (bySession)
        check(wasShown(running, "\x1b[2m"),
              "and what a self-stopping Shalimar program cannot do is drawn faint rather than offered");
    else
        check(!wasShown(running, "\x1b[2m"),
              "and under a host debugger the stack and watch are offered rather than drawn faint");

    file::remove_all(dir);
}

// Help, which is the one menu whose whole job is to be readable.
void theHelpMenu(const std::string& ride) {
    std::printf("the manual, from the Help menu\n");

    file::path dir = freshProject("help-menu");
    writeFile(dir / "src" / "one.c", "int main(void) { return 0; }\n");
    std::string arguments = "\"" + (dir / "src" / "one.c").string() +
                            "\" --project \"" + dir.string() + "\"";

    // Help is the ninth column and Contents its first item, which is already
    // selected when the menu opens - so no downs.
    const std::string toContents = kF10 + times(kRight, 9) + kEnter;
    Screen shown = drive(ride, arguments, toContents + ctrl('q'), dir);
    check(onScreen(shown, "the manual"), "Help > Contents shows the manual's contents");
    check(onScreen(shown, "What it is"), "with the first page in it");
    check(onScreen(shown, "three languages"), "and a line saying what that page is about");
    check(wasShown(shown, "help/"), "and says where the pages themselves are");

    // The version is not written out twice: help::contents() asks about::version() for it. A
    // contents and an About that disagreed about which version this is would be the sort of thing
    // nobody notices for a year.
    Screen about = drive(ride, arguments, kF10 + times(kRight, 9) + times(kDown, 2) + kEnter +
                                             ctrl('q'), dir);
    check(onScreen(about, "RIDE"), "Help > About still names the product");

    // F1 is the keys and is not the contents. The keys run past the seven rows the panel shows and
    // it opens at the top, so what is checked has to be near the top; wasShown does not help, since
    // the editor writes only the rows the panel shows and a line below the fold never reaches the terminal at all.
    Screen keys = drive(ride, arguments, kF1 + ctrl('q'), dir);
    check(onScreen(keys, "these keys"), "F1 shows the keys");
    check(!onScreen(keys, "the manual"), "which is a different screen from the contents");

    file::remove_all(dir);
}


// The audit of 2026-09-19, and what it mended: a Shalimar file made in the editor builds (F9);
// Rename, Delete and Move to group are on the Project menu (F8); the Target and Tools choices are
// the project's while one is open and reach its .pro (F1, F2); Ctrl-Q leaves as File > Quit does, the front file remembered (F10).
void theAuditMends(const std::string& ride, const std::string& shc) {
    std::printf("the audit's mends: Shalimar in the build, the three file operations, "
                "the project's target and compiler\n");
    file::path dir = file::temp_directory_path() / "ride-session-audit";
    file::remove_all(dir);
    file::create_directories(dir);
    const std::string toProject = kF10 + "p";

    // F9: New project, New File prog.shl, a main, F4 - built, not "no source".
    std::string keys = toProject + kEnter + "Proj" + kEnter + kEnter;   // New: the name, then here
    keys += toProject + times(kDown, 4) + kEnter + "prog.shl" + kEnter;
    keys += "fun <> = main()\n{\n}\n" + ctrl('s');
    if (!shc.empty()) keys += kF4;
    Screen made = driveIn(ride, "", keys + ctrl('q') + ctrl('q'), dir, dir);
    std::string pro = readFile(dir / "Proj.pro");
    check(pro.find("\"Sources\"") != std::string::npos && pro.find("prog.shl") != std::string::npos &&
          pro.find("\"Shalimar\"") == std::string::npos,
          "a .shl made in the editor goes to Sources, the group the project builds");
    if (!shc.empty())
        check(wasShown(made, "built Proj"), "and F4 builds it");
    check(wasShown(made, "Proj.pro written"), "and Project > New names the file it wrote");

    // F1, F2: Ctrl-T and Tools > cxx1 with the project open reach the .pro.
    std::string hostArch;
    {
        size_t at = pro.find("\"arch\": \"");
        if (at != std::string::npos) { at += 9; hostArch = pro.substr(at, pro.find('"', at) - at); }
    }
    keys = ctrl('t') + kF10 + "t" + times(kDown, 2) + kEnter + ctrl('q') + ctrl('q');
    Screen chosen = driveIn(ride, "prog.shl", keys, dir, dir);
    pro = readFile(dir / "Proj.pro");
    check(wasShown(chosen, "written to Proj.pro"), "the target and the compiler say they were written");
    check(pro.find("\"toolchain\": \"cpp11\"") != std::string::npos, "Tools > cxx1 is the project's compiler now");
    // Ctrl-T steps the target on from the host's; a new project wrote the
    // host's, so the line has changed.
    check(pro.find("\"arch\": \"") != std::string::npos && pro.find("\"arch\": \"" + hostArch + "\"") == std::string::npos,
          "and Ctrl-T moved the project's target off the host's");
    // F10: the file in front is remembered on Ctrl-Q, as on File > Quit.
    check(pro.find("\"open\": \"prog.shl\"") != std::string::npos, "Ctrl-Q remembers the file in front");

    // F8: Rename, Delete and Move to group, from the Project menu.
    keys = toProject + times(kDown, 7) + kEnter + "main.shl" + kEnter + ctrl('q') + ctrl('q');
    Screen renamed = driveIn(ride, "prog.shl", keys, dir, dir);
    check(file::exists(dir / "main.shl") && !file::exists(dir / "prog.shl"), "Rename File renames it on disk");
    check(readFile(dir / "Proj.pro").find("main.shl") != std::string::npos, "and in the project");
    keys = toProject + times(kDown, 9) + kEnter + "Programs" + kEnter + ctrl('q') + ctrl('q');
    Screen moved = driveIn(ride, "main.shl", keys, dir, dir);
    check(readFile(dir / "Proj.pro").find("\"Programs\"") != std::string::npos, "Move to Group makes the group and puts it there");
    keys = toProject + times(kDown, 8) + kEnter + "yes" + kEnter + ctrl('q') + ctrl('q');
    Screen deleted = driveIn(ride, "main.shl", keys, dir, dir);
    check(!file::exists(dir / "main.shl"), "Delete File, after 'yes', deletes it");
    check(readFile(dir / "Proj.pro").find("main.shl") == std::string::npos, "and takes it out of the project");
    (void)renamed; (void)moved; (void)deleted;

    file::remove_all(dir);
}

int main(int argc, char** argv) {
#ifdef _WIN32
    std::string ride = "RIDEConsole.exe";
#else
    std::string ride = "./RIDE.exe";
#endif
    std::string cc1;
    std::string shc;

    if (argc > 1) ride = argv[1];
    // Named in the environment rather than positionally: an empty CC1 on a make line collapses,
    // and the compiler after it then arrives as the one before - which reads as fifty debugger
    // failures and is nothing of the kind.
    if (argc > 2) cc1 = argv[2];
    if (cc1.empty()) {
        const char* fromEnv = std::getenv("CC1");
        if (fromEnv) cc1 = fromEnv;
    }
    if (shc.empty()) {
        const char* fromEnv = std::getenv("SHC");
        if (fromEnv) shc = fromEnv;
    }
    std::string c2s;
    {
        const char* fromEnv = std::getenv("C2S");
        if (fromEnv) c2s = fromEnv;
    }
    std::string cxx1;
    {
        const char* fromEnv = std::getenv("CXX1");
        if (fromEnv) cxx1 = fromEnv;
    }
    // RIDE is handed the same compilers under its own names - $C90, $CPP11 and
    // $SHALIMAR - which it inherits from here; the harness keeps CC1, CXX1 and
    // SHC, the names every sibling suite is run with.
    {
        const char* names[][2] = { {"C90", cc1.c_str()}, {"CPP11", cxx1.c_str()},
                                   {"SHALIMAR", shc.c_str()} };
        for (int k = 0; k < 3; k++) {
            if (!*names[k][1]) continue;
#ifdef _WIN32
            _putenv_s(names[k][0], names[k][1]);
#else
            setenv(names[k][0], names[k][1], 1);
#endif
        }
    }
    if (!cxx1.empty() && !editor::path::exists(cxx1)) {
        std::printf("no cxx1 at %s - the cases that need one are not tried\n\n", cxx1.c_str());
        cxx1.clear();
    }
    if (!c2s.empty() && !editor::path::exists(c2s)) {
        std::printf("no c2s at %s - the cases that need one are not tried\n\n", c2s.c_str());
        c2s.clear();
    }

    // A compiler named but not there is worse than none named: every case that needs it fails, and
    // none says why. Dropped with a word, so the cases skip themselves as they do when nothing was
    // named. A path with a ~ in it is the way this happens - make does not expand one.
    if (!cc1.empty() && !editor::path::exists(cc1)) {
        std::printf("no cc1 at %s - the cases that need one are not tried\n\n", cc1.c_str());
        cc1.clear();
    }
    if (!shc.empty() && !editor::path::exists(shc)) {
        std::printf("no shc at %s - the cases that need one are not tried\n\n", shc.c_str());
        shc.clear();
    }

    std::printf("driving %s\n\n", ride.c_str());

    editingAndLayout(ride);
    aDirectoryWithNoProject(ride);
    colouring(ride);
    addAndRemoveFile(ride);
    projectPane(ride);
    whichProjectAFileBelongsTo(ride);
    thePaneDrawsOneOfTwoThings(ride);
    aProjectFileOpenedTwoWays(ride);
    thePicker(ride);
    pickingAProject(ride);
    pickingACcsWorkspaceProject(ride);
    theMouse(ride);
    closingTheProject(ride);
    findingAndReplacing(ride);
    leavingWithChanges(ride);
    reindenting(ride);
    undoing(ride);
    selectingAndPasting(ride);
    multiByteText(ride);
    compiling(ride, cc1);
    compilingCpp(ride, cxx1);
    buildingTheProject(ride, cc1, cxx1);
    emulatedTarget(ride, cc1, cxx1);
    emulatedShalimar(ride, shc);
    buildingWithCl(ride);
    configurations(ride, cc1);
    debugPanelPerTarget(ride);
    runningTheProgram(ride, cc1);
    stoppingAndStepping(ride, cc1);
    compilingShalimar(ride, shc);
    convertingFromTheMenu(ride, c2s);
    aShalimarProject(ride, shc);
    stoppingShalimar(ride, shc);
    aCompilerPerGroup(ride, cc1, cxx1);
    theHelpMenu(ride);
    theMenuSaysWhereYouAre(ride);
    theDebugMenuGroups(ride, shc);
    theAuditMends(ride, shc);

    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
