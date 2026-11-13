#include "project.h"

#include <algorithm>
#include <set>

namespace gride { namespace compile {

namespace {

Words list(const Json &j) {
    Words r;
    for (size_t i = 0; i < j.size(); ++i) if (j.at(i).is(Json::String)) r.push_back(j.at(i).text());
    return r;
}

Named named(const Json &j) {
    Named r;
    if (!j.is(Json::Object)) return r;
    for (size_t i = 0; i < j.size(); ++i) r[j.keyAt(i)] = j.valueAt(i).text();
    return r;
}

// a key of an object found without regard to case: "debug" / "Debug", "Debug|Windows" / "debug|windows"
const Json *member(const Json &object, const std::string &key) {
    if (!object.is(Json::Object)) return 0;
    for (size_t i = 0; i < object.size(); ++i) if (same(object.keyAt(i), key)) return &object.valueAt(i);
    return 0;
}

}

const Group *Project::group(const std::string &n) const {
    for (size_t i = 0; i < groups.size(); ++i) if (same(groups[i].name, n)) return &groups[i];
    return 0;
}

bool Project::builds(const std::string &g) const { return !namesBuildGroups || contains(buildGroups, g); }

Configuration Project::configuration(const std::string &config, const TargetDef &t) const {
    Configuration c;
    const Json *j = member(configurations, config + "|" + t.ideTarget);
    if (!j) j = member(configurations, config + "|" + t.arch);
    if (!j || !j->is(Json::Object)) {
        // Grey's form, "buildConfigurations": a list, each entry a name with its defines, flags and output folder -
        // for every target alike. Its output folder may use $(Target), $(Config) and $(Name), and the objects go
        // into "obj" under it, as Grey kept them. The folder Grey gave every configuration before the three
        // shared one rule ("build\$(Target)\$(Config)") is not a choice of the project's: the rule's folders stand.
        for (size_t i = 0; i < buildConfigurations.size(); ++i) {
            const Json &b = buildConfigurations.at(i);
            if (!b.is(Json::Object) || !same(b.get("name").text("Debug"), config)) continue;
            c.stored = true;
            c.defines = list(b.get("defines"));
            c.compileFlags = b.get("flags").text();
            c.linkFlags = b.get("linkFlags").text();
            std::string out = slashes(trim(b.get("outputDir").text()));
            if (!out.empty() && !same(out, "build\\$(Target)\\$(Config)")) {
                out = replaceAll(replaceAll(replaceAll(replaceAll(out, "$(Target)", t.ideTarget), "$(TargetFolder)", t.folder), "$(Config)", config), "$(Name)", name);
                c.outDir = out;
                c.objDir = out + "\\obj";
            }
            break;
        }
        return c;
    }
    c.stored = true;
    c.outDir = j->get("outDir").text();
    c.objDir = j->get("objDir").text();
    c.linkerCmd = j->get("linkerCmd").text();
    c.compileFlags = j->get("compileFlags").text();
    c.linkFlags = j->get("linkFlags").text();
    c.outputName = j->get("outputName").text();
    c.includeDirs = list(j->get("includeDirs"));
    c.libDirs = list(j->get("libDirs"));
    c.libs = list(j->get("libs"));
    c.defines = list(j->get("defines"));
    return c;
}

// the project's own value where its "options" has one, else the definition's default for the configuration
std::string Project::option(const Definitions &d, const std::string &config, const std::string &id) const {
    const Json *store = member(options, config);
    if (store) {
        const Json *v = member(*store, id);
        if (v) return v->is(Json::Bool) ? (v->boolean() ? "1" : "0") : v->text();
    }
    const OptionDef *o = d.option(id);
    return o ? o->defaultFor(config) : std::string();
}

bool readProject(const std::string &file, FileSystem &fs, Project &p, std::string &problem) {
    std::string text;
    if (!fs.read(file, text)) { problem = "cannot read " + file; return false; }
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    std::string error;
    Json root = Json::parse(text, error);
    if (!error.empty() || !root.is(Json::Object)) {
        problem = leaf(file) + " is not a project file" + (error.empty() ? std::string() : ": " + error);
        return false;
    }
    p = Project();
    p.file = tidy(file);
    p.folder = parent(p.file);
    p.name = root.get("name").text(stem(file));
    p.toolchain = trim(root.get("toolchain").text());
    p.arch = trim(root.get("arch").text());
    p.language = root.get("language").text();
    p.kind = root.get("template").text();
    p.targets = list(root.get("targets"));
    p.emulateOnly = root.get("emulateOnly").boolean();
    p.include = list(root.get("include"));
    p.libraries = list(root.get("libraries"));
    p.exclude = list(root.get("exclude"));
    p.excludeFromBuild = list(root.get("excludeFromBuild"));
    p.dependsOn = list(root.get("dependsOn"));
    p.paths = named(root.get("paths"));
    p.options = root.get("options");
    // Black's "configurations" is an object keyed "Debug|Windows"; Grey's "buildConfigurations" is a list (a list
    // under "configurations" is how Grey wrote it before)
    const Json &blacks = root.get("configurations");
    if (blacks.is(Json::Object)) p.configurations = blacks;
    p.buildConfigurations = root.get("buildConfigurations").is(Json::Array) ? root.get("buildConfigurations")
                          : blacks.is(Json::Array) ? blacks : Json();

    // "groups": { "Sources": [ ... ] } or { "Sources": { "files": [ ... ], "toolchain": "c90" } }; a list of
    // { "name": ..., "files": ... } is read too
    const Json &groups = root.get("groups");
    for (size_t i = 0; i < groups.size(); ++i) {
        Group g;
        const Json *value;
        if (groups.is(Json::Object)) { g.name = groups.keyAt(i); value = &groups.valueAt(i); }
        else { value = &groups.at(i); g.name = value->get("name").text(); }
        if (value->is(Json::Array)) g.entries = list(*value);
        else if (value->is(Json::Object)) {
            g.entries = list(value->get("files"));
            g.toolchain = trim(value->get("toolchain").text());
        }
        p.groups.push_back(g);
    }

    const Json &build = root.get("build");
    p.target = trim(build.get("target").text());
    p.arguments = list(build.get("args"));
    if (build.has("groups")) { p.namesBuildGroups = true; p.buildGroups = list(build.get("groups")); }
    return true;
}

// ---- files by pattern ----
// An entry of a group is a pattern; a plain name is the pattern that matches one file. A name beats a pattern;
// between two patterns the first group in the file wins; a file is in one group only. The files of one pattern
// are sorted by name without regard to case, so the order is the same everywhere.

namespace {

struct ByName {
    bool operator()(const std::string &a, const std::string &b) const {
        std::string la = lower(a), lb = lower(b);
        return la != lb ? la < lb : a < b;
    }
};

std::string key(const std::string &relativePath) { return lower(forward(tidy(relativePath))); }

}

// an "exclude" entry against a file's project-relative path: the folder part is written out, * and ? are for
// the name
bool excluded(const Words &entries, const std::string &rel) {
    std::string r = forward(rel);
    size_t slash = r.find_last_of('/');
    std::string folder = slash == std::string::npos ? std::string() : r.substr(0, slash);
    std::string name = slash == std::string::npos ? r : r.substr(slash + 1);
    for (size_t i = 0; i < entries.size(); ++i) {
        std::string e = forward(tidy(entries[i]));
        size_t es = e.find_last_of('/');
        std::string ef = es == std::string::npos ? std::string() : e.substr(0, es);
        std::string en = es == std::string::npos ? e : e.substr(es + 1);
        if (same(ef, folder) && matches(en, name)) return true;
    }
    return false;
}

ProjectFiles resolveFiles(const Project &p, FileSystem &fs, const Words &never) {
    ProjectFiles r;
    std::set<std::string> namedFiles, taken;
    for (size_t g = 0; g < p.groups.size(); ++g)
        for (size_t e = 0; e < p.groups[g].entries.size(); ++e)
            if (!wildcard(p.groups[g].entries[e])) namedFiles.insert(key(p.groups[g].entries[e]));

    for (size_t g = 0; g < p.groups.size(); ++g) {
        const Group &group = p.groups[g];
        for (size_t e = 0; e < group.entries.size(); ++e) {
            const std::string &entry = group.entries[e];
            Listed l;
            l.group = group.name;
            l.entry = entry;
            l.pattern = wildcard(entry);
            if (!l.pattern) {
                std::string k = key(entry);
                if (taken.insert(k).second) {
                    ProjectFile f;
                    f.path = combine(p.folder, entry);
                    f.relative = forward(tidy(entry));
                    f.group = group.name;
                    f.entry = entry;
                    f.missing = !fs.isFile(f.path);
                    r.files.push_back(f);
                    l.files.push_back(f.relative);
                }
                r.listed.push_back(l);
                continue;
            }
            std::string e2 = forward(tidy(entry));
            size_t slash = e2.find_last_of('/');
            std::string sub = slash == std::string::npos ? std::string() : e2.substr(0, slash);
            std::string mask = slash == std::string::npos ? e2 : e2.substr(slash + 1);
            if (wildcard(sub)) { r.listed.push_back(l); continue; }       // only the name takes * and ?
            std::string folder = combine(p.folder, sub);
            bool refused = false;
            for (size_t n = 0; n < never.size(); ++n) if (under(never[n], folder)) refused = true;
            if (refused) { r.listed.push_back(l); continue; }
            Words names = fs.list(folder), found;
            for (size_t n = 0; n < names.size(); ++n) {
                const std::string &name = names[n];
                if (name.empty() || name[name.size() - 1] == '\\') continue;          // a folder
                if (!matches(mask, name)) continue;
                if (samePath(combine(folder, name), p.file)) continue;                // the .pro itself
                if (endsWith(name, ".build.log") || endsWith(name, ".build.previous.log") || endsWith(name, ".stamps")) continue;
                found.push_back(name);
            }
            std::sort(found.begin(), found.end(), ByName());
            for (size_t n = 0; n < found.size(); ++n) {
                std::string rel = sub.empty() ? found[n] : sub + "/" + found[n];
                std::string k = key(rel);
                if (namedFiles.count(k) || excluded(p.exclude, rel)) continue;
                if (!taken.insert(k).second) continue;
                ProjectFile f;
                f.path = combine(folder, found[n]);
                f.relative = rel;
                f.group = group.name;
                f.entry = entry;
                r.files.push_back(f);
                l.files.push_back(rel);
            }
            r.listed.push_back(l);
        }
    }
    return r;
}

// ---- settings.json's "build" ----

namespace {

struct Reader {
    const Json &root;
    std::vector<const Json *> old;       // the objects the old keys are in, in the order to try
    std::vector<int> style;              // per object: 0 Black (environment.*), 1 Grey (PascalCase), 2 White (short)

    explicit Reader(const Json &r) : root(r) {}

    const Json *find(const std::string &key, const char *black, const char *grey, const char *white) const {
        const Json &build = root.get("build");
        if (build.has(key)) return &build.get(key);
        for (size_t i = 0; i < old.size(); ++i) {
            const char *name = style[i] == 0 ? black : style[i] == 1 ? grey : white;
            if (name && *name && old[i]->has(name)) return &old[i]->get(name);
        }
        return 0;
    }
    std::string text(const std::string &key, const char *b, const char *g, const char *w, const std::string &fallback = std::string()) const {
        const Json *j = find(key, b, g, w);
        return j && j->is(Json::String) ? trim(j->text()) : fallback;
    }
    Words words(const std::string &key, const char *b, const char *g, const char *w) const {
        const Json *j = find(key, b, g, w);
        return j ? list(*j) : Words();
    }
    bool flag(const std::string &key, const char *b, const char *g, const char *w, bool fallback) const {
        const Json *j = find(key, b, g, w);
        return j && j->is(Json::Bool) ? j->boolean() : fallback;
    }
};

}

BuildSettings readBuildSettings(const Json &settings, const std::string &outlook) {
    BuildSettings s;
    if (!settings.is(Json::Object)) return s;
    Reader r(settings);
    int first = same(outlook, "Grey") ? 1 : same(outlook, "White") ? 2 : 0;
    for (int k = 0; k < 3; ++k) {
        int which = (first + k) % 3;
        r.old.push_back(which == 0 ? &settings.get("environment") : &settings);
        r.style.push_back(which);
    }
    s.root = r.text("root", "grideRoot", "GrideRoot", "root");
    s.include = r.text("include", "grideIncludeDir", "GrideIncludeDir", "include");
    s.lib = r.text("lib", "grideLibDir", "GrideLibDir", "lib");
    s.includes = r.words("includes", "sharedIncludes", "SharedIncludes", "includes");
    s.libraries = r.words("libraries", "sharedLibraries", "SharedLibraries", "libraries");
    s.compiler = r.text("compiler", "defaultCompiler", "DefaultCompiler", "compiler", "auto");
    s.windowsRuntime = lower(r.text("windowsRuntime", "windowsRuntime", "WindowsRuntime", "windowsRuntime", "auto"));
    if (s.windowsRuntime != "grt" && s.windowsRuntime != "msvc") s.windowsRuntime = "auto";
    s.vcvars = r.text("vcvars", "vcvars", "Vcvars", "vcvars");
    s.ti = r.text("ti", "tiCompilerDir", "TiCompilerDir", "ti");
    s.tilib = r.text("tilib", "tiLibDir", "TiLibDir", "tilib");
    s.askNative = r.flag("askNative", "askNativeTools", "AskNativeTools", "askNative", true);
    const Json &build = settings.get("build");
    s.paths = named(build.has("paths") ? build.get("paths") : settings.get("paths"));
    if (s.compiler.empty()) s.compiler = "auto";
    return s;
}

}}

// ---- name lists written as patterns (3.4, "an existing .pro that lists names") ----
// For the files of one kind in one folder (*.cpp in backend/): when the groups' names of that kind are together
// exactly the files of that kind there, the group that names the most of them takes the pattern in place of
// its names, at the place of the first; the other groups keep their names, which beat the pattern. Nothing is
// lost by it at that moment: every group gives the files it gave. Otherwise the names stay.

namespace gride { namespace compile {

std::vector<Group> compacted(const Project &p, FileSystem &fs, const Words &never, const Definitions &d) {
    std::vector<Group> groups = p.groups;
    struct Kind { std::string folder, ext; std::vector<size_t> count; std::set<std::string> names; };
    std::vector<Kind> kinds;
    for (size_t g = 0; g < groups.size(); ++g)
        for (size_t e = 0; e < groups[g].entries.size(); ++e) {
            const std::string &entry = groups[g].entries[e];
            if (wildcard(entry)) return p.groups;                  // already by pattern: left as written
            std::string rel = forward(tidy(entry));
            if (absolute(entry) || startsWith(rel, "../") || extension(rel).empty()) continue;
            size_t slash = rel.find_last_of('/');
            std::string folder = slash == std::string::npos ? std::string() : rel.substr(0, slash);
            std::string ext = extension(rel);
            size_t k = 0;
            while (k < kinds.size() && !(same(kinds[k].folder, folder) && kinds[k].ext == ext)) ++k;
            if (k == kinds.size()) { Kind kind; kind.folder = folder; kind.ext = ext; kind.count.assign(groups.size(), 0); kinds.push_back(kind); }
            ++kinds[k].count[g];
            kinds[k].names.insert(lower(leaf(rel)));
        }
    for (size_t k = 0; k < kinds.size(); ++k) {
        const Kind &kind = kinds[k];
        std::string folder = combine(p.folder, kind.folder);
        bool refused = false;
        for (size_t n = 0; n < never.size(); ++n) if (under(never[n], folder)) refused = true;
        if (refused) continue;
        std::set<std::string> there;
        Words names = fs.list(folder);
        for (size_t n = 0; n < names.size(); ++n) {
            const std::string &name = names[n];
            if (name.empty() || name[name.size() - 1] == '\\' || extension(name) != kind.ext) continue;
            if (samePath(combine(folder, name), p.file)) continue;
            std::string rel = kind.folder.empty() ? name : kind.folder + "/" + name;
            if (excluded(p.exclude, rel)) continue;
            there.insert(lower(name));
        }
        if (there != kind.names) continue;
        size_t most = 0;
        for (size_t g = 1; g < groups.size(); ++g) if (kind.count[g] > kind.count[most]) most = g;
        // A language compiled as one run takes its first file as the program's own, and a pattern gives its
        // files by name: where the first file would not stay first, it keeps its name in front of the pattern
        // ("primes.shl", "*.shl") - a name beats a pattern, so it is first and the others follow by name.
        std::string firstOfKind, firstByName;
        const LanguageDef *language = d.languageOf("x" + kind.ext);
        if (language && d.compiledAsOne(*language)) {
            for (size_t e = 0; e < groups[most].entries.size(); ++e) {
                std::string rel = forward(tidy(groups[most].entries[e]));
                size_t slash = rel.find_last_of('/');
                std::string f = slash == std::string::npos ? std::string() : rel.substr(0, slash);
                if (absolute(groups[most].entries[e]) || !same(f, kind.folder) || extension(rel) != kind.ext) continue;
                std::string name = leaf(rel);
                if (firstOfKind.empty()) firstOfKind = name;
                if (firstByName.empty() || ByName()(name, firstByName)) firstByName = name;
            }
        }
        bool keepFirst = !firstOfKind.empty() && !same(firstOfKind, firstByName);
        Words kept;
        bool placed = false;
        for (size_t e = 0; e < groups[most].entries.size(); ++e) {
            std::string rel = forward(tidy(groups[most].entries[e]));
            size_t slash = rel.find_last_of('/');
            std::string f = slash == std::string::npos ? std::string() : rel.substr(0, slash);
            bool ofKind = !wildcard(rel) && !absolute(groups[most].entries[e]) && same(f, kind.folder) && extension(rel) == kind.ext;
            if (!ofKind) { kept.push_back(groups[most].entries[e]); continue; }
            if (placed) continue;
            if (keepFirst) kept.push_back(groups[most].entries[e]);
            kept.push_back((kind.folder.empty() ? std::string() : kind.folder + "/") + "*" + kind.ext);
            placed = true;
        }
        groups[most].entries = kept;
    }
    return groups;
}

}}
