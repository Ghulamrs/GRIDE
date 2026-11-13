// log.cpp - the build log (the design's 5.1): planned whole from a plan before anything runs, released in the
// plan's order whatever order the steps end in, the same text in the three outlooks.
//
// A program is written by its name and a file of the project from the project's folder; a file beside the tools
// is written from <tools>, the folder the head names once. So two outlooks building one project give logs that
// differ in the lines of the head that name the outlook and its folder.
#include "context.h"

#include <cstdio>

namespace gride { namespace compile {

namespace {

struct Item {
    std::vector<int> steps;       // the plan's steps this numbered item holds (the assembles of a build are one)
    std::string head, brief;      // its line in the log; the same without the tool and the reason
    bool released;
    Item() : released(false) {}
};

struct StepState {
    bool done, upToDate;
    int exitCode;
    std::string output;
    double seconds;
    StepState() : done(false), upToDate(false), exitCode(0), seconds(0) {}
};

std::string padded(const std::string &s, size_t width) { return s.size() + 2 > width ? s + "  " : s + std::string(width - s.size(), ' '); }

// provisional until the diagnostics reader of the design's step 7: a line counts when it says "error" or
// "warning" the way the tools' dialects all do, before a colon or after one
int counts(const std::string &line, const char *word) {
    std::string l = lower(line), w = word;
    for (size_t at = l.find(w); at != std::string::npos; at = l.find(w, at + 1)) {
        bool before = at == 0 || l[at - 1] == ' ' || l[at - 1] == ':' || l[at - 1] == '\t';
        size_t end = at + w.size();
        bool after = end < l.size() && (l[end] == ':' || l[end] == ' ');
        if (before && after && (l.find(':', end) != std::string::npos || (at > 0 && l.rfind(':', at) != std::string::npos))) return 1;
    }
    return 0;
}

}

struct BuildLogData {
    Plan plan;
    FileSystem *fs;
    std::string projectFolder, toolsFolder, file;
    std::vector<std::string> head;
    std::vector<Item> items;
    std::vector<StepState> states;
    size_t next;                  // the first item not yet released
    bool failed, opened, screenCommands;      // screenCommands: the lines handed back show each command line
    int errors, warnings;
    double seconds;
    std::string text;

    BuildLogData() : fs(0), next(0), failed(false), opened(false), screenCommands(true), errors(0), warnings(0), seconds(0) {}

    std::string shownPath(const std::string &word) const {
        // a path inside a word (/out:C:\..., -IC:\...) is shortened where it starts
        std::string r = word;
        struct Short { const std::string *folder; const char *as; } shorts[2] = { { &projectFolder, "" }, { &toolsFolder, "<tools>\\" } };
        for (int i = 0; i < 2; ++i) {
            const std::string &f = *shorts[i].folder;
            if (f.empty()) continue;
            std::string lr = lower(r), lf = lower(f) + "\\";
            size_t at = lr.find(lf);
            if (at != std::string::npos) r = r.substr(0, at) + shorts[i].as + r.substr(at + lf.size());
        }
        return r;
    }

    std::string command(const Step &s) const {
        Words shown;
        for (size_t i = 0; i < s.argv.size(); ++i) shown.push_back(shownPath(s.argv[i]));
        return "      " + commandLine(leaf(s.program), shown);
    }

    // a line of the log: always into the text that is kept; handed back for the screen unless it is a command
    // line and the screen does not show those
    void add(std::vector<std::string> &into, const std::string &line, bool isCommand = false) {
        if (!isCommand || screenCommands) into.push_back(line);
        text += line + "\r\n";
    }

    void save() { if (fs && opened && !file.empty()) fs->write(file, text); }

    // the lines of one item, with what its steps wrote
    void release(Item &item, std::vector<std::string> &into) {
        item.released = true;
        bool allUpToDate = true;
        for (size_t i = 0; i < item.steps.size(); ++i) if (!states[item.steps[i]].upToDate) allUpToDate = false;
        if (allUpToDate) { add(into, item.brief + " - up to date"); return; }
        add(into, item.head);
        for (size_t i = 0; i < item.steps.size(); ++i) {
            const Step &s = plan.steps[item.steps[i]];
            const StepState &st = states[item.steps[i]];
            if (st.upToDate) { add(into, "      " + s.shown + " - up to date"); continue; }
            add(into, command(s), true);
            Words lines = split(replaceAll(st.output, "\r", ""), "\n", true);
            while (!lines.empty() && trim(lines.back()).empty()) lines.pop_back();
            for (size_t k = 0; k < lines.size(); ++k) {
                errors += counts(lines[k], "error");
                warnings += counts(lines[k], "warning");
                add(into, "      " + shownPath(lines[k]));
            }
            seconds += st.seconds;
            if (st.exitCode != 0) {
                char b[64];
                std::snprintf(b, sizeof b, "      exit code %d", st.exitCode);
                add(into, b);
                if (!screenCommands) into.push_back(command(s));      // a step that failed shows its command after all
                failed = true;
            }
        }
    }

    std::vector<std::string> due() {
        std::vector<std::string> r;
        while (!failed && next < items.size()) {
            Item &item = items[next];
            bool ready = true;
            for (size_t i = 0; i < item.steps.size(); ++i) if (!states[item.steps[i]].done) ready = false;
            if (!ready) break;
            release(item, r);
            ++next;
        }
        if (!r.empty()) save();
        return r;
    }
};

BuildLog::BuildLog(const Workspace &w, const Request &r, const Plan &plan, const std::string &outlook, const std::string &when)
    : data_(new BuildLogData()) {
    BuildLogData &d = *data_;
    d.plan = plan;
    d.fs = w.data_->fs;
    d.projectFolder = parent(tidy(r.project));
    d.toolsFolder = tidy(w.data_->paths.programFolder);
    d.states.resize(plan.steps.size());
    std::string project = stem(r.project);
    if (!plan.objectFolder.empty()) d.file = combine(plan.objectFolder, project + ".build.log");

    const char *what = r.action == Rebuild ? "GRIDE rebuild" : r.action == Clean ? "GRIDE clean" : r.action == CompileFile ? "GRIDE compile" : "GRIDE build";
    std::string configuration = trim(r.configuration).empty() ? std::string("Debug") : trim(r.configuration);
    d.head.push_back(padded(what, 14) + project + "   " + configuration + "   " + r.target + "   " + when + "   " + outlook);
    d.head.push_back(padded("tools", 14) + w.describe());
    d.head.push_back(padded("project", 14) + tidy(r.project));
    for (size_t i = 0; i < plan.listed.size(); ++i) {
        const Listed &l = plan.listed[i];
        if (!l.pattern) continue;
        d.head.push_back(padded("files", 14) + l.group + ": " + l.entry + (l.files.empty() ? " matches no file" : " -> " + join(l.files, ", ")));
    }
    if (!plan.cleans.empty()) {
        Words shown;
        for (size_t i = 0; i < plan.cleans.size(); ++i) shown.push_back(d.shownPath(plan.cleans[i]));
        d.head.push_back(padded("clean", 14) + join(shown, ", "));
    }
    for (size_t i = 0; i < plan.problems.size(); ++i)
        d.head.push_back(padded(plan.problems[i].severity, 14) + d.shownPath(plan.problems[i].text));
    for (size_t i = 0; i < plan.notes.size(); ++i)
        if (!startsWith(plan.notes[i], "runtime: ")) d.head.push_back(padded("note", 14) + d.shownPath(plan.notes[i]));

    // the numbered items: one per step, the assembles of one tool together
    for (size_t i = 0; i < plan.steps.size(); ++i) {
        const Step &s = plan.steps[i];
        if (s.kind == "assemble" && !d.items.empty()) {
            const Step &before = plan.steps[d.items.back().steps[0]];
            if (before.kind == "assemble" && before.tool == s.tool) { d.items.back().steps.push_back(static_cast<int>(i)); continue; }
        }
        Item item;
        item.steps.push_back(static_cast<int>(i));
        d.items.push_back(item);
    }
    for (size_t i = 0; i < d.items.size(); ++i) {
        Item &item = d.items[i];
        const Step &s = plan.steps[item.steps[0]];
        Words shown;
        for (size_t k = 0; k < item.steps.size(); ++k) shown.push_back(plan.steps[item.steps[k]].shown);
        char number[32];
        std::snprintf(number, sizeof number, "[%d/%d] ", static_cast<int>(i + 1), static_cast<int>(d.items.size()));
        item.head = number + padded(s.kind, 10) + padded(join(shown, ", "), 13) + padded(s.tool, 10) + s.why;
        item.brief = number + padded(s.kind, 10) + join(shown, ", ");
        while (!item.head.empty() && item.head[item.head.size() - 1] == ' ') item.head.erase(item.head.size() - 1);
    }
    d.errors = 0;
    for (size_t i = 0; i < plan.problems.size(); ++i) {
        if (plan.problems[i].severity == "error") ++d.errors;
        if (plan.problems[i].severity == "warning") ++d.warnings;
    }
}

BuildLog::~BuildLog() { delete data_; }

std::string BuildLog::planned() const {
    const BuildLogData &d = *data_;
    std::string r;
    for (size_t i = 0; i < d.head.size(); ++i) r += d.head[i] + "\r\n";
    for (size_t i = 0; i < d.items.size(); ++i) {
        r += d.items[i].head + "\r\n";
        for (size_t k = 0; k < d.items[i].steps.size(); ++k) r += d.command(d.plan.steps[d.items[i].steps[k]]) + "\r\n";
    }
    for (size_t i = 0; i < d.plan.files.size(); ++i) r += padded("writes", 14) + d.shownPath(d.plan.files[i].first) + "\r\n";
    if (!d.plan.program.empty() && d.plan.runnable()) r += padded("builds", 14) + d.shownPath(d.plan.program) + "\r\n";
    return r;
}

// The dry run: every command a full build would run, in order, each written whole - the program's path, every
// argument, quoted as Windows wants them - so that the text is the script of the build. The folder the commands
// run in, the variables a command is given and the files written for a tool are part of it. Nothing is run and
// nothing is written; the first and the last line say so.
std::string BuildLog::dryRun() const {
    const BuildLogData &d = *data_;
    const char *banner = "DRY RUN ONLY - nothing was compiled, assembled or linked: these are the commands a full build would run";
    std::string r = std::string(banner) + "\r\n";
    for (size_t i = 0; i < d.head.size(); ++i) r += d.head[i] + "\r\n";
    if (!d.plan.steps.empty()) r += padded("folder", 14) + d.plan.steps[0].directory + "   (every command runs here)\r\n";
    for (size_t i = 0; i < d.plan.files.size(); ++i) {
        r += padded("writes", 14) + d.plan.files[i].first + "\r\n";
        Words lines = split(replaceAll(d.plan.files[i].second, "\r", ""), "\n", true);
        while (!lines.empty() && lines.back().empty()) lines.pop_back();
        for (size_t k = 0; k < lines.size(); ++k) r += "      | " + lines[k] + "\r\n";
    }
    for (size_t i = 0; i < d.items.size(); ++i) {
        r += d.items[i].head + "\r\n";
        for (size_t k = 0; k < d.items[i].steps.size(); ++k) {
            const Step &s = d.plan.steps[d.items[i].steps[k]];
            for (size_t e = 0; e < s.environment.size(); ++e) r += "      set " + s.environment[e].first + "=" + s.environment[e].second + "\r\n";
            r += "      " + commandLine(s.program, s.argv) + "\r\n";
        }
    }
    if (!d.plan.runnable()) r += padded("not runnable", 14) + "the errors above stand in the way of this build\r\n";
    else if (d.plan.steps.empty()) r += padded("nothing", 14) + "this build has no command to run\r\n";
    else if (!d.plan.program.empty()) r += padded("would build", 14) + d.plan.program + "\r\n";
    return r + "DRY RUN ONLY - end\r\n";
}

void BuildLog::showCommands(bool onScreen) { data_->screenCommands = onScreen; }

// the head; the log of the build before is kept as .build.previous.log
std::vector<std::string> BuildLog::begin() {
    BuildLogData &d = *data_;
    std::vector<std::string> r;
    if (d.fs && !d.file.empty()) {
        std::string old;
        if (d.fs->isFile(d.file) && d.fs->read(d.file, old)) d.fs->write(d.file.substr(0, d.file.size() - 4) + ".previous.log", old);
        d.opened = true;
    }
    for (size_t i = 0; i < d.head.size(); ++i) d.add(r, d.head[i]);
    d.save();
    return r;
}

std::vector<std::string> BuildLog::finished(int step, int exitCode, const std::string &output, double seconds) {
    BuildLogData &d = *data_;
    if (step < 0 || static_cast<size_t>(step) >= d.states.size()) return std::vector<std::string>();
    StepState &s = d.states[step];
    s.done = true;
    s.exitCode = exitCode;
    s.output = output;
    s.seconds = seconds;
    return d.due();
}

std::vector<std::string> BuildLog::upToDate(int step) {
    BuildLogData &d = *data_;
    if (step < 0 || static_cast<size_t>(step) >= d.states.size()) return std::vector<std::string>();
    d.states[step].done = true;
    d.states[step].upToDate = true;
    return d.due();
}

std::vector<std::string> BuildLog::end(double elapsed) {
    BuildLogData &d = *data_;
    std::vector<std::string> r;
    char count[96];
    std::snprintf(count, sizeof count, "%d error%s, %d warning%s   %.1f s", d.errors, d.errors == 1 ? "" : "s",
                  d.warnings, d.warnings == 1 ? "" : "s", elapsed >= 0 ? elapsed : d.seconds);
    bool whole = d.next == d.items.size() && !d.failed && d.plan.runnable();
    if (whole) {
        d.add(r, padded(d.plan.program.empty() ? "done" : "built", 14) +
                 (d.plan.program.empty() ? std::string() : d.shownPath(d.plan.program) + "   ") + count);
    } else {
        for (size_t i = d.next; i < d.items.size(); ++i)
            if (!d.items[i].released) d.add(r, padded("not run", 14) + d.items[i].brief);
        d.add(r, padded("failed", 14) + count);
    }
    d.save();
    return r;
}

std::string BuildLog::text() const { return data_->text; }
std::string BuildLog::file() const { return data_->file; }
int BuildLog::errors() const { return data_->errors; }
int BuildLog::warnings() const { return data_->warnings; }

}}
