#include "project.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "json.h"
#include "path.h"
#include "settings.h"
#include "ccs/ccsworkspace.h"
#include "ccs/ccsxml.h"

namespace editor {

namespace {

std::string withSlashes(const std::string& text) { return path::withSlashes(text); }
bool endsWithNoCase(const std::string& text, const std::string& tail);


const char* languageWord(Language lang) {
    if (lang == LangC) return "C";
    if (lang == LangCpp) return "C++";
    if (lang == LangShalimar) return "Shalimar";
    return "text";
}

}

Project::Project()
    : loaded_(false), toolchain_(ToolAuto),
      arch_(hostArch()) {
    indent_.width = settings::indentWidth();
    indent_.tabs = settings::indentTabs();
}


static bool namedPro(const std::string& name) {
    const std::string suffix = Project::suffix();
    if (name.size() <= suffix.size()) return false;

    size_t at = name.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        char a = name[at + i], b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

const char* Project::suffix() { return ".pro"; }

std::vector<std::string> Project::projectFilesIn(const std::string& directory) {
    std::vector<std::string> found;
    std::string base = path::absolute(directory);

    std::vector<path::Entry> here = path::entries(base);
    for (size_t i = 0; i < here.size(); ++i) {
        if (here[i].directory || !namedPro(here[i].name)) continue;
        found.push_back(base + "/" + here[i].name);
    }

    std::sort(found.begin(), found.end());
    return found;
}

std::string Project::fileIn(const std::string& directory) {
    std::vector<std::string> named = projectFilesIn(directory);
    if (!named.empty()) return named[0];
    return std::string();
}

std::string Project::absolute(const std::string& rel) const {
    if (root_.empty()) return rel;
    // A CCS project's linked file is named absolutely, and joins nothing.
    if (rel[0] == '/' || (rel.size() > 1 && rel[1] == ':')) return rel;
    return root_ + "/" + rel;
}

std::string Project::relative(const std::string& file) const {
    std::string out = path::relativeTo(file, root_);
    // A file outside the root - a CCS project's linked source, its program in the temporary
    // directory - is named whole rather than by a chain of "..", which is how its group holds it.
    if (out.empty() || out.compare(0, 2, "..") == 0) return withSlashes(file);
    return out;
}

void Project::begin(const std::string& dir, const std::string& name) {
    root_ = path::absolute(dir);
    file_ = root_ + "/" + name + suffix();
    name_ = name;
    groups_.clear();
    includes_.clear();
    libraries_.clear();
    open_.clear();
    emulateOnly_ = false;
    options_ = options::Store();
    options::setActive(&options_);
    ccs_ = false;
    ccsWorkspace_.clear();
    gride_ = false;
    indentSaid_ = false;
    indent_.width = settings::indentWidth();
    indent_.tabs = settings::indentTabs();

    Group all;
    all.name = "Sources";
    groups_.push_back(all);

    // A new project builds what it holds: its one group, into a program of
    // its own name. Without this F4 refused every project made here and
    // sent its maker to write the entry by hand.
    target_ = Target();
    target_.name = name;
    target_.groups.push_back(all.name);

    loaded_ = true;
}

namespace {

// The line with its comments taken out - a block comment carried across
// lines by `inComment` - so that a `main(` inside one does not count.
std::string uncommented(const std::string& line, Language lang, bool& inComment) {
    std::string out;
    for (size_t i = 0; i < line.size(); ++i) {
        if (inComment) {
            if (line.compare(i, 2, "*/") == 0) { inComment = false; ++i; }
            continue;
        }
        if (lang != LangShalimar && line.compare(i, 2, "/*") == 0) { inComment = true; ++i; continue; }
        if (lang != LangShalimar && line.compare(i, 2, "//") == 0) break;
        out += line[i];
    }
    return out;
}

// Whether a line defines main: C and C++ spell it `main(` and not as a
// declaration ending in ';'; Shalimar as `fun <> = main()`.
bool definesMain(const std::string& line, Language lang) {
    size_t at = line.find("main");
    if (at == std::string::npos) return false;
    if (lang == LangShalimar) return line.find("fun") != std::string::npos && line.find("main()", at) == at;

    if (at > 0 && (std::isalnum(static_cast<unsigned char>(line[at - 1])) || line[at - 1] == '_')) return false;
    size_t paren = line.find_first_not_of(" \t", at + 4);
    if (paren == std::string::npos || line[paren] != '(') return false;
    size_t last = line.find_last_not_of(" \t\r");
    return last == std::string::npos || line[last] != ';';
}

}

std::string Project::mainFile() const {
    for (size_t i = 0; i < groups_.size(); ++i)
        for (size_t j = 0; j < groups_[i].files.size(); ++j) {
            const std::string& relative = groups_[i].files[j];
            Language lang = sourceLanguageFor(relative);
            if (lang == LangPlain) continue;

            FILE* in = std::fopen(absolute(relative).c_str(), "rb");
            if (!in) continue;
            char line[1024];
            bool found = false, inComment = false;
            while (!found && std::fgets(line, sizeof line, in))
                found = definesMain(uncommented(line, lang, inComment), lang);
            std::fclose(in);
            if (found) return relative;
        }
    return std::string();
}

std::string Project::fileToOpen() const {
    if (!open_.empty() && path::exists(absolute(open_))) return open_;
    std::string main = mainFile();
    if (!main.empty()) return main;
    for (size_t i = 0; i < groups_.size(); ++i)
        for (size_t j = 0; j < groups_[i].files.size(); ++j)
            if (path::exists(absolute(groups_[i].files[j]))) return groups_[i].files[j];
    return std::string();
}

std::vector<std::string> Project::absoluteIncludes() const {
    std::vector<std::string> out;
    for (size_t i = 0; i < includes_.size(); ++i) out.push_back(path::absolute(absolute(includes_[i])));
    return out;
}

std::vector<std::string> Project::absoluteLibraries() const {
    std::vector<std::string> out;
    for (size_t i = 0; i < libraries_.size(); ++i) out.push_back(path::absolute(absolute(libraries_[i])));
    return out;
}

/*  **An argument that names a file is resolved; one that does not is left
 *  alone.** `-q` and `-run` are not paths and must reach the program as
 *  written, so only an entry that matches something on disk under the root is
 *  made absolute. The program is run from the root, so a relative path would
 *  usually work anyway - usually, because the emulated target runs the program
 *  somewhere else. */
std::vector<std::string> Project::absoluteTargetArgs() const {
    std::vector<std::string> out;
    for (size_t i = 0; i < target_.args.size(); ++i) {
        const std::string& one = target_.args[i];
        std::string full = path::absolute(absolute(one));
        out.push_back(path::exists(full) ? full : one);
    }
    return out;
}

bool Project::load(const std::string& dir, std::string& error) {
    error.clear();
    loaded_ = false;

    std::string base = path::absolute(dir);
    ccs_ = false;
    ccsWorkspace_.clear();
    gride_ = false;

    // **A CCS project folder** - or one of its three files named outright. A .pro beside them wins:
    // that is RIDE's own. One registered in the workspace it sits in opens through
    // <workspace>/<project>.pro, written if it is not there, and needs no switch; one alone needs "ccs" on.
    {
        std::string folder = path::isDirectory(base) ? base : ccs::isProjectFile(base) ? path::parent(base) : std::string();
        if (!folder.empty() && ccs::isProject(folder) && fileIn(folder).empty()) {
            std::string workspace = ccs::workspaceOf(folder);
            if (!workspace.empty()) {
                ccs::Workspace ws;
                if (!ccs::readWorkspace(workspace, ws, error)) return false;
                for (size_t i = 0; i < ws.projects.size(); ++i) {
                    if (!path::same(ws.projects[i].location, folder)) continue;
                    std::string pro;
                    if (!ccs::writePro(workspace, ws.projects[i].name, pro, error)) return false;
                    return load(pro, error);
                }
            }
            if (settings::ccsEnabled()) return loadCcs(folder, error);
        }
    }

    // **A CCS workspace is not a project**, even with .pro files in it: one of its projects is opened, through its .pro.
    if (path::isDirectory(base) && ccs::isWorkspace(base)) {
        ccs::Workspace ws;
        if (!ccs::readWorkspace(base, ws, error)) return false;
        std::string names;
        for (size_t i = 0; i < ws.projects.size(); ++i) names += (i ? ", " : "") + ws.projects[i].name;
        error = path::filename(base) + " is a CCS workspace, not a project - open one of its projects: " +
                (names.empty() ? std::string("it has none RIDE can build") : names);
        return false;
    }

    std::string path;
    if (path::isDirectory(base)) {
        path = fileIn(base);
        if (path.empty()) return false;
    } else {

        // <workspace>/<project>.pro not written yet - named on a command line, where nothing lists them:
        // the workspace has that project, so the .pro is written and read.
        if (!path::exists(base) && namedPro(path::filename(base)) && ccs::isWorkspace(path::parent(base))) {
            std::string name = path::filename(base), pro;
            name.resize(name.size() - std::string(suffix()).size());
            ccs::Workspace ws;
            if (ccs::readWorkspace(path::parent(base), ws, error) && ws.project(name)) {
                if (!ccs::writePro(ws.dir, name, pro, error)) return false;
            } else if (error.empty()) {
                error = path::filename(path::parent(base)) + " is a CCS workspace with no project " + name;
                return false;
            }
        }
        if (!path::exists(base)) return false;
        path = base;
        base = path::parent(base);
    }

    // A Visual Studio solution or C++ project: read as Visual Studio keeps it, never rewritten.
    if (endsWithNoCase(path, ".sln") || endsWithNoCase(path, ".vcxproj")) return loadVs(path, error);

    FILE* in = std::fopen(path.c_str(), "rb");
    if (!in) return false;

    std::string text;
    char chunk[4096];
    size_t got;
    while ((got = std::fread(chunk, 1, sizeof chunk, in)) > 0) text.append(chunk, got);
    std::fclose(in);

    std::string why;
    Json root = Json::parse(text, why);
    if (!why.empty()) {
        error = path::filename(path) + ": " + why;
        return false;
    }
    if (!root.is(Json::Object)) {
        error = path::filename(path) + ": the file should hold one object";
        return false;
    }

    // One project of a CCS workspace: the .pro names it, and the project is read as CCS keeps it.
    if (root.get("ccs").is(Json::Object)) return loadWorkspacePro(path, root.get("ccs"), error);

    // GRIDEd's and GRIDE 2.0's own .pro: read as they keep it, never rewritten.
    if (root.has("gride") || root.has("folders") || root.has("formatVersion") || root.has("sourceFiles"))
        return loadGride(root, path, base, error);

    root_ = base;
    file_ = path;
    name_ = root.get("name").text(path::filename(base));
    toolchain_ = toolchainFrom(root.get("toolchain").text("auto"));

    // **Another machine's host target means this machine's.** A project moves between the hosts
    // - CXX1Lab.pro, written on a Mac, opened on the Windows box - and `arm64-darwin` there only
    // reaches -S. The emulated target is bound to no machine and stays; the file is not rewritten until a target is chosen.
    arch_ = root.get("arch").text(hostArch());
    if (isHostArch(arch_) && arch_ != hostArch()) arch_ = hostArch();

    indentSaid_ = root.has("indent") || root.has("tabs");
    indent_.width = static_cast<size_t>(root.get("indent").integer(static_cast<long>(settings::indentWidth())));
    if (indent_.width < 1 || indent_.width > 16) indent_.width = 4;
    indent_.tabs = root.get("tabs").boolean(settings::indentTabs());

    groups_.clear();
    const Json& groups = root.get("groups");
    for (size_t i = 0; i < groups.size(); ++i) {
        Group group;
        group.name = groups.keyAt(i);

        const Json& entry = groups.valueAt(i);
        const Json& files = entry.is(Json::Object) ? entry.get("files") : entry;
        if (entry.is(Json::Object))
            group.toolchain = toolchainFrom(entry.get("toolchain").text("auto"));
        for (size_t j = 0; j < files.size(); ++j) {
            std::string relative = withSlashes(files.at(j).text());
            if (relative.empty()) continue;

            std::string reason;
            if (!allows(relative, reason)) {
                if (error.empty()) error = relative + ": " + reason;
                continue;
            }
            group.files.push_back(relative);
        }
        groups_.push_back(group);
    }
    if (groups_.empty()) {
        Group all;
        all.name = "Sources";
        groups_.push_back(all);
    }

    includes_.clear();
    const Json& includes = root.get("include");
    for (size_t i = 0; i < includes.size(); ++i) {
        std::string named = withSlashes(includes.at(i).text());
        if (!named.empty()) includes_.push_back(named);
    }
    open_ = withSlashes(root.get("open").text(std::string()));
    emulateOnly_ = root.get("emulateOnly").boolean(false);
    options_.fromJson(root.get("options"));
    options::setActive(&options_);

    libraries_.clear();
    const Json& libraries = root.get("libraries");
    for (size_t i = 0; i < libraries.size(); ++i) {
        std::string file = withSlashes(libraries.at(i).text());
        if (!file.empty()) libraries_.push_back(file);
    }

    target_ = Target();
    const Json& built = root.get("build");
    if (built.is(Json::Object)) {
        target_.name = built.get("target").text(name_);
        const Json& from = built.get("groups");
        for (size_t i = 0; i < from.size(); ++i) {
            std::string group = from.at(i).text();
            if (!group.empty()) target_.groups.push_back(group);
        }
        const Json& args = built.get("args");
        for (size_t i = 0; i < args.size(); ++i) {
            std::string one = args.at(i).text();
            if (!one.empty()) target_.args.push_back(one);
        }
    } else if (!root.has("build")) {
        // A file with no build entry at all - every project the window wrote before 2026-09-16 -
        // builds its Sources group into a program of its own name, as a project made today does.
        // An entry that is there but empty still means "nothing", as written.
        for (size_t i = 0; i < groups_.size(); ++i)
            if (groups_[i].name == "Sources") {
                target_.name = name_;
                target_.groups.push_back(groups_[i].name);
                break;
            }
    }

    loaded_ = true;
    return true;
}

bool Project::saveAs(const std::string& file, std::string& error) {
    error.clear();
    if (!loaded_) {
        error = "there is no project to save";
        return false;
    }

    // Save As is how a GRIDE project becomes RIDE's: the copy is written in RIDE's form.
    std::string was = file_;
    bool wasGride = gride_;
    file_ = file;
    gride_ = false;
    if (save(error)) return true;

    file_ = was;
    gride_ = wasGride;
    return false;
}

bool Project::save(std::string& error) {
    error.clear();
    if (!loaded_) {
        error = "there is no project to save";
        return false;
    }
    // CCS's files are CCS's: what RIDE keeps of its own goes to settings.json, keyed by the folder.
    if (ccs_) {
        Json state = settings::ccsProjectState(root_);
        if (!state.is(Json::Object)) state = Json::object();
        state.set("open", Json::fromText(open_));
        if (!settings::rememberCcsProjectState(root_, state)) {
            error = "cannot write settings.json, where a CCS project's state is kept";
            return false;
        }
        return true;
    }
    // A GRIDE project's .pro is GRIDE's: RIDE's own state goes to settings.json, a change to its files is refused.
    if (gride_) {
        if (shape() != grideShape_) {
            error = path::filename(file_) + " is a " + foreign_ + " project: RIDE reads it but does not rewrite it - "
                    "Project > Save As makes a RIDE project of it, which can be changed";
            return false;
        }
        Json state = settings::ccsProjectState(root_);
        if (!state.is(Json::Object)) state = Json::object();
        state.set("open", Json::fromText(open_));
        state.set("arch", Json::fromText(arch_));
        state.set("toolchain", Json::fromText(toolchainWord(toolchain_)));
        Json list = Json::array();
        for (size_t i = 0; i < target_.args.size(); ++i) list.push(Json::fromText(target_.args[i]));
        state.set("args", list);
        if (!settings::rememberCcsProjectState(root_, state)) {
            error = "cannot write settings.json, where a " + foreign_ + " project's state is kept";
            return false;
        }
        return true;
    }

    Json root = Json::object();
    root.set("name", Json::fromText(name_));
    root.set("toolchain", Json::fromText(toolchainWord(toolchain_)));
    root.set("arch", Json::fromText(arch_));
    if (indentSaid_) {
        root.set("indent", Json::fromNumber(static_cast<double>(indent_.width)));
        root.set("tabs", Json::fromBool(indent_.tabs));
    }

    Json groups = Json::object();
    for (size_t i = 0; i < groups_.size(); ++i) {
        Json files = Json::array();
        for (size_t j = 0; j < groups_[i].files.size(); ++j)
            files.push(Json::fromText(groups_[i].files[j]));

        if (groups_[i].toolchain == ToolAuto) {
            groups.set(groups_[i].name, files);
        } else {
            Json named = Json::object();
            named.set("files", files);
            named.set("toolchain", Json::fromText(toolchainWord(groups_[i].toolchain)));
            groups.set(groups_[i].name, named);
        }
    }
    root.set("groups", groups);

    if (!includes_.empty()) {
        Json dirs = Json::array();
        for (size_t i = 0; i < includes_.size(); ++i) dirs.push(Json::fromText(includes_[i]));
        root.set("include", dirs);
    }
    if (!libraries_.empty()) {
        Json files = Json::array();
        for (size_t i = 0; i < libraries_.size(); ++i) files.push(Json::fromText(libraries_[i]));
        root.set("libraries", files);
    }
    if (!open_.empty()) root.set("open", Json::fromText(open_));
    if (emulateOnly_) root.set("emulateOnly", Json::fromBool(true));
    if (!options_.empty()) root.set("options", options_.toJson());

    if (builds()) {
        Json target = Json::object();
        target.set("target", Json::fromText(target_.name.empty() ? name_ : target_.name));
        Json from = Json::array();
        for (size_t i = 0; i < target_.groups.size(); ++i)
            from.push(Json::fromText(target_.groups[i]));
        target.set("groups", from);
        if (!target_.args.empty()) {
            Json args = Json::array();
            for (size_t i = 0; i < target_.args.size(); ++i)
                args.push(Json::fromText(target_.args[i]));
            target.set("args", args);
        }
        root.set("build", target);
    }

    std::string text = root.write() + "\n";

    FILE* out = std::fopen(file_.c_str(), "wb");
    if (!out) {
        error = "cannot write " + file_;
        return false;
    }
    size_t written = std::fwrite(text.data(), 1, text.size(), out);
    bool trouble = (written != text.size()) || std::ferror(out) != 0;
    if (std::fclose(out) != 0) trouble = true;

    if (trouble) {
        error = "cannot write " + file_;
        return false;
    }
    return true;
}

bool Project::allows(const std::string& rel, std::string& why) {
    std::string path = withSlashes(rel);
    why.clear();

    if (path.empty()) {
        why = "a file needs a name";
        return false;
    }
    if (path[0] == '/' || (path.size() > 1 && path[1] == ':')) {
        why = "that is an absolute path - files live inside the project";
        return false;
    }
    if (path.find("..") != std::string::npos) {
        why = "no going up out of the project";
        return false;
    }
    if (path[path.size() - 1] == '/') {
        why = "that is a directory, not a file";
        return false;
    }

    size_t depth = 0;
    for (size_t i = 0; i < path.size(); ++i)
        if (path[i] == '/') ++depth;

    if (depth > 1) {
        why = "two levels at most: name.c, or one directory and name.c";
        return false;
    }
    return true;
}

size_t Project::runsAsProject(const std::string& file) const {
    if (!loaded() || !builds() || file.empty()) return 0;
    std::vector<Part> parts;
    std::string why;
    if (!targetParts(parts, why)) return 0;
    size_t count = 0;
    bool mine = false;
    // Compared the way the pane names a file: relative to the root, slashes
    // one way - relative() answers the same for the file whichever way it came.
    std::string wanted = relative(path::absolute(file));
    for (size_t i = 0; i < parts.size(); ++i)
        for (size_t k = 0; k < parts[i].sources.size(); ++k) {
            ++count;
            if (relative(parts[i].sources[k]) == wanted) mine = true;   // parts hold full paths
        }
    return mine && count > 1 ? count : 0;
}

std::vector<std::string> Project::directories() const {
    std::vector<std::string> found;
    for (size_t i = 0; i < groups_.size(); ++i) {
        for (size_t j = 0; j < groups_[i].files.size(); ++j) {
            const std::string& file = groups_[i].files[j];
            size_t slash = file.find('/');
            if (slash == std::string::npos) continue;

            std::string dir = file.substr(0, slash);
            bool seen = false;
            for (size_t k = 0; k < found.size(); ++k)
                if (found[k] == dir) seen = true;
            if (!seen) found.push_back(dir);
        }
    }
    return found;
}

Project::~Project() { options::release(&options_); }

void Project::close() {
    options_ = options::Store();
    options::release(&options_);
    loaded_ = false;
    ccs_ = false;
    ccsWorkspace_.clear();
    gride_ = false;
    root_.clear();
    file_.clear();
    name_.clear();
    groups_.clear();
    target_ = Target();
}

void Project::addGroup(const std::string& group) {
    for (size_t i = 0; i < groups_.size(); ++i)
        if (groups_[i].name == group) return;

    Group made;
    made.name = group;
    groups_.push_back(made);
}

size_t Project::groupOf(const std::string& rel) const {
    std::string want = withSlashes(rel);
    for (size_t i = 0; i < groups_.size(); ++i)
        for (size_t j = 0; j < groups_[i].files.size(); ++j)
            if (groups_[i].files[j] == want) return i;
    return groups_.size();
}

bool Project::addFile(const std::string& rel, const std::string& group) {
    std::string want = withSlashes(rel);
    std::string why;
    if (!allows(want, why)) return false;
    if (groupOf(want) < groups_.size()) return false;

    addGroup(group.empty() ? std::string("Sources") : group);
    std::string into = group.empty() ? std::string("Sources") : group;

    for (size_t i = 0; i < groups_.size(); ++i) {
        if (groups_[i].name != into) continue;
        groups_[i].files.push_back(want);
        std::sort(groups_[i].files.begin(), groups_[i].files.end());
        return true;
    }
    return false;
}

bool Project::removeFile(const std::string& rel) {
    std::string want = withSlashes(rel);
    for (size_t i = 0; i < groups_.size(); ++i) {
        std::vector<std::string>& files = groups_[i].files;
        for (size_t j = 0; j < files.size(); ++j) {
            if (files[j] != want) continue;
            files.erase(files.begin() + static_cast<long>(j));
            return true;
        }
    }
    return false;
}

bool Project::renameFile(const std::string& from, const std::string& to) {
    std::string was = withSlashes(from);
    std::string now = withSlashes(to);
    std::string why;
    if (!allows(now, why)) return false;
    for (size_t i = 0; i < groups_.size(); ++i) {
        std::vector<std::string>& files = groups_[i].files;
        for (size_t j = 0; j < files.size(); ++j) {
            if (files[j] != was) continue;
            files[j] = now;
            std::sort(files.begin(), files.end());
            return true;
        }
    }
    return false;
}

bool Project::moveToGroup(const std::string& rel, const std::string& group) {
    std::string want = withSlashes(rel);
    size_t from = groupOf(want);
    if (from >= groups_.size()) return false;

    if (!removeFile(want)) return false;
    addGroup(group);
    for (size_t i = 0; i < groups_.size(); ++i) {
        if (groups_[i].name != group) continue;
        groups_[i].files.push_back(want);
        std::sort(groups_[i].files.begin(), groups_[i].files.end());
        return true;
    }
    return false;
}

ToolchainKind toolchainOf(const Toolchain& tool, const Part& part) {

    if (part.toolchain != ToolAuto) return part.toolchain;
    return resolve(tool, part.lang);
}

ToolchainKind Project::toolchainFor(const std::string& group) const {
    for (size_t i = 0; i < groups_.size(); ++i)
        if (groups_[i].name == group) return groups_[i].toolchain;
    return ToolAuto;
}

void Project::setGroupToolchain(const std::string& group, ToolchainKind kind) {
    for (size_t i = 0; i < groups_.size(); ++i)
        if (groups_[i].name == group) { groups_[i].toolchain = kind; return; }
}

bool Project::targetParts(std::vector<Part>& parts, std::string& why,
                          std::string* detail) const {
    parts.clear();
    why.clear();
    if (detail) detail->clear();

    if (!builds()) {
        why = std::string("this project does not say what it builds");
        if (detail)
            *detail = std::string("Add a \"build\" entry to ") + path::filename(file_) +
                      " naming the program and the groups its sources are in, like "
                      "\"build\": { \"target\": \"" + name_ +
                      "\", \"groups\": [\"Sources\"] }. Until then, Ctrl-B still "
                      "compiles the file in front of you, which needs no project at all.";
        return false;
    }

    std::vector<std::string> gone;

    for (size_t i = 0; i < target_.groups.size(); ++i) {
        size_t at = groups_.size();
        for (size_t g = 0; g < groups_.size(); ++g)
            if (groups_[g].name == target_.groups[i]) { at = g; break; }

        if (at == groups_.size()) {
            why = "no such group in this project: " + target_.groups[i];
            if (detail)
                *detail = std::string("The \"build\" entry in ") + path::filename(file_) +
                          " names a group the project does not have. Groups are the "
                          "headings in the pane on the left.";
            parts.clear();
            return false;
        }

        const Group& group = groups_[at];

        std::vector<std::string> byLanguage[LangCount];
        bool sawShalimar = false, sawOther = false;
        for (size_t f = 0; f < group.files.size(); ++f) {
            Language lang = sourceLanguageFor(group.files[f]);
            if (lang == LangPlain) continue;
            if (lang == LangShalimar) sawShalimar = true; else sawOther = true;
            std::string full = absolute(group.files[f]);
            if (!path::exists(full)) gone.push_back(group.files[f]);
            byLanguage[lang].push_back(full);
        }

        if (sawShalimar && sawOther) {
            why = target_.groups[i] + " holds Shalimar and C or C++ in one group";
            if (detail)
                *detail = "No compiler takes both, so this is not a matter of naming one: "
                          "shalimar reads Shalimar and nothing else, and c90 and cl read C and "
                          "C++ and not Shalimar. Put the Shalimar in a group of its own.";
            parts.clear();
            return false;
        }

        if (group.toolchain != ToolAuto) {
            Part part;
            part.group = group.name;
            part.toolchain = group.toolchain;
            part.lang = LangPlain;
            for (int l = 0; l < LangCount; ++l) {
                if (byLanguage[l].empty()) continue;
                if (part.lang == LangPlain || l == LangCpp)
                    part.lang = static_cast<Language>(l);
                for (size_t f = 0; f < byLanguage[l].size(); ++f)
                    part.sources.push_back(byLanguage[l][f]);
            }
            if (!part.sources.empty()) parts.push_back(part);
            continue;
        }

        for (int l = 0; l < LangCount; ++l) {
            if (byLanguage[l].empty()) continue;
            Part part;
            part.group = group.name;
            part.toolchain = ToolAuto;
            part.lang = static_cast<Language>(l);
            part.sources = byLanguage[l];
            parts.push_back(part);
        }
    }

    if (!gone.empty()) {
        why = gone[0] + " is in this project and not on disk";
        if (gone.size() > 1) {
            why += " (and " + std::to_string(gone.size() - 1) +
                   (gone.size() == 2 ? " other" : " others") + ")";
        }
        if (detail) {
            *detail = std::string("The build list in ") + path::filename(file_) +
                      " names files that are not there: ";
            for (size_t i = 0; i < gone.size(); ++i) {
                if (i) *detail += ", ";
                *detail += gone[i];
            }
            *detail += ". Put them back, or take them out of the group - a project that "
                       "lists a file it has not got cannot be built from.";
        }
        parts.clear();
        return false;
    }

    if (parts.empty()) {
        why = "the groups this project builds from hold no source";
        if (detail)
            *detail = "A group can hold anything - headers, notes, a Makefile - and none "
                      "of that is compiled. Name a group with .c, .cpp or .shl files in "
                      "it.";
        return false;
    }

    bool shalimar = false;
    for (size_t i = 0; i < parts.size(); ++i)
        if (parts[i].lang == LangShalimar) shalimar = true;

    if (shalimar && parts.size() > 1) {
        why = "Shalimar makes a whole program, so it cannot be part of one";
        if (detail)
            *detail = "shc compiles, assembles and links in one step, and a Shalimar "
                      "object is not a piece of something larger: whichever file it came "
                      "from it exports the same three startup symbols, so two of them "
                      "collide, and the language has no declarations, so a call across a "
                      "link could not be checked. Give the Shalimar its own project, or "
                      "take it out of this target's groups and build it with Ctrl-B. "
                      "See Compiler-S/docs/LINKING.md.";
        parts.clear();
        return false;
    }

    if (shalimar) {
        std::vector<std::string>& only = parts[0].sources;
        if (!oneShalimarProgram(only, why, detail)) {
            parts.clear();
            return false;
        }
    }
    return true;
}

bool Project::targetSources(std::vector<std::string>& sources, Language& lang,
                            std::string& why, std::string* detail) const {
    sources.clear();
    lang = LangPlain;

    std::vector<Part> parts;
    if (!targetParts(parts, why, detail)) return false;

    if (parts.size() > 1) {
        std::vector<std::string> named;
        for (size_t i = 0; i < parts.size(); ++i) {
            std::string word = languageWord(parts[i].lang);
            bool already = false;
            for (size_t j = 0; j < named.size(); ++j)
                if (named[j] == word) already = true;
            if (!already) named.push_back(word);
        }
        std::string all = named.empty() ? std::string() : named[0];
        for (size_t i = 1; i < named.size(); ++i)
            all += (i + 1 == named.size() ? " and " : ", ") + named[i];
        why = "this target holds " + all + ", so it takes more than one compiler";
        if (detail)
            *detail = "That is built rather than refused - each group goes to the compiler "
                      "that can take it and the objects meet at the linker - but whatever "
                      "asked this question wanted one command and one language, and there "
                      "is no honest single answer to give it.";
        return false;
    }

    sources = parts[0].sources;
    lang = parts[0].lang;
    return true;
}

bool Project::oneShalimarProgram(std::vector<std::string>& sources, std::string& why,
                                 std::string* detail) const {
    if (sources.size() == 1) return true;

    const std::string wanted = target_.name.empty() ? name_ : target_.name;
    size_t at = sources.size();
    for (size_t i = 0; i < sources.size(); ++i) {
        std::string leaf = path::filename(sources[i]);
        size_t dot = leaf.find_last_of('.');
        if (dot != std::string::npos) leaf.resize(dot);
        if (leaf != wanted) continue;
        if (at != sources.size()) { at = sources.size(); break; }
        at = i;
    }

    if (at < sources.size()) {
        std::swap(sources[0], sources[at]);
        return true;
    }

    why = "this project has " + std::to_string(sources.size()) +
          " Shalimar programs and builds one";
    if (detail) {
        *detail = "Every Shalimar file has a main(), so the project is what says which "
                  "one is the program; the others are where shalimar looks for what it calls "
                  "and does not define. Name the target after the one to build - "
                  "\"build\": { \"target\": \"" +
                  (sources.empty() ? std::string("name")
                                   : stemOf(path::filename(sources[0]))) +
                  "\" } - or build any of them with Ctrl-B, which never asks what the "
                  "project says. This target is called \"" + wanted +
                  "\" and no source here is.";
    }
    sources.clear();
    return false;
}

std::string Project::stemOf(const std::string& leaf) {
    size_t dot = leaf.find_last_of('.');
    return dot == std::string::npos ? leaf : leaf.substr(0, dot);
}

std::string Project::targetProgram() const {
    std::string name = target_.name.empty() ? name_ : target_.name;
    if (name.empty()) name = "program";
#ifdef _WIN32
    if (name.size() < 4 || name.compare(name.size() - 4, 4, ".exe") != 0) name += ".exe";
#endif
    // Never into a CCS folder: the .vm of assembly RIDE would leave there is a directory of .s
    // files, which CCS's next build would take for assembly sources of the project.
    if (ccs_) {
        std::string dir = path::join(path::tempDir(), std::string("ride-ccs-") + name_);
        path::makeDirectories(dir);
        return path::join(dir, name);
    }
    return path::join(root_, name);
}

// **The CCS project, as read** - groups, target, options and includes filled from the reading
// and nothing of it written back. Files the folder holds go in Sources by their relative
// names, linked ones in Linked by their absolute ones, and what every configuration excludes in Excluded, which the target does not build.
bool Project::loadWorkspacePro(const std::string& pro, const Json& ccs, std::string& error) {
    std::string where = withSlashes(ccs.get("workspace").text("."));
    std::string name = ccs.get("project").text(std::string());
    std::string beside = path::parent(withSlashes(path::absolute(pro)));
    bool rooted = !where.empty() && (where[0] == '/' || (where.size() > 1 && where[1] == ':'));
    std::string workspace = rooted ? where : where == "." || where.empty() ? beside : path::join(beside, where);
    if (name.empty()) { error = path::filename(pro) + ": \"ccs\" names no project"; return false; }
    ccs::Workspace ws;
    if (!ccs::readWorkspace(workspace, ws, error)) { error = path::filename(pro) + ": " + error; return false; }
    const ccs::Member* member = ws.project(name);
    if (!member) {
        error = path::filename(pro) + ": the workspace " + ws.dir + " has no CCS project " + name;
        for (size_t i = 0; i < ws.notes.size(); ++i) if (ws.notes[i].compare(0, name.size(), name) == 0) error += " (" + ws.notes[i] + ")";
        return false;
    }
    if (!loadCcs(member->location, error, ws.dir)) return false;
    file_ = withSlashes(path::absolute(pro));
    return true;
}

bool Project::loadCcs(const std::string& folder, std::string& error, const std::string& workspace) {
    ccs::Reading reading;
    ccs::Workspace ws;
    if (!workspace.empty() && !ccs::readWorkspace(workspace, ws, error)) return false;
    if (!ccs::read(folder, reading, error, workspace.empty() ? 0 : &ws)) return false;
    for (size_t i = 0; i < ws.notes.size(); ++i) reading.notes.push_back("workspace: " + ws.notes[i]);
    ccsReading_ = reading;
    ccs_ = true;
    emulateOnly_ = false;
    ccsWorkspace_ = workspace.empty() ? std::string() : ws.dir;
    root_ = reading.dir;
    file_ = path::join(root_, ".ccsproject");
    name_ = reading.name;
    toolchain_ = ToolAuto;
    arch_ = "tms6747";
    indentSaid_ = false;
    indent_.width = settings::indentWidth();
    indent_.tabs = settings::indentTabs();

    Group sources, headers, linked, excluded;
    sources.name = "Sources";
    headers.name = "Headers";
    // The folder's headers, as CCS's own Project Explorer lists them: opened by a click, never built.
    for (size_t i = 0; i < reading.headers.size(); ++i) headers.files.push_back(reading.headers[i]);
    linked.name = "Linked";
    excluded.name = "Excluded";
    std::vector<std::string> built;
    for (size_t c = 0; c < reading.configs.size(); ++c)
        for (size_t i = 0; i < reading.configs[c].sources.size(); ++i)
            if (std::find(built.begin(), built.end(), reading.configs[c].sources[i]) == built.end())
                built.push_back(reading.configs[c].sources[i]);
    for (size_t i = 0; i < built.size(); ++i) {
        std::string rel = path::relativeTo(built[i], root_);
        bool isLinked = std::find(reading.linked.begin(), reading.linked.end(), built[i]) != reading.linked.end();
        if (isLinked || rel.empty() || rel.compare(0, 2, "..") == 0) linked.files.push_back(withSlashes(built[i]));
        else sources.files.push_back(rel);
    }
    for (size_t c = 0; c < reading.configs.size(); ++c)
        for (size_t i = 0; i < reading.configs[c].excluded.size(); ++i) {
            const std::string& rel = reading.configs[c].excluded[i];
            bool everywhere = true;
            for (size_t k = 0; k < reading.configs.size(); ++k)
                if (std::find(reading.configs[k].excluded.begin(), reading.configs[k].excluded.end(), rel) == reading.configs[k].excluded.end()) everywhere = false;
            if (everywhere && path::exists(absolute(rel)) && std::find(excluded.files.begin(), excluded.files.end(), rel) == excluded.files.end())
                excluded.files.push_back(rel);
        }
    std::sort(sources.files.begin(), sources.files.end());
    groups_.clear();
    groups_.push_back(sources);
    if (!headers.files.empty()) groups_.push_back(headers);
    if (!linked.files.empty()) groups_.push_back(linked);
    if (!excluded.files.empty()) groups_.push_back(excluded);

    target_ = Target();
    target_.name = name_;
    target_.groups.push_back(sources.name);
    if (!linked.files.empty()) target_.groups.push_back(linked.name);

    includes_.clear();
    libraries_.clear();
    options_ = options::Store();
    for (size_t c = 0; c < reading.configs.size(); ++c) {
        const ccs::Config& config = reading.configs[c];
        for (size_t i = 0; i < config.includes.size(); ++i)
            if (std::find(includes_.begin(), includes_.end(), config.includes[i]) == includes_.end()) includes_.push_back(config.includes[i]);
        const char* tools[2] = { "c90", "cpp11" };
        for (int t = 0; t < 2; ++t) {
            std::string tool(tools[t]);
            if (!config.opt.empty()) options_.set(config.which, tool + ".opt", config.opt);
            if (config.debugSaid) options_.set(config.which, tool + ".g", config.debug ? "1" : "0");
            std::string defines, undefines;
            for (size_t i = 0; i < config.defines.size(); ++i) defines += (i ? ";" : "") + config.defines[i];
            for (size_t i = 0; i < config.undefines.size(); ++i) undefines += (i ? ";" : "") + config.undefines[i];
            if (!config.defines.empty()) options_.set(config.which, tool + ".defines", defines);
            if (!config.undefines.empty()) options_.set(config.which, tool + ".undefines", undefines);
        }
        options_.set(config.which, "cpp11.compress", config.noCompress ? "0" : "1");
    }
    options::setActive(&options_);

    Json state = settings::ccsProjectState(root_);
    open_ = withSlashes(state.get("open").text(std::string()));
    const Json& args = state.get("args");
    for (size_t i = 0; i < args.size(); ++i) target_.args.push_back(args.at(i).text(std::string()));
    loaded_ = true;
    return true;
}

std::vector<std::string> Project::ccsReport(Configuration config) const {
    std::vector<std::string> lines;
    if (!ccs_) return lines;
    lines.push_back(ccs::report(ccsReading_, config));
    std::string sources = ccs::sourceReport(ccsReading_);
    if (!sources.empty()) lines.push_back(sources);
    return lines;
}

std::string Project::ccsMapping(Configuration config) const {
    return ccs_ ? ccs::mappingText(ccsReading_, config) : std::string();
}

TiLink Project::tiLink(Configuration config) const {
    const ccs::Config* one = ccs_ ? ccsReading_.config(config) : 0;
    return one ? one->link : TiLink();
}

int Project::ccsConfiguration() const {
    if (!ccs_) return -1;
    std::string said = settings::ccsProjectState(root_).get("config").text(std::string());
    return said == "release" ? ConfigRelease : said == "debug" ? ConfigDebug : -1;
}

std::vector<std::string> Project::splitArguments(const std::string& line) {
    std::vector<std::string> out;
    std::string one;
    bool inQuote = false, any = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') { inQuote = !inQuote; any = true; continue; }
        if (!inQuote && (c == ' ' || c == '\t')) {
            if (any) out.push_back(one);
            one.clear();
            any = false;
            continue;
        }
        one += c;
        any = true;
    }
    if (any) out.push_back(one);
    return out;
}

std::string Project::argumentsText() const {
    std::string line;
    for (size_t i = 0; i < target_.args.size(); ++i) {
        const std::string& one = target_.args[i];
        if (i) line += " ";
        if (one.empty() || one.find_first_of(" \t") != std::string::npos) line += "\"" + one + "\"";
        else line += one;
    }
    return line;
}

bool Project::rememberArguments(const std::string& line, std::string& error) {
    error.clear();
    if (!loaded_) { error = "there is no project to keep arguments for"; return false; }
    target_.args = splitArguments(line);
    if (!ccs_) return save(error);
    Json state = settings::ccsProjectState(root_);
    if (!state.is(Json::Object)) state = Json::object();
    Json list = Json::array();
    for (size_t i = 0; i < target_.args.size(); ++i) list.push(Json::fromText(target_.args[i]));
    state.set("args", list);
    if (settings::rememberCcsProjectState(root_, state)) return true;
    error = "cannot write settings.json, where a CCS project's state is kept";
    return false;
}

namespace {

bool isHeaderFile(const std::string& rel) {
    size_t dot = rel.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = rel.substr(dot + 1);
    for (size_t i = 0; i < ext.size(); ++i) ext[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(ext[i])));
    return ext == "h" || ext == "hpp" || ext == "hh" || ext == "hxx" || ext == "inl";
}

}

// GRIDEd: { "gride", "name", "targets": [...], "folders": { "<folder>": [files] } }.
// GRIDE 2.0: { "formatVersion": 2, "name", "target", "outputName", "sourceFiles": [...], "programArgs": [...] }.
// Each folder is a group (GRIDE 2.0's one list is Sources); headers go to Headers, which is never built.
bool Project::loadGride(const Json& root, const std::string& pro, const std::string& dir, std::string& error) {
    // GRIDE 2.0's .pro of a Visual Studio solution names it and holds no files of its own: the solution is read.
    std::string solution = withSlashes(root.get("visualStudio").text(std::string()));
    if (!solution.empty() && !root.has("sourceFiles")) {
        bool rooted = solution[0] == '/' || (solution.size() > 1 && solution[1] == ':');
        if (!loadVs(rooted ? solution : path::join(dir, solution), error)) return false;
        // What GRIDE 2.0 keeps for it - the compiler (cl for cppparse), the program's arguments - unless RIDE has its own.
        Json state = settings::ccsProjectState(root_);
        if (!state.has("toolchain")) toolchain_ = toolchainFrom(root.get("toolchain").text("auto"));
        if (!state.has("args")) {
            const Json& args = root.get("programArgs");
            for (size_t i = 0; i < args.size(); ++i) target_.args.push_back(args.at(i).text(std::string()));
        }
        file_ = pro;
        return true;
    }
    root_ = dir;
    file_ = pro;
    name_ = root.get("name").text(path::filename(dir));
    toolchain_ = ToolAuto;
    emulateOnly_ = false;
    indentSaid_ = false;
    indent_.width = settings::indentWidth();
    indent_.tabs = settings::indentTabs();
    includes_.clear();
    libraries_.clear();
    options_ = options::Store();
    options::setActive(&options_);

    groups_.clear();
    Group headers;
    headers.name = "Headers";
    std::vector<std::pair<std::string, const Json*> > lists;
    const Json& folders = root.get("folders");
    for (size_t i = 0; i < folders.size(); ++i) {
        const Json& entry = folders.valueAt(i);
        lists.push_back(std::make_pair(folders.keyAt(i), entry.is(Json::Object) ? &entry.get("files") : &entry));
    }
    if (root.has("sourceFiles")) lists.push_back(std::make_pair(std::string("Sources"), &root.get("sourceFiles")));
    for (size_t l = 0; l < lists.size(); ++l) {
        Group group;
        group.name = lists[l].first == "Headers" ? std::string("Header sources") : lists[l].first;
        const Json& files = *lists[l].second;
        for (size_t j = 0; j < files.size(); ++j) {
            std::string relative = withSlashes(files.at(j).text());
            if (relative.empty()) continue;
            std::string reason;
            if (!allows(relative, reason)) {
                if (error.empty()) error = relative + ": " + reason;
                continue;
            }
            (isHeaderFile(relative) ? headers : group).files.push_back(relative);
        }
        if (!group.files.empty()) groups_.push_back(group);
    }
    if (groups_.empty()) {
        Group all;
        all.name = "Sources";
        groups_.push_back(all);
    }
    target_ = Target();
    target_.name = root.get("outputName").text(name_);
    for (size_t g = 0; g < groups_.size(); ++g) target_.groups.push_back(groups_[g].name);
    if (!headers.files.empty()) groups_.push_back(headers);
    const Json& args = root.get("programArgs");
    for (size_t i = 0; i < args.size(); ++i) target_.args.push_back(args.at(i).text(std::string()));

    // The target it was last built for: GRIDE 2.0's "target", GRIDEd's first of "targets".
    std::string target = root.get("target").text(root.get("targets").at(0).text(std::string()));
    arch_ = target.find("6747") != std::string::npos ? std::string("tms6747") : std::string(hostArch());

    finishForeign("GRIDE");
    return true;
}

namespace {

bool endsWithNoCase(const std::string& text, const std::string& tail) {
    if (text.size() < tail.size()) return false;
    for (size_t i = 0; i < tail.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(text[text.size() - tail.size() + i])) !=
            std::tolower(static_cast<unsigned char>(tail[i]))) return false;
    return true;
}

std::vector<std::string> splitOn(const std::string& text, char by) {
    std::vector<std::string> out;
    std::string one;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == by) {
            size_t a = one.find_first_not_of(" \t\r\n"), b = one.find_last_not_of(" \t\r\n");
            if (a != std::string::npos) out.push_back(one.substr(a, b - a + 1));
            one.clear();
        } else one += text[i];
    }
    return out;
}

// A leaf name against a pattern of * and ?, without regard to case, as Windows matches.
bool globMatch(const char* p, const char* s) {
    if (*p == '\0') return *s == '\0';
    if (*p == '*') return globMatch(p + 1, s) || (*s != '\0' && globMatch(p, s + 1));
    if (*s == '\0') return false;
    if (*p != '?' && std::tolower(static_cast<unsigned char>(*p)) != std::tolower(static_cast<unsigned char>(*s))) return false;
    return globMatch(p + 1, s + 1);
}

// What MSBuild would make of a path or a list entry: the project's and the solution's own directory
// macros put in, anything else left as an unknown "$(" the caller drops.
std::string vsExpand(std::string text, const std::string& projectDir, const std::string& solutionDir) {
    struct { const char* macro; const std::string* value; } known[] = {
        { "$(ProjectDir)", &projectDir }, { "$(MSBuildProjectDirectory)", &projectDir },
        { "$(MSBuildThisFileDirectory)", &projectDir }, { "$(SolutionDir)", &solutionDir } };
    for (size_t k = 0; k < sizeof known / sizeof known[0]; ++k) {
        std::string with = *known[k].value + "/";
        for (size_t at; (at = text.find(known[k].macro)) != std::string::npos;)
            text.replace(at, std::strlen(known[k].macro), with);
    }
    for (size_t i = 0; i < text.size(); ++i) if (text[i] == '\\') text[i] = '/';
    for (size_t at; (at = text.find("//")) != std::string::npos;) text.erase(at, 1);
    return text;
}

bool readVsXml(const std::string& file, ccs::XmlNode& root, std::string& error) {
    FILE* in = std::fopen(file.c_str(), "rb");
    if (!in) { error = "cannot read " + file; return false; }
    std::string text;
    char chunk[4096];
    size_t got;
    while ((got = std::fread(chunk, 1, sizeof chunk, in)) > 0) text.append(chunk, got);
    std::fclose(in);
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);   // Visual Studio writes a BOM
    if (!ccs::parseXml(text, root, error)) { error = path::filename(file) + ": " + error; return false; }
    return true;
}

const ccs::XmlNode& vsProjectElement(const ccs::XmlNode& doc) {
    if (doc.name == "Project") return doc;
    const ccs::XmlNode* p = doc.child("Project");
    return p ? *p : doc;
}

// The C++ projects a .sln lists, by name and absolute path; a solution folder or a C# project is passed over.
std::vector<std::pair<std::string, std::string> > solutionProjects(const std::string& sln) {
    std::vector<std::pair<std::string, std::string> > out;
    FILE* in = std::fopen(sln.c_str(), "rb");
    if (!in) return out;
    std::string text;
    char chunk[4096];
    size_t got;
    while ((got = std::fread(chunk, 1, sizeof chunk, in)) > 0) text.append(chunk, got);
    std::fclose(in);
    std::vector<std::string> lines = splitOn(text, '\n');
    for (size_t i = 0; i < lines.size(); ++i) {
        // Project("{type}") = "name", "relative\path.vcxproj", "{guid}"
        const std::string& line = lines[i];
        if (line.compare(0, 8, "Project(") != 0) continue;
        std::vector<std::string> quoted;
        for (size_t at = line.find('='); at != std::string::npos && quoted.size() < 3;) {
            size_t open = line.find('"', at);
            if (open == std::string::npos) break;
            size_t close = line.find('"', open + 1);
            if (close == std::string::npos) break;
            quoted.push_back(line.substr(open + 1, close - open - 1));
            at = close + 1;
        }
        if (quoted.size() < 2 || !endsWithNoCase(quoted[1], ".vcxproj")) continue;
        out.push_back(std::make_pair(quoted[0], path::join(path::parent(sln), vsExpand(quoted[1], "", ""))));
    }
    return out;
}

}

// **A Visual Studio project opened as it is**: a .sln - its one C++ project, or of several the first
// that makes a program - or a .vcxproj, as Visual Studio 2010 to 2022 write them. Read as GRIDEd and
// GRIDE 2.0 read them: the ClCompile and ClInclude items (a wildcard in the file name expanded), what is
// excluded from the build, the include directories and the definitions of Debug and Release on x64
// (or the one platform there is). Never written: RIDE's own state goes to settings.json.
bool Project::loadVs(const std::string& file, std::string& error) {
    std::string opened = withSlashes(path::absolute(file)), vcx = opened, solutionDir;
    ccs::XmlNode doc;
    if (endsWithNoCase(opened, ".sln")) {
        solutionDir = path::parent(opened);
        std::vector<std::pair<std::string, std::string> > projects = solutionProjects(opened);
        if (projects.empty()) {
            error = path::filename(opened) + " has no C++ project (.vcxproj) RIDE can read";
            return false;
        }
        vcx.clear();
        for (size_t i = 0; i < projects.size() && vcx.empty(); ++i) {
            ccs::XmlNode one;
            std::string why;
            if (!readVsXml(projects[i].second, one, why)) continue;
            std::vector<const ccs::XmlNode*> kinds;
            vsProjectElement(one).find("ConfigurationType", kinds);
            if (kinds.empty() || kinds[0]->text == "Application") { vcx = projects[i].second; doc = one; }
        }
        if (vcx.empty()) {
            vcx = projects[0].second;
            if (!readVsXml(vcx, doc, error)) return false;
        }
    } else if (!readVsXml(vcx, doc, error)) {
        return false;
    }
    const ccs::XmlNode& top = vsProjectElement(doc);
    std::string dir = path::parent(vcx);
    if (solutionDir.empty()) solutionDir = dir;

    // The platform: x64 when the project has it, else the first it names.
    std::string platform;
    std::vector<const ccs::XmlNode*> configs;
    top.find("ProjectConfiguration", configs);
    for (size_t i = 0; i < configs.size(); ++i) {
        std::string inc = configs[i]->attribute("Include");
        size_t bar = inc.find('|');
        if (bar == std::string::npos) continue;
        if (platform.empty() || inc.substr(bar + 1) == "x64") platform = inc.substr(bar + 1);
    }

    Group sources, headers, linked, excluded;
    sources.name = "Sources";
    headers.name = "Headers";
    linked.name = "Linked";
    excluded.name = "Excluded";
    std::vector<const ccs::XmlNode*> groups;
    top.find("ItemGroup", groups);
    for (size_t g = 0; g < groups.size(); ++g)
        for (size_t c = 0; c < groups[g]->children.size(); ++c) {
            const ccs::XmlNode& item = groups[g]->children[c];
            bool header = item.name == "ClInclude";
            if (!header && item.name != "ClCompile") continue;
            bool out = false;
            for (size_t k = 0; k < item.children.size(); ++k)
                if (item.children[k].name == "ExcludedFromBuild" && item.children[k].text == "true") out = true;
            std::vector<std::string> named = splitOn(item.attribute("Include"), ';');
            for (size_t n = 0; n < named.size(); ++n) {
                std::string one = vsExpand(named[n], dir, solutionDir);
                if (one.find("$(") != std::string::npos || one.find("%(") != std::string::npos) continue;
                bool rooted = !one.empty() && (one[0] == '/' || (one.size() > 1 && one[1] == ':'));
                std::string full = rooted ? one : path::join(dir, one);
                std::vector<std::string> found;
                std::string leaf = path::filename(full);
                if (leaf.find_first_of("*?") != std::string::npos) {
                    std::string folder = path::parent(full);
                    for (size_t at; (at = folder.find("/**")) != std::string::npos;) folder.erase(at, 3);
                    std::vector<path::Entry> listed = path::entries(folder);
                    for (size_t e = 0; e < listed.size(); ++e)
                        if (!listed[e].directory && globMatch(leaf.c_str(), listed[e].name.c_str()))
                            found.push_back(path::join(folder, listed[e].name));
                    std::sort(found.begin(), found.end());
                } else {
                    found.push_back(full);
                }
                for (size_t f = 0; f < found.size(); ++f) {
                    std::string rel = path::relativeTo(found[f], dir);
                    bool outside = rel.empty() || rel.compare(0, 2, "..") == 0;
                    Group& into = out ? excluded : header ? headers : outside ? linked : sources;
                    std::string entry = outside ? withSlashes(found[f]) : withSlashes(rel);
                    if (std::find(into.files.begin(), into.files.end(), entry) == into.files.end()) into.files.push_back(entry);
                }
            }
        }

    root_ = dir;
    file_ = opened;
    name_ = path::filename(vcx);
    name_.resize(name_.size() - std::string(".vcxproj").size());
    toolchain_ = ToolAuto;
    arch_ = hostArch();
    emulateOnly_ = false;
    indentSaid_ = false;
    indent_.width = settings::indentWidth();
    indent_.tabs = settings::indentTabs();
    includes_.clear();
    libraries_.clear();
    options_ = options::Store();

    // The item definitions: those with no condition hold for both configurations, the others for theirs.
    std::vector<std::string> defines[ConfigCount];
    std::vector<const ccs::XmlNode*> definitions;
    top.find("ItemDefinitionGroup", definitions);
    for (size_t d = 0; d < definitions.size(); ++d) {
        std::string when = definitions[d]->attribute("Condition");
        bool applies[ConfigCount] = { when.empty(), when.empty() };
        if (!when.empty() && (platform.empty() || when.find("|" + platform) != std::string::npos)) {
            applies[ConfigDebug] = when.find("Debug") != std::string::npos;
            applies[ConfigRelease] = when.find("Release") != std::string::npos;
        }
        if (!applies[ConfigDebug] && !applies[ConfigRelease]) continue;
        const ccs::XmlNode* compile = definitions[d]->child("ClCompile");
        if (!compile) continue;
        const ccs::XmlNode* dirs = compile->child("AdditionalIncludeDirectories");
        std::vector<std::string> listed = dirs ? splitOn(dirs->text, ';') : std::vector<std::string>();
        for (size_t i = 0; i < listed.size(); ++i) {
            std::string one = vsExpand(listed[i], dir, solutionDir);
            if (one.find("$(") != std::string::npos || one.find("%(") != std::string::npos) continue;
            while (one.size() > 1 && one[one.size() - 1] == '/') one.erase(one.size() - 1);
            std::string rel = path::relativeTo(one, dir);
            std::string entry = !rel.empty() && rel.compare(0, 2, "..") != 0 ? rel : one;
            if (std::find(includes_.begin(), includes_.end(), entry) == includes_.end()) includes_.push_back(entry);
        }
        const ccs::XmlNode* defs = compile->child("PreprocessorDefinitions");
        std::vector<std::string> said = defs ? splitOn(defs->text, ';') : std::vector<std::string>();
        for (int c = 0; c < ConfigCount; ++c) {
            if (!applies[c]) continue;
            for (size_t i = 0; i < said.size(); ++i)
                if (said[i].find("%(") == std::string::npos && said[i].find("$(") == std::string::npos &&
                    std::find(defines[c].begin(), defines[c].end(), said[i]) == defines[c].end())
                    defines[c].push_back(said[i]);
        }
    }
    for (int c = 0; c < ConfigCount; ++c) {
        std::string joined;
        for (size_t i = 0; i < defines[c].size(); ++i) joined += (i ? ";" : "") + defines[c][i];
        if (joined.empty()) continue;
        options_.set(static_cast<Configuration>(c), "c90.defines", joined);
        options_.set(static_cast<Configuration>(c), "cpp11.defines", joined);
    }
    options::setActive(&options_);

    groups_.clear();
    groups_.push_back(sources);
    if (!linked.files.empty()) groups_.push_back(linked);
    if (!headers.files.empty()) groups_.push_back(headers);
    if (!excluded.files.empty()) groups_.push_back(excluded);

    target_ = Target();
    std::vector<const ccs::XmlNode*> names;
    top.find("TargetName", names);
    target_.name = !names.empty() && names[0]->text.find("$(") == std::string::npos && !names[0]->text.empty() ? names[0]->text : name_;
    target_.groups.push_back(sources.name);
    if (!linked.files.empty()) target_.groups.push_back(linked.name);

    finishForeign("Visual Studio");
    return true;
}

// The end of opening another IDE's project: what it is, as read, and what RIDE remembered of its own.
void Project::finishForeign(const char* kind) {
    grideShape_ = shape();
    Json state = settings::ccsProjectState(root_);
    open_ = withSlashes(state.get("open").text(std::string()));
    if (state.has("args")) {
        target_.args.clear();
        const Json& kept = state.get("args");
        for (size_t i = 0; i < kept.size(); ++i) target_.args.push_back(kept.at(i).text(std::string()));
    }
    std::string arch = state.get("arch").text(std::string());
    if (!arch.empty() && (!isHostArch(arch) || arch == hostArch())) arch_ = arch;
    if (state.has("toolchain")) toolchain_ = toolchainFrom(state.get("toolchain").text("auto"));
    foreign_ = kind;
    gride_ = true;
    loaded_ = true;
}

// What a GRIDE project's .pro would have to say differently for a change: its groups, build and options.
std::string Project::shape() const {
    Json all = Json::object();
    Json groups = Json::object();
    for (size_t i = 0; i < groups_.size(); ++i) {
        Json files = Json::array();
        for (size_t j = 0; j < groups_[i].files.size(); ++j) files.push(Json::fromText(groups_[i].files[j]));
        files.push(Json::fromText(toolchainWord(groups_[i].toolchain)));
        groups.set(groups_[i].name, files);
    }
    all.set("groups", groups);
    Json built = Json::array();
    built.push(Json::fromText(target_.name));
    for (size_t i = 0; i < target_.groups.size(); ++i) built.push(Json::fromText(target_.groups[i]));
    all.set("build", built);
    Json more = Json::array();
    for (size_t i = 0; i < includes_.size(); ++i) more.push(Json::fromText("I" + includes_[i]));
    for (size_t i = 0; i < libraries_.size(); ++i) more.push(Json::fromText("L" + libraries_[i]));
    more.push(Json::fromBool(emulateOnly_));
    all.set("more", more);
    all.set("options", options_.toJson());
    return all.write();
}

bool Project::rememberConfiguration(Configuration config) {
    if (!ccs_) return false;
    Json state = settings::ccsProjectState(root_);
    if (!state.is(Json::Object)) state = Json::object();
    state.set("config", Json::fromText(configName(config)));
    return settings::rememberCcsProjectState(root_, state);
}

bool Project::reloadIfCcs(std::string& error) {
    error.clear();
    if (!ccs_) return true;
    std::string open = open_, folder = root_, file = file_, workspace = ccsWorkspace_;
    if (!loadCcs(folder, error, workspace)) return false;
    file_ = file;
    open_ = open;
    return true;
}

}
