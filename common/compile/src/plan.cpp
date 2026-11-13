// plan.cpp - the planner: a project, a configuration, a target and an action in; the ordered steps out.
// The rules are the design's section 4, in its order; every name, flag and folder comes from toolchain.json.
#include "context.h"

#include <map>
#include <set>

namespace gride { namespace compile {

namespace {

void say(Plan &plan, const std::string &severity, const std::string &file, const std::string &text) {
    Problem p;
    p.severity = severity;
    p.file = file;
    p.text = text;
    plan.problems.push_back(p);
}

std::string get(const Named &n, const std::string &key) {
    Named::const_iterator it = n.find(key);
    return it == n.end() ? std::string() : it->second;
}

// "-I" + dir is one word; "-l " + name is two (the flag ends in a blank)
void flagged(Words &into, const std::string &flag, const std::string &value) {
    if (!flag.empty() && flag[flag.size() - 1] == ' ') { into.push_back(trim(flag)); into.push_back(value); }
    else into.push_back(flag + value);
}

Words one(const std::string &s) { return Words(1, s); }

std::string stampOf(const Step &s) {
    std::string all = s.program;
    for (size_t i = 0; i < s.argv.size(); ++i) all += '\n' + s.argv[i];
    for (size_t i = 0; i < s.environment.size(); ++i) all += '\n' + s.environment[i].first + '=' + s.environment[i].second;
    return stampHash(all);
}

struct Source {
    ProjectFile file;
    const LanguageDef *language;
    const ToolDef *compiler;          // null for assembly
    std::string why, name;            // which rule chose the compiler; the object's name
    Source() : language(0), compiler(0) {}
};

// object and assembly names: the base name of the source when it is the only one of that name in the build,
// else a name made from its path in the project (src\a.c -> src_a), so two a.c never share one a.obj
void nameObjects(std::vector<Source> &sources) {
    std::map<std::string, int> count;
    for (size_t i = 0; i < sources.size(); ++i) ++count[lower(stem(sources[i].file.path))];
    std::set<std::string> used;
    for (size_t i = 0; i < sources.size(); ++i) {
        std::string s = stem(sources[i].file.path);
        if (count[lower(s)] == 1) { sources[i].name = s; used.insert(lower(s)); }
    }
    for (size_t i = 0; i < sources.size(); ++i) {
        if (!sources[i].name.empty()) continue;
        std::string rel = slashes(sources[i].file.relative);
        if (absolute(rel)) rel = leaf(rel);
        size_t dot = rel.find_last_of('.');
        if (dot != std::string::npos && rel.find('\\', dot) == std::string::npos) rel.erase(dot);
        std::string made;
        for (size_t k = 0; k < rel.size(); ++k) {
            char c = rel[k];
            bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || (c & 0x80);
            made += keep ? c : '_';
        }
        size_t first = made.find_first_not_of('_');
        made = first == std::string::npos ? std::string("file") : made.substr(first);
        std::string unique = made;
        for (int n = 2; used.count(lower(unique)); ++n) unique = made + "_" + std::to_string(n);
        used.insert(lower(unique));
        sources[i].name = unique;
    }
}

// whether the archive or object at path names Microsoft's C runtime by /DEFAULTLIB, as a library cl built does
bool namesMicrosoftRuntime(FileSystem &fs, const std::string &path, const Words &runtimes) {
    std::string bytes;
    if (!fs.isFile(path) || !fs.read(path, bytes) || bytes.size() > 64u * 1024 * 1024) return false;
    std::string text = lower(bytes);
    for (size_t at = text.find("defaultlib:"); at != std::string::npos; at = text.find("defaultlib:", at + 11)) {
        if (at == 0 || (text[at - 1] != '/' && text[at - 1] != '-')) continue;
        size_t i = at + 11;
        if (i < text.size() && text[i] == '"') ++i;
        size_t start = i;
        while (i < text.size() && text[i] != '"' && static_cast<unsigned char>(text[i]) > ' ') ++i;
        std::string name = text.substr(start, i - start);
        if (endsWith(name, ".lib")) name.erase(name.size() - 4);
        if (contains(runtimes, name)) return true;
    }
    return false;
}

class Planner {
public:
    Planner(const WorkspaceData &w, const Request &r) : w_(w), r_(r), cx_(w), d_(w.defs), t_(0), debug_(true) {}

    Plan run();

private:
    const WorkspaceData &w_;
    const Request &r_;
    Context cx_;
    const Definitions &d_;
    Plan plan_;
    Project p_;
    const TargetDef *t_;
    Configuration c_;
    std::string config_, configName_;  // the kind (Debug | Release) and the name as asked for
    bool debug_;
    std::string outDir_, objDir_;
    Named names_;                      // what a project's lists may use as $(NAME)
    Table table_;
    std::set<std::string> said_;
    std::string runtime_;              // the runtime the link takes, for its line in the log

    void error(const std::string &text, const std::string &file = std::string()) { say(plan_, "error", file, text); }
    void warn(const std::string &text) { if (said_.insert(text).second) say(plan_, "warning", std::string(), text); }
    void note(const std::string &text) { if (said_.insert(text).second) plan_.notes.push_back(text); }

    std::string filled(const std::string &entry);
    Words filled(const Words &entries);
    std::string inProject(const std::string &entry) { return combine(p_.folder, filled(entry)); }
    void makeNames();

    bool usesArch(const ToolDef &tool) const { return !tool.archFlag.empty(); }
    bool codeViewTarget(const ToolDef &tool) const { return debug_ && same(t_->debugInfo, "codeview") && usesArch(tool); }
    bool ownCodeView(const ToolDef &tool) const { return codeViewTarget(tool) && !get(tool.asmSyntaxFlags, "ownCodeView").empty(); }
    bool microsofts(const ToolDef &tool) const { return tool.role == "hostCompiler" && tool.lookupVcvars; }
    bool hostCompiler(const ToolDef &tool) const { return tool.role == "hostCompiler" || tool.role == "hostCxx"; }

    bool optionApplies(const OptionDef &o, const ToolDef &tool) const;
    Words configFlags(const ToolDef &tool, bool lineTable);
    void optionEnvironment(const ToolDef &tool, Step &step);
    void machineEnvironment(const ToolDef &tool, Step &step);
    Words includeFlags(const ToolDef &tool);

    const ToolDef *choose(Source &s);
    std::string locate(const ToolDef &tool, const std::string &forFile);
    Step make(const std::string &kind, const ToolDef &tool, const std::string &operation, const std::string &forFile, bool &ok);
    void finish(Step &step, const ToolDef &tool);

    void link(const std::vector<Source> &sources, const Words &objects, const std::string &unit, bool clang, bool native);
    bool hostLibraries(const std::vector<Source> &sources, const std::string &unit, const ToolDef &linker, Step &step);
    bool emulatedLibraries(const std::string &unit);
};

// ---- names a project's lists may use ----

void Planner::makeNames() {
    struct Add {
        static void to(Named &n, const std::string &key, const std::string &value) {
            if (!value.empty() && !n.count(key)) n[key] = value;
        }
        static std::string folder(const std::string &f) { return f.empty() ? f : tidy(f) + "\\"; }
    };
    for (Named::const_iterator it = p_.paths.begin(); it != p_.paths.end(); ++it) Add::to(names_, it->first, it->second);
    for (Named::const_iterator it = cx_.settings.paths.begin(); it != cx_.settings.paths.end(); ++it) Add::to(names_, it->first, it->second);
    Add::to(names_, "ProjectDir", Add::folder(p_.folder));
    Add::to(names_, "WorkspaceDir", Add::folder(r_.workspaceFolder.empty() ? p_.folder : r_.workspaceFolder));
    Add::to(names_, "Config", configName_);
    Add::to(names_, "Arch", t_->arch);
    Add::to(names_, "GrideRoot", Add::folder(cx_.installRoot()));
    Add::to(names_, "GrideRoot", Add::folder(cx_.programFolder));
    Add::to(names_, "GrideInclude", Add::folder(cx_.installDir(d_.installInclude, cx_.settings.include)));
    Add::to(names_, "GrideInclude", Add::folder(combine(cx_.programFolder, d_.installInclude)));
    Add::to(names_, "GrideLib", Add::folder(cx_.installDir(d_.installLib, cx_.settings.lib)));
    Add::to(names_, "GrideLib", Add::folder(combine(cx_.programFolder, d_.installLib)));
    for (size_t i = 0; i < d_.libraries.size(); ++i) Add::to(names_, d_.libraries[i].id, Add::folder(cx_.findLibraryDir(d_.libraries[i])));
}

// an entry of a project or of the settings with its $(NAME)s and %NAME%s filled in; a name nobody knows is an
// error that names it and the entry - a compiler given a $(NAME) says something far stranger
std::string Planner::filled(const std::string &entry) {
    std::string s = entry;
    for (int round = 0; round < 8; ++round) {
        Words used = namesIn(s);
        if (used.empty()) break;
        bool changed = false;
        for (size_t i = 0; i < used.size(); ++i) {
            Named::const_iterator it = names_.find(used[i]);
            if (it == names_.end()) {
                error("$(" + used[i] + ") in \"" + entry + "\" is not a known name: name it under \"paths\" in the .pro or in settings.json", p_.file);
                return s;
            }
            s = replaceAll(s, "$(" + used[i] + ")", it->second);
            changed = true;
        }
        if (!changed) break;
    }
    return cx_.expand(s);
}

Words Planner::filled(const Words &entries) {
    Words r;
    for (size_t i = 0; i < entries.size(); ++i) if (!trim(entries[i]).empty()) r.push_back(filled(trim(entries[i])));
    return r;
}

// ---- the Compiler Options ----

bool Planner::optionApplies(const OptionDef &o, const ToolDef &tool) const {
    if (same(o.needs, "lineTable")) return !usesArch(tool) || !t_->debugInfo.empty();
    if (same(o.needs, "emulated")) return t_->emulated;
    if (same(o.needs, "notEmulated")) return !t_->emulated;
    return true;
}

// The flags a compiler is given for the configuration and target, in the order the tool lists its options: a
// check's flagOn / flagOff, a choice's flag unless it is the silent one, a text list's flagEach per entry; an
// option that does not apply to the target writes nothing. A compiler that lists no options takes its
// configFlags. `lineTable` false leaves -g out: a Debug build whose line table nothing here can carry.
Words Planner::configFlags(const ToolDef &tool, bool lineTable) {
    Words r;
    if (tool.options.empty() || d_.options.empty()) {
        std::map<std::string, Named>::const_iterator by = tool.configFlagsByTarget.find(t_->arch);
        std::string flags = by != tool.configFlagsByTarget.end() && by->second.count(config_) ? get(by->second, config_) : get(tool.configFlags, config_);
        r = cutWords(flags);
        if (debug_ && usesArch(tool) && t_->debugInfo.empty()) lineTable = false;
    } else {
        for (size_t i = 0; i < tool.options.size(); ++i) {
            const OptionDef *o = d_.option(tool.options[i]);
            if (!o || !o->environment.empty() || !optionApplies(*o, tool)) continue;
            std::string value = p_.option(d_, config_, o->id);
            Words add;
            if (same(o->control, "check")) {
                if (value == "1" && !o->flagOn.empty()) add = cutWords(o->flagOn);
                else if (value == "0" && !o->flagOff.empty()) add = cutWords(o->flagOff);
            } else if (same(o->control, "choice")) {
                if (!value.empty() && value != o->silent && !o->flag.empty()) add = cutWords(replaceAll(o->flag, "$(Value)", value));
            } else if (!o->flagEach.empty()) {
                Words items = split(value, ";\r\n");
                for (size_t k = 0; k < items.size(); ++k) add.push_back(replaceAll(o->flagEach, "$(Item)", filled(items[k])));
            }
            for (size_t k = 0; k < add.size(); ++k) r.push_back(add[k]);
        }
    }
    if (!lineTable) {
        Words kept;
        for (size_t i = 0; i < r.size(); ++i) if (r[i] != "-g") kept.push_back(r[i]);
        r = kept;
    }
    return r;
}

// the variables a Compiler Option sets (CPP11_DECLINES): set when the option is on, absent when it is off
void Planner::optionEnvironment(const ToolDef &tool, Step &step) {
    for (size_t i = 0; i < tool.options.size(); ++i) {
        const OptionDef *o = d_.option(tool.options[i]);
        if (!o || o->environment.empty()) continue;
        std::string value = p_.option(d_, config_, o->id);
        if (same(o->control, "check")) { if (value != "1") continue; }
        else if (value.empty()) continue;
        step.environment.push_back(std::make_pair(o->environment, value));
    }
}

// A tool that is documented to read INCLUDE, LIB or PATH (toolchain.json's "environment") is given Visual
// Studio's folders there, in front of what the variable already holds. No other tool is given them.
void Planner::machineEnvironment(const ToolDef &tool, Step &step) {
    if (!t_->host) return;
    const VisualStudio &vs = cx_.visualStudio();
    if (!vs.found) return;
    for (size_t i = 0; i < tool.environmentNames.size(); ++i) {
        const std::string &name = tool.environmentNames[i];
        Words dirs = name == "INCLUDE" ? vs.includeDirs() : name == "LIB" ? vs.libDirs() : name == "PATH" ? vs.binDirs() : Words();
        if (dirs.empty()) continue;
        std::string already = cx_.env.variable(name);
        step.environment.push_back(std::make_pair(name, join(dirs, ";") + (already.empty() ? std::string() : ";" + already)));
    }
}

// the header folders a compiler is given: the configuration's, the .pro's "include", the settings' shared
// ones, then the installation's own that the compiler reads; a compiler with no include flag takes none, and
// the machine's own compiler is not given the installation's (they would stand in front of its own headers)
Words Planner::includeFlags(const ToolDef &tool) {
    Words r, dirs;
    if (tool.includeFlag.empty()) return r;
    for (size_t i = 0; i < c_.includeDirs.size(); ++i) dirs.push_back(inProject(c_.includeDirs[i]));
    for (size_t i = 0; i < p_.include.size(); ++i) if (!trim(p_.include[i]).empty()) dirs.push_back(inProject(trim(p_.include[i])));
    Words shared = filled(cx_.settings.includes);
    for (size_t i = 0; i < shared.size(); ++i) dirs.push_back(shared[i]);
    Words own;
    for (size_t i = 0; i < tool.installIncludeDirs.size(); ++i) {
        const std::string &which = tool.installIncludeDirs[i];
        std::string dir = same(which, d_.installLib) || same(which, "lib") ? cx_.installDir(d_.installLib, cx_.settings.lib)
                        : same(which, d_.installInclude) || same(which, "include") ? cx_.installDir(d_.installInclude, cx_.settings.include)
                        : cx_.installDir(which, std::string());
        if (!dir.empty()) addOnce(own, dir);
    }
    if (hostCompiler(tool)) {
        Words theirs, kept;
        std::string a = cx_.installDir(d_.installInclude, cx_.settings.include), b = cx_.installDir(d_.installLib, cx_.settings.lib);
        for (size_t i = 0; i < dirs.size(); ++i) {
            if ((!a.empty() && samePath(dirs[i], a)) || (!b.empty() && samePath(dirs[i], b))) addOnce(theirs, dirs[i]);
            else kept.push_back(dirs[i]);
        }
        dirs = kept;
        if (!theirs.empty()) note(join(theirs, " and ") + " not given to " + tool.name + ": the installation's headers would stand in front of its own");
    } else {
        for (size_t i = 0; i < own.size(); ++i) if (!contains(dirs, own[i])) dirs.push_back(own[i]);
    }
    for (size_t i = 0; i < dirs.size(); ++i) flagged(r, tool.includeFlag, dirs[i]);
    return r;
}

// ---- rule 3: the compiler of a source ----
// The group's word; else the project's; else the settings'; else auto - the compiler whose role is the
// language's on the target. A named compiler that does not take the language is refused, except one that takes
// several languages (cl): a file of another language then goes to its own compiler.
const ToolDef *Planner::choose(Source &s) {
    const LanguageDef &lang = *s.language;
    const Group *g = p_.group(s.file.group);
    std::string word, from;
    if (g && !g->toolchain.empty() && !same(g->toolchain, "auto")) { word = g->toolchain; from = "the group " + g->name + " names " + word; }
    else if (!p_.toolchain.empty() && !same(p_.toolchain, "auto")) { word = p_.toolchain; from = "the project names " + word; }
    else if (!cx_.settings.compiler.empty() && !same(cx_.settings.compiler, "auto")) { word = cx_.settings.compiler; from = "the settings name " + word; }
    std::string ext = extension(s.file.path);
    const ToolDef *own = d_.toolFor(*t_, lang.role);
    if (!word.empty()) {
        const ToolDef *named = d_.toolOfWord(word);
        if (!named) { error("\"" + word + "\" is not a compiler toolchain.json knows (" + from + ")", s.file.path); return 0; }
        if (named->takes(lang.name)) {
            if (!named->serves(t_->arch)) { error(named->name + " does not build for " + t_->arch + " (" + from + ")", s.file.path); return 0; }
            s.why = from;
            return named;
        }
        if (named->languages.size() < 2) {
            error(named->name + " does not compile " + lang.name + ": " + s.file.relative + " (" + from + ")", s.file.path);
            return 0;
        }
        s.why = "by extension (" + ext + " is " + lang.name + "; " + named->name + ", which " + from.substr(0, from.find(" names ")) + " names, does not take it)";
    } else {
        s.why = "by extension (" + ext + " is " + lang.name + "; " +
                (p_.toolchain.empty() ? "the project names no compiler" : "the project says " + p_.toolchain) + ")";
    }
    if (!own) error("toolchain.json names no " + lang.role + " for " + t_->arch + ": nothing compiles " + s.file.relative, s.file.path);
    return own;
}

// rule 7: where a tool is; not found, it stays in the plan under its file name with every place looked in said
std::string Planner::locate(const ToolDef &tool, const std::string &forFile) {
    Words looked;
    std::string found = cx_.findTool(tool, t_, &looked);
    if (!found.empty()) return found;
    std::string text = tool.name + " not found (looked in " + join(looked, ", ") + ")";
    if (said_.insert(text).second) error(text, forFile);
    return tool.exe;
}

void Planner::finish(Step &step, const ToolDef &tool) {
    step.tool = tool.id;
    step.directory = p_.folder;
    step.encoding = tool.outputEncoding;
    step.diagnostics = tool.diagnostics;
    step.stamp = stampOf(step);
}

Step Planner::make(const std::string &kind, const ToolDef &tool, const std::string &operation, const std::string &forFile, bool &ok) {
    Step step;
    step.kind = kind;
    const OperationDef *op = tool.operation(operation);
    if (!op) { error("toolchain.json: " + tool.id + " has no \"" + operation + "\" operation", forFile); ok = false; return step; }
    step.program = locate(tool, forFile);
    Words unknown;
    step.argv = fill(op->args, table_, &unknown);
    for (size_t i = 0; i < unknown.size(); ++i)
        error("toolchain.json: $(" + unknown[i] + ") in " + tool.id + "'s " + operation + " is not a placeholder", forFile);
    ok = true;
    return step;
}

// ---- the build ----

Plan Planner::run() {
    for (size_t i = 0; i < w_.problems.size(); ++i) plan_.problems.push_back(w_.problems[i]);
    if (!plan_.runnable()) return plan_;

    t_ = d_.target(r_.target);
    if (!t_) { error("toolchain.json knows no target \"" + r_.target + "\""); return plan_; }
    std::string why;
    if (!readProject(r_.project, cx_.fs, p_, why)) { error(why, r_.project); return plan_; }
    // A configuration has a name and a kind. The kind is Debug unless the name says Release (Grey's rule, which
    // lets a project name its own - "Release-small"); the Compiler Options, the libraries and the linker's flags
    // go by the kind, the folders and the project's stored configuration by the name.
    configName_ = trim(r_.configuration).empty() ? std::string("Debug") : trim(r_.configuration);
    config_ = lower(configName_).find("release") != std::string::npos ? "Release" : "Debug";
    debug_ = config_ == "Debug";

    // rule 1: a project that names what it builds for builds for nothing else
    Words allowed = p_.targets;
    if (!p_.arch.empty()) allowed.push_back(p_.arch);
    if (!allowed.empty() && !contains(allowed, t_->arch) && !contains(allowed, t_->ideTarget)) {
        say(plan_, "note", p_.file, p_.name + " builds for " + join(allowed, ", ") + " only - nothing to do for " + t_->arch);
        return plan_;
    }

    c_ = p_.configuration(configName_, *t_);
    std::string folders[2] = { d_.outputFolder, d_.objectFolder };
    for (int i = 0; i < 2; ++i) {
        folders[i] = replaceAll(replaceAll(replaceAll(folders[i], "$(Config)", configName_), "$(TargetFolder)", t_->folder), "$(Arch)", t_->arch);
    }
    outDir_ = combine(p_.folder, c_.outDir.empty() ? folders[0] : c_.outDir);
    objDir_ = combine(p_.folder, c_.objDir.empty() ? folders[1] : c_.objDir);
    plan_.outputFolder = outDir_;
    plan_.objectFolder = objDir_;
    std::string programName = c_.outputName.empty() ? p_.programName() : c_.outputName;
    plan_.program = combine(outDir_, programName + t_->programExtension);
    makeNames();

    if (r_.action == Clean || r_.action == Rebuild) {
        // Clean empties a folder, so the folder must lie strictly inside the project's
        std::string both[2] = { objDir_, outDir_ };
        for (int i = 0; i < 2; ++i) {
            if (!under(p_.folder, both[i]) || samePath(p_.folder, both[i]))
                error("Clean refused: " + both[i] + " is not a folder below the project's folder " + p_.folder);
            else addOnce(plan_.cleans, both[i]);
        }
        if (r_.action == Clean || !plan_.runnable()) return plan_;
    }
    if (r_.action != Build && r_.action != Rebuild && r_.action != CompileFile) {
        error("the block does not plan this action yet (run, emulate, simulate, debug and convert are the design's step 7)");
        return plan_;
    }

    // ---- the files (3.4), and of them the sources of this build ----
    Words never;
    if (!samePath(outDir_, p_.folder)) never.push_back(outDir_);
    if (!samePath(objDir_, p_.folder)) never.push_back(objDir_);
    ProjectFiles all = resolveFiles(p_, cx_.fs, never);
    for (size_t i = 0; i < all.listed.size(); ++i) if (p_.builds(all.listed[i].group)) plan_.listed.push_back(all.listed[i]);

    std::vector<Source> sources;
    Words headers;
    for (size_t i = 0; i < all.files.size(); ++i) {
        const ProjectFile &f = all.files[i];
        if (d_.header(f.path)) { if (!f.missing) headers.push_back(f.path); continue; }
        if (!p_.builds(f.group) || excluded(p_.excludeFromBuild, f.relative)) continue;
        const LanguageDef *lang = d_.languageOf(f.path);
        if (!lang) continue;                      // a group can hold anything: notes, a Makefile
        if (f.missing) { error("the build names " + f.relative + ", and it is not there: put it back, or take it out of the group " + f.group, f.path); continue; }
        Source s;
        s.file = f;
        s.language = lang;
        sources.push_back(s);
    }
    if (r_.action == CompileFile) {
        std::string single = tidy(r_.file);
        const LanguageDef *lang = d_.languageOf(single);
        if (!lang) { error(leaf(single) + " is not a source any compiler here takes", single); return plan_; }
        bool there = false;
        for (size_t i = 0; i < sources.size(); ++i) if (samePath(sources[i].file.path, single)) there = true;
        if (!there) {
            Source s;
            s.file.path = single;
            s.file.relative = forward(relative(p_.folder, single));
            s.language = lang;
            sources.push_back(s);
        }
    }
    if (!plan_.runnable()) return plan_;
    nameObjects(sources);

    // ---- the macros of the whole build ----
    table_["Arch"] = one(t_->arch);
    table_["Config"] = one(configName_);
    table_["Target"] = one(t_->ideTarget);
    table_["ProjectDir"] = one(p_.folder);
    table_["ProjectName"] = one(p_.name);
    table_["WorkspaceDir"] = one(r_.workspaceFolder.empty() ? p_.folder : r_.workspaceFolder);
    table_["ObjDir"] = one(objDir_);
    table_["OutDir"] = one(outDir_);
    table_["Output"] = one(plan_.program);
    table_["Program"] = one(plan_.program);
    table_["Arguments"] = r_.arguments.empty() ? p_.arguments : r_.arguments;
    table_["Optimise"] = Words();
    table_["ToolDir"] = Words();
    table_["Runtime"] = Words();
    table_["CompileFlags"] = cutWords(c_.compileFlags);

    // a Shalimar program is one compilation of every Shalimar file of the build; Compile File takes one alone
    std::vector<size_t> unit;
    if (r_.action != CompileFile)
        for (size_t i = 0; i < sources.size(); ++i) if (d_.compiledAsOne(*sources[i].language)) unit.push_back(i);
    std::string unitLanguage = unit.empty() ? std::string() : sources[unit[0]].language->name;
    bool asUnit = unit.size() > 1;

    bool native = r_.native;
    bool cross = !t_->host && !t_->emulated;       // another machine's target: only its assembly is written here
    bool clang = false;                            // a Debug build that goes through clang's CodeView
    Words objects;
    std::vector<Step> compiles, assembles;
    bool unitDone = false;

    for (size_t i = 0; i < sources.size(); ++i) {
        Source &s = sources[i];
        bool assembly = d_.assembly(*s.language);
        bool inUnit = asUnit && d_.compiledAsOne(*s.language);
        if (inUnit && unitDone) continue;
        if (r_.action == CompileFile && !samePath(s.file.path, r_.file)) continue;
        std::string name = inUnit ? programName : s.name;
        std::string asmFile = assembly ? s.file.path : combine(objDir_, name + t_->assemblyExtension);
        std::string objFile = combine(objDir_, name + t_->objectExtension);
        Words unitFiles, unitNames;
        if (inUnit) {
            unitDone = true;
            for (size_t k = 0; k < unit.size(); ++k) { unitFiles.push_back(sources[unit[k]].file.path); unitNames.push_back(leaf(sources[unit[k]].file.path)); }
        }
        table_["Source"] = one(s.file.path);
        table_["Sources"] = inUnit ? unitFiles : one(s.file.path);
        table_["SourceDir"] = one(parent(s.file.path));
        table_["Name"] = one(name);
        table_["AsmFile"] = one(asmFile);
        table_["ObjFile"] = one(objFile);

        const ToolDef *compiler = 0;
        bool writesObject = false, viaClang = false;
        if (!assembly) {
            compiler = choose(s);
            if (!compiler) continue;
            s.compiler = compiler;
            const OperationDef *op = compiler->operation("compileToAssembly");
            writesObject = op && op->args.find("$(ObjFile)") != std::string::npos;

            // rule 4: a Debug build on a target carrying CodeView asks the compiler for its own and stays on
            // GRIDE's assembler and linker; only a compiler without it goes to the Debug assembler and linker
            bool lineTable = true;
            std::string syntax;
            if (!writesObject && ownCodeView(*compiler)) syntax = get(compiler->asmSyntaxFlags, "ownCodeView");
            else if (!writesObject && codeViewTarget(*compiler) && !native) {
                const ToolDef *carrier = d_.toolFor(*t_, "debugAssembler");
                if (carrier && !cx_.findTool(*carrier, t_, 0).empty()) { viaClang = true; clang = true; syntax = get(compiler->asmSyntaxFlags, "codeview"); }
                else {
                    lineTable = false;
                    syntax = get(compiler->asmSyntaxFlags, "assemblerNamed");
                    warn((carrier ? carrier->name : std::string("the Debug assembler")) + " not found - this Debug build has no source line table for " +
                         compiler->name + "'s files: the target's assembler and linker make the program, and the debugger shows no lines");
                }
            } else if (t_->host) syntax = get(compiler->asmSyntaxFlags, "assemblerNamed");

            table_["ArchFlag"] = cutWords(replaceAll(compiler->archFlag, "$(Arch)", t_->arch));
            table_["ConfigFlags"] = configFlags(*compiler, lineTable);
            table_["AsmSyntaxFlag"] = cutWords(syntax);
            table_["LangFlags"] = cutWords(get(compiler->languageFlags, s.language->name));
            table_["IncludeFlags"] = includeFlags(*compiler);
            Words defines;
            if (!compiler->defineFlag.empty()) {
                Words named = filled(c_.defines);
                for (size_t k = 0; k < named.size(); ++k) flagged(defines, compiler->defineFlag, named[k]);
            }
            table_["DefineFlags"] = defines;

            bool ok;
            Step step = make("compile", *compiler, "compileToAssembly", s.file.path, ok);
            if (!ok) continue;
            step.shown = inUnit ? join(unitNames, " ") : leaf(s.file.path);
            step.why = "chosen: " + s.why;
            step.inputs = inUnit ? unitFiles : one(s.file.path);
            for (size_t k = 0; k < headers.size(); ++k) step.inputs.push_back(headers[k]);
            step.outputs = one(writesObject ? objFile : asmFile);
            step.wave = 0;
            optionEnvironment(*compiler, step);
            machineEnvironment(*compiler, step);
            finish(step, *compiler);
            compiles.push_back(step);
        }

        if (cross) continue;
        if (t_->emulated && p_.emulateOnly) continue;       // the assembly is the program: the emulator runs it
        objects.push_back(objFile);
        if (writesObject) continue;

        const ToolDef *as = 0;
        std::string asWhy;
        if (native && !t_->emulated && d_.toolFor(*t_, "nativeAssembler")) { as = d_.toolFor(*t_, "nativeAssembler"); asWhy = "the machine's own assembler, asked for"; }
        else if (viaClang) { as = d_.toolFor(*t_, "debugAssembler"); asWhy = "the Debug assembler: " + compiler->name + " writes no CodeView of its own"; }
        else { as = d_.toolFor(*t_, "assembler"); asWhy = "the target's assembler"; }
        if (!as) { error("toolchain.json names no assembler for " + t_->arch, asmFile); continue; }
        bool ok;
        Step step = make("assemble", *as, "assemble", asmFile, ok);
        if (!ok) continue;
        step.shown = leaf(asmFile);
        step.why = asWhy;
        step.inputs = one(asmFile);
        step.outputs = one(objFile);
        step.wave = 1;
        machineEnvironment(*as, step);
        finish(step, *as);
        assembles.push_back(step);
    }

    for (size_t i = 0; i < compiles.size(); ++i) plan_.steps.push_back(compiles[i]);
    for (size_t i = 0; i < assembles.size(); ++i) plan_.steps.push_back(assembles[i]);
    plan_.asksNative = cx_.settings.askNative && !native && (d_.toolFor(*t_, "nativeAssembler") || d_.toolFor(*t_, "nativeLinker"));

    if (r_.action == CompileFile || !plan_.runnable()) return plan_;
    if (cross) {
        note("assembly is written to " + relative(p_.folder, objDir_) + " - nothing on this machine assembles or links for " + t_->arch);
        plan_.program.clear();
        return plan_;
    }
    if (t_->emulated && p_.emulateOnly) {
        note("no " + t_->programExtension + ": the project says emulateOnly - Run is Emulate");
        plan_.program.clear();
        return plan_;
    }
    if (objects.empty()) {
        error("project " + p_.name + " has no source files to build" +
              (p_.namesBuildGroups ? " in " + join(p_.buildGroups, ", ") + " (the \"build\" entry's groups)" : std::string()), p_.file);
        return plan_;
    }
    if (p_.kind.find("Library") != std::string::npos) {
        note("a library project: its objects are kept in " + relative(p_.folder, objDir_));
        plan_.program.clear();
        return plan_;
    }
    link(sources, objects, unitLanguage, clang, native);
    return plan_;
}

// ---- rule 5: the runtime and the libraries of a host link ----
// The Windows C runtime is GRIDE's or Microsoft's. The settings say which; automatic is Microsoft's only where
// Visual Studio is found and the link has cl's objects or a library that names Microsoft's runtime by
// /DEFAULTLIB, and GRIDE's otherwise.
bool Planner::hostLibraries(const std::vector<Source> &sources, const std::string &unit, const ToolDef &linker, Step &step) {
    (void)step;
    Words dirs;
    for (size_t i = 0; i < c_.libDirs.size(); ++i) flagged(dirs, t_->libraryDirFlag, inProject(c_.libDirs[i]));
    table_["LibDirFlags"] = dirs;

    Words libs = filled(c_.libs);
    for (size_t i = 0; i < p_.libraries.size(); ++i) if (!trim(p_.libraries[i]).empty()) libs.push_back(inProject(trim(p_.libraries[i])));
    Words shared = filled(cx_.settings.libraries);
    for (size_t i = 0; i < shared.size(); ++i) addOnce(libs, shared[i]);

    bool clObjects = false, otherObjects = false;
    for (size_t i = 0; i < sources.size(); ++i) {
        if (sources[i].compiler && microsofts(*sources[i].compiler)) clObjects = true;
        else otherObjects = true;
    }

    std::string crt;
    Words crtLibs;
    const LibraryDef *grt = d_.libraryOfKind(t_->arch, "hostCrt", "grt");
    if (grt && !grt->directory.empty()) {
        // a library the link takes that names Microsoft's runtime: found as the link finds it
        const LibraryDef *msvc = d_.libraryOfKind(t_->arch, "hostCrt", "msvc");
        bool microsoftLibrary = false;
        Words where;
        where.push_back(p_.folder);
        for (size_t i = 0; i < c_.libDirs.size(); ++i) where.push_back(inProject(c_.libDirs[i]));
        for (size_t i = 0; i < libs.size() && !microsoftLibrary; ++i) {
            std::string path;
            if (absolute(libs[i])) path = libs[i];
            else for (size_t k = 0; k < where.size() && path.empty(); ++k) if (cx_.fs.isFile(combine(where[k], libs[i]))) path = combine(where[k], libs[i]);
            if (!path.empty() && namesMicrosoftRuntime(cx_.fs, path, grt->refuses)) microsoftLibrary = true;
        }
        std::string choice = lower(trim(cx_.env.variable("GRIDE_WINDOWS_RUNTIME")));
        if (choice != "grt" && choice != "msvc") choice = cx_.settings.windowsRuntime;
        bool vs = cx_.visualStudio().found;
        crt = choice != "auto" ? choice : (clObjects || microsoftLibrary) && vs ? "msvc" : "grt";
        if (crt == "msvc" && choice == "auto" && microsoftLibrary && !clObjects)
            note("a library this link takes names Microsoft's C runtime by /DEFAULTLIB - Microsoft's is linked");

        std::string dir = cx_.findLibraryDir(*grt);
        Words files = grt->filesFor(config_);
        bool there = !dir.empty();
        for (size_t i = 0; there && i < files.size(); ++i) if (!cx_.fs.isFile(combine(dir, files[i]))) there = false;
        if (crt == "grt" && !there && choice == "auto" && vs && msvc) {
            note("GRIDE's C runtime is not in " + (dir.empty() ? grt->directory : dir) + " - Microsoft's (Visual Studio's) is linked instead");
            crt = "msvc";
        }
        if (crt == "grt") {
            if (!there) {
                error("GRIDE's C runtime is not there: " + join(files, ", ") + " are looked for in " + grt->directory +
                      " beside GRIDE (Setup installs them; Build Solution writes them into x64\\<Configuration>\\lib)");
                return false;
            }
            for (size_t i = 0; i < files.size(); ++i) crtLibs.push_back(combine(dir, files[i]));
            // a library cl built names Microsoft's runtime by /DEFAULTLIB: with GRIDE's runtime those names are refused
            if (!t_->refuseLibraryFlag.empty())
                for (size_t i = 0; i < grt->refuses.size(); ++i) flagged(crtLibs, t_->refuseLibraryFlag, grt->refuses[i]);
            if (clObjects) warn("cl's objects name Microsoft's C runtime, and GRIDE's runtime is chosen: they may not link");
            runtime_ = grt->name + " (" + config_ + ")";
            plan_.notes.push_back("runtime: " + runtime_ + ", from " + dir);
        } else if (msvc) {
            crtLibs = msvc->filesFor(config_);
            runtime_ = msvc->name + " (" + config_ + ")";
            plan_.notes.push_back("runtime: " + runtime_ + ", found through LIB");
        }
        // every object cl's: they name their own C runtime, so no list is added to theirs
        if (clObjects && !otherObjects && linker.role != "linker") {
            crtLibs.clear();
            note("cl's objects name their own C runtime - " + linker.name + " takes it from them");
        }
    }
    table_["CrtLibs"] = crtLibs;

    // a Shalimar program takes its runtime library first: the one built for the C runtime in force
    if (!unit.empty()) {
        const LibraryDef *shm = d_.libraryWhen(t_->arch, unit, crt);
        Words files = shm ? shm->filesFor(config_) : Words();
        std::string dir = shm ? cx_.findLibraryDir(*shm) : std::string();
        if (!files.empty() && !dir.empty() && cx_.fs.isFile(combine(dir, files[0]))) libs.insert(libs.begin(), combine(dir, files[0]));
        else if (shm) warn(join(files, " ") + " is not beside GRIDE (" + shm->directory + ") - the link may miss the " + unit + " runtime");
    }
    Words libFlags;
    for (size_t i = 0; i < libs.size(); ++i) flagged(libFlags, t_->libraryFlag, libs[i]);
    table_["LibFlags"] = libFlags;
    return true;
}

// The emulated target's link: the C runtime of the configuration for every name a project has for it (and
// alone when it names none), a library's Debug or Release file for every alias of it, the Shalimar runtime
// before them for a Shalimar program, the project's linker command file where it is on disk, else the one of
// toolchain.json written into the object folder.
bool Planner::emulatedLibraries(const std::string &unit) {
    const LibraryDef *rts = d_.library(t_->runtime);
    std::string runtimeDir = rts ? cx_.findLibraryDir(*rts) : std::string();
    if (!rts || runtimeDir.empty()) {
        error("a " + t_->arch + " build links against " + (rts ? rts->name + " in " + rts->directory : std::string("its runtime")) +
              " beside GRIDE, and it is not there (TI's own runtime as a stand-in is not planned by the block yet)");
        return false;
    }
    table_["RuntimeDir"] = one(runtimeDir);
    runtime_ = rts->name + " (" + config_ + ")";
    plan_.notes.push_back("runtime: " + runtime_ + ", from " + runtimeDir);

    Words libs;
    Words named = filled(c_.libs);
    bool runtimeNamed = false;
    for (size_t i = 0; i < named.size(); ++i) {
        std::string l = named[i];
        for (size_t k = 0; k < d_.libraries.size(); ++k) {
            const LibraryDef &def = d_.libraries[k];
            if (!same(def.target, t_->arch) || !def.alias(leaf(l)) || absolute(l)) continue;
            Words files = def.filesFor(config_);
            if (!files.empty()) l = files[0];
            if (def.id == rts->id) runtimeNamed = true;
            break;
        }
        addOnce(libs, l);
    }
    if (!runtimeNamed) { Words files = rts->filesFor(config_); if (!files.empty()) addOnce(libs, files[0]); }
    for (size_t i = 0; i < p_.libraries.size(); ++i) if (!trim(p_.libraries[i]).empty()) addOnce(libs, inProject(trim(p_.libraries[i])));
    if (!unit.empty()) {
        const LibraryDef *shm = d_.libraryWhen(t_->arch, unit);
        Words files = shm ? shm->filesFor(config_) : Words();
        if (!files.empty() && cx_.fs.isFile(combine(runtimeDir, files[0]))) {
            Words rest;
            for (size_t i = 0; i < libs.size(); ++i) if (!same(libs[i], files[0])) rest.push_back(libs[i]);
            libs = rest;
            libs.insert(libs.begin(), files[0]);
        } else warn("the " + unit + " runtime library " + join(files, " ") + " is not in " + runtimeDir + " - the link will miss it");
    }
    Words libFlags;
    for (size_t i = 0; i < libs.size(); ++i) flagged(libFlags, t_->libraryFlag, libs[i]);
    table_["LibFlags"] = libFlags;

    Words dirs;
    for (size_t i = 0; i < c_.libDirs.size(); ++i) {
        std::string dir = inProject(c_.libDirs[i]);
        if (cx_.fs.isDirectory(dir)) flagged(dirs, t_->libraryDirFlag, dir);
        else note("library search path not on this machine, left out: " + c_.libDirs[i]);
    }
    table_["LibDirFlags"] = dirs;

    std::string cmd = c_.linkerCmd.empty() ? std::string() : inProject(c_.linkerCmd);
    if (cmd.empty() || !cx_.fs.isFile(cmd)) {
        const LinkerCommandFileDef *f = d_.linkerCommandFile(t_->linkerCommandFile);
        cmd.clear();
        if (f && !f->name.empty()) {
            cmd = combine(objDir_, f->name);
            plan_.files.push_back(std::make_pair(cmd, f->text));
        }
    }
    table_["LinkerCmd"] = cmd.empty() ? Words() : one(cmd);
    table_["CrtLibs"] = Words();
    return true;
}

void Planner::link(const std::vector<Source> &sources, const Words &objects, const std::string &unit, bool clang, bool native) {
    bool clObjects = false;
    for (size_t i = 0; i < sources.size(); ++i) if (sources[i].compiler && microsofts(*sources[i].compiler)) clObjects = true;

    // rule 4 and 6: the vendor's when asked; Microsoft's for cl's objects; the Debug linker for a build that
    // went through clang; else the target's
    const ToolDef *ld = 0;
    std::string why;
    if (native && d_.toolFor(*t_, "nativeLinker")) { ld = d_.toolFor(*t_, "nativeLinker"); why = "the machine's own linker, asked for"; }
    if (!ld && clObjects && t_->host) {
        const char *roles[2] = { "debugLinker", "nativeLinker" };
        for (int i = 0; i < 2 && !ld; ++i) {
            const ToolDef *theirs = d_.toolFor(*t_, roles[i]);
            if (theirs && !cx_.findTool(*theirs, t_, 0).empty()) { ld = theirs; why = "Microsoft's linker: cl's objects want its runtime"; }
        }
        if (!ld) warn("Microsoft's link.exe not found - cl's objects are linked with it; the target's linker is used instead");
    }
    if (!ld && clang) {
        const ToolDef *theirs = d_.toolFor(*t_, "debugLinker");
        if (theirs && !cx_.findTool(*theirs, t_, 0).empty()) { ld = theirs; why = "the Debug linker: this build's CodeView came through the Debug assembler"; }
        else warn("the Debug linker was not found - linked with the target's linker, so the debugger will see no lines");
    }
    if (!ld) { ld = d_.toolFor(*t_, "linker"); why = "the target's linker"; }
    if (!ld) { error("toolchain.json names no linker for " + t_->arch); return; }

    Step step;
    table_["Objects"] = objects;
    Words linkFlags = cutWords(c_.linkFlags);
    Words own = cutWords(get(ld->linkFlags, config_));
    for (size_t i = 0; i < own.size(); ++i) addOnce(linkFlags, own[i]);
    table_["LinkFlags"] = linkFlags;
    if (t_->emulated ? !emulatedLibraries(unit) : !hostLibraries(sources, unit, *ld, step)) return;

    bool ok;
    step = make("link", *ld, "link", std::string(), ok);
    if (!ok) return;
    step.shown = leaf(plan_.program);
    step.why = why + (runtime_.empty() ? std::string() : "; runtime: " + runtime_);
    step.inputs = objects;
    step.outputs = one(plan_.program);
    step.wave = 2;
    machineEnvironment(*ld, step);

    // a line Windows would refuse goes into a file of arguments, where the tool takes one
    if (commandLine(step.program, step.argv).size() > 30000 && !ld->responseFile.empty()) {
        std::string file = combine(objDir_, stem(plan_.program) + ".link.rsp");
        std::string text;
        for (size_t i = 0; i < step.argv.size(); ++i) text += commandLine(step.argv[i], Words()) + "\r\n";
        plan_.files.push_back(std::make_pair(file, text));
        Table t;
        t["File"] = one(file);
        step.argv = fill(ld->responseFile, t);
    }
    finish(step, *ld);
    plan_.steps.push_back(step);
}

}

Plan makePlan(const WorkspaceData &w, const Request &r) {
    Planner planner(w, r);
    return planner.run();
}

}}
