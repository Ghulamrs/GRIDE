#include "options.h"

#include <vector>

#include "settings.h"

namespace editor {

namespace options {

// The rows, in the order each tab shows them. A Choice's first value is the compiler's own default,
// which is never written on a command line; the defaults below are GRIDE's, per configuration.
static const Def kDefs[] = {
    { "general.nologo", "General", "Hide the compiler's banner (-nologo)", Check, "", "c90, cpp11 and shalimar" },
    { "general.jobs", "General", "Parallel jobs (-j)", Choice, "Auto|1|2|4|8", "c90 and cpp11; Auto is serial below four files" },
    { "general.time", "General", "Phase timings (-time)", Check, "", "c90 and cpp11" },
    { "c90.opt", "C (c90)", "Optimization", Choice, "-O0|-O1|-O2", "x86_64 targets only; -O2 is -O1 today" },
    { "c90.g", "C (c90)", "Debug line table (-g)", Check, "", "" },
    { "c90.defines", "C (c90)", "Defines (-D)", Text, "", "';' between them: NAME or NAME=value" },
    { "c90.undefines", "C (c90)", "Undefines (-U)", Text, "", "';' between them" },
    { "cpp11.opt", "C++ (cpp11)", "Optimization", Choice, "-O0|-O1|-O2", "tms6747: -O1 schedules packets and keeps locals in registers, -O2 adds pipelining; x86_64: -O2 adds inlining" },
    { "cpp11.g", "C++ (cpp11)", "Debug line table (-g)", Check, "", "" },
    { "cpp11.defines", "C++ (cpp11)", "Defines (-D)", Text, "", "';' between them: NAME or NAME=value" },
    { "cpp11.undefines", "C++ (cpp11)", "Undefines (-U)", Text, "", "';' between them" },
    { "cpp11.declines", "C++ (cpp11)", "Report what the optimizer declined (CPP11_DECLINES)", Check, "", "one line on stderr per function or call not optimized" },
    { "cpp11.compress", "C++ (cpp11)", "Compact 16-bit instructions (off: --no_compress)", Check, "", "tms6747 only; the C674x runs them natively, and TI's cl6x writes them by default" },
    { "shc.opt", "Shalimar", "Optimization", Choice, "-O0|-O1|-O2", "x86_64 targets only; -O2 is -O1 today" },
    { "shc.debugrt", "Shalimar", "Debug runtime (--debug)", Check, "", "" },
    { "shc.g", "Shalimar", "Debug information (-g)", Check, "", "DWARF on x86_64-linux and arm64-darwin, CodeView on x86_64-windows, .dwpsn on tms6747 (D1 to D5)" },
    { "shc.search", "Shalimar", "Compile sibling .shl and .shm files", Check, "", "off passes --no-search" },
};
static const char* const kTabs[] = { "General", "C (c90)", "C++ (cpp11)", "Shalimar" };

size_t count() { return sizeof kDefs / sizeof kDefs[0]; }
const Def& at(size_t index) { return kDefs[index]; }
const Def* find(const std::string& id) {
    for (size_t i = 0; i < count(); ++i) if (id == kDefs[i].id) return &kDefs[i];
    return 0;
}
size_t tabCount() { return sizeof kTabs / sizeof kTabs[0]; }
const char* tabName(size_t tab) { return tab < tabCount() ? kTabs[tab] : ""; }

std::string defaultValue(Configuration config, const std::string& id) {
    const bool release = config == ConfigRelease;
    if (id == "c90.opt" || id == "cpp11.opt") return release ? "-O2" : "-O0";
    if (id == "shc.opt") return "-O0";
    if (id == "c90.g" || id == "cpp11.g" || id == "shc.debugrt" || id == "shc.g") return release ? "0" : "1";
    if (id == "c90.defines" || id == "cpp11.defines") return release ? "NDEBUG=1" : "_DEBUG=1";
    if (id == "shc.search" || id == "cpp11.compress") return "1";
    const Def* def = find(id);
    if (def && def->control == Choice) {
        std::string all(def->choices);
        return all.substr(0, all.find('|'));
    }
    return def && def->control == Check ? "0" : "";
}

std::string Store::value(Configuration config, const std::string& id) const {
    const Values& v = values_[config];
    Values::const_iterator it = v.find(id);
    return it == v.end() ? defaultValue(config, id) : it->second;
}

void Store::set(Configuration config, const std::string& id, const std::string& value) {
    if (value == defaultValue(config, id)) values_[config].erase(id);
    else values_[config][id] = value;
}

void Store::reset(Configuration config) { values_[config].clear(); }

bool Store::empty() const {
    for (int c = 0; c < ConfigCount; ++c) if (!values_[c].empty()) return false;
    return true;
}

Json Store::toJson() const {
    Json root = Json::object();
    for (int c = 0; c < ConfigCount; ++c) {
        if (values_[c].empty()) continue;
        Json one = Json::object();
        for (Values::const_iterator it = values_[c].begin(); it != values_[c].end(); ++it)
            one.set(it->first, Json::fromText(it->second));
        root.set(configName(static_cast<Configuration>(c)), one);
    }
    return root;
}

void Store::fromJson(const Json& json) {
    for (int c = 0; c < ConfigCount; ++c) {
        values_[c].clear();
        const Json& one = json.get(configName(static_cast<Configuration>(c)));
        for (size_t i = 0; i < one.size(); ++i)
            if (find(one.keyAt(i))) set(static_cast<Configuration>(c), one.keyAt(i), one.valueAt(i).text());
    }
}

bool available(const std::string& id, const std::string& arch, std::string& why) {
    why.clear();
    if (id == "c90.g" || id == "cpp11.g" || id == "shc.g") {
        if (emitsDebugInfo(id == "c90.g" ? ToolCc1 : id == "cpp11.g" ? ToolCxx1 : ToolShc, arch)) return true;
        why = "no line table for " + arch + " (x86_64-linux and arm64-darwin have one)";
        return false;
    }
    if (id == "cpp11.compress" && !isEmulated(arch)) {
        why = "compact instructions are the C6000's; tms6747 only";
        return false;
    }
    // tms6747: a Debug build links shmrt6xd.lib itself, and its session runs through sim6747 (M9);
    // the shalimar compiler links nothing for the C6000, so --debug has nothing to choose there.
    if (id == "shc.debugrt" && isEmulated(arch)) {
        why = "tms6747 takes the Debug runtime from the configuration - a Debug build links shmrt6xd.lib";
        return false;
    }
    return true;
}

static std::vector<std::string> entries(const std::string& list) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at <= list.size()) {
        size_t semi = list.find(';', at);
        std::string one = list.substr(at, semi == std::string::npos ? std::string::npos : semi - at);
        size_t a = one.find_first_not_of(" \t"), b = one.find_last_not_of(" \t");
        if (a != std::string::npos) out.push_back(one.substr(a, b - a + 1));
        if (semi == std::string::npos) break;
        at = semi + 1;
    }
    return out;
}

static std::string quoted(const std::string& s) {
    return s.find_first_of(" \t\"") == std::string::npos ? s : "\"" + s + "\"";
}

std::string flags(const Store& store, ToolchainKind kind, Configuration config, const std::string& arch) {
    std::string why, out;
    const char* tool = kind == ToolCc1 ? "c90" : kind == ToolCxx1 ? "cpp11" : kind == ToolShc ? "shc" : 0;
    if (!tool) return out;
    const std::string t(tool);
    std::string opt = store.value(config, t + ".opt");
    if (opt != "-O0") out += " " + opt;
    if (kind == ToolShc) {
        if (store.value(config, "shc.debugrt") == "1" && available("shc.debugrt", arch, why)) out += " --debug";
        if (store.value(config, "shc.g") == "1" && available("shc.g", arch, why)) out += " -g";
        if (store.value(config, "shc.search") == "0") out += " --no-search";
        if (store.value(config, "general.nologo") == "1") out += " -nologo";
        return out;
    }
    if (store.value(config, t + ".g") == "1" && available(t + ".g", arch, why)) out += " -g";
    if (kind == ToolCxx1 && store.value(config, "cpp11.compress") == "0" && available("cpp11.compress", arch, why))
        out += " --no_compress";
    std::vector<std::string> defs = entries(store.value(config, t + ".defines"));
    for (size_t i = 0; i < defs.size(); ++i) out += " -D" + quoted(defs[i]);
    std::vector<std::string> undefs = entries(store.value(config, t + ".undefines"));
    for (size_t i = 0; i < undefs.size(); ++i) out += " -U" + quoted(undefs[i]);
    if (store.value(config, "general.nologo") == "1") out += " -nologo";
    std::string jobs = store.value(config, "general.jobs");
    if (jobs != "Auto") out += " -j" + jobs;
    if (store.value(config, "general.time") == "1") out += " -time";
    return out;
}

std::map<std::string, std::string> environment(const Store& store, Configuration config) {
    std::map<std::string, std::string> env;
    env["CPP11_DECLINES"] = store.value(config, "cpp11.declines") == "1" ? "1" : "";
    return env;
}

// Pointers, not objects: a native global with a destructor corrupts the window's onexit table
// (settings.cpp, "Pointers, never std::string globals").
static const Store* activeStore = 0;
static Store* installStore = 0;
static const Store* fallbackStore = 0;

Store& installation() {
    if (!installStore) installStore = new Store();
    installStore->fromJson(settings::compilerOptions());
    return *installStore;
}

const Store& active() { return activeStore ? *activeStore : fallbackStore ? *fallbackStore : installation(); }
void setFallback(const Store* store) { fallbackStore = store; }
void setActive(const Store* store) { activeStore = store; }
void release(const Store* store) { if (activeStore == store) activeStore = 0; }

}

}
