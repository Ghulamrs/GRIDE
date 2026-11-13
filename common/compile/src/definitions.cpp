#include "definitions.h"

namespace gride { namespace compile {

namespace {

Words list(const Json &j) {
    Words r;
    for (size_t i = 0; i < j.size(); ++i) r.push_back(j.at(i).text());
    return r;
}

Named named(const Json &j) {
    Named r;
    if (!j.is(Json::Object)) return r;
    for (size_t i = 0; i < j.size(); ++i) r[j.keyAt(i)] = j.valueAt(i).text();
    return r;
}

Words keys(const Json &j) {
    Words r;
    if (!j.is(Json::Object)) return r;
    for (size_t i = 0; i < j.size(); ++i) r.push_back(j.keyAt(i));
    return r;
}

std::string get(const Named &n, const std::string &key) {
    Named::const_iterator it = n.find(key);
    return it == n.end() ? std::string() : it->second;
}

}

// ---- the records ----

const OperationDef *ToolDef::operation(const std::string &operationId) const {
    for (size_t i = 0; i < operations.size(); ++i) if (operations[i].id == operationId) return &operations[i];
    return 0;
}

Words ToolDef::names() const {
    Words r, all;
    all.push_back(exe);
    for (size_t i = 0; i < aliases.size(); ++i) all.push_back(aliases[i]);
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i].empty()) continue;
        addOnce(r, all[i]);
        if (!endsWith(all[i], ".exe")) addOnce(r, all[i] + ".exe");
    }
    return r;
}

std::string TargetDef::role(const std::string &name) const { return get(roles, name); }

bool LibraryDef::alias(const std::string &n) const {
    for (size_t i = 0; i < aliases.size(); ++i) if (matches(aliases[i], n)) return true;
    return false;
}

Words LibraryDef::filesFor(const std::string &configuration) const {
    return split(get(files, same(configuration, "Release") ? "Release" : "Debug"), " ");
}

std::string OptionDef::defaultFor(const std::string &configuration) const {
    Named::const_iterator it = defaults.find(same(configuration, "Release") ? "Release" : "Debug");
    if (it != defaults.end()) return it->second;
    return get(defaults, "");
}

// ---- lookups ----

const TargetDef *Definitions::target(const std::string &name) const {
    for (size_t i = 0; i < targets.size(); ++i)
        if (same(targets[i].arch, name) || same(targets[i].ideTarget, name)) return &targets[i];
    return 0;
}

const ToolDef *Definitions::tool(const std::string &id) const {
    for (size_t i = 0; i < tools.size(); ++i) if (tools[i].id == id) return &tools[i];
    return 0;
}

const ToolDef *Definitions::toolFor(const TargetDef &t, const std::string &role) const {
    std::string id = t.role(role);
    return id.empty() ? 0 : tool(id);
}

const ToolDef *Definitions::toolOfWord(const std::string &word) const {
    for (size_t i = 0; i < tools.size(); ++i)
        if (same(tools[i].id, word) || contains(tools[i].words, word)) return &tools[i];
    return 0;
}

const LibraryDef *Definitions::library(const std::string &id) const {
    for (size_t i = 0; i < libraries.size(); ++i) if (libraries[i].id == id) return &libraries[i];
    return 0;
}

const LibraryDef *Definitions::libraryOfKind(const std::string &arch, const std::string &kind, const std::string &crt) const {
    for (size_t i = 0; i < libraries.size(); ++i) {
        const LibraryDef &l = libraries[i];
        if (!same(l.target, arch) || l.kind != kind) continue;
        if (!crt.empty() && !l.crt.empty() && l.crt != crt) continue;
        return &l;
    }
    return 0;
}

const OptionDef *Definitions::option(const std::string &id) const {
    for (size_t i = 0; i < options.size(); ++i) if (options[i].id == id) return &options[i];
    return 0;
}

const LanguageDef *Definitions::languageOf(const std::string &path) const {
    std::string e = extension(path);
    if (e.empty()) return 0;
    for (size_t i = 0; i < languages.size(); ++i) if (contains(languages[i].extensions, e)) return &languages[i];
    return 0;
}

const LanguageDef *Definitions::language(const std::string &name) const {
    for (size_t i = 0; i < languages.size(); ++i) if (same(languages[i].name, name)) return &languages[i];
    return 0;
}

bool Definitions::header(const std::string &path) const { return contains(headerExtensions, extension(path)); }

bool Definitions::compiledAsOne(const LanguageDef &l) const {
    for (size_t i = 0; i < tools.size(); ++i) {
        if (tools[i].role != l.role) continue;
        const OperationDef *op = tools[i].operation("compileToAssembly");
        if (op && op->args.find("$(Sources)") != std::string::npos) return true;
    }
    return false;
}

const LibraryDef *Definitions::libraryWhen(const std::string &arch, const std::string &languageName, const std::string &crt) const {
    for (size_t i = 0; i < libraries.size(); ++i) {
        const LibraryDef &l = libraries[i];
        if (!same(l.target, arch) || !same(l.when, languageName)) continue;
        if (!crt.empty() && !l.crt.empty() && l.crt != crt) continue;
        return &l;
    }
    return 0;
}

const LinkerCommandFileDef *Definitions::linkerCommandFile(const std::string &id) const {
    for (size_t i = 0; i < linkerCommandFiles.size(); ++i) if (linkerCommandFiles[i].id == id) return &linkerCommandFiles[i];
    return 0;
}

// ---- reading ----

Json merged(const Json &shipped, const Json &user) {
    if (!shipped.is(Json::Object) || !user.is(Json::Object)) return user;
    Json r = shipped;
    for (size_t i = 0; i < user.size(); ++i) {
        const std::string &key = user.keyAt(i);
        r.set(key, r.has(key) ? merged(r.get(key), user.valueAt(i)) : user.valueAt(i));
    }
    return r;
}

Definitions readDefinitions(const Json &root, Words &problems) {
    Definitions d;
    if (!root.is(Json::Object)) { problems.push_back("toolchain.json is not an object"); return d; }

    const Json &inst = root.get("installation");
    d.installRoots = list(inst.get("defaultRoots"));
    d.installMarkers = list(inst.get("markers"));
    d.installBin = inst.get("binDir").text();
    d.installInclude = inst.get("includeDir").text("include");
    d.installLib = inst.get("libDir").text("lib");

    const Json &langs = root.get("languages");
    for (size_t i = 0; i < langs.size(); ++i) {
        LanguageDef l;
        l.name = langs.keyAt(i);
        l.role = langs.valueAt(i).get("role").text();
        Words e = list(langs.valueAt(i).get("extensions"));
        for (size_t k = 0; k < e.size(); ++k) l.extensions.push_back(lower(e[k]));
        d.languages.push_back(l);
    }
    if (d.languages.empty()) problems.push_back("toolchain.json names no \"languages\": no file is a source");
    Words heads = list(root.get("headerExtensions"));
    for (size_t i = 0; i < heads.size(); ++i) d.headerExtensions.push_back(lower(heads[i]));

    d.outputFolder = root.get("folders").get("output").text("out/$(Config)/$(TargetFolder)");
    d.objectFolder = root.get("folders").get("objects").text("obj/$(Config)/$(TargetFolder)");

    const Json &targets = root.get("targets");
    for (size_t i = 0; i < targets.size(); ++i) {
        const Json &j = targets.valueAt(i);
        TargetDef t;
        t.arch = targets.keyAt(i);
        t.ideTarget = j.get("ideTarget").text(t.arch);
        t.folder = j.get("folder").text(t.ideTarget);
        t.host = j.get("host").boolean();
        t.emulated = j.get("emulated").boolean();
        t.debugInfo = j.get("debugInfo").text();
        t.assemblyExtension = j.get("assemblyExtension").text(".s");
        t.objectExtension = j.get("objectExtension").text(".o");
        t.programExtension = j.get("programExtension").text();
        t.libraryDirFlag = j.get("libraryDirFlag").text();
        t.libraryFlag = j.get("libraryFlag").text();
        t.refuseLibraryFlag = j.get("refuseLibraryFlag").text();
        t.roles = named(j.get("roles"));
        t.runtime = j.get("runtime").text();
        t.linkerCommandFile = j.get("linkerCommandFile").text();
        d.targets.push_back(t);
    }
    if (d.targets.empty()) problems.push_back("toolchain.json names no \"targets\"");

    const Json &tools = root.get("tools");
    for (size_t i = 0; i < tools.size(); ++i) {
        const Json &j = tools.valueAt(i);
        ToolDef t;
        t.id = tools.keyAt(i);
        t.name = j.get("name").text(t.id);
        t.role = j.get("role").text();
        t.exe = j.get("exe").text();
        t.languages = list(j.get("languages"));
        t.targets = list(j.get("targets"));
        t.aliases = list(j.get("aliases"));
        t.words = list(j.get("words"));
        t.installIncludeDirs = list(j.get("installIncludeDirs"));
        t.options = list(j.get("options"));
        const Json &lookup = j.get("lookup");
        t.lookupEnv = lookup.get("env").text();
        t.lookupInstallDir = lookup.get("installDir").text();
        t.lookupDirs = list(lookup.get("dirs"));
        t.lookupPath = lookup.get("path").boolean();
        t.lookupVcvars = lookup.get("vcvars").boolean();
        t.outputEncoding = lower(j.get("outputEncoding").text("utf-8"));
        t.diagnostics = j.get("diagnostics").text("none");
        t.archFlag = j.get("archFlag").text();
        t.includeFlag = j.get("includeFlag").text();
        t.defineFlag = j.get("defineFlag").text();
        t.responseFile = j.get("responseFile").text();
        t.languageFlags = named(j.get("languageFlags"));
        t.configFlags = named(j.get("configFlags"));
        t.asmSyntaxFlags = named(j.get("asmSyntaxFlags"));
        t.linkFlags = named(j.get("linkFlags"));
        const Json &byTarget = j.get("configFlagsByTarget");
        for (size_t k = 0; k < byTarget.size(); ++k) t.configFlagsByTarget[byTarget.keyAt(k)] = named(byTarget.valueAt(k));
        t.environmentNames = keys(j.get("environment"));
        const Json &ops = j.get("operations");
        for (size_t k = 0; k < ops.size(); ++k) {
            OperationDef o;
            o.id = ops.keyAt(k);
            o.args = ops.valueAt(k).get("args").text();
            o.output = ops.valueAt(k).get("output").text();
            t.operations.push_back(o);
        }
        if (t.exe.empty()) problems.push_back("toolchain.json: tool \"" + t.id + "\" names no \"exe\"");
        d.tools.push_back(t);
    }

    const Json &libs = root.get("libraries");
    for (size_t i = 0; i < libs.size(); ++i) {
        const Json &j = libs.valueAt(i);
        LibraryDef l;
        l.id = libs.keyAt(i);
        l.name = j.get("name").text(l.id);
        l.kind = j.get("kind").text();
        l.crt = j.get("crt").text();
        l.target = j.get("target").text();
        l.directory = j.get("directory").text();
        l.directoryEnv = j.get("directoryEnv").text();
        l.marker = j.get("marker").text();
        l.when = j.get("when").text("always");
        l.files = named(j.get("files"));
        l.aliases = list(j.get("aliases"));
        l.refuses = list(j.get("refuses"));
        d.libraries.push_back(l);
    }

    const Json &opts = root.get("compilerOptions").get("options");
    for (size_t i = 0; i < opts.size(); ++i) {
        const Json &j = opts.valueAt(i);
        OptionDef o;
        o.id = opts.keyAt(i);
        o.control = j.get("control").text();
        o.flag = j.get("flag").text();
        o.flagOn = j.get("flagOn").text();
        o.flagOff = j.get("flagOff").text();
        o.flagEach = j.get("flagEach").text();
        o.silent = j.get("silent").text();
        o.needs = j.get("needs").text();
        o.environment = j.get("environment").text();
        const Json &def = j.get("default");
        if (def.is(Json::Object)) o.defaults = named(def);
        else o.defaults[""] = def.text();
        d.options.push_back(o);
    }

    const Json &cmds = root.get("linkerCommandFiles");
    for (size_t i = 0; i < cmds.size(); ++i) {
        LinkerCommandFileDef f;
        f.id = cmds.keyAt(i);
        f.name = cmds.valueAt(i).get("name").text();
        Words lines = list(cmds.valueAt(i).get("lines"));
        for (size_t k = 0; k < lines.size(); ++k) f.text += lines[k] + "\r\n";
        d.linkerCommandFiles.push_back(f);
    }

    const Json &vs = root.get("visualStudio");
    VisualStudioDef &v = d.visualStudio;
    v.msvcVersion = vs.get("msvcVersion").text();
    v.vcvars = vs.get("vcvars").text();
    v.msvcTools = vs.get("msvcTools").text();
    v.hostBin = vs.get("hostBin").text();
    v.lib = vs.get("lib").text();
    v.include = vs.get("include").text();
    v.llvmBin = vs.get("llvmBin").text();
    v.defaultRoots = list(vs.get("defaultRoots"));
    v.editions = list(vs.get("editions"));
    const Json &kits = vs.get("windowsKits");
    v.kitRegistryKey = kits.get("registryKey").text();
    v.kitRegistryValue = kits.get("registryValue").text();
    v.kitVersionsUnder = kits.get("versionsUnder").text();
    v.kitBinDir = kits.get("binDir").text();
    v.kitRoots = list(kits.get("roots"));
    v.kitLibDirs = list(kits.get("libDirs"));
    v.kitIncludeDirs = list(kits.get("includeDirs"));

    const Json &ccs = root.get("ccs");
    const Json &ti = ccs.get("tiCompiler");
    d.tiRoots = list(ti.get("roots"));
    d.tiUnder = list(ti.get("under"));
    d.tiBin = ti.get("bin").text();
    d.tiLib = ti.get("lib").text();
    d.tiCompilerDirs = named(ccs.get("compilerDirs"));
    d.tiRuntime = ccs.get("tiRuntime").get("library").text();
    d.tiRuntimeFallback = ccs.get("tiRuntime").get("fallback").text();
    d.tiRuntimeIndex = ccs.get("tiRuntime").get("index").text();
    return d;
}

}}
