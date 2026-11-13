// workspace.cpp - the exported class: toolchain.json and the settings read once, a project read by every plan.
#include "context.h"

namespace gride { namespace compile {

namespace {

bool readJson(FileSystem &fs, const std::string &file, Json &into, std::string &problem) {
    std::string text;
    if (!fs.read(file, text)) { problem = "cannot read " + file; return false; }
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    std::string error;
    into = Json::parse(text, error);
    if (!error.empty()) { problem = file + ": " + error; return false; }
    return true;
}

Problem problem(const std::string &severity, const std::string &file, const std::string &text) {
    Problem p;
    p.severity = severity;
    p.file = file;
    p.text = text;
    return p;
}

}

Workspace::Workspace() : data_(new WorkspaceData()) {}
Workspace::Workspace(const Workspace &other) : data_(new WorkspaceData(*other.data_)) {}
Workspace &Workspace::operator=(const Workspace &other) {
    if (this != &other) { WorkspaceData *copy = new WorkspaceData(*other.data_); delete data_; data_ = copy; }
    return *this;
}
Workspace::~Workspace() { delete data_; }

Workspace Workspace::load(const Paths &paths, FileSystem &fs, Environment &env) {
    Workspace w;
    WorkspaceData &d = *w.data_;
    d.paths = paths;
    d.fs = &fs;
    d.env = &env;

    Json shipped, user, settings;
    std::string why;
    if (!readJson(fs, paths.shippedToolchain, shipped, why)) {
        d.problems.push_back(problem("error", paths.shippedToolchain, "toolchain.json: " + why + " - nothing can be planned without it"));
        return w;
    }
    Json all = shipped;
    if (!paths.userToolchain.empty() && fs.isFile(paths.userToolchain)) {
        if (readJson(fs, paths.userToolchain, user, why)) { all = merged(shipped, user); d.defs.userChanges = user.size() > 0; }
        else d.problems.push_back(problem("warning", paths.userToolchain, "the user's toolchain.json is not read: " + why));
    }
    Words wrong;
    bool changed = d.defs.userChanges;
    d.defs = readDefinitions(all, wrong);
    d.defs.userChanges = changed;
    for (size_t i = 0; i < wrong.size(); ++i) d.problems.push_back(problem("error", paths.shippedToolchain, wrong[i]));

    if (!paths.settings.empty() && fs.isFile(paths.settings)) {
        if (readJson(fs, paths.settings, settings, why)) d.settings = readBuildSettings(settings, paths.outlook);
        else d.problems.push_back(problem("warning", paths.settings, "settings.json is not read, the defaults stand: " + why));
    }
    return w;
}

std::vector<Problem> Workspace::problems() const { return data_->problems; }

Plan Workspace::plan(const Request &request) const { return makePlan(*data_, request); }

std::vector<Listed> Workspace::files(const std::string &project) const {
    Project p;
    std::string why;
    if (!readProject(project, *data_->fs, p, why)) return std::vector<Listed>();
    return resolveFiles(p, *data_->fs, Words()).listed;
}

std::string Workspace::describe() const {
    return std::string("compile ") + GRIDE_COMPILE_NUMBER + ", toolchain.json " +
           (data_->defs.userChanges ? "with the user's changes" : "as shipped") + "; from " + tidy(data_->paths.programFolder);
}

const char *version() { return GRIDE_COMPILE_VERSION; }

std::string windowsCommandLine(const std::string &program, const std::vector<std::string> &argv) { return commandLine(program, argv); }

std::vector<std::string> splitCommandLine(const std::string &line) { return cutWords(line); }

}}
