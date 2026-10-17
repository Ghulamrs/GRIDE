#ifndef EDITOR_OPTIONS_H
#define EDITOR_OPTIONS_H

#include <cstddef>
#include <map>
#include <string>

#include "json.h"
#include "toolchain.h"

namespace editor {

namespace options {

// **The compiler options a project sets, per configuration**, and the table both windowed front ends
// draw their Compiler Options dialog from: one row per option, the tab it sits on, its control and
// the flag it becomes. What is not set answers its default, and the defaults are the flags RIDE passed before the dialog existed.
// What a menu already decides - the assembler, include paths, libraries, tool paths, the target - is not here.
enum Control { Check, Choice, Text };

struct Def {
    const char* id;        // "cpp11.opt": the tab's compiler, then the option
    const char* tab;       // General, C (c90), C++ (cpp11), Shalimar
    const char* label;
    Control control;
    const char* choices;   // a Choice's values, '|' between them; the first is the compiler's default
    const char* hint;      // a line under the control, or ""
};

size_t count();
const Def& at(size_t index);
const Def* find(const std::string& id);
size_t tabCount();
const char* tabName(size_t tab);

// One configuration's values, id to value; "1"/"0" for a Check, ';' between a Text list's entries.
typedef std::map<std::string, std::string> Values;

class Store {
public:
    std::string value(Configuration config, const std::string& id) const;
    void set(Configuration config, const std::string& id, const std::string& value);
    void reset(Configuration config);
    // Only what differs from the default is written, so an untouched project's file is unchanged.
    Json toJson() const;
    void fromJson(const Json& json);
    bool empty() const;
private:
    Values values_[ConfigCount];
};

std::string defaultValue(Configuration config, const std::string& id);

// Whether an option means anything for this target, and why not when it does not.
bool available(const std::string& id, const std::string& arch, std::string& why);

// The flags a compiler is given for a configuration and target, from a store: what configFlags
// answers for c90, cpp11 and shalimar. MSVC and the host's c++ are not the dialog's.
std::string flags(const Store& store, ToolchainKind kind, Configuration config, const std::string& arch);
// The environment a compile runs under, NAME=value pairs: CPP11_DECLINES where it is asked for.
std::map<std::string, std::string> environment(const Store& store, Configuration config);

// The store builds read: the open project's, else the installation's settings.json one.
const Store& active();
void setActive(const Store* store);   // null: the installation's
void release(const Store* store);     // the installation's again, if this one was active
Store& installation();
// What stands in for the installation's store where no project is open - a suite's own, so the
// machine's settings.json does not decide what it measures. Null: the installation's.
void setFallback(const Store* store);

}

}

#endif
