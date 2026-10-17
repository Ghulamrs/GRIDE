#include "ccsoptions.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "../path.h"

namespace editor {
namespace ccs {

namespace {

// A pointer, never a std::string global: a native global with a destructor corrupts the mixed-mode window's onexit table (settings.cpp).
std::string* namedDir = 0;

std::vector<std::string> splitOn(const std::string& text, char sep) {
    std::vector<std::string> out;
    size_t at = 0;
    for (;;) {
        size_t next = text.find(sep, at);
        out.push_back(text.substr(at, next == std::string::npos ? std::string::npos : next - at));
        if (next == std::string::npos) return out;
        at = next + 1;
    }
}

std::string trimmed(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

const char* const kFile74 = "options-ccs74-C6000_8.2.tsv";
const char* const kFile55 = "options-ccs55-C6000_7.4.tsv";

bool holdsDefinitions(const std::string& dir) {
    return path::exists(path::join(dir, kFile74)) && path::exists(path::join(dir, kFile55));
}

}

void setDefinitionsDir(const std::string& dir) {
    if (!namedDir) namedDir = new std::string();
    *namedDir = dir;
}

std::string definitionsDir() {
    if (namedDir && !namedDir->empty()) return holdsDefinitions(*namedDir) ? *namedDir : std::string();
    std::string program = path::programDirectory();
    if (program.empty()) return std::string();
    const std::string tried[] = {
        path::join(path::join(program, "lib"), "ccs"),
        path::join(path::join(path::parent(program), "lib"), "ccs"),
        path::join(path::join(path::join(program, "docs"), "ccs-reference"), "ti-option-definitions"),
        path::join(path::join(path::join(path::parent(program), "docs"), "ccs-reference"), "ti-option-definitions"),
    };
    for (size_t i = 0; i < sizeof tried / sizeof tried[0]; ++i)
        if (holdsDefinitions(tried[i])) return tried[i];
    return std::string();
}

std::string idTail(const std::string& fullId) {
    const std::string prefix = "com.ti.ccstudio.buildDefinitions.";
    if (fullId.compare(0, prefix.size(), prefix) != 0) return fullId;
    std::string rest = fullId.substr(prefix.size());
    // Past "C6000_8.2." - the version carries a dot of its own - or "core."; what follows is compilerID.X, linkerID.X, exe.compilerDebug.
    if (rest.compare(0, 6, "C6000_") == 0) {
        size_t at = 6;
        while (at < rest.size() && (std::isdigit(static_cast<unsigned char>(rest[at])) || rest[at] == '.')) ++at;
        return rest.substr(at);
    }
    return rest;   // core.OPT_TAGS keeps its word
}

std::string enumSuffix(const std::string& optionId, const std::string& valueId) {
    if (valueId.compare(0, optionId.size(), optionId) == 0 && valueId.size() > optionId.size() &&
        valueId[optionId.size()] == '.')
        return valueId.substr(optionId.size() + 1);
    size_t dot = valueId.find_last_of('.');
    return dot == std::string::npos ? valueId : valueId.substr(dot + 1);
}

bool OptionDefs::load(const std::string& version, std::string& why) {
    defs_.clear();
    file_.clear();
    std::string dir = definitionsDir();
    if (dir.empty()) {
        why = "TI's option definitions are not here - lib/ccs beside the program, or docs/ccs-reference/ti-option-definitions";
        return false;
    }
    long major = std::strtol(version.c_str(), 0, 10);
    std::string file = path::join(dir, major >= 8 ? kFile74 : kFile55);
    std::FILE* in = std::fopen(file.c_str(), "rb");
    if (!in) { why = "cannot read " + file; return false; }
    std::string text;
    char chunk[4096];
    size_t got;
    while ((got = std::fread(chunk, 1, sizeof chunk, in)) > 0) text.append(chunk, got);
    std::fclose(in);
    std::vector<std::string> lines = splitOn(text, '\n');
    for (size_t i = 1; i < lines.size(); ++i) {
        std::vector<std::string> cols = splitOn(lines[i], '\t');
        if (cols.size() < 7) continue;
        OptionDef def;
        def.id = idTail(trimmed(cols[1]));
        def.valueType = trimmed(cols[2]);
        def.command = trimmed(cols[3]);
        def.defaultValue = trimmed(cols[5]);
        def.superClass = idTail(trimmed(cols[6]));
        if (cols.size() > 7) {
            std::vector<std::string> values = splitOn(cols[7], '|');
            for (size_t v = 0; v < values.size(); ++v) {
                std::string one = trimmed(values[v]);
                if (one.empty()) continue;
                bool isDefault = false;
                const std::string mark = " (default)";
                if (one.size() > mark.size() && one.compare(one.size() - mark.size(), mark.size(), mark) == 0) {
                    one.resize(one.size() - mark.size());
                    isDefault = true;
                }
                size_t eq = one.find('=');
                std::string suffix = eq == std::string::npos ? one : one.substr(0, eq);
                std::string flag = eq == std::string::npos ? std::string() : one.substr(eq + 1);
                def.enums.push_back(std::make_pair(suffix, flag));
                if (isDefault) def.defaultEnum = suffix;
            }
        }
        // The first row of a tail wins: an instance (compilerRelease's OPT_LEVEL.release) has its own id.
        if (defs_.find(def.id) == defs_.end()) defs_[def.id] = def;
    }
    file_ = file;
    if (defs_.empty()) { why = file + " holds no definitions"; return false; }
    return true;
}

const OptionDef* OptionDefs::find(const std::string& tail) const {
    std::map<std::string, OptionDef>::const_iterator it = defs_.find(tail);
    return it == defs_.end() ? 0 : &it->second;
}

std::string OptionDefs::spell(const std::string& tail, const std::string& value,
                              const std::vector<std::string>& items) const {
    const OptionDef* def = find(tail);
    // An instance carries its superClass's shape: OPT_LEVEL.release spells as OPT_LEVEL does.
    const OptionDef* shape = def && def->valueType.empty() && !def->superClass.empty() ? find(def->superClass) : def;
    if (!shape) {
        std::string said = tail;
        if (!value.empty()) said += "=" + value;
        for (size_t i = 0; i < items.size(); ++i) said += (i ? "," : "=") + items[i];
        return said;
    }
    if (shape->valueType == "boolean") return value == "true" ? shape->command : shape->command + "=false";
    if (shape->valueType == "enumerated") {
        std::string suffix = enumSuffix(std::string(), value);
        for (size_t i = 0; i < shape->enums.size(); ++i)
            if (shape->enums[i].first == suffix) return shape->enums[i].second.empty() ? shape->command : shape->enums[i].second;
        return shape->command + "=" + suffix;
    }
    if (shape->valueType == "string") return shape->command + value;
    std::string said;
    for (size_t i = 0; i < items.size(); ++i) said += (i ? " " : "") + shape->command + items[i];
    return said.empty() ? shape->command : said;
}

std::string OptionDefs::defaultOf(const std::string& tail) const {
    const OptionDef* def = find(tail);
    if (!def) return std::string();
    if (!def->defaultValue.empty()) {
        // An enumerated default is the value's full id; answer its suffix.
        const OptionDef* shape = !def->valueType.empty() ? def : find(def->superClass);
        if (shape && shape->valueType == "enumerated") return enumSuffix(std::string(), def->defaultValue);
        return def->defaultValue;
    }
    if (!def->defaultEnum.empty()) return def->defaultEnum;
    if (def->valueType.empty() && !def->superClass.empty()) return defaultOf(def->superClass);
    return std::string();
}

}
}
