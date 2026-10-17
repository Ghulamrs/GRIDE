#include "ccsproject.h"

#include <algorithm>
#include <cstdlib>

#include "../path.h"
#include "../settings.h"
#include "ccsoptions.h"
#include "ccsworkspace.h"
#include "ccsxml.h"

namespace editor {
namespace ccs {

namespace {

std::string unquoted(const std::string& s) {
    if (s.size() >= 2 && s[0] == '"' && s[s.size() - 1] == '"') return s.substr(1, s.size() - 2);
    return s;
}

bool endsWith(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

bool rooted(const std::string& p) {
    return !p.empty() && (p[0] == '/' || p[0] == '\\' || (p.size() > 1 && p[1] == ':'));
}

std::string extensionOf(const std::string& name) {
    size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? std::string() : name.substr(dot + 1);
}

// What CCS's makefile would take a file for, by its extension (FORMAT.md, "Source files").
enum Kind { Compiled, Command, NotBuilt, Header, Other };
Kind kindOf(const std::string& name) {
    std::string ext = extensionOf(name);
    if (ext == "c" || ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "C" || ext == "c++") return Compiled;
    if (ext == "cmd") return Command;
    if (ext == "asm" || ext == "s" || ext == "sa" || ext == "lib" || ext == "a" || ext == "obj") return NotBuilt;
    if (ext == "h" || ext == "hpp" || ext == "hh" || ext == "hxx") return Header;
    return Other;
}

// One option as .cproject stores it: its definition's id tail, a value, or list items.
struct Stored {
    std::string tail;
    std::string value;
    std::vector<std::string> items;
};

std::vector<Stored> optionsOf(const XmlNode& tool) {
    std::vector<Stored> out;
    std::vector<const XmlNode*> options = tool.all("option");
    for (size_t i = 0; i < options.size(); ++i) {
        Stored one;
        one.tail = idTail(options[i]->attribute("superClass", options[i]->attribute("id")));
        one.value = options[i]->attribute("value");
        std::vector<const XmlNode*> items = options[i]->all("listOptionValue");
        for (size_t k = 0; k < items.size(); ++k) one.items.push_back(items[k]->attribute("value"));
        out.push_back(one);
    }
    return out;
}

const Stored* stored(const std::vector<Stored>& list, const std::string& tail) {
    for (size_t i = 0; i < list.size(); ++i) if (list[i].tail == tail) return &list[i];
    return 0;
}

// The linker tool is the one whose superClass says so; the compiler the same. Names differ
// between versions ("C6000 Compiler" both, but the ids carry exe./library.) so the id is asked.
const XmlNode* toolNamed(const XmlNode& toolChain, const char* word) {
    std::vector<const XmlNode*> tools = toolChain.all("tool");
    for (size_t i = 0; i < tools.size(); ++i)
        if (tools[i]->attribute("superClass").find(word) != std::string::npos) return tools[i];
    return 0;
}

std::string joinedList(const std::vector<std::string>& list, const char* sep = ", ") {
    std::string out;
    for (size_t i = 0; i < list.size(); ++i) out += (i ? sep : "") + list[i];
    return out;
}

void addUnique(std::vector<std::string>& into, const std::string& one) {
    if (std::find(into.begin(), into.end(), one) == into.end()) into.push_back(one);
}

// The files under a folder, recursively, in name order; the build directories and the dot
// directories skipped. Relative to the project, slashes one way.
void walk(const std::string& dir, const std::string& relative, const std::vector<std::string>& skip,
          std::vector<std::string>& into) {
    std::vector<path::Entry> entries = path::entries(dir);
    std::sort(entries.begin(), entries.end(), [](const path::Entry& a, const path::Entry& b) { return a.name < b.name; });
    for (size_t i = 0; i < entries.size(); ++i) {
        const std::string& name = entries[i].name;
        if (name.empty() || name[0] == '.') continue;
        std::string rel = relative.empty() ? name : relative + "/" + name;
        if (entries[i].directory) {
            if (relative.empty() && std::find(skip.begin(), skip.end(), name) != skip.end()) continue;
            if (endsWith(name, ".vm")) continue;
            walk(path::join(dir, name), rel, skip, into);
        } else {
            into.push_back(rel);
        }
    }
}

bool excludedBy(const std::string& relative, const std::vector<std::string>& excluding) {
    for (size_t i = 0; i < excluding.size(); ++i) {
        const std::string& one = excluding[i];
        if (one.empty()) continue;
        if (relative == one) return true;
        if (relative.size() > one.size() && relative.compare(0, one.size(), one) == 0 && relative[one.size()] == '/') return true;
    }
    return false;
}

std::vector<std::string> splitOn(const std::string& text, char sep) {
    std::vector<std::string> out;
    size_t at = 0;
    for (;;) {
        size_t next = text.find(sep, at);
        std::string one = text.substr(at, next == std::string::npos ? std::string::npos : next - at);
        if (!one.empty()) out.push_back(one);
        if (next == std::string::npos) return out;
        at = next + 1;
    }
}

// WORKSPACE_LOC: the workspace the project was opened from, else the folder's parent.
std::string workspaceDir(const Reading& reading) {
    return reading.workspace.empty() ? path::parent(reading.dir) : reading.workspace;
}

// Eclipse's locationURI: PROJECT_LOC/x, PARENT-n-PROJECT_LOC/x, WORKSPACE_LOC/x, file:/...
std::string locationOf(const XmlNode& link, const Reading& reading) {
    const XmlNode* location = link.child("location");
    if (location && !location->text.empty()) return path::withSlashes(location->text);
    const XmlNode* uri = link.child("locationURI");
    if (!uri) return std::string();
    std::string text = uri->text;
    if (text.compare(0, 6, "file:/") == 0) {
        text = text.substr(5);
        while (text.size() > 1 && text[0] == '/' && text[1] == '/') text.erase(0, 1);
        if (text.size() > 2 && text[0] == '/' && text[2] == ':') text.erase(0, 1);
        return path::withSlashes(text);
    }
    size_t slash = text.find('/');
    std::string variable = text.substr(0, slash), rest = slash == std::string::npos ? std::string() : text.substr(slash + 1);
    std::string base;
    if (variable == "PROJECT_LOC") base = reading.dir;
    else if (variable == "WORKSPACE_LOC") base = workspaceDir(reading);
    else if (variable.compare(0, 7, "PARENT-") == 0 && endsWith(variable, "-PROJECT_LOC")) {
        long up = std::strtol(variable.c_str() + 7, 0, 10);
        base = reading.dir;
        for (long i = 0; i < up; ++i) base = path::parent(base);
    } else {
        std::map<std::string, std::string>::const_iterator it = reading.pathVariables.find(variable);
        if (it == reading.pathVariables.end()) return path::withSlashes(text);
        base = it->second;
    }
    return rest.empty() ? base : path::join(base, rest);
}

const char* const kCgToolRootMacro = "${CG_TOOL_ROOT}";

}

const Config* Reading::config(Configuration which) const {
    for (size_t i = 0; i < configs.size(); ++i) if (configs[i].which == which) return &configs[i];
    return 0;
}

bool isProject(const std::string& dir) {
    return path::isDirectory(dir) && path::exists(path::join(dir, ".project")) &&
           path::exists(path::join(dir, ".ccsproject"));
}

bool isProjectFile(const std::string& file) {
    std::string leaf = path::filename(file);
    return leaf == ".project" || leaf == ".ccsproject" || leaf == ".cproject";
}

std::string cgToolRootFor(const std::string& ccsRoot, const std::string& version) {
    if (ccsRoot.empty()) return settings::ti();
    std::string compilers = path::join(path::join(path::withSlashes(ccsRoot), "tools"), "compiler");
    std::vector<path::Entry> entries = path::entries(compilers);
    std::string found;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].directory) continue;
        if (endsWith(entries[i].name, "_" + version)) return path::join(compilers, entries[i].name);
        if (found.empty() && entries[i].name.find(version) != std::string::npos) found = path::join(compilers, entries[i].name);
    }
    if (!found.empty()) return found;
    long major = std::strtol(version.c_str(), 0, 10);
    return path::join(compilers, (major >= 8 ? "ti-cgt-c6000_" : "c6000_") + version);
}

std::string resolveMacros(const std::string& text, const Reading& reading) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        if (text[i] != '$' || i + 1 >= text.size() || text[i + 1] != '{') { out += text[i++]; continue; }
        size_t close = text.find('}', i);
        if (close == std::string::npos) { out += text.substr(i); break; }
        std::string macro = text.substr(i + 2, close - i - 2);
        std::string value;
        bool known = true;
        if (macro == "ProjName") value = reading.name;
        else if (macro == "PROJECT_ROOT" || macro == "PROJECT_LOC") value = reading.dir;
        else if (macro == "CG_TOOL_ROOT") value = reading.cgToolRoot;
        else if (macro == "CG_TOOL_CL") value = "\"" + reading.cgToolRoot + "/bin/cl6x\"";
        else if (macro == "CG_CLEAN_CMD") value = "DEL /F";
        else if (macro == "BuildArtifactFileName") value = reading.name + ".out";
        else if (macro == "BuildDirectory") value = reading.dir;
        else if (macro == "workspace_loc") value = workspaceDir(reading);
        else if (macro.compare(0, 14, "workspace_loc:") == 0) {
            // ${workspace_loc:/P/x}: P is a project of the workspace, wherever its folder is.
            std::string rest = macro.substr(14);
            if (!rest.empty() && rest[0] == '/') rest.erase(0, 1);
            size_t slash = rest.find('/');
            std::string first = rest.substr(0, slash);
            std::map<std::string, std::string>::const_iterator member = reading.projects.find(first);
            if (member != reading.projects.end())
                value = slash == std::string::npos ? member->second : path::join(member->second, rest.substr(slash + 1));
            else value = path::join(workspaceDir(reading), rest);
        } else {
            std::map<std::string, std::string>::const_iterator it = reading.macros.find(macro);
            if (it != reading.macros.end()) value = it->second;
            else known = false;
        }
        if (!known) { out += text.substr(i, close - i + 1); i = close + 1; continue; }
        out += value;
        i = close + 1;
    }
    return out;
}

namespace {

// The mapping of one configuration's stored options onto RIDE's toolchain. Every stored option
// lands in exactly one of: mapped, RIDE's own instead, unsupported.
void mapConfig(const XmlNode& toolChain, const std::vector<Stored>& compiler,
               const std::vector<Stored>& linker, const OptionDefs& defs, Reading& reading, Config& out) {
    const bool release = out.which == ConfigRelease;
    std::vector<std::string> seen;
    (void)toolChain;

    // -- the compiler
    for (size_t i = 0; i < compiler.size(); ++i) {
        const Stored& o = compiler[i];
        const std::string t = o.tail;
        const std::string flag = defs.spell(t, o.value, o.items);
        if (t == "compilerID.OPT_LEVEL" || t == "compilerID.OPT_LEVEL.release") {
            std::string level = enumSuffix(std::string(), o.value);
            if (level == "off" || level == "0") out.opt = "-O0";
            else if (level == "1") out.opt = "-O1";
            else if (level == "2" || level == "3") out.opt = "-O2";
            else { out.unsupported.push_back(flag); continue; }
            out.mapped.push_back(flag + " -> " + out.opt + (level == "3" ? " (cpp11 and c90 have no -O3)" : ""));
        } else if (t == "compilerID.DEBUGGING_MODEL") {
            std::string model = enumSuffix(std::string(), o.value);
            out.debug = model == "SYMDEBUG__DWARF";
            out.debugSaid = true;
            out.mapped.push_back(flag + " -> " + (out.debug ? "-g" : "no -g"));
        } else if (t == "compilerID.DEFINE") {
            for (size_t k = 0; k < o.items.size(); ++k) out.defines.push_back(unquoted(o.items[k]));
            out.mapped.push_back(flag + " -> -D" + joinedList(out.defines, " -D"));
        } else if (t == "compilerID.UNDEFINE") {
            for (size_t k = 0; k < o.items.size(); ++k) out.undefines.push_back(unquoted(o.items[k]));
            out.mapped.push_back(flag + " -> -U" + joinedList(out.undefines, " -U"));
        } else if (t == "compilerID.INCLUDE_PATH") {
            std::vector<std::string> dropped;
            for (size_t k = 0; k < o.items.size(); ++k) {
                std::string raw = unquoted(o.items[k]);
                if (raw.compare(0, std::char_traits<char>::length(kCgToolRootMacro), kCgToolRootMacro) == 0) { dropped.push_back(raw); continue; }
                std::string dir = path::withSlashes(resolveMacros(raw, reading));
                if (!rooted(dir)) dir = path::join(reading.dir, dir);
                addUnique(out.includes, dir);
            }
            out.mapped.push_back(flag + " -> -I" + joinedList(out.includes, " -I") +
                                 (dropped.empty() ? std::string() : " (" + joinedList(dropped) + " is the compiler's own: RIDE's headers instead)"));
        } else if (t == "compilerID.NO_COMPRESS") {
            out.noCompress = o.value == "true";
            out.mapped.push_back(flag + " -> " + (out.noCompress ? "--no_compress" : "compact instructions kept"));
        } else if (t == "compilerID.SILICON_VERSION" || t == "compilerID.ABI" || t == "compilerID.EXCEPTIONS" ||
                   t == "compilerID.RTTI" || t.compare(0, 16, "compilerID.DIAG_") == 0 ||
                   t == "compilerID.DISPLAY_ERROR_NUMBER" || t.compare(0, 19, "compilerID.PREPROC_") == 0 ||
                   t == "compilerID.OBJ_DIRECTORY" || t == "compilerID.OBJ_EXTENSION" || t == "compilerID.ASM_DIRECTORY" ||
                   t == "compilerID.TEMP_DIRECTORY" || t == "compilerID.OUTPUT_FILE" || t == "compilerID.LIST_DIRECTORY") {
            out.ownInstead.push_back(flag);
        } else {
            out.unsupported.push_back(flag);
        }
    }
    if (!stored(compiler, "compilerID.OPT_LEVEL") && !stored(compiler, "compilerID.OPT_LEVEL.release")) {
        std::string level = defs.defaultOf(release ? "compilerID.OPT_LEVEL.release" : "compilerID.OPT_LEVEL");
        if (level == "off" || level == "0") out.opt = "-O0";
        else if (level == "1") out.opt = "-O1";
        else if (level == "2" || level == "3") out.opt = "-O2";
        out.mapped.push_back(level.empty() ? "optimization not set in CCS -> RIDE's default"
                                           : "-O" + level + " (CCS's default for " + out.name + ") -> " + out.opt);
    }

    // -- the linker
    out.link.given = true;
    for (size_t i = 0; i < linker.size(); ++i) {
        const Stored& o = linker[i];
        const std::string t = o.tail;
        const std::string flag = defs.spell(t, o.value, o.items);
        if (t == "linkerID.HEAP_SIZE") { out.link.heap = unquoted(o.value); out.mapped.push_back(flag + " -> lnk6x " + flag); }
        else if (t == "linkerID.STACK_SIZE") { out.link.stack = unquoted(o.value); out.mapped.push_back(flag + " -> lnk6x " + flag); }
        else if (t == "linkerID.SEARCH_PATH") {
            std::vector<std::string> dropped;
            for (size_t k = 0; k < o.items.size(); ++k) {
                std::string raw = unquoted(o.items[k]);
                if (raw.compare(0, std::char_traits<char>::length(kCgToolRootMacro), kCgToolRootMacro) == 0) { dropped.push_back(raw); continue; }
                std::string dir = path::withSlashes(resolveMacros(raw, reading));
                if (!rooted(dir)) dir = path::join(reading.dir, dir);
                addUnique(out.link.searchPaths, dir);
            }
            out.mapped.push_back(flag + " -> -i " + joinedList(out.link.searchPaths, " -i ") +
                                 (dropped.empty() ? std::string() : " (" + joinedList(dropped) + " is the compiler's own: RIDE's runtime directory instead)"));
        } else if (t == "linkerID.LIBRARY") {
            std::vector<std::string> said;
            for (size_t k = 0; k < o.items.size(); ++k) {
                std::string lib = unquoted(o.items[k]);
                if (lib == "libc.a") { said.push_back("libc.a -> rts6740_elf_eh.lib (TI's index, which LNK6x does not read)"); lib = "rts6740_elf_eh.lib"; }
                else said.push_back(lib);
                addUnique(out.link.libraries, lib);
            }
            out.mapped.push_back(flag + " -> -l " + joinedList(said, ", -l "));
        } else if (t == "linkerID.INITIALIZATION_MODEL") {
            std::string model = enumSuffix(std::string(), o.value);
            out.link.romModel = model == "RAM_MODEL" ? 0 : 1;
            out.mapped.push_back(flag + " -> lnk6x " + flag);
        } else if (t == "linkerID.Z" || t == "linkerID.MAP_FILE" || t == "linkerID.OUTPUT_FILE" || t == "linkerID.XML_LINK_INFO" ||
                   t.compare(0, 14, "linkerID.DIAG_") == 0 || t == "linkerID.DISPLAY_ERROR_NUMBER" ||
                   t == "linkerID.REREAD_LIBS" || t == "linkerID.WARN_SECTIONS") {
            out.ownInstead.push_back(flag);
        } else {
            out.unsupported.push_back(flag);
        }
    }
    if (!stored(linker, "linkerID.INITIALIZATION_MODEL")) {
        std::string model = defs.defaultOf("linkerID.INITIALIZATION_MODEL");
        if (model == "RAM_MODEL") { out.link.romModel = 0; out.mapped.push_back("--ram_model (CCS's default) -> lnk6x --ram_model"); }
        else if (model == "ROM_MODEL") { out.link.romModel = 1; out.mapped.push_back("--rom_model (CCS's default) -> lnk6x --rom_model"); }
    }
}

// A file's own options (fileInfo, CCS 7.4): what differs from the folder's is named as unsupported.
void perFileOptions(const XmlNode& fileInfo, const std::vector<Stored>& folder, const OptionDefs& defs, Config& out) {
    std::vector<const XmlNode*> tools = fileInfo.all("tool");
    std::vector<std::string> differ;
    for (size_t t = 0; t < tools.size(); ++t) {
        std::vector<Stored> own = optionsOf(*tools[t]);
        for (size_t i = 0; i < own.size(); ++i) {
            const Stored* base = stored(folder, own[i].tail);
            if (base && base->value == own[i].value && base->items == own[i].items) continue;
            differ.push_back(defs.spell(own[i].tail, own[i].value, own[i].items));
        }
    }
    if (!differ.empty())
        out.unsupported.push_back(fileInfo.attribute("resourcePath") + ": " + joinedList(differ) + " (per-file options)");
}

}

bool read(const std::string& where, Reading& out, std::string& error, const Workspace* workspace) {
    out = Reading();
    error.clear();
    std::string dir = path::withSlashes(path::absolute(where));
    if (isProjectFile(dir)) dir = path::parent(dir);
    while (!dir.empty() && dir[dir.size() - 1] == '/') dir.resize(dir.size() - 1);
    if (!isProject(dir)) { error = dir + " is not a CCS project (no .project and .ccsproject)"; return false; }
    out.dir = dir;
    if (workspace) {
        out.workspace = workspace->dir;
        out.pathVariables = workspace->pathVariables;
        for (size_t i = 0; i < workspace->projects.size(); ++i) out.projects[workspace->projects[i].name] = workspace->projects[i].location;
    }

    XmlNode project, ccsproject, cproject;
    if (!readXmlFile(path::join(dir, ".project"), project, error)) return false;
    if (!readXmlFile(path::join(dir, ".ccsproject"), ccsproject, error)) return false;
    if (!path::exists(path::join(dir, ".cproject"))) { error = dir + ": a CCS project needs a .cproject beside .project"; return false; }
    if (!readXmlFile(path::join(dir, ".cproject"), cproject, error)) return false;

    const XmlNode* name = project.child("name");
    out.name = name ? name->text : path::filename(dir);
    if (out.name.empty()) out.name = path::filename(dir);

    // .project's referenced projects: CCS builds them first, and RIDE builds one project only.
    std::vector<const XmlNode*> referenced;
    if (const XmlNode* projects = project.child("projects")) {
        referenced = projects->all("project");
        for (size_t i = 0; i < referenced.size(); ++i)
            if (!referenced[i]->text.empty()) out.references.push_back(referenced[i]->text);
    }
    for (size_t i = 0; i < out.references.size(); ++i)
        out.notes.push_back("depends on project " + out.references[i] + ", which RIDE does not build - build it in CCS, or open it in RIDE first");

    // .ccsproject: the device, and the compiler version ${CG_TOOL_ROOT} stands for.
    std::vector<const XmlNode*> options;
    ccsproject.find("deviceVariant", options);
    if (!options.empty()) out.device = options[0]->attribute("value");
    options.clear(); ccsproject.find("deviceFamily", options);
    if (!options.empty()) out.family = options[0]->attribute("value");
    options.clear(); ccsproject.find("codegenToolVersion", options);
    if (!options.empty()) out.cgtVersion = options[0]->attribute("value");
    options.clear(); ccsproject.find("ccsVersion", options);
    if (!options.empty()) out.ccsVersion = options[0]->attribute("value");
    options.clear(); ccsproject.find("isElfFormat", options);
    if (!options.empty()) out.elf = options[0]->attribute("value") != "false";
    std::string endianness;
    options.clear(); ccsproject.find("deviceEndianness", options);
    if (!options.empty()) endianness = options[0]->attribute("value");

    // Only the C674x: the C6747 is what cpp11, c90, asm6x, lnk6x and vm6747 are built for.
    if (out.family != "C6000" || out.device.find("C674") == std::string::npos) {
        error = out.name + " is for " + (out.device.empty() ? "an unnamed device" : out.device) +
                (out.family.empty() ? "" : " (" + out.family + ")") + ", and RIDE builds only C6000 C674x/C6747 projects";
        return false;
    }
    if (!out.elf) { error = out.name + " builds COFF, and RIDE's C6000 toolchain makes ELF only"; return false; }
    if (endianness == "big") { error = out.name + " is big-endian, and RIDE's C6000 toolchain is little-endian only"; return false; }

    out.cgToolRoot = path::withSlashes(cgToolRootFor(settings::ccsRoot(), out.cgtVersion));

    OptionDefs defs;
    std::string why;
    if (!defs.load(out.cgtVersion, why)) out.notes.push_back(why + "; options CCS left unstored take RIDE's defaults");

    // .project: the linked resources - a file, or a folder walked like the project's own.
    std::vector<const XmlNode*> links;
    project.find("link", links);
    std::vector<std::pair<std::string, std::string> > linkedFiles;   // project-relative name, absolute
    for (size_t i = 0; i < links.size(); ++i) {
        const XmlNode* linkName = links[i]->child("name");
        const XmlNode* type = links[i]->child("type");
        std::string location = locationOf(*links[i], out);
        if (!linkName || location.empty()) continue;
        if (type && type->text == "2") {
            std::vector<std::string> under;
            walk(location, std::string(), std::vector<std::string>(), under);
            for (size_t k = 0; k < under.size(); ++k)
                linkedFiles.push_back(std::make_pair(linkName->text + "/" + under[k], path::join(location, under[k])));
        } else {
            linkedFiles.push_back(std::make_pair(linkName->text, location));
        }
    }
    for (size_t i = 0; i < linkedFiles.size(); ++i) out.linked.push_back(linkedFiles[i].second);

    // .cproject: one cconfiguration per build configuration.
    std::vector<const XmlNode*> cconfigurations;
    cproject.find("cconfiguration", cconfigurations);
    std::vector<std::string> configNames;
    for (size_t c = 0; c < cconfigurations.size(); ++c) {
        std::vector<const XmlNode*> found;
        cconfigurations[c]->find("configuration", found);
        if (found.empty()) continue;
        const XmlNode& configuration = *found[0];
        Config config;
        config.name = configuration.attribute("name");
        configNames.push_back(config.name);
        std::string parent = configuration.attribute("parent");
        if (endsWith(parent, ".Debug") || (parent.empty() && config.name == "Debug")) config.which = ConfigDebug;
        else if (endsWith(parent, ".Release") || (parent.empty() && config.name == "Release")) config.which = ConfigRelease;
        else { out.notes.push_back("configuration " + config.name + " is neither Debug nor Release, and RIDE builds those two"); continue; }
        if (out.config(config.which)) { out.notes.push_back("configuration " + config.name + " is a second " + configName(config.which) + " one, and the first is taken"); continue; }

        // Build variables: per configuration in the file, one set here, the first's kept.
        std::vector<const XmlNode*> macros;
        cconfigurations[c]->find("stringMacro", macros);
        for (size_t m = 0; m < macros.size(); ++m)
            if (out.macros.find(macros[m]->attribute("name")) == out.macros.end())
                out.macros[macros[m]->attribute("name")] = macros[m]->attribute("value");
        // The workspace's build macros, for the names this project does not define itself.
        if (workspace)
            for (std::map<std::string, std::string>::const_iterator it = workspace->macros.begin(); it != workspace->macros.end(); ++it)
                if (out.macros.find(it->first) == out.macros.end()) out.macros[it->first] = it->second;
        config.prebuild = resolveMacros(configuration.attribute("prebuildStep"), out);
        config.postbuild = resolveMacros(configuration.attribute("postbuildStep"), out);

        found.clear();
        configuration.find("toolChain", found);
        if (found.empty()) { out.notes.push_back("configuration " + config.name + " has no toolChain"); continue; }
        const XmlNode& toolChain = *found[0];
        std::vector<Stored> chain = optionsOf(toolChain);
        if (const Stored* tags = stored(chain, "core.OPT_TAGS"))
            for (size_t k = 0; k < tags->items.size(); ++k) {
                const std::string& tag = tags->items[k];
                if (tag == "OUTPUT_FORMAT=COFF") { error = out.name + " (" + config.name + ") builds COFF, and RIDE's C6000 toolchain makes ELF only"; return false; }
                if (tag == "DEVICE_ENDIANNESS=big") { error = out.name + " (" + config.name + ") is big-endian, and RIDE's C6000 toolchain is little-endian only"; return false; }
                if (tag.compare(0, 12, "OUTPUT_TYPE=") == 0 && tag != "OUTPUT_TYPE=executable")
                    out.notes.push_back(config.name + " builds " + tag.substr(12) + ", and RIDE builds an executable of it");
            }

        const XmlNode* compilerTool = toolNamed(toolChain, ".compiler");
        const XmlNode* linkerTool = toolNamed(toolChain, ".linker");
        std::vector<Stored> compiler = compilerTool ? optionsOf(*compilerTool) : std::vector<Stored>();
        std::vector<Stored> linker = linkerTool ? optionsOf(*linkerTool) : std::vector<Stored>();
        if (const Stored* silicon = stored(compiler, "compilerID.SILICON_VERSION"))
            if (silicon->value != "6740") {
                error = out.name + " (" + config.name + ") is built for -mv" + silicon->value + ", and RIDE's C6000 toolchain is the C674x's (-mv6740)";
                return false;
            }
        if (const Stored* abi = stored(compiler, "compilerID.ABI"))
            if (enumSuffix(std::string(), abi->value) == "coffabi") { error = out.name + " (" + config.name + ") uses the COFF ABI, and RIDE's C6000 toolchain makes ELF only"; return false; }
        if (const Stored* big = stored(compiler, "compilerID.BIG_ENDIAN"))
            if (big->value == "true") { error = out.name + " (" + config.name + ") is big-endian, and RIDE's C6000 toolchain is little-endian only"; return false; }

        mapConfig(toolChain, compiler, linker, defs, out, config);

        found.clear();
        configuration.find("fileInfo", found);
        for (size_t f = 0; f < found.size(); ++f) perFileOptions(*found[f], compiler, defs, config);

        found.clear();
        configuration.find("sourceEntries", found);
        for (size_t s = 0; s < found.size(); ++s) {
            std::vector<const XmlNode*> entries = found[s]->all("entry");
            for (size_t e = 0; e < entries.size(); ++e) {
                std::vector<std::string> names = splitOn(entries[e]->attribute("excluding"), '|');
                for (size_t n = 0; n < names.size(); ++n) config.excluded.push_back(path::withSlashes(names[n]));
            }
        }
        out.configs.push_back(config);
    }
    if (out.configs.empty()) { error = out.name + ": no Debug or Release configuration in .cproject"; return false; }
    std::sort(out.configs.begin(), out.configs.end(), [](const Config& a, const Config& b) { return a.which < b.which; });

    // The sources: every file in the folder by extension, plus the linked ones, minus each
    // configuration's exclusions. The build directories are the configurations' own names.
    std::vector<std::string> under;
    walk(dir, std::string(), configNames, under);
    std::vector<std::pair<std::string, std::string> > candidates;   // relative, absolute
    for (size_t i = 0; i < under.size(); ++i) candidates.push_back(std::make_pair(under[i], path::join(dir, under[i])));
    for (size_t i = 0; i < linkedFiles.size(); ++i) candidates.push_back(linkedFiles[i]);
    for (size_t i = 0; i < candidates.size(); ++i) {
        Kind kind = kindOf(candidates[i].first);
        if (kind == NotBuilt) { addUnique(out.notBuilt, candidates[i].first); continue; }
        // A header of the folder's own is listed to be opened; one linked from elsewhere is reached by #include.
        if (kind == Header) { if (i < under.size()) addUnique(out.headers, candidates[i].first); continue; }
        if (kind == Other) continue;
        for (size_t c = 0; c < out.configs.size(); ++c) {
            Config& config = out.configs[c];
            if (excludedBy(candidates[i].first, config.excluded)) continue;
            if (kind == Command) addUnique(config.link.cmdFiles, candidates[i].second);
            else addUnique(config.sources, candidates[i].second);
        }
    }
    return true;
}

std::string report(const Reading& reading, Configuration which) {
    const Config* config = reading.config(which);
    std::string head = "CCS project " + reading.name;
    if (!config) return head + ": no " + configName(which) + " configuration in it";
    head += " (" + config->name + "): ";
    std::string said;
    if (config->unsupported.empty()) said = "no unsupported options";
    else said = std::to_string(config->unsupported.size()) + (config->unsupported.size() == 1 ? " option" : " options") +
                " not supported, using RIDE's defaults: " + joinedList(config->unsupported);
    if (!config->ownInstead.empty()) said += "; RIDE's own instead of: " + joinedList(config->ownInstead);
    return head + said;
}

std::string sourceReport(const Reading& reading) {
    std::vector<std::string> gone;
    for (size_t i = 0; i < reading.linked.size(); ++i)
        if (!path::exists(reading.linked[i])) gone.push_back(reading.linked[i]);
    std::string said;
    if (!gone.empty()) said += "linked " + std::string(gone.size() == 1 ? "file" : "files") + " not on this machine: " + joinedList(gone);
    if (!reading.notBuilt.empty()) said += (said.empty() ? "" : "; ") + std::string("not built by RIDE: ") + joinedList(reading.notBuilt);
    for (size_t i = 0; i < reading.notes.size(); ++i) said += (said.empty() ? "" : "; ") + reading.notes[i];
    return said.empty() ? said : "CCS project " + reading.name + ": " + said;
}

std::string mappingText(const Reading& reading, Configuration which) {
    const Config* config = reading.config(which);
    std::string text = "CCS project " + reading.name + ", read from " + reading.dir + "\n";
    text += "device " + reading.device + ", compiler " + reading.cgtVersion + (reading.ccsVersion.empty() ? " (CCS 5.5)" : " (CCS " + reading.ccsVersion + ")") + "\n";
    if (!config) return text + "no " + configName(which) + " configuration\n";
    text += "\n" + config->name + " - what CCS set, and what RIDE builds with:\n";
    for (size_t i = 0; i < config->mapped.size(); ++i) text += "  " + config->mapped[i] + "\n";
    if (!config->link.cmdFiles.empty()) text += "  linker command file: " + joinedList(config->link.cmdFiles) + "\n";
    if (!config->unsupported.empty()) {
        text += "\nnot supported, RIDE's defaults instead:\n";
        for (size_t i = 0; i < config->unsupported.size(); ++i) text += "  " + config->unsupported[i] + "\n";
    }
    if (!config->ownInstead.empty()) {
        text += "\nRIDE's own instead of:\n";
        for (size_t i = 0; i < config->ownInstead.size(); ++i) text += "  " + config->ownInstead[i] + "\n";
    }
    if (!config->excluded.empty()) text += "\nexcluded from the build: " + joinedList(config->excluded) + "\n";
    text += "\nThese are read from the CCS project and cannot be changed here - edit them in CCS.\n";
    return text;
}

}
}
